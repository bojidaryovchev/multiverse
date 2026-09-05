// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"
#include "CubeSphere.h"

using CubeSphere::EFace;
using CubeSphere::EFaceEdge;

namespace
{
    constexpr int32 FaceCount = CubeSphere::FaceCount;

    EFace FaceAt(int32 Index) { return static_cast<EFace>(Index); }

    double Dot(const FVector3d& A, const FVector3d& B)
    {
        return A.X * B.X + A.Y * B.Y + A.Z * B.Z;
    }

    FVector3d Cross(const FVector3d& A, const FVector3d& B)
    {
        return FVector3d(
            A.Y * B.Z - A.Z * B.Y,
            A.Z * B.X - A.X * B.Z,
            A.X * B.Y - A.Y * B.X);
    }

    bool ExactlyEqual(const FVector3d& A, const FVector3d& B)
    {
        return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
    }
}

/**
 * The face basis is what every other guarantee rests on, so its properties are
 * asserted directly rather than assumed: orthonormal, right-handed, and built
 * only from exact 0 / +/-1 components.
 *
 * That last property is the load-bearing one. If someone "improves" an axis to
 * a normalised diagonal, cube vectors stop being exact, and the seam tests
 * below would start needing a tolerance to pass - which would mean the planet
 * had developed real cracks that a loosened test was hiding.
 */
bool UniverseTest_CubeSphereFaceBasis(FUniverseTestResult& Result)
{
    for (int32 Index = 0; Index < FaceCount; ++Index)
    {
        const EFace Face = FaceAt(Index);

        const FVector3d Forward = CubeSphere::GetFaceForward(Face);
        const FVector3d Right = CubeSphere::GetFaceRight(Face);
        const FVector3d Up = CubeSphere::GetFaceUp(Face);

        // Every component is exactly 0 or +/-1.
        const FVector3d Axes[3] = { Forward, Right, Up };
        for (const FVector3d& Axis : Axes)
        {
            const double Components[3] = { Axis.X, Axis.Y, Axis.Z };
            for (double Component : Components)
            {
                UVERIFY_TRUE(Result, Component == 0.0 || Component == 1.0 || Component == -1.0);
            }
        }

        // Orthonormal.
        UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Forward, Forward), 1.0);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Right, Right), 1.0);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Up, Up), 1.0);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Forward, Right), 0.0);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Forward, Up), 0.0);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Right, Up), 0.0);

        // Right-handed: Right x Up == Forward, exactly.
        UVERIFY_TRUE(Result, ExactlyEqual(Cross(Right, Up), Forward));

        // The face centre maps to the face normal.
        const FVector3d Centre = CubeSphere::FaceUVToDirection(Face, 0.5, 0.5);
        UVERIFY_TRUE(Result, ExactlyEqual(Centre, Forward));
    }

    // The six normals are distinct and form three opposing pairs.
    for (int32 A = 0; A < FaceCount; ++A)
    {
        for (int32 B = A + 1; B < FaceCount; ++B)
        {
            UVERIFY_FALSE(Result,
                ExactlyEqual(CubeSphere::GetFaceForward(FaceAt(A)), CubeSphere::GetFaceForward(FaceAt(B))));
        }
    }

    return Result.Passed();
}

/**
 * Face/UV -> direction -> face/UV must round-trip.
 *
 * Interior samples only: a point exactly on an edge belongs to two faces and
 * the round trip is legitimately allowed to return either, which the seam
 * tests cover separately.
 */
bool UniverseTest_CubeSphereRoundTrip(FUniverseTestResult& Result)
{
    constexpr int32 Steps = 17;

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const EFace Face = FaceAt(FaceIndex);

        for (int32 J = 1; J < Steps - 1; ++J)
        {
            for (int32 I = 1; I < Steps - 1; ++I)
            {
                const double U = static_cast<double>(I) / static_cast<double>(Steps - 1);
                const double V = static_cast<double>(J) / static_cast<double>(Steps - 1);

                const FVector3d Direction = CubeSphere::FaceUVToDirection(Face, U, V);

                // The direction really is a unit vector.
                UVERIFY_NEAR(Result, Direction.Size(), 1.0, 1.0e-15);

                EFace BackFace = EFace::PosX;
                double BackU = 0.0;
                double BackV = 0.0;
                CubeSphere::DirectionToFaceUV(Direction, BackFace, BackU, BackV);

                UVERIFY_EQ_INT(Result, static_cast<int32>(BackFace), FaceIndex);
                UVERIFY_NEAR(Result, BackU, U, 1.0e-12);
                UVERIFY_NEAR(Result, BackV, V, 1.0e-12);
            }
        }
    }

    return Result.Passed();
}

