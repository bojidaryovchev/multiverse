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
    FrameBounds = FPlanetFrameBounds::FromPlanet(InPlanet);
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

    LastObserverAltitudeMeters = ObserverLocal.Size() - PlanetDescriptor.RadiusMeters;
}
