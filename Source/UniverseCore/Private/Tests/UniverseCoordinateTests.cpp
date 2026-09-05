// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniverseCoreTestList.h"
#include "UniverseCoordinates.h"
#include "UniverseScale.h"

namespace
{
    constexpr double CellD = UniverseScale::CellSizeCmD;
}

/**
 * The constants themselves are load-bearing: the exactness argument in
 * UniverseCoordinates.cpp is only valid if the cell size really is a power of
 * two and the reciprocal really is exact. If someone "tidies" the cell size to
 * a round decimal, every other guarantee in this file quietly stops holding,
 * so that assumption is asserted directly rather than assumed.
 */
bool UniverseTest_ScaleConstants(FUniverseTestResult& Result)
{
    UVERIFY_EQ_INT(Result, UniverseScale::CellSizeCm, 1099511627776LL);
    UVERIFY_EQ_DOUBLE_EXACT(Result, UniverseScale::CellSizeCmD, 1099511627776.0);

    // The reciprocal must be exact, i.e. Cell * (1/Cell) == 1 with no rounding.
    UVERIFY_EQ_DOUBLE_EXACT(Result, UniverseScale::CellSizeCmD * UniverseScale::InvCellSizeCmD, 1.0);

    // And the cell size must be a power of two.
    UVERIFY_EQ_INT(Result, UniverseScale::CellSizeCm & (UniverseScale::CellSizeCm - 1), 0);

    // Sector geometry: 2^22 cells, and a sector edge of ~4.87 light years.
    UVERIFY_EQ_INT(Result, UniverseScale::SectorSizeInCells, 4194304LL);
    UVERIFY_NEAR(Result, UniverseScale::SectorSizeLightYears, 4.8746, 0.001);

    // Worst-case local resolution: 2^40 * 2^-52 = 2^-12 cm = 2.44 um.
    UVERIFY_NEAR(Result, UniverseScale::LocalResolutionCm, 0.000244140625, 1e-12);

    // Universe half-extent per axis, in light years: 2^103 cm.
    const double HalfExtentLy =
        9223372036854775808.0 * UniverseScale::CellSizeCmD / UniverseScale::CmPerLightYear;
    UVERIFY_TRUE(Result, HalfExtentLy > 1.0e13);

    return Result.Passed();
}

/** Positive-direction normalisation and the canonical [0, CellSize) range. */
bool UniverseTest_NormalizationBasic(FUniverseTestResult& Result)
{
    // A local offset of exactly one cell must roll over to the next cell with
    // a local of exactly zero.
    FUniversePosition P = FUniversePosition::FromCells(0, 0, 0);
    P = P.OffsetByCm(FVector3d(CellD, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, P.CellX, 1);
    UVERIFY_EQ_DOUBLE_EXACT(Result, P.Local.X, 0.0);
    UVERIFY_TRUE(Result, P.IsNormalized());

    // Two and a half cells on each axis.
    FUniversePosition Q = FUniversePosition::FromCells(0, 0, 0);
    Q = Q.OffsetByCm(FVector3d(2.5 * CellD, 3.25 * CellD, 1.5 * CellD));
    UVERIFY_EQ_INT(Result, Q.CellX, 2);
    UVERIFY_EQ_INT(Result, Q.CellY, 3);
    UVERIFY_EQ_INT(Result, Q.CellZ, 1);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Q.Local.X, 0.5 * CellD);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Q.Local.Y, 0.25 * CellD);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Q.Local.Z, 0.5 * CellD);

    // Constructing directly from an out-of-range local must normalise too.
    const FUniversePosition R(5, 5, 5, FVector3d(CellD * 3.0 + 100.0, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, R.CellX, 8);
    UVERIFY_EQ_DOUBLE_EXACT(Result, R.Local.X, 100.0);

    return Result.Passed();
}

