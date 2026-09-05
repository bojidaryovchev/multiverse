// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetPatchId.h"
#include "PlanetSurface.h"

/**
 * PlanetQuadtree.h
 *
 * Decides which patches should exist right now.
 *
 * A pure data structure, evaluated from an observer position. It creates no
 * Actors and owns no meshes - the streaming layer above reads the selected set
 * and reconciles whatever it currently has with it. Sprint 002 is explicit that
 * the Actor hierarchy must not become the authoritative quadtree, and keeping
 * this side free of engine types is what enforces that: it is not possible to
 * accidentally make an Actor the source of truth from in here.
 *
 *
 * LOD BY SCREEN-SPACE ERROR
 *
 * Splitting on raw distance is the obvious approach and is wrong in a way that
 * only shows up later: the right threshold depends on planet radius, patch
 * resolution, field of view and screen height, so a distance tuned for one
 * planet misbehaves on every other one.
 *
 * Instead a patch is split when the terrain detail it is failing to represent
 * would be visible:
 *
 *     screen error (pixels) = geometric error (m) * projection / distance (m)
 *
 *     projection = screen height (px) / (2 * tan(vertical FOV / 2))
 *
 * Geometric error is the measured deviation between a patch's triangles and the
 * true terrain (see FPlanetPatchMesh::GeometricErrorMeters). Until a patch has
 * been built that measurement does not exist, so selection uses a conservative
 * estimate derived from patch size and terrain relief; the measured value
 * refines it once available.
 *
 * The consequence worth noting: this is automatically scale-invariant. A planet
 * ten times larger subtends ten times the angle at the same distance and
 * subdivides correspondingly, with no per-planet tuning.
 *
 *
 * HYSTERESIS
 *
 * Split and merge use different thresholds. With a single threshold an observer
 * hovering at the boundary makes a patch split, which immediately satisfies the
 * merge condition, which splits again - rebuilding meshes every frame forever.
 * The merge threshold is a fraction of the split threshold, so a patch that has
 * split must be left visibly further behind before it merges back.
 *
 *
 * NEIGHBOUR BALANCING
 *
 * Adjacent selected patches differ by at most one level. Skirts hide a
 * one-level T-junction comfortably; a five-level difference would need a skirt
 * deeper than the patch is wide, which would be visible from orbit. Balancing
 * runs after selection and forces neighbours to split until the constraint
 * holds.
 */

/** Where the observer is, and what the projection looks like. */
struct UNIVERSEPLANET_API FPlanetLodContext
{
    /** Observer position in planet-centred metres. */
    FVector3d ObserverPositionMeters = FVector3d::ZeroVector;

    /** Vertical field of view, radians. */
    double VerticalFovRadians = 1.5707963267948966;  // 90 degrees

    /** Viewport height in pixels. */
    double ScreenHeightPixels = 1080.0;

    /**
     * Split when a patch's error exceeds this many pixels.
     *
     * Lower means more geometry and a sharper silhouette. Around 4-8 px is the
     * usual range: below about 2 the subdivision cost climbs steeply for detail
     * at the edge of perceptibility.
     */
    double SplitPixelError = 6.0;

    /**
     * Merge threshold as a fraction of the split threshold.
     *
     * 0.5 means a patch must fall to half the error that caused it to split
     * before merging back, which in practice means moving a meaningful distance
     * rather than jittering on the spot.
     */
    double MergeHysteresis = 0.5;

    /** Hard ceiling on subdivision depth. */
    uint8 MaxLevel = 16;

    /** Never subdivide below this, so a planet is never invisible. */
    uint8 MinLevel = 0;

    /** Bound on selected patches, so a pathological configuration degrades
     *  rather than exhausting memory. */
    int32 MaxSelectedPatches = 4096;

    /** Pixels per metre of geometric error at one metre distance. */
    double GetProjectionFactor() const
    {
        const double HalfFov = VerticalFovRadians * 0.5;
        const double TanHalfFov = FMath::Tan(HalfFov);
        if (TanHalfFov <= 0.0)
        {
            return ScreenHeightPixels;
        }
        return ScreenHeightPixels / (2.0 * TanHalfFov);
    }
};

/** One patch chosen for display, with the numbers that chose it. */
struct UNIVERSEPLANET_API FPlanetSelectedPatch
{
    FPlanetPatchId PatchId;

    /** Observer distance to the patch's surface, metres. */
    double DistanceMeters = 0.0;

    /** Estimated screen-space error, pixels. */
    double ScreenErrorPixels = 0.0;

    /** True if the patch was split by neighbour balancing rather than by its
     *  own error - useful for spotting an over-aggressive constraint. */
    bool bForcedByBalancing = false;
};

/** What one selection pass did, for the debug overlay. */
struct UNIVERSEPLANET_API FPlanetSelectionStats
{
    int32 SelectedPatches = 0;
    int32 NodesVisited = 0;
    int32 HorizonCulled = 0;
    int32 BalancingSplits = 0;
    uint8 DeepestLevel = 0;
    bool bHitPatchLimit = false;
};

