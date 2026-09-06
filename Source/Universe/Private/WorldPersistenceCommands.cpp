// Copyright Universe Project. All Rights Reserved.

#include "WorldStateSubsystem.h"
#include "UniversePlayerController.h"
#include "StarSystemStreamingSubsystem.h"
#include "PlanetActor.h"
#include "PlanetCharacter.h"
#include "PlanetStructureComponent.h"
#include "PlanetVegetationComponent.h"
#include "PlanetVegetation.h"
#include "PlanetSurfaceQuery.h"
#include "PlanetTerrain.h"
#include "UniverseAnchorComponent.h"
#include "UniverseGameMode.h"
#include "UniverseProbePawn.h"
#include "UniverseWorldSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

/**
 * WorldPersistenceCommands.cpp
 *
 * Building, removing and inspecting - the interactions that exercise
 * persistence, and the tools that make its state visible.
 *
 * These are console commands rather than a build-mode UI. Sprint 005 is
 * explicit that the objective is persistence architecture and not a
 * construction game, and every one of them is also drivable from -ExecCmds,
 * which is what makes the defining demonstration - place, quit, restart, return
 * - reproducible rather than something someone performs by hand once.
 */

DEFINE_LOG_CATEGORY_STATIC(LogWorldPersistCmd, Log, All);

namespace
{
    /**
     * The local player's controller, when it is one that can ask a server.
     *
     * Sprint 007. On a client every one of these commands stops being a direct
     * write and becomes a request: the server owns the world database and a
     * client that wrote to its own would build a private world nobody else can
     * see. Returns null on a server or in single player, where the direct path
     * below is correct and unchanged.
     */
    AUniversePlayerController* GetRequestingController(UWorld* World)
    {
        if (World == nullptr || World->GetNetMode() != NM_Client)
        {
            return nullptr;
        }

        return Cast<AUniversePlayerController>(World->GetFirstPlayerController());
    }
}

namespace
{
    APlanetActor* FindPlanet(UWorld* World)
    {
        if (World == nullptr)
        {
            return nullptr;
        }

        if (const UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
        {
            if (APlanetActor* Planet = Subsystem->GetFramePlanet())
            {
                return Planet;
            }
        }

        // Then the streamer, which owns every planet since Sprint 006 and
        // exists in every net mode. The game mode does not: it is server-only,
        // so a client asking it for the planet gets null and every command that
        // needs one fails with a message that sounds like the world is missing.
        if (const UStarSystemStreamingSubsystem* Streamer =
                World->GetSubsystem<UStarSystemStreamingSubsystem>())
        {
            if (APlanetActor* Planet = Streamer->GetActivePlanetActor())
            {
                return Planet;
            }
        }

        return nullptr;
    }

    /** Where the player is, in universe coordinates. */
    bool TryGetPlayerPosition(UWorld* World, FUniversePosition& OutPosition)
    {
        const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

        if (const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(Pawn))
        {
            OutPosition = Probe->GetUniversePosition();
            return true;
        }

        if (const APlanetCharacter* Character = Cast<APlanetCharacter>(Pawn))
        {
            OutPosition = Character->GetUniversePosition();
            return true;
        }

        return false;
    }

    /**
     * Where the player is aiming, on the ground.
     *
     * A surface query rather than a physics trace, deliberately. A trace only
     * hits terrain that has been streamed *and* cooked for collision, so aiming
     * at a distant hillside would silently fail depending on LOD. The terrain
     * function is exact everywhere and always available.
     */
    bool TryGetAimPoint(
        UWorld* World,
        const APlanetActor* Planet,
        double MaxRangeMeters,
        FVector3d& OutPlanetLocalMeters)
    {
        const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

        if (Pawn == nullptr || Planet == nullptr)
        {
            return false;
        }

        FUniversePosition Position;

        if (!TryGetPlayerPosition(World, Position))
        {
            return false;
        }

        const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Position);

        // March along the view direction until the point is at or below the
        // ground, then take the surface directly below it. Crude, and entirely
        // adequate for placing a beacon a few metres away.
        FVector ViewLocation = FVector::ZeroVector;
        FRotator ViewRotation = FRotator::ZeroRotator;

        if (const APlayerController* Player = Cast<APlayerController>(Pawn->GetController()))
        {
            Player->GetPlayerViewPoint(ViewLocation, ViewRotation);
        }
        else
        {
            ViewRotation = Pawn->GetActorRotation();
        }

        const FVector Forward = ViewRotation.Vector();
        const FVector3d Direction(Forward.X, Forward.Y, Forward.Z);

        constexpr int32 Steps = 64;

        for (int32 Step = 1; Step <= Steps; ++Step)
        {
            const double Distance = (MaxRangeMeters * Step) / Steps;

            const FVector3d Probe(
                Local.X + Direction.X * Distance,
                Local.Y + Direction.Y * Distance,
                Local.Z + Direction.Z * Distance);

            const double Clearance = FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
                Planet->GetPlanetDescriptor(), Planet->GetTerrainSettings(), Probe);

            if (Clearance <= 0.0)
            {
                FVector3d ProbeDirection;

                if (FPlanetSurfaceQuery::TryGetDirection(Probe, ProbeDirection))
                {
                    OutPlanetLocalMeters = FPlanetSurfaceQuery::GetPositionAboveTerrain(
                        Planet->GetPlanetDescriptor(), Planet->GetTerrainSettings(),
                        ProbeDirection, 0.0);

                    return true;
                }
            }
        }

