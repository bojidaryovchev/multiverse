// Copyright Universe Project. All Rights Reserved.

#include "UniverseGameMode.h"
#include "AstronomicalBodyActor.h"
#include "PlanetActor.h"
#include "PlanetEnvironment.h"
#include "WorldStateSubsystem.h"
#include "PlanetTerrainComponent.h"
#include "UniverseHUD.h"
#include "UniverseProbePawn.h"
#include "UniverseWorldSubsystem.h"

#include "StarSystemGenerator.h"
#include "StarSystemStreamingSubsystem.h"
#include "GalaxyDescriptor.h"
#include "UniverseScale.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverseGameMode, Log, All);

AUniverseGameMode::AUniverseGameMode()
{
    DefaultPawnClass = AUniverseProbePawn::StaticClass();
    HUDClass = AUniverseHUD::StaticClass();
    PlayerControllerClass = APlayerController::StaticClass();
}

void AUniverseGameMode::StartPlay()
{
    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        Super::StartPlay();
        return;
    }

    // Seed the universe before Super::StartPlay spawns the default pawn, so
    // the probe's anchor registers against an already-correct universe.
    if (UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        Subsystem->SetUniverseSeedText(UniverseSeedText);
    }

    BuildTestSystem();

    Super::StartPlay();
}

bool AUniverseGameMode::HasActiveSystem() const
{
    const UWorld* World = GetWorld();
    const UStarSystemStreamingSubsystem* Streamer =
        (World != nullptr) ? World->GetSubsystem<UStarSystemStreamingSubsystem>() : nullptr;

    return (Streamer != nullptr) && Streamer->HasActiveSystem();
}

bool AUniverseGameMode::GetActiveSystem(FStarSystemDescriptor& OutSystem) const
{
    const UWorld* World = GetWorld();
    const UStarSystemStreamingSubsystem* Streamer =
        (World != nullptr) ? World->GetSubsystem<UStarSystemStreamingSubsystem>() : nullptr;

    if (Streamer != nullptr && Streamer->GetActiveSystem(OutSystem))
    {
        return true;
    }

    // Before the streamer has caught up - the first frames of a session - the
    // home system is the honest answer rather than "no system", which would
    // make the HUD flicker through a blank state on every load.
    if (bHasHomeSystem)
    {
        OutSystem = HomeSystem;
        return true;
    }

    return false;
}

APlanetActor* AUniverseGameMode::GetPlanetActor() const
{
    const UWorld* World = GetWorld();
    const UStarSystemStreamingSubsystem* Streamer =
        (World != nullptr) ? World->GetSubsystem<UStarSystemStreamingSubsystem>() : nullptr;

    return (Streamer != nullptr) ? Streamer->GetActivePlanetActor() : nullptr;
}

void AUniverseGameMode::GetVisibleBodies(TArray<AAstronomicalBodyActor*>& OutBodies) const
{
    OutBodies.Reset();

    const UWorld* World = GetWorld();
    const UStarSystemStreamingSubsystem* Streamer =
        (World != nullptr) ? World->GetSubsystem<UStarSystemStreamingSubsystem>() : nullptr;

    if (Streamer == nullptr)
    {
        return;
    }

    for (const FStreamedSystem& System : Streamer->GetTrackedSystems())
    {
        if (System.StarActor != nullptr)
        {
            OutBodies.Add(System.StarActor);
        }

        for (const TObjectPtr<AAstronomicalBodyActor>& Body : System.BodyActors)
        {
            if (Body != nullptr)
            {
                OutBodies.Add(Body);
            }
        }
    }
}

bool AUniverseGameMode::GetProbeStartPose(
    FUniversePosition& OutPosition,
    FUniversePosition& OutLookAt) const
{
    if (!bHasHomeSystem)
    {
        return false;
    }

    OutPosition = ProbeStartPosition;
    OutLookAt = ProbeLookAtPosition;
    return true;
}

