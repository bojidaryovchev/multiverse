// Copyright Universe Project. All Rights Reserved.

#include "StarSystemStreamingSubsystem.h"

#include "AstronomicalBodyActor.h"
#include "PlanetActor.h"
#include "PlanetSurface.h"
#include "PlanetTerrain.h"
#include "StarSystemGenerator.h"
#include "UniverseAnchorComponent.h"
#include "UniverseScale.h"
#include "UniverseWorldSubsystem.h"
#include "WorldStateSubsystem.h"

#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogSystemStreaming, Log, All);

namespace
{
    /** Key prefix for discovery facts in the world store. */
    const TCHAR* DiscoveryKeyPrefix = TEXT("sys.");

    FString MakeDiscoveryKey(const FUniverseSystemId& Id)
    {
        return FString::Printf(TEXT("%s%016llX"),
            DiscoveryKeyPrefix, static_cast<unsigned long long>(Id.Hash));
    }
}

const TCHAR* LexToString(ESystemStreamState State)
{
    switch (State)
    {
    case ESystemStreamState::Unknown:        return TEXT("Unknown");
    case ESystemStreamState::DescriptorOnly: return TEXT("DescriptorOnly");
    case ESystemStreamState::DistantVisual:  return TEXT("DistantVisual");
    case ESystemStreamState::NearbyVisual:   return TEXT("NearbyVisual");
    case ESystemStreamState::Prewarming:     return TEXT("Prewarming");
    case ESystemStreamState::Active:         return TEXT("Active");
    case ESystemStreamState::Unloading:      return TEXT("Unloading");
    default:                                 return TEXT("?");
    }
}

const TCHAR* LexToString(ESystemDiscoveryState State)
{
    switch (State)
    {
    case ESystemDiscoveryState::Undiscovered: return TEXT("Undiscovered");
    case ESystemDiscoveryState::Detected:     return TEXT("Detected");
    case ESystemDiscoveryState::Visited:      return TEXT("Visited");
    default:                                  return TEXT("?");
    }
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void UStarSystemStreamingSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // The universe subsystem owns the seed and the tracked anchor, both of
    // which this needs on its first tick.
    Collection.InitializeDependency<UUniverseWorldSubsystem>();
}

void UStarSystemStreamingSubsystem::Deinitialize()
{
    for (FStreamedSystem& System : Systems)
    {
        ReleaseAll(System);
    }

    Systems.Reset();
    ActiveSystemId = FUniverseSystemId();
    TargetId = FUniverseSystemId();

    Super::Deinitialize();
}

TStatId UStarSystemStreamingSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UStarSystemStreamingSubsystem, STATGROUP_Tickables);
}

bool UStarSystemStreamingSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game
        || WorldType == EWorldType::PIE
        || WorldType == EWorldType::Editor;
}

UUniverseWorldSubsystem* UStarSystemStreamingSubsystem::GetUniverse() const
{
    const UWorld* World = GetWorld();

    return (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;
}

bool UStarSystemStreamingSubsystem::GetViewpoint(
    FUniversePosition& OutPosition, FVector3d& OutVelocityMs) const
{
    OutVelocityMs = FVector3d::ZeroVector;

    const UUniverseWorldSubsystem* Universe = GetUniverse();

    if (Universe == nullptr)
    {
        return false;
    }

    const UUniverseAnchorComponent* Anchor = Universe->GetTrackedAnchor();

    if (Anchor == nullptr)
    {
        return false;
    }

    OutPosition = Anchor->GetUniversePosition();

    return true;
}

void UStarSystemStreamingSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    FUniversePosition Viewpoint;
    FVector3d Velocity;

    if (!GetViewpoint(Viewpoint, Velocity))
    {
        return;
    }

    if (!bDiscoveryLoaded)
    {
        LoadDiscoveryState();
    }

    TimeSinceRescan += DeltaTime;

    if (TimeSinceRescan >= RescanIntervalSeconds)
    {
        TimeSinceRescan = 0.0;
        Rescan(Viewpoint);
    }
    else
    {
        // Distances still need updating every frame even without a rescan: the
        // set of systems changes on the scale of seconds, but which state each
        // one should be in changes at the speed of the ship.
        for (FStreamedSystem& System : Systems)
        {
            System.DistanceLightYears =
                FUniversePosition::DistanceLightYears(Viewpoint, System.Position);
        }
    }

    ApplyStates(DeltaTime);
}

