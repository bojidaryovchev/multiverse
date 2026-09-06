// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetBiome.h"
#include "PlanetPatchId.h"

/**
 * PlanetVegetation.h
 *
 * What grows where, and exactly where each of it stands.
 *
 * Two separable problems, and they are kept separate on purpose:
 *
 *   1. **What a biome grows.** A data table from biome to content categories
 *      and densities. No asset paths, no mesh names, no Unreal types.
 *   2. **Where the individuals go.** A deterministic scatter over a terrain
 *      patch that answers, for a given patch, exactly which things stand where.
 *
 * The first is a designer's problem and the second is a mathematician's, and
 * mixing them is how a vegetation system ends up with tree species hardcoded
 * into placement loops.
 *
 *
 * WHY ARCHETYPES AND NOT ASSETS
 *
 * A profile says "coniferous canopy tree, 40 per hectare", never
 * "/Game/Trees/SM_Pine_02". Which mesh represents a coniferous canopy tree is a
 * rendering decision that belongs in the Unreal layer, and keeping it there is
 * what lets this module stay engine-free and run on a worker thread.
 *
 * It is also what makes an alien biosphere possible without touching this file.
 * A fungal world's forest is a MushroomCanopy at the same density in the same
 * biome; only the archetype changes. Nothing here believes a tree is a tree.
 *
 *
 * WHY PLACEMENT IS A PURE FUNCTION OF THE PATCH
 *
 * Every instance is derived from `hash(planet seed, patch id, cell index)`, so
 * a patch scattered twice produces the identical result, and a player who
 * leaves a forest and comes back finds the same trees in the same places. No
 * state is kept and nothing is written down. That is the same argument as the
 * terrain function's, applied one level up.
 *
 * It also means placement can run on any thread, in any order, for any patch,
 * which is what the streamer needs.
 */

/**
 * Content categories. Deliberately about *form*, not species.
 *
 * The Unreal layer maps each of these to one or more meshes. A category with no
 * mesh assigned simply produces nothing, which is what makes the table safe to
 * extend before the content exists.
 */
enum class EVegetationArchetype : uint8
{
    None = 0,

    // Terrestrial
    BroadleafTree,
    ConiferTree,
    PalmTree,
    DeadTree,
    Shrub,
    Cactus,
    Grass,
    Fern,
    Flower,

    // Mineral, on every world
    Rock,
    Boulder,

    // Fungal
    MushroomCanopy,
    MushroomCluster,
    SporeStalk,
    BioluminescentPlant,

    Count
};

UNIVERSEPLANET_API const TCHAR* LexToString(EVegetationArchetype Archetype);

/**
 * Which streaming layer an archetype belongs to.
 *
 * Layers exist because the three have wildly different costs and ranges. A
 * canopy tree is visible from kilometres away and there are tens per hectare; a
 * grass blade is invisible past thirty metres and there are tens of thousands.
 * Treating them the same guarantees one of the two is wrong.
 */
enum class EVegetationLayer : uint8
{
    /** Trees. Sparse, large, collidable, visible from far away. */
    Canopy = 0,

    /** Shrubs, ferns, cacti. Middling in every respect. */
    Understory,

    /** Grass and flowers. Very dense, very short range, no collision. */
    Ground,

    /** Rocks and boulders. Sparse, collidable, present in every biome. */
    Scatter,

    Count
};

UNIVERSEPLANET_API const TCHAR* LexToString(EVegetationLayer Layer);

/** One kind of thing a biome grows, and how much of it. */
struct UNIVERSEPLANET_API FVegetationEntry
{
    EVegetationArchetype Archetype = EVegetationArchetype::None;
    EVegetationLayer Layer = EVegetationLayer::Canopy;

    /**
     * Instances per hectare at full vegetation potential.
     *
     * Per hectare rather than per patch, because a patch's area changes by a
     * factor of four at every LOD level and a per-patch density would make a
     * forest thin out as the player walked toward it.
     */
    double DensityPerHectare = 0.0;

    double MinScale = 0.85;
    double MaxScale = 1.25;

