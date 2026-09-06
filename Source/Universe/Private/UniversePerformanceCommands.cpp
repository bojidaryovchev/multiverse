// Copyright Universe Project. All Rights Reserved.

#include "GoldenPathSubsystem.h"
#include "PlanetActor.h"
#include "PlanetTerrainComponent.h"
#include "PlanetVegetationComponent.h"
#include "StarSystemStreamingSubsystem.h"
#include "UniverseProbePawn.h"
#include "UniverseWorldSubsystem.h"
#include "WorldStateSubsystem.h"

#include "Engine/World.h"
#include "Misc/App.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMemory.h"
#include "Kismet/GameplayStatics.h"

/**
 * UniversePerformanceCommands.cpp
 *
 * Everything a performance baseline needs, in one line each.
 *
 * Gathered into one command rather than left scattered across five because a
 * baseline is only useful if two of them can be compared, and two readings
 * taken from different commands at different moments are not comparable. This
 * samples everything at once and prints it in a fixed order, so two runs diff
 * cleanly.
 */

DEFINE_LOG_CATEGORY_STATIC(LogUniversePerf, Log, All);

static void UniversePerfCommand(UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    UE_LOG(LogUniversePerf, Log, TEXT("=== Performance sample ==="));

    // --- Frame ----------------------------------------------------------------
    //
    // From the world's own delta rather than from stat unit, so this works in a
    // -nullrhi run where there is no renderer to ask.
    //
    // Under -benchmark the delta is *fixed* by -fps, so this reads exactly
    // 33.33 ms however hard the machine is working. Said out loud rather than
    // left as a trap: a baseline that quotes a benchmark run's frame time is
    // quoting the command line, not the engine.
    const double DeltaMs = World->GetDeltaSeconds() * 1000.0;

    const bool bFixedStep = FApp::IsBenchmarking();

    UE_LOG(LogUniversePerf, Log,
        TEXT("  Frame        : %.2f ms (%.0f fps)%s"),
        DeltaMs, (DeltaMs > 0.0) ? 1000.0 / DeltaMs : 0.0,
        bFixedStep ? TEXT("   [fixed by -benchmark; not a measurement]") : TEXT(""));

    // --- Memory ---------------------------------------------------------------
    const FPlatformMemoryStats Memory = FPlatformMemory::GetStats();

    UE_LOG(LogUniversePerf, Log,
        TEXT("  Memory       : %.0f MB used, %.0f MB peak"),
        Memory.UsedPhysical / (1024.0 * 1024.0),
        Memory.PeakUsedPhysical / (1024.0 * 1024.0));

    // --- Where the player is --------------------------------------------------
    if (const AUniverseProbePawn* Probe =
            Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
    {
        UE_LOG(LogUniversePerf, Log,
            TEXT("  Ship         : %s, %s, %.0f m up"),
            *Probe->GetTravelModeName(),
            Probe->IsLanded() ? TEXT("landed") : TEXT("flying"),
            Probe->GetAltitudeAboveTerrainMeters());
    }

    // --- Origin rebasing ------------------------------------------------------
    if (const UUniverseWorldSubsystem* Universe = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        UE_LOG(LogUniversePerf, Log,
            TEXT("  Rebases      : %d this session, %d anchors, frame %s"),
            Universe->GetRebaseCount(),
            Universe->GetRegisteredAnchorCount(),
            Universe->IsInPlanetaryFrame() ? TEXT("planetary") : TEXT("interstellar"));
    }

    // --- System streaming -----------------------------------------------------
    if (const UStarSystemStreamingSubsystem* Streamer =
            World->GetSubsystem<UStarSystemStreamingSubsystem>())
    {
        TArray<int32> Counts;
        Streamer->GetStateCounts(Counts);

        UE_LOG(LogUniversePerf, Log,
            TEXT("  Systems      : %d tracked, %d generated, %d transitions"),
            Streamer->GetTrackedSystems().Num(),
            Streamer->GetGeneratedSystemCount(),
            Streamer->GetTransitionCount());

        // --- Terrain ----------------------------------------------------------
        if (const APlanetActor* Planet = Streamer->GetActivePlanetActor())
        {
            if (const UPlanetTerrainComponent* Terrain = Planet->GetTerrainComponent())
            {
                const FPlanetTerrainStats& Stats = Terrain->GetStats();

                UE_LOG(LogUniversePerf, Log,
                    TEXT("  Terrain      : %d visible of %d selected, %d with collision, level %d"),
                    Stats.VisiblePatches, Stats.SelectedPatches,
                    Stats.CollisionPatches, Stats.DeepestLevel);

                UE_LOG(LogUniversePerf, Log,
                    TEXT("  Terrain cost : select %.2f ms, upload %.2f ms, generate %.2f ms avg"),
                    Stats.LastSelectionMs, Stats.LastUploadMs, Stats.AverageGenerationMs);

                UE_LOG(LogUniversePerf, Log,
                    TEXT("  Terrain mesh : %d triangles, %d vertices, %d pooled slots"),
                    Stats.TriangleCount, Stats.VertexCount, Stats.PooledSlots);

                UE_LOG(LogUniversePerf, Log,
                    TEXT("  Terrain churn: %d generated, %d released, %d discarded"),
                    Stats.TotalGenerated, Stats.TotalReleased, Stats.DiscardedResults);
            }

            if (const UPlanetVegetationComponent* Vegetation = Planet->GetVegetationComponent())
            {
                UE_LOG(LogUniversePerf, Log,
                    TEXT("  Vegetation   : %d instances across %d patches"),
                    Vegetation->GetInstanceCount(), Vegetation->GetActivePatchCount());
            }
        }
    }

    // --- Persistence ----------------------------------------------------------
    if (const UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
    {
        UE_LOG(LogUniversePerf, Log,
            TEXT("  Persistence  : %lld records, %.2f KB, %d regions cached"),
            WorldState->GetStorageRecordCount(),
            WorldState->GetStorageSizeBytes() / 1024.0,
            WorldState->GetLoadedRegionCount());

        UE_LOG(LogUniversePerf, Log,
            TEXT("  Storage cost : read %.3f ms, write %.3f ms"),
            WorldState->GetLastReadMilliseconds(),
            WorldState->GetLastWriteMilliseconds());
    }
}

static FAutoConsoleCommandWithWorld GUniversePerfCommand(
    TEXT("universe.Perf"),
    TEXT("Samples frame time, memory, streaming, terrain and persistence in one place, ")
    TEXT("in a fixed order, so two baselines can be diffed."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniversePerfCommand));
