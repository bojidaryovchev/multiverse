// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "PlanetPatchMesh.h"

// Pulled in for FProcMeshTangent, which the concrete backend holds in a reused
// scratch buffer. The abstract interface above it needs nothing from the
// plugin - only this one implementation does - so a later replacement drops
// this include along with the class.
#include "ProceduralMeshComponent.h"

#include "PlanetMeshBackend.generated.h"

class UMaterialInterface;

/**
 * PlanetMeshBackend.h
 *
 * The seam between "terrain data" and "something Unreal can draw".
 *
 * Sprint 002 is explicit that the planet data model must not depend on one mesh
 * component implementation. That is not abstraction for its own sake - UE 5.8
 * offers several ways to push generated geometry at the renderer, none of them
 * obviously permanent. UProceduralMeshComponent is stable and well understood
 * but not the fastest; UDynamicMeshComponent is more modern with known
 * rendering limitations; Mesh Terrain is Experimental. Committing the quadtree,
 * the streamer and the terrain function to any of them would make replacing it
 * a rewrite instead of a swap.
 *
 * So everything above this line speaks in FPlanetPatchMesh - plain arrays - and
 * only the concrete backend below knows what a component is.
 *
 * Slots rather than components: the streamer refers to geometry by an integer
 * handle, so it cannot accidentally hold a raw pointer to something the backend
 * has recycled.
 */
UCLASS(Abstract)
class UNIVERSE_API UPlanetMeshBackend : public UObject
{
    GENERATED_BODY()

public:
    /** Attaches the backend to the actor that will own its components. */
    virtual void Initialise(AActor* InOwner, UMaterialInterface* InMaterial) PURE_VIRTUAL(UPlanetMeshBackend::Initialise, );

    /**
     * Reserves a slot, reusing a released one where possible.
     *
     * Pooling is not premature here: a descending observer can turn over
     * hundreds of patches per second, and creating and destroying components at
     * that rate produces a steady stream of garbage-collection work and
     * render-thread churn that shows up directly as hitching.
     */
    virtual int32 AcquireSlot() PURE_VIRTUAL(UPlanetMeshBackend::AcquireSlot, return INDEX_NONE;);

    /** Uploads geometry into a slot and places it relative to the planet centre. */
    virtual void UpdateSlot(int32 Slot, const FPlanetPatchMesh& Mesh, bool bWithCollision)
        PURE_VIRTUAL(UPlanetMeshBackend::UpdateSlot, );

    /** Hides a slot and returns it to the pool. */
    virtual void ReleaseSlot(int32 Slot) PURE_VIRTUAL(UPlanetMeshBackend::ReleaseSlot, );

    /** Slots currently holding visible geometry. */
    virtual int32 GetActiveSlotCount() const PURE_VIRTUAL(UPlanetMeshBackend::GetActiveSlotCount, return 0;);

    /** Slots allocated but idle. */
    virtual int32 GetPooledSlotCount() const PURE_VIRTUAL(UPlanetMeshBackend::GetPooledSlotCount, return 0;);

    /** Slots with collision cooked. */
    virtual int32 GetCollisionSlotCount() const PURE_VIRTUAL(UPlanetMeshBackend::GetCollisionSlotCount, return 0;);

    /** Destroys everything, pooled included. */
    virtual void ReleaseAll() PURE_VIRTUAL(UPlanetMeshBackend::ReleaseAll, );
};

/**
 * UProceduralMeshComponent implementation.
 *
 * Chosen for Sprint 002 because it is stock, stable in 5.8, supports runtime
 * collision cooking, and has an API narrow enough that swapping it out later is
 * a small job. It is explicitly not chosen for performance - when that matters,
 * the replacement goes here and nothing above changes.
 */
UCLASS()
class UNIVERSE_API UPlanetMeshBackend_ProceduralMesh : public UPlanetMeshBackend
{
    GENERATED_BODY()

public:
    virtual void Initialise(AActor* InOwner, UMaterialInterface* InMaterial) override;
    virtual int32 AcquireSlot() override;
    virtual void UpdateSlot(int32 Slot, const FPlanetPatchMesh& Mesh, bool bWithCollision) override;
    virtual void ReleaseSlot(int32 Slot) override;
    virtual int32 GetActiveSlotCount() const override;
    virtual int32 GetPooledSlotCount() const override;
    virtual int32 GetCollisionSlotCount() const override;
    virtual void ReleaseAll() override;

private:
    struct FSlot
    {
        TObjectPtr<UProceduralMeshComponent> Component;
        bool bInUse = false;
        bool bHasCollision = false;
    };

    UPROPERTY()
    TObjectPtr<AActor> Owner;

    UPROPERTY()
    TObjectPtr<UMaterialInterface> Material;

    UPROPERTY()
    TArray<TObjectPtr<UProceduralMeshComponent>> Components;

    TArray<bool> SlotInUse;
    TArray<bool> SlotHasCollision;

    /** Indices of released slots, reused before allocating new ones. */
    TArray<int32> FreeSlots;

    /**
     * Scratch buffers, reused across every upload.
     *
     * UProceduralMeshComponent wants TArray<FVector>/TArray<FVector2D> etc,
     * while the generator produces separate float streams. Converting into
     * members rather than locals keeps a patch upload from allocating several
     * megabyte-scale temporaries every time, which at streaming rates is a
     * measurable amount of allocator traffic.
     */
    TArray<FVector> ScratchVertices;
    TArray<FVector> ScratchNormals;
    TArray<FVector2D> ScratchUVs;
    TArray<FColor> ScratchColors;
    TArray<FProcMeshTangent> ScratchTangents;
    TArray<int32> ScratchIndices;
};
