// Copyright Universe Project. All Rights Reserved.

#include "WorldStateSubsystem.h"
#include "PlanetActor.h"
#include "UniverseHash.h"
#include "PlanetSurfaceQuery.h"

#include "Async/Async.h"
#include "Engine/World.h"
#include "Misc/Paths.h"

DEFINE_LOG_CATEGORY_STATIC(LogWorldState, Log, All);

void UWorldStateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    Store = MakeSQLiteWorldPersistenceStore();
}

void UWorldStateSubsystem::Deinitialize()
{
    // Loads in flight are simply abandoned: their results are read on the game
    // thread and nobody will read them again. Writes have already completed by
    // construction - see CreateEntity - so there is nothing to flush.
    Loads.Reset();
    PendingLoads = 0;

    Regions.Reset();
    RegionLastUsed.Reset();
    Requested.Reset();

    if (Store.IsValid())
    {
        Store->Close();
        Store.Reset();
    }

    Super::Deinitialize();
}

TStatId UWorldStateSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UWorldStateSubsystem, STATGROUP_Tickables);
}

bool UWorldStateSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

bool UWorldStateSubsystem::OpenWorld(const FString& UniverseSeedText, uint64 UniverseSeedValue)
{
    if (!Store.IsValid())
    {
        return false;
    }

    // One database per universe seed.
    //
    // Section 55 asks that deltas never be applied to a different universe.
    // Putting the seed in the *filename* makes that structurally true rather
    // than a check that could be forgotten - changing the seed cannot reach the
    // old data because it is not looking at the same file. The metadata check
    // then catches the remaining case: a file whose name says one thing and
    // whose contents say another.
    const FString FileName = FString::Printf(
        TEXT("Universe-%016llX.db"), static_cast<unsigned long long>(UniverseSeedValue));

    const FString FilePath = FPaths::Combine(
        FPaths::ProjectSavedDir(), TEXT("WorldState"), FileName);

    OpenStatus = Store->Open(FilePath);

    if (OpenStatus != EWorldPersistenceStatus::Ok)
    {
        UE_LOG(LogWorldState, Error,
            TEXT("World state unavailable (%s): %s"),
            LexToString(OpenStatus), *Store->GetLastError());

        return false;
    }

    Metadata.UniverseSeedText = UniverseSeedText;
    Metadata.UniverseSeedValue = UniverseSeedValue;
    Metadata.TerrainVersion = PlanetTerrainVersion::Current;
    Metadata.EnvironmentVersion = PlanetEnvironmentVersion::Current;
    Metadata.SchemaVersion = WorldPersistenceSchema::Version;

    OpenStatus = Store->LoadOrInitialiseMetadata(Metadata);

    // A generation mismatch is loud but not fatal - the world still loads, and
    // the structures in it may be floating. Anything else is fatal to
    // persistence and the session continues without it rather than corrupting
    // data.
    if (OpenStatus != EWorldPersistenceStatus::Ok
        && OpenStatus != EWorldPersistenceStatus::GenerationVersionMismatch)
    {
        UE_LOG(LogWorldState, Error,
            TEXT("World metadata rejected (%s): %s"),
            LexToString(OpenStatus), *Store->GetLastError());

        Store->Close();
        return false;
    }

    UE_LOG(LogWorldState, Log,
        TEXT("World state open: %s  universe \"%s\"  terrain v%u  environment v%u  ")
        TEXT("schema v%d  %lld records  %.1f KB"),
        *FilePath, *Metadata.UniverseSeedText,
        Metadata.TerrainVersion, Metadata.EnvironmentVersion, Metadata.SchemaVersion,
        static_cast<long long>(Store->GetRecordCount()),
        Store->GetStorageSizeBytes() / 1024.0);

    return true;
}

void UWorldStateSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TimeSeconds += static_cast<double>(DeltaTime);

    // Collect finished loads. Iterated backwards so completed entries can be
    // removed without disturbing the ones still to check.
    for (int32 Index = Loads.Num() - 1; Index >= 0; --Index)
    {
        FPendingLoad& Load = Loads[Index];

        if (!Load.bComplete.IsValid() || !*Load.bComplete)
        {
            continue;
        }

        LastReadMs = (FPlatformTime::Seconds() - Load.StartSeconds) * 1000.0;

        if (Load.Result.IsValid())
        {
            ApplyLoaded(*Load.Result);
        }

        Requested.Remove(Load.RegionId);

        Loads.RemoveAtSwap(Index);
        --PendingLoads;
    }

    EvictIfNeeded();
}

void UWorldStateSubsystem::ApplyLoaded(const FWorldRegionDelta& Delta)
{
    if (!Delta.RegionId.IsValid())
    {
        return;
    }

    Regions.Add(Delta.RegionId, Delta);
    RegionLastUsed.Add(Delta.RegionId, TimeSeconds);

    if (!Delta.IsEmpty())
    {
        UE_LOG(LogWorldState, Verbose,
            TEXT("Region %s loaded: %d created, %d removed."),
            *Delta.RegionId.ToString(), Delta.Created.Num(), Delta.Removed.Num());
    }

    OnRegionLoaded.Broadcast(Delta);
}

void UWorldStateSubsystem::RequestRegion(const FPersistenceRegionId& RegionId)
{
    if (!IsOpen() || !RegionId.IsValid())
    {
        return;
    }

    if (FWorldRegionDelta* Existing = Regions.Find(RegionId))
    {
        // Touch it so the cache keeps what is being used rather than what was
        // loaded most recently.
        RegionLastUsed.Add(RegionId, TimeSeconds);
        (void)Existing;
        return;
    }

    if (Requested.Contains(RegionId))
    {
        return;
    }

    Requested.Add(RegionId);

    FPendingLoad Load;
    Load.RegionId = RegionId;
    Load.Result = MakeShared<FWorldRegionDelta, ESPMode::ThreadSafe>();
    Load.bComplete = MakeShared<FThreadSafeBool, ESPMode::ThreadSafe>(false);
    Load.StartSeconds = FPlatformTime::Seconds();

    // The store pointer is captured raw and the task is abandoned rather than
    // cancelled on shutdown, so the store must outlive any load. It does:
    // Deinitialize closes it, and a closed store's LoadRegion returns NotOpen
    // rather than touching a handle. What must never happen is the *subsystem*
    // being captured, which is why the result and the flag are shared pointers.
    IWorldPersistenceStore* StorePtr = Store.Get();

    TSharedPtr<FWorldRegionDelta, ESPMode::ThreadSafe> Result = Load.Result;
    TSharedPtr<FThreadSafeBool, ESPMode::ThreadSafe> Complete = Load.bComplete;

    Loads.Add(MoveTemp(Load));
    ++PendingLoads;

    Async(EAsyncExecution::ThreadPool,
        [StorePtr, RegionId, Result, Complete]()
        {
            if (StorePtr != nullptr && Result.IsValid())
            {
                StorePtr->LoadRegion(RegionId, *Result);
            }

            if (Complete.IsValid())
            {
                *Complete = true;
            }
        });
}

bool UWorldStateSubsystem::LoadRegionBlocking(const FPersistenceRegionId& RegionId)
{
    if (!IsOpen() || !RegionId.IsValid())
    {
        return false;
    }

    if (Regions.Contains(RegionId))
    {
        RegionLastUsed.Add(RegionId, TimeSeconds);
        return true;
    }

    const double Start = FPlatformTime::Seconds();

    FWorldRegionDelta Delta;
    const EWorldPersistenceStatus Status = Store->LoadRegion(RegionId, Delta);

    LastReadMs = (FPlatformTime::Seconds() - Start) * 1000.0;

    if (Status != EWorldPersistenceStatus::Ok && Status != EWorldPersistenceStatus::CorruptRecord)
    {
        return false;
    }

    ApplyLoaded(Delta);

    return true;
}

