// Copyright Universe Project. All Rights Reserved.

#include "GalaxyDescriptor.h"
#include "InterstellarTravel.h"
#include "StarSystemGenerator.h"
#include "StarSystemStreamingSubsystem.h"
#include "UniverseAnchorComponent.h"
#include "UniverseGameMode.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"
#include "UniverseWorldSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

/**
 * InterstellarCommands.cpp
 *
 * Console commands for Sprint 006: what is out there, how to point at it, and
 * how to get there without flying for ten minutes by hand.
 *
 * The same argument as PlanetTraversalCommands: these exist because a
 * four-light-year journey flown manually takes minutes, ends somewhere slightly
 * different every time, and cannot be run headless. Driven from the console it
 * arrives at the same star every time, which is what makes two measurements
 * comparable and what makes an automated acceptance run possible at all.
 */

DEFINE_LOG_CATEGORY_STATIC(LogInterstellar, Log, All);

namespace
{
    AUniverseProbePawn* FindProbe(UWorld* World)
    {
        return Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));
    }

    UStarSystemStreamingSubsystem* FindStreamer(UWorld* World)
    {
        return (World != nullptr) ? World->GetSubsystem<UStarSystemStreamingSubsystem>() : nullptr;
    }

    FUniversePosition GetViewpoint(UWorld* World)
    {
        if (const AUniverseProbePawn* Probe = FindProbe(World))
        {
            return Probe->GetUniversePosition();
        }

        if (const UUniverseWorldSubsystem* Subsystem =
                (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr)
        {
            if (const UUniverseAnchorComponent* Anchor = Subsystem->GetTrackedAnchor())
            {
                return Anchor->GetUniversePosition();
            }
        }

        return FUniversePosition();
    }
}

// ---------------------------------------------------------------------------
// universe.Systems - what is nearby
// ---------------------------------------------------------------------------

