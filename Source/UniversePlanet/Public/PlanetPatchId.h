// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "CubeSphere.h"
#include "UniverseHash.h"
#include "UniverseSerialization.h"

/**
 * PlanetPatchId.h
 *
 * The stable address of one node in a planet's surface quadtree.
 *
 *     (Face, Level, X, Y)
 *
 * Level 0 is the whole face; level L divides it into a 2^L x 2^L grid, and
 * patch (L, X, Y) covers
 *
 *     u in [X / 2^L, (X+1) / 2^L]      v in [Y / 2^L, (Y+1) / 2^L]
 *
 * Deliberately planet-local. A patch ID says *where on a planet*, not *which
 * planet* - the planet's own identity lives in FPlanetDescriptor, and pairing
 * the two is the caller's job (GetPersistenceKey does it for storage). Baking a
 * planet ID into every quadtree node would put eight redundant bytes into the
 * hottest data structure in the system, since a quadtree only ever spans one
 * planet.
 *
 * The identity is arithmetic, never a pointer, a spawn index or an Actor name.
 * A patch generated today, on another machine, after an engine upgrade, must
 * carry the same address - that is what lets terrain be thrown away and
 * regenerated, and what will let a player's structure be pinned to a piece of
 * ground years later.
 *
 * Quadrant naming for children, matching the UV convention in CubeSphere.h
 * (+U is East, +V is North):
 *
 *     v
 *     ^   +---------+---------+
 *     |   |   NW    |   NE    |      NW = (2X,   2Y+1)
 *     |   |         |         |      NE = (2X+1, 2Y+1)
 *     |   +---------+---------+      SW = (2X,   2Y  )
 *     |   |   SW    |   SE    |      SE = (2X+1, 2Y  )
 *     |   |         |         |
 *     |   +---------+---------+
 *     +-------------------------> u
 */

/** Child quadrant, in the UV orientation drawn above. */
enum class EPlanetPatchQuadrant : uint8
{
    SouthWest = 0,
    SouthEast = 1,
    NorthWest = 2,
    NorthEast = 3,
    Count = 4
};

/** Direction to a same-level neighbour. */
enum class EPlanetPatchNeighbour : uint8
{
    West = 0,
    East = 1,
    South = 2,
    North = 3,
    Count = 4
};

struct UNIVERSEPLANET_API FPlanetPatchId
{
    /**
     * Deepest addressable level.
     *
     * At Earth radius a face spans about 10,007 km, so level 24 patches are
     * roughly 0.6 m across - far past any plausible gameplay resolution, and
     * comfortably inside uint32 for X and Y. The cap exists so that 2^Level
     * arithmetic stays in a range where int64 conversions cannot overflow, and
     * so that a corrupt level in a save file is rejected rather than producing
     * an astronomically deep subdivision request.
     */
    static constexpr uint8 MaxLevel = 24;

    uint8 Face = 0;
    uint8 Level = 0;
    uint32 X = 0;
    uint32 Y = 0;

    FPlanetPatchId() = default;

    FPlanetPatchId(CubeSphere::EFace InFace, uint8 InLevel, uint32 InX, uint32 InY)
        : Face(static_cast<uint8>(InFace))
        , Level(InLevel)
        , X(InX)
        , Y(InY)
    {
    }

    /** The root patch of a face: level 0, covering the whole face. */
    static FPlanetPatchId Root(CubeSphere::EFace InFace)
    {
        return FPlanetPatchId(InFace, 0, 0, 0);
    }

    CubeSphere::EFace GetFace() const { return static_cast<CubeSphere::EFace>(Face); }

    /** Number of patches per side at this level: 2^Level. */
    uint32 GetGridSize() const { return 1u << Level; }

    /**
     * True if every field is in range for a real patch.
     *
     * Called on anything arriving from a file or the network - an out-of-range
     * level would otherwise index off the end of the face basis table or
     * request a subdivision depth that never terminates.
     */
    /**
     * The patch at a given level containing a direction.
     *
     * The inverse of GetDirectionAt, and the way anything that knows *where*
     * it is finds out *which patch* that is - weather cells, vegetation
     * lookups, collision queries. Written once here rather than at each call
     * site, because the floor-and-clamp is easy to get subtly wrong exactly on
     * a face boundary, where the UV is 1.0 and the naive index is one past the
     * end.
     */
    static FPlanetPatchId FromDirection(const FVector3d& Direction, uint8 Level);

    bool IsValid() const
    {
        if (Face >= CubeSphere::FaceCount || Level > MaxLevel)
        {
            return false;
        }
        const uint32 GridSize = GetGridSize();
        return X < GridSize && Y < GridSize;
    }

    bool operator==(const FPlanetPatchId& Other) const
    {
        return Face == Other.Face && Level == Other.Level && X == Other.X && Y == Other.Y;
    }

    bool operator!=(const FPlanetPatchId& Other) const { return !(*this == Other); }

    // --- Hierarchy ---------------------------------------------------------

    bool IsRoot() const { return Level == 0; }

    /** The parent patch. Undefined at level 0; check IsRoot first. */
    FPlanetPatchId GetParent() const
    {
        return FPlanetPatchId(GetFace(), static_cast<uint8>(Level - 1), X >> 1, Y >> 1);
    }

