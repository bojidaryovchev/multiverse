// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UniverseCoordinates.h"
#include "UniverseSeed.h"
#include "UniverseRenderSpace.h"
#include "StarSystemDescriptor.h"
#include "UniverseWorldSubsystem.generated.h"

class UUniverseAnchorComponent;

/**
 * UUniverseWorldSubsystem
 *
 * The bridge between the canonical universe and Unreal's coordinate space, and
 * the only place allowed to convert between them.
 *
 * Two responsibilities:
 *
 * 1. It owns the render origin - the universe position that Unreal's (0,0,0)
 *    currently represents - and rebases it when the tracked viewpoint drifts
 *    too far. Rebasing is invisible because every anchored actor is moved by
 *    the same delta in the same frame: the whole scene shifts together, so no
 *    relative geometry changes and the camera sees nothing happen.
 *
 * 2. It owns the universe seed for this world, so that every generator call
 *    descends from one root and the world is reproducible.
 *
 * It deliberately does not cache generated systems. Sprint 001 wants the
 * determinism guarantee exercised, not hidden behind a cache; caching is a
 * later, measurable optimisation.
 */
UCLASS()
class UNIVERSE_API UUniverseWorldSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    // --- USubsystem / FTickableGameObject ---------------------------------
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    // --- Seeding -----------------------------------------------------------

    /** Re-seeds the universe from a text phrase. Existing actors are not
     *  regenerated; callers are expected to do this during setup. */
    UFUNCTION(BlueprintCallable, Category = "Universe")
    void SetUniverseSeedText(const FString& SeedText);

    UFUNCTION(BlueprintPure, Category = "Universe")
    FString GetUniverseSeedText() const { return UniverseSeedText; }

    /** Hex form of the derived root seed, for the debug display. */
    UFUNCTION(BlueprintPure, Category = "Universe")
    FString GetUniverseSeedHex() const;

    const FUniverseSeedHierarchy& GetSeedHierarchy() const { return SeedHierarchy; }

    // --- Presentation ------------------------------------------------------

    const FUniversePresentationSettings& GetPresentation() const { return Presentation; }
    void SetPresentation(const FUniversePresentationSettings& InSettings);

    // --- Origin and conversion --------------------------------------------

    /** The universe position that Unreal's origin currently represents. */
    const FUniversePosition& GetRenderOrigin() const { return RenderOrigin; }

    /**
     * Converts a universe position to an Unreal location.
     *
     * Returns false when the position is too far from the render origin to be
     * expressed as a finite offset. Callers must handle that rather than
     * clamping: an actor that cannot be placed should be hidden, never drawn
     * at a made-up location.
     */
    bool TryGetRenderLocation(
        const FUniversePosition& Position,
        EUniverseRenderSpace Space,
        FVector& OutLocation) const;

    /** Converts an Unreal location in Local space back to a universe position. */
    FUniversePosition RenderLocationToUniverse(const FVector& Location) const;

    /** Forces the render origin to a specific position and resyncs anchors. */
    void SetRenderOrigin(const FUniversePosition& NewOrigin);

    /**
     * Rebases now if the tracked viewpoint has drifted past the rebase radius.
     *
     * Public because the tracked anchor calls it the instant it moves, rather
     * than waiting for this subsystem's own tick. That matters at speed: one
     * frame at the probe's 1e12 m/s cap covers 3.3e12 cm, so if the correction
     * waited for a separate tick, the Actor transform would sit thousands of
     * times beyond the rebase radius for part of every frame - and whether
     * anything observed it would come down to tick ordering. Correcting at the
     * point of movement makes the bound hold structurally instead.
     */
    void RebaseIfNeeded();

    // --- Anchors -----------------------------------------------------------

    void RegisterAnchor(UUniverseAnchorComponent* Anchor);
    void UnregisterAnchor(UUniverseAnchorComponent* Anchor);

    /** The anchor whose drift from the Unreal origin triggers rebasing -
     *  normally the player's. */
    void SetTrackedAnchor(UUniverseAnchorComponent* Anchor);
    UUniverseAnchorComponent* GetTrackedAnchor() const { return TrackedAnchor.Get(); }

    // --- Diagnostics -------------------------------------------------------

    /** How many times the origin has been rebased this session. */
    UFUNCTION(BlueprintPure, Category = "Universe|Debug")
    int32 GetRebaseCount() const { return RebaseCount; }

    /** The shift applied by the most recent rebase, in Unreal cm. */
    UFUNCTION(BlueprintPure, Category = "Universe|Debug")
    FVector GetLastRebaseShift() const { return LastRebaseShift; }

    UFUNCTION(BlueprintPure, Category = "Universe|Debug")
    int32 GetRegisteredAnchorCount() const { return Anchors.Num(); }

    /**
     * The nearest known star system to the tracked anchor.
     *
     * Refreshed on a timer rather than every frame: the search scans a cube of
     * sectors and generating it every tick would dominate the frame for
     * information that changes on a scale of seconds.
     */
    bool GetNearestSystem(FStarSystemDescriptor& OutSystem) const;

    /** Distance in light years to the nearest system, or a negative value if
     *  none is known. */
    double GetNearestSystemDistanceLightYears() const;

private:
    void ResyncAllAnchors();
    void UpdateNearestSystem();

    /** Text the seed was derived from; kept for display and save files. */
    UPROPERTY()
    FString UniverseSeedText;

    FUniverseSeedHierarchy SeedHierarchy;

    UPROPERTY()
    FUniversePresentationSettings Presentation;

    /** The universe position mapped to Unreal (0,0,0). */
    FUniversePosition RenderOrigin;

    /**
     * Weak pointers throughout: an anchor's actor can be destroyed at any time
     * and the subsystem outlives all of them. Holding strong references would
     * both leak and keep destroyed actors alive.
     */
    TArray<TWeakObjectPtr<UUniverseAnchorComponent>> Anchors;
    TWeakObjectPtr<UUniverseAnchorComponent> TrackedAnchor;

    int32 RebaseCount = 0;
    FVector LastRebaseShift = FVector::ZeroVector;

    // Nearest-system cache.
    FStarSystemDescriptor NearestSystem;
    bool bHasNearestSystem = false;
    double NearestSystemDistanceLy = -1.0;
    double TimeSinceNearestSearch = 0.0;

    /** Seconds between nearest-system searches. */
    static constexpr double NearestSearchIntervalSeconds = 1.0;

    /** Search radius, in light years, for the nearest-system query. */
    static constexpr double NearestSearchRadiusLy = 25.0;
};
