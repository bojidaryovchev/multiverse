// Copyright Universe Project. All Rights Reserved.

#include "UniversePlayerController.h"

#include "InterstellarTravel.h"
#include "PlanetActor.h"
#include "PlanetCharacter.h"
#include "PlanetSurfaceQuery.h"
#include "StarSystemStreamingSubsystem.h"
#include "UniverseAnchorComponent.h"
#include "UniverseGameState.h"
#include "UniversePlayerState.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"
#include "UniverseWorldSubsystem.h"
#include "WorldStateSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/Pawn.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverseNetPC, Log, All);

namespace
{
    /**
     * The fastest a player can legitimately be moving, per regime.
     *
     * Deliberately generous. The server does not know which travel profile the
     * client is using, whether it was in warp, or how long a frame it just had,
     * so the check is for the *impossible* rather than for the improbable: a
     * proposal that would need faster-than-warp movement, or a jump across the
     * galaxy in one packet.
     *
     * Tightening this is a real piece of future work and is called out in
     * Networking.md rather than pretended away.
     */
    double GetMaxPlausibleSpeedMs(bool bOnFoot)
    {
        // On foot: a sprinting character, with a wide margin for a fall, a
        // slope, or a slightly wrong elapsed time.
        if (bOnFoot)
        {
            return 500.0;
        }

        // In a ship: the warp profile's maximum. Anything beyond this is not a
        // fast ship, it is a client claiming to be somewhere it cannot be.
        return FTravelProfile::MakeDefault().WarpMaxSpeedMs;
    }

    /** The pawn's canonical position, whichever pawn it is. */
    bool TryGetPawnState(
        const APawn* Pawn,
        FUniversePosition& OutPosition,
        FVector3d& OutVelocityMs,
        bool& bOutOnFoot)
    {
        if (const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
        {
            OutPosition = Probe->GetUniversePosition();
            OutVelocityMs = Probe->GetUniverseVelocity();
            bOutOnFoot = false;
            return true;
        }

        if (const APlanetCharacter* OnFootPawn = Cast<APlanetCharacter>(Pawn))
        {
            OutPosition = OnFootPawn->GetUniversePosition();
            OutVelocityMs = FVector3d::ZeroVector;
            bOutOnFoot = true;
            return true;
        }

        return false;
    }
}

AUniversePlayerController::AUniversePlayerController()
{
    PrimaryActorTick.bCanEverTick = true;
}

void AUniversePlayerController::BeginPlay()
{
    Super::BeginPlay();

    // --- World change fan-out -----------------------------------------------
    //
    // Bound on the server only. Each controller filters by its own
    // subscriptions, so the subsystem stays ignorant of connections and the
    // controller stays the only thing that knows what this client asked for.
    if (HasAuthority())
    {
        if (UWorld* World = GetWorld())
        {
            if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
            {
                WorldState->OnEntityCreated.AddUObject(
                    this, &AUniversePlayerController::OnWorldEntityCreated);

                WorldState->OnEntityRemoved.AddUObject(
                    this, &AUniversePlayerController::OnWorldEntityRemoved);
            }
        }
    }

    // --- Player identity ----------------------------------------------------
    //
    // Assigned by the server, once, and stable from then on. Deliberately not
    // derived from the connection: a reconnecting player must be recognised as
    // the same person, and Unreal reuses connection ids.
    if (HasAuthority())
    {
        if (AUniversePlayerState* State = GetUniversePlayerState())
        {
            if (State->GetPersistentId().IsEmpty())
            {
                // A per-session id for this sprint. An account service replaces
                // exactly this line and nothing else - every durable thing in
                // the project keys on the string it returns, not on how the
                // string was made.
                const FString Assigned = FString::Printf(TEXT("player-%s"),
                    *FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower).Left(8));

                State->SetPersistentId(Assigned);

                UE_LOG(LogUniverseNetPC, Log,
                    TEXT("Assigned persistent id %s to connection %d."),
                    *Assigned, State->GetPlayerId());
            }
        }
    }
}

AUniversePlayerState* AUniversePlayerController::GetUniversePlayerState() const
{
    return Cast<AUniversePlayerState>(PlayerState);
}

bool AUniversePlayerController::IsSubscribedTo(const FPersistenceRegionId& RegionId) const
{
    return SubscribedRegions.Contains(RegionId.GetLocalKey());
}

