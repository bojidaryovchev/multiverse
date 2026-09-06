// Copyright Universe Project. All Rights Reserved.

#include "PlanetTerrainComponent.h"
#include "PlanetSurfaceQuery.h"
#include "CubeSphere.h"
#include "PlanetMeshBackend.h"

#include "Async/Async.h"
#include "GameFramework/Actor.h"
#include "HAL/PlatformTime.h"
#include "Materials/MaterialInterface.h"
#include "HAL/IConsoleManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanetTerrain, Log, All);

namespace
{
    /**
     * Seconds between terrain state log lines (0 = off).
     *
     * The same idea as universe.LogStateInterval for the probe: streaming
     * behaviour has to be observable in a headless run, or the only way to
     * check it is to sit and watch, which cannot be compared between runs.
     */
    static TAutoConsoleVariable<float> CVarTerrainLogInterval(
        TEXT("universe.TerrainLogInterval"),
        0.0f,
        TEXT("Seconds between terrain streaming state log lines (0 = off)."),
        ECVF_Cheat);
}

UPlanetTerrainComponent::UPlanetTerrainComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
    PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

void UPlanetTerrainComponent::BeginPlay()
{
    Super::BeginPlay();

    Shared = MakeShared<FPlanetTerrainShared, ESPMode::ThreadSafe>();

    if (Backend == nullptr)
    {
        Backend = NewObject<UPlanetMeshBackend_ProceduralMesh>(this);
    }
    Backend->Initialise(GetOwner(), TerrainMaterial);
}

void UPlanetTerrainComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Tell any task still running that nobody is listening. The shared block
    // itself stays alive as long as a task holds a reference, so a worker
    // finishing after teardown writes into memory that is still valid and then
    // simply drops the result.
    if (Shared.IsValid())
    {
        Shared->bShutdown = true;
    }

    if (Backend != nullptr)
    {
        Backend->ReleaseAll();
    }
    Patches.Reset();

    Super::EndPlay(EndPlayReason);
}

void UPlanetTerrainComponent::SetPlanet(const FPlanetSurfaceDescriptor& InPlanet, const FPlanetTerrainSettings& InSettings)
{
    Planet = InPlanet;
    TerrainSettings = InSettings;
    ResetTerrain();
}

void UPlanetTerrainComponent::SetObserverPositionMeters(const FVector3d& InObserverMeters)
{
    ObserverMeters = InObserverMeters;
}

void UPlanetTerrainComponent::ResetTerrain()
{
    if (Backend != nullptr)
    {
        for (TPair<uint64, FTrackedPatch>& Pair : Patches)
        {
            if (Pair.Value.Slot != INDEX_NONE)
            {
                Backend->ReleaseSlot(Pair.Value.Slot);
            }
        }
    }

    Patches.Reset();
    Selector.Reset();
    TimeSinceSelection = SelectionIntervalSeconds;  // select on the next tick

    // In-flight tasks are not cancelled - they cannot be - but their results
    // will not match any tracked patch and will be discarded on arrival.
}

