// Copyright Universe Project. All Rights Reserved.

#include "PlanetEnvironmentQuery.h"
#include "PlanetTerrain.h"

FEnvironmentSample FPlanetEnvironmentQuery::SampleWithTerrain(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FVector3d& Direction,
    double ElevationMeters,
    const FVector3d& SurfaceNormal,
    double SimulationTimeSeconds)
{
    FEnvironmentSample Sample;

    Sample.Climate = FPlanetClimate::SampleWithTerrain(
        Planet, Environment, Direction, ElevationMeters, SurfaceNormal);

    if (!Sample.Climate.bValid)
    {
        return Sample;
    }

    Sample.Direction = Sample.Climate.Direction;
    Sample.NormalUnit = SurfaceNormal;

    const double SurfaceRadius = Planet.RadiusMeters + ElevationMeters;

    Sample.SurfacePositionMeters = FVector3d(
        Sample.Direction.X * SurfaceRadius,
        Sample.Direction.Y * SurfaceRadius,
        Sample.Direction.Z * SurfaceRadius);

    Sample.Biome = FPlanetBiomes::Classify(Environment, Sample.Climate);

    Sample.Weather = FPlanetWeather::Sample(Environment, Sample.Climate, SimulationTimeSeconds);

    Sample.bValid = true;

    return Sample;
}

FEnvironmentSample FPlanetEnvironmentQuery::Sample(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction,
    double SimulationTimeSeconds)
{
    FVector3d Unit;

    if (!FPlanetSurfaceQuery::TryGetDirection(Direction, Unit))
    {
        return FEnvironmentSample();
    }

    const double Elevation = FPlanetTerrain::GetElevationMeters(Planet, Settings, Unit);
    const FVector3d Normal = FPlanetSurfaceQuery::GetSurfaceNormal(Planet, Settings, Unit);

    return SampleWithTerrain(Planet, Environment, Unit, Elevation, Normal, SimulationTimeSeconds);
}

FEnvironmentSample FPlanetEnvironmentQuery::SampleStatic(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetEnvironmentDescriptor& Environment,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction)
{
    FVector3d Unit;

    if (!FPlanetSurfaceQuery::TryGetDirection(Direction, Unit))
    {
        return FEnvironmentSample();
    }

    const double Elevation = FPlanetTerrain::GetElevationMeters(Planet, Settings, Unit);
    const FVector3d Normal = FPlanetSurfaceQuery::GetSurfaceNormal(Planet, Settings, Unit);

    FEnvironmentSample Sample;

    Sample.Climate = FPlanetClimate::SampleWithTerrain(Planet, Environment, Unit, Elevation, Normal);

    if (!Sample.Climate.bValid)
    {
        return Sample;
    }

    Sample.Direction = Sample.Climate.Direction;
    Sample.NormalUnit = Normal;

    const double SurfaceRadius = Planet.RadiusMeters + Elevation;

    Sample.SurfacePositionMeters = FVector3d(
        Sample.Direction.X * SurfaceRadius,
        Sample.Direction.Y * SurfaceRadius,
        Sample.Direction.Z * SurfaceRadius);

    Sample.Biome = FPlanetBiomes::Classify(Environment, Sample.Climate);

    // Weather deliberately left at its default. bValid on the weather sample
    // stays false, which is how a caller can tell an unasked question from a
    // clear sky.
    Sample.bValid = true;

    return Sample;
}

bool FPlanetEnvironmentQuery::IsUnderwater(
    const FPlanetEnvironmentDescriptor& Environment,
    const FVector3d& PlanetLocalMeters,
    double& OutDepthMeters)
{
    OutDepthMeters = 0.0;

    if (!Environment.HasOcean())
    {
        return false;
    }

    const double Distance = PlanetLocalMeters.Size();

    if (Distance >= Environment.OceanRadiusMeters)
    {
        return false;
    }

    OutDepthMeters = Environment.OceanRadiusMeters - Distance;

    return true;
}

FString FEnvironmentSample::ToDebugString(EPlanetBiosphere Biosphere) const
{
    if (!bValid)
    {
        return TEXT("<invalid environment sample>");
    }

    FString BiomeText;

    for (int32 Index = 0; Index < Biome.Num; ++Index)
    {
        BiomeText += FString::Printf(
            TEXT("%s%s %.0f%%"),
            (Index > 0) ? TEXT(" + ") : TEXT(""),
            LexToString(Biome.Biomes[Index]),
            Biome.Weights[Index] * 100.0);
    }

    return FString::Printf(
        TEXT("%s | %.1f C  humid %.2f  alt %.0f m  slope %.2f | %s%s | wind %.1f m/s  wet %.2f | %s"),
        *BiomeText,
        Climate.GetTemperatureCelsius(),
        Climate.Humidity,
        Climate.AltitudeAboveOceanMeters,
        Climate.SlopeCosine,
        Climate.bOcean ? TEXT("OCEAN ") : TEXT("land"),
        Climate.bOcean ? *FString::Printf(TEXT("%.0f m deep"), Climate.WaterDepthMeters) : TEXT(""),
        Weather.WindSpeedMs,
        Weather.SurfaceWetness,
        Weather.bValid ? LexToString(Weather.State) : TEXT("(weather not sampled)"));

    (void)Biosphere;
}
