// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UniverseCoordinates.h"
#include "StarSystemDescriptor.h"
#include "WorldPersistenceIdentity.h"
#include "GoldenPathSubsystem.generated.h"

class APlanetCharacter;
class AUniverseProbePawn;
class UStarSystemStreamingSubsystem;

/**
 * GoldenPathSubsystem.h
 *
 * The whole game, once, unattended, with a verdict.
 *
 *
 * WHY ONE TEST AND NOT TWENTY
 *
 * By Sprint 008 this project has 79 unit tests, two scripted journeys and two
 * multiplayer scripts, and between them they cover every subsystem. What none
 * of them covers is *the session*: launch, fly, warp, arrive, land, get out,
 * walk, build, chop, get back in, leave, come back, find it all still there.
 *
 * That sequence is the product. Every individual step passing while the
 * sequence fails is a completely realistic outcome - it is what happens when
 * two subsystems each work and disagree about a handoff - and it is exactly
 * what a release gate has to catch.
 *
 * So this is one long test rather than several short ones, and it is the one
 * that must pass before a build goes to anybody else.
 *
 *
 * WHAT MAKES IT A TEST
 *
 * Every stage has a deadline and a set of assertions, and the run ends with
 * PASS or FAIL and a reason. It records what it did - distance flown, time
 * warped, what it built - so that two runs can be compared rather than merely
 * both saying "PASS".
 *
 * The persistence half needs two processes, so it runs in two parts:
 * `universe.GoldenPath` does the session and writes what it did; a second run
 * with `universe.GoldenPath verify` restarts, returns, and checks it is all
 * still there.
 */
UENUM()
enum class EGoldenPathStage : uint8
{
    Idle,

    /** Waiting for the universe to stream in around the player. */
    Waking,

    /** In space, flying under normal thrust. */
    Flying,

    /** Under warp, crossing interstellar space to another system. */
    Warping,

    /** Arrived; checking the destination is real and different. */
    Arrived,

    /** Descending to the surface. */
    Landing,

    /** On the ground, in the ship. */
    Landed,

    /** Out of the ship, on foot. */
    OnFoot,

    /** Building and clearing. */
    Working,

    /** Back in the ship. */
    Aboard,

    /** Climbing away from the planet. */
    Leaving,

    /** Writing down what happened, for the verify run. */
    Recording,

    Complete,
    Failed,
};

UNIVERSE_API const TCHAR* LexToString(EGoldenPathStage Stage);

UCLASS()
class UNIVERSE_API UGoldenPathSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual bool IsTickable() const override { return Stage != EGoldenPathStage::Idle; }

    /** Runs the session. */
    void Begin();

    /**
     * Checks that a previous run's changes are still there.
     *
     * A separate entry point rather than a stage, because the whole point is
     * that it happens in a *different process*. A verification that ran in the
     * same one would prove only that memory still holds what was put in it.
     */
    void BeginVerify();

    void Abort(const FString& Reason);

    EGoldenPathStage GetStage() const { return Stage; }

private:
    void EnterStage(EGoldenPathStage NewStage, double DeadlineSeconds);
    void Fail(const FString& Reason);
    void Report() const;

    /** Writes what this run did, for a later verify run to check. */
    void RecordOutcome();

    /** Reads back a previous run's record. Returns false if there is none. */
    bool LoadOutcome();

    AUniverseProbePawn* GetProbe() const;
    APlanetCharacter* GetCharacter() const;
    UStarSystemStreamingSubsystem* GetStreamer() const;

    EGoldenPathStage Stage = EGoldenPathStage::Idle;

    double TimeInStageSeconds = 0.0;
    double StageDeadlineSeconds = 0.0;
    double TotalSeconds = 0.0;

    bool bVerifyOnly = false;

    // --- What happened -------------------------------------------------------

    FUniverseSystemId HomeSystemId;
    FString HomeSystemName;

    FUniverseSystemId DestinationSystemId;
    FString DestinationSystemName;
    uint64 DestinationContentHash = 0;

    uint64 PlanetKey = 0;
    FString PlanetName;

    /** The structure this run placed, and where. */
    FPersistentEntityId BuiltId;
    FPersistenceRegionId BuiltRegion;

    /** How many procedural entities were removed. */
    int32 RemovedCount = 0;

    double WarpDistanceLightYears = 0.0;
    double WarpSeconds = 0.0;

    FString FailureReason;

    TArray<FString> Warnings;
};