void UPlanetTerrainComponent::RequestGeneration(FTrackedPatch& Patch)
{
    Patch.Serial = NextSerial++;
    Patch.State = EPlanetPatchState::Queued;

    // Everything the worker needs is copied by value, and the only shared
    // reference is the shared block. Nothing captured here has a lifetime tied
    // to this component.
    const FPlanetSurfaceDescriptor PlanetCopy = Planet;
    const FPlanetTerrainSettings SettingsCopy = TerrainSettings;
    const FPlanetPatchId PatchIdCopy = Patch.PatchId;
    const uint64 SerialCopy = Patch.Serial;

    TSharedPtr<FPlanetTerrainShared, ESPMode::ThreadSafe> SharedCopy = Shared;
    if (!SharedCopy.IsValid())
    {
        return;
    }

    SharedCopy->InFlightCount.Increment();
    Patch.State = EPlanetPatchState::Generating;

    Async(EAsyncExecution::ThreadPool,
        [PlanetCopy, SettingsCopy, PatchIdCopy, SerialCopy, SharedCopy]()
        {
            const double StartSeconds = FPlatformTime::Seconds();

            TSharedPtr<FPlanetPatchMesh, ESPMode::ThreadSafe> Mesh =
                MakeShared<FPlanetPatchMesh, ESPMode::ThreadSafe>();

            FPlanetPatchMeshBuilder::Build(PlanetCopy, SettingsCopy, PatchIdCopy, /*bGenerateSkirt=*/true, *Mesh);
            Mesh->GenerationSerial = SerialCopy;

            Mesh->GenerationMilliseconds = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

            // Validate on the worker, where a failure can be reported against
            // the patch that caused it rather than surfacing later as a broken
            // bounding box on some unrelated component.
#if !UE_BUILD_SHIPPING
            FString Error;
            if (!Mesh->Validate(Error))
            {
                UE_LOG(LogPlanetTerrain, Error, TEXT("Invalid patch %s: %s"),
                    *PatchIdCopy.ToDebugString(), *Error);
                Mesh->Reset();
            }
#endif

            if (SharedCopy->bShutdown)
            {
                // The component is gone; drop the result rather than queueing
                // work nobody will drain.
                SharedCopy->InFlightCount.Decrement();
                return;
            }

            // Timing is carried on the result and totalled by the consumer -
            // accumulating here would be a data race across workers.
            SharedCopy->CompletedMeshes.Enqueue(Mesh);
            SharedCopy->InFlightCount.Decrement();
        });
}

void UPlanetTerrainComponent::RunSelection()
{
    const double StartSeconds = FPlatformTime::Seconds();

    FPlanetLodContext Context;
    Context.ObserverPositionMeters = ObserverMeters;
    Context.SplitPixelError = SplitPixelError;
    Context.MaxLevel = static_cast<uint8>(FMath::Clamp(MaxLodLevel, 0, static_cast<int32>(FPlanetPatchId::MaxLevel)));
    Context.MaxSelectedPatches = MaxSelectedPatches;

    Selector.Select(Planet, TerrainSettings, Context, SelectedScratch, SelectionStats);

    // Prewarm pass. A second, stateless selection around the predicted arrival
    // point, unioned into the first.
    //
    // Stateless deliberately: FPlanetQuadtreeSelector carries the hysteresis
    // history, and running the prewarm through the same selector would let a
    // point the observer is not at influence which patches the point they *are*
    // at considers already split. Two observers sharing one hysteresis state
    // would flicker against each other. The prewarm pass therefore uses the
    // plain FPlanetQuadtree, accepts that its own choices may oscillate
    // slightly, and contributes only membership - never a split decision for
    // the real observer.
    if (bHasPrewarm)
    {
        FPlanetLodContext PrewarmContext = Context;
        PrewarmContext.ObserverPositionMeters = PrewarmMeters;

        // Capped well below the main budget. Prewarm is insurance, and if it
        // could consume the whole patch budget it would degrade the view the
        // player actually has in order to prepare one they might not reach.
        PrewarmContext.MaxSelectedPatches = FMath::Max(1, MaxSelectedPatches / 4);

        FPlanetSelectionStats PrewarmStats;
        FPlanetQuadtree::SelectPatches(
            Planet, TerrainSettings, PrewarmContext, PrewarmScratch, PrewarmStats);

        for (const FPlanetSelectedPatch& Patch : PrewarmScratch)
        {
            SelectedScratch.Add(Patch);
        }

        SelectionStats.PrewarmSelected = PrewarmScratch.Num();
    }

    // Mark everything currently tracked as unselected, then re-mark what the
    // quadtree chose. Anything still unmarked afterwards gets released.
    for (TPair<uint64, FTrackedPatch>& Pair : Patches)
    {
        if (Pair.Value.State != EPlanetPatchState::Releasing)
        {
            Pair.Value.bWantsCollision = false;
        }
    }

    TSet<uint64> SelectedKeys;
    SelectedKeys.Reserve(SelectedScratch.Num());

    for (const FPlanetSelectedPatch& Selected : SelectedScratch)
    {
        const uint64 Key = MakePlanetPatchKey(Selected.PatchId);
        SelectedKeys.Add(Key);

        FTrackedPatch* Existing = Patches.Find(Key);
        if (Existing == nullptr)
        {
            FTrackedPatch NewPatch;
            NewPatch.PatchId = Selected.PatchId;
            NewPatch.State = EPlanetPatchState::Needed;
            NewPatch.DistanceMeters = Selected.DistanceMeters;
            NewPatch.bWantsCollision = Selected.DistanceMeters <= static_cast<double>(CollisionRadiusMeters);
            Patches.Add(Key, NewPatch);
        }
        else
        {
            Existing->DistanceMeters = Selected.DistanceMeters;
            Existing->bWantsCollision = Selected.DistanceMeters <= static_cast<double>(CollisionRadiusMeters);
        }
    }

    // Release anything no longer selected.
    TArray<uint64> ToRemove;
    for (TPair<uint64, FTrackedPatch>& Pair : Patches)
    {
        if (!SelectedKeys.Contains(Pair.Key))
        {
            if (Pair.Value.Slot != INDEX_NONE && Backend != nullptr)
            {
                Backend->ReleaseSlot(Pair.Value.Slot);
                Pair.Value.Slot = INDEX_NONE;
                ++Stats.TotalReleased;
            }
            ToRemove.Add(Pair.Key);
        }
    }

    for (uint64 Key : ToRemove)
    {
        Patches.Remove(Key);
    }

    Stats.LastSelectionMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    Stats.SelectedPatches = SelectedScratch.Num();
    Stats.DeepestLevel = SelectionStats.DeepestLevel;
    Stats.HorizonCulled = SelectionStats.HorizonCulled;
    Stats.BalancingSplits = SelectionStats.BalancingSplits;
}