/**
 * The hand-written adjacency table must agree with the geometry.
 *
 * A cube-sphere adjacency table that disagrees with the actual axis basis is a
 * classic and painful bug: everything looks right until patches on one
 * particular seam fail to find their neighbours. Here the table is checked
 * against WrapFaceUV, which derives the answer from the basis itself, at
 * several points along every edge of every face.
 */
bool UniverseTest_CubeFaceAdjacencyTable(FUniverseTestResult& Result)
{
    // Just outside the edge - far enough to be unambiguous, small enough to
    // stay off the corners.
    constexpr double Outside = 1.0 + 1.0e-6;
    constexpr double Inside = -1.0e-6;

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const EFace Face = FaceAt(FaceIndex);

        for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
        {
            const EFaceEdge Edge = static_cast<EFaceEdge>(EdgeIndex);
            const EFace Expected = CubeSphere::GetEdgeNeighbour(Face, Edge);

            // Sample along the edge, avoiding the corners where three faces meet.
            for (int32 Step = 1; Step < 8; ++Step)
            {
                const double Along = static_cast<double>(Step) / 8.0;

                double U = 0.0;
                double V = 0.0;
                switch (Edge)
                {
                case EFaceEdge::West:  U = Inside;  V = Along;   break;
                case EFaceEdge::East:  U = Outside; V = Along;   break;
                case EFaceEdge::South: U = Along;   V = Inside;  break;
                case EFaceEdge::North: U = Along;   V = Outside; break;
                default: break;
                }

                EFace ActualFace = EFace::PosX;
                double OutU = 0.0;
                double OutV = 0.0;
                CubeSphere::WrapFaceUV(Face, U, V, ActualFace, OutU, OutV);

                UVERIFY_EQ_INT(Result, static_cast<int32>(ActualFace), static_cast<int32>(Expected));

                // The wrapped coordinate must be a valid face UV.
                UVERIFY_TRUE(Result, OutU >= -1.0e-9 && OutU <= 1.0 + 1.0e-9);
                UVERIFY_TRUE(Result, OutV >= -1.0e-9 && OutV <= 1.0 + 1.0e-9);
            }
        }
    }

    // A face is never its own neighbour, and opposite faces are never adjacent.
    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const EFace Face = FaceAt(FaceIndex);
        const FVector3d Forward = CubeSphere::GetFaceForward(Face);

        for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
        {
            const EFace Neighbour = CubeSphere::GetEdgeNeighbour(Face, static_cast<EFaceEdge>(EdgeIndex));
            UVERIFY_TRUE(Result, Neighbour != Face);

            const FVector3d NeighbourForward = CubeSphere::GetFaceForward(Neighbour);
            UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Forward, NeighbourForward), 0.0);
        }
    }

    return Result.Passed();
}

/**
 * The non-negotiable requirement: the shared edge between two faces must be
 * exactly the same curve on the planet, sampled from either side.
 *
 * This asserts *bit-exact* equality of the resulting direction, not equality
 * within a tolerance. That is a stronger claim than a crack-free render needs,
 * and it is deliberate: an exact test either passes or points at a real
 * defect, whereas a tolerance test silently accumulates error until a seam
 * eventually opens.
 *
 * Two things make exactness achievable, and both are asserted elsewhere so
 * they cannot be quietly removed:
 *
 *   - the face basis is built from exact 0 and +/-1 components, so cube
 *     vectors carry no rounding of their own (CubeSphereFaceBasis);
 *   - the sample positions are dyadic (k / 2^m), so 2u - 1 and the reversal
 *     1 - u are both exact. Non-dyadic samples fail this test by one ULP,
 *     which is precisely why patch resolution is constrained to 2^p + 1.
 *
 * All twelve cube edges are covered, each from both sides, via the canonical
 * edge-correspondence table rather than by re-projecting a direction - the
 * table is what real patch code would use.
 */
