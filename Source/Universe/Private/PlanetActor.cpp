// Copyright Universe Project. All Rights Reserved.

#include "PlanetActor.h"
#include "PlanetTerrainComponent.h"
#include "PlanetVegetationComponent.h"
#include "PlanetWildlifeComponent.h"
#include "PlanetStructureComponent.h"
#include "UniverseAnchorComponent.h"
#include "UniverseWorldSubsystem.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"
#include "UniverseHash.h"
#include "PlanetTrajectory.h"
#include "StarSystemDescriptor.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "Components/SkyLightComponent.h"
#include "Camera/CameraComponent.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanetActor, Log, All);

namespace
{
    /**
     * Multiplier on planet time.
     *
     * Days here are tens of hours, so watching a sunrise at 1x is not a test,
     * it is a shift. Acceleration is a debug tool and is deliberately applied
     * to *planet* time only - physics, movement and streaming all keep running
     * at real time, so a fast day cannot be confused with a fast simulation.
     */
    static TAutoConsoleVariable<float> CVarPlanetTimeScale(
        TEXT("universe.TimeScale"),
        1.0f,
        TEXT("Multiplier on planetary time: rotation, day/night and weather. 1 = real time."),
        ECVF_Cheat);
}

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

    VegetationComponent = CreateDefaultSubobject<UPlanetVegetationComponent>(TEXT("Vegetation"));
    VegetationComponent->SetupAttachment(RootScene);

    WildlifeComponent = CreateDefaultSubobject<UPlanetWildlifeComponent>(TEXT("Wildlife"));
    WildlifeComponent->SetupAttachment(RootScene);

    StructureComponent = CreateDefaultSubobject<UPlanetStructureComponent>(TEXT("Structures"));
    StructureComponent->SetupAttachment(RootScene);

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

    // Two lights, one sun.
    //
    // Unreal attenuates an atmosphere sun light by the atmospheric
    // transmittance to whatever it is shading. That is physically right and, at
    // this planet's scale, numerically hopeless: the transmittance is evaluated
    // in single precision against a shell whose radius is millions of units, and
    // the result at ground level came out as zero. The measured symptom was a
    // forest at noon under a correctly-lit blue sky, rendered pure black - and
    // switching the atmosphere off lit it perfectly.
    //
    // So the roles are split. StarLight lights the world and is not an
    // atmosphere sun; AtmosphereSunLight drives the sky's scattering and
    // illuminates nothing, having no lighting channels enabled. Both point the
    // same way, so the sun in the sky and the shadows on the ground still agree.
    //
    // What is lost is the atmosphere tinting the *ground* light - no red
    // sunsets on the terrain, only in the sky. That is a real cost and is
    // recorded as such; the alternative was a black planet.
    StarLight->bAtmosphereSunLight = false;

    AtmosphereSunLight = CreateDefaultSubobject<UDirectionalLightComponent>(TEXT("AtmosphereSunLight"));
    AtmosphereSunLight->SetupAttachment(RootScene);
    AtmosphereSunLight->bAtmosphereSunLight = true;
    AtmosphereSunLight->AtmosphereSunLightIndex = 0;
    AtmosphereSunLight->SetCastShadows(false);

    // Illuminates nothing: every channel off. It exists only so the atmosphere
    // has a sun to scatter.
    AtmosphereSunLight->LightingChannels.bChannel0 = false;
    AtmosphereSunLight->LightingChannels.bChannel1 = false;
    AtmosphereSunLight->LightingChannels.bChannel2 = false;

    // The sky. Configured from the descriptor in Initialise, since its radius
    // and height are properties of the individual planet, and disabled outright
    // on a body with no air.
    SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
    SkyAtmosphere->SetupAttachment(RootScene);
    SkyAtmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
    SkyAtmosphere->SetVisibility(false);

    Clouds = CreateDefaultSubobject<UVolumetricCloudComponent>(TEXT("Clouds"));
    Clouds->SetupAttachment(RootScene);
    Clouds->SetVisibility(false);

    SkyLight = CreateDefaultSubobject<USkyLightComponent>(TEXT("SkyLight"));
    SkyLight->SetupAttachment(RootScene);
    SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
    SkyLight->bRealTimeCapture = true;
    SkyLight->SetIntensity(1.0f);

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
    const FPlanetDescriptor& InAstronomy,
    const FPlanetTerrainSettings& InSettings,
    const FUniversePosition& InStarPosition,
    double InStarLuminositySolar)
{
    PlanetDescriptor = InPlanet;
    TerrainSettings = InSettings;

    // Resolve the environment here rather than taking it as an argument.
    //
    // It is derived entirely from the two descriptors already being passed, so
    // asking a caller to supply it would create a second place where a planet's
    // climate could be decided - and eventually two planets, one for the
    // terrain and one for the spawner.
    EnvironmentDescriptor = FPlanetEnvironment::Resolve(InPlanet, InSettings, InAstronomy);

    UE_LOG(LogPlanetActor, Log, TEXT("%s"), *EnvironmentDescriptor.ToDebugString());
    FrameBounds = FPlanetFrameBounds::FromPlanet(InPlanet);

    // Hand the logical atmosphere straight to the renderer, in kilometres.
    //
    // Same numbers, one source. The altitude at which drag begins and the
    // altitude at which the sky starts to thin are the same field of the same
    // descriptor, so they cannot drift apart. Unreal clamps the ground radius
    // to 10,000 km, which every body generated so far is comfortably inside;
    // a larger one would render without a sky rather than render wrongly, and
    // the clamp is noted here so that is a known limit rather than a mystery.
    if (SkyAtmosphere != nullptr)
    {
        const bool bVisible =
            InPlanet.HasAtmosphere() && InPlanet.RadiusMeters <= 10000000.0;

        if (bVisible)
        {
            // The Set* accessors rather than the fields: they mark the render
            // state dirty, and a value assigned directly is not picked up until
            // something else happens to invalidate it.
            //
            // The bottom radius is the planet's *sea level*, which on a world
            // whose ocean sits above the terrain reference radius is not the
            // same number. Using the reference radius put the ground above the
            // atmosphere's own floor, and the transmittance lookup for a point
            // outside the shell it is defined on returns almost nothing - which
            // rendered a sunlit forest at noon as pure black while the sky above
            // it stayed correctly blue.
            const double SeaLevelMeters = EnvironmentDescriptor.HasOcean()
                ? EnvironmentDescriptor.OceanRadiusMeters
                : InPlanet.RadiusMeters;

            // A little below sea level, so the deepest ground is still inside.
            SkyAtmosphere->SetBottomRadius(
                static_cast<float>((SeaLevelMeters - InPlanet.MaxDepthMeters) / 1000.0));
            SkyAtmosphere->SetAtmosphereHeight(
                static_cast<float>(InPlanet.AtmosphereHeightMeters / 1000.0));

            // Two, as the engine recommends when the high-quality multi-
            // scattering LUT is off, which it is by default.
            SkyAtmosphere->SetMultiScatteringFactor(2.0f);

            // Aerial perspective is scaled down hard.
            //
            // Unreal's aerial-perspective LUT covers a fixed depth range tuned
            // for an Earth-sized world seen from within a few tens of
            // kilometres. On a planet whose visible horizon is two hundred
            // kilometres away it saturates, and everything past a few hundred
            // metres is rendered as pure atmosphere. Cutting the scale keeps
            // the effect where it reads correctly - haze on distant hills -
            // without swallowing the ground the player is standing on.
            SkyAtmosphere->AerialPespectiveViewDistanceScale = 0.05f;
            SkyAtmosphere->MarkRenderStateDirty();
        }

        SkyAtmosphere->SetVisibility(bVisible);
    }

    // Clouds, on the same terms as the sky.
    //
    // The layer sits in the lower fifth of the atmosphere, which is where real
    // weather clouds live: Earth's atmosphere is a hundred kilometres deep and
    // its clouds are almost all below twelve. Deriving the altitude from the
    // planet's own atmosphere height rather than fixing it means a thick-aired
    // world gets a correspondingly deeper cloud deck.
    if (Clouds != nullptr)
    {
        const bool bVisible = InPlanet.HasAtmosphere() && EnvironmentDescriptor.CloudCoverageBias > 0.02;

        if (bVisible)
        {
            const double AtmosphereKm = InPlanet.AtmosphereHeightMeters / 1000.0;

            Clouds->LayerBottomAltitude = static_cast<float>(AtmosphereKm * 0.06);
            Clouds->LayerHeight = static_cast<float>(FMath::Max(AtmosphereKm * 0.14, 1.0));

            // Tracing distance is what the renderer will march through the
            // layer. Matched to the layer rather than left at the default,
            // which assumes an Earth-sized atmosphere.
            Clouds->TracingMaxDistance = static_cast<float>(FMath::Max(AtmosphereKm * 4.0, 20.0));

            Clouds->MarkRenderStateDirty();
        }

        Clouds->SetVisibility(bVisible);
    }

    // Where the planet is in its rotation at time zero.
    //
    // Seed-derived rather than zero, so two planets are not synchronised and a
    // fresh session does not always start at the same hour. Deterministic, so
    // it is the same hour every time for a given world.
    RotationPhaseAtEpoch =
        static_cast<double>(UniverseHash::Hash(EnvironmentDescriptor.Seed.Value, 0x524F5400u, 0)
            >> 11) * (1.0 / 9007199254740992.0) * 2.0 * PI;
    StarPosition = InStarPosition;
    StarLuminositySolar = InStarLuminositySolar;

    if (StarLight != nullptr)
    {
        // Illuminance from the inverse-square law, in real units, computed from
        // the real separation in universe coordinates. Nothing here touches an
        // Unreal transform, so the scaled-space mismatch cannot creep back in.
        const double DistanceAu = FUniversePosition::DistanceAu(PlanetDescriptor.Position, StarPosition);
        const double SafeDistanceAu = FMath::Max(DistanceAu, 1.0e-6);

        const double PhysicalLux =
            UniversePhysics::SolarIlluminanceAt1AuLux * StarLuminositySolar / (SafeDistanceAu * SafeDistanceAu);

        // Unclamped, now that exposure is set from the same number.
        //
        // Sprint 002 clamped this to 25,000 lux because auto-exposure could not
        // cope, and recorded that real photometric range was Sprint 003 work.
        // It is: ApplyExposureTo calibrates the camera to this exact value, so
        // the light can be what it physically is. The lower bound survives
        // because a body far enough out to receive under a lux is lit by
        // starlight rather than by its sun, which is not yet modelled.
        StarIlluminanceLux = FMath::Max(PhysicalLux, 1.0);

        const double Lux = StarIlluminanceLux;

        StarLight->SetIntensity(static_cast<float>(Lux));

        if (AtmosphereSunLight != nullptr)
        {
            AtmosphereSunLight->SetIntensity(static_cast<float>(Lux));
        }

        // Direction is set by UpdateStarLightDirection every tick, since it
        // depends on where the planet is in its rotation. Set once here too so
        // the first frame is lit correctly rather than pointing down the X axis.
        UpdateStarLightDirection();

        UE_LOG(LogPlanetActor, Log,
            TEXT("Star light: %.4f AU away, %.0f lux, EV100 %.2f"),
            DistanceAu, Lux, FMath::Log2(Lux / 2.5));
    }

    if (Anchor != nullptr)
    {
        Anchor->SetUniversePosition(PlanetDescriptor.Position);
    }

    if (TerrainComponent != nullptr)
    {
        TerrainComponent->SetPlanet(PlanetDescriptor, EnvironmentDescriptor, TerrainSettings);

        if (VegetationComponent != nullptr)
        {
            VegetationComponent->SetPlanet(PlanetDescriptor, EnvironmentDescriptor, TerrainSettings);
        }

        if (WildlifeComponent != nullptr)
        {
            WildlifeComponent->SetPlanet(PlanetDescriptor, EnvironmentDescriptor, TerrainSettings);
        }

        if (StructureComponent != nullptr)
        {
            StructureComponent->SetPlanet(PlanetDescriptor, TerrainSettings);
        }
    }

    UE_LOG(LogPlanetActor, Log, TEXT("Planet initialised: %s"), *PlanetDescriptor.ToDebugString());
}

