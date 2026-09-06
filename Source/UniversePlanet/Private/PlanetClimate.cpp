// Copyright Universe Project. All Rights Reserved.

#include "PlanetClimate.h"
#include "PlanetTerrain.h"
#include "PlanetNoise.h"
#include "PlanetSurfaceQuery.h"

namespace
{
    /**
     * Frequencies for the regional climate patterns.
     *
     * Low, deliberately. The terrain layers in PlanetTerrain.h run from 1.1 for
     * continents up to 220 for detail; climate sits below even the continental
     * layer, because a climate band has to be larger than the continent it
     * crosses. At 0.6 a feature spans roughly a third of the planet, which is
     * about the width of a real climate zone.
     */
    constexpr double RegionalTemperatureFrequency = 0.85;
    constexpr double RegionalHumidityFrequency = 0.55;

    constexpr int32 RegionalOctaves = 3;

    /** Peak regional departure from the latitude temperature, in kelvin. */
    constexpr double RegionalTemperatureAmplitudeK = 9.0;

    /** Peak regional departure from the planet humidity bias, in [0, 1]. */
    constexpr double RegionalHumidityAmplitude = 0.34;
}

double FPlanetClimate::GetLatitudeSin(
    const FPlanetEnvironmentDescriptor& Environment,
    const FVector3d& Direction)
{
    const double Dot = FVector3d::DotProduct(Direction, Environment.RotationAxis);

    // Clamped because both inputs are unit only to rounding, and a value a few
    // ULPs outside [-1, 1] would make every squared term downstream slightly
    // wrong at exactly the poles.
    return FMath::Clamp(Dot, -1.0, 1.0);
}

double FPlanetClimate::GetTemperatureK(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FVector3d& Direction,
    double LatitudeSin,
    double AltitudeAboveOceanMeters)
{
    // --- Latitude ----------------------------------------------------------
    //
    // cos(2*lat), written as 1 - 2*sin^2(lat), so no trigonometry is involved.
    // Warmest at the equator, coldest at both poles, exactly at the planet mean
    // at 45 degrees - the same double-humped shape real insolation follows.
    const double LatitudeFactor = 1.0 - 2.0 * (LatitudeSin * LatitudeSin);

    double Temperature =
        Environment.MeanSurfaceTemperatureK + (Environment.EquatorPoleDeltaK * 0.5) * LatitudeFactor;

    // --- Elevation ---------------------------------------------------------
    //
    // Only above the ocean surface. Below it there is no air column to cool,
    // and applying a lapse rate to the sea floor would put permanent ice at the
    // bottom of every trench.
    if (AltitudeAboveOceanMeters > 0.0)
    {
        Temperature -= Environment.LapseRateKPerKm * (AltitudeAboveOceanMeters / 1000.0);
    }

    // --- Regional variation ------------------------------------------------
    //
    // Sampled in 3D on the direction itself, like terrain, so it is continuous
    // across every cube face and corner by construction rather than by
    // stitching. This is what stops the planet being a set of neat latitude
    // stripes.
    const uint64 TemperatureSeed =
        Environment.Seed.Stream(UniverseSeedDomain::StreamPrimary).Value;

    const double Regional = PlanetNoise::FBM(
        Direction, TemperatureSeed, RegionalOctaves, RegionalTemperatureFrequency);

    Temperature += Regional * RegionalTemperatureAmplitudeK;

    // --- Ocean thermal mass ------------------------------------------------
    //
    // Water's heat capacity flattens extremes: the sea is warmer than the land
    // at the poles and cooler at the equator. Pulling submerged points a third
    // of the way toward the planet mean is a crude stand-in for that, and it
    // does the one thing that matters visually - it stops oceans freezing solid
    // wherever the land beside them happens to be cold.
    if (AltitudeAboveOceanMeters < 0.0 && Environment.HasOcean())
    {
        constexpr double OceanModeration = 0.35;

        Temperature += (Environment.MeanSurfaceTemperatureK - Temperature) * OceanModeration;
    }

    (void)Planet;

    return Temperature;
}

