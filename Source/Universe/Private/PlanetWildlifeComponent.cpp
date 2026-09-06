// Copyright Universe Project. All Rights Reserved.

#include "PlanetWildlifeComponent.h"
#include "PlanetEnvironmentQuery.h"
#include "PlanetSurfaceQuery.h"
#include "UniverseScale.h"
#include "UniverseHash.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanetWildlife, Log, All);

namespace
{
    static TAutoConsoleVariable<int32> CVarWildlifeEnabled(
        TEXT("universe.Wildlife"),
        1,
        TEXT("Enable wildlife streaming."),
        ECVF_Cheat);

    double UnitFromHash(uint64 Hash)
    {
        return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);
    }
}

UPlanetWildlifeComponent::UPlanetWildlifeComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

double UPlanetWildlifeComponent::GetBirdDensityPerHectare(EPlanetBiome Biome)
{
    // A population *density*, exactly like vegetation, and for the same reason:
    // it is a property of the biome rather than of the patch, so it does not
    // change when the streaming radius does.
    //
    // Real breeding-bird densities run 1-10 pairs per hectare in temperate
    // woodland, but almost none of them are in the air at once - the number
    // actually *visible* over a field is closer to one or two.
    //
    // These are the visible-in-flight densities rather than the population
    // ones, scaled so a forest has fifteen or so birds in view and a desert
    // has one. The ratios between biomes are the real ones; the absolute level
    // is a presentation choice, and saying so here is better than implying a
    // census that is not being taken.
    switch (Biome)
    {
    case EPlanetBiome::Rainforest:      return 0.9;
    case EPlanetBiome::TemperateForest: return 0.7;
    case EPlanetBiome::Wetland:         return 0.8;
    case EPlanetBiome::Coast:           return 0.6;
    case EPlanetBiome::Grassland:       return 0.4;
    case EPlanetBiome::Savanna:         return 0.35;
    case EPlanetBiome::Taiga:           return 0.3;
    case EPlanetBiome::Tundra:          return 0.08;
    case EPlanetBiome::Desert:          return 0.04;

    // Nothing flies over open ocean in numbers, and nothing at all over ice or
    // bare rock. Zero here is a statement about the biome, not a shortcut.
    case EPlanetBiome::Ocean:
    case EPlanetBiome::Snow:
    case EPlanetBiome::BarrenRock:
    default:                            return 0.0;
    }
}

void UPlanetWildlifeComponent::SetPlanet(
    const FPlanetSurfaceDescriptor& InPlanet,
    const FPlanetEnvironmentDescriptor& InEnvironment,
    const FPlanetTerrainSettings& InSettings)
{
    Planet = InPlanet;
    Environment = InEnvironment;
    TerrainSettings = InSettings;

    Birds.Reset();
}

void UPlanetWildlifeComponent::SetObserverPositionMeters(const FVector3d& InObserverMeters)
{
    ObserverMeters = InObserverMeters;
    bHasObserver = true;
}

FVector3d UPlanetWildlifeComponent::PickTarget(const FVector3d& Around, uint64 Salt) const
{
    // A point in the habitat volume: somewhere within the radius, at a height
    // in the flying band. Derived from a hash so the wander is reproducible for
    // a given individual rather than depending on frame timing.
    FVector3d Up;

    if (!FPlanetSurfaceQuery::TryGetDirection(Around, Up))
    {
        return Around;
    }

    FVector3d TangentU;
    FVector3d TangentV;
    FPlanetTerrain::GetTangentBasis(Up, TangentU, TangentV);

    const double OffsetU = (UnitFromHash(UniverseHash::Hash(Salt, 0x11u, 0)) - 0.5) * 2.0
        * HabitatRadiusMeters;
    const double OffsetV = (UnitFromHash(UniverseHash::Hash(Salt, 0x12u, 0)) - 0.5) * 2.0
        * HabitatRadiusMeters;

    const double Height = FMath::Lerp(
        static_cast<double>(MinHeightMeters), static_cast<double>(MaxHeightMeters),
        UnitFromHash(UniverseHash::Hash(Salt, 0x13u, 0)));

    const FVector3d Horizontal(
        TangentU.X * OffsetU + TangentV.X * OffsetV,
        TangentU.Y * OffsetU + TangentV.Y * OffsetV,
        TangentU.Z * OffsetU + TangentV.Z * OffsetV);

    const FVector3d Candidate(
        Around.X + Horizontal.X, Around.Y + Horizontal.Y, Around.Z + Horizontal.Z);

    // Put it at the wanted height above the *terrain* under that point, which
    // is what stops birds flying into hillsides without needing any avoidance
    // behaviour at all. Being correct by construction beats steering.
    FVector3d Direction;

    if (!FPlanetSurfaceQuery::TryGetDirection(Candidate, Direction))
    {
        return Around;
    }

    return FPlanetSurfaceQuery::GetPositionAboveTerrain(
        Planet, TerrainSettings, Direction, Height);
}

