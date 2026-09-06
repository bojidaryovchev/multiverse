// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Containers/Queue.h"
#include "PlanetSurface.h"
#include "PlanetPatchId.h"
#include "PlanetPatchMesh.h"
#include "PlanetQuadtree.h"
#include "PlanetTerrainComponent.generated.h"

class UPlanetMeshBackend;
class UMaterialInterface;

/**
 * Where a patch is in its lifecycle.
 *
 * The states exist so streaming behaviour is observable rather than inferred.
 * "Terrain is popping in slowly" has completely different causes if patches are
 * piling up in Queued (the generator is saturated) versus Ready (the game
 * thread is not draining results fast enough), and without explicit states the
 * only way to tell them apart is guesswork.
 */
UENUM(BlueprintType)
enum class EPlanetPatchState : uint8
{
    /** Selected by the quadtree, not yet requested. */
    Needed,
    /** Waiting for a worker. */
    Queued,
    /** A worker is building it. */
    Generating,
    /** Geometry built, waiting for the game thread to upload it. */
    Ready,
    /** Uploaded and drawing. */
    Visible,
    /** No longer selected; slot about to be recycled. */
    Releasing,
};

/** Everything the debug overlay needs about the streamer. */
USTRUCT(BlueprintType)
struct UNIVERSE_API FPlanetTerrainStats
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 SelectedPatches = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 VisiblePatches = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 QueuedPatches = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 GeneratingPatches = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 ReadyPatches = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 PooledSlots = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 CollisionPatches = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 PrewarmSelected = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 TriangleCount = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 VertexCount = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 DeepestLevel = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 HorizonCulled = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 BalancingSplits = 0;

    /** Results thrown away because the patch stopped being relevant. */
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 DiscardedResults = 0;

    /** Cumulative counts, for spotting churn and leaks. */
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 TotalGenerated = 0;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") int32 TotalReleased = 0;

    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") float LastSelectionMs = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") float LastUploadMs = 0.0f;
    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") float AverageGenerationMs = 0.0f;

    UPROPERTY(BlueprintReadOnly, Category = "Universe|Terrain") float ObserverAltitudeMeters = 0.0f;
};

/**
 * State shared between the component and its worker tasks.
 *
 * Held by shared pointer and captured by value into every task, so a task can
 * safely outlive the component. Capturing the component instead would be a
 * use-after-free waiting to happen: a generation launched just before the level
 * tears down finishes on a worker some milliseconds later, and by then the
 * component, its queue and its counters are gone.
 *
 * Checking a shutdown flag on the component would not fix that either - the
 * component could be destroyed between the check and the enqueue. Only moving
 * the shared data out of the component removes the window entirely.
 */
struct FPlanetTerrainShared
{
    /** Completed geometry, many workers to one consumer. */
    TQueue<TSharedPtr<FPlanetPatchMesh, ESPMode::ThreadSafe>, EQueueMode::Mpsc> CompletedMeshes;

    /** Generations currently running, so the cap needs no lock. */
    FThreadSafeCounter InFlightCount;

    /** Set on teardown; tasks then drop their results instead of queueing. */
    FThreadSafeBool bShutdown;
};

/**
 * UPlanetTerrainComponent
 *
 * Streams one planet's terrain: selects patches, generates them off the game
 * thread, uploads results, and releases what is no longer needed.
 *
 *
 * THE LIFECYCLE
 *
 *     Needed -> Queued -> Generating -> Ready -> Visible -> Releasing
 *
 * Nothing is created or destroyed per frame; slots are pooled and reused.
 *
 *
 * THREADING
 *
 * Generation is pure mathematics over plain arrays, so it runs on the task
 * graph. Worker threads never touch a UObject - they read an immutable copy of
 * the planet descriptor and settings, write into a heap-allocated
 * FPlanetPatchMesh, and push it onto an MPSC queue. The game thread drains that
 * queue and does all the component work.
 *
 *
 * OUTRUNNING THE STREAMER
 *
 * A spacecraft can cross the region a patch was queued for long before that
 * patch finishes. Every request carries a serial; when a result arrives, it is
 * dropped unless the patch is still selected and the serial still matches. That
 * is what stops a fast traverse from spending minutes finishing terrain nobody
 * will ever see, and it is why the in-flight count is bounded rather than the
 * queue simply growing.
 */
