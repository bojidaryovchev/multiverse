// Copyright Universe Project. All Rights Reserved.

#include "GoldenPathSubsystem.h"

#include "InterstellarTravel.h"
#include "PlanetActor.h"
#include "PlanetCharacter.h"
#include "PlanetStructureComponent.h"
#include "StarSystemStreamingSubsystem.h"
#include "UniverseAnchorComponent.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"
#include "UniverseWorldSubsystem.h"
#include "WorldStateSubsystem.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogGoldenPath, Log, All);

namespace
{
    /** Where the record of a run is kept, so a later process can check it. */
    const TCHAR* OutcomeKey = TEXT("goldenpath.outcome");
}

const TCHAR* LexToString(EGoldenPathStage Stage)
{
    switch (Stage)
    {
    case EGoldenPathStage::Idle:      return TEXT("Idle");
    case EGoldenPathStage::Waking:    return TEXT("Waking");
    case EGoldenPathStage::Flying:    return TEXT("Flying");
    case EGoldenPathStage::Warping:   return TEXT("Warping");
    case EGoldenPathStage::Arrived:   return TEXT("Arrived");
    case EGoldenPathStage::Landing:   return TEXT("Landing");
    case EGoldenPathStage::Landed:    return TEXT("Landed");
    case EGoldenPathStage::OnFoot:    return TEXT("OnFoot");
    case EGoldenPathStage::Working:   return TEXT("Working");
    case EGoldenPathStage::Aboard:    return TEXT("Aboard");
    case EGoldenPathStage::Leaving:   return TEXT("Leaving");
    case EGoldenPathStage::Recording: return TEXT("Recording");
    case EGoldenPathStage::Complete:  return TEXT("Complete");
    case EGoldenPathStage::Failed:    return TEXT("Failed");
    default:                          return TEXT("?");
    }
}

TStatId UGoldenPathSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UGoldenPathSubsystem, STATGROUP_Tickables);
}

bool UGoldenPathSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

AUniverseProbePawn* UGoldenPathSubsystem::GetProbe() const
{
    return Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
}

APlanetCharacter* UGoldenPathSubsystem::GetCharacter() const
{
    return Cast<APlanetCharacter>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
}

UStarSystemStreamingSubsystem* UGoldenPathSubsystem::GetStreamer() const
{
    UWorld* World = GetWorld();

    return (World != nullptr) ? World->GetSubsystem<UStarSystemStreamingSubsystem>() : nullptr;
}

void UGoldenPathSubsystem::Begin()
{
    bVerifyOnly = false;
    TotalSeconds = 0.0;
    RemovedCount = 0;
    WarpDistanceLightYears = 0.0;
    WarpSeconds = 0.0;
    FailureReason.Reset();
    Warnings.Reset();
    BuiltId = FPersistentEntityId();

    UE_LOG(LogGoldenPath, Log, TEXT("=== Golden Path ==="));
    UE_LOG(LogGoldenPath, Log,
        TEXT("fly -> warp -> arrive -> land -> disembark -> build -> clear -> board -> leave"));

    EnterStage(EGoldenPathStage::Waking, 45.0);
}

void UGoldenPathSubsystem::BeginVerify()
{
    bVerifyOnly = true;
    TotalSeconds = 0.0;
    FailureReason.Reset();
    Warnings.Reset();

    UE_LOG(LogGoldenPath, Log, TEXT("=== Golden Path: verifying a previous run ==="));

    if (!LoadOutcome())
    {
        UE_LOG(LogGoldenPath, Error,
            TEXT("No previous Golden Path run recorded in this world. Run universe.GoldenPath first."));

        Stage = EGoldenPathStage::Failed;
        FailureReason = TEXT("nothing recorded to verify");
        Report();
        return;
    }

    UE_LOG(LogGoldenPath, Log,
        TEXT("Previous run: built %s in region %s on planet 0x%016llX, removed %d procedural entities."),
        *BuiltId.ToHexString(), *BuiltRegion.ToString(),
        static_cast<unsigned long long>(PlanetKey), RemovedCount);

    EnterStage(EGoldenPathStage::Waking, 60.0);
}