double FPlanetClimate::GetHumidity(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FVector3d& Direction,
    double TemperatureK,
    double AltitudeAboveOceanMeters)
{
    if (Environment.AtmosphereDensity <= 0.0)
    {
        // No air, no moisture. Not an approximation - a vacuum holds no water.
        return 0.0;
    }

    double Humidity = Environment.HumidityBias;

    // --- Regional pattern ---------------------------------------------------
    //
    // Its own noise stream, so retuning moisture later cannot move the
    // temperature bands, and vice versa.
    const uint64 HumiditySeed =
        Environment.Seed.Stream(UniverseSeedDomain::StreamWeather).Value;

    Humidity += PlanetNoise::FBM(
        Direction, HumiditySeed, RegionalOctaves, RegionalHumidityFrequency)
        * RegionalHumidityAmplitude;

    // --- Ocean proximity, approximated by altitude --------------------------
    //
    // A true distance-to-coast would need a search over the surface, which is
    // not something a per-point field evaluated on a worker thread can do.
    // Altitude above the ocean is the available proxy and it is a reasonable
    // one: low ground is usually near water and high ground usually is not.
    //
    // What it gets wrong is a dry inland basin below sea level, which this
    // model will call humid. That is a known and accepted error, and it is
    // recorded here rather than hidden because the fix - a coarse precomputed
    // distance field over the sphere - is a real piece of future work.
    if (Environment.HasOcean())
    {
        constexpr double MoistureReachMeters = 3000.0;

        const double Proximity =
            1.0 - FMath::Clamp(AltitudeAboveOceanMeters / MoistureReachMeters, 0.0, 1.0);

        Humidity += Proximity * 0.28;
    }

    // --- Temperature --------------------------------------------------------
    //
    // Warm air holds more water. The cold poles being dry is not a quirk of
    // this model - polar deserts are real, and Antarctica is one.
    constexpr double DryColdK = 250.0;
    constexpr double WetWarmK = 305.0;

    const double Warmth =
        FMath::Clamp((TemperatureK - DryColdK) / (WetWarmK - DryColdK), 0.0, 1.0);

    Humidity += (Warmth - 0.5) * 0.30;

    (void)Planet;

    return FMath::Clamp(Humidity, 0.0, 1.0);
}

FClimateSample FPlanetClimate::SampleWithTerrain(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FVector3d& Direction,
    double ElevationMeters,
    const FVector3d& SurfaceNormal)
{
    FClimateSample Sample;

    FVector3d Unit;

    if (!FPlanetSurfaceQuery::TryGetDirection(Direction, Unit))
    {
        return Sample;
    }

    Sample.Direction = Unit;
    Sample.ElevationMeters = ElevationMeters;
    Sample.LatitudeSin = GetLatitudeSin(Environment, Unit);

    const double SurfaceRadius = Planet.RadiusMeters + ElevationMeters;

    // Altitude is measured against the ocean surface when there is one, and
    // against the reference radius when there is not. Two different data, and
    // the distinction is the reason FClimateSample carries both.
    const double Datum = Environment.HasOcean() ? Environment.OceanRadiusMeters : Planet.RadiusMeters;

    Sample.AltitudeAboveOceanMeters = SurfaceRadius - Datum;

    Sample.bOcean = Environment.HasOcean() && Sample.AltitudeAboveOceanMeters < 0.0;
    Sample.WaterDepthMeters = Sample.bOcean ? -Sample.AltitudeAboveOceanMeters : 0.0;

    // Slope against local up, not against any world axis.
    Sample.SlopeCosine = FMath::Clamp(FVector3d::DotProduct(SurfaceNormal, Unit), 0.0, 1.0);

    Sample.TemperatureK = GetTemperatureK(
        Planet, Environment, Unit, Sample.LatitudeSin, Sample.AltitudeAboveOceanMeters);

    Sample.Humidity = GetHumidity(
        Planet, Environment, Unit, Sample.TemperatureK, Sample.AltitudeAboveOceanMeters);

    Sample.bValid = true;

    return Sample;
}

FClimateSample FPlanetClimate::Sample(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction)
{
    FVector3d Unit;

    if (!FPlanetSurfaceQuery::TryGetDirection(Direction, Unit))
    {
        return FClimateSample();
    }

    const double Elevation = FPlanetTerrain::GetElevationMeters(Planet, Settings, Unit);
    const FVector3d Normal = FPlanetSurfaceQuery::GetSurfaceNormal(Planet, Settings, Unit);

    return SampleWithTerrain(Planet, Environment, Unit, Elevation, Normal);
}
