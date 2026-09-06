// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UniverseCoordinates.h"
#include "UniverseJourneySubsystem.generated.h"

class APlanetActor;
class APlanetCharacter;
class AUniverseProbePawn;
class UUniverseWorldSubsystem;

/**
 * The stages of the scripted journey, in order.
 *
 * Named rather than numbered because the log is the test result: a failure that
 * says "stalled in AtmosphericEntry" is diagnosable, and one that says "stalled
 * in stage 4" sends someone back to the source to count.
 */
UENUM()
enum class EUniverseJourneyStage : uint8
{
    Idle,
    /** Well outside the planet's influence, in the interstellar frame. */
    DeepSpace,
    /** Closing on the planet; the frame should be entered during this stage. */
    Approach,
    /** Inside the atmosphere, descending. */
    AtmosphericEntry,
    /** On the ground, at rest. */
    Landed,
    /** Out of the ship and standing on the surface. */
    Disembarked,
    /** Walking, including across a cube-face boundary. */
    Walking,
    /** Back in the ship. */
    Boarded,
    /** Climbing out; the frame should be left during this stage. */
    Departure,
    /** Finished. */
    Complete,
    /** Something did not happen in time. See the log for which. */
    Failed,
};

UNIVERSE_API const TCHAR* LexToString(EUniverseJourneyStage Stage);

/**
 * UUniverseJourneySubsystem
 *
 * Drives the full Sprint 003 journey unattended and checks it.
 *
 *
 * WHY THIS IS A SUBSYSTEM
 *
 * The journey changes which pawn the player possesses partway through - that is
 * most of the point of it - so it cannot live on either pawn. The probe stops
 * being possessed when the player steps out, and a state machine running on it
 * would either stop ticking or carry on driving something nobody is controlling.
 * A world subsystem outlives both, ticks throughout, and sees the possession
 * change as an event rather than as its own disappearance.
 *
 *
 * WHAT IT IS FOR
 *
 * Sprint 003 asks for the whole traversal - deep space to standing on the
 * ground and back - to work continuously, with no loading screen, no teleport
 * and no discontinuity. That is a claim about a sequence of events over
 * minutes, and it cannot be checked by a unit test: it needs a real world, real
 * streaming, real physics and real possession changes.
 *
 * So this runs the sequence and asserts the transitions happen, in order,
 * within a time budget. Each stage has an expectation and a deadline; missing
 * either fails the run with a reason. That makes the journey a repeatable
 * check that either passes or names what broke, rather than something a person
 * flies once and pronounces fine.
 *
 * It is not a substitute for playing it. It cannot see that the horizon looks
 * wrong. It can see that the frame was entered exactly once, that altitude
 * decreased monotonically through the descent, that the character ended up on
 * the ground rather than inside it, and that the ship was still where it was
 * left - which is the part a person is worst at checking.
 */
UCLASS()
class UNIVERSE_API UUniverseJourneySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual bool IsTickable() const override { return Stage != EUniverseJourneyStage::Idle; }

    /** Starts the journey. Restarts it if one is already running. */
    void BeginJourney();

    /** Stops it and reports where it got to. */
    void AbortJourney(const FString& Reason);

    EUniverseJourneyStage GetStage() const { return Stage; }

private:
    void EnterStage(EUniverseJourneyStage NewStage);
    void Fail(const FString& Reason);
    void Report() const;

    APlanetActor* GetPlanet() const;
    AUniverseProbePawn* GetProbe() const;
    APlanetCharacter* GetCharacter() const;
    UUniverseWorldSubsystem* GetUniverse() const;

    /** Current player position, whichever pawn is possessed. */
    bool TryGetObserverPosition(FUniversePosition& OutPosition) const;

    EUniverseJourneyStage Stage = EUniverseJourneyStage::Idle;

    double TimeInStageSeconds = 0.0;
    double TotalSeconds = 0.0;

    /** Deadline for the current stage. Exceeding it fails the run. */
    double StageDeadlineSeconds = 0.0;

    /** Where the ship was left, so it can be checked for having stayed there. */
    FUniversePosition ShipRestPosition;
    bool bHaveShipRestPosition = false;

    /** Drift is reported once on boarding, not on every tick of the stage. */
    bool bReportedShipDrift = false;

    /** Frame transitions counted when the journey started. */
    int32 FrameTransitionsAtStart = 0;

    /** Altitude at the previous tick, for the monotonic-descent check. */
    double PreviousAltitudeMeters = 0.0;

    /** Faults noticed but not fatal, reported at the end. */
    TArray<FString> Warnings;

    int32 DescentRegressions = 0;

    /** Which walk waypoint was last teleported to, so each fires once. */
    int32 LastWalkStep = -1;

    /** Furthest the character moved under movement input at any waypoint. */
    double WalkedMeters = 0.0;

    FUniversePosition WalkStartPosition;
    bool bHaveWalkStart = false;
};
