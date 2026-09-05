// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "UniverseHUD.generated.h"

/**
 * AUniverseHUD
 *
 * The developer diagnostic overlay: universe seed, global cell, local Unreal
 * position, logical velocity, current system and nearest astronomical object.
 *
 * Gated behind the console variable "universe.ShowDebug" (default on, F1
 * toggles it). Keeping the switch in a CVar rather than a compile-time flag
 * means it can be turned off for a capture or a playtest without a rebuild,
 * and stripping it for shipping later is a single check in DrawHUD.
 *
 * The values shown are read straight from the authoritative state rather than
 * from anything the renderer computed. That matters: the whole point of the
 * overlay is to be able to see that the canonical position and the Unreal
 * transform have diverged in exactly the way the architecture intends - the
 * global position ranging over light years while the local one stays inside
 * the rebase radius.
 */
UCLASS()
class UNIVERSE_API AUniverseHUD : public AHUD
{
    GENERATED_BODY()

public:
    AUniverseHUD();

    virtual void DrawHUD() override;

private:
    /** Draws one label/value row and advances the cursor. */
    void DrawRow(const FString& Label, const FString& Value, float& CursorY, const FLinearColor& Colour);

    void DrawHeading(const FString& Text, float& CursorY);

    /**
     * Draws a labelled marker over each astronomical body.
     *
     * This is a navigation aid, not a cheat around scale. Scaled space is a
     * uniform scale model, so a planet seen from across a solar system
     * genuinely subtends a fraction of a degree - it is a dot, exactly as it is
     * from real interplanetary space. The honest fix is to help the player find
     * it and fly to it, not to inflate it until the picture stops being true.
     */
    void DrawBodyMarkers();

    float LabelColumnX = 24.0f;
    float ValueColumnX = 230.0f;
    float RowHeight = 17.0f;
};
