// Copyright Universe Project. All Rights Reserved.

#include "RemotePlayerAvatar.h"

#include "UniverseAnchorComponent.h"
#include "UniversePlayerState.h"
#include "UniverseScale.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ARemotePlayerAvatar::ARemotePlayerAvatar()
{
    PrimaryActorTick.bCanEverTick = true;

    // Never replicated. This actor exists only on the client that spawned it,
    // and there is exactly one per remote player per client. See the header for
    // why replicating it instead would be wrong rather than merely different.
    bReplicates = false;

    RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
    SetRootComponent(RootScene);

    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(RootScene);
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    // A capsule-ish placeholder at roughly human scale. There is no committed
    // character art in this project and inventing some to prove networking
    // would be proving the wrong thing.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CapsuleMesh(
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));

    if (CapsuleMesh.Succeeded())
    {
        BodyMesh->SetStaticMesh(CapsuleMesh.Object);
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    if (BasicMaterial.Succeeded())
    {
        BodyMesh->SetMaterial(0, BasicMaterial.Object);
    }

    // The engine cylinder is 100 cm tall and 50 cm in radius at unit scale.
    BodyMesh->SetRelativeScale3D(FVector(0.7, 0.7, 1.8));

    Anchor = CreateDefaultSubobject<UUniverseAnchorComponent>(TEXT("Anchor"));
}

void ARemotePlayerAvatar::SetPlayerState(AUniversePlayerState* InState)
{
    TrackedState = InState;

    if (InState != nullptr)
    {
        TrackedPersistentId = InState->GetPersistentId();
    }
}

void ARemotePlayerAvatar::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    const AUniversePlayerState* State = TrackedState.Get();
    const UWorld* World = GetWorld();

    if (State == nullptr || World == nullptr || Anchor == nullptr)
    {
        return;
    }

    if (TrackedPersistentId.IsEmpty())
    {
        TrackedPersistentId = State->GetPersistentId();
    }

    const double Now = World->GetTimeSeconds();

    // --- Did a new position arrive? -----------------------------------------
    //
    // Compared against the last one *received* rather than against the last one
    // displayed, because the displayed position is extrapolated and would never
    // match. Player states replicate at ten hertz, so this is true about that
    // often.
    const FUniversePosition Received = State->GetUniversePosition();

    if (!bHaveReceived || !(Received == LastReceivedPosition))
    {
        LastReceivedPosition = Received;
        LastReceivedVelocityMs = State->GetUniverseVelocityMs();
        LastReceivedAtSeconds = Now;
        bHaveReceived = true;
    }

    if (!bHaveReceived)
    {
        return;
    }

    // --- Extrapolate --------------------------------------------------------
    //
    // Forward from the last received position by the replicated velocity.
    // Deliberately not smoothed toward the received position: at warp the
    // extrapolation is light-seconds long, and easing into it would draw the
    // other player consistently behind where they actually are.
    const double Elapsed = FMath::Clamp(Now - LastReceivedAtSeconds, 0.0, 1.0);

    const FVector3d OffsetMeters = LastReceivedVelocityMs * Elapsed;

    const FUniversePosition Shown = OffsetMeters.IsNearlyZero()
        ? LastReceivedPosition
        : LastReceivedPosition.OffsetByCm(FVector3d(
            OffsetMeters.X * UniverseScale::CmPerMeter,
            OffsetMeters.Y * UniverseScale::CmPerMeter,
            OffsetMeters.Z * UniverseScale::CmPerMeter));

    Anchor->SetUniversePosition(Shown);

    SetActorRotation(State->GetUniverseOrientation());

    // Out of render range is not an error and not a reason to guess. The anchor
    // reports it when the position cannot be expressed as a finite offset from
    // this client's origin, which happens whenever the other player is more
    // than a rebase radius away - most of the time, in a universe this size.
    BodyMesh->SetVisibility(!Anchor->IsOutOfRenderRange());
}
