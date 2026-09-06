// Copyright Universe Project. All Rights Reserved.

#include "PlanetActor.h"
#include "PlanetTerrainComponent.h"
#include "UniverseAnchorComponent.h"
#include "UniverseWorldSubsystem.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/DirectionalLightComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanetActor, Log, All);

APlanetActor::APlanetActor()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
    SetRootComponent(RootScene);

    Anchor = CreateDefaultSubobject<UUniverseAnchorComponent>(TEXT("UniverseAnchor"));
    // A planet you can land on must be actual size. Scaled astronomical space
    // is for bodies still being viewed from across a solar system.
    Anchor->RenderSpace = EUniverseRenderSpace::Local;

    TerrainComponent = CreateDefaultSubobject<UPlanetTerrainComponent>(TEXT("Terrain"));

    StarLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("StarLight"));
    StarLight->SetupAttachment(RootScene);
    // A directional light's intensity is always lux in UE5 - there is no unit
    // to select, unlike a point light.

    // Channel 0 only: this light is for terrain, and must not also hit the
    // scaled-space placeholder bodies, which are lit on channel 1.
    StarLight->LightingChannels.bChannel0 = true;
    StarLight->LightingChannels.bChannel1 = false;

    // Shadows off for now. A directional shadow across a body 5000 km wide
    // needs cascades tuned for planetary scale, which is Sprint 003 work; the
    // terrain reads perfectly well from diffuse shading alone.
    StarLight->SetCastShadows(false);

    // An engine material that shades by vertex colour.
    //
    // Sprint 002 wants terrain readability rather than environment art, and
    // this gets it with no authored content: the backend writes elevation, LOD,
    // cube face or a patch checkerboard into vertex colour and the material
    // shows it. Being lit is wanted here - diffuse shading is what makes relief
    // legible, and the planet's own directional light provides it at a correct
    // physical intensity.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexColorMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    if (VertexColorMaterial.Succeeded() && TerrainComponent != nullptr)
    {
        TerrainComponent->TerrainMaterial = VertexColorMaterial.Object;
    }
}

void APlanetActor::Initialise(
    const FPlanetSurfaceDescriptor& InPlanet,
    const FPlanetTerrainSettings& InSettings,
    const FUniversePosition& InStarPosition,
    double InStarLuminositySolar)
{
    PlanetDescriptor = InPlanet;
    TerrainSettings = InSettings;
    StarPosition = InStarPosition;
    StarLuminositySolar = InStarLuminositySolar;

    if (StarLight != nullptr)
    {
        // Illuminance from the inverse-square law, in real units, computed from
        // the real separation in universe coordinates. Nothing here touches an
        // Unreal transform, so the scaled-space mismatch cannot creep back in.
        const double DistanceAu = FUniversePosition::DistanceAu(PlanetDescriptor.Position, StarPosition);
        const double SafeDistanceAu = FMath::Max(DistanceAu, 1.0e-6);

        constexpr double SolarIlluminanceAt1AuLux = 128000.0;
        const double PhysicalLux =
            SolarIlluminanceAt1AuLux * StarLuminositySolar / (SafeDistanceAu * SafeDistanceAu);

        // Clamped for display, not because the physical value is wrong.
        //
        // This planet orbits at 0.23 AU, where the true illuminance is about
        // 950,000 lux - roughly seven times Earth noon. Feeding that to the
        // tonemapper alongside an emissive star in the same frame washes the
        // terrain out completely, because auto-exposure has to straddle both.
        // Clamping to bright-daylight levels keeps relief readable, which is
        // what Sprint 002 asks of the visuals. Real photometric range is part
        // of the atmosphere and scaled-space camera work in Sprint 003.
        constexpr double DisplayMaxLux = 25000.0;
        const double Lux = FMath::Clamp(PhysicalLux, 100.0, DisplayMaxLux);

        StarLight->SetIntensity(static_cast<float>(Lux));

        // Point the light along the star-to-planet direction.
        const FVector3d ToPlanet =
            FUniversePosition::DirectionUnit(StarPosition, PlanetDescriptor.Position);
        if (!ToPlanet.IsZero())
        {
            StarLight->SetWorldRotation(FVector(ToPlanet.X, ToPlanet.Y, ToPlanet.Z).Rotation());
        }

        UE_LOG(LogPlanetActor, Log,
            TEXT("Star light: %.4f AU away, %.0f lux physical, %.0f lux displayed"),
            DistanceAu, PhysicalLux, Lux);
    }

    if (Anchor != nullptr)
    {
        Anchor->SetUniversePosition(PlanetDescriptor.Position);
    }

    if (TerrainComponent != nullptr)
    {
        TerrainComponent->SetPlanet(PlanetDescriptor, TerrainSettings);
    }

    UE_LOG(LogPlanetActor, Log, TEXT("Planet initialised: %s"), *PlanetDescriptor.ToDebugString());
}

void APlanetActor::BeginPlay()
{
    Super::BeginPlay();

    if (TerrainComponent != nullptr && PlanetDescriptor.IsValid())
    {
        TerrainComponent->SetPlanet(PlanetDescriptor, TerrainSettings);
    }
}

FVector3d APlanetActor::UniverseToPlanetLocalMeters(const FUniversePosition& UniversePosition) const
{
    // Straight through the Sprint 001 coordinate system: the offset from the
    // planet centre in exact universe space, converted to metres. No Unreal
    // transform is involved, so this stays correct however far the player has
    // travelled and wherever the render origin currently sits.
    FVector3d RelativeCm;
    if (!FUniversePosition::TryGetRelativeCm(PlanetDescriptor.Position, UniversePosition, RelativeCm))
    {
        // Beyond the representable range means astronomically far from this
        // planet. Returning a point far outside the bounding sphere makes the
        // quadtree select the coarsest representation, which is correct.
        const double Far = PlanetDescriptor.RadiusMeters * 1.0e6;
        return FVector3d(Far, 0.0, 0.0);
    }

    return FVector3d(
        RelativeCm.X * UniverseScale::MetersPerCm,
        RelativeCm.Y * UniverseScale::MetersPerCm,
        RelativeCm.Z * UniverseScale::MetersPerCm);
}

void APlanetActor::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (TerrainComponent == nullptr || !PlanetDescriptor.IsValid())
    {
        return;
    }

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    // The observer is whoever the render origin is following - normally the
    // probe. Taken from its canonical universe position rather than its Unreal
    // transform, so a rebase mid-frame cannot make the terrain flinch.
    FUniversePosition ObserverUniverse;
    bool bHaveObserver = false;

    if (const AUniverseProbePawn* Probe =
            Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
    {
        ObserverUniverse = Probe->GetUniversePosition();
        bHaveObserver = true;
    }
    else if (const UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        ObserverUniverse = Subsystem->GetRenderOrigin();
        bHaveObserver = true;
    }

    if (!bHaveObserver)
    {
        return;
    }

    const FVector3d ObserverLocal = UniverseToPlanetLocalMeters(ObserverUniverse);
    TerrainComponent->SetObserverPositionMeters(ObserverLocal);

    LastObserverAltitudeMeters = ObserverLocal.Size() - PlanetDescriptor.RadiusMeters;
}
