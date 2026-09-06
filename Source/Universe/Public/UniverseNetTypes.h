// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "UniverseCoordinates.h"
#include "StarSystemDescriptor.h"
#include "WorldPersistence.h"
#include "WorldPersistenceIdentity.h"
#include "UniverseNetTypes.generated.h"

/**
 * UniverseNetTypes.h
 *
 * The small set of types that cross the wire.
 *
 *
 * WHAT IS REPLICATED, AND WHAT IS NOT
 *
 * The server and every client can already regenerate the same universe from the
 * same seed. The universe is therefore *shared mathematical data*, not something
 * to send: replicating it would mean transmitting terrain vertices, trees,
 * biome samples and star descriptors that both ends can compute exactly and
 * agree on bit for bit.
 *
 * What actually has to cross the wire is only the part that is not derivable:
 *
 *     seed and version identity     - so both ends agree which universe this is
 *     authoritative dynamic state   - where players are, how they are moving
 *     persistent deltas             - the differences from the generated world
 *     relevant entities             - what is near enough to matter
 *
 * Everything in this header is one of those four.
 *
 *
 * WHY POSITIONS ARE NOT FVECTORS
 *
 * A player's canonical position is an FUniversePosition: an int64 cell index
 * per axis plus a double local offset. It cannot be replicated as an FVector
 * without throwing away everything that makes it useful - a float loses metre
 * resolution at ten thousand kilometres, and the universe is 10^10 light years
 * across.
 *
 * It also cannot be replicated as the raw struct, because FUniversePosition
 * lives in UniverseCore, which does not depend on the engine and must keep
 * compiling standalone. So the wire format is a USTRUCT wrapper here, in the
 * gameplay module, which is where engine dependencies belong.
 *
 *
 * WHY THIS IS THE RIGHT SHAPE FOR ORIGIN REBASING
 *
 * Two clients standing next to each other will in general have *different*
 * render origins - each rebases around its own viewpoint. If positions were
 * replicated in render space, the two would disagree about where the other one
 * is by however far apart their origins happened to be.
 *
 * Replicating the canonical position instead makes that impossible. Each client
 * converts to its own render space on receipt, and both are right.
 */

/**
 * A canonical universe position, in a form that can be replicated.
 *
 * Quantised to the centimetre on the wire, not to a float: the local offset is
 * sent as three int32 counts of a quantisation step, so a position anywhere in
 * the universe costs 24 bytes and is exact to the step. The default step of one
 * centimetre is four hundred times finer than a player can perceive and four
 * hundred times coarser than the coordinate system can express, which is the
 * right place to spend the bits.
 */
USTRUCT(BlueprintType)
struct UNIVERSE_API FReplicatedUniversePosition
{
    GENERATED_BODY()

    UPROPERTY() int64 CellX = 0;
    UPROPERTY() int64 CellY = 0;
    UPROPERTY() int64 CellZ = 0;

    /** Local offset within the cell, in centimetres, quantised on the wire. */
    UPROPERTY() double LocalX = 0.0;
    UPROPERTY() double LocalY = 0.0;
    UPROPERTY() double LocalZ = 0.0;

    FReplicatedUniversePosition() = default;

    explicit FReplicatedUniversePosition(const FUniversePosition& Position)
    {
        Set(Position);
    }

    void Set(const FUniversePosition& Position)
    {
        CellX = Position.CellX;
        CellY = Position.CellY;
        CellZ = Position.CellZ;
        LocalX = Position.Local.X;
        LocalY = Position.Local.Y;
        LocalZ = Position.Local.Z;
    }

    FUniversePosition Get() const
    {
        FUniversePosition Position;
        Position.CellX = CellX;
        Position.CellY = CellY;
        Position.CellZ = CellZ;
        Position.Local = FVector3d(LocalX, LocalY, LocalZ);
        Position.Normalize();

        return Position;
    }

    bool operator==(const FReplicatedUniversePosition& Other) const
    {
        return CellX == Other.CellX && CellY == Other.CellY && CellZ == Other.CellZ
            && LocalX == Other.LocalX && LocalY == Other.LocalY && LocalZ == Other.LocalZ;
    }

    bool operator!=(const FReplicatedUniversePosition& Other) const { return !(*this == Other); }
};

