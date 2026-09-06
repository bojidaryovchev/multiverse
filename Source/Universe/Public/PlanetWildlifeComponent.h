// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "PlanetEnvironment.h"
#include "PlanetSurface.h"
#include "PlanetBiome.h"
#include "PlanetWildlifeComponent.generated.h"

class UInstancedStaticMeshComponent;
class UStaticMesh;

/**
 * UPlanetWildlifeComponent
 *
 * Birds, and the architecture that says a planet's animals do not all exist.
 *
 *
 * WHAT THIS IS ACTUALLY FOR
 *
 * The behaviour is trivial on purpose - fly toward a point, pick another,
 * avoid the ground. Nothing here is an AI system and nothing here should be
 * mistaken for one.
 *
 * What matters is the *contract*: a population is a number attached to a biome,
 * and individuals exist only where somebody is looking. A planet has birds in
 * the same sense that it has a temperature - as a property of a place, not as a
 * list of objects. Somewhere around ten million individuals would be plausible
 * on a world like this; at most a few dozen ever exist.
 *
 * Establishing that now is the whole point, because the alternative - an Actor
 * per animal, spawned when the world loads - is a decision that becomes
 * impossible to reverse once anything depends on animals having identity. This
 * component cannot support a bird the player has named and tamed. That is a
 * deliberate limit of the abstraction and the place to revisit it is when
 * something needs it.
 *
 *
 * WHY THEY ARE INSTANCES, NOT ACTORS
 *
 * Forty birds as Actors is forty ticking objects with transforms, components
 * and replication. Forty birds as instances in one component is one draw call
 * and an array of transforms updated in a loop. At this behavioural complexity
 * the Actor buys nothing at all.
 */
UCLASS(ClassGroup = (Universe), meta = (BlueprintSpawnableComponent))
class UNIVERSE_API UPlanetWildlifeComponent : public USceneComponent
{
    GENERATED_BODY()

public:
    UPlanetWildlifeComponent();

    virtual void TickComponent(
        float DeltaTime,
        ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    void SetPlanet(
        const FPlanetSurfaceDescriptor& InPlanet,
        const FPlanetEnvironmentDescriptor& InEnvironment,
        const FPlanetTerrainSettings& InSettings);

    void SetObserverPositionMeters(const FVector3d& InObserverMeters);

    UFUNCTION(BlueprintPure, Category = "Universe|Wildlife")
    int32 GetActiveCount() const { return Birds.Num(); }

    /** Birds per hectare a biome supports, before weather and time of day. */
    static double GetBirdDensityPerHectare(EPlanetBiome Biome);

    /** Radius within which birds exist at all, in metres. */
    UPROPERTY(EditAnywhere, Category = "Universe|Wildlife")
    float HabitatRadiusMeters = 260.0f;

    /** Hard ceiling on live individuals. */
    UPROPERTY(EditAnywhere, Category = "Universe|Wildlife")
    int32 MaxBirds = 48;

    /** Cruise speed, m/s. */
    UPROPERTY(EditAnywhere, Category = "Universe|Wildlife")
    float SpeedMs = 9.0f;

    /** Height band above the ground they occupy, in metres. */
    UPROPERTY(EditAnywhere, Category = "Universe|Wildlife")
    float MinHeightMeters = 12.0f;

    UPROPERTY(EditAnywhere, Category = "Universe|Wildlife")
    float MaxHeightMeters = 90.0f;

    /** Above this altitude above terrain, no wildlife exists. */
    UPROPERTY(EditAnywhere, Category = "Universe|Wildlife")
    float MaxObserverAltitudeMeters = 600.0f;

private:
    /** One individual. Position and target, and nothing else. */
    struct FBird
    {
        FVector3d PositionMeters = FVector3d::ZeroVector;
        FVector3d TargetMeters = FVector3d::ZeroVector;
        FVector3d HeadingUnit = FVector3d(1.0, 0.0, 0.0);
        double SpeedScale = 1.0;
        double AgeSeconds = 0.0;
    };

    void UpdatePopulation(double DeltaSeconds);
    void UpdateInstances();

    FVector3d PickTarget(const FVector3d& Around, uint64 Salt) const;

    FPlanetSurfaceDescriptor Planet;
    FPlanetEnvironmentDescriptor Environment;
    FPlanetTerrainSettings TerrainSettings;

    FVector3d ObserverMeters = FVector3d::ZeroVector;
    bool bHasObserver = false;

    TArray<FBird> Birds;

    /** How many the current place and conditions support. */
    int32 TargetPopulation = 0;

    double TimeSinceCensus = 0.0;
    uint64 SpawnCounter = 0;

    UPROPERTY(Transient)
    TObjectPtr<UInstancedStaticMeshComponent> Instances;
};
