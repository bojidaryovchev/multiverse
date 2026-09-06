// Copyright Universe Project. All Rights Reserved.

#include "PlanetBiome.h"

const TCHAR* LexToString(EPlanetBiome Biome)
{
    switch (Biome)
    {
    case EPlanetBiome::Ocean:           return TEXT("Ocean");
    case EPlanetBiome::Coast:           return TEXT("Coast");
    case EPlanetBiome::Desert:          return TEXT("Desert");
    case EPlanetBiome::Grassland:       return TEXT("Grassland");
    case EPlanetBiome::Savanna:         return TEXT("Savanna");
    case EPlanetBiome::TemperateForest: return TEXT("TemperateForest");
    case EPlanetBiome::Rainforest:      return TEXT("Rainforest");
    case EPlanetBiome::Wetland:         return TEXT("Wetland");
    case EPlanetBiome::Taiga:           return TEXT("Taiga");
    case EPlanetBiome::Tundra:          return TEXT("Tundra");
    case EPlanetBiome::Snow:            return TEXT("Snow");
    case EPlanetBiome::BarrenRock:      return TEXT("BarrenRock");
    default:                            return TEXT("Unknown");
    }
}

namespace
{
    constexpr double C(double Celsius) { return Celsius + 273.15; }

    /**
     * The terrestrial table.
     *
     * The temperature and moisture boxes are a coarse Whittaker diagram - the
     * standard biologists' plot of biome against mean temperature and annual
     * precipitation - which is where the shape of this comes from rather than
     * from taste. Altitude and slope are added because a planet has mountains
     * and a Whittaker diagram does not.
     *
     * Overlaps between rows are intentional. They are what produces blends;
     * a table of disjoint boxes would put a hard line at every boundary.
     */
    const FBiomeDefinition GTerrestrialTable[] =
    {
        // Water. Classified before the table is consulted, but present so the
        // appearance and the enumeration have one home.
        { EPlanetBiome::Ocean, 0.0, 1000.0, 0.0, 1.0, -100000.0, 0.0, 0.0,
          /*water*/ true, /*land*/ false, { 0.02, 0.09, 0.22, 0.05 }, 1.0 },

        // A narrow band just above the waterline: sand, shingle, salt-tolerant
        // scrub. Bounded by altitude alone, so the coastline follows the
        // terrain rather than being drawn anywhere.
        { EPlanetBiome::Coast, C(-5.0), C(45.0), 0.0, 1.0, 0.0, 60.0, 0.0,
          false, true, { 0.72, 0.66, 0.48, 0.55 }, 1.15 },

        { EPlanetBiome::Desert, C(18.0), C(60.0), 0.0, 0.22, 0.0, 4000.0, 0.55,
          false, true, { 0.78, 0.68, 0.46, 0.45 }, 1.0 },

        { EPlanetBiome::Savanna, C(18.0), C(40.0), 0.20, 0.45, 0.0, 2500.0, 0.6,
          false, true, { 0.65, 0.58, 0.28, 0.7 }, 1.0 },

        { EPlanetBiome::Grassland, C(2.0), C(28.0), 0.25, 0.55, 0.0, 3000.0, 0.6,
          false, true, { 0.34, 0.46, 0.18, 0.75 }, 1.0 },

        { EPlanetBiome::TemperateForest, C(3.0), C(24.0), 0.45, 0.85, 0.0, 2600.0, 0.45,
          false, true, { 0.14, 0.32, 0.12, 0.85 }, 1.0 },

        { EPlanetBiome::Rainforest, C(20.0), C(38.0), 0.68, 1.0, 0.0, 2000.0, 0.4,
          false, true, { 0.08, 0.30, 0.08, 0.9 }, 1.0 },

        // Standing water on land: swamps, marsh, bog. Needs flat ground and a
        // lot of moisture, which is exactly what makes water stand.
        { EPlanetBiome::Wetland, C(2.0), C(32.0), 0.78, 1.0, 0.0, 400.0, 0.93,
          false, true, { 0.20, 0.32, 0.20, 0.6 }, 1.1 },

        { EPlanetBiome::Taiga, C(-12.0), C(8.0), 0.35, 0.85, 0.0, 2200.0, 0.5,
          false, true, { 0.13, 0.24, 0.16, 0.85 }, 1.0 },

        { EPlanetBiome::Tundra, C(-25.0), C(4.0), 0.15, 0.7, 0.0, 3200.0, 0.5,
          false, true, { 0.36, 0.36, 0.28, 0.8 }, 1.0 },

        { EPlanetBiome::Snow, 0.0, C(-1.0), 0.0, 1.0, 0.0, 100000.0, 0.4,
          false, true, { 0.90, 0.92, 0.95, 0.3 }, 1.05 },

        // The fallback, and the reason steep ground is rock everywhere. Its
        // slope requirement is inverted relative to every other row: it wants
        // ground too steep for soil to stay on.
        { EPlanetBiome::BarrenRock, 0.0, 1000.0, 0.0, 1.0, -100000.0, 100000.0, 0.0,
          false, true, { 0.33, 0.30, 0.27, 0.9 }, 0.55 },
    };

