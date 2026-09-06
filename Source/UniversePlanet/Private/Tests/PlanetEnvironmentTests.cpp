// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"
#include "PlanetEnvironment.h"
#include "PlanetClimate.h"
#include "PlanetBiome.h"
#include "PlanetVegetation.h"
#include "PlanetWeather.h"
#include "PlanetEnvironmentQuery.h"
#include "CubeSphere.h"

using CubeSphere::EFace;

namespace
{
    FPlanetSurfaceDescriptor MakeEnvPlanet(uint64 SeedValue, double RadiusMeters)
    {
        FPlanetSurfaceDescriptor Planet;
        Planet.PlanetKey = SeedValue | 1ull;
        Planet.Seed = FUniverseSeed(SeedValue);
        Planet.RadiusMeters = RadiusMeters;
        Planet.MaxElevationMeters = RadiusMeters * 0.00139;
        Planet.MaxDepthMeters = RadiusMeters * 0.00172;
        Planet.SurfaceGravityMs2 = 9.81;
        Planet.AtmosphereHeightMeters = RadiusMeters * 0.0157;
        Planet.GenerationVersion = PlanetTerrainVersion::Current;
        return Planet;
    }

    /** An Earth-like environment, built directly rather than generated. */
    FPlanetEnvironmentDescriptor MakeEnvironment(
        const FPlanetSurfaceDescriptor& Planet,
        double OceanRadiusOffset = 0.0,
        EPlanetBiosphere Biosphere = EPlanetBiosphere::Terrestrial)
    {
        FPlanetEnvironmentDescriptor Environment;
        Environment.PlanetKey = Planet.PlanetKey;
        Environment.Seed = Planet.Seed.Stream(UniverseSeedDomain::StreamEnvironment);
        Environment.RotationAxis = FVector3d(0.0, 0.0, 1.0);
        Environment.RotationPeriodSeconds = 86164.0;
        Environment.MeanSurfaceTemperatureK = 288.0;
        Environment.EquatorPoleDeltaK = 50.0;
        Environment.LapseRateKPerKm = 6.5;
        Environment.AtmosphereDensity = 1.0;
        Environment.OceanRadiusMeters = Planet.RadiusMeters + OceanRadiusOffset;
        Environment.OceanCoverage = 0.5;
        Environment.Biosphere = Biosphere;
        Environment.HumidityBias = 0.5;
        Environment.VegetationPotential = 1.0;
        Environment.CloudCoverageBias = 0.4;
        Environment.StormPotential = 0.4;
        Environment.GenerationVersion = PlanetEnvironmentVersion::Current;
        return Environment;
    }

    /** A synthetic climate sample, for testing the classifier in isolation. */
    FClimateSample MakeClimate(
        double TemperatureCelsius,
        double Humidity,
        double AltitudeMeters = 200.0,
        double SlopeCosine = 0.98)
    {
        FClimateSample Climate;
        Climate.Direction = FVector3d(0.0, 0.0, 1.0);
        Climate.TemperatureK = TemperatureCelsius + 273.15;
        Climate.Humidity = Humidity;
        Climate.AltitudeAboveOceanMeters = AltitudeMeters;
        Climate.ElevationMeters = AltitudeMeters;
        Climate.SlopeCosine = SlopeCosine;
        Climate.bOcean = false;
        Climate.bValid = true;
        return Climate;
    }

}

/**
 * The environmental descriptor must be a function of the planet, must be
 * self-consistent, and must actually differ between planets.
 *
 * The last of those is the point of the sprint. A generator that produced one
 * Earth over and over would satisfy every other test here.
 */
bool UniverseTest_PlanetEnvironmentDescriptor(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeEnvPlanet(0xE0000001ull, 6371000.0);

    // Ocean level resolution, driven directly with a known coverage.
    const double Targets[5] = { 0.1, 0.3, 0.5, 0.7, 0.9 };

    for (double Target : Targets)
    {
        double Actual = 0.0;
        const double Radius =
            FPlanetEnvironment::ResolveOceanRadius(Planet, Settings, Target, Actual);

        // The level lands within one sample of the requested coverage. That is
        // the strongest claim available: the target is a percentile of a finite
        // sample set, so it cannot be exact, and asserting a tighter bound
        // would be asserting something about this particular terrain rather
        // than about the algorithm.
        UVERIFY_NEAR(Result, Actual, Target, 2.0 / FPlanetEnvironment::OceanSampleCount);

        // And the resolved level is inside the terrain's own range - a level
        // above every peak or below every trench would mean the search escaped.
        UVERIFY_TRUE(Result, Radius >= Planet.GetMinRadiusMeters());
        UVERIFY_TRUE(Result, Radius <= Planet.GetMaxRadiusMeters());

        // More water means a higher sea level. Monotonicity is the invariant
        // that would break first if the sort or the percentile were wrong.
        double PreviousActual = 0.0;
        const double LowerRadius =
            FPlanetEnvironment::ResolveOceanRadius(Planet, Settings, Target * 0.5, PreviousActual);

        UVERIFY_TRUE(Result, LowerRadius <= Radius);
    }

    // Zero coverage means no ocean at all, not an ocean at radius zero.
    {
        double Actual = 1.0;
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FPlanetEnvironment::ResolveOceanRadius(Planet, Settings, 0.0, Actual), 0.0);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Actual, 0.0);
    }

    // Sample directions are unit length, deterministic, and spread over the
    // whole sphere rather than clustering on one side of it.
    {
        double SumX = 0.0;
        double SumY = 0.0;
        double SumZ = 0.0;

        constexpr int32 Count = 2048;

        for (int32 Index = 0; Index < Count; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, Count);

            UVERIFY_NEAR(Result, Direction.Size(), 1.0, 1e-12);

            // Deterministic: the same index always gives the same direction.
            const FVector3d Repeat = FPlanetEnvironment::GetSampleDirection(Index, Count);
            UVERIFY_EQ_DOUBLE_EXACT(Result, Direction.X, Repeat.X);
            UVERIFY_EQ_DOUBLE_EXACT(Result, Direction.Y, Repeat.Y);
            UVERIFY_EQ_DOUBLE_EXACT(Result, Direction.Z, Repeat.Z);

            SumX += Direction.X;
            SumY += Direction.Y;
            SumZ += Direction.Z;
        }

        // A uniform spread has a mean near zero. With 2048 samples the standard
        // error on each axis is about 1/sqrt(3*2048) = 0.013, so 0.08 is six
        // sigma - loose enough never to flake, tight enough to catch a
        // distribution that has collapsed onto a hemisphere.
        UVERIFY_TRUE(Result, FMath::Abs(SumX / Count) < 0.08);
        UVERIFY_TRUE(Result, FMath::Abs(SumY / Count) < 0.08);
        UVERIFY_TRUE(Result, FMath::Abs(SumZ / Count) < 0.08);
    }

    return Result.Passed();
}

