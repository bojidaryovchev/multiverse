// Copyright Universe Project. All Rights Reserved.

#include "AstronomicalBodyActor.h"
#include "UniverseAnchorComponent.h"
#include "UniverseWorldSubsystem.h"
#include "UniverseScale.h"
#include "StarSystemGenerator.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    /** The engine sphere primitive has a 50 cm radius at unit scale. */
    constexpr double EngineSphereRadiusCm = 50.0;
}

AAstronomicalBodyActor::AAstronomicalBodyActor()
{
    // Bodies do not tick: their positions are fixed for the sprint and the
    // anchor updates them on rebase. Orbital motion arrives with a proper
    // time model, not a per-actor tick.
    PrimaryActorTick.bCanEverTick = false;

    RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
    SetRootComponent(RootScene);

    BodyMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("BodyMesh"));
    BodyMesh->SetupAttachment(RootScene);
    BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(
        TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if (SphereMesh.Succeeded())
    {
        BodyMesh->SetStaticMesh(SphereMesh.Object);
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (BasicMaterial.Succeeded())
    {
        BodyMesh->SetMaterial(0, BasicMaterial.Object);
    }

    StarLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StarLight"));
    StarLight->SetupAttachment(RootScene);
    StarLight->SetVisibility(false);
    StarLight->SetCastShadows(false);

    Anchor = CreateDefaultSubobject<UUniverseAnchorComponent>(TEXT("UniverseAnchor"));
    // Astronomical bodies are viewed from far away and belong in scaled space.
    Anchor->RenderSpace = EUniverseRenderSpace::ScaledAstronomical;
}

void AAstronomicalBodyActor::ApplyScaledRadius(double RadiusMeters)
{
    LogicalRadiusMeters = RadiusMeters;

    const UUniverseWorldSubsystem* Subsystem =
        GetWorld() != nullptr ? GetWorld()->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;

    const double Scale = Subsystem != nullptr
        ? Subsystem->GetPresentation().AstronomicalScale
        : 1.0e-7;

    const double Inflation = Subsystem != nullptr
        ? Subsystem->GetPresentation().DebugBodyRadiusInflation
        : 1.0;

    // Real radius -> real centimetres -> scaled centimetres -> mesh scale.
    // The same factor scales positions, which is what keeps angular sizes
    // truthful.
    const double RenderRadiusCm = RadiusMeters * UniverseScale::CmPerMeter * Scale * Inflation;
    const double MeshScale = RenderRadiusCm / EngineSphereRadiusCm;

    if (BodyMesh != nullptr)
    {
        BodyMesh->SetWorldScale3D(FVector(MeshScale, MeshScale, MeshScale));
    }
}

void AAstronomicalBodyActor::ApplyColour(const FLinearColor& Colour)
{
    if (BodyMesh == nullptr)
    {
        return;
    }

    // BasicShapeMaterial exposes a "Color" parameter. Setting a parameter that
    // does not exist is a no-op rather than an error, so this stays safe if
    // the engine's placeholder material changes.
    if (UMaterialInstanceDynamic* Dynamic = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
    {
        Dynamic->SetVectorParameterValue(TEXT("Color"), Colour);
    }
}

void AAstronomicalBodyActor::InitialiseAsStar(const FStarSystemDescriptor& System)
{
    BodyName = System.Star.Name;

    ApplyScaledRadius(System.Star.RadiusMeters);

    const FLinearColor Colour(
        static_cast<float>(System.Star.ColorR),
        static_cast<float>(System.Star.ColorG),
        static_cast<float>(System.Star.ColorB),
        1.0f);

    ApplyColour(Colour);

    if (Anchor != nullptr)
    {
        Anchor->SetUniversePosition(System.Position);
    }

    if (StarLight != nullptr)
    {
        StarLight->SetVisibility(true);
        StarLight->SetLightColor(Colour);

        // Intensity and reach are presentation values, not physics. A real
        // inverse-square falloff from a star's true luminosity would either
        // blow out the exposure or vanish entirely at scaled-space distances;
        // photometric lighting arrives with the scaled-space camera.
        StarLight->SetIntensity(static_cast<float>(
            FMath::Clamp(System.Star.LuminositySolar, 0.05, 20.0) * 5.0e6));
        StarLight->SetAttenuationRadius(1.0e7f);
    }

    // Stars are emissive; the mesh should not be lit by its own light.
    if (BodyMesh != nullptr)
    {
        BodyMesh->SetCastShadow(false);
    }
}

void AAstronomicalBodyActor::InitialiseAsPlanet(
    const FStarSystemDescriptor& System,
    const FPlanetDescriptor& Planet)
{
    BodyName = Planet.Name;

    ApplyScaledRadius(Planet.RadiusMeters);

    // Placeholder palette by type. Cosmetic only - biome and surface colour
    // are generated from the planet seed once terrain exists.
    FLinearColor Colour = FLinearColor::Gray;
    switch (Planet.Type)
    {
    case EPlanetType::Molten:      Colour = FLinearColor(0.85f, 0.25f, 0.08f); break;
    case EPlanetType::Rocky:       Colour = FLinearColor(0.45f, 0.42f, 0.38f); break;
    case EPlanetType::Desert:      Colour = FLinearColor(0.80f, 0.65f, 0.35f); break;
    case EPlanetType::Ocean:       Colour = FLinearColor(0.10f, 0.35f, 0.75f); break;
    case EPlanetType::Terrestrial: Colour = FLinearColor(0.20f, 0.55f, 0.30f); break;
    case EPlanetType::Ice:         Colour = FLinearColor(0.80f, 0.90f, 0.95f); break;
    case EPlanetType::GasGiant:    Colour = FLinearColor(0.80f, 0.70f, 0.50f); break;
    case EPlanetType::IceGiant:    Colour = FLinearColor(0.35f, 0.65f, 0.85f); break;
    default: break;
    }
    ApplyColour(Colour);

    if (Anchor != nullptr)
    {
        // Placement comes from the generator, not from this actor, so that the
        // game mode, the HUD markers and this view can never disagree about
        // where a planet is.
        Anchor->SetUniversePosition(FStarSystemGenerator::GetPlanetPosition(System, Planet));
    }

    if (StarLight != nullptr)
    {
        StarLight->SetVisibility(false);
    }
}
