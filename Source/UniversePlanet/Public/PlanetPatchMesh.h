// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetPatchId.h"
#include "PlanetSurface.h"
#include "PlanetTerrain.h"

/**
 * PlanetPatchMesh.h
 *
 * Turns a patch address into vertex and index arrays.
 *
 * Deliberately plain data: positions, normals, indices and a few doubles. No
 * UStaticMesh, no UProceduralMeshComponent, no UObject of any kind. That is
 * what lets the expensive part - hundreds of thousands of noise evaluations -
 * run on a worker thread, where touching a UObject would be unsafe. The game
 * thread does nothing but hand these arrays to whichever mesh component is
 * currently in use.
 *
 *
 * PATCH-LOCAL ORIGIN
 *
 * Vertex positions are relative to the patch centre, not the planet centre.
 *
 * On an Earth-sized planet a vertex is ~6.4e6 m from the planet centre. Stored
 * as float - which is what a vertex buffer holds - that magnitude has an ULP of
 * about 0.5 m, so a patch a few metres across would collapse into a handful of
 * distinct positions and the terrain would visibly quantise. Relative to the
 * patch centre the magnitudes are metres, and float precision is
 * sub-millimetre. The patch centre itself is carried in double and applied by
 * the component transform.
 *
 *
 * SKIRTS
 *
 * Each patch optionally carries a rim of geometry dropped inward along the
 * radius, hiding the gap where a finer neighbour has vertices this patch does
 * not.
 *
 * Skirts are used here strictly for LOD transitions, never to paper over
 * mismatched patch geometry - Sprint 002 is explicit about that distinction,
 * and the seam tests prove base geometry already matches exactly. If the base
 * geometry were wrong, skirts would hide it and the bug would surface much
 * later as terrain that does not line up with collision.
 */

/**
 * One generated patch of terrain.
 *
 * Positions and normals are float because they go straight into a vertex
 * buffer; everything that needs to survive at planetary magnitude is double
 * and lives outside the per-vertex arrays.
 */
struct UNIVERSEPLANET_API FPlanetPatchMesh
{
    /** Which patch this is. */
    FPlanetPatchId PatchId;

    /**
     * A monotonically increasing stamp identifying the generation request.
     *
     * Used to discard results that arrive after the patch stopped being
     * relevant - at spacecraft speeds an observer can outrun the terrain
     * streamer easily, and a late result must be dropped rather than applied.
     */
    uint64 GenerationSerial = 0;

    /** Planet-centred position of the patch origin, metres, double precision. */
    FVector3d PatchOriginMeters = FVector3d::ZeroVector;

    /** Vertex positions relative to PatchOriginMeters, in metres. */
    TArray<float> PositionX;
    TArray<float> PositionY;
    TArray<float> PositionZ;

    /** Unit normals, from the terrain gradient. */
    TArray<float> NormalX;
    TArray<float> NormalY;
    TArray<float> NormalZ;

    /** Elevation relative to sea level, metres. Drives the debug material. */
    TArray<float> Elevation;

    /** Triangle indices, three per triangle. */
    TArray<int32> Indices;

    /** Vertices in the main grid, excluding skirt vertices. */
    int32 GridVertexCount = 0;

    /** Radius of a sphere about PatchOriginMeters containing every vertex. */
    double BoundingRadiusMeters = 0.0;

    /** Lowest and highest elevation in this patch, metres. */
    double MinElevationMeters = 0.0;
    double MaxElevationMeters = 0.0;

    /**
     * Largest deviation, in metres, between this patch's triangles and the
     * terrain function they approximate.
     *
     * Measured rather than assumed: this is the geometric error that drives
     * screen-space LOD selection, and estimating it from patch size alone
     * would over-subdivide flat ground and under-subdivide mountains.
     */
    double GeometricErrorMeters = 0.0;

    int32 GetVertexCount() const { return PositionX.Num(); }
    int32 GetTriangleCount() const { return Indices.Num() / 3; }

    bool IsEmpty() const { return PositionX.Num() == 0; }

    void Reset();

    /**
     * Development-only validation: no NaN, no infinity, indices in range, no
     * degenerate triangles, normals unit length.
     *
     * Sprint 002 asks for numerical corruption to fail loudly rather than
     * propagate. A NaN vertex silently poisons a bounding box, which then
     * breaks culling for an entire component, and tracking that back to one bad
     * noise sample is far harder than catching it here.
     */
    bool Validate(FString& OutError) const;
};

/**
 * Builds patch meshes. Stateless and thread-safe.
 */
class UNIVERSEPLANET_API FPlanetPatchMeshBuilder
{
public:
    /**
     * Generates the mesh for one patch.
     *
     * Pure: the same arguments always produce the same arrays, so a patch can
     * be regenerated on any thread at any time and the result is identical.
     */
    static void Build(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FPlanetPatchId& PatchId,
        bool bGenerateSkirt,
        FPlanetPatchMesh& OutMesh);

    /**
     * How far a skirt hangs below the patch rim, as a fraction of the patch's
     * own size.
     *
     * Proportional rather than absolute so it covers the LOD gap at every
     * level: the gap a coarser neighbour leaves scales with patch size, so a
     * fixed skirt depth would be wasteful at low LOD and inadequate at high.
     */
    static constexpr double SkirtDepthFraction = 0.5;
};
