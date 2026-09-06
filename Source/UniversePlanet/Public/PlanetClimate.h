// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetSurface.h"
#include "PlanetEnvironment.h"

/**
 * PlanetClimate.h
 *
 * Temperature and moisture, anywhere on a planet, from its physical properties.
 *
 * This is the layer that turns "a 5,000 km rock with an atmosphere at 0.23 AU"
 * into "warm and wet here, cold and dry there". Everything visible downstream -
 * which biome, which ground material, what grows, what weather - is a
 * consequence of the two numbers this file produces, so the aim is coherence
 * rather than precision. A planet has to feel internally consistent long before
 * it has to be meteorologically accurate.
 *
 *
 * LATITUDE WITHOUT A MAP
 *
 *     sin(latitude) = dot(surfaceDirection, rotationAxis)
 *
 * From the rotation axis, never from cube-face coordinates. A cube-derived
 * latitude would put six seams and eight corners into the temperature field -
 * in exactly the places ADR-003 worked hardest to make invisible - and the
 * result would be a planet whose climate quietly revealed its topology.
 *
 * Note that `sin(latitude)` is the useful quantity, not the angle. Every
 * formula here wants the sine or its square, both of which come straight out of
 * the dot product, so the arcsine is never taken. That is not a
 * micro-optimisation: it keeps the whole climate path free of transcendentals,
 * which is what makes it bit-identical across platforms for the same reason
 * ADR-003 gives for terrain.
 *
 *
 * WHAT IS DELIBERATELY NOT MODELLED
 *
 * No atmospheric circulation, no ocean currents, no rain shadows, no seasons.
 * Those are simulations; this is a field evaluated in isolation at a point,
 * which is what lets it run on a worker thread, at any resolution, in any
 * order, with no state. The approximations are named where they are made.
 *
 * Pure mathematics on plain data.
 */

/**
 * The climate at one point on a planet's surface.
 *
 * Everything a biome classifier needs, and nothing that depends on time - this
 * is the planet's permanent identity, not its current weather.
 */
struct UNIVERSEPLANET_API FClimateSample
{
    /** Unit surface direction the sample was taken along. */
    FVector3d Direction = FVector3d(0.0, 0.0, 1.0);

    /**
     * Sine of the latitude: +1 at the north pole, 0 at the equator, -1 at the
     * south. Kept as the sine rather than an angle - see the header.
     */
    double LatitudeSin = 0.0;

    /** Terrain elevation relative to the sea-level reference radius, metres. */
    double ElevationMeters = 0.0;

    /**
     * Height above the ocean surface, in metres. Negative means submerged.
     *
     * The datum that matters for climate and for biomes, and *not* the same as
     * elevation: the ocean level is a separate radius resolved per planet, so
     * on a world with a high ocean much of what is "above sea level" by the
     * terrain datum is under water.
     */
    double AltitudeAboveOceanMeters = 0.0;

    /**
     * Surface steepness, as the cosine of the angle between the terrain normal
     * and local up: 1 is flat, 0 is a vertical wall.
     *
     * A cosine rather than an angle, again to avoid an arccosine on a path that
     * is otherwise transcendental-free, and because every consumer wants it for
     * a threshold comparison where the monotone mapping makes no difference.
     */
    double SlopeCosine = 1.0;

    /** Surface temperature in kelvin. */
    double TemperatureK = 288.0;

    /** Relative humidity-like moisture, in [0, 1]. */
    double Humidity = 0.5;

    /** True if the terrain here lies below the ocean surface. */
    bool bOcean = false;

    /** Depth below the ocean surface, in metres. Zero on land. */
    double WaterDepthMeters = 0.0;

    bool bValid = false;

    /** Temperature in degrees Celsius, for anything human-facing. */
    double GetTemperatureCelsius() const { return TemperatureK - 273.15; }
};

class UNIVERSEPLANET_API FPlanetClimate
{
public:
    /** Sine of the latitude at a surface direction. */
    static double GetLatitudeSin(
        const FPlanetEnvironmentDescriptor& Environment,
        const FVector3d& Direction);

    /**
     * Surface temperature in kelvin.
     *
     *     T = mean
     *       + (delta/2) * (1 - 2 sin^2(lat))     latitude
     *       - lapse * altitudeKm                 elevation, above the ocean only
     *       + regional                           low-frequency variation
     *
     * The latitude term is `cos(2*lat)` written without trigonometry, which is
     * the same double-humped curve real insolation follows and reaches exactly
     * the mean at 45 degrees.
     *
     * The lapse term applies only above the ocean surface, because there is no
     * air column below it to cool. Ocean temperature is instead pulled toward
     * the surface value, since water's heat capacity flattens everything.
     */
    static double GetTemperatureK(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FVector3d& Direction,
        double LatitudeSin,
        double AltitudeAboveOceanMeters);

    /**
     * Moisture in [0, 1].
     *
     * Built from four terms, all of them approximations, each named:
     *
     *   planet bias        how wet the world is overall, from ocean coverage
     *   regional pattern   large coherent wet and dry bands, from noise
     *   ocean proximity    approximated by altitude above the ocean, since a
     *                      true distance-to-coast needs a search this cannot do
     *   temperature        warm air holds more water; cold poles are deserts
     *
     * The result is large coherent regions rather than static, which is the
     * property that matters: a moisture field that varies per metre would put
     * a rainforest inside a desert.
     */
    static double GetHumidity(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FVector3d& Direction,
        double TemperatureK,
        double AltitudeAboveOceanMeters);

    /**
     * The full climate at a surface direction.
     *
     * One terrain evaluation plus one normal evaluation, so it is not free -
     * roughly two microseconds - and it is not something to call per-vertex
     * without thought. Patch meshing calls it per-vertex deliberately, since
     * that is where the material needs it.
     */
    static FClimateSample Sample(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction);

    /**
     * The same, when the caller has already evaluated the terrain.
     *
     * The mesh builder has the elevation and normal in hand for every vertex;
     * making it throw them away and re-evaluate would double the cost of
     * meshing a patch for no benefit.
     */
    static FClimateSample SampleWithTerrain(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetEnvironmentDescriptor& Environment,
        const FVector3d& Direction,
        double ElevationMeters,
        const FVector3d& SurfaceNormal);
};