        // Nothing was hit - the player is looking at the sky. Fall back to the
        // ground directly beneath them, which is what somebody typing "build"
        // almost certainly meant.
        FVector3d Below;

        if (FPlanetSurfaceQuery::TryGetDirection(Local, Below))
        {
            OutPlanetLocalMeters = FPlanetSurfaceQuery::GetPositionAboveTerrain(
                Planet->GetPlanetDescriptor(), Planet->GetTerrainSettings(), Below, 0.0);

            return true;
        }

        return false;
    }
}

/**
 * universe.Build [type] [yawDegrees]
 *
 * Places a structure where the player is looking, and persists it immediately.
 */
static void UniverseBuildCommand(const TArray<FString>& Args, UWorld* World)
{
    APlanetActor* Planet = FindPlanet(World);
    UWorldStateSubsystem* WorldState = (World != nullptr)
        ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (Planet == nullptr || WorldState == nullptr || !WorldState->IsUsable())
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("universe.Build: no planet, or world persistence is unavailable."));
        return;
    }

    const FString TypeId = (Args.Num() > 0 && Args[0].Equals(TEXT("foundation"), ESearchCase::IgnoreCase))
        ? WorldEntityTypes::Foundation
        : WorldEntityTypes::Beacon;

    const double YawDegrees = (Args.Num() > 1) ? FCString::Atod(*Args[1]) : 0.0;

    FVector3d AimPoint;

    if (!TryGetAimPoint(World, Planet, 60.0, AimPoint))
    {
        UE_LOG(LogWorldPersistCmd, Warning, TEXT("universe.Build: could not find ground to build on."));
        return;
    }

    FVector3d Direction;

    if (!FPlanetSurfaceQuery::TryGetDirection(AimPoint, Direction))
    {
        return;
    }

    // --- Placement validation ----------------------------------------------
    //
    // Deliberately three rules, as section 87 asks. Each rejects a placement
    // that would produce a structure nobody could use rather than being a
    // design constraint.
    const FPlanetSurfaceSample Ground = FPlanetSurfaceQuery::SampleDirection(
        Planet->GetPlanetDescriptor(), Planet->GetTerrainSettings(), Direction);

    const double SlopeCosine = FVector3d::DotProduct(Ground.NormalUnit, Ground.UpUnit);

    constexpr double MinSlopeCosine = 0.75;

    if (SlopeCosine < MinSlopeCosine)
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("universe.Build: ground is too steep here (slope cosine %.2f, needs %.2f)."),
            SlopeCosine, MinSlopeCosine);
        return;
    }

    const FPlanetEnvironmentDescriptor& Environment = Planet->GetEnvironment();

    if (Environment.HasOcean() && Ground.SurfaceRadiusMeters < Environment.OceanRadiusMeters)
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("universe.Build: that spot is under water (%.0f m deep)."),
            Environment.OceanRadiusMeters - Ground.SurfaceRadiusMeters);
        return;
    }

    if (const UPlanetStructureComponent* Structures = Planet->GetStructureComponent())
    {
        FVector3d ExistingPosition;

        if (Structures->FindNearest(AimPoint, 4.0, ExistingPosition).IsValid())
        {
            UE_LOG(LogWorldPersistCmd, Warning,
                TEXT("universe.Build: something is already there."));
            return;
        }
    }

    FPersistentPlacement Placement;
    Placement.Direction = Direction;
    Placement.HeightAboveTerrainMeters = 0.0;
    Placement.YawRadians = FMath::DegreesToRadians(YawDegrees);
    Placement.Scale = 1.0;

    if (AUniversePlayerController* Requester = GetRequestingController(World))
    {
        Requester->ServerRequestBuild(
            TypeId, FNetPlacement(Placement), Planet->GetPlanetDescriptor().PlanetKey);

        UE_LOG(LogWorldPersistCmd, Log,
            TEXT("Requested %s from the server. It appears when the server agrees."), *TypeId);
        return;
    }

    const FPersistentEntityId Id = WorldState->CreateEntity(Planet, TypeId, Placement);

    if (!Id.IsValid())
    {
        UE_LOG(LogWorldPersistCmd, Error, TEXT("universe.Build: placement failed to persist."));
        return;
    }

    const FPersistenceRegionId Region =
        FPersistenceRegionId::FromDirection(Planet->GetPlanetDescriptor(), Direction);

    UE_LOG(LogWorldPersistCmd, Log,
        TEXT("Built %s  id %s  region %s  elevation %+.0f m"),
        *TypeId, *Id.ToHexString(), *Region.ToString(), Ground.ElevationMeters);
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseBuildCommand(
    TEXT("universe.Build"),
    TEXT("Place a persistent structure where you are looking. e.g. universe.Build beacon 45"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseBuildCommand));