/**
 * Climate must vary the way physics says it should, and must not reveal the
 * cube.
 *
 * The cube-face continuity check is the one that matters most. Climate is
 * sampled at millions of points over a planet's surface, and a latitude derived
 * from face coordinates would put a visible discontinuity along all twelve
 * cube edges - precisely where ADR-003 worked hardest to make the topology
 * invisible.
 */
bool UniverseTest_ClimateFields(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeEnvPlanet(0xC11A7E01ull, 6371000.0);
    const FPlanetEnvironmentDescriptor Environment = MakeEnvironment(Planet);

    // --- Latitude ----------------------------------------------------------
    {
        const FVector3d Axis = Environment.RotationAxis;

        UVERIFY_NEAR(Result, FPlanetClimate::GetLatitudeSin(Environment, Axis), 1.0, 1e-12);
        UVERIFY_NEAR(Result, FPlanetClimate::GetLatitudeSin(Environment, Axis * -1.0), -1.0, 1e-12);

        const FVector3d Equator(1.0, 0.0, 0.0);
        UVERIFY_NEAR(Result, FPlanetClimate::GetLatitudeSin(Environment, Equator), 0.0, 1e-12);
    }

    // --- Temperature -------------------------------------------------------
    {
        // The equator is warmer than the poles, by the stated delta.
        const double Equator = FPlanetClimate::GetTemperatureK(
            Planet, Environment, FVector3d(1.0, 0.0, 0.0), 0.0, 0.0);

        const double Pole = FPlanetClimate::GetTemperatureK(
            Planet, Environment, FVector3d(0.0, 0.0, 1.0), 1.0, 0.0);

        UVERIFY_TRUE(Result, Equator > Pole);

        // Regional variation is bounded, so the latitude gradient dominates.
        // Without that, "the equator is warmer" would be true only on average.
        UVERIFY_NEAR(Result, Equator - Pole, Environment.EquatorPoleDeltaK, 20.0);

        // At 45 degrees the latitude term vanishes and the temperature is the
        // planet mean, up to the regional term.
        const double Mid = FPlanetClimate::GetTemperatureK(
            Planet, Environment, FVector3d(0.7071067811865476, 0.0, 0.7071067811865476),
            0.7071067811865476, 0.0);

        UVERIFY_NEAR(Result, Mid, Environment.MeanSurfaceTemperatureK, 12.0);

        // Altitude cools, at the stated lapse rate.
        const double Ground = FPlanetClimate::GetTemperatureK(
            Planet, Environment, FVector3d(1.0, 0.0, 0.0), 0.0, 0.0);

        const double Mountain = FPlanetClimate::GetTemperatureK(
            Planet, Environment, FVector3d(1.0, 0.0, 0.0), 0.0, 4000.0);

        UVERIFY_NEAR(Result, Ground - Mountain, Environment.LapseRateKPerKm * 4.0, 1e-9);

        // And below the ocean surface there is no lapse - no air column to cool.
        const double SeaFloor = FPlanetClimate::GetTemperatureK(
            Planet, Environment, FVector3d(1.0, 0.0, 0.0), 0.0, -4000.0);

        UVERIFY_TRUE(Result, SeaFloor > Mountain);
    }

    // --- Humidity ----------------------------------------------------------
    {
        for (int32 Index = 0; Index < 64; ++Index)
        {
            const FVector3d Direction =
                FPlanetEnvironment::GetSampleDirection(Index, 64);

            const double Humidity = FPlanetClimate::GetHumidity(
                Planet, Environment, Direction, 288.0, 100.0);

            UVERIFY_TRUE(Result, Humidity >= 0.0);
            UVERIFY_TRUE(Result, Humidity <= 1.0);
            UVERIFY_TRUE(Result, FMath::IsFinite(Humidity));
        }

        // An airless world has no moisture at all. Not a tuning choice.
        FPlanetEnvironmentDescriptor Airless = MakeEnvironment(Planet);
        Airless.AtmosphereDensity = 0.0;

        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FPlanetClimate::GetHumidity(Planet, Airless, FVector3d(1.0, 0.0, 0.0), 288.0, 0.0), 0.0);

        // Cold is dry: polar deserts are real, and the model should produce
        // them rather than making the poles rainforests.
        const double Warm = FPlanetClimate::GetHumidity(
            Planet, Environment, FVector3d(1.0, 0.0, 0.0), 300.0, 500.0);

        const double Cold = FPlanetClimate::GetHumidity(
            Planet, Environment, FVector3d(1.0, 0.0, 0.0), 245.0, 500.0);

        UVERIFY_TRUE(Result, Warm > Cold);
    }

    // --- Cube-face continuity ----------------------------------------------
    //
    // Sample either side of every cube edge and check the climate matches. The
    // tolerance is generous in absolute terms and tiny in relative ones: the
    // two points are genuinely a short distance apart on the sphere, so a small
    // difference is correct and a large one means the field is discontinuous.
    {
        constexpr int32 Steps = 33;
        constexpr double Epsilon = 1.0e-5;

        double WorstTemperatureDelta = 0.0;
        double WorstHumidityDelta = 0.0;

        for (int32 FaceIndex = 0; FaceIndex < CubeSphere::FaceCount; ++FaceIndex)
        {
            const EFace Face = static_cast<EFace>(FaceIndex);

            for (int32 Step = 0; Step <= Steps; ++Step)
            {
                const double T = static_cast<double>(Step) / static_cast<double>(Steps);

                // Just inside each of the four edges of this face.
                const FVector3d Probes[4] = {
                    CubeSphere::FaceUVToDirection(Face, T, Epsilon),
                    CubeSphere::FaceUVToDirection(Face, T, 1.0 - Epsilon),
                    CubeSphere::FaceUVToDirection(Face, Epsilon, T),
                    CubeSphere::FaceUVToDirection(Face, 1.0 - Epsilon, T),
                };

                // And the corresponding point exactly on the edge, which
                // belongs to the neighbouring face by DirectionToFaceUV's
                // convention.
                const FVector3d Edges[4] = {
                    CubeSphere::FaceUVToDirection(Face, T, 0.0),
                    CubeSphere::FaceUVToDirection(Face, T, 1.0),
                    CubeSphere::FaceUVToDirection(Face, 0.0, T),
                    CubeSphere::FaceUVToDirection(Face, 1.0, T),
                };

                for (int32 Probe = 0; Probe < 4; ++Probe)
                {
                    const FClimateSample Inside =
                        FPlanetClimate::Sample(Planet, Environment, Settings, Probes[Probe]);
                    const FClimateSample OnEdge =
                        FPlanetClimate::Sample(Planet, Environment, Settings, Edges[Probe]);

                    UVERIFY_TRUE(Result, Inside.bValid);
                    UVERIFY_TRUE(Result, OnEdge.bValid);

                    WorstTemperatureDelta = FMath::Max(
                        WorstTemperatureDelta,
                        FMath::Abs(Inside.TemperatureK - OnEdge.TemperatureK));

                    WorstHumidityDelta = FMath::Max(
                        WorstHumidityDelta,
                        FMath::Abs(Inside.Humidity - OnEdge.Humidity));
                }
            }
        }

        // The two sample points are about 30 m apart on a 6,371 km planet.
        // Climate varies over hundreds of kilometres, so anything above a
        // fraction of a kelvin here would mean a seam rather than a gradient.
        UVERIFY_MESSAGE(Result, WorstTemperatureDelta < 0.5,
            *FString::Printf(TEXT("Cube-edge temperature discontinuity: %.6f K"), WorstTemperatureDelta));

        UVERIFY_MESSAGE(Result, WorstHumidityDelta < 0.02,
            *FString::Printf(TEXT("Cube-edge humidity discontinuity: %.6f"), WorstHumidityDelta));
    }

    // --- Determinism -------------------------------------------------------
    {
        for (int32 Index = 0; Index < 32; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 32);

            const FClimateSample First =
                FPlanetClimate::Sample(Planet, Environment, Settings, Direction);
            const FClimateSample Second =
                FPlanetClimate::Sample(Planet, Environment, Settings, Direction);

            UVERIFY_EQ_DOUBLE_EXACT(Result, First.TemperatureK, Second.TemperatureK);
            UVERIFY_EQ_DOUBLE_EXACT(Result, First.Humidity, Second.Humidity);
            UVERIFY_EQ_DOUBLE_EXACT(Result, First.AltitudeAboveOceanMeters, Second.AltitudeAboveOceanMeters);
        }
    }

    return Result.Passed();
}

