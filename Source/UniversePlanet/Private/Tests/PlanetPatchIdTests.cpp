// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"
#include "PlanetPatchId.h"

using CubeSphere::EFace;

namespace
{
    constexpr int32 FaceCount = CubeSphere::FaceCount;

    EFace FaceAt(int32 Index) { return static_cast<EFace>(Index); }
}

/** Parent/child relationships, quadrant identity and containment. */
bool UniverseTest_PatchIdHierarchy(FUniverseTestResult& Result)
{
    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const FPlanetPatchId Root = FPlanetPatchId::Root(FaceAt(FaceIndex));

        UVERIFY_TRUE(Result, Root.IsValid());
        UVERIFY_TRUE(Result, Root.IsRoot());
        UVERIFY_EQ_INT(Result, Root.GetGridSize(), 1);
        UVERIFY_TRUE(Result, Root.Contains(Root));

        // Descend a few levels, checking each step both ways.
        FPlanetPatchId Current = Root;
        for (uint8 Level = 0; Level < 8; ++Level)
        {
            UVERIFY_TRUE(Result, Current.CanSplit());

            for (int32 Q = 0; Q < 4; ++Q)
            {
                const EPlanetPatchQuadrant Quadrant = static_cast<EPlanetPatchQuadrant>(Q);
                const FPlanetPatchId Child = Current.GetChild(Quadrant);

                UVERIFY_TRUE(Result, Child.IsValid());
                UVERIFY_EQ_INT(Result, Child.Level, Current.Level + 1);
                UVERIFY_EQ_INT(Result, Child.Face, Current.Face);

                // Child -> parent returns exactly where we came from.
                UVERIFY_TRUE(Result, Child.GetParent() == Current);
                UVERIFY_EQ_INT(Result, static_cast<int32>(Child.GetQuadrantInParent()), Q);

                // Containment holds downward and not upward.
                UVERIFY_TRUE(Result, Current.Contains(Child));
                UVERIFY_FALSE(Result, Child.Contains(Current));

                // The four children are distinct.
                for (int32 Other = 0; Other < Q; ++Other)
                {
                    UVERIFY_TRUE(Result, Child != Current.GetChild(static_cast<EPlanetPatchQuadrant>(Other)));
                }
            }

            Current = Current.GetChild(EPlanetPatchQuadrant::NorthEast);
        }

        // Containment must not leak between faces.
        for (int32 OtherFace = 0; OtherFace < FaceCount; ++OtherFace)
        {
            if (OtherFace == FaceIndex)
            {
                continue;
            }
            UVERIFY_FALSE(Result, Root.Contains(FPlanetPatchId::Root(FaceAt(OtherFace))));
        }
    }

    // A patch at the deepest level cannot split further - the guard exists so a
    // runaway LOD request fails a check rather than overflowing the level byte.
    const FPlanetPatchId Deepest(EFace::PosX, FPlanetPatchId::MaxLevel, 0, 0);
    UVERIFY_TRUE(Result, Deepest.IsValid());
    UVERIFY_FALSE(Result, Deepest.CanSplit());

    return Result.Passed();
}

/**
 * The four children must tile the parent exactly: their UV bounds must union
 * to the parent's with no gap and no overlap.
 *
 * A half-ULP gap here would become a permanent crack running the length of
 * every patch border, so the comparison is exact rather than toleranced. This
 * works because all patch bounds are dyadic (see CubeSphere.h).
 */