    /**
     * The fungal table.
     *
     * Same enumeration, same boxes in shape, shifted where a fungal biosphere
     * plausibly differs: it tolerates cold and low light better, so its forest
     * and grassland equivalents reach further toward the poles, and it needs
     * more moisture, because fungus does.
     *
     * The colours are the visible difference. This is the architectural claim
     * of section 64 made concrete - an alien world is a different table, not a
     * different planet generator.
     */
    const FBiomeDefinition GFungalTable[] =
    {
        { EPlanetBiome::Ocean, 0.0, 1000.0, 0.0, 1.0, -100000.0, 0.0, 0.0,
          true, false, { 0.06, 0.10, 0.14, 0.05 }, 1.0 },

        { EPlanetBiome::Coast, C(-15.0), C(45.0), 0.0, 1.0, 0.0, 60.0, 0.0,
          false, true, { 0.50, 0.44, 0.46, 0.55 }, 1.15 },

        { EPlanetBiome::Desert, C(10.0), C(60.0), 0.0, 0.20, 0.0, 4000.0, 0.55,
          false, true, { 0.55, 0.44, 0.44, 0.5 }, 1.0 },

        { EPlanetBiome::Savanna, C(8.0), C(40.0), 0.18, 0.42, 0.0, 2500.0, 0.6,
          false, true, { 0.46, 0.38, 0.44, 0.7 }, 1.0 },

        // Spore meadows.
        { EPlanetBiome::Grassland, C(-8.0), C(26.0), 0.22, 0.55, 0.0, 3000.0, 0.6,
          false, true, { 0.36, 0.28, 0.46, 0.75 }, 1.0 },

        // Mushroom canopy.
        { EPlanetBiome::TemperateForest, C(-10.0), C(24.0), 0.42, 0.85, 0.0, 2800.0, 0.45,
          false, true, { 0.30, 0.16, 0.38, 0.85 }, 1.0 },

        // Dense bioluminescent growth.
        { EPlanetBiome::Rainforest, C(10.0), C(38.0), 0.65, 1.0, 0.0, 2200.0, 0.4,
          false, true, { 0.16, 0.34, 0.40, 0.9 }, 1.0 },

        { EPlanetBiome::Wetland, C(-6.0), C(32.0), 0.72, 1.0, 0.0, 400.0, 0.93,
          false, true, { 0.22, 0.30, 0.34, 0.6 }, 1.1 },

        { EPlanetBiome::Taiga, C(-24.0), C(6.0), 0.32, 0.85, 0.0, 2400.0, 0.5,
          false, true, { 0.24, 0.18, 0.30, 0.85 }, 1.0 },

        { EPlanetBiome::Tundra, C(-35.0), C(2.0), 0.12, 0.7, 0.0, 3400.0, 0.5,
          false, true, { 0.34, 0.30, 0.36, 0.8 }, 1.0 },

        { EPlanetBiome::Snow, 0.0, C(-8.0), 0.0, 1.0, 0.0, 100000.0, 0.4,
          false, true, { 0.86, 0.88, 0.94, 0.3 }, 1.05 },

        { EPlanetBiome::BarrenRock, 0.0, 1000.0, 0.0, 1.0, -100000.0, 100000.0, 0.0,
          false, true, { 0.30, 0.26, 0.30, 0.9 }, 0.55 },
    };