/**
 * A unique 64-bit key for a patch address, used for set membership.
 *
 * Packs face, level, Y and X into disjoint bit ranges. Exact rather than a
 * hash: this decides whether a patch is in the selection, and a hash collision
 * would silently drop a patch from the balancing constraint and open a crack.
 * X and Y fit in 24 bits each because MaxLevel is 24.
 */
inline uint64 MakePlanetPatchKey(const FPlanetPatchId& Id)
{
    return (static_cast<uint64>(Id.Face) << 56)
         | (static_cast<uint64>(Id.Level) << 48)
         | (static_cast<uint64>(Id.Y) << 24)
         | static_cast<uint64>(Id.X);
}

class UNIVERSEPLANET_API FPlanetQuadtree
{
public:
    /**
     * Chooses the patches that should be active for an observer.
     *
     * Deterministic: the same planet and context always give the same set, in
     * the same order. Output is sorted by patch address, never by distance, so
     * a caller diffing successive frames sees a stable ordering.
     */
    static void SelectPatches(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FPlanetLodContext& Context,
        TArray<FPlanetSelectedPatch>& OutPatches,
        FPlanetSelectionStats& OutStats);

    /**
     * Conservative estimate of a patch's geometric error before it is built.
     *
     * Two contributions: the sampling interval cannot represent features
     * smaller than itself, and terrain relief within the patch bounds how far
     * the true surface can depart from a flat approximation. Deliberately an
     * over-estimate - subdividing slightly too eagerly costs frame time, while
     * under-estimating leaves visible faceting that no later pass can repair.
     */
    static double EstimateGeometricErrorMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FPlanetPatchId& PatchId);

    /**
     * Whether a patch is entirely beyond the horizon.
     *
     * Uses the real sphere-horizon condition rather than a hemisphere test. For
     * an observer at radius r above a sphere of radius R, a point at radius Rp
     * in direction d is hidden when the observer-to-point vector points into
     * the sphere far enough that the line of sight would pass below the
     * surface. Being conservative here matters more than being tight: terrain
     * wrongly culled is a hole in the planet, whereas terrain wrongly kept is
     * merely some wasted triangles.
     */
    static bool IsBeyondHorizon(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetPatchId& PatchId,
        const FVector3d& ObserverPositionMeters);

    /** Distance from an observer to the closest point of a patch, metres. */
    static double GetDistanceToPatchMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetPatchId& PatchId,
        const FVector3d& ObserverPositionMeters);

    /**
     * Stateless selection with an explicit set of nodes that were split last
     * time, which is what makes hysteresis possible.
     *
     * A node in PreviouslySplit is re-split at the lower merge threshold rather
     * than the split threshold, so it must fall visibly further behind before
     * it collapses back. Callers that do not care - tests, one-off queries -
     * use SelectPatches and get no hysteresis, which is the honest default for
     * a function with no memory.
     */
    static void SelectPatchesWithHistory(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FPlanetLodContext& Context,
        const TArray<uint64>& PreviouslySplitSorted,
        TArray<FPlanetSelectedPatch>& OutPatches,
        TArray<uint64>& OutSplitSorted,
        FPlanetSelectionStats& OutStats);

private:
    static void SelectRecursive(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FPlanetLodContext& Context,
        const FPlanetPatchId& PatchId,
        double ProjectionFactor,
        const TArray<uint64>& PreviouslySplitSorted,
        TArray<FPlanetSelectedPatch>& OutPatches,
        TArray<uint64>& OutSplitSorted,
        FPlanetSelectionStats& OutStats);

    static void EnforceNeighbourBalance(
        TArray<FPlanetSelectedPatch>& Patches,
        const FPlanetLodContext& Context,
        FPlanetSelectionStats& OutStats);
};

/**
 * Stateful selector: remembers what was split last frame so hysteresis works.
 *
 * Hysteresis needs memory, and the selection functions above deliberately have
 * none. This is the thin layer that carries it. One instance per observed
 * planet; not thread-safe, and expected to be driven from whichever thread owns
 * the streaming decisions.
 */
class UNIVERSEPLANET_API FPlanetQuadtreeSelector
{
public:
    /** Selects patches, applying hysteresis against the previous call. */
    void Select(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FPlanetLodContext& Context,
        TArray<FPlanetSelectedPatch>& OutPatches,
        FPlanetSelectionStats& OutStats);

    /** Forgets history, so the next selection behaves as a first frame. */
    void Reset() { SplitNodes.Reset(); }

    /** How many interior nodes were split last selection. */
    int32 GetSplitNodeCount() const { return SplitNodes.Num(); }

private:
    /** Sorted keys of nodes split by the previous selection. */
    TArray<uint64> SplitNodes;

    /** Scratch, reused so a per-frame selection allocates nothing. */
    TArray<uint64> NextSplitNodes;
};
