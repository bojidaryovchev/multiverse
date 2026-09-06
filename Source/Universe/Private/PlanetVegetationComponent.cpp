// Copyright Universe Project. All Rights Reserved.

#include "PlanetVegetationComponent.h"
#include "PlanetSurfaceQuery.h"
#include "UniverseScale.h"
#include "CubeSphere.h"
#include "PlanetQuadtree.h"
#include "PlanetTerrain.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"
#include "Async/Async.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanetVegetation, Log, All);

namespace
{
    static TAutoConsoleVariable<int32> CVarVegetationEnabled(
        TEXT("universe.Vegetation"),
        1,
        TEXT("Enable procedural vegetation streaming."),
        ECVF_Cheat);

    static TAutoConsoleVariable<float> CVarVegetationLogInterval(
        TEXT("universe.VegetationLogInterval"),
        0.0f,
        TEXT("Log vegetation streaming state every N seconds. 0 = off."),
        ECVF_Cheat);

    /**
     * Placeholder geometry, from the engine's basic shapes.
     *
     * Sprint 004 is explicit that content is not the point: what has to be
     * proven is that the *pipeline* runs - that a biome decides an archetype,
     * an archetype resolves to geometry, and the geometry streams and is
     * bounded. A cone standing in for a conifer proves all of that, and takes
     * no art time away from the architecture it is meant to validate.
     *
     * The mapping lives here rather than in UniversePlanet deliberately. That
     * module knows about "coniferous canopy tree" and must never know about a
     * mesh path, because it also runs in a headless server that has no meshes
     * at all.
     */
    const TCHAR* GetArchetypeMeshPath(EVegetationArchetype Archetype)
    {
        switch (Archetype)
        {
        case EVegetationArchetype::ConiferTree:
        case EVegetationArchetype::MushroomCanopy:
            return TEXT("/Engine/BasicShapes/Cone.Cone");

        case EVegetationArchetype::BroadleafTree:
        case EVegetationArchetype::PalmTree:
        case EVegetationArchetype::Shrub:
        case EVegetationArchetype::Fern:
        case EVegetationArchetype::MushroomCluster:
        case EVegetationArchetype::BioluminescentPlant:
        case EVegetationArchetype::Rock:
        case EVegetationArchetype::Boulder:
            return TEXT("/Engine/BasicShapes/Sphere.Sphere");

        case EVegetationArchetype::DeadTree:
        case EVegetationArchetype::Cactus:
        case EVegetationArchetype::SporeStalk:
            return TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

        case EVegetationArchetype::Grass:
        case EVegetationArchetype::Flower:
            return TEXT("/Engine/BasicShapes/Cone.Cone");

        default:
            return nullptr;
        }
    }
}

UPlanetVegetationComponent::UPlanetVegetationComponent()
{
    PrimaryComponentTick.bCanEverTick = true;

    // Resolve the placeholder meshes once. ConstructorHelpers only works during
    // construction, which is why this is here rather than in BeginPlay.
    struct FMeshLoad
    {
        const TCHAR* Path;
    };

    static const TCHAR* Paths[] = {
        TEXT("/Engine/BasicShapes/Cone.Cone"),
        TEXT("/Engine/BasicShapes/Sphere.Sphere"),
        TEXT("/Engine/BasicShapes/Cylinder.Cylinder"),
    };

    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(Paths[0]);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMesh(Paths[1]);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderMesh(Paths[2]);

    for (int32 Index = 0; Index < static_cast<int32>(EVegetationArchetype::Count); ++Index)
    {
        const EVegetationArchetype Archetype = static_cast<EVegetationArchetype>(Index);
        const TCHAR* Path = GetArchetypeMeshPath(Archetype);

        if (Path == nullptr)
        {
            continue;
        }

        UStaticMesh* Mesh = nullptr;

        if (FCString::Strcmp(Path, Paths[0]) == 0 && ConeMesh.Succeeded())
        {
            Mesh = ConeMesh.Object;
        }
        else if (FCString::Strcmp(Path, Paths[1]) == 0 && SphereMesh.Succeeded())
        {
            Mesh = SphereMesh.Object;
        }
        else if (FCString::Strcmp(Path, Paths[2]) == 0 && CylinderMesh.Succeeded())
        {
            Mesh = CylinderMesh.Object;
        }

        if (Mesh != nullptr)
        {
            ArchetypeMeshes.Add(static_cast<uint8>(Index), Mesh);
        }
    }

    // A tintable material, not the terrain's vertex-colour one.
    //
    // Instanced meshes share their vertex data by definition, so there are no
    // per-instance vertex colours to read - every tree would be whatever colour
    // the source mesh was authored with, which for the engine basic shapes is
    // white. A material instance per archetype with an explicit colour is the
    // cheap way to get a forest that is green and rock that is grey.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> BasicShape(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));

    if (BasicShape.Succeeded())
    {
        VegetationMaterial = BasicShape.Object;
    }
}

