// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseCoordinates.h"
#include "UniverseSeed.h"
#include "UniverseSerialization.h"

/**
 * GalaxyDescriptor.h
 *
 * The layer above star systems: where stars are *likely* to be at all.
 *
 *
 * WHAT THIS CHANGES
 *
 * Sprint 001 put 0.47 star systems in every sector of the universe, uniformly,
 * forever. That is a defensible placeholder and it is also wrong in a way that
 * matters: a universe with uniform stellar density has no structure, no
 * landmarks, nowhere that is empty and nowhere that is crowded, and no reason
 * for one direction to be different from another.
 *
 * A galaxy is a *density function*. It does not contain a list of stars - it
 * says how likely a star is at a place - and sector generation multiplies its
 * own count by that likelihood. Outside every galaxy the density is zero and
 * sectors are empty, which is what makes intergalactic space a real place
 * rather than a thinner version of the same thing.
 *
 *
 * THE HIERARCHY
 *
 *     Universe
 *       -> Intergalactic cell   2^40 universe cells, about 1.28 million ly
 *          -> Galaxy            tens of thousands of ly across
 *             -> Sector         4.87 ly, from Sprint 001, unchanged
 *                -> System
 *                   -> Planet
 *
 * Every level is addressed by integers derived from position, and nothing at
 * any level is stored. An intergalactic cell containing no galaxy costs
 * nothing, and there are 10^21 of them.
 *
 *
 * A NOTE ON TRANSCENDENTALS
 *
 * The spiral arm term uses `Atan2` and `Sin`, which puts galaxy density on the
 * same cross-platform footing as Sprint 001's star generation - the open libm
 * risk ADR-002 records. That is a deliberate choice rather than an oversight:
 * the alternative formulations that avoid trigonometry do not produce spiral
 * arms, and if cross-platform determinism is ever required the fix is a
 * project-owned libm, which solves star generation and galaxy density together.
 * It is one problem, not two.
 *
 * The terrain and climate paths remain transcendental-free, and that is where
 * it matters most - those decide ground somebody has built on.
 */

/** Galaxy generation version. Bumping it rearranges every star in existence. */
namespace GalaxyGeneratorVersion
{
    inline constexpr uint32 Current = 1;
}

/** Broad shape. The density function branches on it. */
enum class EGalaxyType : uint8
{
    /** A flattened disk with a central bulge and spiral arms. */
    Spiral = 0,

    /** A smooth ellipsoid, denser toward the centre, no arms. */
    Elliptical = 1,

    /** Clumpy and asymmetric, with no organised structure. */
    Irregular = 2,
};

UNIVERSEGENERATION_API const TCHAR* LexToString(EGalaxyType Type);

/**
 * A galaxy's address.
 *
 * Cell coordinates plus an index within the cell, exactly like
 * FUniverseSystemId's relationship to a sector. Derived from position, stable
 * across runs and machines, and never stored.
 */
struct UNIVERSEGENERATION_API FGalaxyId
{
    int64 CellX = 0;
    int64 CellY = 0;
    int64 CellZ = 0;
    int32 Index = 0;

    /** Stable 64-bit hash of the address. */
    uint64 Hash = 0;

    FGalaxyId() = default;
    FGalaxyId(int64 InX, int64 InY, int64 InZ, int32 InIndex);

    bool IsValid() const { return Hash != 0; }

    bool operator==(const FGalaxyId& Other) const
    {
        return CellX == Other.CellX && CellY == Other.CellY
            && CellZ == Other.CellZ && Index == Other.Index;
    }

    bool operator!=(const FGalaxyId& Other) const { return !(*this == Other); }

    FString ToDebugString() const;
};

FORCEINLINE uint32 GetTypeHash(const FGalaxyId& Id)
{
    return static_cast<uint32>(Id.Hash ^ (Id.Hash >> 32));
}

/**
 * One galaxy.
 *
 * Plain data, produced entirely from (universe seed, cell address, index).
 * Reconstructible, never stored.
 */
struct UNIVERSEGENERATION_API FGalaxyDescriptor
{
    FGalaxyId Id;
    FUniverseSeed Seed;

