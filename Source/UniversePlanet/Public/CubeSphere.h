// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"

/**
 * CubeSphere.h
 *
 * The mapping between a planet's spherical surface and six square faces.
 *
 * A sphere cannot be covered by one square chart without a singularity - a
 * latitude/longitude grid degenerates at the poles, where cells become
 * infinitely thin and every quadtree operation breaks down. Six cube faces
 * cover the sphere with no singular point at all: every face is an ordinary
 * square with ordinary neighbours, so one quadtree implementation works
 * everywhere on the planet including the poles.
 *
 *
 * FACE AXES
 *
 * Each face has an outward Forward (its normal) and a right-handed (Right, Up)
 * basis, with Right x Up == Forward:
 *
 *     Face    Forward      Right (u)    Up (v)
 *     ----    -------      ---------    ------
 *     PosX    (+1, 0, 0)   ( 0,+1, 0)   (0, 0,+1)
 *     NegX    (-1, 0, 0)   ( 0,-1, 0)   (0, 0,+1)
 *     PosY    ( 0,+1, 0)   (-1, 0, 0)   (0, 0,+1)
 *     NegY    ( 0,-1, 0)   (+1, 0, 0)   (0, 0,+1)
 *     PosZ    ( 0, 0,+1)   ( 0,+1, 0)   (-1,0, 0)
 *     NegZ    ( 0, 0,-1)   ( 0,+1, 0)   (+1,0, 0)
 *
 * Every component is exactly 0 or +/-1, which is the point: the cube vector
 *
 *     Cube(face, s, t) = Forward + Right*s + Up*t          s, t in [-1, 1]
 *
 * is then computed with no rounding whatsoever. Two different faces that share
 * an edge produce *bit-identical* cube vectors along it, so after normalisation
 * they produce bit-identical directions. Seam continuity is therefore a
 * property of the construction rather than something to be patched up
 * afterwards with a tolerance. UniverseTest_CubeFaceSeamsExact asserts it for
 * all twelve edges and all eight corners.
 *
 *
 * UV CONVENTION
 *
 * Public UV is [0, 1]^2, because that is what quadtree addressing wants:
 * patch (level L, X, Y) covers u in [X/2^L, (X+1)/2^L]. The [-1, 1] cube
 * coordinate is an internal detail, s = 2u - 1.
 *
 * "North" means +V and "East" means +U throughout. Those words appear in
 * neighbour lookups and nowhere else, and they mean nothing geographic - a
 * face's +V is not the planet's north pole.
 *
 *
 * WHY UV MUST BE DYADIC
 *
 * Every guarantee below holds for UVs of the form k / 2^m, and only for those.
 *
 * The reason is that seam handling needs three operations to be exact:
 * s = 2u - 1, its inverse, and the reversal u -> 1 - u used where two faces
 * meet with opposing edge orientation. For a dyadic u all three are exact,
 * because numerator and denominator stay exactly representable integers and a
 * power of two. For an arbitrary double they are not: 2u - 1 and 1 - u each
 * round, and the two sides of a seam end up one ULP apart.
 *
 * This is not academic. It is the reason patch resolution must be 2^p + 1
 * (33, 65, 129 ...) rather than any convenient number: a patch vertex sits at
 * u = (X + i/(N-1)) / 2^L, which is dyadic only when N - 1 is a power of two.
 * Choosing N = 50 would put a one-ULP crack along every patch border on the
 * planet. FPlanetTerrainSettings enforces the constraint.
 *
 *
 * SPHERIFICATION
 *
 * Direction = normalize(Cube(face, s, t)).
 *
 * Plain normalisation, not one of the area-equalising warps. The cost is
 * tessellation uniformity: a face's centre maps to a smaller solid angle than
 * its corners, and patch areas across a face vary by a factor of 3^(3/2) ~ 5.2
 * (linear edge ratio sqrt(3) ~ 1.73). That is visible as slightly denser
 * geometry near face centres and is otherwise harmless.
 *
 * The obvious improvement is the tangent warp s' = tan(s * pi/4), which brings
 * the linear ratio down to about 1.16. It is deliberately NOT used here,
 * because tan(pi/4) evaluates to 0.9999999999999999 rather than 1 in IEEE-754.
 * That single ULP would destroy the exactness argument above: face edges would
 * no longer coincide bit-for-bit, and every seam test would need a tolerance
 * hiding a real discontinuity. Trading provable continuity for prettier
 * triangle distribution is the wrong trade at this stage. If it is ever
 * revisited, the warp must special-case |s| == 1 and it must arrive as a new
 * terrain generation version, because it moves every point on the planet.
 */
namespace CubeSphere
{
    /** The six faces. Values are frozen: they appear in patch IDs and save data. */
    enum class EFace : uint8
    {
        PosX = 0,
        NegX = 1,
        PosY = 2,
        NegY = 3,
        PosZ = 4,
        NegZ = 5,
        Count = 6
    };

    inline constexpr int32 FaceCount = 6;

    /** Which edge of a face, in face-local UV. */
    enum class EFaceEdge : uint8
    {
        West = 0,   // u = 0
        East = 1,   // u = 1
        South = 2,  // v = 0
        North = 3,  // v = 1
        Count = 4
    };

    UNIVERSEPLANET_API const TCHAR* ToString(EFace Face);
    UNIVERSEPLANET_API const TCHAR* ToString(EFaceEdge Edge);

    inline bool IsValidFace(int32 FaceIndex)
    {
        return FaceIndex >= 0 && FaceIndex < FaceCount;
    }

    /** Outward normal of a face. Components are exactly 0 or +/-1. */
    UNIVERSEPLANET_API FVector3d GetFaceForward(EFace Face);