bool UniverseTest_PatchIdChildCoverage(FUniverseTestResult& Result)
{
    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        for (uint8 Level = 0; Level < 6; ++Level)
        {
            const uint32 GridSize = 1u << Level;

            for (uint32 Y = 0; Y < GridSize; ++Y)
            {
                for (uint32 X = 0; X < GridSize; ++X)
                {
                    const FPlanetPatchId Parent(FaceAt(FaceIndex), Level, X, Y);

                    double ParentMinU = 0.0, ParentMinV = 0.0, ParentMaxU = 0.0, ParentMaxV = 0.0;
                    Parent.GetUVBounds(ParentMinU, ParentMinV, ParentMaxU, ParentMaxV);

                    const FPlanetPatchId SW = Parent.GetChild(EPlanetPatchQuadrant::SouthWest);
                    const FPlanetPatchId SE = Parent.GetChild(EPlanetPatchQuadrant::SouthEast);
                    const FPlanetPatchId NW = Parent.GetChild(EPlanetPatchQuadrant::NorthWest);
                    const FPlanetPatchId NE = Parent.GetChild(EPlanetPatchQuadrant::NorthEast);

                    double Bounds[4][4];
                    const FPlanetPatchId Children[4] = { SW, SE, NW, NE };
                    for (int32 Index = 0; Index < 4; ++Index)
                    {
                        Children[Index].GetUVBounds(
                            Bounds[Index][0], Bounds[Index][1], Bounds[Index][2], Bounds[Index][3]);
                    }

                    // The shared internal edges must coincide exactly.
                    // SW/SE meet at the parent's mid-U; SW/NW at mid-V.
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[0][2], Bounds[1][0]);  // SW.maxU == SE.minU
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[2][2], Bounds[3][0]);  // NW.maxU == NE.minU
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[0][3], Bounds[2][1]);  // SW.maxV == NW.minV
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[1][3], Bounds[3][1]);  // SE.maxV == NE.minV

                    // The outer edges must coincide with the parent's exactly.
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[0][0], ParentMinU);    // SW.minU
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[0][1], ParentMinV);    // SW.minV
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[3][2], ParentMaxU);    // NE.maxU
                    UVERIFY_EQ_DOUBLE_EXACT(Result, Bounds[3][3], ParentMaxV);    // NE.maxV

                    // A point on a shared child border must give the identical
                    // direction from either child - the mesh-level consequence
                    // of the bounds matching.
                    const FVector3d FromSW = SW.GetDirectionAt(1.0, 0.5);
                    const FVector3d FromSE = SE.GetDirectionAt(0.0, 0.5);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, FromSW.X, FromSE.X);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, FromSW.Y, FromSE.Y);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, FromSW.Z, FromSE.Z);

                    // And a child border must agree with the parent's own
                    // sampling of the same point, so LOD changes do not move
                    // the surface.
                    const FVector3d FromParent = Parent.GetDirectionAt(0.5, 0.25);
                    const FVector3d FromChild = SW.GetDirectionAt(1.0, 0.5);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, FromParent.X, FromChild.X);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, FromParent.Y, FromChild.Y);
                    UVERIFY_EQ_DOUBLE_EXACT(Result, FromParent.Z, FromChild.Z);
                }
            }
        }
    }

    return Result.Passed();
}

/**
 * Neighbour lookup, including across cube-face seams.
 *
 * The strong assertion here is symmetry: if A's neighbour in some direction is
 * B, then A must appear among B's four neighbours. That single property catches
 * essentially every way the seam table or the index reversal can be wrong,
 * because an incorrect crossing lands on a patch that does not point back.
 */