double UPlanetVegetationComponent::GetArchetypeSizeMeters(EVegetationArchetype Archetype)
{
    // Real-ish heights. The point of using real numbers is that the *relative*
    // scale is right straight away - a shrub reads as a shrub next to a tree -
    // and that a density in stems per hectare produces a forest that looks like
    // a forest rather than needing both numbers tuned against each other.
    switch (Archetype)
    {
    case EVegetationArchetype::BroadleafTree:       return 18.0;
    case EVegetationArchetype::ConiferTree:         return 24.0;
    case EVegetationArchetype::PalmTree:            return 14.0;
    case EVegetationArchetype::DeadTree:            return 12.0;
    case EVegetationArchetype::MushroomCanopy:      return 20.0;
    case EVegetationArchetype::Shrub:               return 2.0;
    case EVegetationArchetype::Cactus:              return 3.0;
    case EVegetationArchetype::Fern:                return 1.2;
    case EVegetationArchetype::MushroomCluster:     return 1.6;
    case EVegetationArchetype::SporeStalk:          return 1.0;
    case EVegetationArchetype::BioluminescentPlant: return 1.4;
    case EVegetationArchetype::Grass:               return 0.5;
    case EVegetationArchetype::Flower:              return 0.4;
    case EVegetationArchetype::Rock:                return 1.0;
    case EVegetationArchetype::Boulder:             return 3.5;
    default:                                        return 1.0;
    }
}

FLinearColor UPlanetVegetationComponent::GetArchetypeColour(EVegetationArchetype Archetype)
{
    // Rough, and deliberately so. These stand in until real assets exist; what
    // matters now is that a forest is not the same colour as a rock, so a
    // screenshot shows whether the classifier put the right things in the right
    // places.
    switch (Archetype)
    {
    case EVegetationArchetype::BroadleafTree:       return FLinearColor(0.09f, 0.24f, 0.06f);
    case EVegetationArchetype::ConiferTree:         return FLinearColor(0.05f, 0.16f, 0.08f);
    case EVegetationArchetype::PalmTree:            return FLinearColor(0.14f, 0.30f, 0.08f);
    case EVegetationArchetype::DeadTree:            return FLinearColor(0.20f, 0.16f, 0.11f);
    case EVegetationArchetype::Shrub:               return FLinearColor(0.13f, 0.22f, 0.07f);
    case EVegetationArchetype::Cactus:              return FLinearColor(0.15f, 0.28f, 0.14f);
    case EVegetationArchetype::Fern:                return FLinearColor(0.10f, 0.26f, 0.10f);
    case EVegetationArchetype::Grass:               return FLinearColor(0.18f, 0.30f, 0.08f);
    case EVegetationArchetype::Flower:              return FLinearColor(0.55f, 0.45f, 0.20f);
    case EVegetationArchetype::Rock:                return FLinearColor(0.24f, 0.22f, 0.20f);
    case EVegetationArchetype::Boulder:             return FLinearColor(0.20f, 0.19f, 0.18f);
    case EVegetationArchetype::MushroomCanopy:      return FLinearColor(0.32f, 0.12f, 0.36f);
    case EVegetationArchetype::MushroomCluster:     return FLinearColor(0.40f, 0.18f, 0.30f);
    case EVegetationArchetype::SporeStalk:          return FLinearColor(0.26f, 0.20f, 0.34f);
    case EVegetationArchetype::BioluminescentPlant: return FLinearColor(0.10f, 0.45f, 0.48f);
    default:                                        return FLinearColor(0.4f, 0.4f, 0.4f);
    }
}

