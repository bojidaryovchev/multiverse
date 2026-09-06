// Copyright Universe Project. All Rights Reserved.

#include "PlanetStructureComponent.h"
#include "WorldStateSubsystem.h"
#include "PlanetSurfaceQuery.h"
#include "PlanetTerrain.h"
#include "UniverseScale.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanetStructures, Log, All);

UPlanetStructureComponent::UPlanetStructureComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

UStaticMesh* UPlanetStructureComponent::GetTypeMesh(const FString& TypeId)
{
    // Placeholder geometry, as section 85 allows. What matters is that the
    // preview and the placed structure resolve through the *same* function, so
    // what the player aims with is what they get.
    const TCHAR* Path = TypeId == WorldEntityTypes::Foundation
        ? TEXT("/Engine/BasicShapes/Cube.Cube")
        : TEXT("/Engine/BasicShapes/Cylinder.Cylinder");

    return LoadObject<UStaticMesh>(nullptr, Path);
}

double UPlanetStructureComponent::GetTypeSizeMeters(const FString& TypeId)
{
    return TypeId == WorldEntityTypes::Foundation ? 6.0 : 8.0;
}

FLinearColor UPlanetStructureComponent::GetTypeColour(const FString& TypeId)
{
    return TypeId == WorldEntityTypes::Foundation
        ? FLinearColor(0.45f, 0.42f, 0.38f)
        : FLinearColor(0.90f, 0.55f, 0.10f);
}

UWorldStateSubsystem* UPlanetStructureComponent::GetWorldState() const
{
    UWorld* World = GetWorld();
    return (World != nullptr) ? World->GetSubsystem<UWorldStateSubsystem>() : nullptr;
}

void UPlanetStructureComponent::BeginPlay()
{
    Super::BeginPlay();

    if (UWorldStateSubsystem* WorldState = GetWorldState())
    {
        RegionLoadedHandle = WorldState->OnRegionLoaded.AddUObject(
            this, &UPlanetStructureComponent::HandleRegionLoaded);

        RegionUnloadedHandle = WorldState->OnRegionUnloaded.AddUObject(
            this, &UPlanetStructureComponent::HandleRegionUnloaded);

        EntityCreatedHandle = WorldState->OnEntityCreated.AddUObject(
            this, &UPlanetStructureComponent::HandleEntityCreated);

        EntityRemovedHandle = WorldState->OnEntityRemoved.AddUObject(
            this, &UPlanetStructureComponent::HandleEntityRemoved);
    }
}

void UPlanetStructureComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    if (UWorldStateSubsystem* WorldState = GetWorldState())
    {
        WorldState->OnRegionLoaded.Remove(RegionLoadedHandle);
        WorldState->OnRegionUnloaded.Remove(RegionUnloadedHandle);
        WorldState->OnEntityCreated.Remove(EntityCreatedHandle);
        WorldState->OnEntityRemoved.Remove(EntityRemovedHandle);
    }

    Super::EndPlay(EndPlayReason);
}

void UPlanetStructureComponent::SetPlanet(
    const FPlanetSurfaceDescriptor& InPlanet,
    const FPlanetTerrainSettings& InSettings)
{
    Planet = InPlanet;
    TerrainSettings = InSettings;
}

void UPlanetStructureComponent::HandleRegionLoaded(const FWorldRegionDelta& Delta)
{
    if (Delta.RegionId.PlanetKey != Planet.PlanetKey)
    {
        // Another planet's region. Ignoring it here is what keeps two planets'
        // structures from appearing on each other.
        return;
    }

    for (const FWorldEntityRecord& Record : Delta.Created)
    {
        SpawnStructure(Record);
    }
}

void UPlanetStructureComponent::HandleRegionUnloaded(const FPersistenceRegionId& RegionId)
{
    if (RegionId.PlanetKey != Planet.PlanetKey)
    {
        return;
    }

    TArray<FPersistentEntityId> ToRemove;

    for (const TPair<FPersistentEntityId, FLiveStructure>& Pair : Live)
    {
        if (Pair.Value.Record.RegionId == RegionId)
        {
            ToRemove.Add(Pair.Key);
        }
    }

    for (const FPersistentEntityId& Id : ToRemove)
    {
        Live.Remove(Id);
    }

    if (ToRemove.Num() > 0)
    {
        bInstancesDirty = true;
        RebuildInstances();

        UE_LOG(LogPlanetStructures, Verbose,
            TEXT("Region %s unloaded: %d structure representation(s) released."),
            *RegionId.ToString(), ToRemove.Num());
    }
}

void UPlanetStructureComponent::HandleEntityCreated(const FWorldEntityRecord& Record)
{
    if (Record.RegionId.PlanetKey == Planet.PlanetKey)
    {
        SpawnStructure(Record);
    }
}

void UPlanetStructureComponent::HandleEntityRemoved(const FPersistentEntityId& EntityId)
{
    DespawnStructure(EntityId);
}

