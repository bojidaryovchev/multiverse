// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseScale.h"
#include "UniverseSerialization.h"

/**
 * FUniversePosition
 *
 * The canonical address of a point in the universe. Not an Actor transform,
 * not an FVector, and never converted to one except relative to a nearby
 * reference point.
 *
 * Representation: an integer cell index per axis plus a double-precision
 * offset inside that cell, in centimetres.
 *
 *      absolute_cm = Cell * 2^40 + Local        (Local in [0, 2^40))
 *
 * The canonical form is unique: Local is always normalised into the half-open
 * range [0, CellSizeCm). Uniqueness is what makes equality, hashing and
 * byte-level serialisation well defined, and it is the reason Normalize() is
 * applied eagerly rather than lazily.
 *
 * Precision does not depend on distance from the universe origin. A position
 * ten billion light years out has exactly the same 2.44 um local resolution as
 * one at the origin, because the large magnitude lives entirely in the integer
 * part where it costs nothing.
 *
 * Sign convention: floor semantics throughout. Cell -1 covers absolute
 * coordinates [-2^40, 0), so cells tile the axis without a gap or a
 * double-width cell straddling zero - the bug that truncation-toward-zero
 * would introduce.
 *
 * Deliberately a plain struct, not a USTRUCT.
 *
 * Reflection would buy Blueprint exposure and UStruct-based replication, and
 * costs more than both are worth here: it drags UnrealHeaderTool and
 * CoreUObject into the one module that has to stay compilable without an
 * engine, and it would force the .generated.h include to be unconditional -
 * which the standalone verification build cannot satisfy.
 *
 * Neither capability is actually lost. Nothing exposes a raw position to
 * Blueprint (the HUD and pawn expose formatted strings and doubles), and
 * replication will use the explicit 48-byte format below, which is a better
 * wire representation than UStruct serialisation would give us anyway.
 */
struct UNIVERSECORE_API FUniversePosition
{
    /** Cell index along X. One cell is UniverseScale::CellSizeCm centimetres. */
    int64 CellX = 0;
    int64 CellY = 0;
    int64 CellZ = 0;

    /** Offset within the cell, centimetres, canonically in [0, CellSizeCm). */
    FVector3d Local = FVector3d::ZeroVector;

    // -----------------------------------------------------------------------
    // Construction
    // -----------------------------------------------------------------------

    FUniversePosition() = default;

    FUniversePosition(int64 InCellX, int64 InCellY, int64 InCellZ, const FVector3d& InLocal)
        : CellX(InCellX), CellY(InCellY), CellZ(InCellZ), Local(InLocal)
    {
        Normalize();
    }

    /** A position at the corner of the given cell. */
    static FUniversePosition FromCells(int64 InCellX, int64 InCellY, int64 InCellZ)
    {
        FUniversePosition Result;
        Result.CellX = InCellX;
        Result.CellY = InCellY;
        Result.CellZ = InCellZ;
        return Result;
    }

    /**
     * A position expressed as centimetres from the universe origin.
     * Only meaningful for |value| below 2^53 cm (~6000 AU); beyond that the
     * double argument itself cannot carry centimetre resolution and the caller
     * should be building the position from cells instead.
     */
    static FUniversePosition FromOriginCm(const FVector3d& AbsoluteCm)
    {
        FUniversePosition Result;
        Result.Local = AbsoluteCm;
        Result.Normalize();
        return Result;
    }

    /** As FromOriginCm, in metres. */
    static FUniversePosition FromOriginMeters(const FVector3d& AbsoluteMeters)
    {
        return FromOriginCm(AbsoluteMeters * UniverseScale::CmPerMeter);
    }

    /** Cell corner plus a local offset, without the FromOriginCm range limit. */
    static FUniversePosition FromCellsAndLocal(int64 InCellX, int64 InCellY, int64 InCellZ, const FVector3d& InLocalCm)
    {
        return FUniversePosition(InCellX, InCellY, InCellZ, InLocalCm);
    }

    // -----------------------------------------------------------------------
    // Normalisation
    // -----------------------------------------------------------------------

    /**
     * Folds Local back into [0, CellSizeCm), moving the excess into the cell
     * indices. Exact for |Local| <= 2^53 cm; see the .cpp for the argument and
     * for the saturation behaviour beyond that.
     */
    void Normalize();

    /** True if Local is already in canonical range on every axis. */
    bool IsNormalized() const;

    // -----------------------------------------------------------------------
    // Displacement
    // -----------------------------------------------------------------------

    /**
     * This position moved by DeltaCm centimetres. The result is normalised, so
     * repeated small steps cross cell boundaries automatically and no caller
     * ever has to think about boundaries.
     */
    FUniversePosition OffsetByCm(const FVector3d& DeltaCm) const;

    /** This position moved by DeltaMeters metres. */
    FUniversePosition OffsetByMeters(const FVector3d& DeltaMeters) const
    {
        return OffsetByCm(DeltaMeters * UniverseScale::CmPerMeter);
    }

    /**
     * This position moved by whole cells. Pure integer arithmetic, therefore
     * exact at any magnitude - this is the correct way to express a jump too
     * large for OffsetByCm (warp, system-to-system travel, map teleports).
     * Returns false without modifying Out if a cell index would overflow.
     */
    bool TryOffsetByCells(int64 DeltaCellX, int64 DeltaCellY, int64 DeltaCellZ, FUniversePosition& Out) const;

    // -----------------------------------------------------------------------
    // Relative geometry
    // -----------------------------------------------------------------------