    /**
     * Flattest ground this will not grow on, and steepest it will.
     *
     * Expressed as slope cosines to match FClimateSample, so no conversion
     * happens at the call site.
     */
    double MinSlopeCosine = 0.55;

    /** Minimum altitude above the ocean, metres. Keeps things off beaches. */
    double MinAltitudeMeters = 0.0;
};

/** Everything one biome grows. */
struct UNIVERSEPLANET_API FVegetationProfile
{
    static constexpr int32 MaxEntries = 8;

    FVegetationEntry Entries[MaxEntries];
    int32 Num = 0;

    /** Total density of one layer, per hectare. */
    double GetLayerDensity(EVegetationLayer Layer) const;
};

/**
 * One placed thing.
 *
 * Plain data with no engine types, so a worker thread can produce an array of
 * these and the game thread can turn them into instances without any conversion
 * beyond units.
 */
struct UNIVERSEPLANET_API FVegetationInstance
{
    /** Planet-local position, metres. On the terrain surface. */
    FVector3d PositionMeters = FVector3d::ZeroVector;

    /** Local up at that point - away from the planet centre. */
    FVector3d UpUnit = FVector3d(0.0, 0.0, 1.0);

    /** Terrain normal. Rocks lie along it; trees stand along up. */
    FVector3d NormalUnit = FVector3d(0.0, 0.0, 1.0);

    /** Rotation about up, radians. */
    double YawRadians = 0.0;

    double Scale = 1.0;

    EVegetationArchetype Archetype = EVegetationArchetype::None;
    EVegetationLayer Layer = EVegetationLayer::Canopy;

    /** The dominant biome where it stands, for debugging and for tinting. */
    EPlanetBiome Biome = EPlanetBiome::BarrenRock;

    /**
     * Which patch and grid cell placed it.
     *
     * Carried on the instance because it is the only *stable* name this thing
     * has. Its index in the output array is not: adding a slope filter or
     * retuning a density renumbers every instance after the first change, and a
     * removal recorded against an index would then delete a different tree.
     * The placement cell does not move when the rules change - it either
     * produces something or it does not.
     *
     * See WorldPersistenceIdentity.h.
     */
    FPlanetPatchId PatchId;
    int32 CellX = 0;
    int32 CellY = 0;
};

class UNIVERSEPLANET_API FPlanetVegetation
{
public:
    /** What a biome grows under a given biosphere. */
    static const FVegetationProfile& GetProfile(EPlanetBiosphere Biosphere, EPlanetBiome Biome);

    /**
     * Places everything of one layer within a patch.
     *
     * Walks a jittered grid over the patch, evaluates the terrain and climate at
     * each candidate, classifies the biome, and rolls against that biome's
     * density for the layer. The grid resolution is derived from the densest
     * entry in play, so a sparse layer costs few evaluations and a dense one
     * costs many, rather than both paying for the worst case.
     *
     * Deterministic: the same patch always yields the same instances, in the
     * same order.
     *
     * Returns the number placed, which may be below the roll if MaxInstances
     * was reached. Hitting the cap is reported through OutbHitBudget rather
     * than being silent, because a forest that stops at an invisible line is a
     * budget problem and should be diagnosable as one.
     */
    static int32 Scatter(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FPlanetTerrainSettings& Settings,
        const FPlanetPatchId& PatchId,
        EVegetationLayer Layer,
        int32 MaxInstances,
        TArray<FVegetationInstance>& OutInstances,
        bool& OutbHitBudget);

    /**
     * Grid resolution used for a layer at a patch size.
     *
     * Capped hard. A dense ground layer on a large patch would otherwise ask
     * for a grid of millions - the correct number for the density, and a
     * catastrophic one for a worker thread. The cap means low-LOD patches place
     * a representative sample rather than everything, which is the right answer
     * anyway: nobody can see individual grass from four kilometres up.
     */
    static int32 GetGridResolution(double PatchSizeMeters, double DensityPerHectare);

    /** Largest grid edge any single patch will be divided into. */
    static constexpr int32 MaxGridResolution = 96;
};
