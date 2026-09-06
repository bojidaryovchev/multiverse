// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldPersistence.h"
#include "WorldStateSubsystem.generated.h"

class APlanetActor;

/**
 * UWorldStateSubsystem
 *
 * What the world is *now*, as opposed to what the generator would produce.
 *
 *
 * THE SEAM
 *
 *     gameplay  ->  UWorldStateSubsystem  ->  IWorldPersistenceStore  ->  SQLite
 *
 * Everything that creates, removes or queries a persistent thing goes through
 * here. Nothing else opens a database, and nothing else knows one exists.
 *
 * That is not tidiness. Sprint 007 replaces local authority with a server, and
 * the difference between "swap the store" and "rewrite every interaction" is
 * whether this layer exists now. A building placement that called SQLite
 * directly would have to be rewritten; one that calls CreateEntity does not.
 *
 * It is also where authority will live. Today CreateEntity succeeds
 * immediately; on a server it will be a request that can be refused. Callers
 * already treat the result as an answer rather than an assumption, which is the
 * habit that makes that change survivable.
 *
 *
 * REGIONS ARE CACHED, NOT QUERIED
 *
 * Vegetation streaming asks "is this tree removed" for tens of thousands of
 * instances per second. That question must never reach a database. Regions are
 * loaded once, held while relevant, and evicted when they are not - so the hot
 * path is a hash lookup in memory and the cold path happens on a worker thread.
 *
 *
 * THREADING
 *
 * Region loads run on the thread pool: a load is a database read and must not
 * be on the game thread. Writes are synchronous and deliberately so - see
 * CreateEntity - because losing a building the player just placed because the
 * process exited before an async write landed is far worse than a millisecond
 * of hitch.
 */