    /**
     * The vector From -> To in centimetres.
     *
     * Returns false when the separation exceeds MaxCm (default 2^53 cm, where
     * a double's ULP reaches one centimetre) or when the cell difference
     * overflows int64. Callers that only need a magnitude should use
     * DistanceMeters, which has no such limit.
     *
     * This is the only sanctioned route from universe space to Unreal space,
     * and the range check is the guardrail that stops anyone quietly handing
     * Unreal a 1e20 cm transform.
     */
    static bool TryGetRelativeCm(
        const FUniversePosition& From,
        const FUniversePosition& To,
        FVector3d& OutCm,
        double MaxCm = UniverseScale::MaxRelativeVectorCm);

    /**
     * The vector From -> To in cell units (fractional). Always representable:
     * cell differences are taken in double, so nothing overflows, and the
     * result keeps full relative precision at any separation. This is the
     * basis for distance and direction at astronomical scale.
     */
    static FVector3d GetRelativeCells(const FUniversePosition& From, const FUniversePosition& To);

    /**
     * Separation in centimetres. Computed in cell space and scaled at the end,
     * so it neither overflows nor loses relative precision no matter how far
     * apart the two positions are.
     */
    static double DistanceCm(const FUniversePosition& A, const FUniversePosition& B);

    static double DistanceMeters(const FUniversePosition& A, const FUniversePosition& B)
    {
        return DistanceCm(A, B) * UniverseScale::MetersPerCm;
    }

    static double DistanceAu(const FUniversePosition& A, const FUniversePosition& B)
    {
        return DistanceMeters(A, B) / UniverseScale::MetersPerAu;
    }

    static double DistanceLightYears(const FUniversePosition& A, const FUniversePosition& B)
    {
        return DistanceMeters(A, B) / UniverseScale::MetersPerLightYear;
    }

    /** Unit vector From -> To. Zero vector if the two coincide. */
    static FVector3d DirectionUnit(const FUniversePosition& From, const FUniversePosition& To)
    {
        return GetRelativeCells(From, To).GetSafeNormal();
    }

    // -----------------------------------------------------------------------
    // Sector addressing
    // -----------------------------------------------------------------------

    /** The sector containing this position (floor division of the cell index). */
    void GetSector(int64& OutSectorX, int64& OutSectorY, int64& OutSectorZ) const
    {
        OutSectorX = UniverseScale::FloorDivPow2(CellX, UniverseScale::SectorShiftInCells);
        OutSectorY = UniverseScale::FloorDivPow2(CellY, UniverseScale::SectorShiftInCells);
        OutSectorZ = UniverseScale::FloorDivPow2(CellZ, UniverseScale::SectorShiftInCells);
    }

    /** The position of a sector's minimum corner. */
    static FUniversePosition FromSectorCorner(int64 SectorX, int64 SectorY, int64 SectorZ)
    {
        return FromCells(
            SectorX * UniverseScale::SectorSizeInCells,
            SectorY * UniverseScale::SectorSizeInCells,
            SectorZ * UniverseScale::SectorSizeInCells);
    }

    // -----------------------------------------------------------------------
    // Identity
    // -----------------------------------------------------------------------

    /**
     * Exact equality of the canonical form. Bit-exact on the local part by
     * design: this is an identity test for a canonical value, not a
     * "close enough" spatial test. Use DistanceMeters with a tolerance when
     * proximity is what you mean.
     */
    bool operator==(const FUniversePosition& Other) const
    {
        return CellX == Other.CellX
            && CellY == Other.CellY
            && CellZ == Other.CellZ
            && Local == Other.Local;
    }

    bool operator!=(const FUniversePosition& Other) const { return !(*this == Other); }

    /** True if the two positions are within ToleranceCm of each other. */
    static bool IsNearlyEqual(const FUniversePosition& A, const FUniversePosition& B, double ToleranceCm)
    {
        return DistanceCm(A, B) <= ToleranceCm;
    }

    /**
     * Stable 64-bit hash of the canonical form. Deterministic across runs,
     * builds and machines - it depends only on the stored bits, never on
     * addresses or engine object IDs - so it is safe to use as a persistence
     * key or a network identifier.
     */
    uint64 GetStableHash64() const;

    // -----------------------------------------------------------------------
    // Serialisation
    // -----------------------------------------------------------------------

    /** 8+8+8+8+8+8 = 48 bytes, fixed layout, little-endian. */
    static constexpr int32 SerializedSizeBytes = 48;

    void Serialize(FUniverseByteWriter& Writer) const;

    /**
     * Reads a position written by Serialize. Returns false if the stream was
     * truncated or the decoded local offset is not canonical (which would mean
     * a corrupt or hostile stream, since Serialize only ever emits canonical
     * values).
     */
    bool Deserialize(FUniverseByteReader& Reader);

    // -----------------------------------------------------------------------
    // Debug
    // -----------------------------------------------------------------------

    /** Multi-value form for HUDs: cells, local cm and derived astronomy. */
    FString ToDebugString() const;

    /** Single-line compact form for logs. */
    FString ToCompactString() const;
};

/** Hash for TMap/TSet keys. Folds the stable 64-bit hash into Unreal's 32. */
inline uint32 GetTypeHash(const FUniversePosition& Position)
{
    const uint64 Hash64 = Position.GetStableHash64();
    return static_cast<uint32>(Hash64 ^ (Hash64 >> 32));
}

#if !(defined(UNIVERSE_STANDALONE) && UNIVERSE_STANDALONE)
/**
 * FArchive support, implemented on top of the explicit byte format so the
 * engine path and the network/persistence path can never diverge.
 */
UNIVERSECORE_API FArchive& operator<<(FArchive& Ar, FUniversePosition& Position);
#endif