double UPlanetVegetationComponent::GetArchetypeWidthRatio(EVegetationArchetype Archetype)
{
    // Width as a fraction of height.
    //
    // Uniform scaling makes a "tree" an eighteen-metre sphere, which touches
    // its neighbours at any realistic stem density and turns a forest into a
    // solid mass. Real crowns are a third to a half as wide as the tree is
    // tall, and applying that is the difference between a forest and a bank of
    // foam.
    switch (Archetype)
    {
    case EVegetationArchetype::BroadleafTree:   return 0.45;
    case EVegetationArchetype::ConiferTree:     return 0.30;
    case EVegetationArchetype::PalmTree:        return 0.35;
    case EVegetationArchetype::DeadTree:        return 0.10;
    case EVegetationArchetype::MushroomCanopy:  return 0.55;
    case EVegetationArchetype::SporeStalk:      return 0.15;
    case EVegetationArchetype::Cactus:          return 0.25;
    case EVegetationArchetype::Grass:
    case EVegetationArchetype::Flower:          return 0.6;
    case EVegetationArchetype::Rock:
    case EVegetationArchetype::Boulder:         return 1.0;
    default:                                    return 0.8;
    }
}

bool UPlanetVegetationComponent::ArchetypeHasCollision(EVegetationArchetype Archetype)
{
    // Only things a player can walk into. Collision on grass would be tens of
    // thousands of primitives for no gameplay benefit at all, and is the single
    // easiest way to make a forest unplayable.
    switch (Archetype)
    {
    case EVegetationArchetype::BroadleafTree:
    case EVegetationArchetype::ConiferTree:
    case EVegetationArchetype::PalmTree:
    case EVegetationArchetype::DeadTree:
    case EVegetationArchetype::MushroomCanopy:
    case EVegetationArchetype::Boulder:
        return true;

    default:
        return false;
    }
}

UStaticMesh* UPlanetVegetationComponent::GetArchetypeMesh(EVegetationArchetype Archetype) const
{
    const TObjectPtr<UStaticMesh>* Found = ArchetypeMeshes.Find(static_cast<uint8>(Archetype));
    return (Found != nullptr) ? Found->Get() : nullptr;
}

float UPlanetVegetationComponent::GetLayerRadiusMeters(EVegetationLayer Layer) const
{
    switch (Layer)
    {
    case EVegetationLayer::Canopy:     return CanopyRadiusMeters;
    case EVegetationLayer::Understory: return UnderstoryRadiusMeters;
    case EVegetationLayer::Scatter:    return ScatterRadiusMeters;
    case EVegetationLayer::Ground:     return GroundRadiusMeters;
    default:                           return 0.0f;
    }
}

int32 UPlanetVegetationComponent::GetLayerInstanceCount(EVegetationLayer Layer) const
{
    const int32 Index = static_cast<int32>(Layer);

    return (Index >= 0 && Index < static_cast<int32>(EVegetationLayer::Count))
        ? LayerInstances[Index]
        : 0;
}

void UPlanetVegetationComponent::BeginPlay()
{
    Super::BeginPlay();

    Shared = MakeShared<FVegetationShared, ESPMode::ThreadSafe>();
}

void UPlanetVegetationComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    // Tell any in-flight job that nobody is listening. It will drop its result
    // rather than queue work into a component that is going away.
    if (Shared.IsValid())
    {
        Shared->bShutdown = true;
    }

    ReleaseAll();

    Super::EndPlay(EndPlayReason);
}

