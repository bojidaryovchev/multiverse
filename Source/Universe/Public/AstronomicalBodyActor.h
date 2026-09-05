// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "StarSystemDescriptor.h"
#include "AstronomicalBodyActor.generated.h"

class UMaterialInterface;
class UPointLightComponent;
class UStaticMeshComponent;
class UUniverseAnchorComponent;

/**
 * AAstronomicalBodyActor
 *
 * The visible placeholder for a star or a planet.
 *
 * It is a pure view. It holds a copy of the descriptor it was built from and
 * derives everything - radius, colour, position - from those logical values
 * through the scaled-space transform. It never edits the descriptor and never
 * becomes the source of truth for anything, so deleting every one of these
 * actors loses nothing: they can be rebuilt from the seed at any time.
 *
 * Geometry is an engine primitive sphere. That is deliberate for Sprint 001:
 * a real planet is a cube-sphere with a quadtree LOD and streamed surface
 * patches, and building a placeholder that pretends to be one would invite
 * exactly the confusion between presentation and simulation that the
 * architecture is set up to avoid.
 */
UCLASS()
class UNIVERSE_API AAstronomicalBodyActor : public AActor
{
    GENERATED_BODY()

public:
    AAstronomicalBodyActor();

    /** Builds this actor as the star of a system. */
    void InitialiseAsStar(const FStarSystemDescriptor& System);

    /** Builds this actor as one planet of a system. */
    void InitialiseAsPlanet(const FStarSystemDescriptor& System, const FPlanetDescriptor& Planet);

    /** Display name, for the debug HUD. */
    UFUNCTION(BlueprintPure, Category = "Universe|Body")
    FString GetBodyName() const { return BodyName; }

    /** True logical radius in metres - not the rendered radius. */
    UFUNCTION(BlueprintPure, Category = "Universe|Body")
    double GetLogicalRadiusMeters() const { return LogicalRadiusMeters; }

    UUniverseAnchorComponent* GetAnchor() const { return Anchor; }

protected:
    UPROPERTY(VisibleAnywhere, Category = "Universe|Body")
    TObjectPtr<USceneComponent> RootScene;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Body")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    /** Present only on stars. */
    UPROPERTY(VisibleAnywhere, Category = "Universe|Body")
    TObjectPtr<UPointLightComponent> StarLight;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Body")
    TObjectPtr<UUniverseAnchorComponent> Anchor;

private:
    /**
     * Applies the logical radius through the scaled-space factor.
     *
     * The engine sphere primitive is 100 cm in diameter, i.e. 50 cm radius, so
     * the mesh scale is (rendered radius in cm) / 50.
     */
    void ApplyScaledRadius(double RadiusMeters);

    /** Applies the tint, switching between the lit and emissive material. */
    void ApplyColour(const FLinearColor& Colour, bool bEmissive);

    /** Lit material used for planets. */
    UPROPERTY()
    TObjectPtr<UMaterialInterface> PlanetMaterial;

    /** Unlit emissive material used for stars. */
    UPROPERTY()
    TObjectPtr<UMaterialInterface> StarMaterial;

    UPROPERTY()
    FString BodyName;

    double LogicalRadiusMeters = 0.0;
};
