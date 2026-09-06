// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetVegetation.h"
#include "PlanetEnvironment.h"
#include "PlanetSurface.h"
#include "PlanetPatchId.h"
#include "PlanetVegetationComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * UPlanetVegetationComponent
 *
 * Streams trees, plants and rocks around the player.
 *
 *
 * WHY IT DOES NOT FOLLOW THE TERRAIN PATCHES
 *
 * The obvious design is to hang vegetation off terrain patch lifetime: a patch
 * streams in, its trees appear; it streams out, they go. It is wrong, and the
 * reason is that the two have different natural resolutions.
 *
 * Terrain LOD is chosen by screen-space error, so from four kilometres up a
 * single patch can be two kilometres across - and that patch would then need
 * every tree on two square kilometres, none of which is individually visible.
 * Meanwhile at walking height the patches under the player are a few metres
 * across, and each would carry a handful of trees in its own component, which
 * is thousands of draw calls to render a forest.
 *
 * So vegetation runs its own selection at a *fixed* patch level chosen to make
 * patches a few hundred metres across, and activates them by distance. Terrain
 * decides how finely the ground is tessellated; vegetation decides how far away
 * a tree is worth existing. They are related but they are not the same
 * question, and tying them together answers neither well.
 *
 *
 * LAYERS AND RANGES
 *
 * Each layer has its own radius, because their costs and their visibility
 * differ by orders of magnitude:
 *
 *     Canopy      trees      far    sparse    collidable
 *     Understory  shrubs     mid    middling  no collision
 *     Scatter     rocks      mid    sparse    collidable
 *     Ground      grass      near   dense     no collision
 *
 * Grass at seven thousand clumps per hectare over even a small radius is tens
 * of thousands of instances, so its radius is the smallest by a wide margin and
 * its budget is separate.
 *
 *
 * THREADING
 *
 * Placement is pure mathematics on plain data - see PlanetVegetation.h - so it
 * runs on the thread pool. Only the instance upload touches Unreal objects, and
 * it happens on the game thread, budgeted per frame. The same arrangement the
 * terrain streamer uses, for the same reasons.
 */
UCLASS(ClassGroup = (Universe), meta = (BlueprintSpawnableComponent))
class UNIVERSE_API UPlanetVegetationComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UPlanetVegetationComponent();

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Which planet to grow. */
    void SetPlanet(
        const FPlanetSurfaceDescriptor& InPlanet,
        const FPlanetEnvironmentDescriptor& InEnvironment,
        const FPlanetTerrainSettings& InSettings);

    /** Where the observer is, in planet-local metres. */
    void SetObserverPositionMeters(const FVector3d& InObserverMeters);

    // --- Diagnostics, surfaced on the HUD ---------------------------------

    UFUNCTION(BlueprintPure, Category = "Universe|Vegetation")
    int32 GetActivePatchCount() const { return Patches.Num(); }

    UFUNCTION(BlueprintPure, Category = "Universe|Vegetation")
    int32 GetInstanceCount() const { return TotalInstances; }

    UFUNCTION(BlueprintPure, Category = "Universe|Vegetation")
    int32 GetPendingJobCount() const { return InFlightJobs; }

    int32 GetLayerInstanceCount(EVegetationLayer Layer) const;

    /** Total placed and released since start, for leak-hunting. */
    int32 GetTotalGenerated() const { return TotalGenerated; }
    int32 GetTotalReleased() const { return TotalReleased; }

    // --- Budgets ----------------------------------------------------------

    /**
     * Quadtree level vegetation patches live at.
     *
     * Chosen so a patch is a few hundred metres across on an Earth-sized world:
     * small enough that activating one is cheap and that the active set follows
     * the player closely, large enough that the per-patch component overhead is
     * amortised over a useful number of instances.
     */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    int32 PatchLevel = 13;

    /** Activation radius per layer, in metres. */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    float CanopyRadiusMeters = 1400.0f;

    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    float UnderstoryRadiusMeters = 500.0f;

    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    float ScatterRadiusMeters = 700.0f;

    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    float GroundRadiusMeters = 140.0f;

    /**
     * Hard ceiling on live instances, across every layer and patch.
     *
     * The number that makes "the planet is computationally bounded" true rather
     * than aspirational. When it is reached, new patches are simply not
     * activated - which thins the far edge of the forest rather than dropping
     * frames, and is reported so the thinning is diagnosable.
     */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    int32 MaxTotalInstances = 60000;

    /** Instances one patch-layer may place. Bounds a single job. */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    int32 MaxInstancesPerPatchLayer = 2000;

    /** Placement jobs in flight at once. */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    int32 MaxConcurrentJobs = 4;

    /** Patch-layers uploaded to Unreal per frame. Upload is game-thread work. */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    int32 MaxUploadsPerFrame = 2;

    /**
     * Altitude above which no vegetation is generated at all, in metres.
     *
     * Nobody can see a tree from orbit, and generating a forest a player is
     * flying over at a kilometre a second is work that will be thrown away
     * before it is drawn. This is the coarsest of the relevance checks and the
     * one that makes fast traversal cheap.
     */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    float MaxObserverAltitudeMeters = 3000.0f;

    /** Seconds between selection passes. */
    UPROPERTY(EditAnywhere, Category = "Universe|Vegetation")
    float SelectionIntervalSeconds = 0.4f;