    /**
     * Distance outside a range, in units of the range's own width.
     *
     * Normalising by the width is what stops a biome with a wide temperature
     * band winning everywhere simply for being permissive. A point one degree
     * outside a five-degree band is further out, in the sense that matters,
     * than a point one degree outside a fifty-degree one.
     */
    double RangeDistance(double Value, double Min, double Max)
    {
        const double Width = FMath::Max(Max - Min, 1.0e-6);

        if (Value < Min)
        {
            return (Min - Value) / Width;
        }

        if (Value > Max)
        {
            return (Value - Max) / Width;
        }

        return 0.0;
    }

    double ScoreBiome(const FBiomeDefinition& Definition, const FClimateSample& Climate)
    {
        double Distance = 0.0;

        Distance += RangeDistance(Climate.TemperatureK, Definition.MinTemperatureK, Definition.MaxTemperatureK);
        Distance += RangeDistance(Climate.Humidity, Definition.MinHumidity, Definition.MaxHumidity);

        // Altitude distances are divided by their own width like the others,
        // but those widths are kilometres, so the term is naturally gentle -
        // altitude nudges a classification rather than deciding it, except at
        // the extremes where the ranges are tight.
        Distance += RangeDistance(
            Climate.AltitudeAboveOceanMeters, Definition.MinAltitudeMeters, Definition.MaxAltitudeMeters);

        // Slope is one-sided: a biome needs ground flat enough, and there is no
        // such thing as ground too flat for a forest.
        if (Climate.SlopeCosine < Definition.MinSlopeCosine)
        {
            const double Shortfall = Definition.MinSlopeCosine - Climate.SlopeCosine;

            // Weighted heavily, because it is the term that puts rock on cliffs
            // and it should not be outvoted by a good temperature match.
            Distance += Shortfall * 4.0;
        }

        return Distance;
    }
}

TArrayView<const FBiomeDefinition> FPlanetBiomes::GetTable(EPlanetBiosphere Biosphere)
{
    switch (Biosphere)
    {
    case EPlanetBiosphere::Fungal:
        return TArrayView<const FBiomeDefinition>(GFungalTable, UE_ARRAY_COUNT(GFungalTable));

    case EPlanetBiosphere::Terrestrial:
    case EPlanetBiosphere::Barren:
    default:
        return TArrayView<const FBiomeDefinition>(GTerrestrialTable, UE_ARRAY_COUNT(GTerrestrialTable));
    }
}

const FBiomeDefinition& FPlanetBiomes::GetDefinition(EPlanetBiosphere Biosphere, EPlanetBiome Biome)
{
    const TArrayView<const FBiomeDefinition> Table = GetTable(Biosphere);

    for (const FBiomeDefinition& Definition : Table)
    {
        if (Definition.Biome == Biome)
        {
            return Definition;
        }
    }

    // The last row is BarrenRock in every table, and is the documented
    // fallback. Returning it is preferable to a null reference for a biome
    // that a caller has somehow constructed by hand.
    return Table[Table.Num() - 1];
}

double FBiomeBlend::GetWeightOf(EPlanetBiome Biome) const
{
    for (int32 Index = 0; Index < Num; ++Index)
    {
        if (Biomes[Index] == Biome)
        {
            return Weights[Index];
        }
    }

    return 0.0;
}

FBiomeAppearance FBiomeBlend::GetAppearance(EPlanetBiosphere Biosphere) const
{
    FBiomeAppearance Result;
    Result.R = 0.0;
    Result.G = 0.0;
    Result.B = 0.0;
    Result.Roughness = 0.0;

    for (int32 Index = 0; Index < Num; ++Index)
    {
        const FBiomeDefinition& Definition = FPlanetBiomes::GetDefinition(Biosphere, Biomes[Index]);
        const double Weight = Weights[Index];

        Result.R += Definition.Appearance.R * Weight;
        Result.G += Definition.Appearance.G * Weight;
        Result.B += Definition.Appearance.B * Weight;
        Result.Roughness += Definition.Appearance.Roughness * Weight;
    }

    return Result;
}