void UPlanetVegetationComponent::SetPlanet(
    const FPlanetSurfaceDescriptor& InPlanet,
    const FPlanetEnvironmentDescriptor& InEnvironment,
    const FPlanetTerrainSettings& InSettings)
{
    Planet = InPlanet;
    Environment = InEnvironment;
    TerrainSettings = InSettings;

    ReleaseAll();
}

void UPlanetVegetationComponent::SetObserverPositionMeters(const FVector3d& InObserverMeters)
{
    ObserverMeters = InObserverMeters;
    bHasObserver = true;
}

void UPlanetVegetationComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!Shared.IsValid() || !bHasObserver || !Planet.IsValid())
    {
        return;
    }

    // Results are drained even when vegetation is switched off, so that jobs
    // already in flight are accounted for rather than leaking their in-flight
    // count forever.
    DrainResults();

    const bool bEnabled = CVarVegetationEnabled.GetValueOnGameThread() != 0;

    // Relevance, coarsest first.
    //
    // Altitude is checked before anything else because it rejects the whole
    // system in one comparison during the case that matters most - a craft
    // crossing a continent at speed, where every job started would be released
    // before it was ever drawn.
    // Height above the *ground*, not above the ocean datum.
    //
    // The distinction is not academic. Measured against the ocean, a player
    // standing on a four-thousand-metre plateau reads as flying at four
    // thousand metres, and the vegetation switches itself off underneath their
    // feet. On a world whose terrain reaches nearly six kilometres that is not
    // an edge case - it is every mountain forest on the planet.
    //
    // One terrain evaluation per tick, which is nothing next to being wrong.
    const double AltitudeMeters = FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
        Planet, TerrainSettings, ObserverMeters);

    const bool bLowEnough = AltitudeMeters < static_cast<double>(MaxObserverAltitudeMeters);

    if (!bEnabled || !bLowEnough || !Environment.IsValid())
    {
        if (Patches.Num() > 0)
        {
            ReleaseAll();
        }
        return;
    }

    TimeSinceSelection += DeltaTime;

    if (TimeSinceSelection >= SelectionIntervalSeconds)
    {
        TimeSinceSelection = 0.0f;
        RunSelection();
    }

    PumpGeneration();

    const float LogInterval = CVarVegetationLogInterval.GetValueOnGameThread();

    if (LogInterval > 0.0f)
    {
        static double TimeSinceLog = 0.0;
        TimeSinceLog += DeltaTime;

        if (TimeSinceLog >= LogInterval)
        {
            TimeSinceLog = 0.0;

            UE_LOG(LogPlanetVegetation, Log,
                TEXT("VEGETATION alt=%.0fm patches=%d instances=%d (canopy %d under %d ground %d scatter %d) ")
                TEXT("jobs=%d built=%d released=%d"),
                AltitudeMeters, Patches.Num(), TotalInstances,
                GetLayerInstanceCount(EVegetationLayer::Canopy),
                GetLayerInstanceCount(EVegetationLayer::Understory),
                GetLayerInstanceCount(EVegetationLayer::Ground),
                GetLayerInstanceCount(EVegetationLayer::Scatter),
                InFlightJobs, TotalGenerated, TotalReleased);
        }
    }
}