// ---------------------------------------------------------------------------
// Scanning
// ---------------------------------------------------------------------------

void UStarSystemStreamingSubsystem::Rescan(const FUniversePosition& Viewpoint)
{
    const UUniverseWorldSubsystem* Universe = GetUniverse();

    if (Universe == nullptr)
    {
        return;
    }

    const FUniverseSeedHierarchy& Hierarchy = Universe->GetSeedHierarchy();

    // Sector addresses within the scan radius. Addresses only - no systems are
    // generated here, because a 25 light year radius is over a thousand sectors
    // and generating every star in them to decide which are worth drawing would
    // be exactly the "loop over the galaxy" the sprint forbids.
    const int64 SectorReach = static_cast<int64>(FMath::CeilToDouble(
        ScanRadiusLightYears / UniverseScale::SectorSizeLightYears));

    int64 CentreX = 0;
    int64 CentreY = 0;
    int64 CentreZ = 0;
    Viewpoint.GetSector(CentreX, CentreY, CentreZ);

    TSet<uint64> Seen;
    Seen.Reserve(Systems.Num() * 2);

    TArray<FStreamedSystem> Next;
    Next.Reserve(Systems.Num() + 16);

    for (int64 Z = CentreZ - SectorReach; Z <= CentreZ + SectorReach; ++Z)
    {
        for (int64 Y = CentreY - SectorReach; Y <= CentreY + SectorReach; ++Y)
        {
            for (int64 X = CentreX - SectorReach; X <= CentreX + SectorReach; ++X)
            {
                const int32 Count =
                    FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, Z);

                for (int32 Index = 0; Index < Count; ++Index)
                {
                    const FUniversePosition Position =
                        FStarSystemGenerator::GetSystemPosition(Hierarchy, X, Y, Z, Index);

                    const double DistanceLy =
                        FUniversePosition::DistanceLightYears(Viewpoint, Position);

                    if (DistanceLy > ScanRadiusLightYears)
                    {
                        continue;
                    }

                    const FUniverseSystemId Id =
                        FStarSystemGenerator::MakeSystemId(X, Y, Z, Index);

                    Seen.Add(Id.Hash);

                    // Carry an existing record forward rather than rebuilding
                    // it: the record owns actors, and losing it would destroy
                    // and respawn a solar system twice a second.
                    const int32 Existing = Systems.IndexOfByPredicate(
                        [&Id](const FStreamedSystem& Candidate) { return Candidate.Id == Id; });

                    if (Existing != INDEX_NONE)
                    {
                        FStreamedSystem Carried = MoveTemp(Systems[Existing]);
                        Carried.DistanceLightYears = DistanceLy;
                        Carried.Position = Position;
                        Next.Add(MoveTemp(Carried));
                        continue;
                    }

                    FStreamedSystem Fresh;
                    Fresh.Id = Id;
                    Fresh.Position = Position;
                    Fresh.DistanceLightYears = DistanceLy;
                    Fresh.State = ESystemStreamState::DescriptorOnly;
                    Fresh.Discovery = GetDiscoveryState(Id);

                    Next.Add(MoveTemp(Fresh));
                }
            }
        }
    }

    // Anything that fell out of range releases whatever it was holding. The
    // record itself goes; the persistent deltas and the discovery state do not.
    for (FStreamedSystem& System : Systems)
    {
        if (!Seen.Contains(System.Id.Hash))
        {
            if (System.State != ESystemStreamState::DescriptorOnly
                && System.State != ESystemStreamState::Unknown)
            {
                UE_LOG(LogSystemStreaming, Verbose,
                    TEXT("Releasing %s: out of scan range."), *System.Id.ToDebugString());
            }

            if (System.Id == ActiveSystemId)
            {
                ActiveSystemId = FUniverseSystemId();
            }

            ReleaseAll(System);
        }
    }

    Systems = MoveTemp(Next);

    Systems.Sort([](const FStreamedSystem& A, const FStreamedSystem& B)
    {
        return A.DistanceLightYears < B.DistanceLightYears;
    });
}

