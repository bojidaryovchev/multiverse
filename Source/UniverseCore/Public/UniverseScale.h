// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"

/**
 * UniverseScale.h
 *
 * The numeric constants that define the universe. Every value here was chosen
 * by analysis, not convenience; the reasoning lives in
 * Docs/Architecture/UniverseCoordinates.md and Docs/ADR/ADR-001.
 *
 * Summary of the two decisions that matter:
 *
 *  - Cell size is 2^40 centimetres. A power of two makes cell <-> local
 *    conversion exact in IEEE-754 double: multiplying by 2^-40 only adjusts
 *    the exponent, so floor() and the subtraction that follows introduce no
 *    rounding at all in the operating range. A decimal cell size (1e9 cm, say)
 *    would make every normalisation a lossy operation, and lossy normalisation
 *    is fatal to determinism because the result depends on the path taken to
 *    reach a position rather than on the position itself.
 *
 *  - Cells are indexed by int64. 2^63 cells x 2^40 cm = 2^103 cm of half-extent
 *    per axis, which is about 1.07e13 light years - roughly 230x the radius of
 *    the observable universe. The logical universe is therefore not the
 *    limiting factor for anything we will ever build.
 *
 * Worst-case local precision is the ULP of a double at the top of the cell:
 * 2^40 * 2^-52 = 2^-12 cm = 2.44 micrometres, everywhere in the universe,
 * because local coordinates never grow with distance from the origin.
 */
namespace UniverseScale
{
    // -----------------------------------------------------------------------
    // Cell geometry
    // -----------------------------------------------------------------------

    /** log2 of the cell edge length in centimetres. */
    inline constexpr int32 CellShift = 40;

    /**
     * Extreme cell indices. Spelled out here rather than using INT64_MAX so
     * the core does not depend on which limits header a given build happens
     * to pull in (Unreal and the standalone harness differ).
     */
    inline constexpr int64 MaxCellIndex =  9223372036854775807LL;
    inline constexpr int64 MinCellIndex = -9223372036854775807LL - 1LL;

    /** Cell edge length in centimetres: 1,099,511,627,776 cm. */
    inline constexpr int64 CellSizeCm = static_cast<int64>(1) << CellShift;

    /** Same value as a double. Exactly representable (it is a power of two). */
    inline constexpr double CellSizeCmD = 1099511627776.0;

    /**
     * 2^-40. Exactly representable, so Local * InvCellSizeCmD is an exact
     * operation - it only decrements the exponent. This is why normalisation
     * multiplies by the reciprocal instead of dividing.
     */
    inline constexpr double InvCellSizeCmD = 1.0 / CellSizeCmD;

    // -----------------------------------------------------------------------
    // Unit conversion. Unreal's unit is the centimetre and we keep it, so that
    // local coordinates hand straight to Actor transforms with no scaling.
    // -----------------------------------------------------------------------

    inline constexpr double CmPerMeter        = 100.0;
    inline constexpr double MetersPerCm       = 0.01;
    inline constexpr double MetersPerCell     = CellSizeCmD * MetersPerCm;      // 1.0995e10 m
    inline constexpr double KilometresPerCell = MetersPerCell * 0.001;          // 1.0995e7 km

    /** IAU astronomical unit, metres (exact by definition). */
    inline constexpr double MetersPerAu = 149597870700.0;
    /** Julian light year, metres (exact by definition). */
    inline constexpr double MetersPerLightYear = 9460730472580800.0;
    /** Parsec, metres. */
    inline constexpr double MetersPerParsec = 3.0856775814913673e16;

    inline constexpr double CmPerAu        = MetersPerAu * CmPerMeter;
    inline constexpr double CmPerLightYear = MetersPerLightYear * CmPerMeter;

    /** Cell edge expressed in AU: about 0.0735 AU (~11 million km). */
    inline constexpr double AuPerCell = MetersPerCell / MetersPerAu;

    // -----------------------------------------------------------------------
    // Sector grid - the coarse partition used for star placement.
    //
    // A sector is 2^22 cells on a side = 2^62 cm = 4.875 light years. At the
    // solar-neighbourhood stellar density of ~0.004 stars/ly^3 that is ~0.46
    // expected stars per sector, so a sector holding zero or one system is the
    // natural granularity and star lookups stay cheap.
    // -----------------------------------------------------------------------