void UPlanetWildlifeComponent::TickComponent(
    float DeltaTime,
    ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

    if (!bHasObserver || !Planet.IsValid() || !Environment.IsValid())
    {
        return;
    }

    const bool bEnabled = CVarWildlifeEnabled.GetValueOnGameThread() != 0;

    const double AltitudeMeters = FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
        Planet, TerrainSettings, ObserverMeters);

    if (!bEnabled
        || !Environment.HasLife()
        || AltitudeMeters > static_cast<double>(MaxObserverAltitudeMeters))
    {
        if (Birds.Num() > 0)
        {
            Birds.Reset();
            UpdateInstances();
        }

        TargetPopulation = 0;
        return;
    }

    UpdatePopulation(static_cast<double>(DeltaTime));
    UpdateInstances();
}

void UPlanetWildlifeComponent::UpdatePopulation(double DeltaSeconds)
{
    // --- Census -------------------------------------------------------------
    //
    // Recomputed on a timer rather than every frame: it costs an environment
    // sample, and the answer changes on the scale of a player walking between
    // biomes, not of a frame.
    TimeSinceCensus += DeltaSeconds;

    if (TimeSinceCensus > 1.5 || Birds.Num() == 0)
    {
        TimeSinceCensus = 0.0;

        const FEnvironmentSample Here = FPlanetEnvironmentQuery::SampleStatic(
            Planet, Environment, TerrainSettings, ObserverMeters);

        double Density = 0.0;

        // Weighted across the biome blend, so a forest edge has fewer birds
        // than its interior rather than the same number right up to a line.
        for (int32 Index = 0; Index < Here.Biome.Num; ++Index)
        {
            Density += GetBirdDensityPerHectare(Here.Biome.Biomes[Index]) * Here.Biome.Weights[Index];
        }

        const FWeatherSample Weather = FPlanetWeather::Sample(Environment, Here.Climate, 0.0);

        // Fewer birds in bad weather. Not a simulation of anything - a single
        // multiplier - but it is the hook section 49 asks for, and it is driven
        // by the weather field rather than by a random number.
        const double WeatherFactor = FMath::Clamp(
            1.0 - Weather.Precipitation * 0.8 - FMath::Max(Weather.WindSpeedMs - 12.0, 0.0) * 0.05,
            0.0, 1.0);

        const double AreaHectares = (PI * HabitatRadiusMeters * HabitatRadiusMeters) / 10000.0;

        TargetPopulation = FMath::Clamp(
            FMath::RoundToInt32(Density * AreaHectares * WeatherFactor * Environment.VegetationPotential),
            0, MaxBirds);
    }

    // --- Spawn and despawn --------------------------------------------------
    while (Birds.Num() > TargetPopulation)
    {
        Birds.Pop();
    }

    while (Birds.Num() < TargetPopulation)
    {
        FBird Bird;

        const uint64 Salt = UniverseHash::Hash(
            Environment.Seed.Value, SpawnCounter++, 0u);

        Bird.PositionMeters = PickTarget(ObserverMeters, Salt);
        Bird.TargetMeters = PickTarget(ObserverMeters, Salt ^ 0x9E3779B97F4A7C15ull);
        Bird.SpeedScale = FMath::Lerp(0.7, 1.4, UnitFromHash(UniverseHash::Hash(Salt, 0x14u, 0)));

        Birds.Add(Bird);
    }

    // --- Move ---------------------------------------------------------------
    for (int32 Index = 0; Index < Birds.Num(); ++Index)
    {
        FBird& Bird = Birds[Index];

        Bird.AgeSeconds += DeltaSeconds;

        FVector3d ToTarget(
            Bird.TargetMeters.X - Bird.PositionMeters.X,
            Bird.TargetMeters.Y - Bird.PositionMeters.Y,
            Bird.TargetMeters.Z - Bird.PositionMeters.Z);

        const double Distance = ToTarget.Size();

        // Arrived, or wandered long enough: pick somewhere else. The age term
        // stops a bird that cannot quite reach its target from circling it
        // forever.
        if (Distance < 8.0 || Bird.AgeSeconds > 22.0)
        {
            const uint64 Salt = UniverseHash::Hash(
                Environment.Seed.Value, SpawnCounter++, static_cast<uint32>(Index));

            Bird.TargetMeters = PickTarget(ObserverMeters, Salt);
            Bird.AgeSeconds = 0.0;

            continue;
        }

        const FVector3d Heading(
            ToTarget.X / Distance, ToTarget.Y / Distance, ToTarget.Z / Distance);

        // Turn toward the target rather than snapping, so the flight reads as
        // flight rather than as points being moved.
        const double Turn = FMath::Clamp(DeltaSeconds * 1.6, 0.0, 1.0);

        Bird.HeadingUnit = FVector3d(
            FMath::Lerp(Bird.HeadingUnit.X, Heading.X, Turn),
            FMath::Lerp(Bird.HeadingUnit.Y, Heading.Y, Turn),
            FMath::Lerp(Bird.HeadingUnit.Z, Heading.Z, Turn)).GetSafeNormal();

        if (Bird.HeadingUnit.IsZero())
        {
            Bird.HeadingUnit = Heading;
        }

        const double Step = SpeedMs * Bird.SpeedScale * DeltaSeconds;

        Bird.PositionMeters = FVector3d(
            Bird.PositionMeters.X + Bird.HeadingUnit.X * Step,
            Bird.PositionMeters.Y + Bird.HeadingUnit.Y * Step,
            Bird.PositionMeters.Z + Bird.HeadingUnit.Z * Step);

        // A floor, checked every frame. Interpolating toward a target that is
        // safely above the ground is not the same as never being below it -
        // a bird crossing a ridge between two safe points would fly through it.
        const double Clearance = FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
            Planet, TerrainSettings, Bird.PositionMeters);

        if (Clearance < MinHeightMeters * 0.5)
        {
            FVector3d Direction;

            if (FPlanetSurfaceQuery::TryGetDirection(Bird.PositionMeters, Direction))
            {
                Bird.PositionMeters = FPlanetSurfaceQuery::GetPositionAboveTerrain(
                    Planet, TerrainSettings, Direction, MinHeightMeters);
            }
        }
    }
}

