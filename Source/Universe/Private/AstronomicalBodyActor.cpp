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

    /** Illuminance from the Sun at 1 AU, in lux. */

    /**
     * Point-light intensity, in candelas, for a star of the given luminosity.
     *
     * Inverse-square illumination does not survive spatial compression, and
     * this is the correction. Unreal computes lux as intensity / distance^2 in
     * metres, but scaled space has already shrunk every distance by the factor
     * S, so feeding it a real-world candela figure would mislight the scene by
     * 1/S^2 - fourteen orders of magnitude at the default scale. That is why
     * the first attempt rendered a black screen.
     *
     * Because the compression is uniform, the correction collapses to a
     * constant. Requiring the rendered illuminance at real distance r to equal
     * the true value E0 * L * (AU/r)^2, with render distance d = r * S:
     *
     *     I / (r*S)^2  =  E0 * L * AU^2 / r^2
     *     I            =  E0 * L * AU^2 * S^2
     *
     * The r^2 cancels, so a single intensity is correct at every orbit and the
     * relative falloff between planets stays physically truthful.
     */
    double ComputeStarIntensityCandelas(double LuminositySolar, double AstronomicalScale)
    {
        const double AuMeters = UniverseScale::MetersPerAu;
        return UniversePhysics::SolarIlluminanceAt1AuLux
            * FMath::Max(LuminositySolar, 1.0e-4)
            * AuMeters * AuMeters
            * AstronomicalScale * AstronomicalScale;
    }
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

    // Two materials: planets are lit by their star, stars light themselves.
    //
    // A star drawn with a lit material is black from every angle, because its
    // own light sits at its centre and is therefore always behind whichever
    // surface faces the camera. An emissive (unlit) material is the only way a
    // star reads as a star.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> LitMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    if (LitMaterial.Succeeded())
    {
        PlanetMaterial = LitMaterial.Object;
        BodyMesh->SetMaterial(0, LitMaterial.Object);
    }

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> EmissiveMaterial(
        TEXT("/Engine/EngineMaterials/EmissiveMeshMaterial.EmissiveMeshMaterial"));
    if (EmissiveMaterial.Succeeded())
    {
        StarMaterial = EmissiveMaterial.Object;
    }

    StarLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("StarLight"));
    StarLight->SetupAttachment(RootScene);
    StarLight->SetVisibility(false);
    StarLight->SetCastShadows(false);

    // Lighting channel 1, not the default 0.
    //
    // This light lives in ScaledAstronomical space, where distances are shrunk
    // by 1e-7, while planet terrain lives in Local space at 1:1. The Unreal
    // distance between them is therefore meaningless, and a point light with a
    // scaled-space intensity lands on local-space terrain at an essentially
    // arbitrary illuminance - measured at 68 lux from 400 km and 950,000 lux
    // from 4 km, which blew the exposure out completely.
    //
    // Restricting this light to channel 1 keeps it lighting only the
    // scaled-space placeholder bodies it was computed for. Terrain gets its own
    // correctly-scaled directional light on channel 0.
    StarLight->LightingChannels.bChannel0 = false;
    StarLight->LightingChannels.bChannel1 = true;

    // The placeholder bodies live in the same space as that light, so they move
    // to the same channel.
    BodyMesh->LightingChannels.bChannel0 = false;
    BodyMesh->LightingChannels.bChannel1 = true;

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

void AAstronomicalBodyActor::ApplyColour(const FLinearColor& Colour, bool bEmissive)
{
    if (BodyMesh == nullptr)
    {
        return;
    }

    UMaterialInterface* Base = bEmissive ? StarMaterial.Get() : PlanetMaterial.Get();
    if (Base != nullptr)
    {
        BodyMesh->SetMaterial(0, Base);
    }

    FLinearColor Tint = Colour;
    if (bEmissive)
    {
        const UUniverseWorldSubsystem* Subsystem =
            GetWorld() != nullptr ? GetWorld()->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;
        const double Brightness = Subsystem != nullptr
            ? Subsystem->GetPresentation().StarEmissiveBrightness
            : 100000000.0;
        Tint = Colour * static_cast<float>(Brightness);
        Tint.A = 1.0f;
    }

    // Both engine placeholder materials expose exactly one vector parameter,
    // "Color" (confirmed by enumerating them at runtime rather than guessing).
    if (UMaterialInstanceDynamic* Dynamic = BodyMesh->CreateAndSetMaterialInstanceDynamic(0))
    {
        Dynamic->SetVectorParameterValue(TEXT("Color"), Tint);
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

    ApplyColour(Colour, /*bEmissive=*/true);

    if (Anchor != nullptr)
    {
        Anchor->SetUniversePosition(System.Position);
    }

    if (StarLight != nullptr)
    {
        const UUniverseWorldSubsystem* Subsystem =
            GetWorld() != nullptr ? GetWorld()->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;
        const double Scale = Subsystem != nullptr
            ? Subsystem->GetPresentation().AstronomicalScale
            : 1.0e-7;

        StarLight->SetVisibility(true);
        StarLight->SetLightColor(Colour);
        StarLight->SetIntensityUnits(ELightUnits::Candelas);
        StarLight->SetIntensity(static_cast<float>(
            ComputeStarIntensityCandelas(System.Star.LuminositySolar, Scale)));

        // Reach far enough to cover a whole planetary system in scaled space:
        // 200 AU, comfortably beyond the outermost orbit the generator emits.
        const double ReachCm = 200.0 * UniverseScale::CmPerAu * Scale;
        StarLight->SetAttenuationRadius(static_cast<float>(ReachCm));
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
    ApplyColour(Colour, /*bEmissive=*/false);

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