// ---------------------------------------------------------------------------
// State selection
// ---------------------------------------------------------------------------

void UStarSystemStreamingSubsystem::ApplyStates(double DeltaSeconds)
{
    const double Hysteresis = 1.0 + FMath::Max(HysteresisFraction, 0.0);

    int32 VisualBudget = MaxVisualSystems;
    int32 NearbyBudget = MaxNearbySystems;

    // Systems are sorted nearest-first, so consuming the budget in order gives
    // it to the ones that matter without a second sort.
    for (FStreamedSystem& System : Systems)
    {
        System.TimeInState += DeltaSeconds;

        const double Distance = System.DistanceLightYears;

        // The threshold a system must be inside to *enter* a state, and the
        // wider one it must leave before dropping out of it. Applied by
        // comparing against the entry radius when climbing and the exit radius
        // when falling, which is what makes the boundary sticky rather than
        // giving the two directions different geometry.
        const bool bWasActive = (System.State >= ESystemStreamState::Prewarming);
        const bool bWasNearby = (System.State >= ESystemStreamState::NearbyVisual);
        const bool bWasVisual = (System.State >= ESystemStreamState::DistantVisual);

        const double ActiveThreshold = ActiveLightYears * (bWasActive ? Hysteresis : 1.0);
        const double NearbyThreshold = NearbyVisualLightYears * (bWasNearby ? Hysteresis : 1.0);
        const double VisualThreshold = DistantVisualLightYears * (bWasVisual ? Hysteresis : 1.0);

        ESystemStreamState Desired = ESystemStreamState::DescriptorOnly;

        if (Distance <= ActiveThreshold)
        {
            Desired = ESystemStreamState::Active;
        }
        else if (Distance <= NearbyThreshold)
        {
            Desired = ESystemStreamState::NearbyVisual;
        }
        else if (Distance <= VisualThreshold)
        {
            Desired = ESystemStreamState::DistantVisual;
        }

        // --- Prewarm on approach --------------------------------------------
        //
        // The one transition that is not distance alone. A ship closing on its
        // target at warp covers the entire NearbyVisual band in less than a
        // frame, so waiting for it to be near enough would mean arriving in
        // black space and watching a planet assemble. Time-to-arrival is the
        // right trigger because it is the quantity that actually bounds how
        // long there is to prepare.
        if (Desired < ESystemStreamState::Prewarming && System.Id == TargetId)
        {
            FUniversePosition Viewpoint;
            FVector3d Velocity;

            if (GetViewpoint(Viewpoint, Velocity))
            {
                const double DistanceMeters =
                    FUniversePosition::DistanceMeters(Viewpoint, System.Position);

                const double ClosingSpeed = Velocity.Size();

                if (ClosingSpeed > 0.0
                    && (DistanceMeters / ClosingSpeed) <= PrewarmLeadSeconds)
                {
                    Desired = ESystemStreamState::Prewarming;
                }
            }
        }

        // --- Budgets ---------------------------------------------------------
        //
        // Applied after the distance decision rather than before it, so a
        // system that is close enough to matter is never denied by a budget
        // that a further one has already spent.
        if (Desired >= ESystemStreamState::NearbyVisual)
        {
            if (NearbyBudget > 0)
            {
                --NearbyBudget;
            }
            else
            {
                Desired = ESystemStreamState::DistantVisual;
            }
        }

        if (Desired >= ESystemStreamState::DistantVisual)
        {
            if (VisualBudget > 0)
            {
                --VisualBudget;
            }
            else
            {
                Desired = ESystemStreamState::DescriptorOnly;
            }
        }

        // Exactly one Active system. Not a performance limit: Active owns the
        // simulation frame and the streaming planet, and two of those is two
        // answers to "which way is down".
        if (Desired >= ESystemStreamState::Prewarming
            && ActiveSystemId.IsValid()
            && !(System.Id == ActiveSystemId))
        {
            Desired = ESystemStreamState::NearbyVisual;
        }

        if (Desired != System.State)
        {
            TransitionTo(System, Desired);
        }
    }
}