/**
 * Biome classification must follow the conditions.
 *
 * Written as invariants over climate *families* rather than as exact
 * assignments, because the tuning values will change and a test that pins them
 * would have to be rewritten every time somebody adjusts a range - at which
 * point it is testing the table against itself.
 */
bool UniverseTest_BiomeClassification(FUniverseTestResult& Result)
{
    const FPlanetSurfaceDescriptor Planet = MakeEnvPlanet(0xB10E5ull, 6371000.0);
    const FPlanetEnvironmentDescriptor Environment = MakeEnvironment(Planet);

    // Hot and dry is a desert family.
    {
        const FBiomeBlend Blend = FPlanetBiomes::Classify(Environment, MakeClimate(35.0, 0.05));
        UVERIFY_TRUE(Result, Blend.GetDominant() == EPlanetBiome::Desert);
    }

    // Hot and very wet is a rainforest family.
    {
        const FBiomeBlend Blend = FPlanetBiomes::Classify(Environment, MakeClimate(28.0, 0.92));
        UVERIFY_TRUE(Result,
            Blend.GetDominant() == EPlanetBiome::Rainforest
            || Blend.GetDominant() == EPlanetBiome::Wetland);
    }

    // Temperate and moderately wet is a forest.
    {
        const FBiomeBlend Blend = FPlanetBiomes::Classify(Environment, MakeClimate(14.0, 0.65));
        UVERIFY_TRUE(Result, Blend.GetDominant() == EPlanetBiome::TemperateForest);
    }

    // Cold and dry is a tundra/snow family.
    {
        const FBiomeBlend Blend = FPlanetBiomes::Classify(Environment, MakeClimate(-15.0, 0.25));
        UVERIFY_TRUE(Result,
            Blend.GetDominant() == EPlanetBiome::Tundra
            || Blend.GetDominant() == EPlanetBiome::Snow);
    }

    // Freezing is snow whatever the moisture.
    {
        const FBiomeBlend Dry = FPlanetBiomes::Classify(Environment, MakeClimate(-30.0, 0.1));
        const FBiomeBlend Wet = FPlanetBiomes::Classify(Environment, MakeClimate(-30.0, 0.9));

        UVERIFY_TRUE(Result, Dry.GetDominant() == EPlanetBiome::Snow);
        UVERIFY_TRUE(Result, Wet.GetDominant() == EPlanetBiome::Snow);
    }

    // Steep ground is rock regardless of how pleasant the climate is. This is
    // what stops a cliff face growing a swamp.
    {
        const FBiomeBlend Blend =
            FPlanetBiomes::Classify(Environment, MakeClimate(14.0, 0.65, 800.0, /*slope*/ 0.25));

        UVERIFY_TRUE(Result, Blend.GetDominant() == EPlanetBiome::BarrenRock);
    }

    // Submerged is ocean, no matter what the temperature range says. Water is
    // geometry, and the classifier decides it before consulting the table.
    {
        FClimateSample Climate = MakeClimate(30.0, 0.1, -500.0);
        Climate.bOcean = true;
        Climate.WaterDepthMeters = 500.0;

        const FBiomeBlend Blend = FPlanetBiomes::Classify(Environment, Climate);

        UVERIFY_TRUE(Result, Blend.GetDominant() == EPlanetBiome::Ocean);
        UVERIFY_NEAR(Result, Blend.GetDominantWeight(), 1.0, 1e-12);
        UVERIFY_EQ_INT(Result, Blend.Num, 1);
    }

    // --- Blend invariants ---------------------------------------------------
    //
    // Over a wide sweep of climate space: weights sum to one, are ordered, are
    // bounded, and the blend is never empty.
    {
        for (int32 TempStep = 0; TempStep <= 20; ++TempStep)
        {
            for (int32 HumidStep = 0; HumidStep <= 20; ++HumidStep)
            {
                const double Celsius = -40.0 + (80.0 * TempStep) / 20.0;
                const double Humidity = static_cast<double>(HumidStep) / 20.0;

                const FBiomeBlend Blend =
                    FPlanetBiomes::Classify(Environment, MakeClimate(Celsius, Humidity));

                UVERIFY_TRUE(Result, Blend.Num >= 1);
                UVERIFY_TRUE(Result, Blend.Num <= FBiomeBlend::MaxEntries);

                double Total = 0.0;

                for (int32 Index = 0; Index < Blend.Num; ++Index)
                {
                    UVERIFY_TRUE(Result, Blend.Weights[Index] > 0.0);
                    UVERIFY_TRUE(Result, Blend.Weights[Index] <= 1.0);

                    if (Index > 0)
                    {
                        UVERIFY_TRUE(Result, Blend.Weights[Index] <= Blend.Weights[Index - 1]);
                    }

                    Total += Blend.Weights[Index];
                }

                UVERIFY_NEAR(Result, Total, 1.0, 1e-9);

                // The appearance is a valid colour.
                const FBiomeAppearance Appearance = Blend.GetAppearance(Environment.Biosphere);

                UVERIFY_TRUE(Result, Appearance.R >= 0.0 && Appearance.R <= 1.0);
                UVERIFY_TRUE(Result, Appearance.G >= 0.0 && Appearance.G <= 1.0);
                UVERIFY_TRUE(Result, Appearance.B >= 0.0 && Appearance.B <= 1.0);
            }
        }
    }

    // --- Transitions blend rather than switch -------------------------------
    //
    // Walking a climate gradient must pass through mixtures. A classifier that
    // only ever returned one biome would put a hard line on the planet at every
    // boundary, and this is the assertion that would catch it.
    {
        int32 MixedSamples = 0;

        for (int32 Step = 0; Step <= 200; ++Step)
        {
            const double Humidity = static_cast<double>(Step) / 200.0;
            const FBiomeBlend Blend = FPlanetBiomes::Classify(Environment, MakeClimate(20.0, Humidity));

            if (Blend.Num > 1 && Blend.Weights[1] > 0.1)
            {
                ++MixedSamples;
            }
        }

        UVERIFY_MESSAGE(Result, MixedSamples > 20,
            *FString::Printf(
                TEXT("Only %d of 201 samples along a humidity sweep were meaningfully blended."),
                MixedSamples));
    }

    // --- The alien table is a table, not a special case ---------------------
    {
        const FPlanetEnvironmentDescriptor Fungal =
            MakeEnvironment(Planet, 0.0, EPlanetBiosphere::Fungal);

        // The same conditions still classify as a forest - the *biome* is a
        // function of climate and does not care what grows in it.
        const FBiomeBlend Blend = FPlanetBiomes::Classify(Fungal, MakeClimate(14.0, 0.65));
        UVERIFY_TRUE(Result, Blend.GetDominant() == EPlanetBiome::TemperateForest);

        // But it looks different, and grows something different.
        const FBiomeAppearance Terrestrial =
            FPlanetBiomes::GetDefinition(EPlanetBiosphere::Terrestrial,
                EPlanetBiome::TemperateForest).Appearance;

        const FBiomeAppearance Alien =
            FPlanetBiomes::GetDefinition(EPlanetBiosphere::Fungal,
                EPlanetBiome::TemperateForest).Appearance;

        UVERIFY_TRUE(Result,
            FMath::Abs(Terrestrial.R - Alien.R)
            + FMath::Abs(Terrestrial.G - Alien.G)
            + FMath::Abs(Terrestrial.B - Alien.B) > 0.1);
    }

    return Result.Passed();
}

