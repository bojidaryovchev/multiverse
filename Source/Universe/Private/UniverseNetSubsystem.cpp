// Copyright Universe Project. All Rights Reserved.

#include "UniverseNetSubsystem.h"

#include "RemotePlayerAvatar.h"
#include "UniverseGameState.h"
#include "UniversePlayerState.h"

#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverseNetInterest, Log, All);

void UUniverseNetSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
}

void UUniverseNetSubsystem::Deinitialize()
{
    for (TPair<FString, TObjectPtr<ARemotePlayerAvatar>>& Pair : Avatars)
    {
        if (Pair.Value != nullptr)
        {
            Pair.Value->Destroy();
        }
    }

    Avatars.Reset();
    RemotePlayers.Reset();

    Super::Deinitialize();
}

TStatId UUniverseNetSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UUniverseNetSubsystem, STATGROUP_Tickables);
}

bool UUniverseNetSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

AUniversePlayerState* UUniverseNetSubsystem::GetLocalPlayerState() const
{
    const UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return nullptr;
    }

    const APlayerController* Controller = World->GetFirstPlayerController();

    return (Controller != nullptr)
        ? Cast<AUniversePlayerState>(Controller->PlayerState)
        : nullptr;
}

FString UUniverseNetSubsystem::GetLocalPersistentId() const
{
    const AUniversePlayerState* State = GetLocalPlayerState();

    return (State != nullptr) ? State->GetPersistentId() : FString();
}

void UUniverseNetSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    TimeSinceEvaluation += static_cast<double>(DeltaTime);

    if (TimeSinceEvaluation < EvaluationIntervalSeconds)
    {
        return;
    }

    TimeSinceEvaluation = 0.0;

    EvaluateRelevance();
    ReconcileAvatars();
}

ENetRelevanceClass UUniverseNetSubsystem::ClassifyRelevance(
    const AUniversePlayerState& Other) const
{
    const AUniversePlayerState* Local = GetLocalPlayerState();

    if (Local == nullptr)
    {
        return ENetRelevanceClass::Irrelevant;
    }

    // --- Structural first ---------------------------------------------------
    //
    // Same system or not. This is the question that actually matters at
    // universe scale: a player in another system is not "far away" in a sense a
    // distance captures, they are somewhere this player cannot see or reach.
    //
    // It is also cheap and exact - two integer addresses - where the distance
    // between two positions four light years apart is a subtraction of numbers
    // near 10^10.
    const FUniverseSystemId LocalSystem = Local->GetSystemId();
    const FUniverseSystemId OtherSystem = Other.GetSystemId();

    if (!LocalSystem.IsValid() || !OtherSystem.IsValid() || !(LocalSystem == OtherSystem))
    {
        return ENetRelevanceClass::Irrelevant;
    }

    // --- Then metric --------------------------------------------------------
    const double DistanceMeters = FUniversePosition::DistanceMeters(
        Local->GetUniversePosition(), Other.GetUniversePosition());

    if (DistanceMeters <= VisibleRangeMeters)
    {
        return ENetRelevanceClass::Visible;
    }

    if (DistanceMeters <= SameRegionRangeMeters)
    {
        return ENetRelevanceClass::SameRegion;
    }

    return ENetRelevanceClass::SameSystem;
}

