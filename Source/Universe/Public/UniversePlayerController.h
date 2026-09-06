// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "UniverseNetTypes.h"
#include "WorldPersistence.h"
#include "UniversePlayerController.generated.h"

class AUniversePlayerState;
class APlanetActor;

/**
 * AUniversePlayerController
 *
 * The authority seam. Every change a client wants to make to shared state
 * passes through exactly one of the RPCs below, and the server decides.
 *
 *
 * THE MOVEMENT MODEL, AND WHY IT IS NOT SERVER-DRIVEN
 *
 * The obvious multiplayer arrangement - client sends input, server simulates,
 * server sends back the result - does not work here, and the reason is
 * interesting rather than merely inconvenient.
 *
 * Simulating a player means having their planet's terrain, their environment,
 * their vegetation and their collision built on the server. That is fine for
 * one player and it is exactly the thing Sprint 006 established the server
 * cannot afford for many: a server holding a fully streamed planet per player
 * is a server that scales with player count times planet cost, which is the
 * shape of a system that does not become an MMO.
 *
 * So movement is **client-simulated and server-validated**. The client runs the
 * same deterministic terrain and gravity it already has, proposes where it now
 * is, and the server checks that the proposal is *possible*: that the distance
 * covered is within what the regime's speed limit allows for the elapsed time.
 * Rejected proposals are corrected, counted and reported.
 *
 * This is a proof-of-architecture position, and it is stated plainly rather
 * than dressed up: it stops a client teleporting across the galaxy or moving at
 * a thousand times its drive's capability, and it does not stop a client
 * walking through a wall. Section 59 of the sprint asks for the *principle* to
 * be established and for the seam to exist, which it does - every check lives
 * in ValidateProposedMove and nothing bypasses it.
 *
 *
 * WORLD EDITS ARE NOT LIKE MOVEMENT
 *
 * Building and removing are fully server-authoritative with no client
 * prediction at all. They are rare, deliberate, durable acts: paying a round
 * trip for one is unnoticeable, and getting one wrong writes a permanent lie
 * into the world database. The server re-derives the placement from the same
 * generator the client used and refuses anything it cannot reproduce.
 */
UCLASS()
class UNIVERSE_API AUniversePlayerController : public APlayerController
{
    GENERATED_BODY()

public:
    AUniversePlayerController();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    // --- Movement -----------------------------------------------------------

    /**
     * The client's proposed movement state.
     *
     * Unreliable: a dropped position update is replaced by the next one a
     * tenth of a second later, and a reliable channel would head-of-line block
     * everything behind it to redeliver information that is already stale.
     */
    UFUNCTION(Server, Unreliable, WithValidation)
    void ServerUpdatePosition(
        const FReplicatedUniversePosition& Position,
        FVector_NetQuantize100 VelocityMs,
        FRotator Orientation,
        bool bOnFoot,
        uint64 PlanetKey,
        const FReplicatedSystemId& SystemId,
        double ClientTimeSeconds);

    /** Server told the client its proposal was rejected; snap back. */
    UFUNCTION(Client, Reliable)
    void ClientCorrectPosition(const FReplicatedUniversePosition& Position, const FString& Reason);

    // --- World edits --------------------------------------------------------

    /** Asks the server to place a structure where this client is standing. */
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRequestBuild(const FString& TypeId, const FNetPlacement& Placement, uint64 PlanetKey);

    /** Asks the server to remove a procedural entity. */
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRequestRemoveProcedural(
        const FNetEntityId& EntityId,
        FVector_NetQuantize100 PlanetLocalMeters,
        uint64 PlanetKey);

    /** Asks the server to destroy a structure this client placed. */
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerRequestDemolish(const FNetEntityId& EntityId, uint64 PlanetKey);

    // --- Region subscription ------------------------------------------------

    /**
     * Tells the server which persistence regions this client needs.
     *
     * The client asks rather than the server guessing, because the client is
     * the one that knows which patches it has streamed in and is about to draw
     * vegetation for. The server bounds what it will honour.
     */
    UFUNCTION(Server, Reliable, WithValidation)
    void ServerSubscribeRegion(const FNetRegionId& RegionId);

    UFUNCTION(Server, Reliable, WithValidation)
    void ServerUnsubscribeRegion(const FNetRegionId& RegionId);

    /** A region's deltas, sent once on subscription and again on change. */
    UFUNCTION(Client, Reliable)
    void ClientReceiveRegionDelta(const FNetRegionDelta& Delta);

    /** One entity created after this client was already subscribed. */
    UFUNCTION(Client, Reliable)
    void ClientEntityCreated(const FNetEntityRecord& Record);

    /** One entity removed after this client was already subscribed. */
    UFUNCTION(Client, Reliable)
    void ClientEntityRemoved(const FNetEntityId& EntityId);

    // --- Diagnostics --------------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 GetSubscribedRegionCount() const { return SubscribedRegions.Num(); }

    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 GetSentMoveCount() const { return SentMoveCount; }

    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 GetCorrectionCount() const { return CorrectionCount; }

    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    FString GetLastCorrectionReason() const { return LastCorrectionReason; }

    /** How often the client proposes a position, in seconds. */
    UPROPERTY(EditDefaultsOnly, Category = "Universe|Net")
    double PositionUpdateIntervalSeconds = 0.1;

    /**
     * How much faster than the regime limit a proposal may be before it is
     * refused.
     *
     * Not 1.0. A client's frame is not the server's, packets arrive in bursts,
     * and a legitimate move measured over a slightly wrong interval looks
     * slightly too fast. Four times the limit still refuses a teleport by
     * several orders of magnitude while never refusing an honest player.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Universe|Net")
    double MovementToleranceFactor = 4.0;

    /** Regions a client may hold at once. Bounds the server's per-client cost. */
    UPROPERTY(EditDefaultsOnly, Category = "Universe|Net")
    int32 MaxSubscribedRegions = 64;

private:
    /**
     * Is this proposal physically possible?
     *
     * Returns false with a reason. The single place a move is judged - a second
     * one would be a second place to forget a check.
     */
    bool ValidateProposedMove(
        const FUniversePosition& Proposed,
        double ElapsedSeconds,
        bool bOnFoot,
        FString& OutReason) const;

    /** Samples the local pawn and sends a proposal, on the client. */
    void SendPositionUpdate();

    AUniversePlayerState* GetUniversePlayerState() const;

    /** Regions this client has asked for, on the server. */
    TSet<uint64> SubscribedRegions;

    double TimeSinceUpdate = 0.0;

    int32 SentMoveCount = 0;
    int32 CorrectionCount = 0;

    FString LastCorrectionReason;

    /** Server time of the last accepted proposal, for the speed check. */
    double LastAcceptedServerTime = 0.0;

    bool bHaveAcceptedAnyMove = false;
};
