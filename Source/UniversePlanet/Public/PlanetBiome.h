// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetClimate.h"

/**
 * PlanetBiome.h
 *
 * Which environment a place is, decided by what its conditions are.
 *
 * The rule the whole sprint turns on: a forest exists because the conditions
 * support a forest. Nothing places a forest. The classifier reads temperature,
 * moisture, altitude and slope, and reports what those conditions produce.
 *
 *
 * WHY IT IS A TABLE AND NOT A FUNCTION
 *
 * The obvious implementation is a cascade of `if` statements, and it works
 * exactly until the first person wants to add a biome. Then every threshold has
 * to be re-checked by hand against every neighbour, because the branches are
 * ordered and the order is load-bearing.
 *
 * A table of *boxes* in climate space removes the ordering. Each biome declares
 * the range of conditions it lives in; a point is scored against every box by
 * how far outside it lies; the nearest wins. Adding a biome is adding a row, and
 * a row that overlaps a neighbour produces a blend rather than a contradiction.
 * That is also what makes alien biospheres a data change rather than a code
 * change - see EPlanetBiosphere.
 *
 *
 * WHY THE RESULT IS A BLEND
 *
 * A single winner puts a hard line across the planet wherever two boxes meet,
 * and hard lines between a forest and a desert look exactly as wrong as they
 * sound. So the classifier returns the best few biomes with weights, and every
 * consumer - terrain material, vegetation density, wildlife selection - is
 * expected to interpolate rather than to switch.
 *
 * The weights come from a softmax over the box distances, which has the useful
 * property that a point deep inside one box gets essentially all of the weight
 * while a point on a boundary gets an even split, with a smooth transition
 * between the two and no tuning to make it so.
 *
 * Pure data and arithmetic. No engine types, no asset paths.
 */

/**
 * The biomes.
 *
 * Deliberately a small set. Fifty biomes is a content decision dressed up as an
 * architecture, and the architecture that matters is the table, not its length.
 *
 * Names are families rather than specific ecosystems - "Rainforest" covers
 * anything hot and very wet, whatever the biosphere fills it with. That is what
 * lets one enumeration serve a terrestrial world and a fungal one: the
 * *conditions* are the same, and only the content differs.
 */
enum class EPlanetBiome : uint8
{
    Ocean = 0,
    Coast,
    Desert,
    Grassland,
    Savanna,
    TemperateForest,
    Rainforest,
    Wetland,
    Taiga,
    Tundra,
    Snow,
    BarrenRock,

    Count
};

UNIVERSEPLANET_API const TCHAR* LexToString(EPlanetBiome Biome);

/**
 * How a biome should be drawn, before any material work.
 *
 * A colour on the definition rather than in a renderer, because it is what the
 * planet looks like from orbit - the broad albedo of a region is a property of
 * the biome, not a presentation detail - and because the debug overlay and the
 * terrain material should never be able to disagree about which green a forest
 * is. Linear sRGB, each channel in [0, 1].
 */
struct UNIVERSEPLANET_API FBiomeAppearance
{
    double R = 0.5;
    double G = 0.5;
    double B = 0.5;

    /** Ground roughness hint in [0, 1]: sand is smooth, rock is not. */
    double Roughness = 0.8;
};

/**
 * One row of the biome table: the conditions a biome occupies, and what it
 * looks like.
 *
 * Ranges are inclusive boxes in climate space. A sample inside every range
 * scores zero distance; outside, the distance is measured in units of the
 * range's own width, so a biome with a narrow temperature band is not
 * automatically beaten by one with a wide band.
 */
struct UNIVERSEPLANET_API FBiomeDefinition
{
    EPlanetBiome Biome = EPlanetBiome::BarrenRock;

    double MinTemperatureK = 0.0;
    double MaxTemperatureK = 1000.0;

    double MinHumidity = 0.0;
    double MaxHumidity = 1.0;

    /** Altitude above the ocean surface, in metres. */
    double MinAltitudeMeters = -100000.0;
    double MaxAltitudeMeters = 100000.0;

    /**
     * Flatness required, as a minimum slope cosine. 1 needs perfectly flat
     * ground; 0 accepts anything.
     */
    double MinSlopeCosine = 0.0;

    /** True for biomes that only exist under water. */
    bool bRequiresWater = false;

    /** True for biomes that cannot exist under water. */
    bool bRequiresLand = true;

    FBiomeAppearance Appearance;

    /** How strongly this biome pulls, all else equal. Breaks ties. */
    double Weight = 1.0;
};

/**
 * The classification result: the dominant biome plus the runners-up.
 *
 * Four entries because that is how many can plausibly meet at a point in a
 * four-dimensional climate space, and because four fits a vertex colour and a
 * material's layer blend without further compression.
 */
struct UNIVERSEPLANET_API FBiomeBlend
{
    static constexpr int32 MaxEntries = 4;

    EPlanetBiome Biomes[MaxEntries] = {
        EPlanetBiome::BarrenRock, EPlanetBiome::BarrenRock,
        EPlanetBiome::BarrenRock, EPlanetBiome::BarrenRock };

    /** Weights, summing to 1, in descending order. */
    double Weights[MaxEntries] = { 1.0, 0.0, 0.0, 0.0 };

    int32 Num = 1;

    EPlanetBiome GetDominant() const { return Biomes[0]; }
    double GetDominantWeight() const { return Weights[0]; }

    /** Weight of a particular biome in this blend, or zero. */
    double GetWeightOf(EPlanetBiome Biome) const;

    /** The blended appearance, for the material and the debug overlay. */
    FBiomeAppearance GetAppearance(EPlanetBiosphere Biosphere) const;
};

class UNIVERSEPLANET_API FPlanetBiomes
{
public:
    /**
     * The table for a biosphere.
     *
     * Barren and Terrestrial share the same climate boxes and differ only in
     * appearance and in what grows; Fungal shifts the boxes slightly, because a
     * fungal biosphere plausibly tolerates cold and dark better than a
     * photosynthetic one. The important part is that the *shape* of the system
     * does not change between them.
     */
    static TArrayView<const FBiomeDefinition> GetTable(EPlanetBiosphere Biosphere);

    /** The definition for one biome under one biosphere. */
    static const FBiomeDefinition& GetDefinition(EPlanetBiosphere Biosphere, EPlanetBiome Biome);

    /**
     * Classifies a climate sample.
     *
     * Water is decided before the table is consulted, not by it: whether a
     * point is submerged is a fact about geometry, and letting a temperature
     * range have an opinion about it would eventually put a desert on a
     * sea floor.
     */
    static FBiomeBlend Classify(
        const FPlanetEnvironmentDescriptor& Environment,
        const FClimateSample& Climate);

    /**
     * How sharply the blend favours the best match.
     *
     * Larger is sharper. 6 gives a transition zone a few hundred metres wide at
     * a typical climate gradient - wide enough not to read as a line, narrow
     * enough that a forest still looks like a forest rather than a smear.
     */
    static constexpr double BlendSharpness = 6.0;
};