    /** Face-local +U axis. Components are exactly 0 or +/-1. */
    UNIVERSEPLANET_API FVector3d GetFaceRight(EFace Face);

    /** Face-local +V axis. Components are exactly 0 or +/-1. */
    UNIVERSEPLANET_API FVector3d GetFaceUp(EFace Face);

    /**
     * The un-normalised cube vector for a face-local UV in [0, 1]^2.
     *
     * Exact: every term is a component of value 0 or +/-1 scaled by s or t, so
     * no rounding occurs beyond whatever s and t already carry.
     */
    inline FVector3d FaceUVToCubeVector(EFace Face, double U, double V)
    {
        const double S = 2.0 * U - 1.0;
        const double T = 2.0 * V - 1.0;

        const FVector3d Forward = GetFaceForward(Face);
        const FVector3d Right = GetFaceRight(Face);
        const FVector3d Up = GetFaceUp(Face);

        return FVector3d(
            Forward.X + Right.X * S + Up.X * T,
            Forward.Y + Right.Y * S + Up.Y * T,
            Forward.Z + Right.Z * S + Up.Z * T);
    }

    /** Unit direction on the sphere for a face-local UV in [0, 1]^2. */
    inline FVector3d FaceUVToDirection(EFace Face, double U, double V)
    {
        const FVector3d Cube = FaceUVToCubeVector(Face, U, V);
        const double LengthSquared = Cube.X * Cube.X + Cube.Y * Cube.Y + Cube.Z * Cube.Z;
        const double InvLength = 1.0 / FMath::Sqrt(LengthSquared);
        return FVector3d(Cube.X * InvLength, Cube.Y * InvLength, Cube.Z * InvLength);
    }

    /**
     * The inverse: which face a direction belongs to, and where on it.
     *
     * The face is the axis of greatest magnitude. A direction exactly on an
     * edge or corner is ambiguous - it genuinely belongs to two or three faces
     * - and the tie is broken by a fixed axis order (X, then Y, then Z) so the
     * result is deterministic rather than dependent on floating-point noise.
     */
    UNIVERSEPLANET_API void DirectionToFaceUV(const FVector3d& Direction, EFace& OutFace, double& OutU, double& OutV);

    /**
     * Re-projects a UV that has left [0, 1]^2 onto the neighbouring face.
     *
     * This is how the seam is crossed, and it is deliberately not a lookup
     * table: it builds the cube vector from the out-of-range coordinate and
     * asks DirectionToFaceUV where that lands. Because both directions run
     * through the same exact arithmetic, the answer is automatically
     * consistent for edges and corners alike, with no 24-entry adjacency table
     * to get subtly wrong.
     *
     * A canonical adjacency table is still published (GetEdgeNeighbour) for
     * documentation and for cheap neighbour queries, and a test asserts the
     * table agrees with this function.
     */
    UNIVERSEPLANET_API void WrapFaceUV(EFace Face, double U, double V, EFace& OutFace, double& OutU, double& OutV);

    /**
     * How one face's edge corresponds to its neighbour's.
     *
     * Knowing only *which* face lies across an edge is not enough to match
     * geometry along it: the neighbour meets it on some particular edge of its
     * own, and in some cases with the along-edge direction reversed. Getting
     * that reversal wrong produces terrain that is continuous on eight of the
     * twelve cube edges and mirrored on the other four - which looks like a
     * random seam bug and is miserable to track down.
     */
    struct FEdgeAdjacency
    {
        EFace NeighbourFace;
        EFaceEdge NeighbourEdge;

        /** True when the along-edge parameter runs opposite on the neighbour. */
        bool bReverse;
    };

    /**
     * The canonical edge-correspondence table.
     *
     * Derived from the face basis by hand and then asserted against the
     * geometry itself by UniverseTest_CubeFaceAdjacencyTable - an unchecked
     * adjacency table is one of the classic sources of cube-sphere seam bugs.
     */
    UNIVERSEPLANET_API FEdgeAdjacency GetEdgeAdjacency(EFace Face, EFaceEdge Edge);

    /** Convenience: just the face across an edge. */
    UNIVERSEPLANET_API EFace GetEdgeNeighbour(EFace Face, EFaceEdge Edge);

    /**
     * The face-local UV of a point a fraction Along (0..1) down the given edge.
     *
     *     West  -> (0, Along)      East  -> (1, Along)
     *     South -> (Along, 0)      North -> (Along, 1)
     */
    inline void GetEdgePointUV(EFaceEdge Edge, double Along, double& OutU, double& OutV)
    {
        switch (Edge)
        {
        case EFaceEdge::West:  OutU = 0.0;   OutV = Along; break;
        case EFaceEdge::East:  OutU = 1.0;   OutV = Along; break;
        case EFaceEdge::South: OutU = Along; OutV = 0.0;   break;
        case EFaceEdge::North: OutU = Along; OutV = 1.0;   break;
        default:               OutU = 0.0;   OutV = 0.0;   break;
        }
    }

    /**
     * Where a point on this face's edge lands in the neighbouring face's UV.
     *
     * Exact for dyadic Along: the only arithmetic is the reversal 1 - Along,
     * which is exact for such values. This is the operation the seam tests use
     * and the one a patch mesh builder would use to fetch a neighbour's border
     * samples.
     */
    inline void MapEdgePointToNeighbour(
        EFace Face, EFaceEdge Edge, double Along,
        EFace& OutFace, double& OutU, double& OutV)
    {
        const FEdgeAdjacency Adjacency = GetEdgeAdjacency(Face, Edge);
        const double NeighbourAlong = Adjacency.bReverse ? (1.0 - Along) : Along;

        OutFace = Adjacency.NeighbourFace;
        GetEdgePointUV(Adjacency.NeighbourEdge, NeighbourAlong, OutU, OutV);
    }
}