void UGoldenPathSubsystem::Abort(const FString& Reason)
{
    if (Stage == EGoldenPathStage::Idle)
    {
        return;
    }

    UE_LOG(LogGoldenPath, Warning, TEXT("Golden Path aborted: %s"), *Reason);
    Stage = EGoldenPathStage::Idle;
}

void UGoldenPathSubsystem::EnterStage(EGoldenPathStage NewStage, double DeadlineSeconds)
{
    Stage = NewStage;
    TimeInStageSeconds = 0.0;
    StageDeadlineSeconds = DeadlineSeconds;

    UE_LOG(LogGoldenPath, Log,
        TEXT("[%6.1fs] -> %s"), TotalSeconds, LexToString(NewStage));
}

void UGoldenPathSubsystem::Fail(const FString& Reason)
{
    FailureReason = Reason;
    Stage = EGoldenPathStage::Failed;

    UE_LOG(LogGoldenPath, Error, TEXT("FAILED: %s"), *Reason);

    Report();
}

// ---------------------------------------------------------------------------
// The run
// ---------------------------------------------------------------------------

void UGoldenPathSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (Stage == EGoldenPathStage::Idle
        || Stage == EGoldenPathStage::Complete
        || Stage == EGoldenPathStage::Failed)
    {
        return;
    }

    const double Dt = static_cast<double>(DeltaTime);
    TimeInStageSeconds += Dt;
    TotalSeconds += Dt;

    UWorld* World = GetWorld();
    UStarSystemStreamingSubsystem* Streamer = GetStreamer();

    if (World == nullptr || Streamer == nullptr)
    {
        Fail(TEXT("the world or the streamer went away"));
        return;
    }

    if (StageDeadlineSeconds > 0.0 && TimeInStageSeconds > StageDeadlineSeconds)
    {
        Fail(FString::Printf(TEXT("stage %s exceeded its %.0f s deadline"),
            LexToString(Stage), StageDeadlineSeconds));
        return;
    }

    switch (Stage)
    {
    // --- Waking -------------------------------------------------------------
    case EGoldenPathStage::Waking:
    {
        FStarSystemDescriptor Active;

        if (!Streamer->GetActiveSystem(Active))
        {
            return;
        }

        if (Streamer->GetActivePlanetActor() == nullptr)
        {
            return;
        }

        HomeSystemId = Active.Id;
        HomeSystemName = Active.Name;

        UE_LOG(LogGoldenPath, Log,
            TEXT("  Spawned in %s, with %s built."),
            *HomeSystemName, *Streamer->GetActivePlanetActor()->GetName());

        // The verify run does not travel: it lands where the last one built and
        // checks the world is as it left it. Travelling again would be testing
        // travel a second time and persistence not at all.
        EnterStage(bVerifyOnly ? EGoldenPathStage::Landing : EGoldenPathStage::Flying, 60.0);
        return;
    }

    // --- Flying under normal thrust ------------------------------------------
    case EGoldenPathStage::Flying:
    {
        AUniverseProbePawn* Probe = GetProbe();

        if (Probe == nullptr)
        {
            Fail(TEXT("no ship"));
            return;
        }

        // A short burn, to prove ordinary flight moves the canonical position
        // before anything exotic happens.
        if (TimeInStageSeconds < 3.0)
        {
            Probe->SetUniverseVelocity(FVector3d(1.0e6, 0.0, 0.0));
            return;
        }

        Probe->FullStop();

        // Pick somewhere else to be.
        const TArray<FStreamedSystem>& Systems = Streamer->GetTrackedSystems();

        const FStreamedSystem* Chosen = nullptr;

        for (const FStreamedSystem& Candidate : Systems)
        {
            if (!(Candidate.Id == HomeSystemId) && Candidate.bHasDescriptor)
            {
                Chosen = &Candidate;
                break;
            }
        }

        if (Chosen == nullptr)
        {
            return;
        }

        DestinationSystemId = Chosen->Id;
        DestinationSystemName = Chosen->Descriptor.Name;
        DestinationContentHash = Chosen->Descriptor.GetContentHash();
        WarpDistanceLightYears = Chosen->DistanceLightYears;

        if (!Streamer->SetTargetSystem(DestinationSystemId))
        {
            Fail(TEXT("could not target the destination"));
            return;
        }

        Probe->SetAutoBrake(true);
        Probe->SetAutoSteer(true);

        if (!Probe->SetWarpEngaged(true))
        {
            Fail(TEXT("could not engage warp"));
            return;
        }

        UE_LOG(LogGoldenPath, Log,
            TEXT("  Warping to %s, %.3f ly."), *DestinationSystemName, WarpDistanceLightYears);

        WarpSeconds = 0.0;

        const double Estimate = FInterstellarTravel::EstimateTravelTimeSeconds(
            Probe->TravelProfile,
            WarpDistanceLightYears * UniverseScale::MetersPerLightYear,
            EUniverseTravelMode::Warp);

        EnterStage(EGoldenPathStage::Warping, FMath::Max(Estimate * 4.0, 60.0));
        return;
    }

    // --- Warping -------------------------------------------------------------
    case EGoldenPathStage::Warping:
    {
        AUniverseProbePawn* Probe = GetProbe();

        if (Probe == nullptr)
        {
            Fail(TEXT("lost the ship in warp"));
            return;
        }

        WarpSeconds += Dt;

        if (Probe->IsWarpEngaged())
        {
            return;
        }

        UE_LOG(LogGoldenPath, Log,
            TEXT("  Arrived after %.1f s."), WarpSeconds);

        EnterStage(EGoldenPathStage::Arrived, 45.0);
        return;
    }

    // --- Arrived -------------------------------------------------------------
    case EGoldenPathStage::Arrived:
    {
        FStarSystemDescriptor Active;

        if (!Streamer->GetActiveSystem(Active))
        {
            return;
        }

        if (!(Active.Id == DestinationSystemId))
        {
            Fail(FString::Printf(TEXT("arrived at %s, expected %s"),
                *Active.Name, *DestinationSystemName));
            return;
        }

        if (Active.GetContentHash() != DestinationContentHash)
        {
            Fail(TEXT("the destination regenerated differently on arrival"));
            return;
        }

        if (Streamer->GetActivePlanetActor() == nullptr)
        {
            return;
        }

        UE_LOG(LogGoldenPath, Log,
            TEXT("  %s is here and identical to what was predicted before departure."),
            *DestinationSystemName);

        EnterStage(EGoldenPathStage::Landing, 90.0);
        return;
    }

    // --- Landing -------------------------------------------------------------
    case EGoldenPathStage::Landing:
    {
        AUniverseProbePawn* Probe = GetProbe();
        APlanetActor* Planet = Streamer->GetActivePlanetActor();

        if (Probe == nullptr || Planet == nullptr)
        {
            return;
        }

        if (!Probe->IsLanded())
        {
            // The same call the L key makes. A run that used a private path
            // would not be testing what a player does.
            if (TimeInStageSeconds > 1.0 && !Probe->TryBeginLanding())
            {
                return;
            }

            return;
        }

        // On a verify run the planet key already holds the *recorded* one, and
        // the report is about that world rather than whichever one this run
        // happens to be standing on.
        if (!bVerifyOnly)
        {
            PlanetKey = Planet->GetPlanetDescriptor().PlanetKey;
        }

        PlanetName = Planet->GetName();

        UE_LOG(LogGoldenPath, Log,
            TEXT("  Landed on planet 0x%016llX."),
            static_cast<unsigned long long>(PlanetKey));

        EnterStage(EGoldenPathStage::Landed, 30.0);
        return;
    }

    // --- Landed, stepping out -------------------------------------------------
    case EGoldenPathStage::Landed:
    {
        AUniverseProbePawn* Probe = GetProbe();

        if (Probe == nullptr)
        {
            // Already on foot: the exit succeeded and possession changed.
            if (GetCharacter() != nullptr)
            {
                EnterStage(EGoldenPathStage::OnFoot, 45.0);
            }

            return;
        }

        // A moment on the ground first, so the terrain under the exit point has
        // a chance to exist before somebody stands on it.
        if (TimeInStageSeconds < 2.0)
        {
            return;
        }

        if (!Probe->TryExitToSurface())
        {
            return;
        }

        EnterStage(EGoldenPathStage::OnFoot, 45.0);
        return;
    }

    // --- On foot --------------------------------------------------------------
    case EGoldenPathStage::OnFoot:
    {
        APlanetCharacter* Character = GetCharacter();

        if (Character == nullptr)
        {
            return;
        }

        if (Character->IsWaitingForCollision())
        {
            return;
        }

        // Walk a little, so the run exercises movement on a sphere rather than
        // only standing on one.
        Character->AddMovementInput(Character->GetActorForwardVector(), 1.0f);

        if (TimeInStageSeconds < 3.0)
        {
            return;
        }

        EnterStage(EGoldenPathStage::Working, 45.0);
        return;
    }

    // --- Building and clearing -------------------------------------------------
    case EGoldenPathStage::Working:
    {
        UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>();

        if (WorldState == nullptr || !WorldState->IsUsable())
        {
            Fail(TEXT("world persistence is unavailable"));
            return;
        }

        if (bVerifyOnly)
        {
            // Nothing is built on a verify run. What is checked is that what a
            // previous *process* built is still here - which is the only form
            // of the question worth asking.
            const FWorldRegionDelta* Delta = WorldState->FindLoadedRegion(BuiltRegion);

            if (Delta == nullptr)
            {
                WorldState->RequestRegion(BuiltRegion);
                return;
            }

            const bool bFound = Delta->Created.ContainsByPredicate(
                [this](const FWorldEntityRecord& Record)
                {
                    return Record.EntityId == BuiltId;
                });

            if (!bFound)
            {
                Fail(FString::Printf(
                    TEXT("the structure built by the previous run (%s) is not in region %s"),
                    *BuiltId.ToHexString(), *BuiltRegion.ToString()));
                return;
            }

            UE_LOG(LogGoldenPath, Log,
                TEXT("  The structure from the previous run is still there: %s"),
                *BuiltId.ToHexString());

            if (RemovedCount > 0 && Delta->Removed.Num() == 0)
            {
                Warnings.Add(TEXT("the previous run's removals are not in this region"));
            }

            Stage = EGoldenPathStage::Complete;
            Report();
            return;
        }

        // --- Build, once ----------------------------------------------------
        if (!BuiltId.IsValid())
        {
            const int32 Before = WorldState->GetCreatedEntityCount();

            // The same command the B key runs.
            GEngine->Exec(World, TEXT("universe.Build beacon"));

            if (WorldState->GetCreatedEntityCount() <= Before)
            {
                // Not a failure yet - the region may still be loading.
                return;
            }

            // Find what was just created, so the verify run has a name to
            // check rather than a count to trust.
            APlanetCharacter* Character = GetCharacter();
            APlanetActor* Planet = Streamer->GetActivePlanetActor();

            if (Character != nullptr && Planet != nullptr)
            {
                const FVector3d Local =
                    Planet->UniverseToPlanetLocalMeters(Character->GetUniversePosition());

                BuiltRegion = FPersistenceRegionId::FromPlanetLocal(
                    Planet->GetPlanetDescriptor(), Local);

                if (const FWorldRegionDelta* Delta = WorldState->FindLoadedRegion(BuiltRegion))
                {
                    if (Delta->Created.Num() > 0)
                    {
                        BuiltId = Delta->Created.Last().EntityId;
                    }
                }
            }

            if (!BuiltId.IsValid())
            {
                Fail(TEXT("built something but could not find what"));
                return;
            }

            UE_LOG(LogGoldenPath, Log,
                TEXT("  Built %s in region %s."), *BuiltId.ToHexString(), *BuiltRegion.ToString());

            return;
        }

        // --- Clear, once ----------------------------------------------------
        if (RemovedCount == 0)
        {
            const int32 Before = WorldState->GetRemovedEntityCount();

            GEngine->Exec(World, TEXT("universe.ChopTree"));

            RemovedCount = WorldState->GetRemovedEntityCount() - Before;

            if (RemovedCount <= 0)
            {
                // A landing spot with no vegetation within range is a legitimate
                // outcome - the last run landed on an 11 km peak - and not a
                // reason to fail a session that has otherwise worked.
                Warnings.Add(TEXT("nothing removable within range of the landing site"));
                RemovedCount = 0;
            }
            else
            {
                UE_LOG(LogGoldenPath, Log,
                    TEXT("  Removed %d procedural entities."), RemovedCount);
            }
        }

        EnterStage(EGoldenPathStage::Aboard, 45.0);
        return;
    }

    // --- Back aboard -----------------------------------------------------------
    case EGoldenPathStage::Aboard:
    {
        if (GetProbe() != nullptr)
        {
            EnterStage(EGoldenPathStage::Leaving, 60.0);
            return;
        }

        APlanetCharacter* Character = GetCharacter();

        if (Character == nullptr)
        {
            Fail(TEXT("neither in the ship nor on foot"));
            return;
        }

        // Walk back to the ship, then board. Both are what a player does.
        const AUniverseProbePawn* Ship = Character->GetShipToReenter();

        if (Ship == nullptr)
        {
            Fail(TEXT("no ship to return to"));
            return;
        }

        const FVector3d ToShip = FUniversePosition::DirectionUnit(
            Character->GetUniversePosition(), Ship->GetUniversePosition());

        if (!ToShip.IsNearlyZero())
        {
            Character->AddMovementInput(FVector(ToShip.X, ToShip.Y, ToShip.Z), 1.0f);
        }

        Character->TryEnterShip();
        return;
    }

    // --- Leaving ---------------------------------------------------------------
    case EGoldenPathStage::Leaving:
    {
        AUniverseProbePawn* Probe = GetProbe();

        if (Probe == nullptr)
        {
            Fail(TEXT("lost the ship on departure"));
            return;
        }

        const UUniverseWorldSubsystem* Universe = World->GetSubsystem<UUniverseWorldSubsystem>();

        // Climb away until the planetary frame releases, which is the same
        // condition Sprint 003's journey uses and means the same thing: the
        // simulation has genuinely let go of the planet.
        //
        // Straight up, taken from the planet's own frame rather than from a
        // world axis - there is no "up" in this project that is not relative to
        // a body.
        if (const APlanetActor* Planet = Streamer->GetActivePlanetActor())
        {
            const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Probe->GetUniversePosition());
            const FVector3d Up = Local.GetSafeNormal();

            if (!Up.IsNearlyZero())
            {
                Probe->SetUniverseVelocity(Up * 2.0e6);
            }
        }

        if (Universe != nullptr && Universe->IsInPlanetaryFrame())
        {
            return;
        }

        Probe->FullStop();

        UE_LOG(LogGoldenPath, Log, TEXT("  Left the planet."));

        EnterStage(EGoldenPathStage::Recording, 15.0);
        return;
    }

    // --- Recording -------------------------------------------------------------
    case EGoldenPathStage::Recording:
    {
        RecordOutcome();

        Stage = EGoldenPathStage::Complete;
        Report();
        return;
    }

    default:
        return;
    }
}