/**
 * A star system's address, in a form that can be replicated.
 *
 * The same reason as the position wrapper: FUniverseSystemId lives in
 * UniverseGeneration, which does not depend on the engine and must keep
 * compiling standalone, so it cannot be a USTRUCT itself.
 *
 * Only the address is sent. The system's *contents* - its star, its planets,
 * their names and sizes - are regenerated identically at both ends from this,
 * so sending them would be sending data the receiver can compute.
 */
USTRUCT(BlueprintType)
struct UNIVERSE_API FReplicatedSystemId
{
    GENERATED_BODY()

    UPROPERTY() int64 SectorX = 0;
    UPROPERTY() int64 SectorY = 0;
    UPROPERTY() int64 SectorZ = 0;
    UPROPERTY() int32 IndexInSector = 0;
    UPROPERTY() bool bValid = false;

    FReplicatedSystemId() = default;

    explicit FReplicatedSystemId(const FUniverseSystemId& Id)
    {
        Set(Id);
    }

    void Set(const FUniverseSystemId& Id)
    {
        SectorX = Id.SectorX;
        SectorY = Id.SectorY;
        SectorZ = Id.SectorZ;
        IndexInSector = Id.IndexInSector;
        bValid = Id.IsValid();
    }

    FUniverseSystemId Get() const
    {
        return bValid
            ? FUniverseSystemId(SectorX, SectorY, SectorZ, IndexInSector)
            : FUniverseSystemId();
    }

    bool IsValid() const { return bValid; }

    bool operator==(const FReplicatedSystemId& Other) const
    {
        return bValid == Other.bValid
            && SectorX == Other.SectorX
            && SectorY == Other.SectorY
            && SectorZ == Other.SectorZ
            && IndexInSector == Other.IndexInSector;
    }

    bool operator!=(const FReplicatedSystemId& Other) const { return !(*this == Other); }
};

/**
 * Which universe this is, and which rules generated it.
 *
 * Every field here is something that, if it differed between a server and a
 * client, would make them disagree about the world while both believing they
 * were right. A client with a different terrain version would walk on ground
 * the server does not think is there; one with a different seed would be in an
 * entirely different universe wearing the same clothes.
 *
 * So this is checked on connection and a mismatch is a refusal, not a warning.
 * There is no version of "mostly the same universe" that is worth playing in.
 */
USTRUCT(BlueprintType)
struct UNIVERSE_API FUniverseWorldIdentity
{
    GENERATED_BODY()

    UPROPERTY() FString SeedText;

    UPROPERTY() int64 SeedValue = 0;

    UPROPERTY() int32 GalaxyVersion = 0;
    UPROPERTY() int32 SystemVersion = 0;
    UPROPERTY() int32 TerrainVersion = 0;
    UPROPERTY() int32 EnvironmentVersion = 0;
    UPROPERTY() int32 PersistenceSchemaVersion = 0;

    bool IsValid() const { return SeedValue != 0; }

    bool operator==(const FUniverseWorldIdentity& Other) const
    {
        return SeedValue == Other.SeedValue
            && SeedText == Other.SeedText
            && GalaxyVersion == Other.GalaxyVersion
            && SystemVersion == Other.SystemVersion
            && TerrainVersion == Other.TerrainVersion
            && EnvironmentVersion == Other.EnvironmentVersion
            && PersistenceSchemaVersion == Other.PersistenceSchemaVersion;
    }

    bool operator!=(const FUniverseWorldIdentity& Other) const { return !(*this == Other); }

    /** The identity this build would produce for a given seed. */
    static FUniverseWorldIdentity MakeLocal(const FString& InSeedText, uint64 InSeedValue);

    /** A human-readable difference, for the log line a refused client gets. */
    FString DescribeMismatch(const FUniverseWorldIdentity& Other) const;

    FString ToDebugString() const;
};

// ---------------------------------------------------------------------------
// Persistence, on the wire
//
// The persistence types live in UniverseCore and UniversePlanet, which do not
// depend on the engine, so none of them can be a USTRUCT and none can be an RPC
// parameter. The wire forms below are thin translations, not second
// definitions: each one converts to and from the real type and holds no
// judgement of its own.
//
// The alternative - making the persistence types USTRUCTs - would drag the
// engine into UniverseCore and break the standalone test harness, which is the
// thing that lets a million assertions run in ten seconds without an editor.
// ---------------------------------------------------------------------------

/** A persistent entity id, on the wire. */
USTRUCT(BlueprintType)
struct UNIVERSE_API FNetEntityId
{
    GENERATED_BODY()

