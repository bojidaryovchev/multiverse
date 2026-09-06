// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetSurface.h"

/**
 * PlanetGravity.h
 *
 * Gravity as a vector field around a spherical body.
 *
 *     direction = normalize(planetCentre - objectPosition)
 *     magnitude = g_surface * (R / r)^2
 *
 * There is no world "down" in this project and there cannot be one. A player
 * standing on the far side of a planet from another player has an "up" that is
 * the exact negative of theirs; both are correct. Any code that reaches for a
 * fixed -Z is asserting that the universe has a preferred axis, which it does
 * not, and the failure mode is not a compile error - it is a player walking
 * over the horizon and falling off the world. So the direction is computed from
 * geometry at every query, and this header is the only place a gravity vector
 * is produced.
 *
 *
 * WHY 1/r^2 AND WHAT HAPPENS INSIDE THE PLANET
 *
 * Outside the body, Newton's shell theorem makes a uniform sphere behave
 * exactly like a point mass at its centre, so g(r) = g_surface * (R/r)^2. That
 * is the physically right answer and it is what an orbiting craft needs: orbits
 * only close if the falloff is correct.
 *
 * Inside the body, the same theorem gives g(r) = g_surface * (r / R) - only the
 * mass beneath you pulls, so gravity falls *linearly* to exactly zero at the
 * centre. Extending the 1/r^2 law inward instead would send the magnitude to
 * infinity at r = 0, and something will eventually end up there: a debug
 * teleport, a badly clamped spawn, a projectile that tunnelled. A singularity
 * inside the playable volume is a crash waiting for a reason, and the linear
 * form is both the correct physics and the one that cannot blow up.
 *
 * This module is pure mathematics on plain data - no UObject, no Actor, no
 * tick - so it runs in the standalone test harness and on worker threads.
 */
struct UNIVERSEPLANET_API FPlanetGravityField
{
    /** Sea-level reference radius, in metres. Must be > 0 to be valid. */
    double RadiusMeters = 6371000.0;

    /** Acceleration at RadiusMeters, in m/s^2. */
    double SurfaceGravityMs2 = 9.81;

    FPlanetGravityField() = default;

    FPlanetGravityField(double InRadiusMeters, double InSurfaceGravityMs2)
        : RadiusMeters(InRadiusMeters)
        , SurfaceGravityMs2(InSurfaceGravityMs2)
    {
    }

    /** The field of a generated planet. The only conversion point. */
    static FPlanetGravityField FromPlanet(const FPlanetSurfaceDescriptor& Planet)
    {
        return FPlanetGravityField(Planet.RadiusMeters, Planet.SurfaceGravityMs2);
    }

    bool IsValid() const { return RadiusMeters > 0.0 && SurfaceGravityMs2 >= 0.0; }

    /**
     * Magnitude at a distance from the centre, in m/s^2. Never negative,
     * never infinite, exactly zero at the centre.
     */
    double GetMagnitudeMs2(double DistanceFromCentreMeters) const;

    /**
     * The acceleration vector at a planet-relative position, in m/s^2.
     *
     * Points at the centre. Returns the zero vector exactly at the centre,
     * where the direction is genuinely undefined and the magnitude is zero
     * anyway, so no arbitrary axis has to be invented.
     */
    FVector3d GetAccelerationMs2(const FVector3d& PlanetLocalMeters) const;

    /**
     * The local up direction at a planet-relative position: away from the
     * centre, unit length.
     *
     * Undefined at the centre. The two-argument form says so through its
     * return value; the one-argument form falls back to +Z, which is a lie
     * but a stable and documented one, and is only ever reached by a query at
     * the exact centre of a planet.
     */
    static bool TryGetLocalUp(const FVector3d& PlanetLocalMeters, FVector3d& OutUp);
    static FVector3d GetLocalUp(const FVector3d& PlanetLocalMeters);

    /** Escape velocity from the surface, in m/s: sqrt(2 g R). */
    double GetEscapeVelocityMs() const;

    /**
     * Speed of a circular orbit at a distance from the centre, in m/s.
     *
     * Only meaningful outside the body; returns 0 for a distance inside it,
     * where a circular orbit does not exist.
     */
    double GetCircularOrbitSpeedMs(double DistanceFromCentreMeters) const;

    /** The standard gravitational parameter mu = g R^2, in m^3/s^2. */
    double GetGravitationalParameter() const { return SurfaceGravityMs2 * RadiusMeters * RadiusMeters; }
};
