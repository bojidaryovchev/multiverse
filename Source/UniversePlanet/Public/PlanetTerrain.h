// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetSurface.h"

/**
 * PlanetTerrain.h
 *
 * The terrain function: a deterministic elevation for any direction on a
 * planet.
 *
 *     elevation = f(planet seed, unit direction)
 *     position  = direction * (radius + elevation)
 *
 * Note what is absent. There is no iterative erosion pass, no accumulation, no
 * dependence on neighbouring samples and no state carried between calls. A
 * point's height is a pure function of where it is, which is what makes terrain
 * reconstructible from a seed instead of stored, and what makes patches
 * generatable in any order on any thread.
 *
 *
 * THE LAYERS
 *
 * Terrain is composed rather than being one noise field, because a single FBM
 * at any amplitude gives uniform rolling hills - recognisably artificial, with
 * no distinction between a continent and a mountain range.
 *
 *   continentalness   very low frequency, decides land against ocean basin
 *          |
 *          v
 *   macro elevation   broad shape of the land that continentalness selected
 *          |
 *          v
 *   mountains         ridged and domain-warped, masked to land, so ranges run
 *          |          in lines rather than appearing as isolated bumps
 *          v
 *   detail            high frequency, small scale, everywhere
 *
 * The masking is what makes this read as geography: mountains are multiplied by
 * a land mask derived from continentalness, so ranges sit on continents instead
 * of rising out of the middle of an ocean.
 *
 * Each layer draws from its own seed sub-stream, so a future change to the
 * mountain layer leaves continents where they are.
 *
 *
 * DETERMINISM
 *
 * Integer hashing plus add, multiply and floor throughout - no transcendentals
 * anywhere on this path. Terrain is therefore bit-identical across platforms
 * and compilers, a stronger guarantee than Sprint 001's star generation, which
 * calls into libm. See PlanetNoise.h.
 */
class UNIVERSEPLANET_API FPlanetTerrain
{
public:
    /**
     * Elevation in metres relative to the sea-level radius. Negative is below.
     *
     * The result is always within [-MaxDepth, +MaxElevation]; the layers are
     * combined in a normalised space and scaled at the end, so the bound holds
     * by construction rather than by clamping.
     */
    static double GetElevationMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction);

    /** Planet-centred position of the surface at a direction, in metres. */
    static FVector3d GetSurfacePositionMeters(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction)
    {
        const double Elevation = GetElevationMeters(Planet, Settings, Direction);
        const double Radius = Planet.RadiusMeters + Elevation;
        return FVector3d(Direction.X * Radius, Direction.Y * Radius, Direction.Z * Radius);
    }

    /**
     * Surface normal at a direction, from the gradient of the terrain function.
     *
     * Sampled by finite differences along two tangents rather than from mesh
     * triangles. That choice is what keeps normals continuous across patch and
     * cube-face borders: a triangle-derived normal depends on which triangles
     * happen to exist, so two patches meeting at a seam compute different
     * normals for the same point and the seam lights up as a visible crease.
     * A gradient-derived normal depends only on position, so both sides agree.
     *
     * Epsilon is expressed as an angle so the sample spacing scales with the
     * planet rather than being tuned for one radius.
     */
    static FVector3d GetSurfaceNormal(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction,
        double AngularEpsilon);

    /**
     * The layer breakdown at a point, for debugging and for the material.
     * Values are the normalised layer outputs before scaling to metres.
     */
    struct FTerrainSample
    {
        double Continentalness = 0.0;
        double LandMask = 0.0;
        double Mountains = 0.0;
        double Detail = 0.0;
        double NormalisedElevation = 0.0;
        double ElevationMeters = 0.0;
    };

    static FTerrainSample SampleDetailed(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const FVector3d& Direction);

    /**
     * Two orthogonal unit tangents at a direction.
     *
     * Direction must be unit length. The tangents are not re-normalised
     * against a non-unit input, because a non-unit direction would already
     * have produced the wrong elevation - the noise field is sampled at the
     * direction itself, so its magnitude is part of the query.
     *
     * Picks the reference axis away from the direction to avoid a degenerate
     * cross product. A fixed reference axis would produce garbage tangents at
     * two points on every planet, and those two points would sit at the poles
     * of whatever axis was chosen - precisely where a naive implementation
     * looks fine right up until someone flies there.
     */
    static void GetTangentBasis(const FVector3d& Direction, FVector3d& OutTangentU, FVector3d& OutTangentV);
};