void APlanetActor::BeginPlay()
{
    Super::BeginPlay();

    if (TerrainComponent != nullptr && PlanetDescriptor.IsValid())
    {
        TerrainComponent->SetPlanet(PlanetDescriptor, EnvironmentDescriptor, TerrainSettings);

        if (VegetationComponent != nullptr)
        {
            VegetationComponent->SetPlanet(PlanetDescriptor, EnvironmentDescriptor, TerrainSettings);
        }

        if (WildlifeComponent != nullptr)
        {
            WildlifeComponent->SetPlanet(PlanetDescriptor, EnvironmentDescriptor, TerrainSettings);
        }

        if (StructureComponent != nullptr)
        {
            StructureComponent->SetPlanet(PlanetDescriptor, TerrainSettings);
        }
    }

    // Offer the body to the frame selector. Until this happens the planet is
    // scenery: it renders, but it cannot claim the player, so nothing standing
    // on it has gravity or an up direction.
    if (UWorld* World = GetWorld())
    {
        if (UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
        {
            Subsystem->RegisterPlanet(this);
        }
    }
}

void APlanetActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorld* World = GetWorld())
    {
        if (UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
        {
            Subsystem->UnregisterPlanet(this);
        }
    }

    Super::EndPlay(EndPlayReason);
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

FUniversePosition APlanetActor::PlanetLocalMetersToUniverse(const FVector3d& PlanetLocalMeters) const
{
    return PlanetDescriptor.Position.OffsetByCm(FVector3d(
        PlanetLocalMeters.X * UniverseScale::CmPerMeter,
        PlanetLocalMeters.Y * UniverseScale::CmPerMeter,
        PlanetLocalMeters.Z * UniverseScale::CmPerMeter));
}

FPlanetFrameCandidate APlanetActor::MakeFrameCandidate(const FUniversePosition& UniversePosition) const
{
    FPlanetFrameCandidate Candidate;
    Candidate.PlanetKey = PlanetDescriptor.PlanetKey;
    Candidate.Bounds = FrameBounds;
    Candidate.DistanceFromCentreMeters = GetDistanceFromCentreMeters(UniversePosition);
    return Candidate;
}

FVector3d APlanetActor::GetGravityAccelerationMs2(const FUniversePosition& UniversePosition) const
{
    return GetGravityField().GetAccelerationMs2(UniverseToPlanetLocalMeters(UniversePosition));
}

FVector3d APlanetActor::GetLocalUp(const FUniversePosition& UniversePosition) const
{
    return FPlanetGravityField::GetLocalUp(UniverseToPlanetLocalMeters(UniversePosition));
}

FPlanetSurfaceSample APlanetActor::SampleSurfaceBelow(const FUniversePosition& UniversePosition) const
{
    return FPlanetSurfaceQuery::SampleBelow(
        PlanetDescriptor, TerrainSettings, UniverseToPlanetLocalMeters(UniversePosition));
}

namespace
{
    const UUniverseWorldSubsystem* FindUniverseSubsystem(const AActor* Actor)
    {
        const UWorld* World = (Actor != nullptr) ? Actor->GetWorld() : nullptr;
        return (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;
    }
}

double APlanetActor::GetAltitudeAboveTerrainMeters(const FVector& RenderLocation) const
{
    // Takes a render location rather than a universe position so it can be
    // exposed to Blueprint, which has no notion of an FUniversePosition. The
    // conversion goes back through the subsystem, so the answer is identical
    // to the universe-space path - this is a different door into the same
    // room, not a second implementation.
    const UUniverseWorldSubsystem* Subsystem = FindUniverseSubsystem(this);

    if (Subsystem == nullptr)
    {
        return 0.0;
    }

    const FUniversePosition Position = Subsystem->RenderLocationToUniverse(RenderLocation);

    return FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
        PlanetDescriptor, TerrainSettings, UniverseToPlanetLocalMeters(Position));
}

double APlanetActor::GetAltitudeAboveSeaLevelMeters(const FUniversePosition& UniversePosition) const
{
    return FPlanetSurfaceQuery::GetAltitudeAboveSeaLevelMeters(
        PlanetDescriptor, UniverseToPlanetLocalMeters(UniversePosition));
}

double APlanetActor::GetDistanceFromCentreMeters(const FUniversePosition& UniversePosition) const
{
    return UniverseToPlanetLocalMeters(UniversePosition).Size();
}

double APlanetActor::GetAtmosphericDepthFraction(const FUniversePosition& UniversePosition) const
{
    return FPlanetSurfaceQuery::GetAtmosphericDepthFraction(
        PlanetDescriptor, UniverseToPlanetLocalMeters(UniversePosition));
}

FUniversePosition APlanetActor::GetUniversePositionAboveTerrain(
    const FVector3d& Direction,
    double HeightAboveTerrainMeters) const
{
    const FVector3d Local = FPlanetSurfaceQuery::GetPositionAboveTerrain(
        PlanetDescriptor, TerrainSettings, Direction, HeightAboveTerrainMeters);

    return PlanetLocalMetersToUniverse(Local);
}

void APlanetActor::ApplyExposureTo(UCameraComponent* Camera) const
{
    if (Camera == nullptr)
    {
        return;
    }

    // EV100 from incident illuminance, with the standard incident-light meter
    // constant of 250:  EV100 = log2(E * ISO / C) = log2(E / 2.5) at ISO 100.
    const double EV100 = FMath::Log2(FMath::Max(StarIlluminanceLux, 1.0) / 2.5);

    // Shutter carries the whole adjustment; aperture and sensitivity are left
    // alone.
    //
    // Aperture is deliberately *not* overridden, and that is not a style
    // choice. Unreal takes the exposure aperture from DepthOfFieldFstop - the
    // same field that drives the physical depth-of-field model - so overriding
    // it switches that model on. With no focal distance set, everything
    // defocuses, and a bright point in the scene turns into a large disc fixed
    // near the centre of the screen. That disc looks exactly like a rendering
    // bug in a distant object, and cost a while to identify as an out-of-focus
    // highlight rather than a planet drawn in the wrong place.
    //
    // So the engine's default f/4 stands, and the shutter absorbs everything.
    // That is also the right division physically: ISO would add grain and
    // aperture would change depth of field, and neither should shift as a
    // player flies from a bright inner planet to a dim outer one.
    constexpr double Aperture = 4.0;
    const double ShutterDenominator = FMath::Pow(2.0, EV100) / (Aperture * Aperture);

    FPostProcessSettings& Settings = Camera->PostProcessSettings;

    Settings.bOverride_AutoExposureMethod = true;
    Settings.AutoExposureMethod = AEM_Manual;

    Settings.bOverride_CameraISO = true;
    Settings.CameraISO = 100.0f;

    // A very fast shutter is what a scene lit at a million lux actually calls
    // for. The upper bound is the engine's, not a physical one.
    Settings.bOverride_CameraShutterSpeed = true;
    Settings.CameraShutterSpeed = static_cast<float>(FMath::Clamp(ShutterDenominator, 1.0, 32000.0));

    // And no depth of field. A defocused planet is not a look this project
    // wants at any distance, and leaving it unset is not the same as setting
    // it off - other post-process sources could still enable it.
    Settings.bOverride_DepthOfFieldFocalDistance = true;
    Settings.DepthOfFieldFocalDistance = 0.0f;

    // A little under a neutral exposure. Metering for the incident light puts
    // mid-grey at mid-grey, which for a scene whose sky is a large bright area
    // reads slightly hot; a third of a stop down keeps the horizon from
    // clipping without darkening the ground perceptibly.
    Settings.bOverride_AutoExposureBias = true;
    Settings.AutoExposureBias = -0.33f;
}

FVector3d APlanetActor::GetSubstellarDirection() const
{
    const FVector3d ToStar = GetStarDirection();

    const FVector Axis(
        EnvironmentDescriptor.RotationAxis.X,
        EnvironmentDescriptor.RotationAxis.Y,
        EnvironmentDescriptor.RotationAxis.Z);

    // The surface turns by -angle relative to the star, so the point that
    // currently faces the star is the star direction turned by +angle. Getting
    // this sign backwards puts local noon exactly where local midnight is,
    // which looks like a working day/night cycle until someone checks a clock.
    const FQuat Spin(Axis, GetRotationAngleRadians());

    const FVector Rotated =
        Spin.RotateVector(FVector(ToStar.X, ToStar.Y, ToStar.Z)).GetSafeNormal();

    return FVector3d(Rotated.X, Rotated.Y, Rotated.Z);
}

double APlanetActor::GetSolarElevationDegrees(const FUniversePosition& UniversePosition) const
{
    const FVector3d Local = UniverseToPlanetLocalMeters(UniversePosition);

    FVector3d Up;

    if (!FPlanetSurfaceQuery::TryGetDirection(Local, Up))
    {
        return 0.0;
    }

    // The sun direction *as seen from the rotating surface*.
    //
    // The star does not move - it is astronomically far away and effectively
    // fixed - so a day happens because the planet turns underneath it. Rotating
    // the surface point backwards about the axis is equivalent to rotating the
    // star forwards around the planet, and is the cheaper of the two because
    // there is one observer and a whole planet.
    const FVector3d ToStar = GetStarDirection();

    const FQuat Spin(
        FVector(EnvironmentDescriptor.RotationAxis.X,
                EnvironmentDescriptor.RotationAxis.Y,
                EnvironmentDescriptor.RotationAxis.Z),
        -GetRotationAngleRadians());

    const FVector Rotated = Spin.RotateVector(FVector(Up.X, Up.Y, Up.Z));

    const double Elevation = FVector::DotProduct(
        Rotated.GetSafeNormal(), FVector(ToStar.X, ToStar.Y, ToStar.Z));

    return FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Elevation, -1.0, 1.0)));
}

