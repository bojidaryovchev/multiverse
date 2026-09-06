// Copyright Universe Project. All Rights Reserved.

#include "InterstellarTravel.h"
#include "RemotePlayerAvatar.h"
#include "UniverseGameState.h"
#include "UniverseNetSubsystem.h"
#include "UniversePlayerController.h"
#include "UniversePlayerState.h"
#include "WorldStateSubsystem.h"

#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "HAL/IConsoleManager.h"

/**
 * UniverseNetCommands.cpp
 *
 * What the network is doing, in numbers.
 *
 * A multiplayer bug is almost never visible from one side. "The other player is
 * in the wrong place" is, from each machine's point of view, a statement that
 * the *other* machine is wrong, and neither can settle it alone. These commands
 * exist so that the same question can be asked on the server and on each client
 * and the three answers compared - which is how every defect in this sprint was
 * actually found.
 */

DEFINE_LOG_CATEGORY_STATIC(LogUniverseNetCmd, Log, All);

namespace
{
    const TCHAR* DescribeNetMode(ENetMode Mode)
    {
        switch (Mode)
        {
        case NM_Standalone:      return TEXT("Standalone");
        case NM_DedicatedServer: return TEXT("DedicatedServer");
        case NM_ListenServer:    return TEXT("ListenServer");
        case NM_Client:          return TEXT("Client");
        default:                 return TEXT("?");
        }
    }
}

// ---------------------------------------------------------------------------
// universe.NetInfo
// ---------------------------------------------------------------------------

