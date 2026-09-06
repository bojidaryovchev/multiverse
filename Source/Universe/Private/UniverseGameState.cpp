// Copyright Universe Project. All Rights Reserved.

#include "UniverseGameState.h"

#include "UniverseWorldSubsystem.h"

#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Net/UnrealNetwork.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverseNet, Log, All);

AUniverseGameState::AUniverseGameState()
{
    PrimaryActorTick.bCanEverTick = true;
    bReplicates = true;
}

void AUniverseGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AUniverseGameState, WorldIdentity);
    DOREPLIFETIME(AUniverseGameState, ServerUniverseTimeSeconds);
}

void AUniverseGameState::BeginPlay()
{
    Super::BeginPlay();

    // A standalone session has no handshake to perform and is trivially
    // consistent with itself. Saying so explicitly means every consumer can ask
    // one question - "is the identity verified" - rather than branching on net
    // mode, which is the kind of branch that gets forgotten in one place.
    if (GetNetMode() == NM_Standalone || HasAuthority())
    {
        bIdentityVerified = true;
    }
}

void AUniverseGameState::SetWorldIdentity(const FUniverseWorldIdentity& Identity)
{
    if (!HasAuthority())
    {
        UE_LOG(LogUniverseNet, Error,
            TEXT("SetWorldIdentity called without authority. The server owns which universe this is."));
        return;
    }

    WorldIdentity = Identity;
    bIdentityVerified = true;

    UE_LOG(LogUniverseNet, Log, TEXT("World identity: %s"), *WorldIdentity.ToDebugString());
}

void AUniverseGameState::OnRep_WorldIdentity()
{
    // --- The handshake ------------------------------------------------------
    //
    // The client rebuilds what *its* build would produce for the server's seed
    // and compares. Everything that differs would make the two disagree about
    // the world while both believed they were right: a client with a different
    // terrain version walks on ground the server does not think is there.
    //
    // So a mismatch is a refusal. There is no version of "mostly the same
    // universe" that is worth playing in, and a silent partial mismatch is far
    // worse than a failed connection because it looks like it works.
    const FUniverseWorldIdentity Local = FUniverseWorldIdentity::MakeLocal(
        WorldIdentity.SeedText, static_cast<uint64>(WorldIdentity.SeedValue));

    if (Local == WorldIdentity)
    {
        bIdentityVerified = true;
        IdentityMismatchReason.Reset();

        UE_LOG(LogUniverseNet, Log,
            TEXT("Handshake accepted: %s"), *WorldIdentity.ToDebugString());

        // Adopt the server's seed. Until this point the client generated from
        // whatever its own default was, which is almost certainly a different
        // universe; everything generated before now is discarded implicitly
        // because nothing has been built from it that outlives the frame.
        if (UWorld* World = GetWorld())
        {
            if (UUniverseWorldSubsystem* Universe = World->GetSubsystem<UUniverseWorldSubsystem>())
            {
                if (Universe->GetUniverseSeedText() != WorldIdentity.SeedText)
                {
                    Universe->SetUniverseSeedText(WorldIdentity.SeedText);
                }
            }
        }

        return;
    }

    bIdentityVerified = false;
    IdentityMismatchReason = Local.DescribeMismatch(WorldIdentity);

    UE_LOG(LogUniverseNet, Error,
        TEXT("Handshake REFUSED. This build differs from the server: %s"),
        *IdentityMismatchReason);

    if (UWorld* World = GetWorld())
    {
        if (APlayerController* Controller = World->GetFirstPlayerController())
        {
            Controller->ClientWasKicked(FText::FromString(FString::Printf(
                TEXT("Incompatible universe: %s"), *IdentityMismatchReason)));
        }
    }
}

void AUniverseGameState::OnRep_ServerUniverseTime()
{
    LastClockDriftSeconds = LocalUniverseTimeSeconds - ServerUniverseTimeSeconds;

    // Snapped rather than eased. The clock drives the sun and the weather,
    // both of which change on a scale of minutes, so a correction of a few
    // hundred milliseconds is invisible - and easing would mean the two players
    // are never actually agreed, only converging.
    LocalUniverseTimeSeconds = ServerUniverseTimeSeconds;
}

double AUniverseGameState::GetUniverseTimeSeconds() const
{
    return LocalUniverseTimeSeconds;
}

void AUniverseGameState::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    const double Dt = static_cast<double>(DeltaSeconds);

    // Advanced locally at both ends. On the server this *is* the clock; on a
    // client it is a prediction that OnRep corrects. Either way the sun moves
    // every frame rather than every packet.
    LocalUniverseTimeSeconds += Dt;

    if (!HasAuthority())
    {
        return;
    }

    TimeSinceClockReplication += Dt;

    if (TimeSinceClockReplication < ClockReplicationIntervalSeconds)
    {
        return;
    }

    TimeSinceClockReplication = 0.0;

    // The replicated field is only *written* at the interval. Writing it every
    // frame would mark it dirty every frame and replicate a number both ends
    // can extrapolate perfectly well - the local clock above is exactly that
    // extrapolation, and this is only its periodic correction.
    ServerUniverseTimeSeconds = LocalUniverseTimeSeconds;
}