/**
 * universe.Demolish
 *
 * Removes the nearest player-built structure, permanently.
 */
static void UniverseDemolishCommand(UWorld* World)
{
    APlanetActor* Planet = FindPlanet(World);
    UWorldStateSubsystem* WorldState = (World != nullptr)
        ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (Planet == nullptr || WorldState == nullptr)
    {
        return;
    }

    FUniversePosition Position;

    if (!TryGetPlayerPosition(World, Position))
    {
        return;
    }

    const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Position);

    const UPlanetStructureComponent* Structures = Planet->GetStructureComponent();

    if (Structures == nullptr)
    {
        return;
    }

    FVector3d Found;
    const FPersistentEntityId Id = Structures->FindNearest(Local, 40.0, Found);

    if (!Id.IsValid())
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("universe.Demolish: nothing within 40 m."));
        return;
    }

    if (AUniversePlayerController* Requester = GetRequestingController(World))
    {
        Requester->ServerRequestDemolish(
            FNetEntityId(Id), Planet->GetPlanetDescriptor().PlanetKey);

        UE_LOG(LogWorldPersistCmd, Log, TEXT("Requested demolition of %s."), *Id.ToHexString());
        return;
    }

    if (WorldState->DestroyCreatedEntity(Id))
    {
        UE_LOG(LogWorldPersistCmd, Log, TEXT("Demolished %s."), *Id.ToHexString());
    }
}

static FAutoConsoleCommandWithWorld GUniverseDemolishCommand(
    TEXT("universe.Demolish"),
    TEXT("Remove the nearest player-built structure, permanently."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseDemolishCommand));