static void UniverseNetInfoCommand(UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    UE_LOG(LogUniverseNetCmd, Log, TEXT("--- Network ---"));
    UE_LOG(LogUniverseNetCmd, Log, TEXT("  Net mode      : %s"), DescribeNetMode(World->GetNetMode()));

    // --- Identity -----------------------------------------------------------
    if (const AUniverseGameState* GameState = World->GetGameState<AUniverseGameState>())
    {
        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  World identity: %s"), *GameState->GetWorldIdentity().ToDebugString());

        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Handshake     : %s%s"),
            GameState->IsWorldIdentityVerified() ? TEXT("verified") : TEXT("NOT VERIFIED"),
            GameState->GetIdentityMismatchReason().IsEmpty()
                ? TEXT("")
                : *FString::Printf(TEXT(" - %s"), *GameState->GetIdentityMismatchReason()));

        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Shared clock  : %.1f s  (last drift %.4f s)"),
            GameState->GetUniverseTimeSeconds(), GameState->GetClockDriftSeconds());
    }
    else
    {
        UE_LOG(LogUniverseNetCmd, Warning, TEXT("  No universe game state."));
    }

    // --- Connections --------------------------------------------------------
    if (UNetDriver* Driver = World->GetNetDriver())
    {
        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Net driver    : %s, %d client connection(s)"),
            *Driver->NetDriverName.ToString(), Driver->ClientConnections.Num());

        if (UNetConnection* Connection = Driver->ServerConnection)
        {
            UE_LOG(LogUniverseNetCmd, Log,
                TEXT("  To server     : %s  ping %.1f ms  in %.1f kB/s  out %.1f kB/s"),
                *Connection->LowLevelGetRemoteAddress(true),
                Connection->AvgLag * 1000.0f,
                Connection->InBytesPerSecond / 1024.0f,
                Connection->OutBytesPerSecond / 1024.0f);
        }

        for (UNetConnection* Connection : Driver->ClientConnections)
        {
            if (Connection == nullptr)
            {
                continue;
            }

            UE_LOG(LogUniverseNetCmd, Log,
                TEXT("  Client        : %s  ping %.1f ms  in %.1f kB/s  out %.1f kB/s"),
                *Connection->LowLevelGetRemoteAddress(true),
                Connection->AvgLag * 1000.0f,
                Connection->InBytesPerSecond / 1024.0f,
                Connection->OutBytesPerSecond / 1024.0f);
        }
    }
    else
    {
        UE_LOG(LogUniverseNetCmd, Log, TEXT("  Net driver    : none (single player)"));
    }

    // --- Players ------------------------------------------------------------
    if (const AGameStateBase* GameState = World->GetGameState())
    {
        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Players       : %d"), GameState->PlayerArray.Num());

        for (const APlayerState* Base : GameState->PlayerArray)
        {
            const AUniversePlayerState* State = Cast<AUniversePlayerState>(Base);

            if (State == nullptr)
            {
                continue;
            }

            const FUniversePosition Position = State->GetUniversePosition();

            int64 SectorX = 0;
            int64 SectorY = 0;
            int64 SectorZ = 0;
            Position.GetSector(SectorX, SectorY, SectorZ);

            UE_LOG(LogUniverseNetCmd, Log,
                TEXT("    %-16s %-12s sector [%lld, %lld, %lld]  %s  %d rejected"),
                *State->GetPersistentId(),
                State->IsOnFoot() ? TEXT("on foot") : TEXT("in a ship"),
                SectorX, SectorY, SectorZ,
                *FInterstellarTravel::FormatSpeed(State->GetUniverseVelocityMs().Size()),
                State->GetRejectedMoveCount());
        }
    }

    // --- Interest -----------------------------------------------------------
    if (const UUniverseNetSubsystem* Net = World->GetSubsystem<UUniverseNetSubsystem>())
    {
        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Interest      : %d same system, %d same region, %d visible"),
            Net->CountAtLeast(ENetRelevanceClass::SameSystem),
            Net->CountAtLeast(ENetRelevanceClass::SameRegion),
            Net->CountAtLeast(ENetRelevanceClass::Visible));

        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Avatars       : %d live, %d spawned, %d released this session"),
            Net->GetAvatarCount(), Net->GetAvatarSpawnCount(), Net->GetAvatarDespawnCount());

        for (const FRemotePlayerSnapshot& Snapshot : Net->GetRemotePlayers())
        {
            const double DistanceMeters = FUniversePosition::DistanceMeters(
                Snapshot.Position.Get(),
                World->GetFirstPlayerController() != nullptr
                    && Cast<AUniversePlayerState>(World->GetFirstPlayerController()->PlayerState) != nullptr
                        ? Cast<AUniversePlayerState>(
                              World->GetFirstPlayerController()->PlayerState)->GetUniversePosition()
                        : FUniversePosition());

            UE_LOG(LogUniverseNetCmd, Log,
                TEXT("    %-16s %-12s %s away"),
                *Snapshot.PlayerId,
                LexToString(Snapshot.Relevance),
                *FInterstellarTravel::FormatDistance(DistanceMeters));
        }
    }

    // --- Local client state -------------------------------------------------
    if (const AUniversePlayerController* Controller =
            Cast<AUniversePlayerController>(World->GetFirstPlayerController()))
    {
        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Moves sent    : %d, %d corrections%s"),
            Controller->GetSentMoveCount(),
            Controller->GetCorrectionCount(),
            Controller->GetLastCorrectionReason().IsEmpty()
                ? TEXT("")
                : *FString::Printf(TEXT(" (last: %s)"), *Controller->GetLastCorrectionReason()));

        // Server-side only. The subscription set lives on the server's copy of
        // the controller - it is what the server has agreed to send this
        // client - so on the client itself this is always zero, and saying so
        // is better than printing a zero that reads like a failure.
        if (World->GetNetMode() != NM_Client)
        {
            UE_LOG(LogUniverseNetCmd, Log,
                TEXT("  Regions served: %d"), Controller->GetSubscribedRegionCount());
        }
    }

    // --- Persistence --------------------------------------------------------
    if (const UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
    {
        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("  Persistence   : %s, %d regions cached"),
            WorldState->HasPersistenceAuthority()
                ? TEXT("authoritative (this instance owns the database)")
                : TEXT("replicated (regions come from the server)"),
            WorldState->GetLoadedRegionCount());
    }
}