void AUniversePlayerController::OnWorldEntityCreated(const FWorldEntityRecord& Record)
{
    if (!HasAuthority() || !IsSubscribedTo(Record.RegionId))
    {
        return;
    }

    // The owning client already knows - it asked for this - but sending it
    // anyway is correct and simpler than not. The client's apply path replaces
    // a record with the same id rather than appending, so a duplicate is a
    // no-op, and the alternative is a special case that only ever gets tested
    // on the machine that made the change.
    ClientEntityCreated(FNetEntityRecord(Record));
}

void AUniversePlayerController::OnWorldEntityRemoved(const FPersistentEntityId& EntityId)
{
    if (!HasAuthority())
    {
        return;
    }

    // Removals are sent to every subscribed client without a region check: the
    // id does not carry a region, and a client that does not have the region
    // cached ignores it. Filtering would mean looking the entity up in a
    // database that has just deleted it.
    if (SubscribedRegions.Num() == 0)
    {
        return;
    }

    ClientEntityRemoved(FNetEntityId(EntityId));
}

// ---------------------------------------------------------------------------
// Movement
// ---------------------------------------------------------------------------

void AUniversePlayerController::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    const double Dt = static_cast<double>(DeltaSeconds);

    // --- The server keeps its own player state current ----------------------
    //
    // A listen server's local player has no RPC to send to itself, and a
    // dedicated server's own state is whatever the last accepted proposal was.
    // Either way, the authoritative record for a locally controlled pawn is
    // updated here rather than through the network path, so that "where is this
    // player" has one answer in every net mode.
    if (HasAuthority() && IsLocalController())
    {
        if (AUniversePlayerState* State = GetUniversePlayerState())
        {
            FUniversePosition Position;
            FVector3d Velocity;
            bool bOnFoot = false;

            if (TryGetPawnState(GetPawn(), Position, Velocity, bOnFoot))
            {
                uint64 PlanetKey = 0;
                FUniverseSystemId SystemId;

                if (const UWorld* World = GetWorld())
                {
                    if (const UUniverseWorldSubsystem* Universe =
                            World->GetSubsystem<UUniverseWorldSubsystem>())
                    {
                        if (const APlanetActor* Planet = Universe->GetFramePlanet())
                        {
                            PlanetKey = Planet->GetPlanetDescriptor().PlanetKey;
                        }
                    }

                    if (const UStarSystemStreamingSubsystem* Streamer =
                            World->GetSubsystem<UStarSystemStreamingSubsystem>())
                    {
                        FStarSystemDescriptor Active;

                        if (Streamer->GetActiveSystem(Active))
                        {
                            SystemId = Active.Id;
                        }
                    }
                }

                State->SetAuthoritativeState(
                    Position, Velocity, GetControlRotation(), bOnFoot, PlanetKey, SystemId);
            }
        }
    }

    // --- Adopt the spawn the server chose -----------------------------------
    //
    // Before this, the client's pawn is wherever it was constructed - the
    // universe origin, which is intergalactic space. See bAdoptedSpawnPosition.
    if (IsLocalController() && !HasAuthority() && !bAdoptedSpawnPosition)
    {
        const AUniversePlayerState* State = GetUniversePlayerState();

        if (State != nullptr && State->HasAuthoritativePosition())
        {
            if (AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(GetPawn()))
            {
                if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
                {
                    Anchor->SetUniversePosition(State->GetUniversePosition());
                    Probe->FullStop();

                    bAdoptedSpawnPosition = true;

                    UE_LOG(LogUniverseNetPC, Log,
                        TEXT("Spawned at the server's position: %s"),
                        *State->GetUniversePosition().ToDebugString());
                }
            }
        }
    }

    if (IsLocalController() && !HasAuthority())
    {
        TimeSinceUpdate += Dt;

        if (TimeSinceUpdate >= PositionUpdateIntervalSeconds)
        {
            TimeSinceUpdate = 0.0;
            SendPositionUpdate();
        }
    }
}

