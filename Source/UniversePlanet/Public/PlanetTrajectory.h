// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetSurface.h"
#include "PlanetTerrain.h"

/**
 * PlanetTrajectory.h
 *
 * Does the straight line from here to where I will be next frame pass through
 * a planet?
 *
 *
 * WHY THIS HAS TO EXIST
 *
 * A physics engine detects collisions by looking at where things are. That is
 * sound as long as nothing moves further in one step than the thickness of what
 * it might hit. This project breaks that assumption by design: the probe has
 * fifteen thrust tiers reaching 10^12 m/s, and at 30 Hz a single frame is then
 * 3.3 x 10^10 metres - over five thousand Earth diameters. The craft is on one
 * side of the planet at the start of the frame and clean through to the other
 * side at the end of it, having never once been *inside*. Chaos sees two
 * positions in empty space, reports nothing, and the planet is simply passed
 * through as though it were not there.
 *
 * No amount of substepping fixes this at these speeds - it would take thousands
 * of substeps per frame - and raising the physics rate is worse. The fix is to
 * ask the analytic question directly: intersect the *segment* with the body,
 * before the movement is committed. That is what this class does, and it is
 * exact and O(1) regardless of how fast the craft is going.
 *
 *
 * TWO LEVELS OF ANSWER
 *
 * The bounding sphere at radius + max elevation is the cheap, conservative
 * test: no terrain evaluation, no false negatives, occasional false positives
 * over a low-lying basin. That is the right trade for the movement path, which
 * runs every frame and only needs to know when to slow down and hand over to
 * real collision.
 *
 * SweepAgainstTerrain then refines a bounding-sphere hit against the actual
 * terrain function by bisection, for the cases that want the true contact point
 * - a landing autopilot, a projectile, a debug teleport that should not end up
 * inside a mountain.
 *
 *
 * NUMERICAL NOTE
 *
 * The quadratic is solved in the form that avoids catastrophic cancellation.
 * The naive (-b +/- sqrt(b^2 - 4ac)) / 2a loses most of its significant digits
 * when the ray starts far from the sphere, which here is the normal case rather
 * than an edge case: b^2 is around 10^26 while 4ac is around 10^13, so the
 * subtraction throws away everything that distinguishes a hit from a miss. The
 * stable form computes the far root first and gets the near one by the product
 * of the roots. See the implementation.
 *
 * Pure mathematics on plain data. Runs standalone and on worker threads.
 */
struct UNIVERSEPLANET_API FPlanetSweepResult
{
    /** True if the segment intersects the body. */
    bool bHit = false;

    /**
     * True if the segment *started* inside the body. Distinct from bHit
     * because a caller that is already underground needs to push out rather
     * than stop short, and clamping to the entry fraction would do nothing.
     */
    bool bStartedInside = false;

    /**
     * Fraction along the segment of the first intersection, in [0, 1].
     * Only meaningful when bHit. Zero when the segment starts inside.
     */
    double EntryFraction = 0.0;

    /** Fraction of the last intersection, in [0, 1]. Only meaningful when bHit. */
    double ExitFraction = 0.0;

    /** Planet-local position of first contact, in metres. Only when bHit. */
    FVector3d EntryPointMeters = FVector3d::ZeroVector;

    /**
     * Closest the segment comes to the planet centre, in metres, and where
     * along it that happens.
     *
     * Filled in whether or not there was a hit, and useful in both cases: a
     * near miss is exactly what a proximity warning and a streaming prewarm
     * want to know about, and neither should have to run a second query for it.
     */
    double ClosestApproachMeters = 0.0;
    double ClosestApproachFraction = 0.0;
};

class UNIVERSEPLANET_API FPlanetTrajectory
{
public:
    /**
     * Segment against a sphere centred on the planet-local origin.
     *
     * Start and End are planet-local metres. A zero-length segment is handled:
     * it hits if and only if the point is inside.
     */
    static FPlanetSweepResult SweepSegmentAgainstSphere(
        const FVector3d& StartMeters,
        const FVector3d& EndMeters,
        double SphereRadiusMeters);

    /**
     * Segment against the planet's terrain bounding sphere, plus a margin.
     *
     * The margin is how much clearance is wanted above the highest possible
     * peak; pass the craft radius plus whatever standoff the caller needs.
     */
    static FPlanetSweepResult SweepAgainstPlanetBounds(
        const FPlanetSurfaceDescriptor& Planet,
        const FVector3d& StartMeters,
        const FVector3d& EndMeters,
        double MarginMeters = 0.0);

    /**
     * Segment against the real terrain surface.
     *
     * Rejects against the bounding sphere first, then bisects between the last
     * known-outside point and the first known-inside one. Bisection is used
     * rather than a root find because the terrain function has no derivative
     * worth trusting at these scales - it is fractal noise - and bisection
     * cannot diverge. RefinementSteps of 32 brings an Earth-diameter bracket
     * below three micrometres, so it is bounded by the terrain function cost,
     * which is 32 evaluations rather than an unbounded search.
     *
     * Returns the bounding-sphere result unchanged when the segment misses.
     */
    static FPlanetSweepResult SweepAgainstTerrain(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& StartMeters,
        const FVector3d& EndMeters,
        double MarginMeters = 0.0,
        int32 RefinementSteps = 32);

    /**
     * Clamps a movement step so it stops short of a body instead of passing
     * through it.
     *
     * Returns true if the step was shortened, with OutEndMeters set to a point
     * StopDistanceMeters clear of the bounding sphere. Returns false and leaves
     * the output alone when the path is clear, so a caller can use it as an
     * "if" without a branch on the geometry.
     *
     * A step that *starts* inside the body is not clamped - there is nothing
     * useful to clamp to, and pretending otherwise would trap anything that got
     * underground. That case is reported through bStartedInside instead and is
     * the collision system's problem, not the trajectory's.
     */
    static bool TryClampStepToBounds(
        const FPlanetSurfaceDescriptor& Planet,
        const FVector3d& StartMeters,
        FVector3d& InOutEndMeters,
        double StopDistanceMeters,
        FPlanetSweepResult& OutSweep);
};
