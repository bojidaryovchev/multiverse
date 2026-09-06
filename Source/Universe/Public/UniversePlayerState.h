// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "UniverseNetTypes.h"
#include "StarSystemDescriptor.h"
#include "UniversePlayerState.generated.h"

/**
 * AUniversePlayerState
 *
 * Where a player is, in the coordinate system everybody agrees on.
 *
 *
 * WHY THE POSITION LIVES HERE AND NOT ON THE PAWN
 *
 * An Actor's replicated transform is expressed in *someone's* render space, and
 * in this project no two clients share one: each rebases the world origin around
 * its own viewpoint, potentially thousands of times a minute. Replicating a
 * pawn's transform would mean each client placing the other wherever its own
 * origin happened to be, which is wrong by however far apart the two origins
 * have drifted.
 *
 * The canonical position is the only thing both ends can agree on, so that is
 * what crosses the wire. Each client converts it into its own render space on
 * receipt, and both are right.
 *
 * That also makes the player state - which survives pawn changes - the natural
 * owner. Boarding a ship destroys and replaces the pawn; it does not move the
 * player, and nothing about where they are should have to be re-established
 * because their vehicle changed.
 *
 *
 * PLAYER IDENTITY IS NOT CONNECTION IDENTITY
 *
 * PlayerId is a connection-lifetime integer that Unreal reuses. PersistentId is
 * a stable string that survives a disconnect, so that reconnecting returns a
 * player to where they were rather than to the spawn point. The two are
 * deliberately separate: everything durable keys on PersistentId, and nothing
 * durable keys on the connection.
 *
 *
 * AUTHORITY
 *
 * The server owns this. A client proposes a position through
 * AUniversePlayerController::ServerUpdatePosition and the server decides
 * whether to believe it - see MovementValidation in Networking.md. Nothing a
 * client writes here directly is replicated to anybody.
 */
UCLASS()
class UNIVERSE_API AUniversePlayerState : public APlayerState
{
    GENERATED_BODY()

public:
    AUniversePlayerState();

    virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
    virtual void CopyProperties(APlayerState* NewPlayerState) override;

    // --- Identity -----------------------------------------------------------

    /** Stable across reconnects. Not the connection id. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    const FString& GetPersistentId() const { return PersistentId; }

    /** Server only. */
    void SetPersistentId(const FString& InId);

    // --- Canonical position -------------------------------------------------

    FUniversePosition GetUniversePosition() const { return CanonicalPosition.Get(); }

    FVector3d GetUniverseVelocityMs() const
    {
        return FVector3d(NetVelocityMs.X, NetVelocityMs.Y, NetVelocityMs.Z);
    }

    FRotator GetUniverseOrientation() const { return Orientation; }

    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    bool IsOnFoot() const { return bOnFoot; }

    /** The planet the player is standing on, or 0 in space. */
    uint64 GetPlanetKey() const { return PlanetKey; }

    /** The system the player is currently in, if any. */
    FUniverseSystemId GetSystemId() const { return SystemId.Get(); }

    /**
     * Records an authoritative movement state. Server only.
     *
     * This is the single place a player's canonical position changes on the
     * server, which is what makes movement validation possible at all: a second
     * path that also wrote it would be a second path that skipped the check.
     */
    void SetAuthoritativeState(
        const FUniversePosition& Position,
        const FVector3d& VelocityMs,
        const FRotator& InOrientation,
        bool bInOnFoot,
        uint64 InPlanetKey,
        const FUniverseSystemId& InSystemId);

    /** Server time the state above was accepted. */
    double GetLastUpdateServerTime() const { return LastUpdateServerTime; }

    /**
     * True once the server has decided where this player is.
     *
     * Replicated, and the thing a joining client waits for. Without it a client
     * cannot tell "the server placed me at the universe origin" from "the
     * server has not placed me yet", and the origin is intergalactic space -
     * so guessing wrong means spawning somewhere with no stars, no planet and
     * nothing to stand on.
     */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    bool HasAuthoritativePosition() const { return bHasAuthoritativePosition; }

    /** Server only. Sets the spawn position and marks it decided. */
    void SetSpawnPosition(const FUniversePosition& Position);

    // --- Movement validation ------------------------------------------------

    /** How many proposed moves the server has rejected as impossible. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 GetRejectedMoveCount() const { return RejectedMoveCount; }

    void RecordRejectedMove() { ++RejectedMoveCount; }

private:
    UPROPERTY(Replicated)
    FString PersistentId;

    UPROPERTY(Replicated)
    FReplicatedUniversePosition CanonicalPosition;

    UPROPERTY(Replicated)
    FVector_NetQuantize100 NetVelocityMs = FVector_NetQuantize100(ForceInitToZero);

    UPROPERTY(Replicated)
    FRotator Orientation = FRotator::ZeroRotator;

    UPROPERTY(Replicated)
    bool bOnFoot = false;

    UPROPERTY(Replicated)
    uint64 PlanetKey = 0;

    UPROPERTY(Replicated)
    FReplicatedSystemId SystemId;

    UPROPERTY(Replicated)
    int32 RejectedMoveCount = 0;

    UPROPERTY(Replicated)
    bool bHasAuthoritativePosition = false;

    double LastUpdateServerTime = 0.0;
};
