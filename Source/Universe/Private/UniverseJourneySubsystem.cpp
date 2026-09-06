// Copyright Universe Project. All Rights Reserved.

#include "UniverseJourneySubsystem.h"
#include "PlanetActor.h"
#include "PlanetCharacter.h"
#include "PlanetSurfaceQuery.h"
#include "UniverseAnchorComponent.h"
#include "UniverseGameMode.h"
#include "UniverseProbePawn.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "UniverseWorldSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverseJourney, Log, All);

namespace
{
    /**
     * Seconds allowed for each stage.
     *
     * Generous, and deliberately so. These are deadlines for detecting a
     * *stall* - a frame that never gets entered, a landing that never
     * completes, a character held forever waiting for collision that will
     * never arrive - not performance targets. A tight budget here would turn
     * a slow machine into a test failure, which teaches everyone to ignore the
     * test.
     */
    constexpr double DeepSpaceSeconds = 3.0;
    constexpr double ApproachSeconds = 60.0;
    constexpr double EntrySeconds = 60.0;
    constexpr double LandedSeconds = 10.0;
    constexpr double DisembarkSeconds = 15.0;
    /**
     * The whole walking stage: six waypoints, each allowed up to eight seconds
     * to get cooked collision under the character and then walk on it.
     *
     * Twenty seconds was enough while waypoints advanced on a two-second clock
     * and not enough once they wait for the ground to become real - which they
     * have to, because how long that takes is a property of the planet.
     */
    constexpr double WalkSeconds = 60.0;
    constexpr double BoardSeconds = 15.0;
    constexpr double DepartureSeconds = 60.0;

    /** How far out the journey starts, in planet radii. */
    constexpr double StartDistanceRadii = 8.0;

    /** Altitude at which the descent is considered to have begun, in metres. */
    constexpr double EntryAltitudeMeters = 60000.0;
}

const TCHAR* LexToString(EUniverseJourneyStage Stage)
{
    switch (Stage)
    {
    case EUniverseJourneyStage::Idle:             return TEXT("Idle");
    case EUniverseJourneyStage::WaitingForPlanet: return TEXT("WaitingForPlanet");
    case EUniverseJourneyStage::DeepSpace:        return TEXT("DeepSpace");
    case EUniverseJourneyStage::Approach:         return TEXT("Approach");
    case EUniverseJourneyStage::AtmosphericEntry: return TEXT("AtmosphericEntry");
    case EUniverseJourneyStage::Landed:           return TEXT("Landed");
    case EUniverseJourneyStage::Disembarked:      return TEXT("Disembarked");
    case EUniverseJourneyStage::Walking:          return TEXT("Walking");
    case EUniverseJourneyStage::Boarded:          return TEXT("Boarded");
    case EUniverseJourneyStage::Departure:        return TEXT("Departure");
    case EUniverseJourneyStage::Complete:         return TEXT("Complete");
    case EUniverseJourneyStage::Failed:           return TEXT("Failed");
    default:                                      return TEXT("Unknown");
    }
}

TStatId UUniverseJourneySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UUniverseJourneySubsystem, STATGROUP_Tickables);
}

bool UUniverseJourneySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