// ---------------------------------------------------------------------------
// Recording, so a second process can check
// ---------------------------------------------------------------------------

void UGoldenPathSubsystem::RecordOutcome()
{
    UWorld* World = GetWorld();

    UWorldStateSubsystem* WorldState =
        (World != nullptr) ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (WorldState == nullptr || !WorldState->IsUsable())
    {
        Warnings.Add(TEXT("could not record the outcome; a verify run will have nothing to check"));
        return;
    }

    // Written into the world's own database rather than a side file, so that a
    // verify run cannot accidentally check a record belonging to a different
    // universe: the record and the world it describes are the same file.
    const FString Encoded = FString::Printf(
        TEXT("%s|%llu|%hhu|%hhu|%u|%u|%d|%llu"),
        *BuiltId.ToHexString(),
        static_cast<unsigned long long>(BuiltRegion.PlanetKey),
        BuiltRegion.Face, BuiltRegion.Level, BuiltRegion.X, BuiltRegion.Y,
        RemovedCount,
        static_cast<unsigned long long>(PlanetKey));

    WorldState->SetWorldFact(OutcomeKey, Encoded);

    UE_LOG(LogGoldenPath, Log, TEXT("  Recorded: %s"), *Encoded);
}

bool UGoldenPathSubsystem::LoadOutcome()
{
    UWorld* World = GetWorld();

    UWorldStateSubsystem* WorldState =
        (World != nullptr) ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (WorldState == nullptr)
    {
        return false;
    }

    FString Encoded;

    if (!WorldState->GetWorldFact(OutcomeKey, Encoded))
    {
        return false;
    }

    TArray<FString> Parts;
    Encoded.ParseIntoArray(Parts, TEXT("|"));

    if (Parts.Num() != 8)
    {
        return false;
    }

    if (!FPersistentEntityId::FromHexString(Parts[0], BuiltId))
    {
        return false;
    }

    BuiltRegion.PlanetKey = FCString::Strtoui64(*Parts[1], nullptr, 10);
    BuiltRegion.Face = static_cast<uint8>(FCString::Atoi(*Parts[2]));
    BuiltRegion.Level = static_cast<uint8>(FCString::Atoi(*Parts[3]));
    BuiltRegion.X = static_cast<uint32>(FCString::Strtoui64(*Parts[4], nullptr, 10));
    BuiltRegion.Y = static_cast<uint32>(FCString::Strtoui64(*Parts[5], nullptr, 10));
    RemovedCount = FCString::Atoi(*Parts[6]);
    PlanetKey = FCString::Strtoui64(*Parts[7], nullptr, 10);

    return BuiltId.IsValid() && BuiltRegion.IsValid();
}