/**
 * Negative transitions. This is where truncation-toward-zero would break the
 * grid: with truncation, cells -1 and 0 would both cover part of [-Cell, Cell)
 * and a position just below zero would report cell 0 with a negative local.
 * Floor semantics are asserted explicitly here.
 */
bool UniverseTest_NormalizationNegative(FUniverseTestResult& Result)
{
    // One centimetre below the origin belongs to cell -1, near its top.
    FUniversePosition P = FUniversePosition::FromCells(0, 0, 0);
    P = P.OffsetByCm(FVector3d(-1.0, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, P.CellX, -1);
    UVERIFY_EQ_DOUBLE_EXACT(Result, P.Local.X, CellD - 1.0);
    UVERIFY_TRUE(Result, P.IsNormalized());

    // Exactly one cell below the origin is the corner of cell -1.
    FUniversePosition Q = FUniversePosition::FromCells(0, 0, 0);
    Q = Q.OffsetByCm(FVector3d(-CellD, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, Q.CellX, -1);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Q.Local.X, 0.0);

    // Two and a quarter cells below: floor(-2.25) == -3, remainder 0.75.
    FUniversePosition S = FUniversePosition::FromCells(0, 0, 0);
    S = S.OffsetByCm(FVector3d(-2.25 * CellD, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, S.CellX, -3);
    UVERIFY_EQ_DOUBLE_EXACT(Result, S.Local.X, 0.75 * CellD);

    // Crossing back and forth must return exactly to the start.
    FUniversePosition T = FUniversePosition::FromCells(-7, 3, -1);
    const FUniversePosition Start = T;
    for (int32 Index = 0; Index < 64; ++Index)
    {
        T = T.OffsetByCm(FVector3d(-CellD * 0.5, CellD * 0.5, -CellD * 0.5));
    }
    for (int32 Index = 0; Index < 64; ++Index)
    {
        T = T.OffsetByCm(FVector3d(CellD * 0.5, -CellD * 0.5, CellD * 0.5));
    }
    UVERIFY_TRUE(Result, T == Start);

    return Result.Passed();
}

/** Exact cell edges, and the representable values either side of them. */
bool UniverseTest_CellBoundaryExact(FUniverseTestResult& Result)
{
    // Sitting exactly on the upper edge is canonicalised into the next cell.
    const FUniversePosition OnEdge(4, 0, 0, FVector3d(CellD, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, OnEdge.CellX, 5);
    UVERIFY_EQ_DOUBLE_EXACT(Result, OnEdge.Local.X, 0.0);

    // The largest local value that stays in the cell is one ULP below
    // CellSize, i.e. CellSize - 2^-12 cm. That value must be exactly
    // representable, otherwise the top of every cell would be unreachable.
    const double JustBelow = CellD - UniverseScale::LocalResolutionCm;
    UVERIFY_TRUE(Result, JustBelow < CellD);
    const FUniversePosition Below(4, 0, 0, FVector3d(JustBelow, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, Below.CellX, 4);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Below.Local.X, JustBelow);

    // One resolution unit below the corner of cell 4 must land in cell 3, at
    // exactly one unit below its top - not clamp to zero, and not lose the
    // offset. This is the crossing that truncating arithmetic gets wrong.
    const FUniversePosition JustUnder(4, 0, 0, FVector3d(-UniverseScale::LocalResolutionCm, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, JustUnder.CellX, 3);
    UVERIFY_EQ_DOUBLE_EXACT(Result, JustUnder.Local.X, JustBelow);
    UVERIFY_TRUE(Result, JustUnder.IsNormalized());

    // ...and stepping back up returns exactly to the corner of cell 4.
    const FUniversePosition BackUp = JustUnder.OffsetByCm(FVector3d(UniverseScale::LocalResolutionCm, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, BackUp.CellX, 4);
    UVERIFY_EQ_DOUBLE_EXACT(Result, BackUp.Local.X, 0.0);

    // A displacement far below the local resolution is absorbed rather than
    // accumulating: the representation has a finite 2.44 um resolution and
    // this documents where that floor is. Callers integrating velocity must
    // not rely on sub-resolution steps summing (see ADR-001).
    const FUniversePosition Corner = FUniversePosition::FromCells(4, 0, 0);
    const FUniversePosition SubResolution = Corner.OffsetByCm(FVector3d(1.0e-9, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, SubResolution.CellX, 4);
    UVERIFY_TRUE(Result, SubResolution.IsNormalized());

    // A step at the resolution limit, by contrast, is preserved exactly.
    const FUniversePosition AtResolution = Corner.OffsetByCm(
        FVector3d(UniverseScale::LocalResolutionCm, 0.0, 0.0));
    UVERIFY_TRUE(Result, AtResolution != Corner);
    UVERIFY_EQ_DOUBLE_EXACT(Result, AtResolution.Local.X, UniverseScale::LocalResolutionCm);

    return Result.Passed();
}

/**
 * Walks a single representable step across a boundary in both directions and
 * checks that the two representations denote points that are actually one step
 * apart - i.e. the boundary introduces no gap and no overlap.
 */
bool UniverseTest_CellBoundaryNeighbourhood(FUniverseTestResult& Result)
{
    const double Step = 1.0;  // one centimetre, comfortably above the 2.44 um ULP

    for (int64 Cell = -3; Cell <= 3; ++Cell)
    {
        // Just below the top edge of Cell.
        const FUniversePosition A(Cell, 0, 0, FVector3d(CellD - Step, 0.0, 0.0));
        // One step further: should be exactly at the corner of Cell + 1.
        const FUniversePosition B = A.OffsetByCm(FVector3d(Step, 0.0, 0.0));

        UVERIFY_EQ_INT(Result, B.CellX, Cell + 1);
        UVERIFY_EQ_DOUBLE_EXACT(Result, B.Local.X, 0.0);

        // The measured separation must be exactly one step despite the
        // boundary crossing - this is the "no discontinuity" guarantee.
        UVERIFY_EQ_DOUBLE_EXACT(Result, FUniversePosition::DistanceCm(A, B), Step);

        // Stepping back must land exactly where we started.
        const FUniversePosition C = B.OffsetByCm(FVector3d(-Step, 0.0, 0.0));
        UVERIFY_TRUE(Result, C == A);
    }

    return Result.Passed();
}

/**
 * Accumulates an enormous total displacement in many small steps and checks
 * the result against the analytically expected position.
 *
 * Step size is chosen to be exactly representable so the expected value is
 * exact: this test is about the coordinate system not drifting, not about
 * floating-point addition in general.
 */
bool UniverseTest_LargeDisplacementAccumulation(FUniverseTestResult& Result)
{
    // 2^38 cm per step: a quarter of a cell, exactly representable.
    const double StepCm = 274877906944.0;
    const int32 StepCount = 100000;

    FUniversePosition P = FUniversePosition::FromCells(0, 0, 0);
    for (int32 Index = 0; Index < StepCount; ++Index)
    {
        P = P.OffsetByCm(FVector3d(StepCm, 0.0, 0.0));
    }

    // 100000 quarter-cells = 25000 cells exactly.
    UVERIFY_EQ_INT(Result, P.CellX, 25000);
    UVERIFY_EQ_DOUBLE_EXACT(Result, P.Local.X, 0.0);

    // Total travelled distance, cross-checked in metres.
    const FUniversePosition Origin;
    const double ExpectedMeters = 25000.0 * UniverseScale::MetersPerCell;
    UVERIFY_NEAR(Result, FUniversePosition::DistanceMeters(Origin, P), ExpectedMeters, 1.0);

    // And travelling the same distance back must land exactly on the origin -
    // no accumulated drift over 200,000 boundary-crossing operations.
    for (int32 Index = 0; Index < StepCount; ++Index)
    {
        P = P.OffsetByCm(FVector3d(-StepCm, 0.0, 0.0));
    }
    UVERIFY_TRUE(Result, P == Origin);

    return Result.Passed();
}

/**
 * The central claim of the architecture: local precision does not degrade with
 * distance from the universe origin.
 *
 * A naive single-double-per-axis world position would have an ULP of ~10^13 cm
 * at these magnitudes - it could not represent a metre, let alone a
 * millimetre. Here the same sub-millimetre step must remain distinguishable
 * ten billion light years out as it is at the origin.
 */
bool UniverseTest_LocalPrecisionAtExtremeCoordinates(FUniverseTestResult& Result)
{
    // ~10 billion light years from the origin on every axis.
    const int64 FarCell = 5000000000000000LL;

    const FUniversePosition Far = FUniversePosition::FromCells(FarCell, -FarCell, FarCell);

    // Confirm the position really is at astronomical distance.
    const double LightYears = FUniversePosition::DistanceLightYears(FUniversePosition(), Far);
    UVERIFY_TRUE(Result, LightYears > 9.0e9);

    // A one-millimetre step (0.1 cm) must be exactly representable and exactly
    // recoverable.
    const double MillimetreCm = 0.1;
    const FUniversePosition Stepped = Far.OffsetByCm(FVector3d(MillimetreCm, 0.0, 0.0));

    UVERIFY_TRUE(Result, Stepped != Far);
    UVERIFY_EQ_INT(Result, Stepped.CellX, Far.CellX);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Stepped.Local.X - Far.Local.X, MillimetreCm);
    UVERIFY_EQ_DOUBLE_EXACT(Result, FUniversePosition::DistanceCm(Far, Stepped), MillimetreCm);

    // Even a 10 micrometre step - four times the worst-case resolution -
    // remains distinguishable out here.
    const double TenMicronsCm = 0.001;
    const FUniversePosition Tiny = Far.OffsetByCm(FVector3d(0.0, TenMicronsCm, 0.0));
    UVERIFY_TRUE(Result, Tiny != Far);
    UVERIFY_TRUE(Result, FUniversePosition::DistanceCm(Far, Tiny) > 0.0);

    // Stepping out and back is exact at extreme coordinates.
    const FUniversePosition Returned = Stepped.OffsetByCm(FVector3d(-MillimetreCm, 0.0, 0.0));
    UVERIFY_TRUE(Result, Returned == Far);

    // For contrast, record what a single-double representation would give:
    // the ULP at this absolute magnitude, in centimetres. The test asserts the
    // comparison is genuinely dramatic rather than marginal.
    const double AbsoluteCm = static_cast<double>(FarCell) * CellD;
    const double NaiveUlpCm = AbsoluteCm * 2.220446049250313e-16;
    UVERIFY_TRUE(Result, NaiveUlpCm > 1.0e5);                     // >1 km of error
    UVERIFY_TRUE(Result, UniverseScale::LocalResolutionCm < 0.001);  // vs. <10 um here

    return Result.Passed();
}

/** Relative vectors, their range limit, and scale-independent distance. */
bool UniverseTest_RelativeAndDistance(FUniverseTestResult& Result)
{
    const FUniversePosition A(10, 20, 30, FVector3d(1000.0, 2000.0, 3000.0));
    const FUniversePosition B(12, 20, 30, FVector3d(1500.0, 2000.0, 3000.0));

    FVector3d Relative;
    UVERIFY_TRUE(Result, FUniversePosition::TryGetRelativeCm(A, B, Relative));
    UVERIFY_EQ_DOUBLE_EXACT(Result, Relative.X, 2.0 * CellD + 500.0);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Relative.Y, 0.0);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Relative.Z, 0.0);

    // Reversing the arguments negates the vector.
    FVector3d Reverse;
    UVERIFY_TRUE(Result, FUniversePosition::TryGetRelativeCm(B, A, Reverse));
    UVERIFY_EQ_DOUBLE_EXACT(Result, Reverse.X, -Relative.X);

    // Distance is symmetric and matches the vector length.
    UVERIFY_EQ_DOUBLE_EXACT(Result,
        FUniversePosition::DistanceCm(A, B), FUniversePosition::DistanceCm(B, A));
    UVERIFY_NEAR(Result, FUniversePosition::DistanceCm(A, B), Relative.Size(), 1.0e-2);

    // A separation beyond the cm-vector limit must be refused rather than
    // silently returning a meaningless number.
    const FUniversePosition VeryFar = FUniversePosition::FromCells(1000000000LL, 0, 0);
    FVector3d Unused;
    UVERIFY_FALSE(Result, FUniversePosition::TryGetRelativeCm(A, VeryFar, Unused));

    // ...but the distance to it is still computable, and enormous.
    const double FarLy = FUniversePosition::DistanceLightYears(A, VeryFar);
    UVERIFY_TRUE(Result, FarLy > 1.0);
    UVERIFY_TRUE(Result, FMath::IsFinite(FarLy));

    // Distance across the whole universe must not overflow to infinity. This
    // is the case that a naive sqrt(dx*dx + ...) in centimetres would fail.
    const FUniversePosition Edge1 = FUniversePosition::FromCells(
        -4000000000000000000LL, -4000000000000000000LL, -4000000000000000000LL);
    const FUniversePosition Edge2 = FUniversePosition::FromCells(
        4000000000000000000LL, 4000000000000000000LL, 4000000000000000000LL);
    const double SpanLy = FUniversePosition::DistanceLightYears(Edge1, Edge2);
    UVERIFY_TRUE(Result, FMath::IsFinite(SpanLy));
    UVERIFY_TRUE(Result, SpanLy > 1.0e13);

    // A known one-light-year separation, checked against the definition.
    const int64 CellsPerLightYear = static_cast<int64>(
        UniverseScale::CmPerLightYear / UniverseScale::CellSizeCmD);
    const FUniversePosition Here = FUniversePosition::FromCells(0, 0, 0);
    const FUniversePosition OneLy = FUniversePosition::FromCells(CellsPerLightYear, 0, 0);
    UVERIFY_NEAR(Result, FUniversePosition::DistanceLightYears(Here, OneLy), 1.0, 1.0e-5);

    return Result.Passed();
}

/** Whole-cell jumps: exact at any magnitude, and refused on overflow. */
bool UniverseTest_CellOffsetJumps(FUniverseTestResult& Result)
{
    const FUniversePosition Start(100, 200, 300, FVector3d(12.5, 25.0, 50.0));

    FUniversePosition Jumped;
    UVERIFY_TRUE(Result, Start.TryOffsetByCells(1000000000000LL, -500000000000LL, 0, Jumped));
    UVERIFY_EQ_INT(Result, Jumped.CellX, 1000000000100LL);
    UVERIFY_EQ_INT(Result, Jumped.CellY, -499999999800LL);
    UVERIFY_EQ_INT(Result, Jumped.CellZ, 300);

    // The local offset is untouched by a cell jump - this is what makes the
    // jump exact regardless of how far it goes.
    UVERIFY_EQ_DOUBLE_EXACT(Result, Jumped.Local.X, 12.5);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Jumped.Local.Y, 25.0);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Jumped.Local.Z, 50.0);

    // Jumping back returns exactly to the start.
    FUniversePosition Back;
    UVERIFY_TRUE(Result, Jumped.TryOffsetByCells(-1000000000000LL, 500000000000LL, 0, Back));
    UVERIFY_TRUE(Result, Back == Start);

    // Overflow is reported, not wrapped.
    const FUniversePosition NearMax = FUniversePosition::FromCells(UniverseScale::MaxCellIndex - 5, 0, 0);
    FUniversePosition Overflowed;
    UVERIFY_FALSE(Result, NearMax.TryOffsetByCells(100, 0, 0, Overflowed));
    UVERIFY_TRUE(Result, NearMax.TryOffsetByCells(5, 0, 0, Overflowed));
    UVERIFY_EQ_INT(Result, Overflowed.CellX, UniverseScale::MaxCellIndex);

    return Result.Passed();
}

/**
 * Sector addressing, including across the origin where truncating division
 * would collapse sectors -1 and 0 into one double-width sector.
 */
bool UniverseTest_SectorAddressing(FUniverseTestResult& Result)
{
    const int64 SectorCells = UniverseScale::SectorSizeInCells;

    int64 SectorX = 0;
    int64 SectorY = 0;
    int64 SectorZ = 0;

    FUniversePosition::FromCells(0, 0, 0).GetSector(SectorX, SectorY, SectorZ);
    UVERIFY_EQ_INT(Result, SectorX, 0);

    FUniversePosition::FromCells(SectorCells - 1, 0, 0).GetSector(SectorX, SectorY, SectorZ);
    UVERIFY_EQ_INT(Result, SectorX, 0);

    FUniversePosition::FromCells(SectorCells, 0, 0).GetSector(SectorX, SectorY, SectorZ);
    UVERIFY_EQ_INT(Result, SectorX, 1);

    // The cell immediately below the origin belongs to sector -1, not 0.
    FUniversePosition::FromCells(-1, 0, 0).GetSector(SectorX, SectorY, SectorZ);
    UVERIFY_EQ_INT(Result, SectorX, -1);

    FUniversePosition::FromCells(-SectorCells, 0, 0).GetSector(SectorX, SectorY, SectorZ);
    UVERIFY_EQ_INT(Result, SectorX, -1);

    FUniversePosition::FromCells(-SectorCells - 1, 0, 0).GetSector(SectorX, SectorY, SectorZ);
    UVERIFY_EQ_INT(Result, SectorX, -2);

    // Round trip: the corner of a sector reports that sector.
    for (int64 Index = -4; Index <= 4; ++Index)
    {
        const FUniversePosition Corner = FUniversePosition::FromSectorCorner(Index, Index * 2, -Index);
        Corner.GetSector(SectorX, SectorY, SectorZ);
        UVERIFY_EQ_INT(Result, SectorX, Index);
        UVERIFY_EQ_INT(Result, SectorY, Index * 2);
        UVERIFY_EQ_INT(Result, SectorZ, -Index);
    }

    return Result.Passed();
}

/** Byte-exact serialisation round trip, including rejection of bad input. */
bool UniverseTest_PositionSerializationRoundTrip(FUniverseTestResult& Result)
{
    const FUniversePosition Cases[] = {
        FUniversePosition(),
        FUniversePosition(1, -2, 3, FVector3d(0.5, 0.25, 0.125)),
        FUniversePosition(-9000000000000000000LL, 9000000000000000000LL, 0,
                          FVector3d(CellD - 1.0, 0.0, 1234.56789)),
        FUniversePosition::FromCells(UniverseScale::MaxCellIndex, UniverseScale::MinCellIndex, 0),
        FUniversePosition(7, 7, 7, FVector3d(1.0e-300, 1.0e-300, CellD * 0.5)),
    };

    for (const FUniversePosition& Original : Cases)
    {
        FUniverseByteWriter Writer;
        Original.Serialize(Writer);

        UVERIFY_EQ_INT(Result, Writer.Num(), FUniversePosition::SerializedSizeBytes);

        FUniverseByteReader Reader(Writer.GetBytes());
        FUniversePosition Restored;
        UVERIFY_TRUE(Result, Restored.Deserialize(Reader));

        // Bit-exact, not approximately equal: the format transports raw IEEE
        // bits precisely so that a saved universe reloads identically.
        UVERIFY_TRUE(Result, Restored == Original);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Restored.Local.X, Original.Local.X);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Restored.Local.Y, Original.Local.Y);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Restored.Local.Z, Original.Local.Z);
        UVERIFY_EQ_UINT(Result, Restored.GetStableHash64(), Original.GetStableHash64());
        UVERIFY_TRUE(Result, Reader.AtEnd());
    }

    // A truncated stream must fail rather than yielding a zeroed position.
    {
        FUniverseByteWriter Writer;
        Cases[1].Serialize(Writer);
        FUniverseByteReader Short(Writer.GetBytes().GetData(), 20);
        FUniversePosition Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Short));
    }

    // A non-canonical local offset must be rejected: Serialize never produces
    // one, so its presence means corruption or tampering.
    {
        FUniverseByteWriter Writer;
        Writer.WriteInt64(0);
        Writer.WriteInt64(0);
        Writer.WriteInt64(0);
        Writer.WriteDouble(CellD * 2.0);   // out of canonical range
        Writer.WriteDouble(0.0);
        Writer.WriteDouble(0.0);
        FUniverseByteReader Reader(Writer.GetBytes());
        FUniversePosition Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Reader));
    }

    // Multiple positions packed back to back must decode in sequence.
    {
        FUniverseByteWriter Writer;
        for (const FUniversePosition& Original : Cases)
        {
            Original.Serialize(Writer);
        }
        FUniverseByteReader Reader(Writer.GetBytes());
        for (const FUniversePosition& Original : Cases)
        {
            FUniversePosition Restored;
            UVERIFY_TRUE(Result, Restored.Deserialize(Reader));
            UVERIFY_TRUE(Result, Restored == Original);
        }
        UVERIFY_TRUE(Result, Reader.AtEnd());
    }

    return Result.Passed();
}

