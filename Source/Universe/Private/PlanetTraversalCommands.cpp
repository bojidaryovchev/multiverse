// Copyright Universe Project. All Rights Reserved.

#include "PlanetActor.h"
#include "PlanetCharacter.h"
#include "PlanetSurfaceQuery.h"
#include "PlanetTerrain.h"
#include "UniverseAnchorComponent.h"
#include "UniverseGameMode.h"
#include "UniverseProbePawn.h"
#include "UniverseWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

/**
 * PlanetTraversalCommands.cpp
 *
 * Console commands for the Sprint 003 journey: land, step out, walk somewhere
 * specific, and run the whole thing unattended.
 *
 * These are debug tools, and the reason they are worth their weight is
 * repeatability. The full journey - orbit, descent, atmospheric entry, landing,
 * disembarking, walking, boarding, leaving - takes several minutes to fly by
 * hand and lands somewhere slightly different every time, which makes comparing
 * two runs impossible and makes a regression something you notice weeks later.
 * Driven from the console, it lands on the same square metre of the same
 * mountain every time, so a patch count or a frame time from today is directly
 * comparable with one from last week.
 *
 * They also make a headless run possible, which is how the long-traversal
 * behaviour is actually validated - see the sprint report.
 */

DEFINE_LOG_CATEGORY_STATIC(LogPlanetTraversal, Log, All);

namespace
{
    APlanetActor* FindFramePlanet(UWorld* World)
    {
        if (World == nullptr)
        {
            return nullptr;
        }

        // The frame planet first, since that is the body the simulation is
        // actually attached to. The game mode's streaming planet is the
        // fallback for the case where the player is too far out for any body
        // to have claimed them yet - which is exactly when a teleport toward
        // one is most useful.
        if (const UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
        {
            if (APlanetActor* Planet = Subsystem->GetFramePlanet())
            {
                return Planet;
            }
        }

        if (const AUniverseGameMode* GameMode = World->GetAuthGameMode<AUniverseGameMode>())
        {
            return GameMode->GetPlanetActor();
        }

        return nullptr;
    }

    /** The direction the player is currently over, or a stable default. */
    FVector3d GetCurrentDirection(const APlanetActor* Planet, UWorld* World)
    {
        const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

        FUniversePosition Position;
        bool bHave = false;

        if (const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
        {
            Position = Probe->GetUniversePosition();
            bHave = true;
        }
        else if (const APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
        {
            Position = Character->GetUniversePosition();
            bHave = true;
        }

        if (bHave)
        {
            const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Position);

            FVector3d Direction;
            if (FPlanetSurfaceQuery::TryGetDirection(Local, Direction))
            {
                return Direction;
            }
        }

        return Planet->GetSpawnDirection(0);
    }

    /**
     * Points a pawn along the local horizon.
     *
     * A teleport that leaves the camera pointing wherever it happened to be
     * before is disorienting on a planet, because "wherever it happened to be"
     * is defined relative to a local up that has just changed completely -
     * arriving on the far side of a world leaves the camera looking at the sky
     * or into the ground with no indication that anything is wrong. Facing the
     * horizon is the one orientation that means the same thing everywhere on
     * a sphere.
     */
    void LookAtHorizon(AActor* Actor, const FVector3d& UpDirection)
    {
        if (Actor == nullptr)
        {
            return;
        }

        const FVector Up(UpDirection.X, UpDirection.Y, UpDirection.Z);

        FVector3d TangentU;
        FVector3d TangentV;
        FPlanetTerrain::GetTangentBasis(UpDirection, TangentU, TangentV);

        const FVector Forward(TangentU.X, TangentU.Y, TangentU.Z);

        Actor->SetActorRotation(FRotationMatrix::MakeFromXZ(Forward, Up).Rotator());
    }
}

/**
 * universe.Land [spawnIndex]
 *
 * Puts the ship on the ground, landed and at rest.
 *
 * With no argument it lands directly below wherever the ship currently is, so
 * a descent that was going to take a minute can be finished immediately. With
 * an index it lands at that numbered spawn point, which is derived from the
 * planet seed and is therefore the same place on every run.
 */
static void UniverseLandCommand(const TArray<FString>& Args, UWorld* World)
{
    APlanetActor* Planet = FindFramePlanet(World);
    AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));