static FAutoConsoleCommandWithWorld GUniverseNetInfoCommand(
    TEXT("universe.NetInfo"),
    TEXT("Reports net mode, world identity, connections, players, interest and bandwidth."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseNetInfoCommand));

// ---------------------------------------------------------------------------
// universe.NetPlayers - just the players, for a quick two-machine comparison
// ---------------------------------------------------------------------------

static void UniverseNetPlayersCommand(UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    const AGameStateBase* GameState = World->GetGameState();

    if (GameState == nullptr)
    {
        UE_LOG(LogUniverseNetCmd, Warning, TEXT("No game state."));
        return;
    }

    // Deliberately terse and deliberately identical in format on every machine,
    // so two logs can be diffed rather than read.
    for (const APlayerState* Base : GameState->PlayerArray)
    {
        const AUniversePlayerState* State = Cast<AUniversePlayerState>(Base);

        if (State == nullptr)
        {
            continue;
        }

        const FUniversePosition Position = State->GetUniversePosition();

        const FUniverseSystemId SystemId = State->GetSystemId();

        UE_LOG(LogUniverseNetCmd, Log,
            TEXT("PLAYER %s cell=[%lld,%lld,%lld] onfoot=%d planet=0x%016llX system=%s"),
            *State->GetPersistentId(),
            Position.CellX, Position.CellY, Position.CellZ,
            State->IsOnFoot() ? 1 : 0,
            static_cast<unsigned long long>(State->GetPlanetKey()),
            SystemId.IsValid() ? *SystemId.ToDebugString() : TEXT("none"));
    }
}

static FAutoConsoleCommandWithWorld GUniverseNetPlayersCommand(
    TEXT("universe.NetPlayers"),
    TEXT("Prints every player's canonical position in a fixed format, for diffing logs."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseNetPlayersCommand));

// ---------------------------------------------------------------------------
// universe.NetTeleportTest - prove that movement validation actually rejects
// ---------------------------------------------------------------------------

static void UniverseNetTeleportTestCommand(UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    AUniversePlayerController* Controller =
        Cast<AUniversePlayerController>(World->GetFirstPlayerController());

    if (Controller == nullptr || World->GetNetMode() != NM_Client)
    {
        UE_LOG(LogUniverseNetCmd, Warning,
            TEXT("universe.NetTeleportTest only means anything on a client."));
        return;
    }

    // A deliberately impossible claim: a hundred light years, this frame.
    //
    // The point of this command is that a security check nobody has ever seen
    // fire is a security check that might not work. This makes it fire on
    // demand, and the rejection appears in the server's log and in the
    // correction count on the client.
    const AUniversePlayerState* State = Cast<AUniversePlayerState>(Controller->PlayerState);

    if (State == nullptr)
    {
        return;
    }

    FUniversePosition Absurd = State->GetUniversePosition();
    Absurd.CellX += static_cast<int64>(100.0 * UniverseScale::CmPerLightYear / UniverseScale::CellSizeCmD);

    UE_LOG(LogUniverseNetCmd, Log,
        TEXT("Claiming to have moved 100 light years this frame. The server should refuse."));

    Controller->ServerUpdatePosition(
        FReplicatedUniversePosition(Absurd),
        FVector_NetQuantize100(0.0f, 0.0f, 0.0f),
        FRotator::ZeroRotator,
        State->IsOnFoot(),
        State->GetPlanetKey(),
        FReplicatedSystemId(State->GetSystemId()),
        0.0);
}

static FAutoConsoleCommandWithWorld GUniverseNetTeleportTestCommand(
    TEXT("universe.NetTeleportTest"),
    TEXT("Client only: claims an impossible move so the server's rejection can be observed."),
    FConsoleCommandWithWorldDelegate::CreateStatic(&UniverseNetTeleportTestCommand));
