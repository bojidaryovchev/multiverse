// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetClimate.h"
#include "PlanetBiome.h"
#include "PlanetWeather.h"
#include "PlanetSurfaceQuery.h"

/**
 * PlanetEnvironmentQuery.h
 *
 * One question, one answer: what is it like here?
 *
 * Everything downstream of the environment - materials, vegetation, wildlife,
 * audio, and eventually survival gameplay, resources, construction and NPCs -
 * asks this rather than assembling the answer itself from climate, biome and
 * weather. There are two reasons that matters more than convenience.
 *
 * **Consistency.** Five systems reproducing the same derivation will
 * eventually disagree about one of them, and the disagreement will be
 * invisible: the material says desert, the spawner says grassland, and nobody
 * notices until someone wonders why there are birds over the sand.
 *
 * **Cost.** A climate sample is a terrain evaluation plus a normal, which is
 * four noise evaluations; a naive caller asking for temperature, then humidity,
 * then biome pays for it three times. Sampling once and returning everything
 * makes the expensive part happen once by construction.
 *
 * This is also the API a headless server would use. Nothing here touches a
 * renderer, an actor or a tick, so "which biome is the player in" and "is it
 * raining on them" are answerable without drawing anything - which is what a
 * future authoritative server needs and what would be impossible if the answer
 * lived in a material.
 */
struct UNIVERSEPLANET_API FEnvironmentSample
{
    /** Where. Unit direction from the planet centre, planet-local. */
    FVector3d Direction = FVector3d(0.0, 0.0, 1.0);

    /** The permanent properties of this place. */
    FClimateSample Climate;

    /** Which biomes it is, and in what proportion. */
    FBiomeBlend Biome;

    /**
     * What the sky is doing, at the time this was sampled.
     *
     * The only part of this structure that is not a function of the planet
     * alone - see PlanetWeather.h for why that distinction is kept sharp.
     */
    FWeatherSample Weather;

    /** Terrain surface position, planet-local metres. */
    FVector3d SurfacePositionMeters = FVector3d::ZeroVector;

    /** Terrain normal at that point. */
    FVector3d NormalUnit = FVector3d(0.0, 0.0, 1.0);

    bool bValid = false;

    // --- Convenience ------------------------------------------------------
    //
    // Named accessors for the questions actually asked, so a call site reads as
    // what it means instead of reaching three structures deep.

    EPlanetBiome GetBiome() const { return Biome.GetDominant(); }
    double GetTemperatureK() const { return Climate.TemperatureK; }
    double GetTemperatureCelsius() const { return Climate.GetTemperatureCelsius(); }
    double GetHumidity() const { return Climate.Humidity; }
    double GetElevationMeters() const { return Climate.ElevationMeters; }
    double GetAltitudeAboveOceanMeters() const { return Climate.AltitudeAboveOceanMeters; }
    double GetSlopeCosine() const { return Climate.SlopeCosine; }

    bool IsOcean() const { return Climate.bOcean; }
    double GetWaterDepthMeters() const { return Climate.WaterDepthMeters; }

    EWeatherState GetWeatherState() const { return Weather.State; }
    const FVector3d& GetWindDirection() const { return Weather.WindDirection; }
    double GetWindSpeedMs() const { return Weather.WindSpeedMs; }
    double GetSurfaceWetness() const { return Weather.SurfaceWetness; }

    FString ToDebugString(EPlanetBiosphere Biosphere) const;
};

class UNIVERSEPLANET_API FPlanetEnvironmentQuery
{
public:
    /**
     * Everything about a surface direction, including weather at a time.
     *
     * Costs one terrain evaluation and one normal - about two microseconds.
     * Not something to call per-vertex without meaning to; the mesh builder
     * does exactly that, deliberately, and uses the cheaper overload below.
     */
    static FEnvironmentSample Sample(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction,
        double SimulationTimeSeconds);

    /**
     * The same, for a caller that has already evaluated the terrain.
     *
     * The patch mesher has elevation and normal in hand for every vertex.
     * Making it discard them and re-evaluate would double the cost of meshing.
     */
    static FEnvironmentSample SampleWithTerrain(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FVector3d& Direction,
        double ElevationMeters,
        const FVector3d& SurfaceNormal,
        double SimulationTimeSeconds);

    /**
     * Climate and biome only, with no weather.
     *
     * For callers that genuinely do not care what the sky is doing - the
     * terrain material, vegetation placement - and would otherwise pay for
     * five cell evaluations and a noise field to learn something they discard.
     */
    static FEnvironmentSample SampleStatic(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction);

    /**
     * Underwater state at a planet-local position, rather than at the surface
     * below it.
     *
     * A separate question from FClimateSample::bOcean, and confusing the two
     * is a bug waiting to happen: the ground below a flying craft can be ocean
     * while the craft is a kilometre up in clear air. This asks about the
     * *position*.
     */
    static bool IsUnderwater(
        const FPlanetEnvironmentDescriptor& Environment,
        const FVector3d& PlanetLocalMeters,
        double& OutDepthMeters);
};