UCLASS(ClassGroup = (Universe), meta = (BlueprintSpawnableComponent))
class UNIVERSE_API UPlanetTerrainComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UPlanetTerrainComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    /** Sets the planet this component streams. Rebuilds from scratch. */
    void SetPlanet(
        const FPlanetSurfaceDescriptor& InPlanet,
        const FPlanetEnvironmentDescriptor& InEnvironment,
        const FPlanetTerrainSettings& InSettings);

    const FPlanetSurfaceDescriptor& GetPlanet() const { return Planet; }
    const FPlanetTerrainSettings& GetTerrainSettings() const { return TerrainSettings; }
    const FPlanetEnvironmentDescriptor& GetEnvironment() const { return Environment; }

    /**
     * Sets the observer position, in planet-centred metres.
     *
     * Passed in rather than read from a camera so the terrain system has no
     * opinion about who is looking - the caller decides whether that is the
     * player, a debug camera or a scripted stress path.
     */
    void SetObserverPositionMeters(const FVector3d& InObserverMeters);

    /**
     * A second point to refine around: where the observer is predicted to
     * arrive. Pass the zero vector to clear it.
     */
    void SetPrewarmPositionMeters(const FVector3d& InPrewarmMeters);

    /**
     * True if the ground below a planet-local position has cooked collision
     * available right now.
     *
     * The question a character has to ask before trusting the floor. Terrain
     * that has been selected, generated and uploaded still has no collision
     * until the backend has cooked it, and standing on a patch in that state
     * means falling through the planet.
     */
    bool HasCollisionAt(const FVector3d& PlanetLocalMeters) const;

    const FPlanetTerrainStats& GetStats() const { return Stats; }

    /** Material applied to every patch. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    TObjectPtr<UMaterialInterface> TerrainMaterial;

    /** LOD tuning. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    float SplitPixelError = 6.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    int32 MaxLodLevel = 14;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    int32 MaxSelectedPatches = 1024;

    /**
     * Patch generations allowed in flight at once.
     *
     * Bounded so a fast traverse cannot build an unbounded backlog. Roughly the
     * worker count: more just queues work that will be stale by the time it
     * runs.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    int32 MaxConcurrentGenerations = 8;

    /**
     * Reserved share of the generation budget for patches that need collision.
     *
     * Without a reservation, a fast descent fills the whole budget with distant
     * visual patches - there are far more of them - and the ground directly
     * under the player is generated last. The player then lands on terrain that
     * renders but has no collision, and falls through the world. Reserving
     * capacity for collision patches means the ground you are about to stand on
     * is never starved by scenery you are merely looking at.
     */
    UPROPERTY(EditAnywhere, Category = "Universe|Terrain")
    int32 ReservedCollisionGenerations = 4;

    /** Patch uploads permitted per frame, to bound game-thread time. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    int32 MaxUploadsPerFrame = 4;

    /**
     * Collision is cooked only for patches within this many metres of the
     * observer.
     *
     * Collision cooking is far more expensive than rendering, and terrain the
     * player cannot reach does not need it. Sprint 002 asks specifically that
     * distant terrain not generate expensive collision.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    float CollisionRadiusMeters = 5000.0f;

    /**
     * Extra prewarm radius around the predicted arrival point, in metres.
     *
     * A craft descending at a kilometre a second reaches the ground in a few
     * seconds, and a patch takes tens of milliseconds to generate; without
     * prewarming, arrival and the ground being ready are a race that arrival
     * frequently wins. The predicted point is fed in as a second observer so
     * the quadtree refines around where the player is *going* as well as where
     * they are.
     */
    UPROPERTY(EditAnywhere, Category = "Universe|Terrain")
    float PrewarmRadiusMeters = 20000.0f;

    /** Seconds between LOD re-selections. Zero re-selects every frame. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Terrain")
    float SelectionIntervalSeconds = 0.15f;

    /** Releases everything and starts over. */
    UFUNCTION(BlueprintCallable, Category = "Universe|Terrain")
    void ResetTerrain();

private:
    /** One tracked patch. */
    struct FTrackedPatch
    {
        FPlanetPatchId PatchId;
        EPlanetPatchState State = EPlanetPatchState::Needed;
        int32 Slot = INDEX_NONE;
        uint64 Serial = 0;
        int32 TriangleCount = 0;
        int32 VertexCount = 0;
        bool bHasCollision = false;
        bool bWantsCollision = false;
        double DistanceMeters = 0.0;
    };

    void RunSelection();
    void PumpGeneration();
    void DrainResults(double TimeBudgetSeconds);
    void ReleaseUnselected();

    void RequestGeneration(FTrackedPatch& Patch);

    FPlanetSurfaceDescriptor Planet;
    FPlanetEnvironmentDescriptor Environment;
    FPlanetTerrainSettings TerrainSettings;

    FVector3d ObserverMeters = FVector3d::ZeroVector;

    /** Predicted arrival point, and whether one has been set this frame. */
    FVector3d PrewarmMeters = FVector3d::ZeroVector;
    bool bHasPrewarm = false;

    UPROPERTY()
    TObjectPtr<UPlanetMeshBackend> Backend;

    /** Tracked patches, keyed by the packed patch key. */
    TMap<uint64, FTrackedPatch> Patches;

    /** Selection scratch, reused so a frame allocates nothing. */
    TArray<FPlanetSelectedPatch> SelectedScratch;
    TArray<FPlanetSelectedPatch> PrewarmScratch;
    FPlanetSelectionStats SelectionStats;

    FPlanetQuadtreeSelector Selector;

    /** Queue, counter and shutdown flag, outliving the component if needed. */
    TSharedPtr<FPlanetTerrainShared, ESPMode::ThreadSafe> Shared;

    uint64 NextSerial = 1;
    float TimeSinceSelection = 0.0f;
    float TimeSinceStatsLog = 0.0f;

    double GenerationMsAccumulator = 0.0;
    int32 GenerationSamples = 0;

    FPlanetTerrainStats Stats;
};
