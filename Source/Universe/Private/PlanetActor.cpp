// Copyright Universe Project. All Rights Reserved.

#include "PlanetActor.h"
#include "PlanetTerrainComponent.h"
#include "UniverseAnchorComponent.h"
#include "UniverseWorldSubsystem.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"
#include "UniverseHash.h"
#include "PlanetTrajectory.h"

#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Camera/CameraComponent.h"
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

    // This is the light the atmosphere scatters. Without the flag the sky
    // renders black regardless of where the star is.
    StarLight->bAtmosphereSunLight = true;
    StarLight->AtmosphereSunLightIndex = 0;

    // The sky. Configured from the descriptor in Initialise, since its radius
    // and height are properties of the individual planet, and disabled outright
    // on a body with no air.
    SkyAtmosphere = CreateDefaultSubobject<USkyAtmosphereComponent>(TEXT("SkyAtmosphere"));
    SkyAtmosphere->SetupAttachment(RootScene);
    SkyAtmosphere->TransformMode = ESkyAtmosphereTransformMode::PlanetCenterAtComponentTransform;
    SkyAtmosphere->SetVisibility(false);

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
            SkyAtmosphere->SetBottomRadius(static_cast<float>(InPlanet.RadiusMeters / 1000.0));
            SkyAtmosphere->SetAtmosphereHeight(
                static_cast<float>(InPlanet.AtmosphereHeightMeters / 1000.0));

            // Two, as the engine recommends when the high-quality multi-
            // scattering LUT is off, which it is by default.
            SkyAtmosphere->SetMultiScatteringFactor(2.0f);
        }

        SkyAtmosphere->SetVisibility(bVisible);
    }
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

        // Point the light along the star-to-planet direction.
        const FVector3d ToPlanet =
            FUniversePosition::DirectionUnit(StarPosition, PlanetDescriptor.Position);
        if (!ToPlanet.IsZero())
        {
            StarLight->SetWorldRotation(FVector(ToPlanet.X, ToPlanet.Y, ToPlanet.Z).Rotation());
        }

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

    const FVector3d ObserverLocal = UniverseToPlanetLocalMeters(ObserverUniverse);
    TerrainComponent->SetObserverPositionMeters(ObserverLocal);

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