UCLASS()
class UNIVERSE_API UWorldStateSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    // --- Lifecycle --------------------------------------------------------

    /**
     * Opens the store for a universe.
     *
     * Called by the game mode once the universe seed is known, because the seed
     * is part of the world's identity and opening before it is settled would
     * mean adopting the wrong one.
     */
    bool OpenWorld(const FString& UniverseSeedText, uint64 UniverseSeedValue);

    bool IsOpen() const { return Store.IsValid() && Store->IsOpen(); }

    const FWorldSaveMetadata& GetMetadata() const { return Metadata; }
    EWorldPersistenceStatus GetOpenStatus() const { return OpenStatus; }

    /**
     * True when the store was written by a different generator version.
     *
     * The world still loads. Structures may no longer sit on the ground they
     * were built on, and saying so is more useful than refusing to start.
     */
    bool HasGenerationVersionMismatch() const
    {
        return OpenStatus == EWorldPersistenceStatus::GenerationVersionMismatch;
    }

    // --- Creating and removing --------------------------------------------

    /**
     * Places a persistent entity and writes it immediately.
     *
     * Synchronous on purpose. Section 37 asks that changes persist on the
     * action rather than at exit, and the strongest form of that is that
     * CreateEntity has already written by the time it returns - so a crash one
     * frame later cannot lose it. A structure placement is a rare, deliberate
     * act; paying a millisecond for it to be durable is the right trade.
     *
     * Returns the assigned id, or an invalid one on failure.
     */
    FPersistentEntityId CreateEntity(
        const APlanetActor* Planet,
        const FString& TypeId,
        const FPersistentPlacement& Placement);

    /** Deletes a player-created entity, permanently. */
    bool DestroyCreatedEntity(const FPersistentEntityId& EntityId);

    /**
     * Records that a procedural entity is gone.
     *
     * A tombstone, not a copy. All that is stored is the name and where it was,
     * because the generator can still produce the thing and the only new
     * information is that it should not be shown.
     */
    bool RemoveProceduralEntity(
        const APlanetActor* Planet,
        const FPersistentEntityId& EntityId,
        const FVector3d& PlanetLocalMeters);

    /** Restores a removed procedural entity, for debugging. */
    bool RestoreProceduralEntity(const FPersistentEntityId& EntityId);

    // --- Querying ---------------------------------------------------------

    /**
     * True if a procedural entity has been removed.
     *
     * The hot path: called for every vegetation instance placed. Answers from
     * the region cache and never touches storage. A region that has not been
     * loaded yet answers "not removed", which is the safe direction - a tree
     * that briefly reappears is a visual glitch, a tree that vanishes because
     * a load was pending is a lost building's worth of confusion.
     */
    bool IsProceduralEntityRemoved(const FPersistentEntityId& EntityId) const;

    // --- Authority --------------------------------------------------------
    //
    // Sprint 007 moved persistence writes to the server. This is the seam that
    // enforces it, and it enforces it in one place rather than in each caller:
    // every mutating function below refuses without authority.
    //
    // A client is not merely discouraged from writing - it *cannot*. It has no
    // database open at all: OpenWorld is a no-op on a client, so a write that
    // slipped through the checks would fail at the store rather than corrupt
    // anything. Two independent reasons for the same guarantee is deliberate;
    // one of them will eventually be edited by somebody who does not know about
    // the other.

    /** True when this instance owns the world database. Server or standalone. */
    UFUNCTION(BlueprintPure, Category = "Universe|Persistence")
    bool HasPersistenceAuthority() const;

    /**
     * True when world state can be used at all - which is not the same as
     * IsOpen().
     *
     * IsOpen() asks "is there a database here", and on a client the answer is
     * correctly no: it has no local store and never will. But a client can
     * still read regions the server sent it and still request changes, so every
     * caller that guarded on IsOpen() was refusing to work on a client for a
     * reason that does not apply there.
     *
     * That was a real defect rather than a hypothetical one: universe.Build
     * reported "world persistence is unavailable" on a client that was
     * connected, landed, and perfectly able to ask the server to build.
     */
    UFUNCTION(BlueprintPure, Category = "Universe|Persistence")
    bool IsUsable() const { return HasPersistenceAuthority() ? IsOpen() : true; }

    /**
     * Applies a whole region's deltas received from the server.
     *
     * Client only. The received delta *replaces* whatever was cached for that
     * region rather than merging into it: a region is a complete statement of
     * what is different there, and merging two complete statements is how a
     * removed tree comes back.
     */
    void ApplyReplicatedRegion(const FWorldRegionDelta& Delta);

    /** Applies one entity creation received from the server. Client only. */
    void ApplyReplicatedRecord(const FWorldEntityRecord& Record);

    /** Applies one procedural removal received from the server. Client only. */
    void ApplyReplicatedRemoval(const FPersistentEntityId& EntityId);

    // --- World facts ------------------------------------------------------
    //
    // World-scoped key/value state, for things that are true of the world
    // rather than of a place in it. Discovery is the only consumer so far.
    //
    // Routed through this subsystem rather than letting callers reach the store
    // directly, for the same reason everything else is: this is the server
    // authority seam, and a caller that writes to storage without passing
    // through it is a caller that will not work when the authority moves to a
    // server in Sprint 007.

    /** Writes one world fact. Synchronous, like every other write here. */
    bool SetWorldFact(const FString& Key, const FString& Value);

    /** Reads one world fact. False when it is absent or the world is not open. */
    bool GetWorldFact(const FString& Key, FString& OutValue) const;

    /** Every world fact under a key prefix. */
    bool GetWorldFactsWithPrefix(
        const FString& Prefix,
        TArray<TPair<FString, FString>>& OutFacts) const;

    /** The cached delta for a region, or null if it is not loaded. */
    const FWorldRegionDelta* FindLoadedRegion(const FPersistenceRegionId& RegionId) const;

    /** Asks for a region to be loaded, if it is not already. */
    void RequestRegion(const FPersistenceRegionId& RegionId);

    /** Loads a region and waits. For tests and for commands. */
    bool LoadRegionBlocking(const FPersistenceRegionId& RegionId);

    // --- Events -----------------------------------------------------------

    DECLARE_MULTICAST_DELEGATE_OneParam(FOnRegionLoaded, const FWorldRegionDelta&);
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnRegionUnloaded, const FPersistenceRegionId&);
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntityCreated, const FWorldEntityRecord&);
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnEntityRemoved, const FPersistentEntityId&);

    FOnRegionLoaded OnRegionLoaded;
    FOnRegionUnloaded OnRegionUnloaded;
    FOnEntityCreated OnEntityCreated;
    FOnEntityRemoved OnEntityRemoved;

    // --- Observability ----------------------------------------------------

    UFUNCTION(BlueprintPure, Category = "Universe|Persistence")
    int32 GetLoadedRegionCount() const { return Regions.Num(); }

    UFUNCTION(BlueprintPure, Category = "Universe|Persistence")
    int32 GetPendingLoadCount() const { return PendingLoads; }

    int32 GetCreatedEntityCount() const;
    int32 GetRemovedEntityCount() const;

    int64 GetStorageRecordCount() const;
    int64 GetStorageSizeBytes() const;

    /** Last read and write latencies, milliseconds. */
    double GetLastReadMilliseconds() const { return LastReadMs; }
    double GetLastWriteMilliseconds() const { return LastWriteMs; }

    FString GetLastError() const;

    /** The store, for development commands. Null when closed. */
    IWorldPersistenceStore* GetStore() const { return Store.Get(); }

    /** Drops every cached region without touching storage. */
    void FlushRegionCache();

    /** How many regions are kept in memory. */
    UPROPERTY(EditAnywhere, Category = "Universe|Persistence")
    int32 MaxCachedRegions = 64;

    /** A stable local identity, until accounts exist. */
    static FString GetLocalOwnerId() { return TEXT("local-player"); }

private:
    /** A region load in flight. */
    struct FPendingLoad
    {
        FPersistenceRegionId RegionId;
        TSharedPtr<FWorldRegionDelta, ESPMode::ThreadSafe> Result;
        TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> bComplete;
        double StartSeconds = 0.0;
    };

    void EvictIfNeeded();
    void ApplyLoaded(const FWorldRegionDelta& Delta);

    TUniquePtr<IWorldPersistenceStore> Store;

    FWorldSaveMetadata Metadata;
    EWorldPersistenceStatus OpenStatus = EWorldPersistenceStatus::NotOpen;

    TMap<FPersistenceRegionId, FWorldRegionDelta> Regions;

    /** When each region was last touched, for eviction. */
    TMap<FPersistenceRegionId, double> RegionLastUsed;

    TArray<FPendingLoad> Loads;
    TSet<FPersistenceRegionId> Requested;

    int32 PendingLoads = 0;

    double LastReadMs = 0.0;
    double LastWriteMs = 0.0;
    double TimeSeconds = 0.0;

    uint64 CreationCounter = 0;
};