    if (Planet == nullptr || Probe == nullptr)
    {
        UE_LOG(LogPlanetTraversal, Warning, TEXT("universe.Land: no planet, or the ship is not possessed."));
        return;
    }

    const FVector3d Direction = (Args.Num() > 0)
        ? Planet->GetSpawnDirection(FCString::Atoi(*Args[0]))
        : GetCurrentDirection(Planet, World);

    const FUniversePosition Target =
        Planet->GetUniversePositionAboveTerrain(Direction, Probe->LandedClearanceMeters);

    Probe->FullStop();

    if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
    {
        Anchor->SetUniversePosition(Target);
    }

    // Land it properly rather than merely placing it at the right height: a
    // ship at rest on the ground but not flagged as landed would start falling
    // the moment gravity was integrated, and would report itself as flying to
    // anything that asked.
    Probe->ForceLanded();

    const FPlanetSurfaceSample Ground = Planet->SampleSurfaceBelow(Target);

    UE_LOG(LogPlanetTraversal, Log,
        TEXT("Landed on %s at elevation %+.0f m, %.1f m clearance."),
        *Planet->GetName(), Ground.ElevationMeters, Probe->LandedClearanceMeters);
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseLandCommand(
    TEXT("universe.Land"),
    TEXT("Debug: land the ship below its current position, or at a numbered spawn point. e.g. universe.Land 3"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseLandCommand));

/**
 * universe.ExitShip / universe.EnterShip
 *
 * The console equivalents of pressing F. Present so the whole journey can be
 * driven from -ExecCmds in a headless run, where there is nobody to press
 * anything.
 */
static void UniverseExitShipCommand(UWorld* World)
{
    if (AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
    {
        Probe->TryExitToSurface();
        return;
    }

    UE_LOG(LogPlanetTraversal, Warning, TEXT("universe.ExitShip: the ship is not possessed."));
}

static FAutoConsoleCommandWithWorld GUniverseExitShipCommand(
    TEXT("universe.ExitShip"),
    TEXT("Debug: step out of the landed ship onto the surface."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseExitShipCommand));

static void UniverseEnterShipCommand(UWorld* World)
{
    if (APlanetCharacter* Character = Cast<APlanetCharacter>(UGameplayStatics::GetPlayerPawn(World, 0)))
    {
        Character->TryEnterShip();
        return;
    }

    UE_LOG(LogPlanetTraversal, Warning, TEXT("universe.EnterShip: not currently on foot."));
}

static FAutoConsoleCommandWithWorld GUniverseEnterShipCommand(
    TEXT("universe.EnterShip"),
    TEXT("Debug: board the ship the character stepped out of."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseEnterShipCommand));

/**
 * universe.GotoSurface <spawnIndex> [heightAboveTerrain]
 *
 * Teleports whatever the player is controlling to a numbered point on the
 * surface.
 *
 * The spawn index is the important part. Indices are hashed from the planet
 * seed, so index 7 is the same place on the same planet on every machine and
 * in every run - which is what makes "the seam at index 7 is wrong" a
 * reproducible bug report rather than an anecdote. It is also how the cube-face
 * and corner crossings are exercised: the spread is uniform over the sphere, so
 * a handful of indices reliably includes points near edges and corners.
 */
static void UniverseGotoSurfaceCommand(const TArray<FString>& Args, UWorld* World)
{
    APlanetActor* Planet = FindFramePlanet(World);

    if (Planet == nullptr)
    {
        UE_LOG(LogPlanetTraversal, Warning, TEXT("universe.GotoSurface: no planet."));
        return;
    }

    const int32 Index = (Args.Num() > 0) ? FCString::Atoi(*Args[0]) : 0;
    const double Height = (Args.Num() > 1) ? FCString::Atod(*Args[1]) : 2.0;

    const FVector3d Direction = Planet->GetSpawnDirection(Index);
    const FUniversePosition Target = Planet->GetUniversePositionAboveTerrain(Direction, Height);

    APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

    if (APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
    {
        Character->PlaceOnSurface(Planet, Direction);
    }
    else if (AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
    {
        Probe->FullStop();

        if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
        {
            Anchor->SetUniversePosition(Target);
        }

        LookAtHorizon(Probe, Direction);
    }
    else
    {
        UE_LOG(LogPlanetTraversal, Warning, TEXT("universe.GotoSurface: nothing to move."));
        return;
    }

    const FPlanetSurfaceSample Ground = Planet->SampleSurfaceBelow(Target);

    UE_LOG(LogPlanetTraversal, Log,
        TEXT("Surface point %d: direction (%.4f, %.4f, %.4f), elevation %+.0f m, %.1f m above ground."),
        Index, Direction.X, Direction.Y, Direction.Z, Ground.ElevationMeters, Height);
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseGotoSurfaceCommand(
    TEXT("universe.GotoSurface"),
    TEXT("Debug: teleport to a seed-derived surface point. e.g. universe.GotoSurface 7 2"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseGotoSurfaceCommand));

/**
 * universe.FrameInfo
 *
 * Logs everything the simulation currently believes about where the player is.
 *
 * One command that prints the frame, all three altitudes, gravity and the
 * atmosphere together, because the questions being asked of it are always
 * comparative - "is the altitude I am reading the one I think it is" - and an
 * answer that requires three separate commands invites reading two of them from
 * different frames.
 */
static void UniverseFrameInfoCommand(UWorld* World)
{
    const UUniverseWorldSubsystem* Subsystem =
        (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;

    if (Subsystem == nullptr)
    {
        return;
    }

    const FUniverseFrameState& Frame = Subsystem->GetFrameState();
    const APlanetActor* Planet = Subsystem->GetFramePlanet();

    UE_LOG(LogPlanetTraversal, Log,
        TEXT("Frame: %s  dominance %.4f  transitions %d"),
        LexToString(Frame.Kind), Subsystem->GetFrameDominance(), Subsystem->GetFrameTransitionCount());

    if (Planet == nullptr)
    {
        UE_LOG(LogPlanetTraversal, Log, TEXT("  No planet attached: no gravity, no up, no altitude."));
        return;
    }

    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

    FUniversePosition Position = Subsystem->GetRenderOrigin();

    if (const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
    {
        Position = Probe->GetUniversePosition();
    }
    else if (const APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
    {
        Position = Character->GetUniversePosition();
    }

    const FPlanetSurfaceSample Ground = Planet->SampleSurfaceBelow(Position);
    const double FromCentre = Planet->GetDistanceFromCentreMeters(Position);
    const FVector3d Gravity = Planet->GetGravityAccelerationMs2(Position);
    const FPlanetFrameBounds& Bounds = Planet->GetFrameBounds();

    UE_LOG(LogPlanetTraversal, Log,
        TEXT("  Planet %s  r=%.1f km  enter %.1f km  leave %.1f km"),
        *Planet->GetName(),
        Planet->GetPlanetDescriptor().RadiusMeters / 1000.0,
        Bounds.EnterRadiusMeters / 1000.0,
        Bounds.ExitRadiusMeters / 1000.0);

    UE_LOG(LogPlanetTraversal, Log,
        TEXT("  From centre %.1f m   above sea level %.1f m   above terrain %.1f m   (ground %+.0f m)"),
        FromCentre,
        Planet->GetAltitudeAboveSeaLevelMeters(Position),
        FromCentre - Ground.SurfaceRadiusMeters,
        Ground.ElevationMeters);

    UE_LOG(LogPlanetTraversal, Log,
        TEXT("  Gravity %.4f m/s2 toward (%.4f, %.4f, %.4f)   atmosphere %.1f%%"),
        Gravity.Size(),
        Gravity.GetSafeNormal().X, Gravity.GetSafeNormal().Y, Gravity.GetSafeNormal().Z,
        Planet->GetAtmosphericDepthFraction(Position) * 100.0);
}

static FAutoConsoleCommandWithWorld GUniverseFrameInfoCommand(
    TEXT("universe.FrameInfo"),
    TEXT("Debug: log the simulation frame, all three altitudes, gravity and the atmosphere."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseFrameInfoCommand));

/**
 * universe.GotoSubstellar [heightAboveTerrain] [elevationDegrees]
 *
 * Teleports to the point where the star is directly overhead - local noon -
 * or, with an elevation angle, to a point where it sits that many degrees
 * above the horizon.
 *
 * Sprint 003 asks for day and night to be visible. Without a rotation model
 * they are a function of position rather than time, so being able to jump to a
 * chosen solar elevation is how that is checked: 90 degrees is noon, 0 is the
 * terminator, and a negative angle is night. It is also the only reliable way
 * to photograph the atmosphere, which is nearly black at a grazing sun and
 * blue overhead - a distinction that looks like a rendering failure until you
 * know which side of the planet you landed on.
 */
static void UniverseGotoSubstellarCommand(const TArray<FString>& Args, UWorld* World)
{
    APlanetActor* Planet = FindFramePlanet(World);

    if (Planet == nullptr)
    {
        UE_LOG(LogPlanetTraversal, Warning, TEXT("universe.GotoSubstellar: no planet."));
        return;
    }

    const double Height = (Args.Num() > 0) ? FCString::Atod(*Args[0]) : 2.0;
    const double ElevationDegrees = (Args.Num() > 1) ? FCString::Atod(*Args[1]) : 90.0;

    const FVector3d ToStar = Planet->GetStarDirection();

    // Rotate away from the sub-stellar point by (90 - elevation) degrees, about
    // an axis perpendicular to the star direction. Any perpendicular axis gives
    // the same solar elevation - the set of points at a given elevation is a
    // circle - so the tangent basis provides one rather than a chosen compass
    // direction, which would need a north this planet does not have.
    FVector3d TangentU;
    FVector3d TangentV;
    FPlanetTerrain::GetTangentBasis(ToStar, TangentU, TangentV);

    const double AngleRadians = FMath::DegreesToRadians(90.0 - ElevationDegrees);
    const double CosAngle = FMath::Cos(AngleRadians);
    const double SinAngle = FMath::Sin(AngleRadians);

    const FVector3d Direction(
        ToStar.X * CosAngle + TangentU.X * SinAngle,
        ToStar.Y * CosAngle + TangentU.Y * SinAngle,
        ToStar.Z * CosAngle + TangentU.Z * SinAngle);

    const FUniversePosition Target = Planet->GetUniversePositionAboveTerrain(Direction, Height);

    APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

    if (APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
    {
        Character->PlaceOnSurface(Planet, Direction);
    }
    else if (AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
    {
        Probe->FullStop();

        if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
        {
            Anchor->SetUniversePosition(Target);
        }

        Probe->ForceLanded();
        LookAtHorizon(Probe, Direction);
    }

    const FPlanetSurfaceSample Ground = Planet->SampleSurfaceBelow(Target);

    UE_LOG(LogPlanetTraversal, Log,
        TEXT("Sun %.1f degrees above the horizon. Ground elevation %+.0f m, %.1f m above it."),
        ElevationDegrees, Ground.ElevationMeters, Height);
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseGotoSubstellarCommand(
    TEXT("universe.GotoSubstellar"),
    TEXT("Debug: teleport to a chosen solar elevation. e.g. universe.GotoSubstellar 2 60"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseGotoSubstellarCommand));