void UPlanetVegetationComponent::RunSelection()
{
    FVector3d ObserverDirection;

    if (!FPlanetSurfaceQuery::TryGetDirection(ObserverMeters, ObserverDirection))
    {
        return;
    }

    const uint8 Level = static_cast<uint8>(
        FMath::Clamp(PatchLevel, 1, static_cast<int32>(FPlanetPatchId::MaxLevel)));

    const FPlanetPatchId Centre = FPlanetPatchId::FromDirection(ObserverDirection, Level);
    const double PatchSizeMeters = Centre.GetApproximateSizeMeters(Planet.RadiusMeters);

    if (PatchSizeMeters <= 0.0)
    {
        return;
    }

    TSet<uint64> Wanted;

    // Walk outward from the observer's own patch in a square ring, converting
    // each step to a direction rather than to patch indices.
    //
    // Index arithmetic would be wrong at every cube-face boundary, where a
    // neighbouring patch is on another face with a different orientation.
    // Stepping in *directions* and asking which patch each lands in is
    // topology-agnostic: it does the right thing across faces and corners
    // without a single special case.
    FVector3d TangentU;
    FVector3d TangentV;
    FPlanetTerrain::GetTangentBasis(ObserverDirection, TangentU, TangentV);

    // Angular size of one patch, used to step in direction space.
    const double PatchAngle = PatchSizeMeters / Planet.RadiusMeters;

    for (int32 LayerIndex = 0; LayerIndex < static_cast<int32>(EVegetationLayer::Count); ++LayerIndex)
    {
        const EVegetationLayer Layer = static_cast<EVegetationLayer>(LayerIndex);
        const double Radius = static_cast<double>(GetLayerRadiusMeters(Layer));

        if (Radius <= 0.0)
        {
            continue;
        }

        const int32 Reach = FMath::Clamp(
            FMath::CeilToInt32(Radius / PatchSizeMeters), 0, 8);

        for (int32 StepV = -Reach; StepV <= Reach; ++StepV)
        {
            for (int32 StepU = -Reach; StepU <= Reach; ++StepU)
            {
                const double OffsetU = StepU * PatchAngle;
                const double OffsetV = StepV * PatchAngle;

                const FVector3d Probe = FVector3d(
                    ObserverDirection.X + TangentU.X * OffsetU + TangentV.X * OffsetV,
                    ObserverDirection.Y + TangentU.Y * OffsetU + TangentV.Y * OffsetV,
                    ObserverDirection.Z + TangentU.Z * OffsetU + TangentV.Z * OffsetV)
                    .GetSafeNormal();

                if (Probe.IsZero())
                {
                    continue;
                }

                const FPlanetPatchId PatchId = FPlanetPatchId::FromDirection(Probe, Level);

                // Distance *along the surface*, not through space.
                //
                // Measuring the 3D distance from the observer to a point on the
                // reference sphere folds in the observer's own height above it,
                // and that height is the terrain elevation - which on this
                // planet runs to nearly six kilometres. A player standing on a
                // two-kilometre plateau would then measure every patch,
                // including the one under their feet, as two kilometres away,
                // and a fourteen-hundred-metre activation radius would select
                // nothing at all.
                //
                // The chord between the two unit directions, times the radius,
                // is the surface distance to well within a percent at these
                // angles - and needs no arccosine, keeping this off the
                // transcendental path like everything else on the planet.
                const FVector3d PatchCentre = PatchId.GetDirectionAt(0.5, 0.5);

                const double Chord = FVector3d(
                    PatchCentre.X - ObserverDirection.X,
                    PatchCentre.Y - ObserverDirection.Y,
                    PatchCentre.Z - ObserverDirection.Z).Size();

                const double DistanceMeters = Chord * Planet.RadiusMeters;

                if (DistanceMeters > Radius + PatchSizeMeters)
                {
                    continue;
                }

                const uint64 Key = MakePlanetPatchKey(PatchId) ^ (static_cast<uint64>(Layer) << 60);

                Wanted.Add(Key);

                if (FActivePatch* Existing = Patches.Find(Key))
                {
                    Existing->DistanceMeters = DistanceMeters;
                    continue;
                }

                if (TotalInstances >= MaxTotalInstances)
                {
                    // The budget is spent. Thinning the far edge of the active
                    // set is preferable to dropping frames, and is visible in
                    // the instance count rather than being silent.
                    continue;
                }

                FActivePatch NewPatch;
                NewPatch.PatchId = PatchId;
                NewPatch.Layer = Layer;
                NewPatch.DistanceMeters = DistanceMeters;

                Patches.Add(Key, MoveTemp(NewPatch));
            }
        }
    }

    // Release everything no longer wanted.
    TArray<uint64> ToRemove;

    for (TPair<uint64, FActivePatch>& Pair : Patches)
    {
        if (!Wanted.Contains(Pair.Key))
        {
            ReleasePatch(Pair.Value);
            ToRemove.Add(Pair.Key);
        }
    }

    for (uint64 Key : ToRemove)
    {
        Patches.Remove(Key);
    }
}