const FWorldRegionDelta* UWorldStateSubsystem::FindLoadedRegion(
    const FPersistenceRegionId& RegionId) const
{
    return Regions.Find(RegionId);
}

FPersistentEntityId UWorldStateSubsystem::CreateEntity(
    const APlanetActor* Planet,
    const FString& TypeId,
    const FPersistentPlacement& Placement)
{
    FPersistentEntityId Id;

    if (!IsOpen() || Planet == nullptr || !Placement.IsValid())
    {
        return Id;
    }

    const FPlanetSurfaceDescriptor& Surface = Planet->GetPlanetDescriptor();

    const FPersistenceRegionId RegionId =
        FPersistenceRegionId::FromDirection(Surface, Placement.Direction);

    if (!RegionId.IsValid())
    {
        return Id;
    }

    // Entropy for the id. A clock plus a counter plus the placement, so two
    // entities created in the same tick still differ - the counter alone would
    // repeat across a restart, and the clock alone repeats within a tick.
    const uint64 EntropyA = UniverseHash::Hash(
        static_cast<uint64>(FDateTime::UtcNow().GetTicks()), CreationCounter++, 0u);

    const uint64 EntropyB = UniverseHash::Hash(
        RegionId.GetLocalKey(),
        static_cast<uint64>(Placement.Direction.X * 1.0e9),
        static_cast<uint32>(Placement.YawRadians * 1.0e6));

    Id = FPersistentEntityId::CreateNew(EntropyA, EntropyB);

    FWorldEntityRecord Record;
    Record.EntityId = Id;
    Record.RegionId = RegionId;
    Record.TypeId = TypeId;
    Record.Placement = Placement;
    Record.bIsRemoval = false;
    Record.OwnerId = GetLocalOwnerId();
    Record.DataVersion = 1;

    const double Start = FPlatformTime::Seconds();
    const EWorldPersistenceStatus Status = Store->SaveRecord(Record);
    LastWriteMs = (FPlatformTime::Seconds() - Start) * 1000.0;

    if (Status != EWorldPersistenceStatus::Ok)
    {
        UE_LOG(LogWorldState, Error,
            TEXT("Could not persist a %s: %s"), *TypeId, *Store->GetLastError());

        return FPersistentEntityId();
    }

    // Reflected into the cache immediately, so the entity is part of the
    // current world before the write is even acknowledged elsewhere. Waiting
    // for a reload would make a freshly placed building invisible until the
    // player walked away and back.
    if (FWorldRegionDelta* Region = Regions.Find(RegionId))
    {
        Region->Created.Add(Record);
    }

    UE_LOG(LogWorldState, Log,
        TEXT("Created %s %s in region %s (%.2f ms)."),
        *TypeId, *Id.ToHexString(), *RegionId.ToString(), LastWriteMs);

    OnEntityCreated.Broadcast(Record);

    return Id;
}

bool UWorldStateSubsystem::DestroyCreatedEntity(const FPersistentEntityId& EntityId)
{
    if (!IsOpen() || !EntityId.IsValid())
    {
        return false;
    }

    if (EntityId.GetKind() != EPersistentEntityKind::Created)
    {
        // Deleting a procedural entity's row would *restore* it, which is the
        // opposite of what a caller asking to destroy something wants.
        UE_LOG(LogWorldState, Warning,
            TEXT("DestroyCreatedEntity called on a %s id. Use RemoveProceduralEntity."),
            LexToString(EntityId.GetKind()));

        return false;
    }

    const double Start = FPlatformTime::Seconds();
    const EWorldPersistenceStatus Status = Store->DeleteRecord(EntityId);
    LastWriteMs = (FPlatformTime::Seconds() - Start) * 1000.0;

    if (Status != EWorldPersistenceStatus::Ok)
    {
        UE_LOG(LogWorldState, Error,
            TEXT("Could not delete %s: %s"), *EntityId.ToHexString(), *Store->GetLastError());

        return false;
    }

    for (TPair<FPersistenceRegionId, FWorldRegionDelta>& Pair : Regions)
    {
        Pair.Value.Created.RemoveAll([&EntityId](const FWorldEntityRecord& Record)
        {
            return Record.EntityId == EntityId;
        });
    }

    OnEntityRemoved.Broadcast(EntityId);

    return true;
}