bool UniverseTest_CubeFaceSeamsExact(FUniverseTestResult& Result)
{
    // Dyadic: 64 is a power of two, so every Along is exactly representable
    // and so is its reversal.
    constexpr int32 Samples = 64;

    int32 EdgesChecked = 0;
    int32 ReversedEdgesSeen = 0;

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const EFace Face = FaceAt(FaceIndex);

        for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
        {
            const EFaceEdge Edge = static_cast<EFaceEdge>(EdgeIndex);
            const CubeSphere::FEdgeAdjacency Adjacency = CubeSphere::GetEdgeAdjacency(Face, Edge);
            ++EdgesChecked;
            if (Adjacency.bReverse)
            {
                ++ReversedEdgesSeen;
            }

            for (int32 Step = 0; Step <= Samples; ++Step)
            {
                const double Along = static_cast<double>(Step) / static_cast<double>(Samples);

                double U = 0.0;
                double V = 0.0;
                CubeSphere::GetEdgePointUV(Edge, Along, U, V);
                const FVector3d FromThisFace = CubeSphere::FaceUVToDirection(Face, U, V);

                EFace NeighbourFace = EFace::PosX;
                double NeighbourU = 0.0;
                double NeighbourV = 0.0;
                CubeSphere::MapEdgePointToNeighbour(Face, Edge, Along, NeighbourFace, NeighbourU, NeighbourV);

                const FVector3d FromNeighbour =
                    CubeSphere::FaceUVToDirection(NeighbourFace, NeighbourU, NeighbourV);

                // Bit-exact, not merely close.
                UVERIFY_EQ_DOUBLE_EXACT(Result, FromNeighbour.X, FromThisFace.X);
                UVERIFY_EQ_DOUBLE_EXACT(Result, FromNeighbour.Y, FromThisFace.Y);
                UVERIFY_EQ_DOUBLE_EXACT(Result, FromNeighbour.Z, FromThisFace.Z);
            }

            // The correspondence must be symmetric: following it back from the
            // neighbour returns to this face and this edge, with the same
            // reversal flag. An asymmetric table would let terrain match in one
            // direction and not the other.
            const CubeSphere::FEdgeAdjacency Back =
                CubeSphere::GetEdgeAdjacency(Adjacency.NeighbourFace, Adjacency.NeighbourEdge);
            UVERIFY_EQ_INT(Result, static_cast<int32>(Back.NeighbourFace), FaceIndex);
            UVERIFY_EQ_INT(Result, static_cast<int32>(Back.NeighbourEdge), EdgeIndex);
            UVERIFY_TRUE(Result, Back.bReverse == Adjacency.bReverse);
        }
    }

    // Six faces x four edges: all twelve cube edges, seen from both sides.
    UVERIFY_EQ_INT(Result, EdgesChecked, 24);

    // Some seams genuinely do reverse. If this were zero the table would be
    // suspiciously uniform and almost certainly wrong.
    UVERIFY_TRUE(Result, ReversedEdgesSeen > 0);
    UVERIFY_EQ_INT(Result, ReversedEdgesSeen % 2, 0);

    return Result.Passed();
}

/**
 * The dyadic requirement, stated as a test.
 *
 * A non-dyadic sample position breaks exact seam matching by one ULP. That is
 * documented in CubeSphere.h and enforced by the patch resolution constraint,
 * but a claim that is only in a comment tends not to survive contact with a
 * future contributor, so it is pinned here: this test asserts that the failure
 * mode is real and bounded to one ULP rather than something worse.
 */
bool UniverseTest_CubeSeamDyadicRequirement(FUniverseTestResult& Result)
{
    // 1/3 is not representable and not dyadic - the worst realistic case.
    const double NonDyadic = 1.0 / 3.0;

    int32 Mismatches = 0;
    int32 Compared = 0;

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const EFace Face = FaceAt(FaceIndex);

        for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
        {
            const EFaceEdge Edge = static_cast<EFaceEdge>(EdgeIndex);

            double U = 0.0;
            double V = 0.0;
            CubeSphere::GetEdgePointUV(Edge, NonDyadic, U, V);
            const FVector3d FromThisFace = CubeSphere::FaceUVToDirection(Face, U, V);

            EFace NeighbourFace = EFace::PosX;
            double NeighbourU = 0.0;
            double NeighbourV = 0.0;
            CubeSphere::MapEdgePointToNeighbour(Face, Edge, NonDyadic, NeighbourFace, NeighbourU, NeighbourV);
            const FVector3d FromNeighbour =
                CubeSphere::FaceUVToDirection(NeighbourFace, NeighbourU, NeighbourV);

            ++Compared;

            const bool bExact =
                FromNeighbour.X == FromThisFace.X &&
                FromNeighbour.Y == FromThisFace.Y &&
                FromNeighbour.Z == FromThisFace.Z;

            if (!bExact)
            {
                ++Mismatches;
            }

            // Even when not bit-exact the disagreement must stay at the level
            // of floating-point noise. Anything larger would mean the edge
            // correspondence itself is wrong, not merely rounded.
            UVERIFY_NEAR(Result, FromNeighbour.X, FromThisFace.X, 1.0e-15);
            UVERIFY_NEAR(Result, FromNeighbour.Y, FromThisFace.Y, 1.0e-15);
            UVERIFY_NEAR(Result, FromNeighbour.Z, FromThisFace.Z, 1.0e-15);
        }
    }

    UVERIFY_EQ_INT(Result, Compared, 24);

    // Reversed seams are where the non-dyadic reversal 1 - a rounds, so at
    // least some mismatches are expected. If this ever becomes zero, the
    // dyadic constraint has stopped being load-bearing and the comment in
    // CubeSphere.h should be revisited rather than trusted.
    UVERIFY_TRUE(Result, Mismatches > 0);

    return Result.Passed();
}