void UPlanetVegetationComponent::PumpGeneration()
{
    if (!Shared.IsValid())
    {
        return;
    }

    const int32 Budget = MaxConcurrentJobs - Shared->InFlight.GetValue();

    if (Budget <= 0)
    {
        return;
    }

    // Nearest first, and canopy before ground: a tree missing at fifty metres
    // is far more noticeable than a grass clump missing at thirty.
    TArray<TPair<double, uint64>> Candidates;

    for (const TPair<uint64, FActivePatch>& Pair : Patches)
    {
        if (Pair.Value.bGenerating || Pair.Value.bLive)
        {
            continue;
        }

        const double LayerBias = (Pair.Value.Layer == EVegetationLayer::Ground) ? 1.0e6 : 0.0;

        Candidates.Add(TPair<double, uint64>(Pair.Value.DistanceMeters + LayerBias, Pair.Key));
    }

    Candidates.Sort([](const TPair<double, uint64>& A, const TPair<double, uint64>& B)
    {
        return A.Key < B.Key;
    });

    const int32 Launches = FMath::Min(Budget, Candidates.Num());

    for (int32 Index = 0; Index < Launches; ++Index)
    {
        FActivePatch* Patch = Patches.Find(Candidates[Index].Value);

        if (Patch == nullptr)
        {
            continue;
        }

        Patch->bGenerating = true;
        Patch->Serial = NextSerial++;

        // Everything the worker needs, by value. The only shared reference is
        // the shared block; nothing captured has a lifetime tied to this
        // component.
        const FPlanetSurfaceDescriptor PlanetCopy = Planet;
        const FPlanetEnvironmentDescriptor EnvironmentCopy = Environment;
        const FPlanetTerrainSettings SettingsCopy = TerrainSettings;
        const FPlanetPatchId PatchIdCopy = Patch->PatchId;
        const EVegetationLayer LayerCopy = Patch->Layer;
        const uint64 SerialCopy = Patch->Serial;
        const int32 MaxCopy = MaxInstancesPerPatchLayer;

        TSharedPtr<FVegetationShared, ESPMode::ThreadSafe> SharedCopy = Shared;

        SharedCopy->InFlight.Increment();
        ++InFlightJobs;

        Async(EAsyncExecution::ThreadPool,
            [PlanetCopy, EnvironmentCopy, SettingsCopy, PatchIdCopy, LayerCopy, SerialCopy, MaxCopy, SharedCopy]()
            {
                const double StartSeconds = FPlatformTime::Seconds();

                TSharedPtr<FVegetationJobResult, ESPMode::ThreadSafe> Result =
                    MakeShared<FVegetationJobResult, ESPMode::ThreadSafe>();

                Result->PatchId = PatchIdCopy;
                Result->Layer = LayerCopy;
                Result->Serial = SerialCopy;

                FPlanetVegetation::Scatter(
                    PlanetCopy, EnvironmentCopy, SettingsCopy, PatchIdCopy, LayerCopy,
                    MaxCopy, Result->Instances, Result->bHitBudget);

                Result->MillisecondsTaken = (FPlatformTime::Seconds() - StartSeconds) * 1000.0;

                if (SharedCopy->bShutdown)
                {
                    SharedCopy->InFlight.Decrement();
                    return;
                }

                SharedCopy->Completed.Enqueue(Result);
                SharedCopy->InFlight.Decrement();
            });
    }
}