void UPlanetTerrainComponent::PumpGeneration()
{
    // Nearest first: the patches the observer is about to look at matter more
    // than the ones on the horizon, and with a bounded in-flight count the
    // order decides what gets built when moving fast.
    if (!Shared.IsValid())
    {
        return;
    }

    const int32 Budget = MaxConcurrentGenerations - Shared->InFlightCount.GetValue();
    if (Budget <= 0)
    {
        return;
    }

    TArray<TPair<double, uint64>> Candidates;
    for (const TPair<uint64, FTrackedPatch>& Pair : Patches)
    {
        if (Pair.Value.State == EPlanetPatchState::Needed)
        {
            Candidates.Add(TPair<double, uint64>(Pair.Value.DistanceMeters, Pair.Key));
        }
    }

    if (Candidates.Num() == 0)
    {
        return;
    }

    Candidates.Sort([](const TPair<double, uint64>& A, const TPair<double, uint64>& B)
    {
        return A.Key < B.Key;
    });

    // Collision patches first, up to the reservation, then nearest-first for
    // the rest.
    //
    // Distance alone is not enough, and the failure it produces is specific: a
    // descending craft selects hundreds of distant visual patches and a handful
    // of near ones needing collision. Some of the visual patches are nearer
    // than some collision patches on the far side of the safety radius, so a
    // pure distance sort interleaves them, and the ground under the player
    // finishes last. Reserving part of the budget for collision decouples the
    // two: scenery can never starve the floor.
    int32 Launched = 0;
    const int32 CollisionBudget = FMath::Min(Budget, FMath::Max(0, ReservedCollisionGenerations));

    for (int32 Index = 0; Index < Candidates.Num() && Launched < CollisionBudget; ++Index)
    {
        FTrackedPatch* Patch = Patches.Find(Candidates[Index].Value);

        if (Patch != nullptr && Patch->bWantsCollision && Patch->State == EPlanetPatchState::Needed)
        {
            RequestGeneration(*Patch);
            ++Launched;
        }
    }

    for (int32 Index = 0; Index < Candidates.Num() && Launched < Budget; ++Index)
    {
        FTrackedPatch* Patch = Patches.Find(Candidates[Index].Value);

        if (Patch != nullptr && Patch->State == EPlanetPatchState::Needed)
        {
            RequestGeneration(*Patch);
            ++Launched;
        }
    }
}