bool UniverseTest_PatchIdNeighbours(FUniverseTestResult& Result)
{
    int32 SeamCrossings = 0;
    int32 SameFaceCrossings = 0;

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        for (uint8 Level = 0; Level <= 4; ++Level)
        {
            const uint32 GridSize = 1u << Level;

            for (uint32 Y = 0; Y < GridSize; ++Y)
            {
                for (uint32 X = 0; X < GridSize; ++X)
                {
                    const FPlanetPatchId Patch(FaceAt(FaceIndex), Level, X, Y);

                    for (int32 D = 0; D < 4; ++D)
                    {
                        const EPlanetPatchNeighbour Direction = static_cast<EPlanetPatchNeighbour>(D);

                        FPlanetPatchId Neighbour;
                        UVERIFY_TRUE(Result, Patch.TryGetNeighbour(Direction, Neighbour));

                        // A planet surface has no boundary: every patch has
                        // four valid neighbours, always.
                        UVERIFY_TRUE(Result, Neighbour.IsValid());
                        UVERIFY_EQ_INT(Result, Neighbour.Level, Level);
                        UVERIFY_TRUE(Result, Neighbour != Patch);

                        if (Neighbour.Face == Patch.Face)
                        {
                            ++SameFaceCrossings;
                        }
                        else
                        {
                            ++SeamCrossings;
                        }

                        // Symmetry: this patch must be one of the neighbour's
                        // own neighbours.
                        bool bFoundBack = false;
                        for (int32 BackD = 0; BackD < 4; ++BackD)
                        {
                            FPlanetPatchId Back;
                            if (Neighbour.TryGetNeighbour(static_cast<EPlanetPatchNeighbour>(BackD), Back)
                                && Back == Patch)
                            {
                                bFoundBack = true;
                                break;
                            }
                        }
                        UVERIFY_TRUE(Result, bFoundBack);
                    }
                }
            }
        }
    }

    UVERIFY_TRUE(Result, SameFaceCrossings > 0);
    UVERIFY_TRUE(Result, SeamCrossings > 0);

    // At level 0 every one of the six root patches has four neighbours and all
    // of them are on other faces - the cube's edge count seen from patch space.
    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const FPlanetPatchId Root = FPlanetPatchId::Root(FaceAt(FaceIndex));
        for (int32 D = 0; D < 4; ++D)
        {
            FPlanetPatchId Neighbour;
            UVERIFY_TRUE(Result, Root.TryGetNeighbour(static_cast<EPlanetPatchNeighbour>(D), Neighbour));
            UVERIFY_TRUE(Result, Neighbour.Face != Root.Face);
            UVERIFY_EQ_INT(Result, Neighbour.Level, 0);
        }
    }

    return Result.Passed();
}

/**
 * A neighbour found through patch indices must actually be adjacent on the
 * sphere: the two patches must share a border, so sampling the same physical
 * point from each must give the same direction.
 *
 * Index bookkeeping can be self-consistent and still geometrically wrong -
 * symmetry alone would not catch a table that swapped two faces consistently.
 * This ties the addressing back to real geometry.
 */
bool UniverseTest_PatchIdNeighbourGeometry(FUniverseTestResult& Result)
{
    int32 Checked = 0;

    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        for (uint8 Level = 1; Level <= 3; ++Level)
        {
            const uint32 GridSize = 1u << Level;

            for (uint32 Y = 0; Y < GridSize; ++Y)
            {
                for (uint32 X = 0; X < GridSize; ++X)
                {
                    const FPlanetPatchId Patch(FaceAt(FaceIndex), Level, X, Y);

                    for (int32 D = 0; D < 4; ++D)
                    {
                        const EPlanetPatchNeighbour Direction = static_cast<EPlanetPatchNeighbour>(D);

                        FPlanetPatchId Neighbour;
                        if (!Patch.TryGetNeighbour(Direction, Neighbour))
                        {
                            continue;
                        }

                        // The centres of two genuinely adjacent same-level
                        // patches are about one patch apart. Allow generous
                        // slack for cube-sphere distortion, but not enough to
                        // let a non-adjacent patch through.
                        const FVector3d A = Patch.GetCentreDirection();
                        const FVector3d B = Neighbour.GetCentreDirection();

                        const double Dot = FMath::Clamp(A.X * B.X + A.Y * B.Y + A.Z * B.Z, -1.0, 1.0);
                        const double Angle = FMath::Acos(Dot);

                        const double PatchAngle = 2.0 * Patch.GetAngularRadius();
                        UVERIFY_TRUE(Result, Angle <= PatchAngle * 1.75);
                        UVERIFY_TRUE(Result, Angle > 0.0);

                        ++Checked;
                    }
                }
            }
        }
    }

    UVERIFY_TRUE(Result, Checked > 100);

    return Result.Passed();
}

