// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseSerialization.h"
#include "PlanetSurface.h"
#include "PlanetPatchId.h"
#include "PlanetVegetation.h"

/**
 * WorldPersistenceIdentity.h
 *
 * Names for things that have to survive a restart.
 *
 * The whole persistence model rests on one equation:
 *
 *     procedural base world  +  sparse deltas  =  current world
 *
 * and a delta is worthless without a name for what it applies to. "The third
 * tree in that patch" is not a name - it is a position in an array that the
 * next change to the placement rules will renumber. This file is about names
 * that do not do that.
 *
 *
 * TWO KINDS OF THING
 *
 * **Procedural entities** already exist in the base world. Their identity is
 * *derived*, not assigned: the same tree computes the same id on every machine
 * and in every run, because it is a hash of where the generator put it. Nothing
 * has to be written down for a tree to have a name - which is exactly what
 * makes "tree 817291 is removed" a complete thirty-byte statement about a world
 * containing billions of trees.
 *
 * **Created entities** do not exist in the base world. Their identity has to be
 * *assigned*, so it is a random 128-bit value. Locally that is a UUID; when a
 * server arrives it becomes the server's to allocate, and nothing else changes.
 *
 * The two are distinguishable by construction, which matters: applying a
 * "removed" delta to a created entity, or vice versa, would be silently wrong.
 *
 *
 * WHAT IDENTITY MUST NOT DEPEND ON
 *
 * Not Actor names, not pointers, not spawn order, not array index, not a GUID
 * regenerated each run, not memory addresses, not iteration order of any map.
 * The same list as ProceduralGeneration.md's, for the same reason: anything
 * with process lifetime produces a name that means something different tomorrow.
 *
 * Pure data and arithmetic. No engine types.
 */

/**
 * A spatial bucket for persistent deltas.
 *
 * Deltas are partitioned so that returning to a place loads that place's
 * changes and nothing else. One file or table for the universe would make the
 * cost of visiting a planet proportional to everything every player had ever
 * done anywhere - which is the exact failure this architecture exists to avoid.
 *
 *
 * WHY NOT A TERRAIN PATCH
 *
 * A terrain patch is chosen by screen-space error and its level changes as the
 * player moves. A persistence region must be *stable*: the region a building
 * belongs to has to be the same next year, after an LOD rewrite, and on a
 * machine with different graphics settings. Tying the two together would mean a
 * settings change relocated everybody's houses.
 *
 * So regions are a cube-sphere address at a level chosen once per planet, from
 * the planet's own radius, and stored in the id.
 *
 *
 * WHY THE LEVEL IS PER PLANET
 *
 * A fixed level would give 2.4 km regions on an Earth-sized world and 66 m ones
 * on a small moon - the first reasonable, the second producing a hundred times
 * more rows than it needs for the same amount of building. Choosing the level
 * from the radius targets a constant *metric* size instead, so the number of
 * entities in a region depends on how much has been built there rather than on
 * which body it is.
 *
 * The radius is part of the frozen planet descriptor, so the chosen level is
 * itself stable, and it is stored in the id rather than recomputed - a region
 * read from the database must not change meaning if the targeting rule is ever
 * retuned.
 */
struct UNIVERSEPLANET_API FPersistenceRegionId
{
    /** Which planet. Zero means unset. */
    uint64 PlanetKey = 0;

    /** Cube face, 0-5. */
    uint8 Face = 0;

    /** Quadtree level this region is addressed at. */
    uint8 Level = 0;

    uint32 X = 0;
    uint32 Y = 0;

    /**
     * Target region size, in metres.
     *
     * Two kilometres. Large enough that a settlement fits in one and that
     * crossing a boundary is rare; small enough that loading one is a handful
     * of rows rather than a district. It is also comfortably larger than
     * anything a player can build in this sprint, so the "structure spans two
     * regions" problem stays theoretical for now - see the header note in
     * WorldPersistence.md.
     */
    static constexpr double TargetSizeMeters = 2000.0;

    /** Levels the targeting is clamped to. */
    static constexpr uint8 MinLevel = 5;
    static constexpr uint8 MaxLevel = 16;

    /** The level a planet of this radius uses. */
    static uint8 GetLevelForRadius(double PlanetRadiusMeters);

    /** The region containing a surface direction on a planet. */
    static FPersistenceRegionId FromDirection(
        const FPlanetSurfaceDescriptor& Planet,
        const FVector3d& Direction);

    /** The region containing a planet-local position. */
    static FPersistenceRegionId FromPlanetLocal(
        const FPlanetSurfaceDescriptor& Planet,
        const FVector3d& PlanetLocalMeters);

    bool IsValid() const { return PlanetKey != 0 && Level >= MinLevel && Face < 6; }

    /** The patch this region corresponds to, for geometry queries. */
    FPlanetPatchId ToPatchId() const;