void UPlanetTerrainComponent::DrainResults(double TimeBudgetSeconds)
{
    const double StartSeconds = FPlatformTime::Seconds();
    int32 Uploaded = 0;

    if (!Shared.IsValid())
    {
        return;
    }

    TSharedPtr<FPlanetPatchMesh, ESPMode::ThreadSafe> Mesh;
    while (Uploaded < MaxUploadsPerFrame && Shared->CompletedMeshes.Dequeue(Mesh))
    {
        if (!Mesh.IsValid())
        {
            continue;
        }

        const uint64 Key = MakePlanetPatchKey(Mesh->PatchId);
        FTrackedPatch* Patch = Patches.Find(Key);

        // The patch may have stopped being relevant while the worker ran, or a
        // newer request may have superseded this one. Either way the result is
        // stale and gets dropped - this is what keeps a fast traverse from
        // spending its time finishing terrain nobody will see.
        if (Patch == nullptr || Patch->Serial != Mesh->GenerationSerial)
        {
            ++Stats.DiscardedResults;
            continue;
        }

        // Totalled here, on the one thread that owns these counters.
        GenerationMsAccumulator += Mesh->GenerationMilliseconds;
        ++GenerationSamples;

        if (Mesh->IsEmpty())
        {
            Patch->State = EPlanetPatchState::Needed;
            continue;
        }

        if (Patch->Slot == INDEX_NONE && Backend != nullptr)
        {
            Patch->Slot = Backend->AcquireSlot();
        }

        if (Patch->Slot != INDEX_NONE && Backend != nullptr)
        {
            Backend->UpdateSlot(Patch->Slot, *Mesh, Patch->bWantsCollision);
            Patch->State = EPlanetPatchState::Visible;
            Patch->bHasCollision = Patch->bWantsCollision;
            Patch->TriangleCount = Mesh->GetTriangleCount();
            Patch->VertexCount = Mesh->GetVertexCount();
            ++Stats.TotalGenerated;
            ++Uploaded;
        }

        // Uploading is game-thread work; a burst of completions must not blow
        // the frame, so the budget is time as well as count.
        if ((FPlatformTime::Seconds() - StartSeconds) > TimeBudgetSeconds)
        {
            break;
        }
    }

    Stats.LastUploadMs = static_cast<float>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
}

void UPlanetTerrainComponent::ReleaseUnselected()
{
    // Selection already removed unselected patches; this refreshes counters.
    int32 Visible = 0;
    int32 Queued = 0;
    int32 Generating = 0;
    int32 Ready = 0;
    int32 Triangles = 0;
    int32 Vertices = 0;
    int32 CollisionCount = 0;

    for (const TPair<uint64, FTrackedPatch>& Pair : Patches)
    {
        switch (Pair.Value.State)
        {
        case EPlanetPatchState::Visible:    ++Visible; break;
        case EPlanetPatchState::Queued:     ++Queued; break;
        case EPlanetPatchState::Generating: ++Generating; break;
        case EPlanetPatchState::Ready:      ++Ready; break;
        default: break;
        }

        if (Pair.Value.State == EPlanetPatchState::Visible)
        {
            Triangles += Pair.Value.TriangleCount;
            Vertices += Pair.Value.VertexCount;
            CollisionCount += Pair.Value.bHasCollision ? 1 : 0;
        }
    }

    Stats.VisiblePatches = Visible;
    Stats.QueuedPatches = Queued;
    // The in-flight counter alone, not added to the Generating state count -
    // a patch in state Generating IS an in-flight task, so summing them
    // reported double the real figure during the stress run.
    (void)Generating;
    Stats.GeneratingPatches = Shared.IsValid() ? Shared->InFlightCount.GetValue() : 0;
    Stats.ReadyPatches = Ready;
    Stats.TriangleCount = Triangles;
    Stats.VertexCount = Vertices;
    Stats.CollisionPatches = CollisionCount;
    Stats.PooledSlots = (Backend != nullptr) ? Backend->GetPooledSlotCount() : 0;

    Stats.AverageGenerationMs = (GenerationSamples > 0)
        ? static_cast<float>(GenerationMsAccumulator / static_cast<double>(GenerationSamples))
        : 0.0f;

    const double ObserverRadius = ObserverMeters.Size();
    Stats.ObserverAltitudeMeters = static_cast<float>(ObserverRadius - Planet.RadiusMeters);
}

