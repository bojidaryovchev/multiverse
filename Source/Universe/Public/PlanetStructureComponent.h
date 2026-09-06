// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "WorldPersistence.h"
#include "PlanetSurface.h"
#include "PlanetStructureComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;
class UWorldStateSubsystem;

/**
 * UPlanetStructureComponent
 *
 * Player-built things, on the ground, streamed by region.
 *
 *
 * PERSISTENT EXISTENCE IS NOT RUNTIME REPRESENTATION
 *
 * This distinction is the point of the component, and section 25 is right to
 * call it fundamental. A beacon on the far side of a planet **exists**: it is a
 * row in the database and it is part of the current world. It has no Actor, no
 * transform, no collision and no cost, because nobody is near it.
 *
 * Conflating the two is the failure mode that makes persistent worlds
 * impossible to scale. If existence requires an Actor, then a world with ten
 * thousand structures spawns ten thousand Actors at startup, and a world with
 * ten million cannot be loaded at all. Here the database row is the truth and
 * the Actor is a temporary rendering of it, created when a region is relevant
 * and destroyed when it is not.
 *
 *
 * WHY IT LISTENS RATHER THAN POLLS
 *
 * Regions are loaded asynchronously by UWorldStateSubsystem. This component
 * subscribes to its load and unload events, so a structure appears when its
 * region's deltas arrive and disappears when the region is evicted - and the
 * ordering is guaranteed rather than a race between two independent timers.
 *
 * The same subscription handles creation: placing a beacon broadcasts
 * OnEntityCreated, and this component spawns its representation without the
 * placement code knowing anything about rendering.
 */
UCLASS(ClassGroup = (Universe), meta = (BlueprintSpawnableComponent))
class UNIVERSE_API UPlanetStructureComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UPlanetStructureComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    void SetPlanet(
        const FPlanetSurfaceDescriptor& InPlanet,
        const FPlanetTerrainSettings& InSettings);

    /** How many structures currently have a runtime representation. */
    UFUNCTION(BlueprintPure, Category = "Universe|Structures")
    int32 GetLiveCount() const { return Live.Num(); }

    /**
     * The nearest structure to a planet-local position, within a radius.
     *
     * Used by the removal interaction. Returns an invalid id if there is none.
     */
    FPersistentEntityId FindNearest(
        const FVector3d& PlanetLocalMeters,
        double RadiusMeters,
        FVector3d& OutPositionMeters) const;

    /** Removes a structure's representation without touching storage. */
    void DespawnStructure(const FPersistentEntityId& EntityId);

    /** Geometry for a type. Public so the placement preview matches exactly. */
    static UStaticMesh* GetTypeMesh(const FString& TypeId);
    static double GetTypeSizeMeters(const FString& TypeId);
    static FLinearColor GetTypeColour(const FString& TypeId);

private:
    /** One structure with a runtime representation. */
    struct FLiveStructure
    {
        FWorldEntityRecord Record;
        FVector3d PositionMeters = FVector3d::ZeroVector;
        int32 InstanceIndex = INDEX_NONE;
        FString TypeId;
    };

    void HandleRegionLoaded(const FWorldRegionDelta& Delta);
    void HandleRegionUnloaded(const FPersistenceRegionId& RegionId);
    void HandleEntityCreated(const FWorldEntityRecord& Record);
    void HandleEntityRemoved(const FPersistentEntityId& EntityId);

    void SpawnStructure(const FWorldEntityRecord& Record);
    void RebuildInstances();

    UInstancedStaticMeshComponent* GetOrCreateComponent(const FString& TypeId);

    UWorldStateSubsystem* GetWorldState() const;

    FPlanetSurfaceDescriptor Planet;
    FPlanetTerrainSettings TerrainSettings;

    TMap<FPersistentEntityId, FLiveStructure> Live;

    /** One instanced component per structure type. */
    UPROPERTY(Transient)
    TMap<FString, TObjectPtr<UInstancedStaticMeshComponent>> Components;

    FDelegateHandle RegionLoadedHandle;
    FDelegateHandle RegionUnloadedHandle;
    FDelegateHandle EntityCreatedHandle;
    FDelegateHandle EntityRemovedHandle;

    /**
     * True when the instance arrays need rebuilding from Live.
     *
     * Instances are rebuilt wholesale rather than added and removed
     * individually. Removing an instance from a UInstancedStaticMeshComponent
     * swaps the last one into the hole and invalidates the index of whatever
     * was there - so every stored index would have to be fixed up, and getting
     * that wrong renders one structure at another's position. At the scale this
     * operates on, rebuilding is both faster to write and impossible to get
     * wrong.
     */
    bool bInstancesDirty = false;
};