bool UWorldStateSubsystem::RemoveProceduralEntity(
    const APlanetActor* Planet,
    const FPersistentEntityId& EntityId,
    const FVector3d& PlanetLocalMeters)
{
    if (!IsOpen() || Planet == nullptr || !EntityId.IsValid())
    {
        return false;
    }

    if (EntityId.GetKind() != EPersistentEntityKind::Procedural)
    {
        UE_LOG(LogWorldState, Warning,
            TEXT("RemoveProceduralEntity called on a %s id."), LexToString(EntityId.GetKind()));

        return false;
    }

    const FPersistenceRegionId RegionId = FPersistenceRegionId::FromPlanetLocal(
        Planet->GetPlanetDescriptor(), PlanetLocalMeters);

    if (!RegionId.IsValid())
    {
        return false;
    }

    // The tombstone carries the id and the region and nothing else. Recording
    // the tree's species, position or scale would make removing a tree cost as
    // much as creating one, and would defeat the point of deriving identity.
    FWorldEntityRecord Record;
    Record.EntityId = EntityId;
    Record.RegionId = RegionId;
    Record.bIsRemoval = true;
    Record.OwnerId = GetLocalOwnerId();
    Record.DataVersion = 1;

    // The direction is stored so a tombstone can be located for debugging and,
    // later, for spatial queries. It is not needed to apply the removal.
    FVector3d Direction;

    if (FPlanetSurfaceQuery::TryGetDirection(PlanetLocalMeters, Direction))
    {
        Record.Placement.Direction = Direction;
    }

    const double Start = FPlatformTime::Seconds();
    const EWorldPersistenceStatus Status = Store->SaveRecord(Record);
    LastWriteMs = (FPlatformTime::Seconds() - Start) * 1000.0;

    if (Status != EWorldPersistenceStatus::Ok)
    {
        UE_LOG(LogWorldState, Error,
            TEXT("Could not persist a removal: %s"), *Store->GetLastError());

        return false;
    }

    if (FWorldRegionDelta* Region = Regions.Find(RegionId))
    {
        // A set, so removing the same thing twice is a no-op rather than two
        // rows - idempotency by data structure rather than by check.
        Region->Removed.Add(EntityId);
    }

    UE_LOG(LogWorldState, Log,
        TEXT("Removed procedural entity %s in region %s (%.2f ms)."),
        *EntityId.ToHexString(), *RegionId.ToString(), LastWriteMs);

    OnEntityRemoved.Broadcast(EntityId);

    return true;
}

bool UWorldStateSubsystem::RestoreProceduralEntity(const FPersistentEntityId& EntityId)
{
    if (!IsOpen() || !EntityId.IsValid())
    {
        return false;
    }

    if (Store->DeleteRecord(EntityId) != EWorldPersistenceStatus::Ok)
    {
        return false;
    }

    for (TPair<FPersistenceRegionId, FWorldRegionDelta>& Pair : Regions)
    {
        Pair.Value.Removed.Remove(EntityId);
    }

    return true;
}

bool UWorldStateSubsystem::SetWorldFact(const FString& Key, const FString& Value)
{
    if (!IsOpen())
    {
        return false;
    }

    return Store->SaveFact(Key, Value) == EWorldPersistenceStatus::Ok;
}