APlanetActor* UUniverseJourneySubsystem::GetPlanet() const
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return nullptr;
    }

    if (const UUniverseWorldSubsystem* Universe = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        if (APlanetActor* Planet = Universe->GetFramePlanet())
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

AUniverseProbePawn* UUniverseJourneySubsystem::GetProbe() const
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return nullptr;
    }

    // The possessed pawn first; if the player is on foot, the ship they came
    // out of. Both are needed at different stages and the journey has to be
    // able to find the ship while walking away from it.
    APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

    if (AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
    {
        return Probe;
    }

    if (const APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
    {
        return Character->GetShipToReenter();
    }

    return nullptr;
}

APlanetCharacter* UUniverseJourneySubsystem::GetCharacter() const
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return nullptr;
    }

    APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

    if (APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
    {
        return Character;
    }

    if (const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
    {
        return Probe->GetDisembarkedCharacter();
    }

    return nullptr;
}

UUniverseWorldSubsystem* UUniverseJourneySubsystem::GetUniverse() const
{
    UWorld* World = GetWorld();
    return (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;
}

bool UUniverseJourneySubsystem::TryGetObserverPosition(FUniversePosition& OutPosition) const
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return false;
    }

    const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

    if (const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
    {
        OutPosition = Probe->GetUniversePosition();
        return true;
    }

    if (const APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
    {
        OutPosition = Character->GetUniversePosition();
        return true;
    }

    return false;
}

void UUniverseJourneySubsystem::BeginJourney()
{
    AUniverseProbePawn* Probe = GetProbe();

    if (Probe == nullptr)
    {
        UE_LOG(LogUniverseJourney, Error, TEXT("Cannot start: no ship."));
        return;
    }

    Warnings.Reset();
    TotalSeconds = 0.0;
    DescentRegressions = 0;
    LastWalkStep = -1;
    WalkedMeters = 0.0;
    bHaveWalkStart = false;
    bHaveShipRestPosition = false;
    bReportedShipDrift = false;

    const UUniverseWorldSubsystem* Universe = GetUniverse();
    FrameTransitionsAtStart = (Universe != nullptr) ? Universe->GetFrameTransitionCount() : 0;

    // A planet may not exist yet: since Sprint 006 they are streamed in rather
    // than spawned before play begins, so a command issued on the first frame
    // legitimately runs before there is anything to fly to.
    if (APlanetActor* Planet = GetPlanet())
    {
        StartOnPlanet(*Planet, *Probe);
        return;
    }

    UE_LOG(LogUniverseJourney, Log,
        TEXT("=== Journey queued. Waiting for a planet to stream in. ==="));

    EnterStage(EUniverseJourneyStage::WaitingForPlanet);
}

void UUniverseJourneySubsystem::StartOnPlanet(APlanetActor& PlanetRef, AUniverseProbePawn& ProbeRef)
{
    APlanetActor* Planet = &PlanetRef;
    AUniverseProbePawn* Probe = &ProbeRef;

    // Start well outside the influence radius, so entering the frame is
    // something the journey observes happening rather than something it starts
    // having already happened.
    const FPlanetSurfaceDescriptor& Descriptor = Planet->GetPlanetDescriptor();
    const FVector3d Direction = Planet->GetSpawnDirection(0);
    const double StartRadius = Descriptor.RadiusMeters * StartDistanceRadii;

    const FUniversePosition Start = Planet->PlanetLocalMetersToUniverse(FVector3d(
        Direction.X * StartRadius, Direction.Y * StartRadius, Direction.Z * StartRadius));

    Probe->FullStop();

    if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
    {
        Anchor->SetUniversePosition(Start);
    }

    UE_LOG(LogUniverseJourney, Log,
        TEXT("=== Journey begins. %s, r=%.1f km, starting %.1f km out (%.1f radii). ==="),
        *Planet->GetName(),
        Descriptor.RadiusMeters / 1000.0,
        StartRadius / 1000.0,
        StartDistanceRadii);

    EnterStage(EUniverseJourneyStage::DeepSpace);
}

void UUniverseJourneySubsystem::EnterStage(EUniverseJourneyStage NewStage)
{
    Stage = NewStage;
    TimeInStageSeconds = 0.0;

    switch (NewStage)
    {
    case EUniverseJourneyStage::DeepSpace:        StageDeadlineSeconds = DeepSpaceSeconds; break;
    case EUniverseJourneyStage::Approach:         StageDeadlineSeconds = ApproachSeconds; break;
    case EUniverseJourneyStage::AtmosphericEntry: StageDeadlineSeconds = EntrySeconds; break;
    case EUniverseJourneyStage::Landed:           StageDeadlineSeconds = LandedSeconds; break;
    case EUniverseJourneyStage::Disembarked:      StageDeadlineSeconds = DisembarkSeconds; break;
    case EUniverseJourneyStage::Walking:
        StageDeadlineSeconds = WalkSeconds;
        PendingWalkStep = 0;
        WalkSecondsAtWaypoint = 0.0;
        WaypointStartedAtSeconds = 0.0;
        break;
    case EUniverseJourneyStage::Boarded:          StageDeadlineSeconds = BoardSeconds; break;
    case EUniverseJourneyStage::Departure:        StageDeadlineSeconds = DepartureSeconds; break;
    default:                                      StageDeadlineSeconds = 0.0; break;
    }

    UE_LOG(LogUniverseJourney, Log, TEXT("[%6.1fs] -> %s"), TotalSeconds, LexToString(NewStage));
}

void UUniverseJourneySubsystem::Fail(const FString& Reason)
{
    UE_LOG(LogUniverseJourney, Error,
        TEXT("JOURNEY FAILED in %s after %.1fs: %s"), LexToString(Stage), TotalSeconds, *Reason);

    Stage = EUniverseJourneyStage::Failed;
    Report();
}

void UUniverseJourneySubsystem::AbortJourney(const FString& Reason)
{
    if (Stage == EUniverseJourneyStage::Idle)
    {
        return;
    }

    Fail(Reason);
}

void UUniverseJourneySubsystem::Report() const
{
    UE_LOG(LogUniverseJourney, Log, TEXT("--- Journey report ---"));
    UE_LOG(LogUniverseJourney, Log, TEXT("  Final stage      : %s"), LexToString(Stage));
    UE_LOG(LogUniverseJourney, Log, TEXT("  Elapsed          : %.1f s"), TotalSeconds);

    if (const UUniverseWorldSubsystem* Universe = GetUniverse())
    {
        UE_LOG(LogUniverseJourney, Log,
            TEXT("  Frame transitions: %d (expected 2: one in, one out)"),
            Universe->GetFrameTransitionCount() - FrameTransitionsAtStart);

        UE_LOG(LogUniverseJourney, Log,
            TEXT("  Origin rebases   : %d"), Universe->GetRebaseCount());
    }

    UE_LOG(LogUniverseJourney, Log,
        TEXT("  Descent regressions: %d (altitude increasing during descent)"), DescentRegressions);

    for (const FString& Warning : Warnings)
    {
        UE_LOG(LogUniverseJourney, Warning, TEXT("  ! %s"), *Warning);
    }

    UE_LOG(LogUniverseJourney, Log,
        TEXT("--- %s ---"),
        (Stage == EUniverseJourneyStage::Complete && Warnings.Num() == 0)
            ? TEXT("PASS")
            : (Stage == EUniverseJourneyStage::Complete ? TEXT("PASS WITH WARNINGS") : TEXT("FAIL")));
}

void UUniverseJourneySubsystem::Tick(float DeltaTime)
{
    if (Stage == EUniverseJourneyStage::Idle
        || Stage == EUniverseJourneyStage::Complete
        || Stage == EUniverseJourneyStage::Failed)
    {
        return;
    }

    const double Dt = static_cast<double>(DeltaTime);
    TimeInStageSeconds += Dt;
    TotalSeconds += Dt;

    APlanetActor* Planet = GetPlanet();
    UUniverseWorldSubsystem* Universe = GetUniverse();

    // --- Waiting for the world -------------------------------------------
    //
    // Handled before the null check below, because "there is no planet yet" is
    // the normal state of this stage rather than a failure.
    if (Stage == EUniverseJourneyStage::WaitingForPlanet)
    {
        AUniverseProbePawn* WaitingProbe = GetProbe();

        if (Planet != nullptr && WaitingProbe != nullptr)
        {
            StartOnPlanet(*Planet, *WaitingProbe);
            return;
        }

        if (TimeInStageSeconds > 30.0)
        {
            Fail(TEXT("no planet streamed in within 30 s."));
        }

        return;
    }

    if (Planet == nullptr || Universe == nullptr)
    {
        Fail(TEXT("the planet or the universe subsystem disappeared."));
        return;
    }

    if (StageDeadlineSeconds > 0.0 && TimeInStageSeconds > StageDeadlineSeconds)
    {
        Fail(FString::Printf(TEXT("stage exceeded its %.0f s deadline."), StageDeadlineSeconds));
        return;
    }

    FUniversePosition Observer;

    if (!TryGetObserverPosition(Observer))
    {
        Fail(TEXT("nothing is possessed."));
        return;
    }

    const double FromCentre = Planet->GetDistanceFromCentreMeters(Observer);
    const double AboveSeaLevel = Planet->GetAltitudeAboveSeaLevelMeters(Observer);
    const bool bPlanetary = Universe->IsInPlanetaryFrame();

    AUniverseProbePawn* Probe = GetProbe();

    switch (Stage)
    {
    case EUniverseJourneyStage::DeepSpace:
    {
        // Starting outside the influence radius, the frame must be
        // interstellar. If it is not, the enter radius is wrong or the
        // selector is attaching to something it should not be.
        if (bPlanetary)
        {
            Warnings.Add(FString::Printf(
                TEXT("Already in the planetary frame at %.1f km from centre (enter radius %.1f km)."),
                FromCentre / 1000.0, Planet->GetFrameBounds().EnterRadiusMeters / 1000.0));
        }

        if (TimeInStageSeconds > 1.0)
        {
            PreviousAltitudeMeters = AboveSeaLevel;
            EnterStage(EUniverseJourneyStage::Approach);
        }
        break;
    }

    case EUniverseJourneyStage::Approach:
    {
        // Close at a rate proportional to the distance left, rather than at a
        // fixed speed.
        //
        // A fixed speed cannot work across the range this has to cover. The
        // approach starts eight planet radii out - forty thousand kilometres
        // here, and proportionally more on a gas giant - and ends sixty
        // kilometres up. One speed that crosses the first distance in a
        // reasonable time arrives at the second having covered several planet
        // diameters in a single frame; one slow enough to arrive gently never
        // finishes. A proportional rate is scale-free: it decays toward the
        // target, is fast where there is room and slow where there is not, and
        // its per-frame step is always a small fraction of the distance
        // remaining, so it can never step past the planet.
        if (Probe != nullptr)
        {
            const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Observer);
            const FVector3d Down = FPlanetSurfaceQuery::GetLocalUp(Local) * -1.0;

            constexpr double ApproachTimeConstantSeconds = 6.0;
            constexpr double MinimumApproachSpeedMs = 2000.0;

            const double TargetRadius =
                Planet->GetPlanetDescriptor().RadiusMeters + EntryAltitudeMeters;

            const double Remaining = FMath::Max(FromCentre - TargetRadius, 0.0);
            const double Speed =
                FMath::Max(Remaining / ApproachTimeConstantSeconds, MinimumApproachSpeedMs);

            Probe->SetUniverseVelocity(Down * Speed);
        }

        // The frame must be entered on the way in. This is the transition
        // Sprint 003 exists to make explicit, so the journey waits for it
        // rather than assuming it.
        if (AboveSeaLevel < EntryAltitudeMeters)
        {
            if (!bPlanetary)
            {
                Fail(FString::Printf(
                    TEXT("still interstellar at %.1f km altitude - the frame was never entered."),
                    AboveSeaLevel / 1000.0));
                return;
            }

            PreviousAltitudeMeters = AboveSeaLevel;
            EnterStage(EUniverseJourneyStage::AtmosphericEntry);
        }
        break;
    }

    case EUniverseJourneyStage::AtmosphericEntry:
    {
        // Altitude must decrease. A rise means the ship bounced off the
        // collision clamp or gravity is pointing the wrong way, both of which
        // are silent failures that a "did it land" check would miss.
        if (AboveSeaLevel > PreviousAltitudeMeters + 1.0)
        {
            ++DescentRegressions;
        }

        PreviousAltitudeMeters = AboveSeaLevel;

        // Finish the descent with the teleport rather than flying all the way
        // down. The descent itself is what is being observed; the last few
        // hundred metres of it add a minute to the run and prove nothing the
        // first ten kilometres did not.
        if (AboveSeaLevel < 2000.0 || TimeInStageSeconds > EntrySeconds * 0.6)
        {
            if (Probe != nullptr)
            {
                Probe->FullStop();

                const FVector3d Direction = Planet->GetSpawnDirection(0);
                const FUniversePosition Rest = Planet->GetUniversePositionAboveTerrain(
                    Direction, Probe->LandedClearanceMeters);

                if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
                {
                    Anchor->SetUniversePosition(Rest);
                }

                Probe->ForceLanded();

                ShipRestPosition = Rest;
                bHaveShipRestPosition = true;
            }

            EnterStage(EUniverseJourneyStage::Landed);
        }
        break;
    }

    case EUniverseJourneyStage::Landed:
    {
        if (Probe == nullptr || !Probe->IsLanded())
        {
            Fail(TEXT("the ship did not settle as landed."));
            return;
        }

        // Give streaming a moment to cook collision under the ship before
        // stepping out, so the character is not immediately held.
        if (TimeInStageSeconds > 3.0)
        {
            if (!Probe->TryExitToSurface())
            {
                Fail(TEXT("could not step out of the landed ship."));
                return;
            }

            EnterStage(EUniverseJourneyStage::Disembarked);
        }
        break;
    }

    case EUniverseJourneyStage::Disembarked:
    {
        const APlanetCharacter* Character = GetCharacter();

        if (Character == nullptr)
        {
            Fail(TEXT("no character after stepping out."));
            return;
        }

        // Wait for the character to be standing on real collision rather than
        // being held. Being held is the correct behaviour while the floor is
        // being cooked, but it has to end.
        if (!Character->IsWaitingForCollision() && TimeInStageSeconds > 2.0)
        {
            const double AboveTerrain = Character->GetAltitudeAboveTerrainMeters();

            // Standing on the ground, not inside it and not floating above it.
            // The tolerance is a capsule half-height plus slack, because the
            // capsule centre is what is being measured.
            if (AboveTerrain < -5.0)
            {
                Warnings.Add(FString::Printf(
                    TEXT("Character is %.1f m below the terrain surface."), -AboveTerrain));
            }

            EnterStage(EUniverseJourneyStage::Walking);
        }
        break;
    }

    case EUniverseJourneyStage::Walking:
    {
        // Walking is exercised by teleporting between surface points rather
        // than by driving input, because what is being checked is that
        // arbitrary points on the sphere are stood on correctly - including
        // across cube faces - and driving a character on foot to a different
        // cube face would take hours of real time.
        APlanetCharacter* Character = GetCharacter();

        if (Character == nullptr)
        {
            Fail(TEXT("the character disappeared while walking."));
            return;
        }

        // --- Waypoint pacing --------------------------------------------
        //
        // Advanced on a condition rather than a clock. The original version
        // moved on every two seconds, which worked on the planet the demo
        // happened to start beside and silently stopped working the moment
        // Sprint 006 changed that: a teleport lands somewhere with no cooked
        // collision, the character is held until a patch is built there, and on
        // a planet two and a half times larger that takes longer than the two
        // seconds it was given. Every waypoint then reported zero metres
        // walked - not because walking was broken, but because the test never
        // let it start.
        //
        // So a waypoint now ends when the character has actually walked at it,
        // or when it has clearly failed to, and the deadline is what catches
        // the second case.
        if (WalkSecondsAtWaypoint > 0.0)
        {
            WalkSecondsAtWaypoint += Dt;
        }

        const bool bWaypointDone =
            (WalkSecondsAtWaypoint > 1.5)
            || (TimeInStageSeconds - WaypointStartedAtSeconds > 8.0);

        if (bWaypointDone && LastWalkStep >= 0)
        {
            ++PendingWalkStep;
            WalkSecondsAtWaypoint = 0.0;
            WaypointStartedAtSeconds = TimeInStageSeconds;
        }

        const int32 Step = PendingWalkStep;

        if (Step < 6)
        {
            // A member rather than a function-local static: a static would
            // persist across journey runs, so the second run in a session
            // would skip whichever steps the first happened to end on.
            if (Step != LastWalkStep)
            {
                LastWalkStep = Step;
                WalkStartPosition = Character->GetUniversePosition();
                bHaveWalkStart = false;
                WalkSecondsAtWaypoint = 0.0;
                WaypointStartedAtSeconds = TimeInStageSeconds;

                // Spread over the sphere, so the sequence crosses cube faces
                // and passes near at least one corner.
                Character->PlaceOnSurface(Character->GetFramePlanet(),
                    Planet->GetSpawnDirection(Step * 7 + 1));
            }
            else
            {
                // Between teleports, actually walk.
                //
                // Teleporting alone would prove that arbitrary points on the
                // sphere can be stood on, which is worth proving, but it would
                // never run the movement component - and the movement component
                // is where custom gravity, floor sweeps and the tangent-plane
                // basis all have to agree. Driving real input for a second at
                // each waypoint exercises that, at six points spread over the
                // whole planet including across cube faces.
                if (!Character->IsWaitingForCollision())
                {
                    if (!bHaveWalkStart)
                    {
                        bHaveWalkStart = true;
                        WalkStartPosition = Character->GetUniversePosition();
                    }

                    // Only counts from the frame the floor became real.
                    WalkSecondsAtWaypoint += Dt;

                    Character->AddMovementInput(Character->GetActorForwardVector(), 1.0f);

                    if (const UCharacterMovementComponent* Move = Character->GetCharacterMovement())
                    {
                        UE_LOG(LogUniverseJourney, Verbose,
                            TEXT("  waypoint %d: mode=%d speed=%.1f cm/s floor=%d walking for %.1f s"),
                            Step,
                            static_cast<int32>(Move->MovementMode),
                            Move->Velocity.Size(),
                            Move->CurrentFloor.bBlockingHit ? 1 : 0,
                            WalkSecondsAtWaypoint);
                    }

                    // Standing on the ground, not sinking into it and not
                    // launched off it. The band is a capsule half-height either
                    // side, which is the resolution this can meaningfully
                    // assert - the capsule centre is what is being measured.
                    const double AboveTerrain =
                        static_cast<double>(Character->GetAltitudeAboveTerrainMeters());

                    if (AboveTerrain < -3.0 || AboveTerrain > 20.0)
                    {
                        Warnings.Add(FString::Printf(
                            TEXT("Character %.1f m from the ground at waypoint %d."),
                            AboveTerrain, Step));
                    }

                    const double Walked = FUniversePosition::DistanceMeters(
                        WalkStartPosition, Character->GetUniversePosition());

                    WalkedMeters = FMath::Max(WalkedMeters, Walked);
                }
            }
        }
        else
        {
            // Return to the ship and board it.
            Character->PlaceOnSurface(Planet, Planet->GetSpawnDirection(0));

            UE_LOG(LogUniverseJourney, Log,
                TEXT("  Furthest walked under own power at a waypoint: %.2f m"), WalkedMeters);

            if (WalkedMeters < 0.5)
            {
                Warnings.Add(TEXT("The character never moved under movement input."));
            }

            if (!Character->TryEnterShip())
            {
                Fail(TEXT("could not board the ship after walking back to it."));
                return;
            }

            EnterStage(EUniverseJourneyStage::Boarded);
        }
        break;
    }

    case EUniverseJourneyStage::Boarded:
    {
        if (Probe == nullptr)
        {
            Fail(TEXT("no ship after boarding."));
            return;
        }

        // The ship must be exactly where it was left. This is the check a
        // person is worst at: a craft that drifts a few metres per minute
        // while unattended looks fine on any single frame and is somewhere
        // else entirely after a long excursion.
        if (bHaveShipRestPosition && !bReportedShipDrift)
        {
            bReportedShipDrift = true;

            const double DriftMeters =
                FUniversePosition::DistanceMeters(ShipRestPosition, Probe->GetUniversePosition());

            UE_LOG(LogUniverseJourney, Log,
                TEXT("  Ship drift while unattended: %.6f m"), DriftMeters);

            if (DriftMeters > 0.01)
            {
                Warnings.Add(FString::Printf(
                    TEXT("Ship moved %.4f m while the player was away."), DriftMeters));
            }
        }

        if (TimeInStageSeconds > 2.0)
        {
            EnterStage(EUniverseJourneyStage::Departure);
        }
        break;
    }

    case EUniverseJourneyStage::Departure:
    {
        // The frame must be left on the way out, and only after passing the
        // exit radius - which is further than the enter radius. Leaving at the
        // enter radius would mean the hysteresis is not working.
        const FPlanetFrameBounds& Bounds = Planet->GetFrameBounds();

        // Climb out at a proportional rate too, aiming comfortably past the
        // exit radius. Symmetry with the approach is not cosmetic: the same
        // argument about a fixed speed applies, and the ascent has to clear a
        // radius that scales with the planet.
        if (Probe != nullptr && bPlanetary)
        {
            const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Observer);
            const FVector3d Up = FPlanetSurfaceQuery::GetLocalUp(Local);

            constexpr double AscentTimeConstantSeconds = 5.0;
            constexpr double MinimumAscentSpeedMs = 5000.0;

            const double TargetRadius = Bounds.ExitRadiusMeters * 1.2;
            const double Remaining = FMath::Max(TargetRadius - FromCentre, 0.0);
            const double Speed =
                FMath::Max(Remaining / AscentTimeConstantSeconds, MinimumAscentSpeedMs);

            Probe->SetUniverseVelocity(Up * Speed);
        }

        if (!bPlanetary)
        {
            if (FromCentre < Bounds.EnterRadiusMeters)
            {
                Warnings.Add(FString::Printf(
                    TEXT("Frame was released at %.1f km, inside the enter radius of %.1f km."),
                    FromCentre / 1000.0, Bounds.EnterRadiusMeters / 1000.0));
            }

            UE_LOG(LogUniverseJourney, Log,
                TEXT("  Frame released at %.1f km from centre (exit radius %.1f km)."),
                FromCentre / 1000.0, Bounds.ExitRadiusMeters / 1000.0);

            if (Probe != nullptr)
            {
                Probe->FullStop();
            }

            Stage = EUniverseJourneyStage::Complete;

            UE_LOG(LogUniverseJourney, Log, TEXT("=== Journey complete. ==="));
            Report();
        }
        break;
    }

    default:
        break;
    }
}

/**
 * universe.Journey
 *
 * Runs the whole thing. Written to be usable from -ExecCmds so the journey can
 * be checked in a headless run without anyone flying it.
 */
static void UniverseJourneyCommand(UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    if (UUniverseJourneySubsystem* Journey = World->GetSubsystem<UUniverseJourneySubsystem>())
    {
        Journey->BeginJourney();
    }
}

static FAutoConsoleCommandWithWorld GUniverseJourneyCommand(
    TEXT("universe.Journey"),
    TEXT("Debug: run the scripted deep space -> descent -> landing -> walk -> departure journey and check it."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseJourneyCommand));
