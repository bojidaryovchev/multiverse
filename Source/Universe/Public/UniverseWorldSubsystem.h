// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UniverseCoordinates.h"
#include "UniverseSeed.h"
#include "UniverseRenderSpace.h"
#include "StarSystemDescriptor.h"
#include "SimulationFrame.h"
#include "UniverseWorldSubsystem.generated.h"

class UUniverseAnchorComponent;
class APlanetActor;

/**
 * UUniverseWorldSubsystem
 *
 * The bridge between the canonical universe and Unreal's coordinate space, and
 * the only place allowed to convert between them.
 *
 * Three responsibilities:
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
 * 3. It owns the simulation frame - whether the player is in deep space or
 *    attached to a planet - and is therefore the single answer to "which way
 *    is down". Gravity is asked of the subsystem rather than of a planet
 *    directly, so that a caller cannot accidentally use a body the frame
 *    selector has already released. See SimulationFrame.h for why the frame is
 *    explicit and why its boundary has hysteresis.
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

    // --- Planets and the simulation frame ---------------------------------

    /**
     * Planets offer themselves to the frame selector. Registration is what
     * makes a body eligible to claim the player; a planet actor that has not
     * registered is scenery.
     */
    void RegisterPlanet(APlanetActor* Planet);
    void UnregisterPlanet(APlanetActor* Planet);

    /** Which frame the simulation is currently in. */
    UFUNCTION(BlueprintPure, Category = "Universe|Frame")
    bool IsInPlanetaryFrame() const { return FrameSelector.GetKind() == EUniverseFrameKind::Planetary; }

    const FUniverseFrameState& GetFrameState() const { return FrameSelector.GetState(); }

    /** The planet the frame is attached to, or null in the interstellar frame. */
    APlanetActor* GetFramePlanet() const { return FramePlanet.Get(); }

    /** Distance to the frame planet as a fraction of its enter radius. */
    UFUNCTION(BlueprintPure, Category = "Universe|Frame")
    float GetFrameDominance() const { return static_cast<float>(FrameSelector.GetDominance()); }

    /** How many times the frame has changed this session. Catches flapping. */
    UFUNCTION(BlueprintPure, Category = "Universe|Frame")
    int32 GetFrameTransitionCount() const { return FrameSelector.GetTransitionCount(); }

    /**
     * Gravitational acceleration at a universe position, in m/s^2, in universe
     * axes. Zero in the interstellar frame.
     *
     * The single source of gravity in the project. Nothing else may compute a
     * down vector - see PlanetGravity.h for why a hardcoded axis is not merely
     * inelegant here but wrong.
     */
    FVector3d GetGravityAccelerationMs2(const FUniversePosition& Position) const;

    /**
     * Local up at a universe position: away from the frame planet centre.
     *
     * In the interstellar frame there is no up, and this returns +Z. That is a
     * fallback for code that must have some basis to build a rotation from,
     * not a claim that the universe has a preferred axis; callers that care
     * should check the frame first.
     */
    FVector3d GetLocalUp(const FUniversePosition& Position) const;

    /** Broadcast after the frame changes, with the new state. */
    DECLARE_MULTICAST_DELEGATE_OneParam(FOnSimulationFrameChanged, const FUniverseFrameState&);
    FOnSimulationFrameChanged OnSimulationFrameChanged;

private:
    void ResyncAllAnchors();
    void UpdateNearestSystem();
    void UpdateSimulationFrame();

    /** Shows or hides everything anchored in scaled astronomical space. */
    void ApplyScaledSpaceVisibility(bool bVisible);

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

    /** Planets eligible to claim the player, and the one that currently has. */
    TArray<TWeakObjectPtr<APlanetActor>> Planets;
    TWeakObjectPtr<APlanetActor> FramePlanet;

    FSimulationFrameSelector FrameSelector;

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