bool UWorldStateSubsystem::GetWorldFact(const FString& Key, FString& OutValue) const
{
    if (!IsOpen())
    {
        return false;
    }

    return Store->LoadFact(Key, OutValue);
}

bool UWorldStateSubsystem::GetWorldFactsWithPrefix(
    const FString& Prefix,
    TArray<TPair<FString, FString>>& OutFacts) const
{
    OutFacts.Reset();

    if (!IsOpen())
    {
        return false;
    }

    return Store->LoadFactsWithPrefix(Prefix, OutFacts) == EWorldPersistenceStatus::Ok;
}

bool UWorldStateSubsystem::IsProceduralEntityRemoved(const FPersistentEntityId& EntityId) const
{
    if (Regions.Num() == 0 || !EntityId.IsValid())
    {
        return false;
    }

    // Scans the loaded regions rather than being given one.
    //
    // The caller - vegetation placement - knows the patch an instance came from
    // but not which persistence region contains it, and computing that per
    // instance would cost a direction normalise and a cube projection per tree.
    // With a bounded cache of a few dozen regions, and almost all of them empty
    // on any real world, a scan of the *non-empty* ones is a handful of set
    // lookups.
    for (const TPair<FPersistenceRegionId, FWorldRegionDelta>& Pair : Regions)
    {
        if (Pair.Value.Removed.Num() > 0 && Pair.Value.Removed.Contains(EntityId))
        {
            return true;
        }
    }

    return false;
}

void UWorldStateSubsystem::EvictIfNeeded()
{
    if (Regions.Num() <= MaxCachedRegions)
    {
        return;
    }

    // Evict the least recently used until the cache is inside its bound. The
    // bound is what makes the memory cost of visiting a planet independent of
    // how long the session has been running.
    while (Regions.Num() > MaxCachedRegions)
    {
        FPersistenceRegionId Oldest;
        double OldestTime = TNumericLimits<double>::Max();
        bool bFound = false;

        for (const TPair<FPersistenceRegionId, FWorldRegionDelta>& Pair : Regions)
        {
            const double* Used = RegionLastUsed.Find(Pair.Key);
            const double When = (Used != nullptr) ? *Used : 0.0;

            if (When < OldestTime)
            {
                OldestTime = When;
                Oldest = Pair.Key;
                bFound = true;
            }
        }

        if (!bFound)
        {
            break;
        }

        Regions.Remove(Oldest);
        RegionLastUsed.Remove(Oldest);

        OnRegionUnloaded.Broadcast(Oldest);
    }
}

void UWorldStateSubsystem::FlushRegionCache()
{
    TArray<FPersistenceRegionId> Keys;
    Regions.GetKeys(Keys);

    Regions.Reset();
    RegionLastUsed.Reset();
    Requested.Reset();

    for (const FPersistenceRegionId& Key : Keys)
    {
        OnRegionUnloaded.Broadcast(Key);
    }
}

int32 UWorldStateSubsystem::GetCreatedEntityCount() const
{
    int32 Total = 0;

    for (const TPair<FPersistenceRegionId, FWorldRegionDelta>& Pair : Regions)
    {
        Total += Pair.Value.Created.Num();
    }

    return Total;
}

int32 UWorldStateSubsystem::GetRemovedEntityCount() const
{
    int32 Total = 0;

    for (const TPair<FPersistenceRegionId, FWorldRegionDelta>& Pair : Regions)
    {
        Total += Pair.Value.Removed.Num();
    }

    return Total;
}

int64 UWorldStateSubsystem::GetStorageRecordCount() const
{
    return Store.IsValid() ? Store->GetRecordCount() : 0;
}

int64 UWorldStateSubsystem::GetStorageSizeBytes() const
{
    return Store.IsValid() ? Store->GetStorageSizeBytes() : 0;
}

FString UWorldStateSubsystem::GetLastError() const
{
    return Store.IsValid() ? Store->GetLastError() : FString();
}