void AUniversePlayerController::SendPositionUpdate()
{
    FUniversePosition Position;
    FVector3d Velocity;
    bool bOnFoot = false;

    if (!TryGetPawnState(GetPawn(), Position, Velocity, bOnFoot))
    {
        return;
    }

    uint64 PlanetKey = 0;
    FReplicatedSystemId SystemId;

    if (const UWorld* World = GetWorld())
    {
        if (const UUniverseWorldSubsystem* Universe = World->GetSubsystem<UUniverseWorldSubsystem>())
        {
            if (const APlanetActor* Planet = Universe->GetFramePlanet())
            {
                PlanetKey = Planet->GetPlanetDescriptor().PlanetKey;
            }
        }

        if (const UStarSystemStreamingSubsystem* Streamer =
                World->GetSubsystem<UStarSystemStreamingSubsystem>())
        {
            FStarSystemDescriptor Active;

            if (Streamer->GetActiveSystem(Active))
            {
                SystemId.Set(Active.Id);
            }
        }
    }

    double ClientTime = 0.0;

    if (const AUniverseGameState* GameState = GetWorld()->GetGameState<AUniverseGameState>())
    {
        ClientTime = GameState->GetUniverseTimeSeconds();
    }

    ++SentMoveCount;

    ServerUpdatePosition(
        FReplicatedUniversePosition(Position),
        FVector_NetQuantize100(Velocity.X, Velocity.Y, Velocity.Z),
        GetControlRotation(),
        bOnFoot,
        PlanetKey,
        SystemId,
        ClientTime);
}

bool AUniversePlayerController::ServerUpdatePosition_Validate(
    const FReplicatedUniversePosition& Position,
    FVector_NetQuantize100 VelocityMs,
    FRotator Orientation,
    bool bOnFoot,
    uint64 PlanetKey,
    const FReplicatedSystemId& SystemId,
    double ClientTimeSeconds)
{
    // Structural validation only - the plausibility check needs the previous
    // state and belongs in the implementation. What is rejected here is a
    // packet that could not have come from an honest client at all.
    return FMath::IsFinite(Position.LocalX)
        && FMath::IsFinite(Position.LocalY)
        && FMath::IsFinite(Position.LocalZ)
        && FMath::IsFinite(ClientTimeSeconds);
}

void AUniversePlayerController::ServerUpdatePosition_Implementation(
    const FReplicatedUniversePosition& Position,
    FVector_NetQuantize100 VelocityMs,
    FRotator Orientation,
    bool bOnFoot,
    uint64 PlanetKey,
    const FReplicatedSystemId& SystemId,
    double ClientTimeSeconds)
{
    AUniversePlayerState* State = GetUniversePlayerState();
    const UWorld* World = GetWorld();

    if (State == nullptr || World == nullptr)
    {
        return;
    }

    const double Now = World->GetTimeSeconds();
    const double Elapsed = bHaveAcceptedAnyMove
        ? FMath::Max(Now - LastAcceptedServerTime, 0.0)
        : 0.0;

    const FUniversePosition Proposed = Position.Get();

    FString Reason;

    if (bHaveAcceptedAnyMove && !ValidateProposedMove(Proposed, Elapsed, bOnFoot, Reason))
    {
        State->RecordRejectedMove();

        UE_LOG(LogUniverseNetPC, Warning,
            TEXT("Rejected move from %s: %s"), *State->GetPersistentId(), *Reason);

        // Corrected to the last position the server believed, not to nothing.
        // A client that has genuinely desynchronised needs somewhere to be.
        ClientCorrectPosition(
            FReplicatedUniversePosition(State->GetUniversePosition()), Reason);

        return;
    }

    State->SetAuthoritativeState(
        Proposed,
        FVector3d(VelocityMs.X, VelocityMs.Y, VelocityMs.Z),
        Orientation,
        bOnFoot,
        PlanetKey,
        SystemId.Get());

    LastAcceptedServerTime = Now;
    bHaveAcceptedAnyMove = true;
}