    UPROPERTY() uint64 High = 0;
    UPROPERTY() uint64 Low = 0;

    FNetEntityId() = default;
    explicit FNetEntityId(const FPersistentEntityId& Id) : High(Id.High), Low(Id.Low) {}

    FPersistentEntityId Get() const
    {
        FPersistentEntityId Id;
        Id.High = High;
        Id.Low = Low;
        return Id;
    }

    bool operator==(const FNetEntityId& Other) const
    {
        return High == Other.High && Low == Other.Low;
    }
};

/** A persistence region id, on the wire. */
USTRUCT(BlueprintType)
struct UNIVERSE_API FNetRegionId
{
    GENERATED_BODY()

    UPROPERTY() uint64 PlanetKey = 0;
    UPROPERTY() uint8 Face = 0;
    UPROPERTY() uint8 Level = 0;
    UPROPERTY() uint32 X = 0;
    UPROPERTY() uint32 Y = 0;

    FNetRegionId() = default;

    explicit FNetRegionId(const FPersistenceRegionId& Id)
        : PlanetKey(Id.PlanetKey), Face(Id.Face), Level(Id.Level), X(Id.X), Y(Id.Y)
    {
    }

    FPersistenceRegionId Get() const
    {
        FPersistenceRegionId Id;
        Id.PlanetKey = PlanetKey;
        Id.Face = Face;
        Id.Level = Level;
        Id.X = X;
        Id.Y = Y;
        return Id;
    }

    bool IsValid() const { return Get().IsValid(); }

    bool operator==(const FNetRegionId& Other) const
    {
        return PlanetKey == Other.PlanetKey && Face == Other.Face
            && Level == Other.Level && X == Other.X && Y == Other.Y;
    }
};

/** Where a persistent thing sits on a planet, on the wire. */
USTRUCT(BlueprintType)
struct UNIVERSE_API FNetPlacement
{
    GENERATED_BODY()

    UPROPERTY() FVector_NetQuantizeNormal Direction = FVector_NetQuantizeNormal(0.0, 0.0, 1.0);
    UPROPERTY() float HeightAboveTerrainMeters = 0.0f;
    UPROPERTY() float YawRadians = 0.0f;
    UPROPERTY() float Scale = 1.0f;

    FNetPlacement() = default;

    explicit FNetPlacement(const FPersistentPlacement& Placement)
        : Direction(Placement.Direction.X, Placement.Direction.Y, Placement.Direction.Z)
        , HeightAboveTerrainMeters(static_cast<float>(Placement.HeightAboveTerrainMeters))
        , YawRadians(static_cast<float>(Placement.YawRadians))
        , Scale(static_cast<float>(Placement.Scale))
    {
    }

    FPersistentPlacement Get() const
    {
        FPersistentPlacement Placement;
        Placement.Direction = FVector3d(Direction.X, Direction.Y, Direction.Z).GetSafeNormal();
        Placement.HeightAboveTerrainMeters = static_cast<double>(HeightAboveTerrainMeters);
        Placement.YawRadians = static_cast<double>(YawRadians);
        Placement.Scale = static_cast<double>(Scale);
        return Placement;
    }
};

/** One persistent record, on the wire. */
USTRUCT(BlueprintType)
struct UNIVERSE_API FNetEntityRecord
{
    GENERATED_BODY()

    UPROPERTY() FNetEntityId EntityId;
    UPROPERTY() FNetRegionId RegionId;
    UPROPERTY() FString TypeId;
    UPROPERTY() FNetPlacement Placement;
    UPROPERTY() bool bIsRemoval = false;
    UPROPERTY() FString OwnerId;
    UPROPERTY() int32 DataVersion = 1;

    FNetEntityRecord() = default;

    explicit FNetEntityRecord(const FWorldEntityRecord& Record)
        : EntityId(Record.EntityId)
        , RegionId(Record.RegionId)
        , TypeId(Record.TypeId)
        , Placement(Record.Placement)
        , bIsRemoval(Record.bIsRemoval)
        , OwnerId(Record.OwnerId)
        , DataVersion(Record.DataVersion)
    {
    }

    FWorldEntityRecord Get() const
    {
        FWorldEntityRecord Record;
        Record.EntityId = EntityId.Get();
        Record.RegionId = RegionId.Get();
        Record.TypeId = TypeId;
        Record.Placement = Placement.Get();
        Record.bIsRemoval = bIsRemoval;
        Record.OwnerId = OwnerId;
        Record.DataVersion = DataVersion;
        return Record;
    }
};