double APlanetActor::GetTimeOfDayFraction(const FUniversePosition& UniversePosition) const
{
    const FVector3d Local = UniverseToPlanetLocalMeters(UniversePosition);

    FVector3d Up;

    if (!FPlanetSurfaceQuery::TryGetDirection(Local, Up))
    {
        return 0.0;
    }

    const FVector3d ToStar = GetStarDirection();
    const FVector3d Axis = EnvironmentDescriptor.RotationAxis;

    // Project both the local up and the sun direction onto the equatorial
    // plane and measure the angle between them. That angle *is* the hour: it
    // runs a full turn per rotation and is zero at local noon, regardless of
    // latitude - which a naive elevation-based clock is not, since the sun
    // never rises at all inside a polar night.
    const FVector3d East = FVector3d::CrossProduct(Axis, ToStar).GetSafeNormal();
    const FVector3d Noon = FVector3d::CrossProduct(East, Axis).GetSafeNormal();

    if (East.IsZero() || Noon.IsZero())
    {
        return 0.0;
    }

    const FQuat Spin(FVector(Axis.X, Axis.Y, Axis.Z), -GetRotationAngleRadians());
    const FVector Rotated = Spin.RotateVector(FVector(Up.X, Up.Y, Up.Z));

    const FVector3d Local3d(Rotated.X, Rotated.Y, Rotated.Z);

    const double NoonComponent = FVector3d::DotProduct(Local3d, Noon);
    const double EastComponent = FVector3d::DotProduct(Local3d, East);

    const double Angle = FMath::Atan2(EastComponent, NoonComponent);

    // Atan2 gives [-pi, pi] with zero at noon; shift so the day runs
    // midnight to midnight and 0.5 is noon.
    return FMath::Frac((Angle / (2.0 * PI)) + 1.5);
}

