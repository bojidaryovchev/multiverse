// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"

/**
 * UniverseSweep.h
 *
 * Does the straight line from here to where I will be next frame pass through
 * that sphere?
 *
 *
 * WHY THIS IS IN UNIVERSECORE
 *
 * It started life inside FPlanetTrajectory, where the question was "does the
 * ship fly through the planet". Sprint 006 asks the identical question of stars
 * and of whole star systems during warp, from a module that sits *below*
 * UniversePlanet in the dependency chain and cannot call into it.
 *
 * There were two options: copy the quadratic, or move it down. Copying loses
 * every time - the numerical argument below is subtle, the two copies would
 * drift, and the copy that drifted would be the one that silently let a ship
 * pass through a star. So it lives here, in the module both callers already
 * depend on, and FPlanetTrajectory delegates to it.
 *
 *
 * WHY THIS HAS TO EXIST AT ALL
 *
 * A physics engine detects collisions by looking at where things are. That is
 * sound as long as nothing moves further in one step than the thickness of what
 * it might hit, and this project breaks that assumption by design. At warp the
 * ship covers light years per second; at 30 Hz a single frame is then a
 * substantial fraction of a light year, and a star one and a half million
 * kilometres across is not so much passed through as skipped over entirely.
 * Chaos sees two positions in empty space and reports nothing.
 *
 * No amount of substepping fixes that - it would take millions of substeps -
 * and raising the physics rate is worse. The fix is to ask the analytic
 * question directly, before the movement is committed. It is exact and O(1) no
 * matter how fast the craft is going.
 *
 *
 * NUMERICAL NOTE
 *
 * The quadratic is solved in the form that avoids catastrophic cancellation.
 * The naive (-b +/- sqrt(b^2 - 4ac)) / 2a loses most of its significant digits
 * when the ray starts far from the sphere, which here is the normal case rather
 * than an edge case: at interstellar range b^2 is around 10^32 while 4ac is
 * around 10^18, so the subtraction throws away everything that distinguishes a
 * hit from a miss. The stable form computes the root where the signs agree and
 * recovers the other from the product of the roots.
 *
 * Units are the caller's choice - metres throughout the engine - as long as the
 * start, end and radius agree. Pure mathematics on plain data: runs standalone
 * and on worker threads.
 */
struct UNIVERSECORE_API FSegmentSphereResult
{
    /** True if the segment intersects the sphere. */
    bool bHit = false;

    /**
     * True if the segment *started* inside the sphere. Distinct from bHit
     * because a caller that is already inside needs to push out rather than
     * stop short, and clamping to the entry fraction would do nothing.
     */
    bool bStartedInside = false;

    /**
     * Fraction along the segment of the first intersection, in [0, 1]. Only
     * meaningful when bHit. Zero when the segment starts inside.
     */
    double EntryFraction = 0.0;

    /** Fraction of the last intersection, in [0, 1]. Only meaningful when bHit. */
    double ExitFraction = 0.0;

    /** Position of first contact, in the caller's frame. Only when bHit. */
    FVector3d EntryPoint = FVector3d::ZeroVector;

    /**
     * Closest the segment comes to the sphere centre, and where along it that
     * happens.
     *
     * Filled in whether or not there was a hit, and useful in both cases: a
     * near miss is exactly what a proximity warning and a streaming prewarm
     * want to know about, and neither should have to run a second query for it.
     */
    double ClosestApproach = 0.0;
    double ClosestApproachFraction = 0.0;
};

class UNIVERSECORE_API FUniverseSweep
{
public:
    /**
     * Segment against a sphere centred on the origin of the caller's frame.
     *
     * A zero-length segment is handled: it hits if and only if the point is
     * inside.
     */
    static FSegmentSphereResult SegmentSphere(
        const FVector3d& Start,
        const FVector3d& End,
        double SphereRadius);

    /**
     * Segment against a sphere at an arbitrary centre. Convenience for callers
     * that have not already translated into the body's frame.
     */
    static FSegmentSphereResult SegmentSphereAt(
        const FVector3d& Start,
        const FVector3d& End,
        const FVector3d& Centre,
        double SphereRadius);
};