void UPlanetWildlifeComponent::UpdateInstances()
{
    if (Instances == nullptr)
    {
        AActor* Owner = GetOwner();

        if (Owner == nullptr)
        {
            return;
        }

        Instances = NewObject<UInstancedStaticMeshComponent>(Owner);

        if (Instances == nullptr)
        {
            return;
        }

        Instances->SetupAttachment(this);
        Instances->RegisterComponent();
        Instances->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Instances->SetCastShadow(false);

        if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(
                nullptr, TEXT("/Engine/BasicShapes/Cone.Cone")))
        {
            Instances->SetStaticMesh(Mesh);
        }

        if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(
                nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
        {
            if (UMaterialInstanceDynamic* Tinted =
                    Instances->CreateDynamicMaterialInstance(0, Material))
            {
                Tinted->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.05f, 0.04f, 0.04f));
            }
        }
    }

    // Rebuilt wholesale each frame rather than updated per instance.
    //
    // At forty instances the difference is unmeasurable, and clearing avoids an
    // entire class of index-management bug: an instance array that is added to
    // and removed from in place has to keep its indices in step with the bird
    // array, and getting that wrong makes one bird render at another's
    // position, which is exactly the sort of thing nobody notices.
    Instances->ClearInstances();

    for (const FBird& Bird : Birds)
    {
        const FVector Location(
            Bird.PositionMeters.X * UniverseScale::CmPerMeter,
            Bird.PositionMeters.Y * UniverseScale::CmPerMeter,
            Bird.PositionMeters.Z * UniverseScale::CmPerMeter);

        const FVector Forward(Bird.HeadingUnit.X, Bird.HeadingUnit.Y, Bird.HeadingUnit.Z);

        const FVector3d Up = FPlanetSurfaceQuery::GetLocalUp(Bird.PositionMeters);

        const FRotator Rotation =
            FRotationMatrix::MakeFromXZ(Forward, FVector(Up.X, Up.Y, Up.Z)).Rotator();

        // Small, and wider than long: a cone pointed along the flight direction
        // is not a bird, but at fifty metres it reads as one.
        const FTransform Transform(Rotation, Location, FVector(70.0, 30.0, 20.0));

        Instances->AddInstance(Transform, /*bWorldSpace=*/false);
    }
}