/**
 * universe.ChopTree [radiusMeters]
 *
 * Removes the nearest procedural tree and records a tombstone.
 *
 * The tree is found by re-running the *placement* for the surrounding patches
 * rather than by tracing against rendered instances. That is the whole point of
 * deriving identity: the generator can tell you what is there without anything
 * being rendered, so a removal is recorded against the thing the generator
 * would produce - which is the same thing it will produce next session.
 */
static void UniverseChopTreeCommand(const TArray<FString>& Args, UWorld* World)
{
    APlanetActor* Planet = FindPlanet(World);
    UWorldStateSubsystem* WorldState = (World != nullptr)
        ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (Planet == nullptr || WorldState == nullptr || !WorldState->IsUsable())
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("universe.ChopTree: no planet, or world persistence is unavailable."));
        return;
    }

    const double RadiusMeters = (Args.Num() > 0) ? FCString::Atod(*Args[0]) : 60.0;

    FUniversePosition Position;

    if (!TryGetPlayerPosition(World, Position))
    {
        return;
    }

    const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Position);

    FVector3d Direction;

    if (!FPlanetSurfaceQuery::TryGetDirection(Local, Direction))
    {
        return;
    }

    const FPlanetSurfaceDescriptor& Surface = Planet->GetPlanetDescriptor();
    const FPlanetEnvironmentDescriptor& Environment = Planet->GetEnvironment();

    // The patches around the player, at the vegetation component's own level.
    const UPlanetVegetationComponent* Vegetation = Planet->GetVegetationComponent();

    const uint8 Level = static_cast<uint8>(
        (Vegetation != nullptr) ? Vegetation->PatchLevel : 13);

    // The same budget the streamer uses.
    //
    // Identity no longer depends on the budget - see PlanetVegetation.cpp - but
    // *presence* still does, and chopping a tree the streamer never places
    // would record a removal that suppresses nothing.
    const int32 Budget = (Vegetation != nullptr) ? Vegetation->MaxInstancesPerPatchLayer : 2000;

    const FPlanetPatchId Centre = FPlanetPatchId::FromDirection(Direction, Level);
    const double PatchSize = Centre.GetApproximateSizeMeters(Surface.RadiusMeters);
    const double PatchAngle = PatchSize / Surface.RadiusMeters;

    FVector3d TangentU;
    FVector3d TangentV;
    FPlanetTerrain::GetTangentBasis(Direction, TangentU, TangentV);

    FPersistentEntityId BestId;
    FVector3d BestPosition;
    double BestDistanceSquared = RadiusMeters * RadiusMeters;
    FString BestArchetype;

    TArray<FVegetationInstance> Instances;
    bool bHitBudget = false;

    // A three-by-three block of patches, which comfortably covers any radius a
    // player can reach on foot.
    for (int32 StepV = -1; StepV <= 1; ++StepV)
    {
        for (int32 StepU = -1; StepU <= 1; ++StepU)
        {
            const FVector3d Probe = FVector3d(
                Direction.X + TangentU.X * StepU * PatchAngle + TangentV.X * StepV * PatchAngle,
                Direction.Y + TangentU.Y * StepU * PatchAngle + TangentV.Y * StepV * PatchAngle,
                Direction.Z + TangentU.Z * StepU * PatchAngle + TangentV.Z * StepV * PatchAngle)
                .GetSafeNormal();

            if (Probe.IsZero())
            {
                continue;
            }

            const FPlanetPatchId PatchId = FPlanetPatchId::FromDirection(Probe, Level);

            FPlanetVegetation::Scatter(
                Surface, Environment, Planet->GetTerrainSettings(), PatchId,
                EVegetationLayer::Canopy, Budget, Instances, bHitBudget);

            for (const FVegetationInstance& Instance : Instances)
            {
                const FVector3d Delta(
                    Instance.PositionMeters.X - Local.X,
                    Instance.PositionMeters.Y - Local.Y,
                    Instance.PositionMeters.Z - Local.Z);

                const double DistanceSquared = Delta.SizeSquared();

                if (DistanceSquared >= BestDistanceSquared)
                {
                    continue;
                }

                const FPersistentEntityId Id = FPersistentEntityId::ForVegetation(
                    Surface.PlanetKey, Instance,
                    Surface.GenerationVersion, Environment.GenerationVersion);

                // Skip anything already removed, so repeated use of the command
                // works through the trees rather than re-removing the nearest.
                if (WorldState->IsProceduralEntityRemoved(Id))
                {
                    continue;
                }

                BestDistanceSquared = DistanceSquared;
                BestId = Id;
                BestPosition = Instance.PositionMeters;
                BestArchetype = LexToString(Instance.Archetype);
            }
        }
    }

    if (!BestId.IsValid())
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("universe.ChopTree: no tree within %.0f m."), RadiusMeters);
        return;
    }

    if (AUniversePlayerController* Requester = GetRequestingController(World))
    {
        Requester->ServerRequestRemoveProcedural(
            FNetEntityId(BestId),
            FVector_NetQuantize100(BestPosition.X, BestPosition.Y, BestPosition.Z),
            Planet->GetPlanetDescriptor().PlanetKey);

        UE_LOG(LogWorldPersistCmd, Log,
            TEXT("Requested removal of %s from the server."), *BestId.ToHexString());
        return;
    }

    if (!WorldState->RemoveProceduralEntity(Planet, BestId, BestPosition))
    {
        UE_LOG(LogWorldPersistCmd, Error, TEXT("universe.ChopTree: removal failed to persist."));
        return;
    }

    UE_LOG(LogWorldPersistCmd, Log,
        TEXT("Removed %s  id %s  %.1f m away"),
        *BestArchetype, *BestId.ToHexString(), FMath::Sqrt(BestDistanceSquared));

    // Force the vegetation around the player to rebuild so the tree disappears
    // now rather than when the region next streams.
    if (UPlanetVegetationComponent* Mutable = Planet->GetVegetationComponent())
    {
        Mutable->RefreshVegetation();
    }
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseChopTreeCommand(
    TEXT("universe.ChopTree"),
    TEXT("Remove the nearest procedural tree, permanently. e.g. universe.ChopTree 40"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseChopTreeCommand));