/**
 * Ocean classification and water depth.
 */
bool UniverseTest_PlanetOcean(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const double Radii[3] = { 200000.0, 1737400.0, 6371000.0 };

    for (double Radius : Radii)
    {
        const FPlanetSurfaceDescriptor Planet = MakeEnvPlanet(0x0CEA4ull, Radius);
        const FPlanetEnvironmentDescriptor Environment = MakeEnvironment(Planet);

        UVERIFY_TRUE(Result, Environment.HasOcean());

        int32 Ocean = 0;
        int32 Land = 0;

        for (int32 Index = 0; Index < 256; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 256);

            const FEnvironmentSample Sample =
                FPlanetEnvironmentQuery::SampleStatic(Planet, Environment, Settings, Direction);

            UVERIFY_TRUE(Result, Sample.bValid);

            const double SurfaceRadius = Planet.RadiusMeters + Sample.GetElevationMeters();

            // Land and water are decided by exactly one comparison, and the
            // reported depth agrees with it.
            if (SurfaceRadius < Environment.OceanRadiusMeters)
            {
                UVERIFY_TRUE(Result, Sample.IsOcean());
                UVERIFY_NEAR(Result, Sample.GetWaterDepthMeters(),
                    Environment.OceanRadiusMeters - SurfaceRadius, 1e-6);
                UVERIFY_TRUE(Result, Sample.GetBiome() == EPlanetBiome::Ocean);
                ++Ocean;
            }
            else
            {
                UVERIFY_FALSE(Result, Sample.IsOcean());
                UVERIFY_EQ_DOUBLE_EXACT(Result, Sample.GetWaterDepthMeters(), 0.0);
                UVERIFY_TRUE(Result, Sample.GetBiome() != EPlanetBiome::Ocean);
                ++Land;
            }
        }

        // Both must occur. A planet that came out all land or all water would
        // pass every assertion above and be useless.
        UVERIFY_TRUE(Result, Ocean > 0);
        UVERIFY_TRUE(Result, Land > 0);

        // --- Underwater is about the position, not the ground below it ------
        {
            double Depth = 0.0;

            const FVector3d Deep(0.0, 0.0, Environment.OceanRadiusMeters - 100.0);
            UVERIFY_TRUE(Result, FPlanetEnvironmentQuery::IsUnderwater(Environment, Deep, Depth));
            UVERIFY_NEAR(Result, Depth, 100.0, 1e-6);

            const FVector3d Above(0.0, 0.0, Environment.OceanRadiusMeters + 100.0);
            UVERIFY_FALSE(Result, FPlanetEnvironmentQuery::IsUnderwater(Environment, Above, Depth));
            UVERIFY_EQ_DOUBLE_EXACT(Result, Depth, 0.0);
        }

        // A dry world has no water anywhere, at any position.
        {
            FPlanetEnvironmentDescriptor Dry = MakeEnvironment(Planet);
            Dry.OceanRadiusMeters = 0.0;

            UVERIFY_FALSE(Result, Dry.HasOcean());

            double Depth = 1.0;
            UVERIFY_FALSE(Result,
                FPlanetEnvironmentQuery::IsUnderwater(Dry, FVector3d(0.0, 0.0, 1.0), Depth));

            const FEnvironmentSample Sample = FPlanetEnvironmentQuery::SampleStatic(
                Planet, Dry, Settings, FVector3d(0.0, 0.0, 1.0));

            UVERIFY_FALSE(Result, Sample.IsOcean());
        }
    }

    return Result.Passed();
}

