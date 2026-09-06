// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseHash.h"
#include "UniverseScale.h"

/**
 * UniverseSeed.h
 *
 * The deterministic seed hierarchy. Every generated thing in the universe
 * derives its seed by descending this tree from the single root seed:
 *
 *      Universe
 *        -> Sector          (addressed by integer sector coordinates)
 *          -> Star system   (addressed by index within the sector)
 *            -> Body        (addressed by index within the system)
 *              -> Surface   (addressed by cube face / quadtree node - Sprint 2+)
 *
 * Two properties make this work, and both are structural rather than
 * incidental:
 *
 * 1. Descent is pure. A child seed is a function of (parent seed, domain tag,
 *    integer address) and nothing else. There is no traversal state, no
 *    counter, no allocation order. That is why a sector can be generated in
 *    isolation, on any thread, in any order, having never generated its
 *    neighbours - which is the whole reason the universe does not need a
 *    database.
 *
 * 2. Every level is domain-tagged. Without a tag, Sector(s, 1, 0, 0) and
 *    System(s, 1, 0, 0) would be the same hash of the same numbers, and the
 *    structure of the universe would show visible correlations between
 *    unrelated levels. The tags below are arbitrary but FROZEN: changing one
 *    regenerates the universe.
 */

/**
 * Domain tags for seed derivation. Values are frozen; append only.
 *
 * The constants are 64-bit ASCII tags rather than small integers so that a
 * seed appearing in a log or a save file can be traced back to the level that
 * produced it, and so that an accidentally reused value is obvious on sight.
 */
namespace UniverseSeedDomain
{
    inline constexpr uint64 Universe = 0x554E495645525345ull;  // "UNIVERSE"
    inline constexpr uint64 Sector   = 0x534543544F520001ull;  // "SECTOR"
    inline constexpr uint64 System   = 0x53595354454D0001ull;  // "SYSTEM"
    inline constexpr uint64 Body     = 0x424F445900000001ull;  // "BODY"
    inline constexpr uint64 Surface  = 0x5355524641434501ull;  // "SURFACE"   (Sprint 2+)
    inline constexpr uint64 Feature  = 0x4645415455524501ull;  // "FEATURE"   (Sprint 2+)

    /**
     * Sub-streams within a single level. A generator that needs several
     * independent random sequences for one object (mass vs. orbit vs. name)
     * derives one stream per aspect instead of drawing repeatedly from a
     * shared generator, so adding a new aspect later cannot shift the values
     * of the existing ones.
     */
    inline constexpr uint64 StreamPrimary  = 0x0000000000000001ull;
    inline constexpr uint64 StreamName     = 0x0000000000000002ull;
    inline constexpr uint64 StreamPhysical = 0x0000000000000003ull;
    inline constexpr uint64 StreamOrbital  = 0x0000000000000004ull;
    inline constexpr uint64 StreamSurface  = 0x0000000000000005ull;

    // Sprint 004. Appended rather than inserted: adding a stream must not
    // shift the value of any existing one, or every planet already generated
    // changes shape.
    inline constexpr uint64 StreamEnvironment = 0x0000000000000006ull;
    inline constexpr uint64 StreamWeather     = 0x0000000000000007ull;
    inline constexpr uint64 StreamVegetation  = 0x0000000000000008ull;
}

/**
 * FUniverseSeed
 *
 * A 64-bit seed at some level of the hierarchy. A distinct type rather than a
 * bare uint64 so that a sector seed cannot be silently passed where a system
 * seed is expected - a mistake that produces a plausible-looking but wrong
 * universe and is otherwise almost impossible to spot.
 */
struct UNIVERSECORE_API FUniverseSeed
{
    /**
     * The derived 64-bit seed value.
     *
     * Deliberately NOT a USTRUCT/UPROPERTY: UnrealHeaderTool has no uint64
     * property type, and more importantly the seed hierarchy is pure
     * mathematics that must stay independent of UObject reflection so it can
     * run on any thread, in a commandlet, or in the standalone test harness.
     * Reflection-exposed data lives in the game module instead.
     */
    uint64 Value = 0;