/**
 * universe.PersistenceInfo
 *
 * Everything the persistence layer currently believes.
 */
static void UniversePersistenceInfoCommand(UWorld* World)
{
    UWorldStateSubsystem* WorldState = (World != nullptr)
        ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (WorldState == nullptr)
    {
        return;
    }

    const FWorldSaveMetadata& Metadata = WorldState->GetMetadata();

    UE_LOG(LogWorldPersistCmd, Log,
        TEXT("World state: %s  status %s"),
        WorldState->IsOpen() ? TEXT("open") : TEXT("CLOSED"),
        LexToString(WorldState->GetOpenStatus()));

    UE_LOG(LogWorldPersistCmd, Log,
        TEXT("  Universe \"%s\" (0x%016llX)  terrain v%u  environment v%u  schema v%d"),
        *Metadata.UniverseSeedText,
        static_cast<unsigned long long>(Metadata.UniverseSeedValue),
        Metadata.TerrainVersion, Metadata.EnvironmentVersion, Metadata.SchemaVersion);

    UE_LOG(LogWorldPersistCmd, Log,
        TEXT("  Storage: %lld records, %.2f KB.  Cache: %d regions, %d created, %d removed, %d loading."),
        static_cast<long long>(WorldState->GetStorageRecordCount()),
        WorldState->GetStorageSizeBytes() / 1024.0,
        WorldState->GetLoadedRegionCount(),
        WorldState->GetCreatedEntityCount(),
        WorldState->GetRemovedEntityCount(),
        WorldState->GetPendingLoadCount());

    UE_LOG(LogWorldPersistCmd, Log,
        TEXT("  Latency: last read %.2f ms, last write %.2f ms."),
        WorldState->GetLastReadMilliseconds(),
        WorldState->GetLastWriteMilliseconds());

    if (WorldState->HasGenerationVersionMismatch())
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("  GENERATION VERSION MISMATCH: %s"), *WorldState->GetLastError());
    }

    // The region the player is standing in, and what is in it.
    if (const APlanetActor* Planet = FindPlanet(World))
    {
        FUniversePosition Position;

        if (TryGetPlayerPosition(World, Position))
        {
            const FPersistenceRegionId Region = FPersistenceRegionId::FromPlanetLocal(
                Planet->GetPlanetDescriptor(), Planet->UniverseToPlanetLocalMeters(Position));

            UE_LOG(LogWorldPersistCmd, Log,
                TEXT("  Here: region %s (%.0f m across)"),
                *Region.ToString(), Region.GetSizeMeters(Planet->GetPlanetDescriptor().RadiusMeters));

            if (const FWorldRegionDelta* Delta = WorldState->FindLoadedRegion(Region))
            {
                UE_LOG(LogWorldPersistCmd, Log,
                    TEXT("    %d created, %d removed"), Delta->Created.Num(), Delta->Removed.Num());

                for (const FWorldEntityRecord& Record : Delta->Created)
                {
                    UE_LOG(LogWorldPersistCmd, Log,
                        TEXT("      + %s  %s"), *Record.TypeId, *Record.EntityId.ToHexString());
                }

                for (const FPersistentEntityId& Removed : Delta->Removed)
                {
                    UE_LOG(LogWorldPersistCmd, Log,
                        TEXT("      - %s"), *Removed.ToHexString());
                }
            }
            else
            {
                UE_LOG(LogWorldPersistCmd, Log, TEXT("    (region not loaded)"));
            }
        }
    }
}

