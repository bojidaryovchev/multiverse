// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PlanetSurface.h"
#include "PlanetSurfaceQuery.h"
#include "PlanetGravity.h"
#include "SimulationFrame.h"
#include "PlanetEnvironment.h"
#include "UniverseCoordinates.h"
#include "PlanetActor.generated.h"

class UDirectionalLightComponent;
class USkyAtmosphereComponent;
class UVolumetricCloudComponent;
class USkyLightComponent;
class UPlanetTerrainComponent;
class UPlanetVegetationComponent;
class UPlanetWildlifeComponent;
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
 *
 *
 * ONE PLANET, ONE SCALE
 *
 * A body that has been promoted to a streaming planet has exactly one
 * representation in the world, at true scale. The game mode destroys its scaled
 * placeholder when it promotes it, and that is load-bearing rather than
 * cosmetic: two representations of one planet, at scales seven orders of
 * magnitude apart, would give every query about "the planet" two different
 * answers depending on which actor the caller happened to find. The logical
 * radius in FPlanetSurfaceDescriptor drives traversal, gravity, altitude and
 * collision alike, and no presentation scale is applied anywhere on that path.
 *
 *
 * WHAT THIS ACTOR ANSWERS
 *
 * It is also the bridge for gameplay queries. Everything a character, a ship
 * or a spawner wants to know - where the ground is, which way is up, how hard
 * gravity pulls, whether a movement step would pass through the body - is
 * asked here, in universe coordinates, and answered by converting into
 * planet-local metres and calling into UniversePlanet. Concentrating the
 * conversion in one place is the point: a caller that did it itself would be
 * one sign error away from a character standing on the wrong side of a world.
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
        const struct FPlanetDescriptor& InAstronomy,
        const FPlanetTerrainSettings& InSettings,
        const FUniversePosition& InStarPosition,
        double InStarLuminositySolar);

    const FPlanetSurfaceDescriptor& GetPlanetDescriptor() const { return PlanetDescriptor; }

    UPlanetTerrainComponent* GetTerrainComponent() const { return TerrainComponent; }
    UPlanetVegetationComponent* GetVegetationComponent() const { return VegetationComponent; }
    UPlanetWildlifeComponent* GetWildlifeComponent() const { return WildlifeComponent; }
    UUniverseAnchorComponent* GetAnchor() const { return Anchor; }

    // --- Rotation and time of day -----------------------------------------

    /**
     * Local solar elevation at a universe position, in degrees.
     *
     * 90 is the sun overhead, 0 the horizon, negative is night. The single
     * question every time-of-day consumer actually asks.
     */
    double GetSolarElevationDegrees(const FUniversePosition& UniversePosition) const;

    /** Fraction of the local day elapsed, [0, 1). 0.5 is local noon. */
    double GetTimeOfDayFraction(const FUniversePosition& UniversePosition) const;

    /** Simulation seconds elapsed on this planet, driving rotation and weather. */
    double GetSimulationTimeSeconds() const { return SimulationTimeSeconds; }

    /** Observer altitude above the sea-level radius, metres. */
    UFUNCTION(BlueprintPure, Category = "Universe|Planet")
    double GetObserverAltitudeMeters() const { return LastObserverAltitudeMeters; }

    const FPlanetTerrainSettings& GetTerrainSettings() const { return TerrainSettings; }

    /** This planet's environmental identity: climate, ocean, biosphere. */
    const FPlanetEnvironmentDescriptor& GetEnvironment() const { return EnvironmentDescriptor; }

    /** Planet-local observer position in metres, from a universe position. */
    FVector3d UniverseToPlanetLocalMeters(const FUniversePosition& UniversePosition) const;

    /** The inverse: a planet-local position in metres, as a universe position. */
    FUniversePosition PlanetLocalMetersToUniverse(const FVector3d& PlanetLocalMeters) const;

    // --- Gravity and frame -----------------------------------------------

    /** The gravity field of this body. */
    FPlanetGravityField GetGravityField() const { return FPlanetGravityField::FromPlanet(PlanetDescriptor); }

    /** The radii at which this body claims and releases the observer. */
    const FPlanetFrameBounds& GetFrameBounds() const { return FrameBounds; }

    /** This body as a candidate for the frame selector, at a universe position. */
    FPlanetFrameCandidate MakeFrameCandidate(const FUniversePosition& UniversePosition) const;

    /**
     * Gravitational acceleration at a universe position, in m/s^2, expressed
     * in universe axes.
     *
     * Universe axes rather than planet-local ones because the two are parallel
     * - the planet-local frame is a translation, not a rotation - so the vector
     * needs no transform, and saying so here saves every caller from wondering.
     */
    FVector3d GetGravityAccelerationMs2(const FUniversePosition& UniversePosition) const;

    /** Local up at a universe position: away from this planet centre. */
    FVector3d GetLocalUp(const FUniversePosition& UniversePosition) const;

    // --- The surface ------------------------------------------------------

    /**
     * The ground directly below a universe position.
     *
     * Evaluates the terrain function, so it is not free - about a microsecond -
     * and is not something to call per-vertex. It is exact regardless of
     * whether the patch under that point has been streamed in, which is what
     * makes it usable for spawning and for teleport targets.
     */
    FPlanetSurfaceSample SampleSurfaceBelow(const FUniversePosition& UniversePosition) const;

    /** Height above the ground directly below, in metres. See PlanetSurfaceQuery.h. */
    UFUNCTION(BlueprintPure, Category = "Universe|Planet")
    double GetAltitudeAboveTerrainMeters(const FVector& RenderLocation) const;

    /** Height above the sea-level reference sphere, in metres. Cheap. */
    double GetAltitudeAboveSeaLevelMeters(const FUniversePosition& UniversePosition) const;

    /** Distance from the planet centre, in metres. */
    double GetDistanceFromCentreMeters(const FUniversePosition& UniversePosition) const;

    /** Normalised depth in the atmosphere: 0 at the top, 1 at sea level. */
    double GetAtmosphericDepthFraction(const FUniversePosition& UniversePosition) const;

    // --- Placement --------------------------------------------------------

    /**
     * A universe position a given height above the ground, along a
     * planet-local direction. The primitive behind surface spawning.
     */
    FUniversePosition GetUniversePositionAboveTerrain(
        const FVector3d& Direction,
        double HeightAboveTerrainMeters) const;

    /**
     * Calibrates a camera's exposure to this planet's actual illuminance.
     *
     * Sprint 002 clamped the star light to 25,000 lux "for display", noting
     * that real photometric range was Sprint 003 work. This is that work, and
     * the clamp is gone.
     *
     * The clamp existed because a physically-lit scene has no meaning without a
     * physically-set camera: auto-exposure has to guess, and it guesses badly
     * when a scene contains both an emissive star and ground lit at a million
     * lux. Adding a sky made it worse - a scattering atmosphere fills most of
     * the frame at sun-adjacent brightness, and the result was a white image.
     *
     * The fix is the one a photographer would use. The illuminance is known
     * exactly, so the exposure it calls for is known exactly, and the camera is
     * simply set to it: fixed ISO and aperture, shutter derived from the scene.
     * At Earth's 1 AU this lands within a third of a stop of the sunny f/16
     * rule, which is a reassuring sign that the units are right rather than
     * merely self-consistent.
     */
    void ApplyExposureTo(class UCameraComponent* Camera) const;

    /** Illuminance at this planet from its star, in lux. Physical, unclamped. */
    double GetStarIlluminanceLux() const { return StarIlluminanceLux; }

    /**
     * The surface direction where the star is currently overhead.
     *
     * Distinct from GetStarDirection, and the difference is the rotation. The
     * star direction is fixed in universe axes; the sub-stellar *point* moves
     * across the surface as the planet turns, which is what a day is. Anything
     * asking "where is it noon" wants this one.
     */
    FVector3d GetSubstellarDirection() const;

    /**
     * Unit direction from the planet centre toward its star, planet-local.
     *
     * Also the sub-stellar point: the place on the surface where the star is
     * directly overhead, and therefore local noon. Its negation is midnight.
     * With no rotation model yet, day and night are a function of *where* you
     * are rather than of when - which is a real limitation, but the geometry
     * is the same geometry, so the lighting is right at every point even
     * though it does not yet change over time.
     */
    FVector3d GetStarDirection() const;

    /**
     * A deterministic-but-arbitrary direction on the planet, for spawn points
     * that want somewhere rather than somewhere in particular.
     *
     * Derived from the planet seed and an index, so the same index always
     * gives the same place on the same planet - a debug teleport that landed
     * somewhere different each run would make comparing two runs impossible.
     */
    FVector3d GetSpawnDirection(int32 Index) const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<USceneComponent> RootScene;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UUniverseAnchorComponent> Anchor;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UPlanetTerrainComponent> TerrainComponent;

    /**
     * Trees, plants and rocks, streamed around the observer.
     *
     * A sibling of the terrain streamer rather than a child of it: the two
     * follow the same observer but at different resolutions and different
     * ranges, and coupling their lifetimes serves neither. See
     * PlanetVegetationComponent.h.
     */
    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UPlanetVegetationComponent> VegetationComponent;

    /** Birds. See PlanetWildlifeComponent.h for why they are not Actors. */
    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UPlanetWildlifeComponent> WildlifeComponent;

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

    /**
     * Sky and aerial perspective, when the body has an atmosphere.
     *
     * Unreal's sky atmosphere is one of the few stock rendering features that
     * is already planet-shaped rather than level-shaped: it is parameterised by
     * a planet centre, a ground radius and an atmosphere height, and it
     * computes scattering along the view ray through a spherical shell. So the
     * logical atmosphere this project defines - the one that decides drag and
     * where a flight model changes - can be handed to it directly, in the same
     * kilometres, rather than approximated with height fog that assumes a flat
     * world and a world-Z up.
     *
     * That is the whole reason this is worth doing rather than faking. The
     * boundary a player can see and the boundary the simulation acts on are the
     * same number, read from the same field of the same descriptor. A visual
     * cue that drifted from the simulation would be worse than none.
     */
    /**
     * The sun the atmosphere scatters, which illuminates nothing itself.
     *
     * See the comment in the constructor: an atmosphere sun light is attenuated
     * by atmospheric transmittance, and at planetary radii that attenuation
     * evaluates to zero on the ground. Separating "the light that makes the sky
     * blue" from "the light that lands on the terrain" is what keeps both
     * correct.
     */
    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UDirectionalLightComponent> AtmosphereSunLight;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<USkyAtmosphereComponent> SkyAtmosphere;

    /**
     * Clouds, when the body has air to hold them.
     *
     * Unreal's volumetric cloud component is, like the sky atmosphere,
     * already planet-shaped: it is defined by a layer bottom and thickness
     * measured from the same planet centre the atmosphere uses, so it wraps a
     * sphere rather than sitting on a plane. That is what makes it usable here
     * at all - a plane of clouds is fine over a level and wrong over a world.
     *
     * Coverage and density come from the planet descriptor, so a dry world has
     * a clear sky and a humid one does not, for the same reason its biomes are
     * wetter.
     */
    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<UVolumetricCloudComponent> Clouds;

    /**
     * Ambient light from the sky.
     *
     * Without it the scene has exactly one light and no bounce at all, so
     * anything the sun does not reach directly is pure black - and once an
     * atmosphere is scattering above it, most of the ground is in that
     * category. A real overcast day is lit almost entirely by the sky, and a
     * scene with no ambient renders it as midnight.
     *
     * Captured from the scene so it picks up whatever colour the atmosphere is
     * actually producing, rather than being a hand-set fill that would be wrong
     * on every planet but the one it was tuned on.
     */
    UPROPERTY(VisibleAnywhere, Category = "Universe|Planet")
    TObjectPtr<USkyLightComponent> SkyLight;

private:
    FPlanetSurfaceDescriptor PlanetDescriptor;
    FPlanetEnvironmentDescriptor EnvironmentDescriptor;
    FPlanetTerrainSettings TerrainSettings;

    double LastObserverAltitudeMeters = 0.0;

    /**
     * Planet time, in seconds, advanced by the tick and scaled by
     * universe.TimeScale.
     *
     * Owned by the planet rather than read from the world clock, because it has
     * to survive being accelerated for debugging without dragging physics with
     * it - and because a planet's day is its own property, not the session's.
     */
    double SimulationTimeSeconds = 0.0;

    /** Rotation about the planet axis at time zero, radians. Seed-derived. */
    double RotationPhaseAtEpoch = 0.0;

    /** Total rotation since epoch, radians. */
    double GetRotationAngleRadians() const;

    /** Points the star light for the planet's current rotation. */
    void UpdateStarLightDirection();

    FPlanetFrameBounds FrameBounds;

    FUniversePosition StarPosition;
    double StarLuminositySolar = 1.0;

    /** Physical illuminance at this planet, lux. Drives light and exposure. */
    double StarIlluminanceLux = 128000.0;
};