/** Serialisation round trip, and rejection of malformed input. */
bool UniverseTest_PatchIdSerialization(FUniverseTestResult& Result)
{
    const FPlanetPatchId Cases[] = {
        FPlanetPatchId::Root(EFace::PosX),
        FPlanetPatchId(EFace::NegZ, 1, 1, 0),
        FPlanetPatchId(EFace::PosY, 12, 4095, 1234),
        FPlanetPatchId(EFace::NegY, FPlanetPatchId::MaxLevel, (1u << FPlanetPatchId::MaxLevel) - 1u, 0),
    };

    for (const FPlanetPatchId& Original : Cases)
    {
        UVERIFY_TRUE(Result, Original.IsValid());

        FUniverseByteWriter Writer;
        Original.Serialize(Writer);
        UVERIFY_EQ_INT(Result, Writer.Num(), FPlanetPatchId::SerializedSizeBytes);

        FUniverseByteReader Reader(Writer.GetBytes());
        FPlanetPatchId Restored;
        UVERIFY_TRUE(Result, Restored.Deserialize(Reader));
        UVERIFY_TRUE(Result, Restored == Original);
        UVERIFY_EQ_UINT(Result, Restored.GetStableHash64(), Original.GetStableHash64());
        UVERIFY_TRUE(Result, Reader.AtEnd());
    }

    // Distinct addresses must not collide, including permutations.
    UVERIFY_TRUE(Result,
        FPlanetPatchId(EFace::PosX, 5, 3, 7).GetStableHash64()
        != FPlanetPatchId(EFace::PosX, 5, 7, 3).GetStableHash64());
    UVERIFY_TRUE(Result,
        FPlanetPatchId(EFace::PosX, 5, 3, 7).GetStableHash64()
        != FPlanetPatchId(EFace::NegX, 5, 3, 7).GetStableHash64());
    UVERIFY_TRUE(Result,
        FPlanetPatchId(EFace::PosX, 5, 3, 7).GetStableHash64()
        != FPlanetPatchId(EFace::PosX, 6, 3, 7).GetStableHash64());

    // The persistence key must separate planets that share a patch address.
    const FPlanetPatchId Shared(EFace::PosZ, 8, 100, 200);
    UVERIFY_TRUE(Result, Shared.GetPersistenceKey(1234) != Shared.GetPersistenceKey(5678));
    UVERIFY_EQ_UINT(Result, Shared.GetPersistenceKey(1234), Shared.GetPersistenceKey(1234));

    // An out-of-range level must be rejected, not clamped: silently repairing a
    // corrupt address would attach persistent data to the wrong ground.
    {
        FUniverseByteWriter Writer;
        Writer.WriteUInt8(0);
        Writer.WriteUInt8(FPlanetPatchId::MaxLevel + 1);
        Writer.WriteUInt32(0);
        Writer.WriteUInt32(0);
        FUniverseByteReader Reader(Writer.GetBytes());
        FPlanetPatchId Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Reader));
    }

    // An out-of-range face must be rejected.
    {
        FUniverseByteWriter Writer;
        Writer.WriteUInt8(CubeSphere::FaceCount);
        Writer.WriteUInt8(0);
        Writer.WriteUInt32(0);
        Writer.WriteUInt32(0);
        FUniverseByteReader Reader(Writer.GetBytes());
        FPlanetPatchId Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Reader));
    }

    // X or Y beyond the grid at that level must be rejected.
    {
        FUniverseByteWriter Writer;
        Writer.WriteUInt8(0);
        Writer.WriteUInt8(3);
        Writer.WriteUInt32(8);   // valid range at level 3 is 0..7
        Writer.WriteUInt32(0);
        FUniverseByteReader Reader(Writer.GetBytes());
        FPlanetPatchId Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Reader));
    }

    // A truncated stream must be rejected.
    {
        FUniverseByteWriter Writer;
        Cases[1].Serialize(Writer);
        FUniverseByteReader Short(Writer.GetBytes().GetData(), 3);
        FPlanetPatchId Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Short));
    }

    return Result.Passed();
}