/**
 * The eight cube corners, where three faces meet.
 *
 * Corners are the worst case for any cube-sphere: three charts claim the same
 * point, and if any pair disagrees even slightly a permanent pinhole appears
 * in the planet. All three addressings must yield the identical direction.
 */
bool UniverseTest_CubeCornersExact(FUniverseTestResult& Result)
{
    // The eight corners of the cube, as exact unit-cube vectors.
    const double Signs[2] = { -1.0, 1.0 };
    int32 CornersChecked = 0;

    for (int32 Sx = 0; Sx < 2; ++Sx)
    {
        for (int32 Sy = 0; Sy < 2; ++Sy)
        {
            for (int32 Sz = 0; Sz < 2; ++Sz)
            {
                const FVector3d Corner(Signs[Sx], Signs[Sy], Signs[Sz]);
                ++CornersChecked;

                // The three faces that meet at this corner are the ones whose
                // normals share a sign with the corner on their own axis.
                const EFace Faces[3] = {
                    Signs[Sx] > 0.0 ? EFace::PosX : EFace::NegX,
                    Signs[Sy] > 0.0 ? EFace::PosY : EFace::NegY,
                    Signs[Sz] > 0.0 ? EFace::PosZ : EFace::NegZ,
                };

                FVector3d Directions[3];

                for (int32 Index = 0; Index < 3; ++Index)
                {
                    const EFace Face = Faces[Index];

                    // Where is this corner in that face's UV?
                    const FVector3d Forward = CubeSphere::GetFaceForward(Face);
                    const FVector3d Right = CubeSphere::GetFaceRight(Face);
                    const FVector3d Up = CubeSphere::GetFaceUp(Face);

                    // The corner lies on the face plane, so the projection is
                    // exact: every dot product is a sum of 0 and +/-1 terms.
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Dot(Corner, Forward), 1.0);

                    const double S = Dot(Corner, Right);
                    const double T = Dot(Corner, Up);
                    UVERIFY_TRUE(Result, S == 1.0 || S == -1.0);
                    UVERIFY_TRUE(Result, T == 1.0 || T == -1.0);

                    const double U = 0.5 * (S + 1.0);
                    const double V = 0.5 * (T + 1.0);
                    UVERIFY_TRUE(Result, U == 0.0 || U == 1.0);
                    UVERIFY_TRUE(Result, V == 0.0 || V == 1.0);

                    // The cube vector rebuilt from that UV must be the corner.
                    const FVector3d Rebuilt = CubeSphere::FaceUVToCubeVector(Face, U, V);
                    UVERIFY_TRUE(Result, ExactlyEqual(Rebuilt, Corner));

                    Directions[Index] = CubeSphere::FaceUVToDirection(Face, U, V);
                }

                // All three faces must agree bit-for-bit.
                for (int32 Index = 1; Index < 3; ++Index)
                {
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Directions[Index].X, Directions[0].X);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Directions[Index].Y, Directions[0].Y);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Directions[Index].Z, Directions[0].Z);
                }

                // And the corner direction really is the normalised corner.
                UVERIFY_NEAR(Result, Directions[0].Size(), 1.0, 1.0e-15);
            }
        }
    }

    UVERIFY_EQ_INT(Result, CornersChecked, 8);

    return Result.Passed();
}