void UStarSystemStreamingSubsystem::TransitionTo(
    FStreamedSystem& System, ESystemStreamState NewState)
{
    const ESystemStreamState Previous = System.State;

    // Everything from DistantVisual up needs the descriptor. Failing to build
    // it leaves the system where it is rather than promoting it into a state
    // whose invariants it cannot meet.
    if (NewState >= ESystemStreamState::DistantVisual && !EnsureDescriptor(System))
    {
        return;
    }

    System.State = NewState;
    System.TimeInState = 0.0;
    ++TransitionCount;

    switch (NewState)
    {
    case ESystemStreamState::DescriptorOnly:
        ReleasePlanetActor(System);
        ReleaseBodyActors(System);

        if (System.StarActor != nullptr)
        {
            System.StarActor->Destroy();
            System.StarActor = nullptr;
        }
        break;

    case ESystemStreamState::DistantVisual:
        ReleasePlanetActor(System);
        ReleaseBodyActors(System);
        BuildStarActor(System);
        MarkDiscovered(System, ESystemDiscoveryState::Detected);
        break;

    case ESystemStreamState::NearbyVisual:
        ReleasePlanetActor(System);
        BuildStarActor(System);
        BuildBodyActors(System);
        MarkDiscovered(System, ESystemDiscoveryState::Detected);
        break;

    case ESystemStreamState::Prewarming:
    case ESystemStreamState::Active:
        BuildStarActor(System);
        BuildBodyActors(System);
        BuildPlanetActor(System);
        ActiveSystemId = System.Id;
        MarkDiscovered(System, ESystemDiscoveryState::Visited);
        break;

    default:
        break;
    }

    if (Previous >= ESystemStreamState::Prewarming
        && NewState < ESystemStreamState::Prewarming
        && System.Id == ActiveSystemId)
    {
        ActiveSystemId = FUniverseSystemId();
    }

    UE_LOG(LogSystemStreaming, Verbose,
        TEXT("%s: %s -> %s at %.4f ly"),
        *System.Id.ToDebugString(), LexToString(Previous), LexToString(NewState),
        System.DistanceLightYears);
}

bool UStarSystemStreamingSubsystem::EnsureDescriptor(FStreamedSystem& System)
{
    if (System.bHasDescriptor)
    {
        return true;
    }

    const UUniverseWorldSubsystem* Universe = GetUniverse();

    if (Universe == nullptr)
    {
        return false;
    }

    if (!FStarSystemGenerator::GenerateSystem(
            Universe->GetSeedHierarchy(),
            System.Id.SectorX, System.Id.SectorY, System.Id.SectorZ, System.Id.IndexInSector,
            System.Descriptor))
    {
        return false;
    }

    System.bHasDescriptor = true;
    ++GeneratedSystemCount;

    return true;
}

// ---------------------------------------------------------------------------
// Building and releasing
// ---------------------------------------------------------------------------

void UStarSystemStreamingSubsystem::BuildStarActor(FStreamedSystem& System)
{
    if (System.StarActor != nullptr)
    {
        return;
    }

    UWorld* World = GetWorld();

    if (World == nullptr || !System.bHasDescriptor)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    System.StarActor = World->SpawnActor<AAstronomicalBodyActor>(
        AAstronomicalBodyActor::StaticClass(), FTransform::Identity, SpawnParams);

    if (System.StarActor != nullptr)
    {
        System.StarActor->InitialiseAsStar(System.Descriptor);
    }
}

