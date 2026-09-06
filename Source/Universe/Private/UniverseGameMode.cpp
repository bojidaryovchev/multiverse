// Copyright Universe Project. All Rights Reserved.

#include "UniverseGameMode.h"
#include "AstronomicalBodyActor.h"
#include "PlanetActor.h"
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

    // Search outward from the universe origin for a real generated system.
    // Nothing is hand-placed: whatever the generator says is there is what
    // gets built, so the scene is evidence of the generator's behaviour.
    const FUniversePosition SearchCentre;

    if (!FStarSystemGenerator::FindNearestSystem(
            Subsystem->GetSeedHierarchy(), SearchCentre, SystemSearchRadiusLightYears, ActiveSystem))
    {
        UE_LOG(LogUniverseGameMode, Warning,
            TEXT("No star system found within %.1f ly of the universe origin for seed \"%s\"."),
            SystemSearchRadiusLightYears, *UniverseSeedText);
        return;
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
    if (ActiveSystem.Planets.Num() > 0)
    {
        StreamingPlanetOrbitIndex = ActiveSystem.Planets.Num() - 1;

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
                    Surface, TerrainSettings,
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
        const FPlanetDescriptor& Target = ActiveSystem.Planets[ActiveSystem.Planets.Num() - 1];

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