private:
    /** Placement results, handed back from a worker. */
    struct FVegetationJobResult
    {
        FPlanetPatchId PatchId;
        EVegetationLayer Layer = EVegetationLayer::Canopy;
        uint64 Serial = 0;
        TArray<FVegetationInstance> Instances;
        bool bHitBudget = false;
        double MillisecondsTaken = 0.0;
    };

    /**
     * Shared with worker tasks. Captured by value as a shared pointer, never
     * `this`, so a job that outlives the component drops its result instead of
     * writing into freed memory - the same contract FPlanetTerrainShared has.
     */
    struct FVegetationShared
    {
        TQueue<TSharedPtr<FVegetationJobResult, ESPMode::ThreadSafe>, EQueueMode::Mpsc> Completed;
        FThreadSafeCounter InFlight;
        FThreadSafeBool bShutdown;
    };

    /** One active patch-layer. */
    struct FActivePatch
    {
        FPlanetPatchId PatchId;
        EVegetationLayer Layer = EVegetationLayer::Canopy;
        uint64 Serial = 0;
        bool bGenerating = false;
        bool bLive = false;
        int32 InstanceCount = 0;
        double DistanceMeters = 0.0;

        /** One component per archetype used by this patch-layer. */
        TArray<TObjectPtr<UInstancedStaticMeshComponent>> Components;
    };

    void RunSelection();
    void PumpGeneration();
    void DrainResults();
    void ReleasePatch(FActivePatch& Patch);
    void ReleaseAll();

    float GetLayerRadiusMeters(EVegetationLayer Layer) const;

    UInstancedStaticMeshComponent* AcquireComponent(EVegetationArchetype Archetype);
    void RecycleComponent(UInstancedStaticMeshComponent* Component);

    /** Mesh and metre-scale for an archetype. Null mesh means "grow nothing". */
    UStaticMesh* GetArchetypeMesh(EVegetationArchetype Archetype) const;
    static double GetArchetypeSizeMeters(EVegetationArchetype Archetype);

    /** Width as a fraction of height. Stops trees being spheres. */
    static double GetArchetypeWidthRatio(EVegetationArchetype Archetype);

    /** Placeholder tint, until real assets exist. */
    static FLinearColor GetArchetypeColour(EVegetationArchetype Archetype);
    static bool ArchetypeHasCollision(EVegetationArchetype Archetype);

    FPlanetSurfaceDescriptor Planet;
    FPlanetEnvironmentDescriptor Environment;
    FPlanetTerrainSettings TerrainSettings;

    FVector3d ObserverMeters = FVector3d::ZeroVector;
    bool bHasObserver = false;

    TMap<uint64, FActivePatch> Patches;

    TSharedPtr<FVegetationShared, ESPMode::ThreadSafe> Shared;

    uint64 NextSerial = 1;
    int32 InFlightJobs = 0;
    int32 TotalInstances = 0;
    int32 TotalGenerated = 0;
    int32 TotalReleased = 0;
    int32 LayerInstances[static_cast<int32>(EVegetationLayer::Count)] = {};

    float TimeSinceSelection = 0.0f;

    /** Components kept alive for reuse, keyed by archetype. */
    UPROPERTY(Transient)
    TArray<TObjectPtr<UInstancedStaticMeshComponent>> ComponentPool;

    /** Meshes resolved once at construction. */
    UPROPERTY(Transient)
    TMap<uint8, TObjectPtr<UStaticMesh>> ArchetypeMeshes;

    UPROPERTY(Transient)
    TObjectPtr<class UMaterialInterface> VegetationMaterial;
};