void UStarSystemStreamingSubsystem::BuildBodyActors(FStreamedSystem& System)
{
    if (System.BodyActors.Num() > 0 || !System.bHasDescriptor)
    {
        return;
    }

    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    for (const FPlanetDescriptor& Planet : System.Descriptor.Planets)
    {
        AAstronomicalBodyActor* Body = World->SpawnActor<AAstronomicalBodyActor>(
            AAstronomicalBodyActor::StaticClass(), FTransform::Identity, SpawnParams);

        if (Body != nullptr)
        {
            Body->InitialiseAsPlanet(System.Descriptor, Planet);
        }

        System.BodyActors.Add(Body);
    }
}

void UStarSystemStreamingSubsystem::BuildPlanetActor(FStreamedSystem& System)
{
    if (System.PlanetActor != nullptr || !System.bHasDescriptor)
    {
        return;
    }

    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    const int32 OrbitIndex = ChooseStreamingPlanet(System.Descriptor);

    if (!System.Descriptor.Planets.IsValidIndex(OrbitIndex))
    {
        return;
    }

    const FPlanetSurfaceDescriptor Surface =
        FPlanetSurfaceDescriptor::FromGeneratedPlanet(System.Descriptor, OrbitIndex);

    if (!Surface.IsValid())
    {
        return;
    }

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    System.PlanetActor = World->SpawnActor<APlanetActor>(
        APlanetActor::StaticClass(), FTransform::Identity, SpawnParams);

    if (System.PlanetActor == nullptr)
    {
        return;
    }

    System.StreamingPlanetOrbitIndex = OrbitIndex;

    FPlanetTerrainSettings TerrainSettings;
    System.PlanetActor->Initialise(
        Surface,
        System.Descriptor.Planets[OrbitIndex],
        TerrainSettings,
        System.Descriptor.Position,
        System.Descriptor.Star.LuminositySolar);

    // The placeholder sphere for the promoted body would sit inside the real
    // terrain. Removed rather than hidden: two representations of one planet is
    // the beginning of the render/simulation confusion the architecture exists
    // to avoid, and a hidden one is still a thing that can be un-hidden by
    // accident.
    if (System.BodyActors.IsValidIndex(OrbitIndex) && System.BodyActors[OrbitIndex] != nullptr)
    {
        System.BodyActors[OrbitIndex]->Destroy();
        System.BodyActors[OrbitIndex] = nullptr;
    }

    UE_LOG(LogSystemStreaming, Log,
        TEXT("Activated %s: streaming %s"),
        *System.Descriptor.Name, *Surface.ToDebugString());
}

void UStarSystemStreamingSubsystem::ReleaseBodyActors(FStreamedSystem& System)
{
    for (TObjectPtr<AAstronomicalBodyActor>& Body : System.BodyActors)
    {
        if (Body != nullptr)
        {
            Body->Destroy();
            Body = nullptr;
        }
    }

    System.BodyActors.Reset();
}

void UStarSystemStreamingSubsystem::ReleasePlanetActor(FStreamedSystem& System)
{
    if (System.PlanetActor == nullptr)
    {
        return;
    }

    // The expensive things - terrain patches, environment, collision,
    // vegetation, wildlife - all hang off the planet actor and go with it.
    // Persistent deltas do not: they live in the world store, which is exactly
    // the point of "procedural base world plus sparse deltas".
    System.PlanetActor->Destroy();
    System.PlanetActor = nullptr;
    System.StreamingPlanetOrbitIndex = INDEX_NONE;
}

void UStarSystemStreamingSubsystem::ReleaseAll(FStreamedSystem& System)
{
    ReleasePlanetActor(System);
    ReleaseBodyActors(System);

    if (System.StarActor != nullptr)
    {
        System.StarActor->Destroy();
        System.StarActor = nullptr;
    }

    System.State = ESystemStreamState::Unknown;
}