double APlanetActor::GetRotationAngleRadians() const
{
    const double Period = EnvironmentDescriptor.RotationPeriodSeconds;

    if (FMath::IsNearlyZero(Period))
    {
        return RotationPhaseAtEpoch;
    }

    return RotationPhaseAtEpoch + (SimulationTimeSeconds / Period) * 2.0 * PI;
}

FVector3d APlanetActor::GetStarDirection() const
{
    FVector3d RelativeCm;

    if (!FUniversePosition::TryGetRelativeCm(PlanetDescriptor.Position, StarPosition, RelativeCm))
    {
        return FVector3d(0.0, 0.0, 1.0);
    }

    const double Length = RelativeCm.Size();

    if (Length <= 0.0)
    {
        return FVector3d(0.0, 0.0, 1.0);
    }

    return FVector3d(RelativeCm.X / Length, RelativeCm.Y / Length, RelativeCm.Z / Length);
}

FVector3d APlanetActor::GetSpawnDirection(int32 Index) const
{
    // Two independent hash streams off the planet seed, mapped to a uniform
    // point on the sphere. Uniform rather than a lat/long grid because a grid
    // clusters spawn points at the poles, and because face centres and edges
    // are exactly where cube-sphere problems hide - a spawner that only ever
    // used them would never find one.
    const FUniverseSeed Stream = PlanetDescriptor.Seed.Stream(UniverseSeedDomain::StreamSurface);

    const uint64 HashU = UniverseHash::Hash(Stream.Value, 0x53504157u, Index);
    const uint64 HashV = UniverseHash::Hash(Stream.Value, 0x53504158u, Index);

    constexpr double InverseRange = 1.0 / 18446744073709551616.0;

    const double U = static_cast<double>(HashU) * InverseRange;
    const double V = static_cast<double>(HashV) * InverseRange;

    const double Z = 2.0 * U - 1.0;
    const double Radial = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
    const double Theta = 2.0 * PI * V;

    return FVector3d(Radial * FMath::Cos(Theta), Radial * FMath::Sin(Theta), Z);
}