static FAutoConsoleCommandWithWorld GUniversePersistenceInfoCommand(
    TEXT("universe.PersistenceInfo"),
    TEXT("Log world persistence state, storage size and the current region's contents."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniversePersistenceInfoCommand));

/**
 * universe.PersistenceReset [planet]
 *
 * Deletes persistent modifications. Development tooling.
 *
 * Requires an explicit argument, so that typing the command by itself does not
 * silently destroy a test world - section 57's "safeguards for destructive
 * commands", implemented as the cheapest possible one that actually works.
 */
static void UniversePersistenceResetCommand(const TArray<FString>& Args, UWorld* World)
{
    UWorldStateSubsystem* WorldState = (World != nullptr)
        ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (WorldState == nullptr || !WorldState->IsOpen())
    {
        return;
    }

    IWorldPersistenceStore* Store = WorldState->GetStore();

    if (Store == nullptr)
    {
        return;
    }

    if (Args.Num() == 0)
    {
        UE_LOG(LogWorldPersistCmd, Warning,
            TEXT("universe.PersistenceReset needs an argument: 'planet' for this planet, ")
            TEXT("or 'all' to erase every modification in this universe."));
        return;
    }

    if (Args[0].Equals(TEXT("planet"), ESearchCase::IgnoreCase))
    {
        const APlanetActor* Planet = FindPlanet(World);

        if (Planet == nullptr)
        {
            return;
        }

        const uint64 PlanetKey = Planet->GetPlanetDescriptor().PlanetKey;

        if (Store->DeletePlanet(PlanetKey) == EWorldPersistenceStatus::Ok)
        {
            WorldState->FlushRegionCache();

            UE_LOG(LogWorldPersistCmd, Log,
                TEXT("Erased every modification on planet 0x%016llX."),
                static_cast<unsigned long long>(PlanetKey));
        }
    }
    else if (Args[0].Equals(TEXT("all"), ESearchCase::IgnoreCase))
    {
        if (Store->DeleteAll() == EWorldPersistenceStatus::Ok)
        {
            WorldState->FlushRegionCache();

            UE_LOG(LogWorldPersistCmd, Log,
                TEXT("Erased every modification in this universe."));
        }
    }
    else
    {
        UE_LOG(LogWorldPersistCmd, Warning, TEXT("Unknown argument \"%s\"."), *Args[0]);
    }
}