    FUniverseSeed() = default;
    explicit FUniverseSeed(uint64 InValue) : Value(InValue) {}

    bool operator==(const FUniverseSeed& Other) const { return Value == Other.Value; }
    bool operator!=(const FUniverseSeed& Other) const { return Value != Other.Value; }

    /** Derives an independent sub-stream seed for one aspect of this object. */
    FUniverseSeed Stream(uint64 StreamId) const
    {
        return FUniverseSeed(UniverseHash::Hash(Value, StreamId));
    }

    FString ToDebugString() const;
};

inline uint32 GetTypeHash(const FUniverseSeed& Seed)
{
    return static_cast<uint32>(Seed.Value ^ (Seed.Value >> 32));
}

/**
 * FUniverseSeedHierarchy
 *
 * Stateless seed derivation. Every function is a pure function of its
 * arguments; the struct exists only to namespace them and to hold the root
 * seed for convenience.
 */
struct UNIVERSECORE_API FUniverseSeedHierarchy
{
public:
    FUniverseSeedHierarchy() = default;

    explicit FUniverseSeedHierarchy(uint64 InUniverseSeed)
        : UniverseSeedValue(UniverseHash::Hash(InUniverseSeed, UniverseSeedDomain::Universe))
    {
    }

    /**
     * Builds a hierarchy from a human-readable seed phrase.
     * Empty text falls back to a fixed default so that a missing config value
     * produces a known universe rather than an accidental one.
     */
    static FUniverseSeedHierarchy FromText(const TCHAR* SeedText);

    /** The root seed, already domain-tagged. */
    FUniverseSeed GetUniverseSeed() const { return FUniverseSeed(UniverseSeedValue); }

    /** Raw root value as supplied/derived, for display. */
    uint64 GetUniverseSeedValue() const { return UniverseSeedValue; }

    /** Seed for the sector at integer sector coordinates. */
    FUniverseSeed GetSectorSeed(int64 SectorX, int64 SectorY, int64 SectorZ) const
    {
        return FUniverseSeed(UniverseHash::Hash(
            UniverseSeedValue, UniverseSeedDomain::Sector, SectorX, SectorY, SectorZ));
    }

    /** Seed for the SystemIndex'th system within a sector. */
    static FUniverseSeed GetSystemSeed(const FUniverseSeed& SectorSeed, int32 SystemIndex)
    {
        return FUniverseSeed(UniverseHash::Hash(
            SectorSeed.Value, UniverseSeedDomain::System, SystemIndex));
    }

    /** Seed for the BodyIndex'th body within a system. Index 0 is the star. */
    static FUniverseSeed GetBodySeed(const FUniverseSeed& SystemSeed, int32 BodyIndex)
    {
        return FUniverseSeed(UniverseHash::Hash(
            SystemSeed.Value, UniverseSeedDomain::Body, BodyIndex));
    }

    /**
     * Seed for a cube-sphere surface patch. Present now so the hierarchy is
     * complete and testable; consumed in Sprint 002 when planet terrain lands.
     * Face is 0-5, Level is the quadtree depth, (U, V) the node index at that
     * depth.
     */
    static FUniverseSeed GetSurfacePatchSeed(const FUniverseSeed& BodySeed, int32 Face, int32 Level, int64 U, int64 V)
    {
        return FUniverseSeed(UniverseHash::Hash(
            BodySeed.Value, UniverseSeedDomain::Surface, Face, Level, U, V));
    }

    /**
     * Convenience: sector seed for whichever sector contains the given cell.
     */
    FUniverseSeed GetSectorSeedForCell(int64 CellX, int64 CellY, int64 CellZ) const
    {
        return GetSectorSeed(
            UniverseScale::FloorDivPow2(CellX, UniverseScale::SectorShiftInCells),
            UniverseScale::FloorDivPow2(CellY, UniverseScale::SectorShiftInCells),
            UniverseScale::FloorDivPow2(CellZ, UniverseScale::SectorShiftInCells));
    }

private:
    /** Domain-tagged root. Never the raw user value. */
    uint64 UniverseSeedValue = 0;
};