    EGalaxyType Type = EGalaxyType::Spiral;

    /** Canonical position of the galactic centre. */
    FUniversePosition Position;

    /**
     * Radius of the disk, in light years, beyond which density is zero.
     *
     * The Milky Way's disk is about 50,000 ly in radius. Generated galaxies
     * range either side of that.
     */
    double RadiusLightYears = 50000.0;

    /** Half-thickness of the disk at the centre, in light years. */
    double DiskThicknessLightYears = 1000.0;

    /** Radius of the central bulge, in light years. */
    double BulgeRadiusLightYears = 5000.0;

    /**
     * Orientation, as an orthonormal basis in universe axes.
     *
     * Stored as two vectors rather than a quaternion because that is what the
     * density function needs - it projects a position onto the disk plane and
     * measures its height above it - and because a basis can be checked for
     * orthonormality by inspection, which a quaternion cannot.
     */
    FVector3d DiskNormal = FVector3d(0.0, 0.0, 1.0);
    FVector3d DiskRight = FVector3d(1.0, 0.0, 0.0);

    /** Number of spiral arms. Zero for elliptical and irregular. */
    int32 ArmCount = 2;

    /** How tightly the arms wind. Larger is tighter. */
    double ArmWindingTightness = 0.25;

    /** How much the arms modulate density, in [0, 1]. */
    double ArmContrast = 0.6;

    /**
     * Peak stellar density multiplier at the galactic centre.
     *
     * Sector generation multiplies its own baseline count by the density at
     * that sector, so a value of 1 reproduces Sprint 001's uniform density at
     * the very centre and less everywhere else.
     */
    double CoreDensity = 1.0;

    /** Rough number of stars, for display. Never used to generate anything. */
    double StarCountEstimate = 1.0e11;

    uint32 GenerationVersion = GalaxyGeneratorVersion::Current;

    FString Name;

    bool IsValid() const { return Id.IsValid() && RadiusLightYears > 0.0; }

    /** The third basis vector, completing the frame. */
    FVector3d GetDiskForward() const;

    FString ToDebugString() const;
};

/**
 * A position expressed relative to a galaxy.
 *
 * Light years rather than metres, and doubles rather than the integer-plus-
 * offset scheme, because at galactic scale a double has about a
 * centimetre of resolution at 50,000 ly - vastly more than anything at this
 * layer needs - and because every consumer wants to do arithmetic on it.
 *
 * Canonical position remains FUniversePosition. This is a *view* of it.
 */
struct UNIVERSEGENERATION_API FGalaxyLocalPosition
{
    /** Distance from the galactic centre along the disk plane, in ly. */
    double RadiusLightYears = 0.0;

    /** Height above the disk plane, in ly. Signed. */
    double HeightLightYears = 0.0;

    /** Angle around the disk from the galaxy's right vector, in radians. */
    double AngleRadians = 0.0;

    /** Full offset from the galactic centre, in ly, in universe axes. */
    FVector3d OffsetLightYears = FVector3d::ZeroVector;

    /** Straight-line distance from the centre, in ly. */
    double GetDistanceLightYears() const { return OffsetLightYears.Size(); }
};

class UNIVERSEGENERATION_API FGalaxyGenerator
{
public:
    /**
     * Size of an intergalactic cell, as a power-of-two count of universe cells.
     *
     * 2^40 universe cells is about 1.28 million light years - roughly half the
     * distance to Andromeda. Power of two for the same reason the sector shift
     * is: floor division is then exact, so a position never lands in the wrong
     * cell because of a rounding error at the boundary.
     */
    static constexpr int32 CellShiftInCells = 40;

    /** Cell size in light years, for display and for search radii. */
    static double GetCellSizeLightYears();

    /** Which intergalactic cell a universe position is in. */
    static void GetCellCoordinates(
        const FUniversePosition& Position,
        int64& OutCellX, int64& OutCellY, int64& OutCellZ);