void UPlanetTerrainComponent::TickComponent(
    float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!Planet.IsValid() || !TerrainSettings.IsValid() || Backend == nullptr)
    {
        return;
    }

    // Re-selecting every frame is wasted work: the selection changes on the
    // scale of the observer moving a patch-width, not every 16 ms.
    TimeSinceSelection += DeltaTime;
    if (TimeSinceSelection >= SelectionIntervalSeconds)
    {
        TimeSinceSelection = 0.0f;
        RunSelection();
    }

    PumpGeneration();

    // A couple of milliseconds of upload per frame. Enough to keep up with the
    // generator, small enough not to be the reason a frame is late.
    DrainResults(0.002);

    ReleaseUnselected();

    // --- Periodic state log ------------------------------------------------
    const double LogInterval = static_cast<double>(CVarTerrainLogInterval.GetValueOnGameThread());
    if (LogInterval > 0.0)
    {
        TimeSinceStatsLog += DeltaTime;
        if (TimeSinceStatsLog >= LogInterval)
        {
            TimeSinceStatsLog = 0.0f;

            UE_LOG(LogPlanetTerrain, Log,
                TEXT("TERRAIN alt=%.1fm selected=%d visible=%d gen=%d pooled=%d collision=%d ")
                TEXT("tris=%d verts=%d deepest=L%d culled=%d balance=%d discarded=%d ")
                TEXT("built=%d released=%d sel=%.2fms upload=%.2fms avgGen=%.2fms"),
                Stats.ObserverAltitudeMeters,
                Stats.SelectedPatches, Stats.VisiblePatches, Stats.GeneratingPatches,
                Stats.PooledSlots, Stats.CollisionPatches,
                Stats.TriangleCount, Stats.VertexCount,
                Stats.DeepestLevel, Stats.HorizonCulled, Stats.BalancingSplits,
                Stats.DiscardedResults, Stats.TotalGenerated, Stats.TotalReleased,
                Stats.LastSelectionMs, Stats.LastUploadMs, Stats.AverageGenerationMs);
        }
    }
}

void UPlanetTerrainComponent::SetPrewarmPositionMeters(const FVector3d& InPrewarmMeters)
{
    PrewarmMeters = InPrewarmMeters;
    bHasPrewarm = !InPrewarmMeters.IsZero();
}

bool UPlanetTerrainComponent::HasCollisionAt(const FVector3d& PlanetLocalMeters) const
{
    FVector3d Direction;

    if (!FPlanetSurfaceQuery::TryGetDirection(PlanetLocalMeters, Direction))
    {
        return false;
    }

    CubeSphere::EFace Face = CubeSphere::EFace::PosX;
    double U = 0.0;
    double V = 0.0;
    CubeSphere::DirectionToFaceUV(Direction, Face, U, V);

    // Ask the patches themselves rather than recomputing which patch ought to
    // contain the point. Whether collision exists is a fact about the streamer's
    // current state, not about the quadtree's ideal one, and deriving it from
    // the ideal would confidently report collision on a patch that has been
    // selected but not yet cooked - which is precisely the window in which a
    // character falls through the world.
    for (const TPair<uint64, FTrackedPatch>& Pair : Patches)
    {
        const FTrackedPatch& Patch = Pair.Value;

        if (!Patch.bHasCollision || Patch.State != EPlanetPatchState::Visible)
        {
            continue;
        }

        if (Patch.PatchId.GetFace() != Face)
        {
            continue;
        }

        double MinU = 0.0;
        double MinV = 0.0;
        double MaxU = 0.0;
        double MaxV = 0.0;
        Patch.PatchId.GetUVBounds(MinU, MinV, MaxU, MaxV);

        if (U >= MinU && U <= MaxU && V >= MinV && V <= MaxV)
        {
            return true;
        }
    }

    return false;
}
