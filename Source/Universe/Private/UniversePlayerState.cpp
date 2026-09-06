// Copyright Universe Project. All Rights Reserved.

#include "UniversePlayerState.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

AUniversePlayerState::AUniversePlayerState()
{
    // Ten times a second. Player positions are the only thing this project
    // replicates continuously, and at interstellar speed a higher rate would
    // not help: the interesting motion is either slow (walking) or so fast that
    // no rate reproduces it and the receiver extrapolates instead.
    NetUpdateFrequency = 10.0f;
    MinNetUpdateFrequency = 2.0f;
}

void AUniversePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);

    DOREPLIFETIME(AUniversePlayerState, PersistentId);
    DOREPLIFETIME(AUniversePlayerState, CanonicalPosition);
    DOREPLIFETIME(AUniversePlayerState, NetVelocityMs);
    DOREPLIFETIME(AUniversePlayerState, Orientation);
    DOREPLIFETIME(AUniversePlayerState, bOnFoot);
    DOREPLIFETIME(AUniversePlayerState, PlanetKey);
    DOREPLIFETIME(AUniversePlayerState, SystemId);
    DOREPLIFETIME(AUniversePlayerState, RejectedMoveCount);
}

void AUniversePlayerState::CopyProperties(APlayerState* NewPlayerState)
{
    Super::CopyProperties(NewPlayerState);

    // Carried across a seamless travel or a pawn replacement. Losing the
    // canonical position here would teleport the player to the origin of the
    // universe, which is now intergalactic space with no stars in it.
    if (AUniversePlayerState* Next = Cast<AUniversePlayerState>(NewPlayerState))
    {
        Next->PersistentId = PersistentId;
        Next->CanonicalPosition = CanonicalPosition;
        Next->NetVelocityMs = NetVelocityMs;
        Next->Orientation = Orientation;
        Next->bOnFoot = bOnFoot;
        Next->PlanetKey = PlanetKey;
        Next->SystemId = SystemId;
        Next->RejectedMoveCount = RejectedMoveCount;
        Next->LastUpdateServerTime = LastUpdateServerTime;
    }
}

void AUniversePlayerState::SetPersistentId(const FString& InId)
{
    if (!HasAuthority())
    {
        return;
    }

    PersistentId = InId;
}

void AUniversePlayerState::SetAuthoritativeState(
    const FUniversePosition& Position,
    const FVector3d& VelocityMs,
    const FRotator& InOrientation,
    bool bInOnFoot,
    uint64 InPlanetKey,
    const FUniverseSystemId& InSystemId)
{
    if (!HasAuthority())
    {
        return;
    }

    CanonicalPosition.Set(Position);

    NetVelocityMs = FVector_NetQuantize100(VelocityMs.X, VelocityMs.Y, VelocityMs.Z);
    Orientation = InOrientation;
    bOnFoot = bInOnFoot;
    PlanetKey = InPlanetKey;
    SystemId.Set(InSystemId);

    if (const UWorld* World = GetWorld())
    {
        LastUpdateServerTime = World->GetTimeSeconds();
    }
}