    /**
     * How many galaxies are in a cell.
     *
     * Real galaxy density is about one per 3,000 cubic megaparsecs, which over
     * a cell this size is roughly one in a hundred and sixty. That is correct
     * and it makes finding a second galaxy a search over thousands of cells.
     *
     * The generated density is higher - an expectation of about 0.15 per cell,
     * so neighbours are a few million light years apart. That is a deliberate
     * departure from reality in favour of a universe that can be navigated,
     * and it is recorded here rather than buried: the number is a gameplay
     * decision, not an astronomical one.
     */
    static int32 GetGalaxyCountInCell(
        const FUniverseSeedHierarchy& Hierarchy,
        int64 CellX, int64 CellY, int64 CellZ);

    /** The stable identity of a galaxy, without generating it. */
    static FGalaxyId MakeGalaxyId(int64 CellX, int64 CellY, int64 CellZ, int32 Index);

    /** Where a galaxy's centre is, without generating the rest of it. */
    static FUniversePosition GetGalaxyPosition(
        const FUniverseSeedHierarchy& Hierarchy,
        int64 CellX, int64 CellY, int64 CellZ, int32 Index);

    /** Generates a galaxy. Returns false if that index does not exist. */
    static bool GenerateGalaxy(
        const FUniverseSeedHierarchy& Hierarchy,
        int64 CellX, int64 CellY, int64 CellZ, int32 Index,
        FGalaxyDescriptor& OutGalaxy);

    /** Every galaxy whose centre is within a radius of a point. */
    static void FindGalaxiesWithin(
        const FUniverseSeedHierarchy& Hierarchy,
        const FUniversePosition& Centre,
        double RadiusLightYears,
        TArray<FGalaxyDescriptor>& OutGalaxies,
        int32 MaxResults = 32);

    /**
     * The nearest galaxy to a point, searching outward in cell shells.
     *
     * Bounded: intergalactic space is mostly empty, and an unbounded outward
     * search from a genuinely isolated point would run until it ran out of
     * int64. Returns false rather than searching forever, and the caller
     * decides what an answerless universe means.
     */
    static bool FindNearestGalaxy(
        const FUniverseSeedHierarchy& Hierarchy,
        const FUniversePosition& Centre,
        FGalaxyDescriptor& OutGalaxy,
        int32 MaxCellShells = 6);

    /**
     * A position well inside a galaxy, for anything that needs somewhere with
     * stars in it: the game mode's starting search, and the generation tests.
     *
     * Half way out along the disk rather than at the core - the core is dense
     * enough to be atypical, and the rim is empty enough to be a bad sample of
     * a galaxy. Deterministic for a given galaxy.
     */
    static FUniversePosition GetInhabitedPosition(const FGalaxyDescriptor& Galaxy);

    /**
     * The galaxy containing a position, or the nearest one within a radius.
     *
     * "Containing" means inside the disk radius, which is the question almost
     * every caller actually has: which galaxy's density applies here.
     */
    static bool FindGalaxyAt(
        const FUniverseSeedHierarchy& Hierarchy,
        const FUniversePosition& Position,
        FGalaxyDescriptor& OutGalaxy);

    // --- The density function ---------------------------------------------

    /** A universe position, expressed relative to a galaxy. */
    static FGalaxyLocalPosition ToGalaxyLocal(
        const FGalaxyDescriptor& Galaxy,
        const FUniversePosition& Position);

    /** The inverse: a galaxy-local offset in light years, as a position. */
    static FUniversePosition FromGalaxyLocal(
        const FGalaxyDescriptor& Galaxy,
        const FVector3d& OffsetLightYears);

    /**
     * Relative stellar density at a point, in [0, 1].
     *
     * 1 means as dense as the galactic core; 0 means no stars at all. Sector
     * generation multiplies its baseline count by this, so the whole of the
     * existing star generator continues to work unchanged and simply produces
     * nothing where the galaxy is not.
     */
    static double GetStellarDensity(
        const FGalaxyDescriptor& Galaxy,
        const FUniversePosition& Position);

    /** The same, from an already-computed galaxy-local position. */
    static double GetStellarDensityAt(
        const FGalaxyDescriptor& Galaxy,
        const FGalaxyLocalPosition& Local);
};