    /**
     * Approximate *edge* length, in metres.
     *
     * Not the diagonal, which is what FPlanetPatchId reports and what LOD
     * selection wants. A region's "size" in every other context here - the
     * targeting rule, how much fits in one, how often a boundary is crossed -
     * means the edge, and having the two disagree by a factor of root two is
     * the kind of quiet inconsistency that turns into a wrong constant later.
     */
    double GetSizeMeters(double PlanetRadiusMeters) const;

    /**
     * A single 64-bit key for this region *within its planet*.
     *
     * The planet key is deliberately not folded in: the database stores it as
     * its own column, so a query can ask for one planet's regions without
     * unpacking anything, and two planets cannot collide because the pair is
     * the key rather than a hash of it. A hash would also be one birthday
     * collision away from silently merging two settlements.
     */
    uint64 GetLocalKey() const;

    bool operator==(const FPersistenceRegionId& Other) const
    {
        return PlanetKey == Other.PlanetKey && Face == Other.Face
            && Level == Other.Level && X == Other.X && Y == Other.Y;
    }

    bool operator!=(const FPersistenceRegionId& Other) const { return !(*this == Other); }

    FString ToString() const;
};

FORCEINLINE uint32 GetTypeHash(const FPersistenceRegionId& Id)
{
    return ::GetTypeHash(Id.PlanetKey) ^ ::GetTypeHash(Id.GetLocalKey());
}

/**
 * What kind of thing an entity id names.
 *
 * Stored in the id itself rather than alongside it, so a bare id is
 * self-describing and a delta applied to the wrong kind is impossible rather
 * than merely unlikely.
 */
enum class EPersistentEntityKind : uint8
{
    /** Not a valid id. */
    None = 0,

    /** Exists in the procedural base world. Identity is derived. */
    Procedural = 1,

    /** Placed by a player. Identity is assigned. */
    Created = 2,
};

UNIVERSEPLANET_API const TCHAR* LexToString(EPersistentEntityKind Kind);

/**
 * A name for one persistent thing.
 *
 * 128 bits, in two halves, because 64 is not enough for the created case: a
 * random 64-bit id reaches a 50% collision chance at four billion entities,
 * which sounds like a lot until it is a shared universe with years of history.
 * At 128 bits it is not a consideration.
 *
 * The procedural case would fit in 64, but sharing one type for both is worth
 * more than eight bytes a row.
 */
struct UNIVERSEPLANET_API FPersistentEntityId
{
    uint64 High = 0;
    uint64 Low = 0;

    EPersistentEntityKind GetKind() const;

    bool IsValid() const { return GetKind() != EPersistentEntityKind::None; }

    /**
     * The id of a procedurally placed vegetation instance.
     *
     * Derived from the address the generator placed it at - planet, patch,
     * layer and grid cell - never from its index in the output array. Those
     * indices shift the moment a density is retuned or a filter is added, and
     * a removal recorded against an index would then delete a different tree.
     *
     * The terrain and environment generation versions are folded in
     * deliberately. A bump to either genuinely changes which trees exist, so a
     * removal recorded under the old version must NOT silently apply to the new
     * world's tree at the same address - it should stop matching, which is
     * exactly what including the version achieves.
     */
    static FPersistentEntityId ForVegetation(
        uint64 PlanetKey,
        const FPlanetPatchId& PatchId,
        EVegetationLayer Layer,
        int32 CellX,
        int32 CellY,
        uint32 TerrainVersion,
        uint32 EnvironmentVersion);

    /** The id of a vegetation instance, from the instance itself. */
    static FPersistentEntityId ForVegetation(
        uint64 PlanetKey,
        const FVegetationInstance& Instance,
        uint32 TerrainVersion,
        uint32 EnvironmentVersion);

    /**
     * A fresh id for something a player made.
     *
     * Random, from the supplied entropy. Locally that is a time and a counter;
     * when a server owns creation it will be the server's allocation, and only
     * this function changes.
     */
    static FPersistentEntityId CreateNew(uint64 EntropyA, uint64 EntropyB);

    /** Round-trips through a 32-character hex string, for the database. */
    FString ToHexString() const;
    static bool FromHexString(const FString& Text, FPersistentEntityId& OutId);

    bool operator==(const FPersistentEntityId& Other) const
    {
        return High == Other.High && Low == Other.Low;
    }

    bool operator!=(const FPersistentEntityId& Other) const { return !(*this == Other); }

    static constexpr int32 SerializedSizeBytes = 16;

    void Serialize(FUniverseByteWriter& Writer) const;
    bool Deserialize(FUniverseByteReader& Reader);
};

FORCEINLINE uint32 GetTypeHash(const FPersistentEntityId& Id)
{
    return ::GetTypeHash(Id.High) ^ ::GetTypeHash(Id.Low);
}
