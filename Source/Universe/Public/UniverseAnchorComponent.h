// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "UniverseCoordinates.h"
#include "UniverseRenderSpace.h"
#include "UniverseAnchorComponent.generated.h"

class UUniverseWorldSubsystem;

/**
 * UUniverseAnchorComponent
 *
 * Gives an Actor a canonical universe position and keeps its Unreal transform
 * derived from it.
 *
 * The direction of authority matters and is one-way: the universe position is
 * the truth and the Actor's location is a rendering artefact recomputed from
 * it. Nothing may move an anchored Actor by setting its Unreal location - that
 * change would be silently discarded on the next rebase. Movement goes through
 * SetUniversePosition.
 *
 * Attach one to anything that exists at a place in the universe. Actors that
 * exist only relative to their parent (a cockpit panel, a wheel) do not need
 * one; ordinary attachment already handles them.
 */
UCLASS(ClassGroup = (Universe), meta = (BlueprintSpawnableComponent))
class UNIVERSE_API UUniverseAnchorComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UUniverseAnchorComponent();

    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Which space this actor's transform is computed in. */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Universe")
    EUniverseRenderSpace RenderSpace = EUniverseRenderSpace::Local;

    /** The canonical position. Authoritative. */
    const FUniversePosition& GetUniversePosition() const { return UniversePosition; }

    /** Moves the actor by setting its universe position, then refreshing the
     *  Unreal transform from it. */
    void SetUniversePosition(const FUniversePosition& NewPosition);

    /** Recomputes the Unreal transform from the current position and origin.
     *  Called by the subsystem after a rebase. */
    void SyncTransformToOrigin();

    /**
     * True when the actor is too far from the render origin to be placed.
     *
     * Such an actor is hidden rather than drawn somewhere plausible-looking:
     * a wrong position on screen is far harder to diagnose than a missing one,
     * and at these scales "somewhere plausible" is meaningless.
     */
    UFUNCTION(BlueprintPure, Category = "Universe")
    bool IsOutOfRenderRange() const { return bOutOfRenderRange; }

private:
    UUniverseWorldSubsystem* GetUniverseSubsystem() const;

    FUniversePosition UniversePosition;

    bool bOutOfRenderRange = false;
    bool bRegistered = false;
};