void UGoldenPathSubsystem::Report() const
{
    UE_LOG(LogGoldenPath, Log, TEXT("=== Golden Path report ==="));
    UE_LOG(LogGoldenPath, Log, TEXT("  Result       : %s"),
        (Stage == EGoldenPathStage::Complete) ? TEXT("PASS") : TEXT("FAIL"));

    if (!FailureReason.IsEmpty())
    {
        UE_LOG(LogGoldenPath, Log, TEXT("  Reason       : %s"), *FailureReason);
    }

    UE_LOG(LogGoldenPath, Log, TEXT("  Mode         : %s"),
        bVerifyOnly ? TEXT("verify a previous run") : TEXT("full session"));

    UE_LOG(LogGoldenPath, Log, TEXT("  Elapsed      : %.1f s"), TotalSeconds);

    if (!bVerifyOnly)
    {
        UE_LOG(LogGoldenPath, Log, TEXT("  Home         : %s"), *HomeSystemName);
        UE_LOG(LogGoldenPath, Log, TEXT("  Destination  : %s"), *DestinationSystemName);
        UE_LOG(LogGoldenPath, Log, TEXT("  Warp         : %.3f ly in %.1f s"),
            WarpDistanceLightYears, WarpSeconds);
    }

    UE_LOG(LogGoldenPath, Log, TEXT("  Planet       : 0x%016llX %s"),
        static_cast<unsigned long long>(PlanetKey), *PlanetName);

    UE_LOG(LogGoldenPath, Log, TEXT("  Built        : %s"),
        BuiltId.IsValid() ? *BuiltId.ToHexString() : TEXT("nothing"));

    UE_LOG(LogGoldenPath, Log, TEXT("  Removed      : %d"), RemovedCount);

    for (const FString& Warning : Warnings)
    {
        UE_LOG(LogGoldenPath, Warning, TEXT("  ! %s"), *Warning);
    }

    UE_LOG(LogGoldenPath, Log,
        TEXT("--- %s ---"),
        (Stage == EGoldenPathStage::Complete)
            ? (Warnings.Num() == 0 ? TEXT("PASS") : TEXT("PASS WITH WARNINGS"))
            : TEXT("FAIL"));
}

// ---------------------------------------------------------------------------
// Console entry point
// ---------------------------------------------------------------------------

static void UniverseGoldenPathCommand(const TArray<FString>& Args, UWorld* World)
{
    UGoldenPathSubsystem* Golden =
        (World != nullptr) ? World->GetSubsystem<UGoldenPathSubsystem>() : nullptr;

    if (Golden == nullptr)
    {
        UE_LOG(LogGoldenPath, Error, TEXT("No Golden Path subsystem."));
        return;
    }

    if (Args.Num() > 0 && Args[0].Equals(TEXT("verify"), ESearchCase::IgnoreCase))
    {
        Golden->BeginVerify();
        return;
    }

    if (Args.Num() > 0 && Args[0].Equals(TEXT("abort"), ESearchCase::IgnoreCase))
    {
        Golden->Abort(TEXT("requested from the console"));
        return;
    }

    Golden->Begin();
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseGoldenPathCommand(
    TEXT("universe.GoldenPath"),
    TEXT("Runs the whole player experience once and verifies it. ")
    TEXT("Arguments: verify to check a previous run's changes, abort to stop."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseGoldenPathCommand));