/** Equality and hashing behave as a canonical value type should. */
bool UniverseTest_PositionEqualityAndHash(FUniverseTestResult& Result)
{
    // Two positions reached by different routes must compare equal and hash
    // equal - this is exactly what canonicalisation buys.
    const FUniversePosition Direct(3, 0, 0, FVector3d(500.0, 0.0, 0.0));

    FUniversePosition ViaSteps = FUniversePosition::FromCells(0, 0, 0);
    ViaSteps = ViaSteps.OffsetByCm(FVector3d(CellD * 2.0, 0.0, 0.0));
    ViaSteps = ViaSteps.OffsetByCm(FVector3d(CellD, 0.0, 0.0));
    ViaSteps = ViaSteps.OffsetByCm(FVector3d(500.0, 0.0, 0.0));

    UVERIFY_TRUE(Result, Direct == ViaSteps);
    UVERIFY_EQ_UINT(Result, Direct.GetStableHash64(), ViaSteps.GetStableHash64());
    UVERIFY_EQ_UINT(Result, GetTypeHash(Direct), GetTypeHash(ViaSteps));

    // A position reached by wrapping down from above must also match.
    FUniversePosition FromAbove = FUniversePosition::FromCells(10, 0, 0);
    FromAbove = FromAbove.OffsetByCm(FVector3d(-CellD * 7.0 + 500.0, 0.0, 0.0));
    UVERIFY_TRUE(Result, FromAbove == Direct);

    // Distinct positions must differ, including by a single ULP.
    const FUniversePosition Nudged = Direct.OffsetByCm(FVector3d(0.001, 0.0, 0.0));
    UVERIFY_TRUE(Result, Nudged != Direct);
    UVERIFY_TRUE(Result, Nudged.GetStableHash64() != Direct.GetStableHash64());

    // Neighbouring cells must not collide (the rotation in Combine matters).
    UVERIFY_TRUE(Result,
        FUniversePosition::FromCells(3, 7, 0).GetStableHash64()
        != FUniversePosition::FromCells(7, 3, 0).GetStableHash64());
    UVERIFY_TRUE(Result,
        FUniversePosition::FromCells(1, 0, 0).GetStableHash64()
        != FUniversePosition::FromCells(0, 1, 0).GetStableHash64());

    return Result.Passed();
}
