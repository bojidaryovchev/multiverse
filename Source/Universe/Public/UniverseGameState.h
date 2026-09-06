// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "UniverseNetTypes.h"
#include "UniverseGameState.generated.h"

/**
 * AUniverseGameState
 *
 * What every player in this universe agrees on.
 *
 * Two things live here and nothing else does:
 *
 * 1. **World identity.** Which universe this is and which rules generated it.
 *    Replicated once, checked by every client on receipt, and a mismatch is a
 *    refusal rather than a warning - see FUniverseWorldIdentity.
 *
 * 2. **The shared simulation clock.** One number that decides what time of day
 *    it is on every planet and what the weather is doing. Weather and day/night
 *    are already pure functions of (place, time) from Sprint 004, so making
 *    them coherent between players is not a replication problem at all: it is
 *    the problem of agreeing on `time`, which is this.
 *
 * Deliberately *not* here: anything about the universe's contents. Stars,
 * planets, terrain and biomes are regenerated identically at both ends from the
 * seed, and sending them would be sending data both sides can compute.
 */
UCLASS()
class UNIVERSE_API AUniverseGameState : public AGameStateBase
{
    GENERATED_BODY()

public:
    AUniverseGameState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void BeginPlay() override;

    // --- World identity -----------------------------------------------------

    /** Sets the identity. Server only; asserts on a client. */
    void SetWorldIdentity(const FUniverseWorldIdentity& Identity);

    const FUniverseWorldIdentity& GetWorldIdentity() const { return WorldIdentity; }

    /** True once the client has received and accepted the server's identity. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    bool IsWorldIdentityVerified() const { return bIdentityVerified; }

    /** Why the handshake failed, when it did. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    FString GetIdentityMismatchReason() const { return IdentityMismatchReason; }

    // --- Shared simulation clock -------------------------------------------

    /**
     * Seconds since the world began, on the server's clock.
     *
     * Replicated periodically rather than every frame, and advanced locally in
     * between. A clock that only moved when a packet arrived would make the sun
     * stutter; one that was never corrected would drift apart between players
     * over a session.
     */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    double GetUniverseTimeSeconds() const;

    /**
     * How far the local clock was from the server's at the last correction.
     *
     * Surfaced because it is the number that tells you whether the clock is
     * working. A drift that grows without bound is a clock that is not being
     * corrected; one that jumps is a clock being corrected too rarely.
     */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    double GetClockDriftSeconds() const { return LastClockDriftSeconds; }

    /** How often the server replicates the clock, in seconds. */
    UPROPERTY(EditDefaultsOnly, Category = "Universe|Net")
    double ClockReplicationIntervalSeconds = 5.0;

protected:
    UFUNCTION()
    void OnRep_WorldIdentity();

    UFUNCTION()
    void OnRep_ServerUniverseTime();

    UPROPERTY(ReplicatedUsing = OnRep_WorldIdentity)
    FUniverseWorldIdentity WorldIdentity;

    UPROPERTY(ReplicatedUsing = OnRep_ServerUniverseTime)
    double ServerUniverseTimeSeconds = 0.0;

private:
    /** The locally advanced clock, corrected by OnRep. */
    double LocalUniverseTimeSeconds = 0.0;

    double LastClockDriftSeconds = 0.0;

    double TimeSinceClockReplication = 0.0;

    bool bIdentityVerified = false;

    FString IdentityMismatchReason;
};
