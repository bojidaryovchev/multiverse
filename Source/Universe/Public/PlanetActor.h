// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetSurface.h"
#include "PlanetActor.generated.h"

class UDirectionalLightComponent;
class UPlanetTerrainComponent;
class UUniverseAnchorComponent;

/**
 * APlanetActor
 *
 * A planet's presence in the Unreal world: an anchor at its centre and a
 * terrain streamer hanging off it.
 *
 *
 * HOW THE COORDINATE LAYERS MEET
 *
 *     universe position of the planet centre     (Sprint 001, FUniversePosition)
 *                    +
 *     planet-local metres                        (Sprint 002, terrain)
 *                    v
 *     render origin relative transform           (Sprint 001, rebasing)
 *                    v
 *     Unreal actor transform
 *
 * The planet centre is an FUniversePosition on the anchor component, exactly
 * like the probe. Terrain is generated in planet-local metres and never knows
 * where the planet sits in the universe. Patch components are children of this
 * actor, so a render-origin rebase moves the whole planet by moving one actor
 * and every patch follows - no per-patch fixup, and no possibility of a patch
 * being left behind at a stale origin.
 *
 * The observer position handed to the streamer is computed in planet-local
 * space from universe coordinates, so it stays exact regardless of where the
 * planet is or how far the player has travelled.
 *
 * Terrain renders in Local space at 1:1, not the scaled astronomical space the
 * placeholder bodies use. A planet you are standing on has to be actual size;
 * scaled space is for things still being looked at from across a solar system.
 * Bridging the two continuously is the Sprint 003 problem.
 */
UCLASS()
class UNIVERSE_API APlanetActor : public AActor
{
    GENERATED_BODY()

public:
    APlanetActor();

    virtual void Tick(float DeltaSeconds) override;

    /**
     * Configures which planet this actor represents, and where its star is.
     *
     * The star is passed as a universe position and a luminosity rather than an
     * Actor: a star seen from a planet is effectively a directional light, and
     * the direction has to be computed in universe coordinates because the star
     * Actor lives in a different render space entirely.
     */
    void Initialise(
        const FPlanetSurfaceDescriptor& InPlanet,
        const FPlanetTerrainSettings& InSettings,
        const struct FUniversePosition& InStarPosition,
        double InStarLuminositySolar);

    const FPlanetSurfaceDescriptor& GetPlanetDescriptor() const { return PlanetDescriptor; }

    UPlanetTerrainComponent* GetTerrainComponent() const { return TerrainComponent; }
    UUniverseAnchorComponent* GetAnchor() const { return Anchor; }

    /** Observer altitude above the sea-level radius, metres. */
    UFUNCTION(BlueprintPure, Category = "Universe|Planet")
    double GetObserverAltitudeMeters() const { return LastObserverAltitudeMeters; }

    /** Planet-local observer position in metres, from a universe position. */
    FVector3d UniverseToPlanetLocalMeters(const struct FUniversePosition& UniversePosition) const;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<USceneComponent> RootScene;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UUniverseAnchorComponent> Anchor;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UPlanetTerrainComponent> TerrainComponent;

    /**
     * The star, as it appears from this planet.
     *
     * Directional rather than a point light because the star is astronomically
     * far away relative to the planet: its rays are parallel to well within any
     * measurable tolerance, and a directional light needs no distance and so is
     * immune to the render-space mismatch that makes a point light unusable
     * here.
     */
    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UDirectionalLightComponent> StarLight;

private:
    FPlanetSurfaceDescriptor PlanetDescriptor;
    FPlanetTerrainSettings TerrainSettings;

    double LastObserverAltitudeMeters = 0.0;

    FUniversePosition StarPosition;
    double StarLuminositySolar = 1.0;
};
