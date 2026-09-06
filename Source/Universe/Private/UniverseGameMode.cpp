// Copyright Universe Project. All Rights Reserved.

#include "UniverseGameMode.h"
#include "AstronomicalBodyActor.h"
#include "PlanetActor.h"
#include "PlanetEnvironment.h"
#include "PlanetTerrainComponent.h"
#include "UniverseHUD.h"
#include "UniverseProbePawn.h"
#include "UniverseWorldSubsystem.h"

#include "StarSystemGenerator.h"
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

bool AUniverseGameMode::GetProbeStartPose(
    FUniversePosition& OutPosition,
    FUniversePosition& OutLookAt) const
{
    if (!bHasActiveSystem)
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
    const FUniversePosition SearchCentre;

    if (!FindHabitableSystem(*Subsystem, SearchCentre, ActiveSystem, StreamingPlanetOrbitIndex))
    {
        if (!FStarSystemGenerator::FindNearestSystem(
                Subsystem->GetSeedHierarchy(), SearchCentre, SystemSearchRadiusLightYears, ActiveSystem))
        {
            UE_LOG(LogUniverseGameMode, Warning,
                TEXT("No star system found within %.1f ly of the universe origin for seed \"%s\"."),
                SystemSearchRadiusLightYears, *UniverseSeedText);
            return;
        }

        StreamingPlanetOrbitIndex = ActiveSystem.Planets.Num() - 1;

        UE_LOG(LogUniverseGameMode, Warning,
            TEXT("No habitable planet within %.1f ly; falling back to the nearest system."),
            HabitableSearchRadiusLightYears);
    }

    bHasActiveSystem = true;

    UE_LOG(LogUniverseGameMode, Log, TEXT("Test system:\n%s"), *ActiveSystem.ToDebugString());

    FActorSpawnParameters SpawnParams;
    SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

    // The star.
    if (AAstronomicalBodyActor* Star = World->SpawnActor<AAstronomicalBodyActor>(
            AAstronomicalBodyActor::StaticClass(), FTransform::Identity, SpawnParams))
    {
        Star->InitialiseAsStar(ActiveSystem);
        SpawnedBodies.Add(Star);
    }

    // The planets.
    for (const FPlanetDescriptor& Planet : ActiveSystem.Planets)
    {
        if (AAstronomicalBodyActor* Body = World->SpawnActor<AAstronomicalBodyActor>(
                AAstronomicalBodyActor::StaticClass(), FTransform::Identity, SpawnParams))
        {
            Body->InitialiseAsPlanet(ActiveSystem, Planet);
            SpawnedBodies.Add(Body);
        }
    }

    // --- Promote one planet to a fully streaming world --------------------
    //
    // Placeholder spheres are fine for bodies being looked at from across a
    // system, but Sprint 002 needs one planet that is actually built from
    // terrain patches. The outermost is chosen because the probe starts beside
    // it, and because it is the one with room around it.
    if (ActiveSystem.Planets.IsValidIndex(StreamingPlanetOrbitIndex))
    {
        const FPlanetSurfaceDescriptor Surface =
            FPlanetSurfaceDescriptor::FromGeneratedPlanet(ActiveSystem, StreamingPlanetOrbitIndex);

        if (Surface.IsValid())
        {
            FActorSpawnParameters PlanetSpawn;
            PlanetSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

            PlanetActor = World->SpawnActor<APlanetActor>(
                APlanetActor::StaticClass(), FTransform::Identity, PlanetSpawn);

            if (PlanetActor != nullptr)
            {
                FPlanetTerrainSettings TerrainSettings;
                PlanetActor->Initialise(
                    Surface, ActiveSystem.Planets[StreamingPlanetOrbitIndex], TerrainSettings,
                    ActiveSystem.Position, ActiveSystem.Star.LuminositySolar);

                // The placeholder sphere for this body would sit inside the real
                // terrain; remove it so there is exactly one planet on screen.
                if (SpawnedBodies.IsValidIndex(StreamingPlanetOrbitIndex + 1))
                {
                    if (AAstronomicalBodyActor* Placeholder = SpawnedBodies[StreamingPlanetOrbitIndex + 1])
                    {
                        Placeholder->Destroy();
                        SpawnedBodies[StreamingPlanetOrbitIndex + 1] = nullptr;
                    }
                }

                UE_LOG(LogUniverseGameMode, Log,
                    TEXT("Streaming planet: %s"), *Surface.ToDebugString());
            }
        }
    }

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
    if (ActiveSystem.Planets.Num() > 0)
    {
        const int32 TargetIndex = ActiveSystem.Planets.IsValidIndex(StreamingPlanetOrbitIndex)
            ? StreamingPlanetOrbitIndex
            : ActiveSystem.Planets.Num() - 1;

        const FPlanetDescriptor& Target = ActiveSystem.Planets[TargetIndex];

        ProbeLookAtPosition = FStarSystemGenerator::GetPlanetPosition(ActiveSystem, Target);

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
            ActiveSystem.Star.RadiusMeters * 60.0 * UniverseScale::CmPerMeter;

        ProbeLookAtPosition = ActiveSystem.Position;
        ProbeStartPosition = ActiveSystem.Position.OffsetByCm(FVector3d(-StandoffCm, 0.0, 0.0));
    }

    UE_LOG(LogUniverseGameMode, Log,
        TEXT("Spawned %d placeholder bodies for %s. Probe starts %.6f AU from the star."),
        SpawnedBodies.Num(), *ActiveSystem.Name,
        FUniversePosition::DistanceAu(ProbeStartPosition, ActiveSystem.Position));
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