void UPlanetVegetationComponent::DrainResults()
{
    if (!Shared.IsValid())
    {
        return;
    }

    int32 Uploaded = 0;

    TSharedPtr<FVegetationJobResult, ESPMode::ThreadSafe> Result;

    while (Uploaded < MaxUploadsPerFrame && Shared->Completed.Dequeue(Result))
    {
        --InFlightJobs;

        if (!Result.IsValid())
        {
            continue;
        }

        const uint64 Key =
            MakePlanetPatchKey(Result->PatchId) ^ (static_cast<uint64>(Result->Layer) << 60);

        FActivePatch* Patch = Patches.Find(Key);

        // The patch may have been released while the job ran - the player moved
        // on. Discarding the result is the correct outcome and is the reason
        // the serial exists: a stale result must not be uploaded into a patch
        // that has since been re-activated with different content.
        if (Patch == nullptr || Patch->Serial != Result->Serial)
        {
            continue;
        }

        Patch->bGenerating = false;
        Patch->bLive = true;

        ++Uploaded;

        UE_LOG(LogPlanetVegetation, Verbose,
            TEXT("Patch %s layer %s: %d instances in %.2f ms"),
            *Result->PatchId.ToDebugString(), LexToString(Result->Layer),
            Result->Instances.Num(), Result->MillisecondsTaken);

        if (Result->Instances.Num() == 0)
        {
            continue;
        }

        // Group by archetype: one component per archetype in this patch-layer.
        TMap<uint8, UInstancedStaticMeshComponent*> Components;

        for (const FVegetationInstance& Instance : Result->Instances)
        {
            const uint8 ArchetypeKey = static_cast<uint8>(Instance.Archetype);

            UInstancedStaticMeshComponent** Found = Components.Find(ArchetypeKey);
            UInstancedStaticMeshComponent* Component = (Found != nullptr) ? *Found : nullptr;

            if (Component == nullptr)
            {
                Component = AcquireComponent(Instance.Archetype);

                if (Component == nullptr)
                {
                    continue;
                }

                Components.Add(ArchetypeKey, Component);
                Patch->Components.Add(Component);
            }

            // Build the transform in the planet's local frame. The component is
            // attached to the planet actor, so its parent transform carries the
            // render origin and this needs no rebasing of its own - which is
            // what lets tens of thousands of instances survive an origin shift
            // for free.
            const FVector Up(Instance.UpUnit.X, Instance.UpUnit.Y, Instance.UpUnit.Z);

            // Trees stand along up; rocks lie along the terrain normal. Using
            // the normal for a tree would have it leaning down every hillside,
            // which is the single most obvious tell of a naive scatter.
            const FVector Axis = ArchetypeHasCollision(Instance.Archetype)
                || Instance.Layer != EVegetationLayer::Scatter
                    ? Up
                    : FVector(Instance.NormalUnit.X, Instance.NormalUnit.Y, Instance.NormalUnit.Z);

            const FQuat Yaw(Up, Instance.YawRadians);

            FVector Forward = FVector::CrossProduct(Axis, FVector::ForwardVector);

            if (Forward.IsNearlyZero())
            {
                Forward = FVector::CrossProduct(Axis, FVector::RightVector);
            }

            Forward = Yaw.RotateVector(Forward.GetSafeNormal());

            const FRotator Rotation = FRotationMatrix::MakeFromZX(Axis, Forward).Rotator();

            const double SizeMeters =
                GetArchetypeSizeMeters(Instance.Archetype) * Instance.Scale;

            // Engine basic shapes are 100 uu across, so a metre of desired size
            // is one unit of scale after the centimetre conversion.
            const double MeshScale = SizeMeters * UniverseScale::CmPerMeter / 100.0;

            const FVector Location(
                Instance.PositionMeters.X * UniverseScale::CmPerMeter,
                Instance.PositionMeters.Y * UniverseScale::CmPerMeter,
                Instance.PositionMeters.Z * UniverseScale::CmPerMeter);

            const double WidthScale = MeshScale * GetArchetypeWidthRatio(Instance.Archetype);

            const FTransform Transform(
                Rotation, Location, FVector(WidthScale, WidthScale, MeshScale));

            Component->AddInstance(Transform, /*bWorldSpace=*/false);
        }

        Patch->InstanceCount = Result->Instances.Num();

        TotalInstances += Patch->InstanceCount;
        TotalGenerated += Patch->InstanceCount;
        LayerInstances[static_cast<int32>(Patch->Layer)] += Patch->InstanceCount;

        if (Result->bHitBudget)
        {
            UE_LOG(LogPlanetVegetation, Verbose,
                TEXT("Patch %s layer %s hit its %d instance budget."),
                *Result->PatchId.ToDebugString(), LexToString(Result->Layer), MaxInstancesPerPatchLayer);
        }
    }
}

