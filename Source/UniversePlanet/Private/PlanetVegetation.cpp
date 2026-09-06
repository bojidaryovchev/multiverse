// Copyright Universe Project. All Rights Reserved.

#include "PlanetVegetation.h"
#include "PlanetTerrain.h"
#include "PlanetSurfaceQuery.h"
#include "CubeSphere.h"
#include "UniverseHash.h"

const TCHAR* LexToString(EVegetationArchetype Archetype)
{
    switch (Archetype)
    {
    case EVegetationArchetype::BroadleafTree:       return TEXT("BroadleafTree");
    case EVegetationArchetype::ConiferTree:         return TEXT("ConiferTree");
    case EVegetationArchetype::PalmTree:            return TEXT("PalmTree");
    case EVegetationArchetype::DeadTree:            return TEXT("DeadTree");
    case EVegetationArchetype::Shrub:               return TEXT("Shrub");
    case EVegetationArchetype::Cactus:              return TEXT("Cactus");
    case EVegetationArchetype::Grass:               return TEXT("Grass");
    case EVegetationArchetype::Fern:                return TEXT("Fern");
    case EVegetationArchetype::Flower:              return TEXT("Flower");
    case EVegetationArchetype::Rock:                return TEXT("Rock");
    case EVegetationArchetype::Boulder:             return TEXT("Boulder");
    case EVegetationArchetype::MushroomCanopy:      return TEXT("MushroomCanopy");
    case EVegetationArchetype::MushroomCluster:     return TEXT("MushroomCluster");
    case EVegetationArchetype::SporeStalk:          return TEXT("SporeStalk");
    case EVegetationArchetype::BioluminescentPlant: return TEXT("BioluminescentPlant");
    case EVegetationArchetype::None:                return TEXT("None");
    default:                                        return TEXT("Unknown");
    }
}

const TCHAR* LexToString(EVegetationLayer Layer)
{
    switch (Layer)
    {
    case EVegetationLayer::Canopy:     return TEXT("Canopy");
    case EVegetationLayer::Understory: return TEXT("Understory");
    case EVegetationLayer::Ground:     return TEXT("Ground");
    case EVegetationLayer::Scatter:    return TEXT("Scatter");
    default:                           return TEXT("Unknown");
    }
}

namespace
{
    using EArch = EVegetationArchetype;
    using ELayer = EVegetationLayer;

    /** Shorthand for a table row. */
    constexpr FVegetationEntry E(
        EArch Archetype, ELayer Layer, double Density,
        double MinScale, double MaxScale, double MinSlopeCosine, double MinAltitude = 0.0)
    {
        return FVegetationEntry{ Archetype, Layer, Density, MinScale, MaxScale, MinSlopeCosine, MinAltitude };
    }

    /**
     * Densities are per hectare, and roughly real.
     *
     * A managed temperate woodland runs 300-500 stems per hectare; a tropical
     * rainforest 400-600; a savanna perhaps 10-40. Using real numbers rather
     * than tuned ones means the *ratios* between biomes are right even before
     * anything is tuned for looks, and it makes the streaming budgets in the
     * Unreal layer meaningful rather than arbitrary.
     *
     * Grass is the exception: real grass is tens of thousands of blades per
     * hectare, and a mesh instance stands in for a clump rather than a blade.
     */
    const FVegetationProfile& MakeEmptyProfile()
    {
        static const FVegetationProfile Empty;
        return Empty;
    }

    FVegetationProfile MakeProfile(std::initializer_list<FVegetationEntry> Entries)
    {
        FVegetationProfile Profile;

        for (const FVegetationEntry& Entry : Entries)
        {
            if (Profile.Num < FVegetationProfile::MaxEntries)
            {
                Profile.Entries[Profile.Num++] = Entry;
            }
        }

        return Profile;
    }

    struct FProfileTable
    {
        FVegetationProfile Profiles[static_cast<int32>(EPlanetBiome::Count)];
    };

