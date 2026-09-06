// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "WorldPersistenceIdentity.h"
#include "UniverseCoordinates.h"

/**
 * WorldPersistence.h
 *
 * What gets stored, and the interface that stores it.
 *
 *
 * THE ONE RULE
 *
 *     procedural base world  +  sparse deltas  =  current world
 *
 * Nothing is saved because it exists. A planet has billions of trees and not
 * one of them is a row; the generator already knows where they are. A row
 * appears only when a player has made the world differ from what the generator
 * would produce - a thing placed, a thing removed, a thing changed.
 *
 * The property this buys is the one the whole project rests on: **storage
 * scales with player activity, not with universe size.** Ten billion untouched
 * planets cost the same as ten - nothing.
 *
 *
 * WHY THERE IS AN INTERFACE AT ALL
 *
 * Gameplay must not know it is talking to SQLite. Sprint 007 replaces local
 * authority with a server, and if a building placement calls a database
 * directly then every gameplay interaction has to be rewritten to do it. The
 * interface is the seam:
 *
 *     gameplay  ->  world state  ->  IWorldPersistenceStore  ->  SQLite / server
 *
 * The SQLite dependency is private to the module for the same reason - the
 * build enforces the seam rather than leaving it to discipline.
 */

/** Storage schema version. Bumping it requires a migration path. */
namespace WorldPersistenceSchema
{
    /**
     * 2 adds the world_facts table.
     *
     * Bumped rather than silently extended: a save written by version 1 has no
     * such table, and a build that assumed one would fail on the first read
     * from an existing world rather than at open time where it can be reported.
     */
    inline constexpr int32 Version = 2;
}

/**
 * A stable name for a kind of thing a player can place.
 *
 * A string, not a C++ class name and not an enum ordinal. A class rename must
 * not orphan every building of that type in the database, and an enum ordinal
 * shifts the moment somebody inserts a value in the middle. The version suffix
 * is part of the name so that changing what a foundation *is* produces a new
 * type rather than silently reinterpreting the old rows.
 */
namespace WorldEntityTypes
{
    inline const TCHAR* Beacon = TEXT("universe.structure.beacon.v1");
    inline const TCHAR* Foundation = TEXT("universe.structure.foundation.v1");
}

/**
 * Where a persistent thing sits on a planet, in a form that survives everything.
 *
 * Not an Unreal transform. An Unreal transform is relative to a render origin
 * that is rebased whenever the player moves ten kilometres, so storing one
 * would record a position that means something different every session.
 *
 * Not a planet-local vector either, quite: the *direction* and the *height*
 * are stored separately because they answer different questions and have wildly
 * different precision requirements. A direction is unit length and needs full
 * double precision to place a metre accurately on a planet-sized sphere; a
 * height above the terrain is a small number that stays small.
 *
 *
 * ORIENTATION
 *
 * Yaw about the local up, in radians. Nothing else.
 *
 * A world-space quaternion would bake in the planet's orientation at the moment
 * of placement, so a building would appear correctly rotated only if the planet
 * had never turned - and this planet rotates. Storing the rotation *relative to
 * the local tangent frame* means the building's relationship to the ground it
 * sits on is what is preserved, which is the thing that actually matters.
 *
 * Pitch and roll are deliberately absent: a structure sits flat on the ground,
 * and the ground's own slope supplies any tilt. When something needs to be
 * placed at an angle, this grows a field rather than the meaning of yaw
 * changing.
 */
struct UNIVERSE_API FPersistentPlacement
{
    /** Unit direction from the planet centre. */
    FVector3d Direction = FVector3d(0.0, 0.0, 1.0);

    /** Height above the terrain surface at that direction, in metres. */
    double HeightAboveTerrainMeters = 0.0;

    /** Rotation about local up, radians. */
    double YawRadians = 0.0;

    /** Uniform scale. */
    double Scale = 1.0;

    bool IsValid() const { return !Direction.IsZero(); }
};

/** One persistent record: a thing that exists, or a thing that no longer does. */
struct UNIVERSE_API FWorldEntityRecord
{
    FPersistentEntityId EntityId;
    FPersistenceRegionId RegionId;

    /**
     * What it is. Empty for a removal tombstone, because a tombstone says
     * nothing about the thing it removes beyond its name - and duplicating the
     * procedural entity's data just to record its absence would defeat the
     * point of deriving identity in the first place.
     */
    FString TypeId;

    FPersistentPlacement Placement;

    /**
     * True if this record removes a procedural entity rather than creating one.
     *
     * A separate flag rather than a separate table: a region load wants both in
     * one query, and splitting them doubles the round trips to answer the only
     * question anybody asks - "what is different here".
     */
    bool bIsRemoval = false;

    /** Who made it. A stable local id for now; an account later. */
    FString OwnerId;

    /** Schema version of the payload, for future migration. */
    int32 DataVersion = 1;

    /** Free-form per-entity state. Empty for this sprint's structures. */
    FString StatePayload;

    bool IsValid() const { return EntityId.IsValid() && RegionId.IsValid(); }
};

/** Everything persistent about one region. */
struct UNIVERSE_API FWorldRegionDelta
{
    FPersistenceRegionId RegionId;

    /** Things the player placed. */
    TArray<FWorldEntityRecord> Created;