void UUniverseNetSubsystem::EvaluateRelevance()
{
    RemotePlayers.Reset();

    const UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    const AGameStateBase* GameState = World->GetGameState();

    if (GameState == nullptr)
    {
        return;
    }

    const AUniversePlayerState* Local = GetLocalPlayerState();

    double ServerTime = 0.0;

    if (const AUniverseGameState* Universe = Cast<AUniverseGameState>(GameState))
    {
        ServerTime = Universe->GetUniverseTimeSeconds();
    }

    for (APlayerState* Base : GameState->PlayerArray)
    {
        const AUniversePlayerState* State = Cast<AUniversePlayerState>(Base);

        if (State == nullptr || State == Local)
        {
            continue;
        }

        // A player state without a persistent id has not finished arriving.
        // Skipped rather than shown with a blank name: an avatar keyed on an
        // empty string would collide with the next one to arrive.
        if (State->GetPersistentId().IsEmpty())
        {
            continue;
        }

        FRemotePlayerSnapshot Snapshot;
        Snapshot.PlayerId = State->GetPersistentId();
        Snapshot.DisplayName = State->GetPlayerName();
        Snapshot.Position = FReplicatedUniversePosition(State->GetUniversePosition());

        const FVector3d Velocity = State->GetUniverseVelocityMs();
        Snapshot.VelocityMs = FVector_NetQuantize100(Velocity.X, Velocity.Y, Velocity.Z);

        Snapshot.Orientation = State->GetUniverseOrientation();
        Snapshot.bOnFoot = State->IsOnFoot();
        Snapshot.PlanetKey = State->GetPlanetKey();
        Snapshot.SystemId.Set(State->GetSystemId());
        Snapshot.Relevance = ClassifyRelevance(*State);
        Snapshot.ServerTimeSeconds = ServerTime;

        RemotePlayers.Add(MoveTemp(Snapshot));
    }
}

void UUniverseNetSubsystem::ReconcileAvatars()
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    const AGameStateBase* GameState = World->GetGameState();

    if (GameState == nullptr)
    {
        return;
    }

    // --- Who should have one ------------------------------------------------
    TSet<FString> ShouldExist;

    for (const FRemotePlayerSnapshot& Snapshot : RemotePlayers)
    {
        if (Snapshot.Relevance == ENetRelevanceClass::Visible
            || Snapshot.Relevance == ENetRelevanceClass::SameRegion)
        {
            ShouldExist.Add(Snapshot.PlayerId);
        }
    }

    // --- Remove the ones that should not -------------------------------------
    TArray<FString> ToRemove;

    for (const TPair<FString, TObjectPtr<ARemotePlayerAvatar>>& Pair : Avatars)
    {
        if (!ShouldExist.Contains(Pair.Key) || Pair.Value == nullptr)
        {
            ToRemove.Add(Pair.Key);
        }
    }

    for (const FString& Id : ToRemove)
    {
        if (TObjectPtr<ARemotePlayerAvatar>* Found = Avatars.Find(Id))
        {
            if (*Found != nullptr)
            {
                (*Found)->Destroy();
                ++AvatarDespawnCount;

                UE_LOG(LogUniverseNetInterest, Verbose,
                    TEXT("Released avatar for %s."), *Id);
            }
        }

        Avatars.Remove(Id);
    }

    // --- Add the ones that are missing ---------------------------------------
    for (APlayerState* Base : GameState->PlayerArray)
    {
        AUniversePlayerState* State = Cast<AUniversePlayerState>(Base);

        if (State == nullptr)
        {
            continue;
        }

        const FString& Id = State->GetPersistentId();

        if (Id.IsEmpty() || !ShouldExist.Contains(Id) || Avatars.Contains(Id))
        {
            continue;
        }

        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        ARemotePlayerAvatar* Avatar = World->SpawnActor<ARemotePlayerAvatar>(
            ARemotePlayerAvatar::StaticClass(), FTransform::Identity, SpawnParams);

        if (Avatar == nullptr)
        {
            continue;
        }

        Avatar->SetPlayerState(State);
        Avatars.Add(Id, Avatar);
        ++AvatarSpawnCount;

        UE_LOG(LogUniverseNetInterest, Log,
            TEXT("Spawned avatar for %s (%s)."), *Id, *State->GetPlayerName());
    }
}

int32 UUniverseNetSubsystem::CountAtLeast(ENetRelevanceClass Minimum) const
{
    int32 Count = 0;

    for (const FRemotePlayerSnapshot& Snapshot : RemotePlayers)
    {
        if (Snapshot.Relevance >= Minimum)
        {
            ++Count;
        }
    }

    return Count;
}