    inline constexpr int32 SectorShiftInCells = 22;
    inline constexpr int64 SectorSizeInCells  = static_cast<int64>(1) << SectorShiftInCells;
    inline constexpr double SectorSizeCmD     = CellSizeCmD * static_cast<double>(SectorSizeInCells);
    inline constexpr double SectorSizeLightYears = SectorSizeCmD / CmPerLightYear;  // ~4.875 ly

    // -----------------------------------------------------------------------
    // Precision limits
    // -----------------------------------------------------------------------

    /**
     * Worst-case spacing between representable local coordinates, in cm.
     * 2^CellShift * 2^-52. This is the resolution of the universe, and it is
     * constant everywhere rather than degrading with distance.
     */
    inline constexpr double LocalResolutionCm = CellSizeCmD * 2.220446049250313e-16;  // ~2.44e-4 cm

    /**
     * Largest displacement, in cm, that Normalize() handles with provably
     * exact arithmetic: 2^53 cm ~ 9.0e15 cm ~ 6013 AU. Any single frame's
     * movement is many orders of magnitude below this even at warp speeds;
     * jumps larger than this must be expressed as whole-cell offsets, which
     * are pure integer arithmetic and therefore always exact.
     */
    inline constexpr double MaxExactDisplacementCm = 9007199254740992.0;  // 2^53

    /**
     * Default limit for expressing a separation as a single cm-space vector.
     * At 2^53 cm the ULP of a double is exactly 1 cm, so beyond this a
     * centimetre-denominated relative vector stops carrying centimetre
     * meaning. Callers wanting greater separations use cell-space deltas.
     */
    inline constexpr double MaxRelativeVectorCm = MaxExactDisplacementCm;

    // -----------------------------------------------------------------------
    // Rendering
    // -----------------------------------------------------------------------

    /**
     * Distance (cm) the tracked viewpoint may drift from the Unreal origin
     * before the render frame is rebased. This is deliberately far smaller
     * than a cell: it is bounded by float precision in the rendering and
     * physics paths, not by the double precision of the canonical position.
     * At 1e6 cm (10 km) a float ULP is ~0.06 cm, which is below the visible
     * threshold for vertex jitter and Z-fighting.
     *
     * Cell size and rebase radius are independent knobs and must stay that
     * way: one bounds the logical universe, the other bounds render error.
     */
    inline constexpr double DefaultRebaseRadiusCm = 1.0e6;

    // -----------------------------------------------------------------------
    // Checked integer arithmetic.
    //
    // Cell indices are int64 and signed overflow is undefined behaviour, so
    // every cell add/subtract that could wrap goes through these.
    // -----------------------------------------------------------------------

    /** A + B, returning false (and leaving Out untouched) on signed overflow. */
    inline bool AddChecked(int64 A, int64 B, int64& Out)
    {
        const uint64 UA = static_cast<uint64>(A);
        const uint64 UB = static_cast<uint64>(B);
        const uint64 UR = UA + UB;
        // Overflow iff the operands share a sign and the result differs from it.
        if ((~(UA ^ UB) & (UA ^ UR)) >> 63)
        {
            return false;
        }
        Out = static_cast<int64>(UR);
        return true;
    }

    /** A - B, returning false (and leaving Out untouched) on signed overflow. */
    inline bool SubtractChecked(int64 A, int64 B, int64& Out)
    {
        const uint64 UA = static_cast<uint64>(A);
        const uint64 UB = static_cast<uint64>(B);
        const uint64 UR = UA - UB;
        // Overflow iff the operands differ in sign and the result differs from A.
        if (((UA ^ UB) & (UA ^ UR)) >> 63)
        {
            return false;
        }
        Out = static_cast<int64>(UR);
        return true;
    }

    /**
     * Arithmetic (floor) division of a cell index by a positive power-of-two
     * divisor. Plain C++ integer division truncates toward zero, which would
     * put cells -1 and 0 in the same sector and break the grid across the
     * origin. An arithmetic right shift floors for both signs.
     */
    inline int64 FloorDivPow2(int64 Value, int32 Shift)
    {
        return Value >> Shift;  // guaranteed arithmetic for signed types since C++20
    }

    /** Non-negative remainder of Value modulo 2^Shift. */
    inline int64 ModPow2(int64 Value, int32 Shift)
    {
        return Value & ((static_cast<int64>(1) << Shift) - 1);
    }
}