bool AUniversePlayerController::ValidateProposedMove(
    const FUniversePosition& Proposed,
    double ElapsedSeconds,
    bool bOnFoot,
    FString& OutReason) const
{
    const AUniversePlayerState* State = GetUniversePlayerState();

    if (State == nullptr)
    {
        OutReason = TEXT("no player state");
        return false;
    }

    if (ElapsedSeconds <= 0.0)
    {
        // Two proposals in the same server frame. Not suspicious - a burst of
        // packets does this - and there is no interval to measure a speed over,
        // so it is accepted rather than judged on no evidence.
        return true;
    }

    const double DistanceMeters =
        FUniversePosition::DistanceMeters(State->GetUniversePosition(), Proposed);

    if (!FMath::IsFinite(DistanceMeters))
    {
        OutReason = TEXT("proposed position is not finite");
        return false;
    }

    const double MaxSpeed = GetMaxPlausibleSpeedMs(bOnFoot);
    const double Allowed = MaxSpeed * ElapsedSeconds * MovementToleranceFactor;

    if (DistanceMeters > Allowed)
    {
        OutReason = FString::Printf(
            TEXT("moved %s in %.3f s; the limit %s allows %s"),
            *FInterstellarTravel::FormatDistance(DistanceMeters),
            ElapsedSeconds,
            *FInterstellarTravel::FormatSpeed(MaxSpeed),
            *FInterstellarTravel::FormatDistance(Allowed));

        return false;
    }

    return true;
}

void AUniversePlayerController::ClientCorrectPosition_Implementation(
    const FReplicatedUniversePosition& Position, const FString& Reason)
{
    ++CorrectionCount;
    LastCorrectionReason = Reason;

    UE_LOG(LogUniverseNetPC, Warning,
        TEXT("Server corrected our position: %s"), *Reason);

    const FUniversePosition Corrected = Position.Get();

    if (AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(GetPawn()))
    {
        if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
        {
            Anchor->SetUniversePosition(Corrected);
            Probe->FullStop();
        }

        return;
    }

    if (APlanetCharacter* OnFootPawn = Cast<APlanetCharacter>(GetPawn()))
    {
        if (UUniverseAnchorComponent* Anchor = OnFootPawn->GetAnchor())
        {
            Anchor->SetUniversePosition(Corrected);
        }
    }
}

// ---------------------------------------------------------------------------
// World edits
// ---------------------------------------------------------------------------

bool AUniversePlayerController::ServerRequestBuild_Validate(
    const FString& TypeId, const FNetPlacement& Placement, uint64 PlanetKey)
{
    return !TypeId.IsEmpty() && TypeId.Len() < 128 && PlanetKey != 0;
}