void APlanetActor::UpdateStarLightDirection()
{
    if (StarLight == nullptr)
    {
        return;
    }

    // The planet turns; the star does not.
    //
    // Sprint 003 pointed the light along the fixed star-to-planet direction,
    // which is correct for a planet that never rotates and gives permanent noon
    // at the sub-stellar point. Rotating the light around the planet's axis
    // instead produces a day - and does it without moving a single vertex of
    // terrain, which is the reason it is done this way round. Rotating the
    // planet mesh would mean re-streaming every patch and re-deriving every
    // universe position on it, several times a minute.
    //
    // The visible consequence is that the *stars* do not wheel overhead, only
    // the sun does. That is wrong, and it is recorded as such: it needs the
    // scaled-space bodies to share the rotation, which needs them to be visible
    // from a surface, which is the far-field render pass Sprint 003 deferred.
    const FVector3d ToPlanetUniverse =
        FUniversePosition::DirectionUnit(StarPosition, PlanetDescriptor.Position);

    if (ToPlanetUniverse.IsZero())
    {
        return;
    }

    const FVector Axis(
        EnvironmentDescriptor.RotationAxis.X,
        EnvironmentDescriptor.RotationAxis.Y,
        EnvironmentDescriptor.RotationAxis.Z);

    const FQuat Spin(Axis, GetRotationAngleRadians());

    const FVector Direction =
        Spin.RotateVector(FVector(ToPlanetUniverse.X, ToPlanetUniverse.Y, ToPlanetUniverse.Z));

    StarLight->SetWorldRotation(Direction.Rotation());

    if (AtmosphereSunLight != nullptr)
    {
        AtmosphereSunLight->SetWorldRotation(Direction.Rotation());
    }
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

    // The observer is whichever anchor the subsystem is tracking, not
    // specifically the probe.
    //
    // Sprint 002 asked the player pawn directly, which was fine while there was
    // exactly one kind of pawn. It stops being fine the moment the player can
    // leave the ship and walk: the terrain would keep streaming around whatever
    // the probe was, and a character who stepped off it would be standing on
    // ground that nobody was asking for. The tracked anchor is the subsystem's
    // own answer to "where is the player", it is what the render origin already
    // follows, and it moves with possession.
    if (const UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        if (const UUniverseAnchorComponent* Tracked = Subsystem->GetTrackedAnchor())
        {
            ObserverUniverse = Tracked->GetUniversePosition();
            bHaveObserver = true;
        }
        else
        {
            ObserverUniverse = Subsystem->GetRenderOrigin();
            bHaveObserver = true;
        }
    }

    if (!bHaveObserver)
    {
        return;
    }

    // Planet time advances here rather than from the world clock, so that
    // acceleration affects the sky and the weather without touching physics.
    SimulationTimeSeconds +=
        static_cast<double>(DeltaSeconds) * FMath::Max(CVarPlanetTimeScale.GetValueOnGameThread(), 0.0f);

    UpdateStarLightDirection();

    const FVector3d ObserverLocal = UniverseToPlanetLocalMeters(ObserverUniverse);
    TerrainComponent->SetObserverPositionMeters(ObserverLocal);

    if (VegetationComponent != nullptr)
    {
        VegetationComponent->SetObserverPositionMeters(ObserverLocal);
    }

    if (WildlifeComponent != nullptr)
    {
        WildlifeComponent->SetObserverPositionMeters(ObserverLocal);
    }

    // Prewarm where the observer is heading, if they are heading anywhere fast.
    //
    // The predicted point is the analytic intersection of the current velocity
    // with the body, looked ahead by a fixed time rather than a fixed distance:
    // what matters is how long the streamer has to prepare, and seconds are the
    // unit that is in. Slow movement produces no prediction at all, because
    // there the observer's own position is already the right answer and a
    // second selection pass would be pure cost.
    FVector3d PrewarmLocal = FVector3d::ZeroVector;

    if (const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
    {
        const FVector3d Velocity = Probe->GetUniverseVelocity();
        const double Speed = Velocity.Size();

        constexpr double PrewarmLookAheadSeconds = 8.0;
        constexpr double MinimumPrewarmSpeedMs = 200.0;

        if (Speed > MinimumPrewarmSpeedMs)
        {
            const FVector3d Ahead(
                ObserverLocal.X + Velocity.X * PrewarmLookAheadSeconds,
                ObserverLocal.Y + Velocity.Y * PrewarmLookAheadSeconds,
                ObserverLocal.Z + Velocity.Z * PrewarmLookAheadSeconds);

            const FPlanetSweepResult Sweep = FPlanetTrajectory::SweepAgainstPlanetBounds(
                PlanetDescriptor, ObserverLocal, Ahead);

            // Only prewarm somewhere the observer will actually arrive. A
            // trajectory that misses the planet has no arrival point, and
            // refining around the closest approach of a flyby would spend the
            // budget on ground nobody is going to stand on.
            if (Sweep.bHit && !Sweep.bStartedInside)
            {
                PrewarmLocal = Sweep.EntryPointMeters;
            }
        }
    }

    TerrainComponent->SetPrewarmPositionMeters(PrewarmLocal);

    // The sky is drawn only from inside the air.
    //
    // Unreal's aerial-perspective LUT has a bounded depth range - about a
    // hundred kilometres by default - and beyond it the scattering it applies
    // is an extrapolation rather than a computation. Viewed from orbit, several
    // hundred kilometres of that extrapolation sits between the camera and the
    // ground, and the planet disappears behind a flat blue haze. That is a
    // regression against the orbital view Sprint 002 had working, and a real
    // one: terrain that was legible from 400 km became invisible.
    //
    // So the component is switched off above the atmosphere. The honest cost is
    // that a planet has no visible blue limb from space, which is a thing worth
    // having and is recorded as future work - it needs either a much deeper
    // aerial-perspective range or a separate shell drawn for the limb. What it
    // buys is that neither view is broken, and the altitude at which the
    // treatment changes is the same atmosphere height everything else uses.
    if (SkyAtmosphere != nullptr && PlanetDescriptor.HasAtmosphere())
    {
        const bool bInsideAir =
            ObserverLocal.Size() <= PlanetDescriptor.GetAtmosphereTopRadiusMeters();

        if (bInsideAir != SkyAtmosphere->IsVisible())
        {
            SkyAtmosphere->SetVisibility(bInsideAir);
        }
    }

    LastObserverAltitudeMeters = ObserverLocal.Size() - PlanetDescriptor.RadiusMeters;
}