static void UniverseSystemsCommand(const TArray<FString>& Args, UWorld* World)
{
    const UStarSystemStreamingSubsystem* Streamer = FindStreamer(World);

    if (Streamer == nullptr)
    {
        UE_LOG(LogInterstellar, Error, TEXT("No streaming subsystem."));
        return;
    }

    const int32 Limit = (Args.Num() > 0) ? FMath::Max(FCString::Atoi(*Args[0]), 1) : 16;

    const TArray<FStreamedSystem>& Systems = Streamer->GetTrackedSystems();

    UE_LOG(LogInterstellar, Log,
        TEXT("--- Nearby systems (%d tracked, showing %d) ---"),
        Systems.Num(), FMath::Min(Limit, Systems.Num()));

    UE_LOG(LogInterstellar, Log,
        TEXT("  %-22s %12s  %-15s %-12s %s"),
        TEXT("NAME"), TEXT("DISTANCE"), TEXT("STATE"), TEXT("DISCOVERY"), TEXT("ADDRESS"));

    for (int32 Index = 0; Index < Systems.Num() && Index < Limit; ++Index)
    {
        const FStreamedSystem& System = Systems[Index];

        const FString Name = System.bHasDescriptor
            ? System.Descriptor.Name
            : FString(TEXT("(not generated)"));

        UE_LOG(LogInterstellar, Log,
            TEXT("  %-22s %9.4f ly  %-15s %-12s %s%s"),
            *Name,
            System.DistanceLightYears,
            LexToString(System.State),
            LexToString(System.Discovery),
            *System.Id.ToDebugString(),
            (System.Id == Streamer->GetTargetId()) ? TEXT("  <-- TARGET") : TEXT(""));
    }

    TArray<int32> Counts;
    Streamer->GetStateCounts(Counts);

    UE_LOG(LogInterstellar, Log,
        TEXT("  States: descriptor %d, distant %d, nearby %d, prewarming %d, active %d"),
        Counts.IsValidIndex(1) ? Counts[1] : 0,
        Counts.IsValidIndex(2) ? Counts[2] : 0,
        Counts.IsValidIndex(3) ? Counts[3] : 0,
        Counts.IsValidIndex(4) ? Counts[4] : 0,
        Counts.IsValidIndex(5) ? Counts[5] : 0);

    int32 Detected = 0;
    int32 Visited = 0;
    Streamer->GetDiscoveryCounts(Detected, Visited);

    UE_LOG(LogInterstellar, Log,
        TEXT("  Discovery: %d detected, %d visited. Generated %d systems, %d transitions."),
        Detected, Visited, Streamer->GetGeneratedSystemCount(), Streamer->GetTransitionCount());
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseSystemsCommand(
    TEXT("universe.Systems"),
    TEXT("Lists nearby star systems and their streaming state. Optional argument: how many."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseSystemsCommand));

// ---------------------------------------------------------------------------
// universe.Target - choose a destination
// ---------------------------------------------------------------------------

static void UniverseTargetCommand(const TArray<FString>& Args, UWorld* World)
{
    UStarSystemStreamingSubsystem* Streamer = FindStreamer(World);

    if (Streamer == nullptr)
    {
        UE_LOG(LogInterstellar, Error, TEXT("No streaming subsystem."));
        return;
    }

    const TArray<FStreamedSystem>& Systems = Streamer->GetTrackedSystems();

    if (Args.Num() == 0)
    {
        UE_LOG(LogInterstellar, Log,
            TEXT("universe.Target <index|name|clear|ahead>. universe.Systems lists them."));
        return;
    }

    const FString& Argument = Args[0];

    if (Argument.Equals(TEXT("clear"), ESearchCase::IgnoreCase))
    {
        Streamer->ClearTarget();
        UE_LOG(LogInterstellar, Log, TEXT("Target cleared."));
        return;
    }

    if (Argument.Equals(TEXT("ahead"), ESearchCase::IgnoreCase))
    {
        // Whatever the ship is pointing at, within about eleven degrees. The
        // player-facing way to pick a target without a starmap, and the one the
        // sprint asks for: "select a nearby star, see distance and direction".
        const AUniverseProbePawn* Probe = FindProbe(World);

        if (Probe == nullptr)
        {
            UE_LOG(LogInterstellar, Error, TEXT("No probe pawn to take a heading from."));
            return;
        }

        const FVector Forward = Probe->GetActorForwardVector();

        FUniverseSystemId Found;

        if (!Streamer->FindSystemInDirection(
                FVector3d(Forward.X, Forward.Y, Forward.Z), 0.98, Found))
        {
            UE_LOG(LogInterstellar, Warning, TEXT("No system within 11 degrees of the heading."));
            return;
        }

        Streamer->SetTargetSystem(Found);
    }
    else if (Argument.IsNumeric())
    {
        const int32 Index = FCString::Atoi(*Argument);

        if (!Systems.IsValidIndex(Index))
        {
            UE_LOG(LogInterstellar, Error,
                TEXT("Index %d is out of range; %d systems tracked."), Index, Systems.Num());
            return;
        }

        Streamer->SetTargetSystem(Systems[Index].Id);
    }
    else
    {
        const FStreamedSystem* Match = Systems.FindByPredicate(
            [&Argument](const FStreamedSystem& Candidate)
            {
                return Candidate.bHasDescriptor
                    && Candidate.Descriptor.Name.Equals(Argument, ESearchCase::IgnoreCase);
            });

        if (Match == nullptr)
        {
            UE_LOG(LogInterstellar, Error,
                TEXT("No tracked system named \"%s\". Note that a system only has a name once ")
                TEXT("it has been generated, which happens when it is close enough to draw."),
                *Argument);
            return;
        }

        Streamer->SetTargetSystem(Match->Id);
    }

    const FTravelTarget Target = Streamer->GetTravelTarget();

    if (!Target.IsValid())
    {
        return;
    }

    const FUniversePosition Viewpoint = GetViewpoint(World);
    const double DistanceMeters = FUniversePosition::DistanceMeters(Viewpoint, Target.Position);
    const FVector3d Direction = FUniversePosition::DirectionUnit(Viewpoint, Target.Position);

    FTravelProfile Profile;

    if (const AUniverseProbePawn* Probe = FindProbe(World))
    {
        Profile = Probe->TravelProfile;
    }

    UE_LOG(LogInterstellar, Log,
        TEXT("Target: %s"), *Target.Name);
    UE_LOG(LogInterstellar, Log,
        TEXT("  Distance   : %s"), *FInterstellarTravel::FormatDistance(DistanceMeters));
    UE_LOG(LogInterstellar, Log,
        TEXT("  Direction  : (%.4f, %.4f, %.4f) in universe axes"),
        Direction.X, Direction.Y, Direction.Z);
    UE_LOG(LogInterstellar, Log,
        TEXT("  Travel time: %.1f s at warp, %.3g years sublight"),
        FInterstellarTravel::EstimateTravelTimeSeconds(
            Profile, DistanceMeters, EUniverseTravelMode::Warp),
        FInterstellarTravel::EstimateTravelTimeSeconds(
            Profile, DistanceMeters, EUniverseTravelMode::Interstellar) / (365.25 * 86400.0));
    UE_LOG(LogInterstellar, Log,
        TEXT("  Arrival    : within %s of the star"),
        *FInterstellarTravel::FormatDistance(Target.ArrivalRadiusMeters));
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseTargetCommand(
    TEXT("universe.Target"),
    TEXT("Selects a navigation target: an index from universe.Systems, a system name, ")
    TEXT("\"ahead\" for whatever the ship is pointing at, or \"clear\"."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseTargetCommand));

// ---------------------------------------------------------------------------
// universe.Warp - engage or disengage
// ---------------------------------------------------------------------------

static void UniverseWarpCommand(const TArray<FString>& Args, UWorld* World)
{
    AUniverseProbePawn* Probe = FindProbe(World);

    if (Probe == nullptr)
    {
        UE_LOG(LogInterstellar, Error, TEXT("No probe pawn."));
        return;
    }

    const bool bWanted = (Args.Num() == 0)
        ? !Probe->IsWarpEngaged()
        : (Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase) || Args[0] == TEXT("1"));

    const bool bResult = Probe->SetWarpEngaged(bWanted);

    UE_LOG(LogInterstellar, Log,
        TEXT("Warp %s. Mode: %s. Speed: %s."),
        bResult ? TEXT("engaged") : TEXT("disengaged"),
        *Probe->GetTravelModeName(),
        *FInterstellarTravel::FormatSpeed(Probe->GetSpeedMetersPerSecond()));
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseWarpCommand(
    TEXT("universe.Warp"),
    TEXT("Engages or disengages the warp drive. Optional argument: on|off."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseWarpCommand));

// ---------------------------------------------------------------------------
// universe.FlyTo - fly to the target unattended
//
// Named FlyTo rather than Autopilot because universe.AutoPilot already exists:
// it is the Sprint 001 CVar that holds the thrust key down, and registering a
// command over a variable of the same name is a fatal error at startup rather
// than a warning. Two different things should not share a name anyway - that
// one is "press W forever", this one is "take me to that star".
// ---------------------------------------------------------------------------

static void UniverseFlyToCommand(const TArray<FString>& Args, UWorld* World)
{
    AUniverseProbePawn* Probe = FindProbe(World);
    UStarSystemStreamingSubsystem* Streamer = FindStreamer(World);

    if (Probe == nullptr || Streamer == nullptr)
    {
        UE_LOG(LogInterstellar, Error, TEXT("No probe pawn or streaming subsystem."));
        return;
    }

    // A target may be named here, so that a single command is a complete
    // scripted journey rather than two that have to be issued in order.
    if (Args.Num() > 0
        && !Args[0].Equals(TEXT("on"), ESearchCase::IgnoreCase)
        && !Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase)
        && Args[0] != TEXT("0")
        && Args[0] != TEXT("1"))
    {
        UniverseTargetCommand(Args, World);
    }

    const bool bWanted = (Args.Num() == 0)
        ? true
        : !(Args[0].Equals(TEXT("off"), ESearchCase::IgnoreCase) || Args[0] == TEXT("0"));

    if (bWanted && !Streamer->HasTarget())
    {
        // Refused rather than silently doing nothing. An autopilot with no
        // destination that reports success is a debugging session spent
        // wondering why the ship is not moving.
        UE_LOG(LogInterstellar, Error,
            TEXT("No navigation target. Use universe.Target first."));
        return;
    }

    Probe->SetAutoSteer(bWanted);
    Probe->SetAutoBrake(true);

    if (bWanted)
    {
        Probe->SetWarpEngaged(true);
    }

    UE_LOG(LogInterstellar, Log,
        TEXT("Autopilot %s%s."),
        bWanted ? TEXT("engaged, heading for ") : TEXT("disengaged"),
        bWanted ? *Streamer->GetTravelTarget().Name : TEXT(""));
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseFlyToCommand(
    TEXT("universe.FlyTo"),
    TEXT("Steers and brakes onto a system under warp. Argument: a target as accepted by ")
    TEXT("universe.Target, or on|off to toggle."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseFlyToCommand));

// ---------------------------------------------------------------------------
// universe.GalaxyInfo - where in the galaxy the player is
// ---------------------------------------------------------------------------

static void UniverseGalaxyInfoCommand(UWorld* World)
{
    const UUniverseWorldSubsystem* Subsystem =
        (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;

    if (Subsystem == nullptr)
    {
        UE_LOG(LogInterstellar, Error, TEXT("No universe subsystem."));
        return;
    }

    const FUniversePosition Position = GetViewpoint(World);

    UE_LOG(LogInterstellar, Log, TEXT("--- Galactic position ---"));
    UE_LOG(LogInterstellar, Log, TEXT("  Universe   : %s"), *Position.ToDebugString());

    int64 CellX = 0;
    int64 CellY = 0;
    int64 CellZ = 0;
    FGalaxyGenerator::GetCellCoordinates(Position, CellX, CellY, CellZ);

    UE_LOG(LogInterstellar, Log,
        TEXT("  Intergalactic cell: [%lld, %lld, %lld] of %.0f ly each"),
        CellX, CellY, CellZ, FGalaxyGenerator::GetCellSizeLightYears());

    FGalaxyDescriptor Galaxy;

    if (!FGalaxyGenerator::FindGalaxyAt(Subsystem->GetSeedHierarchy(), Position, Galaxy))
    {
        UE_LOG(LogInterstellar, Log,
            TEXT("  Galaxy     : none - this is intergalactic space, and it is genuinely empty."));

        if (FGalaxyGenerator::FindNearestGalaxy(Subsystem->GetSeedHierarchy(), Position, Galaxy))
        {
            UE_LOG(LogInterstellar, Log,
                TEXT("  Nearest    : %s, %.0f ly away"),
                *Galaxy.Name,
                FUniversePosition::DistanceLightYears(Position, Galaxy.Position));
        }

        return;
    }

    const FGalaxyLocalPosition Local = FGalaxyGenerator::ToGalaxyLocal(Galaxy, Position);

    UE_LOG(LogInterstellar, Log, TEXT("  Galaxy     : %s"), *Galaxy.ToDebugString());
    UE_LOG(LogInterstellar, Log,
        TEXT("  Galactic r : %.0f ly of %.0f (%.1f%% out)"),
        Local.RadiusLightYears, Galaxy.RadiusLightYears,
        100.0 * Local.RadiusLightYears / Galaxy.RadiusLightYears);
    UE_LOG(LogInterstellar, Log,
        TEXT("  Above disk : %.0f ly (disk half-thickness %.0f)"),
        Local.HeightLightYears, Galaxy.DiskThicknessLightYears);
    UE_LOG(LogInterstellar, Log,
        TEXT("  Density    : %.4f of core"),
        FGalaxyGenerator::GetStellarDensityAt(Galaxy, Local));

    int64 SectorX = 0;
    int64 SectorY = 0;
    int64 SectorZ = 0;
    Position.GetSector(SectorX, SectorY, SectorZ);

    UE_LOG(LogInterstellar, Log,
        TEXT("  Sector     : [%lld, %lld, %lld], %d systems, density %.4f"),
        SectorX, SectorY, SectorZ,
        FStarSystemGenerator::GetSystemCountInSector(
            Subsystem->GetSeedHierarchy(), SectorX, SectorY, SectorZ),
        FStarSystemGenerator::GetSectorStellarDensity(
            Subsystem->GetSeedHierarchy(), SectorX, SectorY, SectorZ));
}

static FAutoConsoleCommandWithWorld GUniverseGalaxyInfoCommand(
    TEXT("universe.GalaxyInfo"),
    TEXT("Reports where in the galaxy the player is, and the stellar density there."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseGalaxyInfoCommand));

// ---------------------------------------------------------------------------
// universe.TravelInfo - the movement state, in readable units
// ---------------------------------------------------------------------------

static void UniverseTravelInfoCommand(UWorld* World)
{
    const AUniverseProbePawn* Probe = FindProbe(World);
    const UStarSystemStreamingSubsystem* Streamer = FindStreamer(World);

    if (Probe == nullptr)
    {
        UE_LOG(LogInterstellar, Error, TEXT("No probe pawn."));
        return;
    }

    UE_LOG(LogInterstellar, Log, TEXT("--- Travel ---"));
    UE_LOG(LogInterstellar, Log, TEXT("  Mode       : %s"), *Probe->GetTravelModeName());
    UE_LOG(LogInterstellar, Log, TEXT("  Warp       : %s"),
        Probe->IsWarpEngaged() ? TEXT("engaged") : TEXT("off"));
    UE_LOG(LogInterstellar, Log, TEXT("  Speed      : %s"),
        *FInterstellarTravel::FormatSpeed(Probe->GetSpeedMetersPerSecond()));
    UE_LOG(LogInterstellar, Log, TEXT("  Odometer   : %.6f ly this session"),
        Probe->GetOdometerLightYears());
    UE_LOG(LogInterstellar, Log, TEXT("  Autopilot  : steer %s, brake %s%s"),
        Probe->IsAutoSteerEnabled() ? TEXT("on") : TEXT("off"),
        Probe->IsAutoBrakeEnabled() ? TEXT("on") : TEXT("off"),
        Probe->IsBraking() ? TEXT(" (braking now)") : TEXT(""));

    if (Streamer != nullptr && Streamer->HasTarget())
    {
        const FTravelTarget Target = Streamer->GetTravelTarget();

        UE_LOG(LogInterstellar, Log, TEXT("  Target     : %s at %s"),
            *Target.Name,
            *FInterstellarTravel::FormatDistance(Probe->GetDistanceToTargetMeters()));

        UE_LOG(LogInterstellar, Log, TEXT("  ETA        : %.1f s"),
            Probe->GetEstimatedArrivalSeconds());
    }
    else
    {
        UE_LOG(LogInterstellar, Log, TEXT("  Target     : none"));
    }

    UE_LOG(LogInterstellar, Log, TEXT("  Hazard stops: %d"), Probe->GetHazardStopCount());

    if (Probe->GetHazardStopCount() > 0)
    {
        const FTravelHazard& Hazard = Probe->GetLastHazard();

        UE_LOG(LogInterstellar, Log,
            TEXT("  Last stop  : %s, %s, standoff %s"),
            *Hazard.SystemId.ToDebugString(),
            (Hazard.PlanetIndex == INDEX_NONE) ? TEXT("star") : TEXT("planet"),
            *FInterstellarTravel::FormatDistance(Hazard.StandoffRadiusMeters));
    }
}

static FAutoConsoleCommandWithWorld GUniverseTravelInfoCommand(
    TEXT("universe.TravelInfo"),
    TEXT("Reports the movement state: mode, speed, target, ETA and hazard stops."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseTravelInfoCommand));
