// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UniverseCoordinates.h"
#include "StarSystemDescriptor.h"
#include "InterstellarJourneySubsystem.generated.h"

class AUniverseProbePawn;
class UStarSystemStreamingSubsystem;

/**
 * InterstellarJourneySubsystem.h
 *
 * The Sprint 006 acceptance run, driven from the console and verified as it
 * goes: leave a system, cross interstellar space, arrive at another one, and
 * come back to find the first exactly as it was.
 *
 *
 * WHY THIS IS A SUBSYSTEM AND NOT A TEST
 *
 * The claim being checked is that a journey between two stars can be made
 * continuously, with everything streaming in and out around the player, and
 * that the universe is unchanged by having been left. None of that is
 * expressible as a pure function, so none of it can live in the standalone
 * harness: it needs a running world, actors being created and destroyed, and
 * real elapsed time.
 *
 * What it can do is verify itself rather than leaving a human to read a log and
 * decide. Every stage has a deadline and a set of assertions, and the run ends
 * with a verdict. A journey that silently takes four minutes instead of forty
 * seconds has failed even though it arrived.
 *
 *
 * WHAT IT ACTUALLY PROVES
 *
 * The strongest assertion is the return leg. The home system's content hash is
 * recorded before departure and checked again on the way back, after the
 * streamer has destroyed and rebuilt every actor in it. If procedural
 * regeneration were order-dependent - a shared RNG, a cache keyed on visit
 * order - this is where it would show, and nothing short of actually leaving
 * and returning would catch it.
 */
UENUM()
enum class EInterstellarJourneyStage : uint8
{
    Idle,

    /** Waiting for the streamer to settle and the home system to activate. */
    Settling,

    /** Choosing a destination and recording what home looks like. */
    Departing,

    /** Under warp, between the stars. */
    Cruising,

    /** Arrived; checking that the right system is here and that it is real. */
    Arriving,

    /** Flying home again. */
    Returning,

    /** Home, and verifying that it regenerated identically. */
    Verifying,

    Complete,
    Failed,
};

UNIVERSE_API const TCHAR* LexToString(EInterstellarJourneyStage Stage);

UCLASS()
class UNIVERSE_API UInterstellarJourneySubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
    virtual bool IsTickable() const override { return Stage != EInterstellarJourneyStage::Idle; }

    /**
     * Starts the run.
     *
     * bReturnTrip adds the leg that proves the universe is unchanged by having
     * been left, which is the whole point; it is optional only because the
     * outbound leg alone is a useful shorter smoke test.
     */
    void BeginJourney(bool bReturnTrip);

    void AbortJourney(const FString& Reason);

    EInterstellarJourneyStage GetStage() const { return Stage; }

private:
    void EnterStage(EInterstellarJourneyStage NewStage, double DeadlineSeconds);

    /** Periodic "still going" line, so a two-minute leg is legible in a log. */
    void LogProgress(const AUniverseProbePawn& Probe, double ElapsedSeconds, const TCHAR* LegName);

    void Fail(const FString& Reason);
    void Report() const;

    double LastProgressLogSeconds = -1000.0;

    AUniverseProbePawn* GetProbe() const;
    UStarSystemStreamingSubsystem* GetStreamer() const;

    /** Points the ship at a destination and lets go of the controls. */
    bool StartLegTo(const FUniverseSystemId& Destination);

    EInterstellarJourneyStage Stage = EInterstellarJourneyStage::Idle;

    double TimeInStageSeconds = 0.0;
    double StageDeadlineSeconds = 0.0;

    bool bWantReturnTrip = false;

    // --- What was recorded before departure --------------------------------

    FUniverseSystemId HomeId;
    uint64 HomeContentHash = 0;
    FString HomeName;
    FUniversePosition HomePosition;

    FUniverseSystemId DestinationId;
    uint64 DestinationContentHashBefore = 0;
    FString DestinationName;
    FUniversePosition DestinationPosition;

    /** Straight-line distance of the outbound leg, light years. */
    double LegDistanceLightYears = 0.0;

    // --- Measurements ------------------------------------------------------

    double OutboundSeconds = 0.0;
    double ReturnSeconds = 0.0;

    double PeakSpeedMs = 0.0;

    int32 HazardStopsAtStart = 0;

    FString FailureReason;
};