void AUniverseGameMode::BuildTestSystem()
{
    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>();
    if (Subsystem == nullptr)
    {
        UE_LOG(LogUniverseGameMode, Error, TEXT("No universe subsystem; cannot build the test system."));
        return;
    }

    // Search outward from the universe origin for a system with a living world.
    //
    // Nothing is hand-placed: whatever the generator says is there is what gets
    // built, so the scene is evidence of the generator's behaviour. What
    // changed in Sprint 004 is *which* of those systems is chosen. Taking the
    // nearest one and its outermost planet - the Sprint 002 rule - landed the
    // demo on a 486 K airless rock, which is a perfectly correct output of the
    // generator and shows nothing whatever about climate, biomes or life.
    //
    // So the search now scores candidates and prefers a habitable one. This is
    // a *presentation* decision, not a generation one: the universe is
    // unchanged and the barren rock is still there, but the world the player
    // starts beside is one where the environment system has something to say.
    // If no habitable planet is found within the search radius, the nearest
    // system is used and the fact is logged rather than hidden.
    // --- Find a galaxy first ------------------------------------------------
    //
    // Sprint 006 made stellar density a property of a galaxy, and the universe
    // origin is now almost certainly intergalactic space with no stars in it at
    // all. Searching from there was correct in Sprint 001 and finds nothing
    // now, so the search starts from somewhere that actually has stars: half
    // way out along the nearest galaxy's disk, which is typical of a galaxy in
    // a way that neither its core nor its rim is.
    FUniversePosition SearchCentre;

    if (FGalaxyGenerator::FindNearestGalaxy(
            Subsystem->GetSeedHierarchy(), FUniversePosition(), HomeGalaxy))
    {
        bHasHomeGalaxy = true;
        SearchCentre = FGalaxyGenerator::GetInhabitedPosition(HomeGalaxy);

        UE_LOG(LogUniverseGameMode, Log,
            TEXT("Home galaxy: %s"), *HomeGalaxy.ToDebugString());
    }
    else
    {
        UE_LOG(LogUniverseGameMode, Warning,
            TEXT("No galaxy found near the universe origin for seed \"%s\"; ")
            TEXT("the search will start from the origin and will probably find nothing."),
            *UniverseSeedText);
    }

    if (!FindHabitableSystem(*Subsystem, SearchCentre, HomeSystem, StreamingPlanetOrbitIndex))
    {
        if (!FStarSystemGenerator::FindSystemNear(
                Subsystem->GetSeedHierarchy(), SearchCentre, HomeSystem))
        {
            UE_LOG(LogUniverseGameMode, Warning,
                TEXT("No star system found near the galactic search centre for seed \"%s\"."),
                *UniverseSeedText);
            return;
        }

        StreamingPlanetOrbitIndex = HomeSystem.Planets.Num() - 1;

        UE_LOG(LogUniverseGameMode, Warning,
            TEXT("No habitable planet within %.1f ly; falling back to the nearest system."),
            HabitableSearchRadiusLightYears);
    }

    bHasHomeSystem = true;

    // Open the world's persistent state now that the universe seed is settled.
    //
    // Not earlier: the seed is part of the world's identity, and a store opened
    // before it is known would adopt whatever the default happened to be and
    // then refuse to match. Not later either - the planet actor below starts
    // streaming immediately, and vegetation must be able to ask what has been
    // removed before it places anything.
    if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
    {
        const FUniverseSeed UniverseSeed = Subsystem->GetSeedHierarchy().GetUniverseSeed();

        if (!WorldState->OpenWorld(Subsystem->GetUniverseSeedText(), UniverseSeed.Value))
        {
            UE_LOG(LogUniverseGameMode, Error,
                TEXT("World persistence is unavailable; this session will not save changes."));
        }
    }

    UE_LOG(LogUniverseGameMode, Log, TEXT("Home system:\n%s"), *HomeSystem.ToDebugString());

    // --- Nothing is spawned here any more ----------------------------------
    //
    // Sprints 002 through 005 had the game mode build the scene: one star, its
    // planets as placeholders, and one of them promoted to a real streaming
    // world. That was right while there was exactly one system, and it stops
    // being right the moment the player can leave it - a game mode that spawns
    // the scene owns actors it has no way to release, and "the system" stops
    // being a fact about the world and becomes a question about where the
    // player is.
    //
    // The streamer owns all of it now. The game mode picks where the player
    // starts and gets out of the way; the system around them appears because
    // they are near it, by exactly the same rule that will build the next one.

    // Work out where the probe should start.
    //
    // Next to the outermost planet, not looking down on the whole system.
    // Scaled space is a uniform scale model, so angular sizes are the real
    // ones: from a vantage point that frames an entire solar system, planets
    // genuinely subtend a fraction of a degree - they are dots, exactly as they
    // are from real interplanetary space. Inflating them to compensate would be
    // the "visual scale becomes universe scale" mistake this architecture
    // exists to avoid, so instead the probe starts where a planet is already a
    // proper disc and the HUD marks the rest.
    if (HomeSystem.Planets.Num() > 0)
    {
        const int32 TargetIndex = HomeSystem.Planets.IsValidIndex(StreamingPlanetOrbitIndex)
            ? StreamingPlanetOrbitIndex
            : HomeSystem.Planets.Num() - 1;

        const FPlanetDescriptor& Target = HomeSystem.Planets[TargetIndex];

        ProbeLookAtPosition = FStarSystemGenerator::GetPlanetPosition(HomeSystem, Target);

        // Measured in planet radii rather than metres: what decides whether a
        // body fills the view is the ratio of distance to radius, and scaled
        // space preserves angles, so this frames the planet the same way on a
        // moon or a gas giant.
        const double StandoffCm =
            Target.RadiusMeters * StartDistanceInPlanetRadii * UniverseScale::CmPerMeter;

        ProbeStartPosition = ProbeLookAtPosition.OffsetByCm(
            FVector3d(-StandoffCm, -StandoffCm * 0.35, StandoffCm * 0.3));
    }
    else
    {
        // A system with no planets: stand off from the star instead.
        const double StandoffCm =
            HomeSystem.Star.RadiusMeters * 60.0 * UniverseScale::CmPerMeter;

        ProbeLookAtPosition = HomeSystem.Position;
        ProbeStartPosition = HomeSystem.Position.OffsetByCm(FVector3d(-StandoffCm, 0.0, 0.0));
    }

    UE_LOG(LogUniverseGameMode, Log,
        TEXT("Home system %s. Probe starts %.6f AU from the star; the streamer builds the rest."),
        *HomeSystem.Name,
        FUniversePosition::DistanceAu(ProbeStartPosition, HomeSystem.Position));
}