FBiomeBlend FPlanetBiomes::Classify(
    const FPlanetEnvironmentDescriptor& Environment,
    const FClimateSample& Climate)
{
    FBiomeBlend Blend;

    // Water is geometry, not climate.
    //
    // Deciding it here rather than letting the table have an opinion is what
    // guarantees that everything below the ocean surface is ocean. A
    // temperature range that happened to reach far enough would otherwise put a
    // desert on a sea floor, and that failure would be invisible until someone
    // flew over the right stretch of water.
    if (Climate.bOcean)
    {
        Blend.Biomes[0] = EPlanetBiome::Ocean;
        Blend.Weights[0] = 1.0;
        Blend.Num = 1;
        return Blend;
    }

    const TArrayView<const FBiomeDefinition> Table = GetTable(Environment.Biosphere);

    // Score every land biome. The table is a dozen rows of arithmetic, so
    // scoring all of them costs less than the branch tree that would avoid it.
    double Scores[static_cast<int32>(EPlanetBiome::Count)];
    EPlanetBiome Candidates[static_cast<int32>(EPlanetBiome::Count)];
    int32 CandidateCount = 0;

    double BestDistance = TNumericLimits<double>::Max();

    for (const FBiomeDefinition& Definition : Table)
    {
        if (Definition.bRequiresWater)
        {
            continue;
        }

        const double Distance = ScoreBiome(Definition, Climate);

        // The weight is a prior, applied as a discount on distance rather than
        // as a multiplier on the final weight, so that a biome with a strong
        // prior wins ties without being able to override a genuinely poor
        // climate match.
        const double Adjusted = Distance / FMath::Max(Definition.Weight, 1.0e-3);

        Scores[CandidateCount] = Adjusted;
        Candidates[CandidateCount] = Definition.Biome;
        ++CandidateCount;

        BestDistance = FMath::Min(BestDistance, Adjusted);
    }

    if (CandidateCount == 0)
    {
        return Blend;
    }

    // Softmax over the negated distances, offset by the best so the exponent
    // never overflows and the best entry always contributes exactly 1.
    double Total = 0.0;

    for (int32 Index = 0; Index < CandidateCount; ++Index)
    {
        const double Excess = Scores[Index] - BestDistance;

        // Exp is the one transcendental on this path. It is acceptable here in
        // a way it would not be on the terrain path: biome weights feed
        // appearance and density, never geometry, so a last-bit difference
        // between platforms changes a blend imperceptibly rather than moving
        // ground somebody built on. The classification itself - which biome
        // wins - is decided by the comparison above, which is exact.
        Scores[Index] = FMath::Exp(-Excess * BlendSharpness);
        Total += Scores[Index];
    }

    // Take the best MaxEntries by selection, which for four out of twelve is
    // cheaper and simpler than sorting.
    const double InverseTotal = (Total > 0.0) ? (1.0 / Total) : 0.0;

    bool bTaken[static_cast<int32>(EPlanetBiome::Count)] = {};

    Blend.Num = 0;

    for (int32 Slot = 0; Slot < FBiomeBlend::MaxEntries; ++Slot)
    {
        int32 BestIndex = INDEX_NONE;
        double BestWeight = 0.0;

        for (int32 Index = 0; Index < CandidateCount; ++Index)
        {
            if (!bTaken[Index] && Scores[Index] > BestWeight)
            {
                BestWeight = Scores[Index];
                BestIndex = Index;
            }
        }

        if (BestIndex == INDEX_NONE)
        {
            break;
        }

        bTaken[BestIndex] = true;

        const double Weight = Scores[BestIndex] * InverseTotal;

        // Stop once the remaining entries are too faint to matter. Carrying a
        // 0.3% contribution costs a material layer and changes nothing.
        if (Blend.Num > 0 && Weight < 0.01)
        {
            break;
        }

        Blend.Biomes[Blend.Num] = Candidates[BestIndex];
        Blend.Weights[Blend.Num] = Weight;
        ++Blend.Num;
    }

    // Renormalise, since the tail was discarded.
    double Kept = 0.0;

    for (int32 Index = 0; Index < Blend.Num; ++Index)
    {
        Kept += Blend.Weights[Index];
    }

    if (Kept > 0.0)
    {
        for (int32 Index = 0; Index < Blend.Num; ++Index)
        {
            Blend.Weights[Index] /= Kept;
        }
    }

    return Blend;
}
