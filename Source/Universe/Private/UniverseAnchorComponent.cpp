// Copyright Universe Project. All Rights Reserved.

#include "UniverseAnchorComponent.h"
#include "UniverseWorldSubsystem.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

UUniverseAnchorComponent::UUniverseAnchorComponent()
{
    // The component does no per-frame work of its own: it updates when its
    // position is set and when the subsystem rebases. Ticking every anchor
    // every frame would be pure waste, and the subsystem already ticks once.
    PrimaryComponentTick.bCanEverTick = false;
}

UUniverseWorldSubsystem* UUniverseAnchorComponent::GetUniverseSubsystem() const
{
    const UWorld* World = GetWorld();
    return World != nullptr ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;
}

void UUniverseAnchorComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UUniverseWorldSubsystem* Subsystem = GetUniverseSubsystem())
    {
        Subsystem->RegisterAnchor(this);
        bRegistered = true;
        SyncTransformToOrigin();
    }
}

void UUniverseAnchorComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (bRegistered)
    {
        if (UUniverseWorldSubsystem* Subsystem = GetUniverseSubsystem())
        {
            Subsystem->UnregisterAnchor(this);
        }
        bRegistered = false;
    }

    Super::EndPlay(EndPlayReason);
}

void UUniverseAnchorComponent::SetUniversePosition(const FUniversePosition& NewPosition)
{
    UniversePosition = NewPosition;
    SyncTransformToOrigin();
}

void UUniverseAnchorComponent::SyncTransformToOrigin()
{
    AActor* Owner = GetOwner();
    if (Owner == nullptr)
    {
        return;
    }

    const UUniverseWorldSubsystem* Subsystem = GetUniverseSubsystem();
    if (Subsystem == nullptr)
    {
        return;
    }

    FVector RenderLocation;
    if (!Subsystem->TryGetRenderLocation(UniversePosition, RenderSpace, RenderLocation))
    {
        // Too far to place. Hide rather than invent a location - see the
        // header comment for why a missing actor beats a misplaced one.
        if (!bOutOfRenderRange)
        {
            bOutOfRenderRange = true;
            Owner->SetActorHiddenInGame(true);
        }
        return;
    }

    if (bOutOfRenderRange)
    {
        bOutOfRenderRange = false;
        Owner->SetActorHiddenInGame(false);
    }

    // Teleport, not sweep: this is a coordinate-frame change, not motion
    // through the world. Sweeping would generate spurious collisions on every
    // rebase, and at rebase distances would frequently fail outright.
    Owner->SetActorLocation(RenderLocation, /*bSweep=*/false, /*OutSweepHitResult=*/nullptr, ETeleportType::TeleportPhysics);
}