bool AUniverseGameMode::FindHabitableSystem(
    const UUniverseWorldSubsystem& Subsystem,
    const FUniversePosition& Centre,
    FStarSystemDescriptor& OutSystem,
    int32& OutPlanetIndex) const
{
    TArray<FStarSystemDescriptor> Systems;

    FStarSystemGenerator::FindSystemsWithin(
        Subsystem.GetSeedHierarchy(), Centre, HabitableSearchRadiusLightYears,
        Systems, MaxHabitableSearchSystems);

    if (Systems.Num() == 0)
    {
        return false;
    }

    // Resolving an environment costs a few thousand terrain evaluations, so
    // candidates are filtered on the cheap astronomical properties first and
    // only the survivors are resolved properly. On a typical search that is a
    // handful out of several dozen.
    const FPlanetTerrainSettings Settings;

    double BestScore = 0.0;
    bool bFound = false;

    int32 Considered = 0;
    int32 Resolved = 0;

    for (const FStarSystemDescriptor& System : Systems)
    {
        for (int32 Index = 0; Index < System.Planets.Num(); ++Index)
        {
            const FPlanetDescriptor& Planet = System.Planets[Index];

            ++Considered;

            // A body with no air and no liquid-water temperature range cannot
            // support anything, and that is decided by two fields rather than
            // by a full environment resolve.
            if (!Planet.bHasAtmosphere
                || Planet.EquilibriumTemperatureK < 200.0
                || Planet.EquilibriumTemperatureK > 320.0)
            {
                continue;
            }

            const FPlanetSurfaceDescriptor Surface =
                FPlanetSurfaceDescriptor::FromGeneratedPlanet(System, Index);

            if (!Surface.IsValid())
            {
                continue;
            }

            const FPlanetEnvironmentDescriptor Environment =
                FPlanetEnvironment::Resolve(Surface, Settings, Planet);

            ++Resolved;

            if (!Environment.HasLife())
            {
                continue;
            }

            // Prefer a world with a lot to look at: life, water, and a mix of
            // land and sea rather than an ocean planet or a near-dry one. The
            // coverage term peaks at half and falls off either side, which is
            // what puts coastlines in the frame.
            const double Balance = 1.0 - FMath::Abs(Environment.OceanCoverage - 0.5) * 2.0;

            const double Score =
                Environment.VegetationPotential
                * (0.5 + 0.5 * FMath::Max(Balance, 0.0))
                * (Environment.Biosphere == EPlanetBiosphere::Barren ? 0.0 : 1.0);

            if (Score > BestScore)
            {
                BestScore = Score;
                OutSystem = System;
                OutPlanetIndex = Index;
                bFound = true;
            }
        }
    }

    if (bFound)
    {
        UE_LOG(LogUniverseGameMode, Log,
            TEXT("Habitable search: %d systems, %d planets considered, %d environments resolved. ")
            TEXT("Chose %s planet %d (score %.3f)."),
            Systems.Num(), Considered, Resolved,
            *OutSystem.Name, OutPlanetIndex, BestScore);
    }

    return bFound;
}