int32 UStarSystemStreamingSubsystem::ChooseStreamingPlanet(const FStarSystemDescriptor& System) const
{
    if (System.Planets.Num() == 0)
    {
        return INDEX_NONE;
    }

    // The most temperate world, by distance of its equilibrium temperature from
    // liquid water. A system that has no such world still gets one - the
    // outermost, as Sprint 002 chose - because "nothing to stand on" is a worse
    // answer than "somewhere cold".
    int32 Best = System.Planets.Num() - 1;
    double BestScore = TNumericLimits<double>::Max();

    for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
    {
        const double Score = FMath::Abs(System.Planets[Index].EquilibriumTemperatureK - 288.0);

        if (Score < BestScore)
        {
            BestScore = Score;
            Best = Index;
        }
    }

    return Best;
}

// ---------------------------------------------------------------------------
// Queries
// ---------------------------------------------------------------------------

bool UStarSystemStreamingSubsystem::GetActiveSystem(FStarSystemDescriptor& OutSystem) const
{
    const FStreamedSystem* Found = FindSystem(ActiveSystemId);

    if (Found == nullptr || !Found->bHasDescriptor)
    {
        return false;
    }

    OutSystem = Found->Descriptor;
    return true;
}

APlanetActor* UStarSystemStreamingSubsystem::GetActivePlanetActor() const
{
    const FStreamedSystem* Found = FindSystem(ActiveSystemId);

    return (Found != nullptr) ? Found->PlanetActor : nullptr;
}

const FStreamedSystem* UStarSystemStreamingSubsystem::FindSystem(const FUniverseSystemId& Id) const
{
    if (!Id.IsValid())
    {
        return nullptr;
    }

    return Systems.FindByPredicate(
        [&Id](const FStreamedSystem& Candidate) { return Candidate.Id == Id; });
}

void UStarSystemStreamingSubsystem::GetStateCounts(TArray<int32>& OutCounts) const
{
    OutCounts.Init(0, static_cast<int32>(ESystemStreamState::Unloading) + 1);

    for (const FStreamedSystem& System : Systems)
    {
        const int32 Index = static_cast<int32>(System.State);

        if (OutCounts.IsValidIndex(Index))
        {
            ++OutCounts[Index];
        }
    }
}

bool UStarSystemStreamingSubsystem::GetNearestSystem(FStreamedSystem& OutSystem) const
{
    if (Systems.Num() == 0)
    {
        return false;
    }

    // Systems are kept sorted nearest-first by Rescan.
    OutSystem = Systems[0];
    return true;
}

// ---------------------------------------------------------------------------
// Navigation
// ---------------------------------------------------------------------------

bool UStarSystemStreamingSubsystem::SetTargetSystem(const FUniverseSystemId& Id)
{
    if (FindSystem(Id) == nullptr)
    {
        return false;
    }

    TargetId = Id;
    return true;
}

void UStarSystemStreamingSubsystem::ClearTarget()
{
    TargetId = FUniverseSystemId();
}

FTravelTarget UStarSystemStreamingSubsystem::GetTravelTarget() const
{
    FTravelTarget Target;

    const FStreamedSystem* Found = FindSystem(TargetId);

    if (Found == nullptr)
    {
        return Target;
    }

    Target.bValid = true;
    Target.Position = Found->Position;
    Target.SystemId = Found->Id;
    Target.Name = Found->bHasDescriptor ? Found->Descriptor.Name : Found->Id.ToDebugString();

    // Arrival radius scaled to the system rather than fixed. A ship that stops
    // 10^11 metres from a red dwarf is inside its planetary system; the same
    // distance from a supergiant is inside the star.
    const double StarRadius = Found->bHasDescriptor
        ? Found->Descriptor.Star.RadiusMeters
        : 6.957e8;

    Target.ArrivalRadiusMeters = FMath::Max(StarRadius * 200.0, 1.0e11);

    return Target;
}