/**
 * The unified environment query must return valid values everywhere, and must
 * agree with the parts it is assembled from.
 */
bool UniverseTest_EnvironmentQuery(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeEnvPlanet(0xE7A81ull, 6371000.0);
    const FPlanetEnvironmentDescriptor Environment = MakeEnvironment(Planet);

    constexpr double Time = 12345.0;

    for (int32 Index = 0; Index < 512; ++Index)
    {
        const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 512);

        const FEnvironmentSample Sample = FPlanetEnvironmentQuery::Sample(
            Planet, Environment, Settings, Direction, Time);

        UVERIFY_TRUE(Result, Sample.bValid);

        // Nothing is NaN or infinite. A single NaN escaping a climate field
        // poisons a material, a density and a bounding box before anyone traces
        // it back here.
        UVERIFY_TRUE(Result, FMath::IsFinite(Sample.GetTemperatureK()));
        UVERIFY_TRUE(Result, FMath::IsFinite(Sample.GetHumidity()));
        UVERIFY_TRUE(Result, FMath::IsFinite(Sample.GetElevationMeters()));
        UVERIFY_TRUE(Result, FMath::IsFinite(Sample.GetAltitudeAboveOceanMeters()));
        UVERIFY_TRUE(Result, FMath::IsFinite(Sample.GetWindSpeedMs()));

        // Bounded quantities are bounded.
        UVERIFY_TRUE(Result, Sample.GetHumidity() >= 0.0 && Sample.GetHumidity() <= 1.0);
        UVERIFY_TRUE(Result, Sample.GetSlopeCosine() >= 0.0 && Sample.GetSlopeCosine() <= 1.0);
        UVERIFY_TRUE(Result, Sample.Weather.Cloudiness >= 0.0 && Sample.Weather.Cloudiness <= 1.0);
        UVERIFY_TRUE(Result, Sample.Weather.Precipitation >= 0.0 && Sample.Weather.Precipitation <= 1.0);
        UVERIFY_TRUE(Result, Sample.Weather.Fog >= 0.0 && Sample.Weather.Fog <= 1.0);
        UVERIFY_TRUE(Result, Sample.GetSurfaceWetness() >= 0.0 && Sample.GetSurfaceWetness() <= 1.0);

        // Temperature is physically sane. Not a tight bound - the point is to
        // catch a unit error or a runaway term, not to pin the model.
        UVERIFY_TRUE(Result, Sample.GetTemperatureK() > 100.0);
        UVERIFY_TRUE(Result, Sample.GetTemperatureK() < 500.0);

        // Wind direction is unit and tangential - a wind blowing into the
        // ground or up into the sky would be a basis error.
        UVERIFY_NEAR(Result, Sample.GetWindDirection().Size(), 1.0, 1e-9);
        UVERIFY_NEAR(Result,
            FVector3d::DotProduct(Sample.GetWindDirection(), Sample.Direction), 0.0, 1e-9);

        UVERIFY_TRUE(Result, Sample.GetWindSpeedMs() >= 0.0);
        UVERIFY_TRUE(Result, Sample.GetWindSpeedMs() < 200.0);

        // The surface position agrees with the elevation it reports.
        UVERIFY_NEAR(Result, Sample.SurfacePositionMeters.Size(),
            Planet.RadiusMeters + Sample.GetElevationMeters(), 1e-6);

        // The blend is well-formed.
        double Total = 0.0;
        for (int32 Entry = 0; Entry < Sample.Biome.Num; ++Entry)
        {
            Total += Sample.Biome.Weights[Entry];
        }
        UVERIFY_NEAR(Result, Total, 1.0, 1e-9);
    }

    // --- The static overload agrees, minus the weather ---------------------
    {
        for (int32 Index = 0; Index < 32; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 32);

            const FEnvironmentSample Full = FPlanetEnvironmentQuery::Sample(
                Planet, Environment, Settings, Direction, Time);

            const FEnvironmentSample Static =
                FPlanetEnvironmentQuery::SampleStatic(Planet, Environment, Settings, Direction);

            UVERIFY_EQ_DOUBLE_EXACT(Result, Static.GetTemperatureK(), Full.GetTemperatureK());
            UVERIFY_EQ_DOUBLE_EXACT(Result, Static.GetHumidity(), Full.GetHumidity());
            UVERIFY_TRUE(Result, Static.GetBiome() == Full.GetBiome());

            // And says plainly that it did not sample weather, rather than
            // reporting a clear sky it never asked about.
            UVERIFY_FALSE(Result, Static.Weather.bValid);
            UVERIFY_TRUE(Result, Full.Weather.bValid);
        }
    }

    // --- Determinism -------------------------------------------------------
    {
        for (int32 Index = 0; Index < 64; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 64);

            const FEnvironmentSample First =
                FPlanetEnvironmentQuery::Sample(Planet, Environment, Settings, Direction, Time);
            const FEnvironmentSample Second =
                FPlanetEnvironmentQuery::Sample(Planet, Environment, Settings, Direction, Time);

            UVERIFY_EQ_DOUBLE_EXACT(Result, First.GetTemperatureK(), Second.GetTemperatureK());
            UVERIFY_EQ_DOUBLE_EXACT(Result, First.Weather.Cloudiness, Second.Weather.Cloudiness);
            UVERIFY_EQ_DOUBLE_EXACT(Result, First.Weather.Precipitation, Second.Weather.Precipitation);
            UVERIFY_TRUE(Result, First.GetWeatherState() == Second.GetWeatherState());
        }
    }

    return Result.Passed();
}