void UPlanetStructureComponent::SpawnStructure(const FWorldEntityRecord& Record)
{
    if (!Record.IsValid() || Record.bIsRemoval || !Planet.IsValid())
    {
        return;
    }

    // Already represented.
    //
    // This is what makes rapid region traversal safe: a region loaded, evicted
    // and loaded again broadcasts twice, and without this check the second
    // broadcast would produce a second beacon standing inside the first.
    if (Live.Contains(Record.EntityId))
    {
        return;
    }

    // Reconstruct the position from the stored direction and height.
    //
    // The terrain is re-evaluated rather than a position being stored, and that
    // is the point of storing a direction: the structure sits on the ground
    // wherever the ground *is*, so it stays planted rather than floating if the
    // patch it is on happens to be at a different LOD than when it was placed.
    const FVector3d Position = FPlanetSurfaceQuery::GetPositionAboveTerrain(
        Planet, TerrainSettings, Record.Placement.Direction,
        Record.Placement.HeightAboveTerrainMeters);

    FLiveStructure Structure;
    Structure.Record = Record;
    Structure.PositionMeters = Position;
    Structure.TypeId = Record.TypeId;

    Live.Add(Record.EntityId, MoveTemp(Structure));

    UE_LOG(LogPlanetStructures, Log,
        TEXT("Structure %s (%s) now represented; %d live."),
        *Record.EntityId.ToHexString(), *Record.TypeId, Live.Num());

    bInstancesDirty = true;
    RebuildInstances();
}

void UPlanetStructureComponent::DespawnStructure(const FPersistentEntityId& EntityId)
{
    if (Live.Remove(EntityId) > 0)
    {
        bInstancesDirty = true;
        RebuildInstances();
    }
}

UInstancedStaticMeshComponent* UPlanetStructureComponent::GetOrCreateComponent(const FString& TypeId)
{
    if (TObjectPtr<UInstancedStaticMeshComponent>* Found = Components.Find(TypeId))
    {
        return Found->Get();
    }

    AActor* Owner = GetOwner();

    if (Owner == nullptr)
    {
        return nullptr;
    }

    UInstancedStaticMeshComponent* Component = NewObject<UInstancedStaticMeshComponent>(Owner);

    if (Component == nullptr)
    {
        return nullptr;
    }

    Component->SetupAttachment(this);
    Component->RegisterComponent();

    if (UStaticMesh* Mesh = GetTypeMesh(TypeId))
    {
        Component->SetStaticMesh(Mesh);
    }

    if (UMaterialInterface* Material = LoadObject<UMaterialInterface>(
            nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")))
    {
        if (UMaterialInstanceDynamic* Tinted = Component->CreateDynamicMaterialInstance(0, Material))
        {
            Tinted->SetVectorParameterValue(TEXT("Color"), GetTypeColour(TypeId));
        }
    }

    // Structures are solid. Unlike vegetation, where collision on grass would
    // be ruinous, a building the player can walk through is not a building.
    Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    Component->SetCastShadow(false);

    Components.Add(TypeId, Component);

    return Component;
}

void UPlanetStructureComponent::RebuildInstances()
{
    if (!bInstancesDirty)
    {
        return;
    }

    bInstancesDirty = false;

    for (TPair<FString, TObjectPtr<UInstancedStaticMeshComponent>>& Pair : Components)
    {
        if (Pair.Value != nullptr)
        {
            Pair.Value->ClearInstances();
        }
    }

    for (TPair<FPersistentEntityId, FLiveStructure>& Pair : Live)
    {
        FLiveStructure& Structure = Pair.Value;

        UInstancedStaticMeshComponent* Component = GetOrCreateComponent(Structure.TypeId);

        if (Component == nullptr)
        {
            continue;
        }

        // Rebuild the orientation from the stored yaw and the local tangent
        // frame *at load time*, not from a saved world rotation.
        //
        // This is what makes a structure survive the planet turning, an origin
        // rebase, and being approached from a different direction: what was
        // preserved is its relationship to the ground, and the world rotation
        // is re-derived from that every time.
        const FVector3d Up = FPlanetSurfaceQuery::GetLocalUp(Structure.PositionMeters);

        FVector3d TangentU;
        FVector3d TangentV;
        FPlanetTerrain::GetTangentBasis(Up, TangentU, TangentV);

        const FVector UpVector(Up.X, Up.Y, Up.Z);
        const FQuat Yaw(UpVector, Structure.Record.Placement.YawRadians);

        const FVector Forward =
            Yaw.RotateVector(FVector(TangentU.X, TangentU.Y, TangentU.Z)).GetSafeNormal();

        const FRotator Rotation = FRotationMatrix::MakeFromZX(UpVector, Forward).Rotator();

        const double SizeMeters =
            GetTypeSizeMeters(Structure.TypeId) * Structure.Record.Placement.Scale;

        const double MeshScale = SizeMeters * UniverseScale::CmPerMeter / 100.0;

        const FVector Location(
            Structure.PositionMeters.X * UniverseScale::CmPerMeter,
            Structure.PositionMeters.Y * UniverseScale::CmPerMeter,
            Structure.PositionMeters.Z * UniverseScale::CmPerMeter);

        const FTransform Transform(
            Rotation, Location, FVector(MeshScale * 0.35, MeshScale * 0.35, MeshScale));

        Structure.InstanceIndex = Component->AddInstance(Transform, /*bWorldSpace=*/false);
    }
}

FPersistentEntityId UPlanetStructureComponent::FindNearest(
    const FVector3d& PlanetLocalMeters,
    double RadiusMeters,
    FVector3d& OutPositionMeters) const
{
    FPersistentEntityId Best;
    double BestDistanceSquared = RadiusMeters * RadiusMeters;

    for (const TPair<FPersistentEntityId, FLiveStructure>& Pair : Live)
    {
        const FVector3d Delta(
            Pair.Value.PositionMeters.X - PlanetLocalMeters.X,
            Pair.Value.PositionMeters.Y - PlanetLocalMeters.Y,
            Pair.Value.PositionMeters.Z - PlanetLocalMeters.Z);

        const double DistanceSquared = Delta.SizeSquared();

        if (DistanceSquared < BestDistanceSquared)
        {
            BestDistanceSquared = DistanceSquared;
            Best = Pair.Key;
            OutPositionMeters = Pair.Value.PositionMeters;
        }
    }

    return Best;
}
