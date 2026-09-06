// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetSurface.h"
#include "PlanetTerrain.h"

/**
 * PlanetSurfaceQuery.h
 *
 * "Where is the ground, and which way is up?" - asked by gameplay rather than
 * by the mesher.
 *
 * FPlanetTerrain answers questions about the terrain *function*: give it a unit
 * direction and it gives back an elevation. That is the right shape for a mesh
 * builder, which already knows the direction it is sampling. It is the wrong
 * shape for everything else: a character, a landing craft, a spawner and a
 * collision safety check all start from a *position* in space and want to know
 * what is underneath it. This class is that layer. It owns the projection from
 * position to direction so that no caller has to normalise by hand and get it
 * subtly wrong at the centre of a planet.
 *
 *
 * THE THREE ALTITUDES - READ THIS BEFORE USING ANY OF THEM
 *
 * "Altitude" is ambiguous, and the ambiguity is not academic: the difference
 * between two of these on Earth is nearly nine kilometres, which is also the
 * difference between a safe approach and flying into a mountain. So the word
 * never appears unqualified in this codebase. There are exactly three
 * measurements and each has a name that says which one it is:
 *
 *   GetDistanceFromCentreMeters      |position|, from the planet centre.
 *                                    Always positive. This is the one orbital
 *                                    mechanics uses, and the only one that is
 *                                    meaningful without a terrain sample.
 *
 *   GetAltitudeAboveSeaLevelMeters   distance - RadiusMeters.
 *                                    The datum is the smooth reference sphere,
 *                                    not the ground. Negative inside it.
 *                                    Cheap: no noise evaluation at all.
 *
 *   GetAltitudeAboveTerrainMeters    distance - (RadiusMeters + elevation).
 *                                    Height above the actual ground directly
 *                                    below. This is the one that decides
 *                                    whether you are about to hit something,
 *                                    and it is the expensive one, because it
 *                                    has to evaluate the terrain function.
 *
 * For someone standing on the summit of Everest these read 6,379,848 m /
 * 8,848 m / 0 m. Anything that says merely "altitude" is a bug report waiting
 * to be written.
 *
 *
 * FRAME
 *
 * Every position here is *planet-local*: metres, in a frame centred on the
 * planet centre and aligned with universe axes. Converting universe
 * coordinates into that frame is the caller's job - APlanetActor does it - and
 * deliberately not this module's, which has no notion of a universe position's
 * cell.
 *
 * Pure mathematics on plain data. Runs standalone and on worker threads.
 */
struct UNIVERSEPLANET_API FPlanetSurfaceSample
{
    /** The unit direction the sample was taken along, planet-local. */
    FVector3d Direction = FVector3d(0.0, 0.0, 1.0);

    /** Signed elevation relative to the sea-level radius, in metres. */
    double ElevationMeters = 0.0;

    /** Distance from the centre to the ground here: radius + elevation. */
    double SurfaceRadiusMeters = 0.0;

    /** Planet-local position of the ground, in metres. */
    FVector3d SurfacePositionMeters = FVector3d::ZeroVector;

    /** Terrain normal at the ground, unit length. Tilts with slope. */
    FVector3d NormalUnit = FVector3d(0.0, 0.0, 1.0);

    /**
     * Local up: straight away from the planet centre, unit length.
     *
     * Not the same as NormalUnit, and the difference matters. Up is what a
     * character's orientation and a gravity vector use - it is defined by
     * position alone and is continuous everywhere. The normal is defined by
     * the *slope*, and swings around on rough ground; orienting a character to
     * it makes them lurch on every pebble. Use Up to stand, and the normal to
     * shade and to decide whether a slope is walkable.
     */
    FVector3d UpUnit = FVector3d(0.0, 0.0, 1.0);

    /** True if the sample came from a position that could be projected. */
    bool bValid = false;
};

class UNIVERSEPLANET_API FPlanetSurfaceQuery
{
public:
    /**
     * Sample spacing used for terrain normals when a caller does not specify
     * one, in metres.
     *
     * A metre, because that is roughly the scale a character interacts with:
     * finer picks up noise below the resolution of anything they can stand on,
     * coarser smooths away slopes they can see. Expressed in metres rather
     * than as an angle so it means the same thing on a moon and on a gas
     * giant - FPlanetTerrain wants an angle, and the conversion happens here.
     */
    static constexpr double DefaultNormalSpacingMeters = 1.0;

    // --- Directions -------------------------------------------------------

    /**
     * Unit direction from the planet centre to a planet-local position.
     * Returns false exactly at the centre, where it is undefined.
     */
    static bool TryGetDirection(const FVector3d& PlanetLocalMeters, FVector3d& OutDirection);

    /** Local up at a position: away from the centre. +Z at the centre. */
    static FVector3d GetLocalUp(const FVector3d& PlanetLocalMeters);

    // --- The three altitudes (see the header comment) ---------------------

    static double GetDistanceFromCentreMeters(const FVector3d& PlanetLocalMeters);

    static double GetAltitudeAboveSeaLevelMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FVector3d& PlanetLocalMeters);

    static double GetAltitudeAboveTerrainMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& PlanetLocalMeters);

    // --- The surface ------------------------------------------------------

    /**
     * Distance from the centre to the ground along a direction, in metres:
     * RadiusMeters + elevation. Never negative for a sane planet.
     */
    static double GetSurfaceHeightMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction);

    /** Planet-local position of the ground along a direction, in metres. */
    static FVector3d GetSurfacePositionMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction);

    /**
     * Terrain normal along a direction, unit length.
     *
     * SampleSpacingMeters is the finite-difference step measured on the
     * ground; pass 0 or less for DefaultNormalSpacingMeters.
     */
    static FVector3d GetSurfaceNormal(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction,
        double SampleSpacingMeters = 0.0);

    /** Everything above, along a direction, in one call. */
    static FPlanetSurfaceSample SampleDirection(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction,
        double NormalSpacingMeters = 0.0);

    /** The same, for the ground directly below a planet-local position. */
    static FPlanetSurfaceSample SampleBelow(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& PlanetLocalMeters,
        double NormalSpacingMeters = 0.0);

    // --- Placement --------------------------------------------------------

    /**
     * A planet-local position a given height above the ground along a
     * direction. The primitive behind surface spawning and teleports.
     *
     * HeightAboveTerrainMeters may be negative, which puts the result
     * underground - useful for burying a foundation, and the caller's
     * responsibility otherwise.
     */
    static FVector3d GetPositionAboveTerrain(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction,
        double HeightAboveTerrainMeters);

    // --- Atmosphere -------------------------------------------------------

    /**
     * Where a position sits in the atmosphere: 0 at the top of it, 1 at sea
     * level, clamped outside that range. 0 for an airless body.
     *
     * A normalised depth rather than a density, because density needs a scale
     * height and a composition, and inventing those here would be a physical
     * claim this project is not yet in a position to make. This is a
     * simulation ramp with an honest name.
     */
    static double GetAtmosphericDepthFraction(
        const FPlanetSurfaceDescriptor& Planet,
        const FVector3d& PlanetLocalMeters);

    /** True if the position is at or below the top of the atmosphere. */
    static bool IsInsideAtmosphere(
        const FPlanetSurfaceDescriptor& Planet,
        const FVector3d& PlanetLocalMeters);
};