    /**
     * Procedural entities the player removed.
     *
     * A set of names, not records. This is the compact form the whole model
     * depends on: removing a tree costs sixteen bytes and says nothing about
     * the tree, because the generator can still produce it and the only new
     * information is that it should not be shown.
     */
    TSet<FPersistentEntityId> Removed;

    bool bLoaded = false;

    bool IsEmpty() const { return Created.Num() == 0 && Removed.Num() == 0; }
};

/** World-level metadata, checked at startup. */
struct UNIVERSE_API FWorldSaveMetadata
{
    /** Which universe these deltas belong to. */
    FString UniverseSeedText;
    uint64 UniverseSeedValue = 0;

    /**
     * Generation versions in force when this world was created.
     *
     * The reason this exists: a terrain version bump moves mountains, and a
     * house recorded on a hill that is now a valley floats three hundred metres
     * above the ground. That must be *detected* and reported rather than
     * silently rendered.
     */
    uint32 TerrainVersion = 0;
    uint32 EnvironmentVersion = 0;

    int32 SchemaVersion = 0;

    int64 CreatedAtUnixSeconds = 0;
    int64 LastSavedAtUnixSeconds = 0;

    bool IsValid() const { return SchemaVersion > 0; }
};

/** Why a store operation failed. */
enum class EWorldPersistenceStatus : uint8
{
    Ok = 0,
    NotOpen,
    OpenFailed,
    SchemaMismatch,
    UniverseMismatch,
    GenerationVersionMismatch,
    WriteFailed,
    ReadFailed,
    CorruptRecord,
};

UNIVERSE_API const TCHAR* LexToString(EWorldPersistenceStatus Status);

/**
 * Storage, abstracted.
 *
 * Synchronous by design at this layer. Asynchrony belongs to the caller - see
 * UWorldStateSubsystem, which runs these on the thread pool and hands results
 * back to the game thread. Making the interface itself async would force every
 * implementation to reinvent a task system, and would make the SQLite backend
 * far harder to test than it needs to be: a synchronous store is a function,
 * and a function can be called from a test with no world, no tick and no
 * waiting.
 */
class UNIVERSE_API IWorldPersistenceStore
{
public:
    virtual ~IWorldPersistenceStore() = default;

    /** Opens or creates the store. Returns Ok, or why not. */
    virtual EWorldPersistenceStatus Open(const FString& FilePath) = 0;

    virtual void Close() = 0;
    virtual bool IsOpen() const = 0;

    /** Reads world metadata, writing defaults if the store is new. */
    virtual EWorldPersistenceStatus LoadOrInitialiseMetadata(
        FWorldSaveMetadata& InOutMetadata) = 0;

    /** Everything persistent in one region. */
    virtual EWorldPersistenceStatus LoadRegion(
        const FPersistenceRegionId& RegionId,
        FWorldRegionDelta& OutDelta) = 0;

    /**
     * Writes a record. Idempotent: writing the same id twice replaces rather
     * than duplicating, so a retried operation cannot produce two buildings.
     */
    virtual EWorldPersistenceStatus SaveRecord(const FWorldEntityRecord& Record) = 0;

    /** Writes several records atomically. */
    virtual EWorldPersistenceStatus SaveRecords(const TArray<FWorldEntityRecord>& Records) = 0;

    /**
     * Deletes a record. Idempotent: deleting something absent succeeds, because
     * the caller's intent - that it not be there - is already satisfied.
     */
    virtual EWorldPersistenceStatus DeleteRecord(const FPersistentEntityId& EntityId) = 0;

    /** Deletes everything for a planet. Development tooling. */
    virtual EWorldPersistenceStatus DeletePlanet(uint64 PlanetKey) = 0;

    /** Deletes everything. Development tooling. */
    virtual EWorldPersistenceStatus DeleteAll() = 0;

    // --- Observability ----------------------------------------------------

    /** Total records stored, across every region. */
    virtual int64 GetRecordCount() const = 0;

    /** Store size on disk, in bytes. */
    virtual int64 GetStorageSizeBytes() const = 0;

    /** Last error text, for the log and the HUD. */
    virtual FString GetLastError() const = 0;

    // --- World facts --------------------------------------------------------
    //
    // A small key/value table for things that are true of the *world* rather
    // than of a place in it: which systems the player has seen, and whatever
    // else turns out to be world-scoped later.
    //
    // Separate from entity_records because it genuinely is a different thing.
    // Discovery is not attached to a planet or a region, and storing it as an
    // entity would mean inventing a planet key for it - the kind of shortcut
    // that reads fine for a week and then makes "delete this planet's data"
    // silently forget where the player has been.

    /** Writes one fact. An existing key is replaced. */
    virtual EWorldPersistenceStatus SaveFact(const FString& Key, const FString& Value) = 0;

    /** Reads one fact. Returns false when the key is absent. */
    virtual bool LoadFact(const FString& Key, FString& OutValue) const = 0;

    /**
     * Every fact whose key starts with a prefix.
     *
     * Prefixed keys rather than a second table: discovery uses "sys." and there
     * is exactly one consumer. A table per fact type would be more structure
     * than the problem has.
     */
    virtual EWorldPersistenceStatus LoadFactsWithPrefix(
        const FString& Prefix,
        TArray<TPair<FString, FString>>& OutFacts) const = 0;
};

/** Creates the local SQLite-backed store. */
UNIVERSE_API TUniquePtr<IWorldPersistenceStore> MakeSQLiteWorldPersistenceStore();