static FAutoConsoleCommandWithWorldAndArgs GUniversePersistenceResetCommand(
    TEXT("universe.PersistenceReset"),
    TEXT("Development: erase persistent modifications. Requires 'planet' or 'all'."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniversePersistenceResetCommand));

/**
 * universe.PersistenceStress <count>
 *
 * Places many structures around the player, to measure write and query cost.
 */
static void UniversePersistenceStressCommand(const TArray<FString>& Args, UWorld* World)
{
    APlanetActor* Planet = FindPlanet(World);
    UWorldStateSubsystem* WorldState = (World != nullptr)
        ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;

    if (Planet == nullptr || WorldState == nullptr || !WorldState->IsUsable())
    {
        return;
    }

    const int32 Count = (Args.Num() > 0) ? FMath::Clamp(FCString::Atoi(*Args[0]), 1, 20000) : 100;

    const int64 SizeBefore = WorldState->GetStorageSizeBytes();
    const int64 RecordsBefore = WorldState->GetStorageRecordCount();

    const double Start = FPlatformTime::Seconds();

    int32 Placed = 0;

    for (int32 Index = 0; Index < Count; ++Index)
    {
        // Spread over the whole planet rather than clustered, so the region
        // index is exercised rather than one region being filled.
        const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, Count);

        FPersistentPlacement Placement;
        Placement.Direction = Direction;
        Placement.HeightAboveTerrainMeters = 0.0;
        Placement.YawRadians = 0.0;
        Placement.Scale = 1.0;

        if (WorldState->CreateEntity(Planet, WorldEntityTypes::Beacon, Placement).IsValid())
        {
            ++Placed;
        }
    }

    const double Elapsed = FPlatformTime::Seconds() - Start;

    const int64 SizeAfter = WorldState->GetStorageSizeBytes();
    const int64 RecordsAfter = WorldState->GetStorageRecordCount();

    UE_LOG(LogWorldPersistCmd, Log,
        TEXT("Stress: placed %d structures in %.3f s (%.3f ms each). ")
        TEXT("Records %lld -> %lld. Storage %.1f KB -> %.1f KB (%.1f bytes per record)."),
        Placed, Elapsed, (Elapsed * 1000.0) / FMath::Max(Placed, 1),
        static_cast<long long>(RecordsBefore), static_cast<long long>(RecordsAfter),
        SizeBefore / 1024.0, SizeAfter / 1024.0,
        (RecordsAfter > RecordsBefore)
            ? static_cast<double>(SizeAfter - SizeBefore) / (RecordsAfter - RecordsBefore)
            : 0.0);

    // And a region read, which is the query that matters at runtime.
    FUniversePosition Position;

    if (TryGetPlayerPosition(World, Position))
    {
        const FPersistenceRegionId Region = FPersistenceRegionId::FromPlanetLocal(
            Planet->GetPlanetDescriptor(), Planet->UniverseToPlanetLocalMeters(Position));

        WorldState->FlushRegionCache();

        const double ReadStart = FPlatformTime::Seconds();
        WorldState->LoadRegionBlocking(Region);
        const double ReadMs = (FPlatformTime::Seconds() - ReadStart) * 1000.0;

        UE_LOG(LogWorldPersistCmd, Log,
            TEXT("Region read after %lld records: %.3f ms."),
            static_cast<long long>(RecordsAfter), ReadMs);
    }
}

static FAutoConsoleCommandWithWorldAndArgs GUniversePersistenceStressCommand(
    TEXT("universe.PersistenceStress"),
    TEXT("Development: place N structures and report write and query cost. e.g. universe.PersistenceStress 1000"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniversePersistenceStressCommand));