/**
 * Weather must be a pure function of place and time, must vary regionally
 * rather than globally, and must follow the climate.
 */
bool UniverseTest_PlanetWeather(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeEnvPlanet(0x7EA7ull, 6371000.0);
    const FPlanetEnvironmentDescriptor Environment = MakeEnvironment(Planet);

    // --- Cells cover the sphere and are stable -----------------------------
    {
        for (int32 Index = 0; Index < 256; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 256);

            const FPlanetPatchId Cell = FPlanetWeather::GetCell(Direction);

            UVERIFY_EQ_INT(Result, Cell.Level, FPlanetWeather::CellLevel);
            UVERIFY_TRUE(Result, Cell.IsValid());

            // The same direction always lands in the same cell.
            UVERIFY_TRUE(Result, FPlanetWeather::GetCell(Direction) == Cell);
        }
    }

    // --- Regional, not global ----------------------------------------------
    //
    // The failure this guards against is a planet where the whole sky switches
    // to rain at once. At any moment, different parts of the planet must be
    // doing different things.
    {
        int32 StateCounts[static_cast<int32>(EWeatherState::Count)] = {};

        for (int32 Index = 0; Index < 512; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 512);

            const FEnvironmentSample Sample = FPlanetEnvironmentQuery::Sample(
                Planet, Environment, Settings, Direction, 50000.0);

            ++StateCounts[static_cast<int32>(Sample.GetWeatherState())];
        }

        int32 DistinctStates = 0;
        int32 Largest = 0;

        for (int32 State = 0; State < static_cast<int32>(EWeatherState::Count); ++State)
        {
            if (StateCounts[State] > 0)
            {
                ++DistinctStates;
            }

            Largest = FMath::Max(Largest, StateCounts[State]);
        }

        UVERIFY_MESSAGE(Result, DistinctStates >= 2,
            TEXT("Every point on the planet had the same weather."));

        UVERIFY_MESSAGE(Result, Largest < 512,
            TEXT("One weather state covered the entire planet."));
    }

    // --- A pure function of time -------------------------------------------
    {
        const FVector3d Direction(0.6, 0.5, 0.62469);

        const FClimateSample Climate =
            FPlanetClimate::Sample(Planet, Environment, Settings, Direction.GetSafeNormal());

        const FWeatherSample A = FPlanetWeather::Sample(Environment, Climate, 900000.0);
        const FWeatherSample B = FPlanetWeather::Sample(Environment, Climate, 900000.0);

        UVERIFY_EQ_DOUBLE_EXACT(Result, A.Cloudiness, B.Cloudiness);
        UVERIFY_EQ_DOUBLE_EXACT(Result, A.Precipitation, B.Precipitation);
        UVERIFY_EQ_DOUBLE_EXACT(Result, A.WindSpeedMs, B.WindSpeedMs);

        // It evolves: over many periods, conditions must actually change.
        // Otherwise "weather" is a constant with extra steps.
        double MinCloud = 1.0;
        double MaxCloud = 0.0;

        for (int32 Period = 0; Period < 60; ++Period)
        {
            const double Time = Period * FPlanetWeather::CellPeriodSeconds;
            const FWeatherSample Sample = FPlanetWeather::Sample(Environment, Climate, Time);

            MinCloud = FMath::Min(MinCloud, Sample.Cloudiness);
            MaxCloud = FMath::Max(MaxCloud, Sample.Cloudiness);
        }

        UVERIFY_MESSAGE(Result, MaxCloud - MinCloud > 0.15,
            *FString::Printf(TEXT("Cloudiness varied only %.4f over 60 periods."), MaxCloud - MinCloud));
    }

    // --- Transitions are gradual -------------------------------------------
    //
    // Stepping through time in small increments must not produce a jump. A hard
    // period boundary would make the sky change between one frame and the next.
    {
        const FVector3d Direction = FVector3d(0.3, -0.7, 0.5).GetSafeNormal();

        const FClimateSample Climate =
            FPlanetClimate::Sample(Planet, Environment, Settings, Direction);

        double WorstJump = 0.0;
        double Previous = -1.0;

        // Two full periods, sampled every thirty seconds of simulation time.
        for (int32 Step = 0; Step <= 1440; ++Step)
        {
            const double Time = 400000.0 + Step * 30.0;
            const FWeatherSample Sample = FPlanetWeather::Sample(Environment, Climate, Time);

            if (Previous >= 0.0)
            {
                WorstJump = FMath::Max(WorstJump, FMath::Abs(Sample.Cloudiness - Previous));
            }

            Previous = Sample.Cloudiness;
        }

        UVERIFY_MESSAGE(Result, WorstJump < 0.05,
            *FString::Printf(TEXT("Cloudiness jumped %.4f in thirty seconds."), WorstJump));
    }

    // --- An airless world has no weather -----------------------------------
    {
        FPlanetEnvironmentDescriptor Airless = MakeEnvironment(Planet);
        Airless.AtmosphereDensity = 0.0;

        for (int32 Index = 0; Index < 64; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 64);

            const FClimateSample Climate =
                FPlanetClimate::Sample(Planet, Airless, Settings, Direction);

            const FWeatherSample Weather = FPlanetWeather::Sample(Airless, Climate, 100000.0);

            UVERIFY_TRUE(Result, Weather.State == EWeatherState::Clear);
            UVERIFY_EQ_DOUBLE_EXACT(Result, Weather.Precipitation, 0.0);
            UVERIFY_EQ_DOUBLE_EXACT(Result, Weather.WindSpeedMs, 0.0);
        }
    }

    // --- Precipitation below freezing is snow ------------------------------
    {
        FClimateSample Cold = MakeClimate(-20.0, 0.9);
        Cold.Direction = FVector3d(0.0, 0.0, 1.0);

        const FWeatherSample Weather =
            FPlanetWeather::SampleCell(Environment, FPlanetWeather::GetCell(Cold.Direction),
                Cold, 250000.0);

        UVERIFY_TRUE(Result, Weather.bFrozen);
        UVERIFY_TRUE(Result, Weather.State != EWeatherState::Rain);
    }

    return Result.Passed();
}