void AUniversePlayerController::ServerRequestBuild_Implementation(
    const FString& TypeId, const FNetPlacement& Placement, uint64 PlanetKey)
{
    UWorld* World = GetWorld();
    AUniversePlayerState* State = GetUniversePlayerState();

    if (World == nullptr || State == nullptr)
    {
        return;
    }

    UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>();

    if (WorldState == nullptr || !WorldState->IsOpen())
    {
        UE_LOG(LogUniverseNetPC, Warning,
            TEXT("Build refused: world persistence is unavailable on the server."));
        return;
    }

    // --- The planet must be one the server actually has ---------------------
    //
    // Not "one the client says it has". The server re-derives everything from
    // the planet key, so a client naming a planet that is not streamed here
    // gets a refusal rather than a row in the database describing a place
    // nobody can check.
    const APlanetActor* Planet = nullptr;

    if (const UUniverseWorldSubsystem* Universe = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        if (const APlanetActor* Candidate = Universe->GetFramePlanet())
        {
            if (Candidate->GetPlanetDescriptor().PlanetKey == PlanetKey)
            {
                Planet = Candidate;
            }
        }
    }

    if (Planet == nullptr)
    {
        if (const UStarSystemStreamingSubsystem* Streamer =
                World->GetSubsystem<UStarSystemStreamingSubsystem>())
        {
            if (const APlanetActor* Candidate = Streamer->GetActivePlanetActor())
            {
                if (Candidate->GetPlanetDescriptor().PlanetKey == PlanetKey)
                {
                    Planet = Candidate;
                }
            }
        }
    }

    if (Planet == nullptr)
    {
        UE_LOG(LogUniverseNetPC, Warning,
            TEXT("Build refused: planet 0x%016llX is not active on the server."),
            static_cast<unsigned long long>(PlanetKey));
        return;
    }

    const FPersistentPlacement Real = Placement.Get();

    if (!Real.IsValid())
    {
        UE_LOG(LogUniverseNetPC, Warning, TEXT("Build refused: degenerate placement."));
        return;
    }

    // --- Range ---------------------------------------------------------------
    //
    // A player may only build where they are. Without this a client could place
    // a structure anywhere on any planet it knows the key of, which is a
    // world-editing primitive rather than a game action.
    //
    // Measured *tangentially*: the build point is taken at the player's own
    // distance from the planet centre, so what is compared is how far along the
    // surface the two are, not how high up the player is.
    //
    // The first version put the build point at sea level and refused a player
    // standing on a mountain - "11.2 km from the player, limit 1000 m", from
    // somebody standing exactly on the spot, because the mountain was 11,174 m
    // tall. Elevation is not distance from the thing you are building.
    const FVector3d PlayerLocal =
        Planet->UniverseToPlanetLocalMeters(State->GetUniversePosition());

    const double PlayerRadius = PlayerLocal.Size();

    const FUniversePosition BuildPosition = Planet->PlanetLocalMetersToUniverse(
        Real.Direction * FMath::Max(PlayerRadius, 1.0));

    const double RangeMeters =
        FUniversePosition::DistanceMeters(State->GetUniversePosition(), BuildPosition);

    // Generous: the direction is quantised on the wire and the player's own
    // position is a tenth of a second stale. A kilometre refuses remote
    // world-editing while never refusing somebody standing on the spot.
    constexpr double MaxBuildRangeMeters = 1000.0;

    if (RangeMeters > MaxBuildRangeMeters)
    {
        UE_LOG(LogUniverseNetPC, Warning,
            TEXT("Build refused: %s from the player, limit %.0f m."),
            *FInterstellarTravel::FormatDistance(RangeMeters), MaxBuildRangeMeters);
        return;
    }

    const FPersistentEntityId Created = WorldState->CreateEntity(Planet, TypeId, Real);

    if (!Created.IsValid())
    {
        UE_LOG(LogUniverseNetPC, Warning, TEXT("Build refused: the store rejected it."));
        return;
    }

    UE_LOG(LogUniverseNetPC, Log,
        TEXT("%s built %s at %s."),
        *State->GetPersistentId(), *TypeId, *Created.ToHexString());
}

bool AUniversePlayerController::ServerRequestRemoveProcedural_Validate(
    const FNetEntityId& EntityId, FVector_NetQuantize100 PlanetLocalMeters, uint64 PlanetKey)
{
    return EntityId.Get().IsValid() && PlanetKey != 0;
}

void AUniversePlayerController::ServerRequestRemoveProcedural_Implementation(
    const FNetEntityId& EntityId, FVector_NetQuantize100 PlanetLocalMeters, uint64 PlanetKey)
{
    UWorld* World = GetWorld();
    AUniversePlayerState* State = GetUniversePlayerState();

    if (World == nullptr || State == nullptr)
    {
        return;
    }

    UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>();

    if (WorldState == nullptr || !WorldState->IsOpen())
    {
        return;
    }

    const APlanetActor* Planet = nullptr;

    if (const UUniverseWorldSubsystem* Universe = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        if (const APlanetActor* Candidate = Universe->GetFramePlanet())
        {
            if (Candidate->GetPlanetDescriptor().PlanetKey == PlanetKey)
            {
                Planet = Candidate;
            }
        }
    }

    if (Planet == nullptr)
    {
        UE_LOG(LogUniverseNetPC, Warning,
            TEXT("Removal refused: planet 0x%016llX is not active on the server."),
            static_cast<unsigned long long>(PlanetKey));
        return;
    }

    const FVector3d Local(PlanetLocalMeters.X, PlanetLocalMeters.Y, PlanetLocalMeters.Z);

    // Tangential, for the same reason as building: a player on a hilltop is not
    // far from a tree at its foot.
    const FVector3d PlayerLocal =
        Planet->UniverseToPlanetLocalMeters(State->GetUniversePosition());

    const FVector3d LocalDirection = Local.GetSafeNormal();

    const FUniversePosition Where = LocalDirection.IsNearlyZero()
        ? Planet->PlanetLocalMetersToUniverse(Local)
        : Planet->PlanetLocalMetersToUniverse(
              LocalDirection * FMath::Max(PlayerLocal.Size(), 1.0));

    const double RangeMeters =
        FUniversePosition::DistanceMeters(State->GetUniversePosition(), Where);

    // The same reasoning as building, with a wider limit because a tree is
    // removed at a distance rather than underfoot.
    constexpr double MaxRemoveRangeMeters = 2000.0;

    if (RangeMeters > MaxRemoveRangeMeters)
    {
        UE_LOG(LogUniverseNetPC, Warning,
            TEXT("Removal refused: %s from the player, limit %.0f m."),
            *FInterstellarTravel::FormatDistance(RangeMeters), MaxRemoveRangeMeters);
        return;
    }

    if (WorldState->RemoveProceduralEntity(Planet, EntityId.Get(), Local))
    {
        UE_LOG(LogUniverseNetPC, Log,
            TEXT("%s removed %s."), *State->GetPersistentId(), *EntityId.Get().ToHexString());
    }
}