/**
 * Everything persistent about one region, on the wire.
 *
 * Sent whole on subscription and never again: subsequent changes arrive as
 * single-entity messages. A region is a few rows at most, so the whole-region
 * message is small, and sending it once removes any question of a client
 * having half of one.
 *
 * The sequence number is what makes late and duplicate messages safe. A client
 * that has already applied sequence 7 ignores a re-delivered 5, and one that
 * receives 9 without 8 knows it has a gap and can ask for the region again.
 */
USTRUCT(BlueprintType)
struct UNIVERSE_API FNetRegionDelta
{
    GENERATED_BODY()

    UPROPERTY() FNetRegionId RegionId;

    UPROPERTY() TArray<FNetEntityRecord> Created;

    UPROPERTY() TArray<FNetEntityId> Removed;

    /** Monotonic per region. See above. */
    UPROPERTY() int32 Sequence = 0;

    FNetRegionDelta() = default;

    explicit FNetRegionDelta(const FWorldRegionDelta& Delta, int32 InSequence = 0)
        : RegionId(Delta.RegionId)
        , Sequence(InSequence)
    {
        Created.Reserve(Delta.Created.Num());

        for (const FWorldEntityRecord& Record : Delta.Created)
        {
            Created.Emplace(Record);
        }

        Removed.Reserve(Delta.Removed.Num());

        for (const FPersistentEntityId& Id : Delta.Removed)
        {
            Removed.Emplace(Id);
        }
    }

    FWorldRegionDelta Get() const
    {
        FWorldRegionDelta Delta;
        Delta.RegionId = RegionId.Get();
        Delta.bLoaded = true;

        Delta.Created.Reserve(Created.Num());

        for (const FNetEntityRecord& Record : Created)
        {
            Delta.Created.Add(Record.Get());
        }

        for (const FNetEntityId& Id : Removed)
        {
            Delta.Removed.Add(Id.Get());
        }

        return Delta;
    }
};

/** How much of a remote player this client is being told about. */
UENUM(BlueprintType)
enum class ENetRelevanceClass : uint8
{
    /** Not replicated at all. Different system, or further. */
    Irrelevant      UMETA(DisplayName = "Irrelevant"),

    /** Same system. Position only, at a low rate. */
    SameSystem      UMETA(DisplayName = "Same System"),

    /** Same planet or nearby in space. Position and orientation. */
    SameRegion      UMETA(DisplayName = "Same Region"),

    /** Close enough to see. Everything, at full rate. */
    Visible         UMETA(DisplayName = "Visible"),
};

UNIVERSE_API const TCHAR* LexToString(ENetRelevanceClass Class);

/**
 * One remote player, as this client is being told about them.
 *
 * A flat replicated record rather than a replicated pawn. That is a deliberate
 * choice and the reason is origin rebasing: a replicated actor transform is in
 * *someone's* render space, and two clients never share one. Sending the
 * canonical position as data and letting each client place its own proxy is the
 * only arrangement in which both are right.
 *
 * It also makes relevance a property of the data rather than of an actor's
 * lifetime, so a player crossing a system boundary changes an enum rather than
 * destroying and respawning an actor on every client that can see them.
 */
USTRUCT(BlueprintType)
struct UNIVERSE_API FRemotePlayerSnapshot
{
    GENERATED_BODY()

    /** Stable across reconnects. Not the connection id. */
    UPROPERTY() FString PlayerId;

    UPROPERTY() FString DisplayName;

    UPROPERTY() FReplicatedUniversePosition Position;

    /** Metres per second, universe axes. */
    UPROPERTY() FVector_NetQuantize100 VelocityMs = FVector_NetQuantize100(ForceInitToZero);

    UPROPERTY() FRotator Orientation = FRotator::ZeroRotator;

    /** True when the player is on foot rather than in a ship. */
    UPROPERTY() bool bOnFoot = false;

    /** The planet they are standing on, or 0. */
    UPROPERTY() uint64 PlanetKey = 0;

    UPROPERTY() FReplicatedSystemId SystemId;

    UPROPERTY() ENetRelevanceClass Relevance = ENetRelevanceClass::Irrelevant;

    /** Server time this was sampled, for extrapolation. */
    UPROPERTY() double ServerTimeSeconds = 0.0;
};