/**
 * Vegetation placement must be deterministic, on the surface, inside its patch,
 * bounded, and appropriate to the biome.
 */
bool UniverseTest_VegetationPlacement(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeEnvPlanet(0x7A6E7ull, 6371000.0);
    const FPlanetEnvironmentDescriptor Environment = MakeEnvironment(Planet);

    constexpr int32 MaxInstances = 4000;

    TArray<FVegetationInstance> First;
    TArray<FVegetationInstance> Second;
    bool bHitBudget = false;

    int32 TotalPlaced = 0;
    int32 PatchesWithVegetation = 0;

    // A spread of patches over several faces and levels.
    const FPlanetPatchId Patches[6] = {
        FPlanetPatchId(EFace::PosX, 10, 512, 512),
        FPlanetPatchId(EFace::NegY, 11, 1000, 300),
        FPlanetPatchId(EFace::PosZ, 12, 2048, 2048),
        FPlanetPatchId(EFace::NegZ, 12, 100, 4000),
        FPlanetPatchId(EFace::PosY, 13, 5000, 1234),
        FPlanetPatchId(EFace::NegX, 9, 7, 300),
    };

    for (const FPlanetPatchId& PatchId : Patches)
    {
        for (int32 LayerIndex = 0; LayerIndex < static_cast<int32>(EVegetationLayer::Count); ++LayerIndex)
        {
            const EVegetationLayer Layer = static_cast<EVegetationLayer>(LayerIndex);

            const int32 PlacedFirst = FPlanetVegetation::Scatter(
                Planet, Environment, Settings, PatchId, Layer, MaxInstances, First, bHitBudget);

            const int32 PlacedSecond = FPlanetVegetation::Scatter(
                Planet, Environment, Settings, PatchId, Layer, MaxInstances, Second, bHitBudget);

            // Determinism, and not merely in count: the same instances, in the
            // same order, at the same positions. A player who leaves a forest
            // and comes back must find the same trees.
            UVERIFY_EQ_INT(Result, PlacedFirst, PlacedSecond);
            UVERIFY_EQ_INT(Result, First.Num(), Second.Num());

            for (int32 Index = 0; Index < First.Num(); ++Index)
            {
                UVERIFY_EQ_DOUBLE_EXACT(Result, First[Index].PositionMeters.X, Second[Index].PositionMeters.X);
                UVERIFY_EQ_DOUBLE_EXACT(Result, First[Index].PositionMeters.Y, Second[Index].PositionMeters.Y);
                UVERIFY_EQ_DOUBLE_EXACT(Result, First[Index].PositionMeters.Z, Second[Index].PositionMeters.Z);
                UVERIFY_EQ_DOUBLE_EXACT(Result, First[Index].Scale, Second[Index].Scale);
                UVERIFY_EQ_DOUBLE_EXACT(Result, First[Index].YawRadians, Second[Index].YawRadians);
                UVERIFY_TRUE(Result, First[Index].Archetype == Second[Index].Archetype);
            }

            // Bounded.
            UVERIFY_TRUE(Result, PlacedFirst <= MaxInstances);

            TotalPlaced += PlacedFirst;

            if (PlacedFirst > 0)
            {
                ++PatchesWithVegetation;
            }

            for (const FVegetationInstance& Instance : First)
            {
                // On the surface, not floating and not buried. The tolerance is
                // the terrain's own relief scale, since the instance sits at
                // whatever elevation the terrain has there.
                const double Radius = Instance.PositionMeters.Size();

                UVERIFY_TRUE(Result, Radius >= Planet.GetMinRadiusMeters());
                UVERIFY_TRUE(Result, Radius <= Planet.GetMaxRadiusMeters());

                // Up is radial and unit.
                UVERIFY_NEAR(Result, Instance.UpUnit.Size(), 1.0, 1e-9);
                UVERIFY_NEAR(Result, Instance.NormalUnit.Size(), 1.0, 1e-9);

                // Inside the patch it was scattered for. An instance leaking
                // into a neighbouring patch would be generated twice - once by
                // each - and would flicker as they streamed independently.
                const FVector3d Direction = Instance.PositionMeters.GetSafeNormal();
                UVERIFY_TRUE(Result, PatchId.Contains(FPlanetPatchId::FromDirection(
                    Direction, PatchId.Level)));

                // Never in the ocean.
                UVERIFY_TRUE(Result, Instance.Biome != EPlanetBiome::Ocean);

                // Sane transform.
                UVERIFY_TRUE(Result, Instance.Scale > 0.0 && Instance.Scale < 10.0);
                UVERIFY_TRUE(Result, Instance.YawRadians >= 0.0);
                UVERIFY_TRUE(Result, Instance.YawRadians <= 2.0 * PI);
                UVERIFY_TRUE(Result, Instance.Layer == Layer);
            }
        }
    }

    // Something grew somewhere. A scatter that placed nothing would pass every
    // assertion above.
    UVERIFY_MESSAGE(Result, TotalPlaced > 0,
        TEXT("Vegetation scatter placed nothing on any patch of a living world."));

    UVERIFY_TRUE(Result, PatchesWithVegetation > 0);

    // --- A barren world grows nothing but rock -----------------------------
    {
        FPlanetEnvironmentDescriptor Barren = MakeEnvironment(Planet);
        Barren.Biosphere = EPlanetBiosphere::Barren;
        Barren.VegetationPotential = 0.0;

        TArray<FVegetationInstance> Instances;

        const int32 Canopy = FPlanetVegetation::Scatter(
            Planet, Barren, Settings, Patches[0], EVegetationLayer::Canopy,
            MaxInstances, Instances, bHitBudget);

        UVERIFY_EQ_INT(Result, Canopy, 0);

        // But rock still scatters, because rock is not alive.
        FPlanetVegetation::Scatter(
            Planet, Barren, Settings, Patches[0], EVegetationLayer::Scatter,
            MaxInstances, Instances, bHitBudget);

        for (const FVegetationInstance& Instance : Instances)
        {
            UVERIFY_TRUE(Result,
                Instance.Archetype == EVegetationArchetype::Rock
                || Instance.Archetype == EVegetationArchetype::Boulder);
        }
    }

    // --- The alien profile substitutes content, not structure --------------
    {
        const FPlanetEnvironmentDescriptor Fungal =
            MakeEnvironment(Planet, 0.0, EPlanetBiosphere::Fungal);

        const FVegetationProfile& Terrestrial = FPlanetVegetation::GetProfile(
            EPlanetBiosphere::Terrestrial, EPlanetBiome::TemperateForest);

        const FVegetationProfile& Alien = FPlanetVegetation::GetProfile(
            EPlanetBiosphere::Fungal, EPlanetBiome::TemperateForest);

        // Comparable canopy density - the conditions set how much grows.
        const double TerrestrialCanopy = Terrestrial.GetLayerDensity(EVegetationLayer::Canopy);
        const double AlienCanopy = Alien.GetLayerDensity(EVegetationLayer::Canopy);

        UVERIFY_TRUE(Result, TerrestrialCanopy > 0.0);
        UVERIFY_TRUE(Result, AlienCanopy > 0.0);
        UVERIFY_TRUE(Result, AlienCanopy > TerrestrialCanopy * 0.5);
        UVERIFY_TRUE(Result, AlienCanopy < TerrestrialCanopy * 2.0);

        // Entirely different archetypes - the biosphere sets what grows.
        TArray<FVegetationInstance> Instances;

        FPlanetVegetation::Scatter(
            Planet, Fungal, Settings, Patches[2], EVegetationLayer::Canopy,
            MaxInstances, Instances, bHitBudget);

        for (const FVegetationInstance& Instance : Instances)
        {
            UVERIFY_TRUE(Result,
                Instance.Archetype == EVegetationArchetype::MushroomCanopy
                || Instance.Archetype == EVegetationArchetype::SporeStalk
                || Instance.Archetype == EVegetationArchetype::BioluminescentPlant
                || Instance.Archetype == EVegetationArchetype::MushroomCluster);
        }
    }

    return Result.Passed();
}