bool AUniversePlayerController::ServerRequestDemolish_Validate(
    const FNetEntityId& EntityId, uint64 PlanetKey)
{
    return EntityId.Get().IsValid();
}

void AUniversePlayerController::ServerRequestDemolish_Implementation(
    const FNetEntityId& EntityId, uint64 PlanetKey)
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>();

    if (WorldState == nullptr || !WorldState->IsOpen())
    {
        return;
    }

    if (WorldState->DestroyCreatedEntity(EntityId.Get()))
    {
        UE_LOG(LogUniverseNetPC, Log, TEXT("Demolished %s."), *EntityId.Get().ToHexString());
    }
}

// ---------------------------------------------------------------------------
// Region subscription
// ---------------------------------------------------------------------------

bool AUniversePlayerController::ServerSubscribeRegion_Validate(const FNetRegionId& RegionId)
{
    return RegionId.IsValid();
}

void AUniversePlayerController::ServerSubscribeRegion_Implementation(const FNetRegionId& RegionId)
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    const FPersistenceRegionId Real = RegionId.Get();
    const uint64 Key = Real.GetLocalKey();

    if (SubscribedRegions.Contains(Key))
    {
        return;
    }

    if (SubscribedRegions.Num() >= MaxSubscribedRegions)
    {
        // Bounded per client. A client that asks for the whole planet gets the
        // first sixty-four regions and a warning, rather than a server that
        // loads a planet's worth of rows on request.
        UE_LOG(LogUniverseNetPC, Warning,
            TEXT("Subscription refused: %d regions already held (limit %d)."),
            SubscribedRegions.Num(), MaxSubscribedRegions);
        return;
    }

    SubscribedRegions.Add(Key);

    UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>();

    if (WorldState == nullptr || !WorldState->IsOpen())
    {
        return;
    }

    // Loaded synchronously, because the client is waiting for it before it can
    // decide which trees to draw, and a region is a handful of rows.
    WorldState->LoadRegionBlocking(Real);

    if (const FWorldRegionDelta* Delta = WorldState->FindLoadedRegion(Real))
    {
        ClientReceiveRegionDelta(FNetRegionDelta(*Delta));
    }
    else
    {
        // An empty region is still an answer, and the client needs it: without
        // one it cannot tell "nothing here" from "not told yet", and the safe
        // reading of the second is to draw nothing.
        FWorldRegionDelta Empty;
        Empty.RegionId = Real;
        Empty.bLoaded = true;

        ClientReceiveRegionDelta(FNetRegionDelta(Empty));
    }
}

bool AUniversePlayerController::ServerUnsubscribeRegion_Validate(const FNetRegionId& RegionId)
{
    return true;
}

void AUniversePlayerController::ServerUnsubscribeRegion_Implementation(const FNetRegionId& RegionId)
{
    SubscribedRegions.Remove(RegionId.Get().GetLocalKey());
}

void AUniversePlayerController::ClientReceiveRegionDelta_Implementation(const FNetRegionDelta& Delta)
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
    {
        WorldState->ApplyReplicatedRegion(Delta.Get());
    }
}

void AUniversePlayerController::ClientEntityCreated_Implementation(const FNetEntityRecord& Record)
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
    {
        WorldState->ApplyReplicatedRecord(Record.Get());
    }
}

void AUniversePlayerController::ClientEntityRemoved_Implementation(const FNetEntityId& EntityId)
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
    {
        WorldState->ApplyReplicatedRemoval(EntityId.Get());
    }
}
