// Copyright Universe Project. All Rights Reserved.

#include "Containers/Ticker.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"

/**
 * UniverseDeferredCommands.cpp
 *
 * universe.After <seconds> <command...> - run a console command later.
 *
 *
 * WHY THIS EXISTS
 *
 * -ExecCmds runs everything on the first frame. That was fine while the game
 * mode built the whole scene during StartPlay, because by the time any command
 * could run there was already a planet to land on. Sprint 006 made the world
 * stream in around the player, so on frame zero there is a seed, a galaxy and
 * nothing else - and every scripted acceptance run that starts with
 * "universe.Land" now fails on an empty world rather than on anything real.
 *
 * The alternatives were worse. Making each command wait for a planet would put
 * a state machine in every one of a dozen debug commands; making the streamer
 * build the home system synchronously during StartPlay would undo the point of
 * the sprint. Deferring the command is the small piece that was actually
 * missing.
 *
 * Time is measured in game seconds, which under -benchmark -fps=30 are fixed
 * 1/30 steps rather than wall clock. That is deliberate: a scripted run has to
 * behave the same on a fast machine and a slow one, and the frame count is what
 * the rest of the harness already measures in.
 */

DEFINE_LOG_CATEGORY_STATIC(LogUniverseDeferred, Log, All);

namespace
{
    /**
     * A command waiting to run.
     *
     * Held in a plain array on a ticker rather than as a UObject: this outlives
     * no world state and owns nothing, and a subsystem for it would need a world
     * that may not exist yet when the command is registered.
     */
    struct FDeferredCommand
    {
        double RemainingSeconds = 0.0;
        FString Command;
    };

    TArray<FDeferredCommand> GDeferredCommands;
    FTSTicker::FDelegateHandle GTickerHandle;

    bool TickDeferredCommands(float DeltaSeconds)
    {
        if (GDeferredCommands.Num() == 0)
        {
            return true;
        }

        // Reverse iteration so that removing a fired command does not skip the
        // next one - the classic mistake in exactly this loop.
        for (int32 Index = GDeferredCommands.Num() - 1; Index >= 0; --Index)
        {
            FDeferredCommand& Pending = GDeferredCommands[Index];

            Pending.RemainingSeconds -= static_cast<double>(DeltaSeconds);

            if (Pending.RemainingSeconds > 0.0)
            {
                continue;
            }

            const FString Command = Pending.Command;
            GDeferredCommands.RemoveAt(Index);

            UWorld* World = nullptr;

            if (GEngine != nullptr)
            {
                for (const FWorldContext& Context : GEngine->GetWorldContexts())
                {
                    if (Context.World() != nullptr
                        && (Context.WorldType == EWorldType::Game
                            || Context.WorldType == EWorldType::PIE))
                    {
                        World = Context.World();
                        break;
                    }
                }
            }

            UE_LOG(LogUniverseDeferred, Log, TEXT("Running deferred: %s"), *Command);

            if (World != nullptr && GEngine != nullptr)
            {
                GEngine->Exec(World, *Command);
            }
            else
            {
                UE_LOG(LogUniverseDeferred, Warning,
                    TEXT("No game world; dropped deferred command \"%s\"."), *Command);
            }
        }

        return true;
    }

    void EnsureTicker()
    {
        if (!GTickerHandle.IsValid())
        {
            GTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
                FTickerDelegate::CreateStatic(&TickDeferredCommands));
        }
    }
}

static void UniverseAfterCommand(const TArray<FString>& Args, UWorld* World)
{
    if (Args.Num() < 2)
    {
        UE_LOG(LogUniverseDeferred, Log,
            TEXT("universe.After <seconds> <command...>  - e.g. universe.After 3 universe.Land"));
        return;
    }

    FDeferredCommand Pending;
    Pending.RemainingSeconds = FCString::Atod(*Args[0]);

    TArray<FString> Rest;
    Rest.Reserve(Args.Num() - 1);

    for (int32 Index = 1; Index < Args.Num(); ++Index)
    {
        Rest.Add(Args[Index]);
    }

    Pending.Command = FString::Join(Rest, TEXT(" "));

    EnsureTicker();

    UE_LOG(LogUniverseDeferred, Log,
        TEXT("Deferred %.1f s: %s"), Pending.RemainingSeconds, *Pending.Command);

    GDeferredCommands.Add(MoveTemp(Pending));
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseAfterCommand(
    TEXT("universe.After"),
    TEXT("Runs a console command after a delay in game seconds. ")
    TEXT("Exists because -ExecCmds runs on frame zero, before the world has streamed in."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseAfterCommand));