/**
 * Angular size behaves sensibly, which LOD selection depends on.
 *
 * Also pins the cube-sphere distortion documented in CubeSphere.h. Patches at a
 * face CENTRE subtend a larger angle than patches at a corner, which is the
 * opposite of the intuitive guess: differentiating normalize(1, s, t) gives an
 * angular rate of 1.0 per unit s at the centre but only sqrt(2)/3 ~ 0.47 at the
 * corner, so corner cells are the small ones. If this ratio ever shifts
 * markedly, the spherification has been changed and every point on every planet
 * has moved with it.
 */
bool UniverseTest_PatchAngularSize(FUniverseTestResult& Result)
{
    // A root patch covers a whole face: a quarter of a great circle to its
    // corner, i.e. the angle between the face normal and a cube corner.
    // acos(1/sqrt(3)) ~ 0.9553 rad.
    const FPlanetPatchId Root = FPlanetPatchId::Root(EFace::PosX);
    UVERIFY_NEAR(Result, Root.GetAngularRadius(), 0.9553166, 1.0e-6);

    // Each level roughly halves the angular radius.
    double Previous = Root.GetAngularRadius();
    for (uint8 Level = 1; Level <= 10; ++Level)
    {
        const FPlanetPatchId Patch(EFace::PosX, Level, 0, 0);
        const double Radius = Patch.GetAngularRadius();

        UVERIFY_TRUE(Result, Radius > 0.0);
        UVERIFY_TRUE(Result, Radius < Previous);

        // Halving is approximate near the face corner because of distortion,
        // so the bound is loose - the point is monotone decrease at roughly
        // the right rate, not an exact factor.
        UVERIFY_TRUE(Result, Radius > Previous * 0.35);
        UVERIFY_TRUE(Result, Radius < Previous * 0.75);

        Previous = Radius;
    }

    // Distortion: at a fixed level, a corner patch subtends a larger angle
    // than a centre patch. This is the documented cost of plain normalisation.
    {
        constexpr uint8 Level = 4;
        constexpr uint32 GridSize = 1u << Level;

        const FPlanetPatchId CornerPatch(EFace::PosX, Level, 0, 0);
        const FPlanetPatchId CentrePatch(EFace::PosX, Level, GridSize / 2, GridSize / 2);

        const double CornerRadius = CornerPatch.GetAngularRadius();
        const double CentreRadius = CentrePatch.GetAngularRadius();

        UVERIFY_TRUE(Result, CentreRadius > CornerRadius);

        // The analytic linear rate ratio is 1 : sqrt(2)/3, about 2.12. Patch
        // angular radius is a diagonal rather than a single-axis rate, so the
        // measured ratio is smaller; the bound is loose enough to be robust and
        // tight enough to catch a changed mapping.
        const double Ratio = CentreRadius / CornerRadius;
        UVERIFY_TRUE(Result, Ratio > 1.05);
        UVERIFY_TRUE(Result, Ratio < 2.20);
    }

    // Physical size scales linearly with planet radius, so LOD behaves the
    // same way on planets of any size.
    {
        const FPlanetPatchId Patch(EFace::PosZ, 6, 12, 20);
        const double AtEarth = Patch.GetApproximateSizeMeters(6371000.0);
        const double AtTenth = Patch.GetApproximateSizeMeters(637100.0);
        UVERIFY_NEAR(Result, AtEarth / AtTenth, 10.0, 1.0e-9);
        UVERIFY_TRUE(Result, AtEarth > 0.0);
    }

    return Result.Passed();
}