UInstancedStaticMeshComponent* UPlanetVegetationComponent::AcquireComponent(EVegetationArchetype Archetype)
{
    UStaticMesh* Mesh = GetArchetypeMesh(Archetype);

    if (Mesh == nullptr)
    {
        return nullptr;
    }

    AActor* Owner = GetOwner();

    if (Owner == nullptr)
    {
        return nullptr;
    }

    UInstancedStaticMeshComponent* Component = nullptr;

    if (ComponentPool.Num() > 0)
    {
        Component = ComponentPool.Pop().Get();
    }

    if (Component == nullptr)
    {
        Component = NewObject<UInstancedStaticMeshComponent>(Owner);

        if (Component == nullptr)
        {
            return nullptr;
        }

        Component->SetupAttachment(this);
        Component->RegisterComponent();
    }

    Component->SetStaticMesh(Mesh);

    if (VegetationMaterial != nullptr)
    {
        UMaterialInstanceDynamic* Tinted = Component->CreateDynamicMaterialInstance(0, VegetationMaterial);

        if (Tinted != nullptr)
        {
            Tinted->SetVectorParameterValue(TEXT("Color"), GetArchetypeColour(Archetype));
        }
    }

    // Collision only where it matters. Everything else is decoration, and
    // decoration with collision is what makes a forest impossible to walk
    // through and a landing site impossible to find.
    Component->SetCollisionEnabled(
        ArchetypeHasCollision(Archetype)
            ? ECollisionEnabled::QueryAndPhysics
            : ECollisionEnabled::NoCollision);

    Component->SetCastShadow(false);
    Component->SetVisibility(true);
    Component->SetHiddenInGame(false);

    return Component;
}

void UPlanetVegetationComponent::RecycleComponent(UInstancedStaticMeshComponent* Component)
{
    if (Component == nullptr)
    {
        return;
    }

    // Cleared and hidden rather than destroyed. Destroying and recreating
    // components at streaming rates is a steady source of garbage and of
    // render-state churn; a pool turns both into a fixed cost.
    Component->ClearInstances();
    Component->SetVisibility(false);
    Component->SetHiddenInGame(true);
    Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);

    ComponentPool.Add(Component);
}

void UPlanetVegetationComponent::ReleasePatch(FActivePatch& Patch)
{
    for (const TObjectPtr<UInstancedStaticMeshComponent>& Component : Patch.Components)
    {
        RecycleComponent(Component.Get());
    }

    Patch.Components.Reset();

    TotalInstances -= Patch.InstanceCount;
    TotalReleased += Patch.InstanceCount;
    LayerInstances[static_cast<int32>(Patch.Layer)] -= Patch.InstanceCount;

    Patch.InstanceCount = 0;
    Patch.bLive = false;

    // The serial is *not* reset. A job for this patch may still be running, and
    // bumping past its serial is how its result gets discarded rather than
    // uploaded into a patch that no longer wants it.
    Patch.Serial = NextSerial++;
}

void UPlanetVegetationComponent::ReleaseAll()
{
    for (TPair<uint64, FActivePatch>& Pair : Patches)
    {
        ReleasePatch(Pair.Value);
    }

    Patches.Reset();

    TotalInstances = 0;

    for (int32 Index = 0; Index < static_cast<int32>(EVegetationLayer::Count); ++Index)
    {
        LayerInstances[Index] = 0;
    }
}