    /** One of the four children. Undefined at MaxLevel; check CanSplit first. */
    FPlanetPatchId GetChild(EPlanetPatchQuadrant Quadrant) const
    {
        const uint32 ChildX = (X << 1) + ((static_cast<uint8>(Quadrant) & 1u) ? 1u : 0u);
        const uint32 ChildY = (Y << 1) + ((static_cast<uint8>(Quadrant) & 2u) ? 1u : 0u);
        return FPlanetPatchId(GetFace(), static_cast<uint8>(Level + 1), ChildX, ChildY);
    }

    bool CanSplit() const { return Level < MaxLevel; }

    /** Which quadrant of its parent this patch occupies. */
    EPlanetPatchQuadrant GetQuadrantInParent() const
    {
        const uint32 Bits = (X & 1u) | ((Y & 1u) << 1);
        return static_cast<EPlanetPatchQuadrant>(Bits);
    }

    /** True if Other is this patch or lies beneath it in the quadtree. */
    bool Contains(const FPlanetPatchId& Other) const;

    // --- Neighbours --------------------------------------------------------

    /**
     * The same-level neighbour in a given direction, crossing cube-face seams
     * when necessary.
     *
     * Returns false only at MaxLevel overflow or for an invalid patch; a planet
     * surface has no boundary, so every valid patch has four neighbours.
     *
     * Crossing a seam is the interesting case: the neighbour may live on a
     * different face, meet this patch on a differently-oriented edge, and have
     * its along-edge coordinate reversed. All of that is resolved through the
     * canonical edge-correspondence table rather than by re-projecting a
     * direction, so the result is exact integer arithmetic.
     */
    bool TryGetNeighbour(EPlanetPatchNeighbour Direction, FPlanetPatchId& OutNeighbour) const;

    // --- Surface region ----------------------------------------------------

    /** UV bounds of this patch on its face. All four values are dyadic. */
    void GetUVBounds(double& OutMinU, double& OutMinV, double& OutMaxU, double& OutMaxV) const
    {
        const double InvGrid = 1.0 / static_cast<double>(GetGridSize());
        OutMinU = static_cast<double>(X) * InvGrid;
        OutMinV = static_cast<double>(Y) * InvGrid;
        OutMaxU = static_cast<double>(X + 1) * InvGrid;
        OutMaxV = static_cast<double>(Y + 1) * InvGrid;
    }

    /**
     * A UV inside this patch, from normalised patch-local coordinates in
     * [0, 1]^2.
     *
     * Written as (X + Local) / GridSize rather than MinU + Local * SizeU
     * because the former is exactly dyadic whenever Local is - which is the
     * property the seam guarantees depend on. The algebraically identical
     * second form rounds.
     */
    void GetUVAt(double LocalU, double LocalV, double& OutU, double& OutV) const
    {
        const double InvGrid = 1.0 / static_cast<double>(GetGridSize());
        OutU = (static_cast<double>(X) + LocalU) * InvGrid;
        OutV = (static_cast<double>(Y) + LocalV) * InvGrid;
    }

    /** Unit direction at normalised patch-local coordinates. */
    FVector3d GetDirectionAt(double LocalU, double LocalV) const
    {
        double U = 0.0;
        double V = 0.0;
        GetUVAt(LocalU, LocalV, U, V);
        return CubeSphere::FaceUVToDirection(GetFace(), U, V);
    }

    /** Unit direction at the patch centre. */
    FVector3d GetCentreDirection() const { return GetDirectionAt(0.5, 0.5); }

    /**
     * Angular radius of the patch, in radians: the largest angle between the
     * centre direction and any of the four corners.
     *
     * This is what LOD selection needs, and it is why patch "size" is measured
     * as an angle rather than a UV extent - cube-sphere distortion makes two
     * patches of equal UV size cover noticeably different amounts of sphere
     * depending on where they sit on the face.
     */
    double GetAngularRadius() const;

    /** Arc length of the patch's diagonal on a sphere of the given radius. */
    double GetApproximateSizeMeters(double PlanetRadiusMeters) const
    {
        return 2.0 * GetAngularRadius() * PlanetRadiusMeters;
    }

    // --- Identity ----------------------------------------------------------

    /**
     * Stable 64-bit hash of the address. Deterministic across runs, machines
     * and engine versions.
     */
    uint64 GetStableHash64() const
    {
        uint64 Hash = UniverseHash::Mix64(0x5041544348494400ull);  // "PATCHID"
        Hash = UniverseHash::Combine(Hash, static_cast<uint32>(Face));
        Hash = UniverseHash::Combine(Hash, static_cast<uint32>(Level));
        Hash = UniverseHash::Combine(Hash, X);
        Hash = UniverseHash::Combine(Hash, Y);
        return Hash;
    }

    /**
     * A key that identifies this patch on a specific planet, for persistence
     * and future networking. The planet's seed is mixed in here rather than
     * stored in every patch ID.
     */
    uint64 GetPersistenceKey(uint64 PlanetSeed) const
    {
        return UniverseHash::Hash(GetStableHash64(), PlanetSeed);
    }

    static constexpr int32 SerializedSizeBytes = 1 + 1 + 4 + 4;

    void Serialize(FUniverseByteWriter& Writer) const
    {
        Writer.WriteUInt8(Face);
        Writer.WriteUInt8(Level);
        Writer.WriteUInt32(X);
        Writer.WriteUInt32(Y);
    }

    /** Reads a patch ID, rejecting anything out of range. */
    bool Deserialize(FUniverseByteReader& Reader);

    FString ToDebugString() const;
};

inline uint32 GetTypeHash(const FPlanetPatchId& Id)
{
    const uint64 Hash = Id.GetStableHash64();
    return static_cast<uint32>(Hash ^ (Hash >> 32));
}