    const FProfileTable& GetTerrestrialProfiles()
    {
        static const FProfileTable Table = []
        {
            FProfileTable T;
            auto& P = T.Profiles;

            // Ocean and barren rock grow nothing. Rock still scatters rocks.
            P[static_cast<int32>(EPlanetBiome::Ocean)] = MakeProfile({});

            P[static_cast<int32>(EPlanetBiome::BarrenRock)] = MakeProfile({
                E(EArch::Rock,    ELayer::Scatter, 55.0, 0.5, 1.6, 0.0),
                E(EArch::Boulder, ELayer::Scatter,  8.0, 0.7, 2.2, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Coast)] = MakeProfile({
                E(EArch::PalmTree, ELayer::Canopy,      6.0, 0.8, 1.3, 0.85),
                E(EArch::Shrub,    ELayer::Understory, 25.0, 0.6, 1.2, 0.8),
                E(EArch::Grass,    ELayer::Ground,   1800.0, 0.5, 1.2, 0.85),
                E(EArch::Rock,     ELayer::Scatter,   30.0, 0.4, 1.1, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Desert)] = MakeProfile({
                E(EArch::Cactus,  ELayer::Understory,  9.0, 0.7, 1.6, 0.8),
                E(EArch::Shrub,   ELayer::Understory,  6.0, 0.5, 1.0, 0.75),
                E(EArch::Grass,   ELayer::Ground,    250.0, 0.4, 0.9, 0.8),
                E(EArch::Rock,    ELayer::Scatter,    40.0, 0.4, 1.5, 0.0),
                E(EArch::Boulder, ELayer::Scatter,     5.0, 0.8, 2.4, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Savanna)] = MakeProfile({
                E(EArch::BroadleafTree, ELayer::Canopy,     22.0, 0.9, 1.5, 0.75),
                E(EArch::Shrub,         ELayer::Understory, 45.0, 0.6, 1.2, 0.7),
                E(EArch::Grass,         ELayer::Ground,   5200.0, 0.7, 1.5, 0.75),
                E(EArch::Rock,          ELayer::Scatter,    12.0, 0.4, 1.2, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Grassland)] = MakeProfile({
                E(EArch::BroadleafTree, ELayer::Canopy,      9.0, 0.9, 1.4, 0.75),
                E(EArch::Shrub,         ELayer::Understory, 60.0, 0.6, 1.2, 0.7),
                E(EArch::Flower,        ELayer::Ground,    900.0, 0.7, 1.2, 0.85),
                E(EArch::Grass,         ELayer::Ground,   7500.0, 0.8, 1.4, 0.7),
                E(EArch::Rock,          ELayer::Scatter,    10.0, 0.4, 1.1, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::TemperateForest)] = MakeProfile({
                E(EArch::BroadleafTree, ELayer::Canopy,     310.0, 0.8, 1.6, 0.6),
                E(EArch::ConiferTree,   ELayer::Canopy,      90.0, 0.9, 1.7, 0.6),
                E(EArch::DeadTree,      ELayer::Canopy,      12.0, 0.8, 1.3, 0.6),
                E(EArch::Shrub,         ELayer::Understory, 220.0, 0.6, 1.3, 0.6),
                E(EArch::Fern,          ELayer::Understory, 400.0, 0.6, 1.2, 0.65),
                E(EArch::Grass,         ELayer::Ground,    3600.0, 0.6, 1.2, 0.65),
                E(EArch::Rock,          ELayer::Scatter,     18.0, 0.4, 1.3, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Rainforest)] = MakeProfile({
                E(EArch::BroadleafTree, ELayer::Canopy,     480.0, 0.9, 2.0, 0.55),
                E(EArch::PalmTree,      ELayer::Canopy,     120.0, 0.8, 1.6, 0.6),
                E(EArch::Fern,          ELayer::Understory, 900.0, 0.7, 1.5, 0.6),
                E(EArch::Shrub,         ELayer::Understory, 350.0, 0.7, 1.4, 0.6),
                E(EArch::Grass,         ELayer::Ground,    4200.0, 0.7, 1.4, 0.6),
                E(EArch::Rock,          ELayer::Scatter,     10.0, 0.4, 1.2, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Wetland)] = MakeProfile({
                E(EArch::DeadTree, ELayer::Canopy,      45.0, 0.8, 1.5, 0.9),
                E(EArch::Fern,     ELayer::Understory, 600.0, 0.7, 1.3, 0.9),
                E(EArch::Grass,    ELayer::Ground,    6800.0, 0.8, 1.6, 0.9),
            });

            P[static_cast<int32>(EPlanetBiome::Taiga)] = MakeProfile({
                E(EArch::ConiferTree, ELayer::Canopy,     340.0, 0.9, 1.8, 0.6),
                E(EArch::DeadTree,    ELayer::Canopy,      20.0, 0.8, 1.3, 0.6),
                E(EArch::Shrub,       ELayer::Understory, 120.0, 0.5, 1.1, 0.65),
                E(EArch::Grass,       ELayer::Ground,    1400.0, 0.5, 1.0, 0.7),
                E(EArch::Rock,        ELayer::Scatter,     25.0, 0.4, 1.4, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Tundra)] = MakeProfile({
                E(EArch::Shrub, ELayer::Understory,  55.0, 0.4, 0.9, 0.7),
                E(EArch::Grass, ELayer::Ground,     900.0, 0.4, 0.9, 0.7),
                E(EArch::Rock,  ELayer::Scatter,     45.0, 0.4, 1.5, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Snow)] = MakeProfile({
                E(EArch::Rock,    ELayer::Scatter, 20.0, 0.4, 1.4, 0.0),
                E(EArch::Boulder, ELayer::Scatter,  4.0, 0.8, 2.0, 0.0),
            });

            return T;
        }();

        return Table;
    }

    /**
     * The fungal profiles.
     *
     * Same biomes, same densities, different archetypes - which is the whole
     * demonstration. A fungal rainforest is as dense as a terrestrial one
     * because the *conditions* are what set the density; only the thing filling
     * the canopy differs.
     */
    const FProfileTable& GetFungalProfiles()
    {
        static const FProfileTable Table = []
        {
            FProfileTable T;
            auto& P = T.Profiles;

            P[static_cast<int32>(EPlanetBiome::Ocean)] = MakeProfile({});

            P[static_cast<int32>(EPlanetBiome::BarrenRock)] = MakeProfile({
                E(EArch::Rock,    ELayer::Scatter, 55.0, 0.5, 1.6, 0.0),
                E(EArch::Boulder, ELayer::Scatter,  8.0, 0.7, 2.2, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Coast)] = MakeProfile({
                E(EArch::MushroomCluster, ELayer::Understory,  40.0, 0.6, 1.4, 0.8),
                E(EArch::SporeStalk,      ELayer::Ground,    1400.0, 0.5, 1.3, 0.85),
                E(EArch::Rock,            ELayer::Scatter,     30.0, 0.4, 1.1, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Desert)] = MakeProfile({
                E(EArch::SporeStalk, ELayer::Understory, 12.0, 0.6, 1.5, 0.8),
                E(EArch::Rock,       ELayer::Scatter,    40.0, 0.4, 1.5, 0.0),
                E(EArch::Boulder,    ELayer::Scatter,     5.0, 0.8, 2.4, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Savanna)] = MakeProfile({
                E(EArch::MushroomCanopy,  ELayer::Canopy,      20.0, 1.0, 2.0, 0.75),
                E(EArch::MushroomCluster, ELayer::Understory,  60.0, 0.6, 1.3, 0.7),
                E(EArch::SporeStalk,      ELayer::Ground,    4800.0, 0.7, 1.5, 0.75),
            });

            P[static_cast<int32>(EPlanetBiome::Grassland)] = MakeProfile({
                E(EArch::MushroomCanopy,      ELayer::Canopy,        8.0, 0.9, 1.8, 0.75),
                E(EArch::MushroomCluster,     ELayer::Understory,   80.0, 0.6, 1.3, 0.7),
                E(EArch::BioluminescentPlant, ELayer::Ground,      700.0, 0.7, 1.3, 0.85),
                E(EArch::SporeStalk,          ELayer::Ground,     6800.0, 0.8, 1.5, 0.7),
            });

            P[static_cast<int32>(EPlanetBiome::TemperateForest)] = MakeProfile({
                E(EArch::MushroomCanopy,      ELayer::Canopy,      330.0, 1.0, 2.4, 0.6),
                E(EArch::MushroomCluster,     ELayer::Understory,  420.0, 0.6, 1.5, 0.6),
                E(EArch::BioluminescentPlant, ELayer::Understory,  180.0, 0.7, 1.4, 0.65),
                E(EArch::SporeStalk,          ELayer::Ground,     3400.0, 0.6, 1.3, 0.65),
                E(EArch::Rock,                ELayer::Scatter,      18.0, 0.4, 1.3, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Rainforest)] = MakeProfile({
                E(EArch::MushroomCanopy,      ELayer::Canopy,      560.0, 1.1, 3.0, 0.55),
                E(EArch::BioluminescentPlant, ELayer::Understory,  800.0, 0.8, 1.8, 0.6),
                E(EArch::MushroomCluster,     ELayer::Understory,  500.0, 0.7, 1.6, 0.6),
                E(EArch::SporeStalk,          ELayer::Ground,     4600.0, 0.7, 1.5, 0.6),
            });

            P[static_cast<int32>(EPlanetBiome::Wetland)] = MakeProfile({
                E(EArch::MushroomCanopy,      ELayer::Canopy,       60.0, 1.0, 2.2, 0.9),
                E(EArch::BioluminescentPlant, ELayer::Understory,  900.0, 0.8, 1.6, 0.9),
                E(EArch::SporeStalk,          ELayer::Ground,     6200.0, 0.8, 1.7, 0.9),
            });

            P[static_cast<int32>(EPlanetBiome::Taiga)] = MakeProfile({
                E(EArch::MushroomCanopy,  ELayer::Canopy,      300.0, 1.0, 2.2, 0.6),
                E(EArch::MushroomCluster, ELayer::Understory,  200.0, 0.5, 1.2, 0.65),
                E(EArch::SporeStalk,      ELayer::Ground,     1600.0, 0.5, 1.1, 0.7),
                E(EArch::Rock,            ELayer::Scatter,      25.0, 0.4, 1.4, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Tundra)] = MakeProfile({
                E(EArch::MushroomCluster, ELayer::Understory,  90.0, 0.4, 1.0, 0.7),
                E(EArch::SporeStalk,      ELayer::Ground,    1100.0, 0.4, 1.0, 0.7),
                E(EArch::Rock,            ELayer::Scatter,     45.0, 0.4, 1.5, 0.0),
            });

            P[static_cast<int32>(EPlanetBiome::Snow)] = MakeProfile({
                E(EArch::Rock,    ELayer::Scatter, 20.0, 0.4, 1.4, 0.0),
                E(EArch::Boulder, ELayer::Scatter,  4.0, 0.8, 2.0, 0.0),
            });

            return T;
        }();

        return Table;
    }

    /** A uniform double in [0, 1) from a hash. */
    double UnitFromHash(uint64 Hash)
    {
        return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);
    }
}

double FVegetationProfile::GetLayerDensity(EVegetationLayer Layer) const
{
    double Total = 0.0;

    for (int32 Index = 0; Index < Num; ++Index)
    {
        if (Entries[Index].Layer == Layer)
        {
            Total += Entries[Index].DensityPerHectare;
        }
    }

    return Total;
}

const FVegetationProfile& FPlanetVegetation::GetProfile(
    EPlanetBiosphere Biosphere,
    EPlanetBiome Biome)
{
    const int32 Index = static_cast<int32>(Biome);

    if (Index < 0 || Index >= static_cast<int32>(EPlanetBiome::Count))
    {
        return MakeEmptyProfile();
    }

    switch (Biosphere)
    {
    case EPlanetBiosphere::Fungal:
        return GetFungalProfiles().Profiles[Index];

    case EPlanetBiosphere::Terrestrial:
        return GetTerrestrialProfiles().Profiles[Index];

    case EPlanetBiosphere::Barren:
    default:
        // A barren world still has rock and only rock, whatever its climate
        // says. Reusing the BarrenRock profile for every biome is the simplest
        // way to say that, and it means a barren world's coastlines and
        // mountains still get scattered stones.
        return GetTerrestrialProfiles().Profiles[static_cast<int32>(EPlanetBiome::BarrenRock)];
    }
}

int32 FPlanetVegetation::GetGridResolution(double PatchSizeMeters, double DensityPerHectare)
{
    if (PatchSizeMeters <= 0.0 || DensityPerHectare <= 0.0)
    {
        return 0;
    }

    // One hectare is 10,000 m^2. The grid wants roughly one cell per expected
    // instance, so that the per-cell probability sits near - but below - one and
    // the jitter does the work of hiding the lattice.
    const double AreaHectares = (PatchSizeMeters * PatchSizeMeters) / 10000.0;
    const double Expected = AreaHectares * DensityPerHectare;

    const int32 Resolution = FMath::CeilToInt32(FMath::Sqrt(FMath::Max(Expected, 1.0)));

    return FMath::Clamp(Resolution, 1, MaxGridResolution);
}

int32 FPlanetVegetation::Scatter(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FPlanetTerrainSettings& Settings,
    const FPlanetPatchId& PatchId,
    EVegetationLayer Layer,
    int32 MaxInstances,
    TArray<FVegetationInstance>& OutInstances,
    bool& OutbHitBudget)
{
    OutInstances.Reset();
    OutbHitBudget = false;

    if (MaxInstances <= 0 || !Environment.IsValid() || !Environment.HasLife())
    {
        // A barren world still scatters rock. Everything else needs a
        // biosphere, and checking here rather than per-cell avoids walking a
        // grid on an airless moon to place nothing.
        if (!(Environment.IsValid() && Layer == EVegetationLayer::Scatter))
        {
            return 0;
        }
    }

    const double PatchSizeMeters = PatchId.GetApproximateSizeMeters(Planet.RadiusMeters);

    if (PatchSizeMeters <= 0.0)
    {
        return 0;
    }

    // Grid resolution comes from the densest biome this patch could contain.
    //
    // Using the patch's actual biome would be a chicken and egg problem: the
    // biome is only known after sampling, and the sampling rate is what is
    // being chosen. Taking the maximum over the table is conservative, cheap to
    // compute, and errs toward sampling too finely, which costs time rather
    // than correctness.
    double PeakDensity = 0.0;

    for (int32 BiomeIndex = 0; BiomeIndex < static_cast<int32>(EPlanetBiome::Count); ++BiomeIndex)
    {
        const FVegetationProfile& Profile =
            GetProfile(Environment.Biosphere, static_cast<EPlanetBiome>(BiomeIndex));

        PeakDensity = FMath::Max(PeakDensity, Profile.GetLayerDensity(Layer));
    }

    if (PeakDensity <= 0.0)
    {
        return 0;
    }

    int32 Resolution = GetGridResolution(PatchSizeMeters, PeakDensity);

    if (Resolution <= 0)
    {
        return 0;
    }

    // Thin uniformly rather than truncate.
    //
    // At most one instance is placed per cell, so a grid of R x R cells can
    // never exceed R^2 instances. Capping the *resolution* by the budget rather
    // than stopping the walk when the budget runs out is what makes a
    // budget-limited patch sparse everywhere instead of dense in one corner and
    // empty in the rest - which is what truncation produces, and which reads
    // unmistakably as a bug: a forest with a straight edge through the middle
    // of it.
    //
    // The cells get larger as the resolution falls, so the per-cell probability
    // rises to compensate and the placement stays as dense as the budget
    // allows, spread over the whole patch.
    const int32 BudgetResolution = FMath::FloorToInt32(FMath::Sqrt(static_cast<double>(MaxInstances)));

    Resolution = FMath::Max(FMath::Min(Resolution, BudgetResolution), 1);

    // How much of what the density asks for this grid can actually deliver.
    //
    // When the grid has been capped - a dense ground layer on a large patch -
    // one cell stands for many instances, and placing one thing per cell would
    // silently thin the vegetation by whatever the cap happened to be. Instead
    // the *scale* of what is placed is left alone and the shortfall is recorded
    // as a budget hit, so a thin forest is visible as a number rather than as a
    // vague impression.
    const double CellSizeMeters = PatchSizeMeters / static_cast<double>(Resolution);
    const double CellAreaHectares = (CellSizeMeters * CellSizeMeters) / 10000.0;

    const uint64 PatchSeed = UniverseHash::Hash(
        Environment.Seed.Stream(UniverseSeedDomain::StreamVegetation).Value,
        PatchId.GetStableHash64(),
        static_cast<uint32>(Layer));

    double MinU = 0.0;
    double MinV = 0.0;
    double MaxU = 0.0;
    double MaxV = 0.0;
    PatchId.GetUVBounds(MinU, MinV, MaxU, MaxV);

    const double SpanU = MaxU - MinU;
    const double SpanV = MaxV - MinV;

    int32 Placed = 0;

    for (int32 CellY = 0; CellY < Resolution; ++CellY)
    {
        for (int32 CellX = 0; CellX < Resolution; ++CellX)
        {
            if (Placed >= MaxInstances)
            {
                OutbHitBudget = true;
                return Placed;
            }

            const uint64 CellSeed = UniverseHash::Hash(PatchSeed, CellX, CellY);

            // Jitter inside the cell. Without it the placement is a visible
            // lattice, and a lattice of trees reads as artificial from any
            // distance at which more than a few are visible.
            const double JitterU = UnitFromHash(UniverseHash::Hash(CellSeed, 0x4Au, 0));
            const double JitterV = UnitFromHash(UniverseHash::Hash(CellSeed, 0x4Bu, 0));

            const double U = MinU + SpanU * ((static_cast<double>(CellX) + JitterU) / Resolution);
            const double V = MinV + SpanV * ((static_cast<double>(CellY) + JitterV) / Resolution);

            const FVector3d Direction = CubeSphere::FaceUVToDirection(PatchId.GetFace(), U, V);

            const double Elevation = FPlanetTerrain::GetElevationMeters(Planet, Settings, Direction);
            const FVector3d Normal = FPlanetSurfaceQuery::GetSurfaceNormal(Planet, Settings, Direction);

            const FClimateSample Climate = FPlanetClimate::SampleWithTerrain(
                Planet, Environment, Direction, Elevation, Normal);

            if (!Climate.bValid || Climate.bOcean)
            {
                continue;
            }

            const FBiomeBlend Blend = FPlanetBiomes::Classify(Environment, Climate);
            const FVegetationProfile& Profile = GetProfile(Environment.Biosphere, Blend.GetDominant());

            // Roll once for the cell against the total density of this layer,
            // then choose among the entries proportionally. One roll rather than
            // one per entry keeps the total density honest: rolling each entry
            // separately would let a biome with four entries place four things
            // in a cell that was only ever meant to hold one.
            double Density = 0.0;

            for (int32 Index = 0; Index < Profile.Num; ++Index)
            {
                const FVegetationEntry& Entry = Profile.Entries[Index];

                if (Entry.Layer != Layer)
                {
                    continue;
                }

                if (Climate.SlopeCosine < Entry.MinSlopeCosine)
                {
                    continue;
                }

                if (Climate.AltitudeAboveOceanMeters < Entry.MinAltitudeMeters)
                {
                    continue;
                }

                Density += Entry.DensityPerHectare;
            }

            if (Density <= 0.0)
            {
                continue;
            }

            // Blended by biome weight, so a forest thins out as it approaches a
            // grassland rather than stopping at a line. This is where the
            // biome blend earns its keep.
            const double BlendedDensity = Density * Blend.GetDominantWeight();

            // Vegetation potential scales everything: a marginal world is
            // sparse everywhere rather than lush in patches.
            const double Expected =
                BlendedDensity * CellAreaHectares * FMath::Max(Environment.VegetationPotential, 0.0);

            const double Roll = UnitFromHash(UniverseHash::Hash(CellSeed, 0x4Cu, 0));

            if (Roll >= FMath::Min(Expected, 1.0))
            {
                continue;
            }

            // Choose which entry, proportionally to density among those that
            // passed the slope and altitude filters.
            const double Choice =
                UnitFromHash(UniverseHash::Hash(CellSeed, 0x4Du, 0)) * Density;

            double Accumulated = 0.0;
            const FVegetationEntry* Chosen = nullptr;

            for (int32 Index = 0; Index < Profile.Num; ++Index)
            {
                const FVegetationEntry& Entry = Profile.Entries[Index];

                if (Entry.Layer != Layer
                    || Climate.SlopeCosine < Entry.MinSlopeCosine
                    || Climate.AltitudeAboveOceanMeters < Entry.MinAltitudeMeters)
                {
                    continue;
                }

                Accumulated += Entry.DensityPerHectare;

                if (Choice < Accumulated)
                {
                    Chosen = &Entry;
                    break;
                }
            }

            if (Chosen == nullptr)
            {
                continue;
            }

            FVegetationInstance Instance;

            const double SurfaceRadius = Planet.RadiusMeters + Elevation;

            Instance.PositionMeters = FVector3d(
                Direction.X * SurfaceRadius, Direction.Y * SurfaceRadius, Direction.Z * SurfaceRadius);

            Instance.UpUnit = Direction;
            Instance.NormalUnit = Normal;

            Instance.YawRadians =
                UnitFromHash(UniverseHash::Hash(CellSeed, 0x4Eu, 0)) * (2.0 * PI);

            Instance.Scale = FMath::Lerp(
                Chosen->MinScale, Chosen->MaxScale,
                UnitFromHash(UniverseHash::Hash(CellSeed, 0x4Fu, 0)));

            Instance.Archetype = Chosen->Archetype;
            Instance.Layer = Layer;
            Instance.Biome = Blend.GetDominant();

            // The instance's stable name. See WorldPersistenceIdentity.h.
            Instance.PatchId = PatchId;
            Instance.CellX = CellX;
            Instance.CellY = CellY;

            OutInstances.Add(Instance);
            ++Placed;
        }
    }

    return Placed;
}