bool UStarSystemStreamingSubsystem::FindSystemInDirection(
    const FVector3d& DirectionUnit,
    double ConeCosine,
    FUniverseSystemId& OutId) const
{
    FUniversePosition Viewpoint;
    FVector3d Velocity;

    if (!GetViewpoint(Viewpoint, Velocity) || DirectionUnit.IsNearlyZero())
    {
        return false;
    }

    const FVector3d Forward = DirectionUnit.GetSafeNormal();

    double BestDistance = TNumericLimits<double>::Max();
    bool bFound = false;

    for (const FStreamedSystem& System : Systems)
    {
        const FVector3d ToSystem =
            FUniversePosition::DirectionUnit(Viewpoint, System.Position);

        if (ToSystem.IsNearlyZero())
        {
            continue;
        }

        if (FVector3d::DotProduct(ToSystem, Forward) < ConeCosine)
        {
            continue;
        }

        if (System.DistanceLightYears < BestDistance)
        {
            BestDistance = System.DistanceLightYears;
            OutId = System.Id;
            bFound = true;
        }
    }

    return bFound;
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

ESystemDiscoveryState UStarSystemStreamingSubsystem::GetDiscoveryState(
    const FUniverseSystemId& Id) const
{
    const FStreamedSystem* Found = FindSystem(Id);

    return (Found != nullptr) ? Found->Discovery : ESystemDiscoveryState::Undiscovered;
}

void UStarSystemStreamingSubsystem::GetDiscoveryCounts(int32& OutDetected, int32& OutVisited) const
{
    OutDetected = 0;
    OutVisited = 0;

    for (const FStreamedSystem& System : Systems)
    {
        if (System.Discovery == ESystemDiscoveryState::Detected)
        {
            ++OutDetected;
        }
        else if (System.Discovery == ESystemDiscoveryState::Visited)
        {
            ++OutVisited;
            ++OutDetected;
        }
    }
}

void UStarSystemStreamingSubsystem::MarkDiscovered(
    FStreamedSystem& System, ESystemDiscoveryState NewState)
{
    // Discovery only ever moves forward. Leaving a system does not un-visit it.
    if (NewState <= System.Discovery)
    {
        return;
    }

    System.Discovery = NewState;

    SaveDiscoveryState(System);

    if (System.bHasDescriptor)
    {
        OnSystemDiscovered.Broadcast(System.Descriptor, NewState);

        UE_LOG(LogSystemStreaming, Log,
            TEXT("%s is now %s."), *System.Descriptor.Name, LexToString(NewState));
    }
}

void UStarSystemStreamingSubsystem::SaveDiscoveryState(const FStreamedSystem& System)
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

    WorldState->SetWorldFact(
        MakeDiscoveryKey(System.Id),
        FString::FromInt(static_cast<int32>(System.Discovery)));
}

void UStarSystemStreamingSubsystem::LoadDiscoveryState()
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>();

    if (WorldState == nullptr || !WorldState->IsOpen())
    {
        // Not an error, and not a reason to give up permanently: the world
        // opens after the game mode has picked a system, which can be several
        // ticks after this subsystem starts running.
        return;
    }

    TArray<TPair<FString, FString>> Facts;
    WorldState->GetWorldFactsWithPrefix(DiscoveryKeyPrefix, Facts);

    // Keyed by system hash rather than by address, so the lookup below does not
    // have to parse a key back into sector coordinates.
    TMap<uint64, ESystemDiscoveryState> Loaded;
    Loaded.Reserve(Facts.Num());

    for (const TPair<FString, FString>& Fact : Facts)
    {
        const FString HashText = Fact.Key.RightChop(FCString::Strlen(DiscoveryKeyPrefix));

        // The key is written as sixteen hex digits by MakeDiscoveryKey.
        const uint64 Hash = FParse::HexNumber64(*HashText);

        if (Hash != 0)
        {
            Loaded.Add(Hash, static_cast<ESystemDiscoveryState>(FCString::Atoi(*Fact.Value)));
        }
    }

    for (FStreamedSystem& System : Systems)
    {
        const ESystemDiscoveryState* Found = Loaded.Find(System.Id.Hash);

        if (Found != nullptr && *Found > System.Discovery)
        {
            System.Discovery = *Found;
        }
    }

    bDiscoveryLoaded = true;

    UE_LOG(LogSystemStreaming, Log,
        TEXT("Loaded discovery state for %d systems."), Facts.Num());
}
