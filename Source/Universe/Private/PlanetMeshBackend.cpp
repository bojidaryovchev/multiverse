// Copyright Universe Project. All Rights Reserved.

#include "PlanetMeshBackend.h"
#include "ProceduralMeshComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInterface.h"
#include "UniverseScale.h"
#include "HAL/IConsoleManager.h"

namespace
{
    /** Terrain works in metres; Unreal works in centimetres. */
    constexpr double MetersToCm = UniverseScale::CmPerMeter;

    /**
     * Which structure the terrain colour reveals.
     *
     * Sprint 002 asks for patch boundaries, LOD levels and cube faces to be
     * visualisable. All three are properties of the patch a vertex belongs to,
     * so rather than three materials they are three encodings written into
     * vertex colour at upload time, selected here. Changing the mode rebuilds
     * the terrain, which is instant enough for a debug toggle and avoids
     * carrying four sets of vertex colours around permanently.
     */
    static TAutoConsoleVariable<int32> CVarTerrainDebugMode(
        TEXT("universe.TerrainDebugMode"),
        0,
        TEXT("Terrain colouring: 0 biome, 1 LOD level, 2 cube face, 3 patch checkerboard, ")
        TEXT("4 elevation, 5 temperature, 6 humidity, 7 biome index."),
        ECVF_Cheat);

    /** Distinct, evenly spread hues for categorical debug modes. */
    FColor CategoricalColor(int32 Index)
    {
        static const FColor Palette[8] = {
            FColor(220,  60,  60), FColor( 60, 200,  90), FColor( 70, 110, 230),
            FColor(230, 190,  60), FColor(200,  90, 220), FColor( 70, 210, 210),
            FColor(240, 140,  60), FColor(170, 170, 170),
        };
        return Palette[Index & 7];
    }

    /**
     * Elevation ramp: ocean blue, coastal, green lowland, brown highland, white
     * peaks. Crude on purpose - the goal is reading the terrain's shape at a
     * glance, not environment art.
     */
    FColor ElevationColor(float ElevationMeters, float MaxElevation, float MaxDepth)
    {
        if (ElevationMeters < 0.0f)
        {
            const float Depth = FMath::Clamp(-ElevationMeters / FMath::Max(MaxDepth, 1.0f), 0.0f, 1.0f);
            const uint8 Blue = static_cast<uint8>(FMath::Lerp(180.0f, 60.0f, Depth));
            return FColor(static_cast<uint8>(FMath::Lerp(40.0f, 5.0f, Depth)),
                          static_cast<uint8>(FMath::Lerp(90.0f, 25.0f, Depth)),
                          Blue, 255);
        }

        const float Height = FMath::Clamp(ElevationMeters / FMath::Max(MaxElevation, 1.0f), 0.0f, 1.0f);

        if (Height < 0.08f)
        {
            return FColor(210, 200, 150, 255);   // coast
        }
        if (Height < 0.45f)
        {
            const float T = (Height - 0.08f) / 0.37f;
            return FColor(static_cast<uint8>(FMath::Lerp(70.0f, 120.0f, T)),
                          static_cast<uint8>(FMath::Lerp(140.0f, 110.0f, T)),
                          static_cast<uint8>(FMath::Lerp(60.0f, 60.0f, T)), 255);
        }
        if (Height < 0.78f)
        {
            const float T = (Height - 0.45f) / 0.33f;
            return FColor(static_cast<uint8>(FMath::Lerp(120.0f, 150.0f, T)),
                          static_cast<uint8>(FMath::Lerp(110.0f, 130.0f, T)),
                          static_cast<uint8>(FMath::Lerp(60.0f, 120.0f, T)), 255);
        }

        const float T = (Height - 0.78f) / 0.22f;
        const uint8 White = static_cast<uint8>(FMath::Lerp(180.0f, 255.0f, T));
        return FColor(White, White, White, 255);
    }

    /** Vertex colour for the currently selected debug mode. */
    /**
     * A blue-to-red ramp for a normalised scalar, for the field overlays.
     *
     * Deliberately not a rainbow. A rainbow ramp has no perceptual ordering -
     * nobody can tell whether green is above or below yellow without checking a
     * key - so a two-ended ramp with a neutral middle is easier to read at a
     * glance and, more importantly, makes a *gradient* legible, which is what
     * these overlays exist to check.
     */
    FColor RampColor(double Normalised)
    {
        const double T = FMath::Clamp(Normalised, 0.0, 1.0);

        const double R = FMath::Clamp(T * 2.0, 0.0, 1.0);
        const double B = FMath::Clamp((1.0 - T) * 2.0, 0.0, 1.0);
        const double G = 1.0 - FMath::Abs(T - 0.5) * 2.0;

        return FColor(
            static_cast<uint8>(R * 255.0),
            static_cast<uint8>(G * 200.0),
            static_cast<uint8>(B * 255.0),
            255);
    }

    FColor EncodeDebugColor(
        int32 Mode,
        float ElevationMeters, float MaxElevation, float MaxDepth,
        const FPlanetPatchId& PatchId,
        int32 VertexIndex, int32 Resolution,
        uint32 BiomeColor, uint8 BiomeIndex, float TemperatureK, float Humidity)
    {
        switch (Mode)
        {
        case 1:  // LOD level
            return CategoricalColor(PatchId.Level);

        case 2:  // cube face
            return CategoricalColor(PatchId.Face);

        case 3:  // patch checkerboard, so borders are unmistakable
        {
            const bool bAlternate = ((PatchId.X + PatchId.Y) & 1u) != 0u;

            // A darker band on the outermost ring draws the patch outline.
            const int32 Row = (Resolution > 0) ? (VertexIndex / Resolution) : 0;
            const int32 Column = (Resolution > 0) ? (VertexIndex % Resolution) : 0;
            const bool bEdge =
                Row == 0 || Column == 0 || Row == Resolution - 1 || Column == Resolution - 1;

            if (bEdge)
            {
                return FColor(255, 255, 0, 255);
            }
            return bAlternate ? FColor(90, 90, 110, 255) : FColor(160, 160, 180, 255);
        }

        case 4:  // elevation, the Sprint 002 view
            return ElevationColor(ElevationMeters, MaxElevation, MaxDepth);

        case 5:  // temperature, -50 C to +50 C
            return RampColor((static_cast<double>(TemperatureK) - 223.15) / 100.0);

        case 6:  // humidity
            return RampColor(static_cast<double>(Humidity));

        case 7:  // biome, one flat colour each
            return CategoricalColor(BiomeIndex);

        case 0:
        default:
            // The default is now the biome appearance rather than the elevation
            // ramp. Sprint 002's ramp said how high the ground was; this says
            // what it *is*, which is the thing the sprint set out to make
            // visible, and it comes straight from the generator rather than
            // being re-derived here.
            return FColor(
                static_cast<uint8>((BiomeColor >> 16) & 0xFFu),
                static_cast<uint8>((BiomeColor >> 8) & 0xFFu),
                static_cast<uint8>(BiomeColor & 0xFFu),
                255);
        }
    }
}

void UPlanetMeshBackend_ProceduralMesh::Initialise(AActor* InOwner, UMaterialInterface* InMaterial)
{
    Owner = InOwner;
    Material = InMaterial;
}

int32 UPlanetMeshBackend_ProceduralMesh::AcquireSlot()
{
    if (Owner == nullptr)
    {
        return INDEX_NONE;
    }

    if (FreeSlots.Num() > 0)
    {
        const int32 Slot = FreeSlots.Pop(EAllowShrinking::No);
        SlotInUse[Slot] = true;
        return Slot;
    }

    UProceduralMeshComponent* Component = NewObject<UProceduralMeshComponent>(Owner);
    if (Component == nullptr)
    {
        return INDEX_NONE;
    }

    Component->SetupAttachment(Owner->GetRootComponent());
    Component->RegisterComponent();

    // Terrain never moves once built; marking it static lets the renderer take
    // the cheaper path.
    Component->SetMobility(EComponentMobility::Movable);
    Component->bUseAsyncCooking = true;
    Component->SetCastShadow(false);

    if (Material != nullptr)
    {
        Component->SetMaterial(0, Material);
    }

    const int32 Slot = Components.Add(Component);
    SlotInUse.Add(true);
    SlotHasCollision.Add(false);

    return Slot;
}

void UPlanetMeshBackend_ProceduralMesh::UpdateSlot(int32 Slot, const FPlanetPatchMesh& Mesh, bool bWithCollision)
{
    if (!Components.IsValidIndex(Slot) || Components[Slot] == nullptr)
    {
        return;
    }

    UProceduralMeshComponent* Component = Components[Slot];

    const int32 VertexCount = Mesh.GetVertexCount();
    if (VertexCount == 0 || Mesh.Indices.Num() == 0)
    {
        Component->ClearAllMeshSections();
        Component->SetVisibility(false);
        return;
    }

    ScratchVertices.Reset(VertexCount);
    ScratchNormals.Reset(VertexCount);
    ScratchUVs.Reset(VertexCount);
    ScratchColors.Reset(VertexCount);
    ScratchTangents.Reset(0);

    const float MaxElevation = static_cast<float>(FMath::Max(Mesh.MaxElevationMeters, 1.0));
    const float MaxDepth = static_cast<float>(FMath::Max(-Mesh.MinElevationMeters, 1.0));

    const int32 DebugMode = CVarTerrainDebugMode.GetValueOnGameThread();

    // Grid rows are square, so the resolution is the square root of the grid
    // vertex count - needed by the checkerboard mode to find patch edges.
    int32 Resolution = 0;
    while (Resolution * Resolution < Mesh.GridVertexCount)
    {
        ++Resolution;
    }

    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        // Metres to centimetres. The values are patch-local, so they stay small
        // and float precision is sub-millimetre.
        ScratchVertices.Add(FVector(
            static_cast<double>(Mesh.PositionX[Index]) * MetersToCm,
            static_cast<double>(Mesh.PositionY[Index]) * MetersToCm,
            static_cast<double>(Mesh.PositionZ[Index]) * MetersToCm));

        ScratchNormals.Add(FVector(
            Mesh.NormalX[Index], Mesh.NormalY[Index], Mesh.NormalZ[Index]));

        // U carries elevation, V carries the level, so a material can band by
        // height or visualise LOD without extra plumbing.
        ScratchUVs.Add(FVector2D(
            static_cast<double>(Mesh.Elevation[Index]),
            static_cast<double>(Mesh.PatchId.Level)));

        ScratchColors.Add(EncodeDebugColor(
            DebugMode, Mesh.Elevation[Index], MaxElevation, MaxDepth,
            Mesh.PatchId, Index, Resolution,
            Mesh.BiomeColor[Index], Mesh.BiomeIndex[Index],
            Mesh.TemperatureK[Index], Mesh.Humidity[Index]));
    }

    ScratchIndices = Mesh.Indices;

    // Section 0 is replaced wholesale. CreateMeshSection rather than
    // UpdateMeshSection because the vertex count changes between patches, which
    // the update path does not allow.
    Component->CreateMeshSection(
        0,
        ScratchVertices,
        ScratchIndices,
        ScratchNormals,
        ScratchUVs,
        ScratchColors,
        ScratchTangents,
        bWithCollision);

    Component->SetRelativeLocation(FVector(
        Mesh.PatchOriginMeters.X * MetersToCm,
        Mesh.PatchOriginMeters.Y * MetersToCm,
        Mesh.PatchOriginMeters.Z * MetersToCm));

    Component->SetCollisionEnabled(
        bWithCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);

    Component->SetVisibility(true);

    SlotHasCollision[Slot] = bWithCollision;
}

void UPlanetMeshBackend_ProceduralMesh::ReleaseSlot(int32 Slot)
{
    if (!Components.IsValidIndex(Slot) || !SlotInUse.IsValidIndex(Slot) || !SlotInUse[Slot])
    {
        return;
    }

    if (UProceduralMeshComponent* Component = Components[Slot])
    {
        // Hidden and stripped of collision, but not destroyed - the component
        // goes back in the pool. Destroying it would hand the garbage collector
        // steady work at streaming rates.
        Component->SetVisibility(false);
        Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Component->ClearAllMeshSections();
    }

    SlotInUse[Slot] = false;
    SlotHasCollision[Slot] = false;
    FreeSlots.Add(Slot);
}

int32 UPlanetMeshBackend_ProceduralMesh::GetActiveSlotCount() const
{
    int32 Count = 0;
    for (bool bInUse : SlotInUse)
    {
        Count += bInUse ? 1 : 0;
    }
    return Count;
}

int32 UPlanetMeshBackend_ProceduralMesh::GetPooledSlotCount() const
{
    return FreeSlots.Num();
}

int32 UPlanetMeshBackend_ProceduralMesh::GetCollisionSlotCount() const
{
    int32 Count = 0;
    for (int32 Index = 0; Index < SlotHasCollision.Num(); ++Index)
    {
        if (SlotInUse.IsValidIndex(Index) && SlotInUse[Index] && SlotHasCollision[Index])
        {
            ++Count;
        }
    }
    return Count;
}

void UPlanetMeshBackend_ProceduralMesh::ReleaseAll()
{
    for (UProceduralMeshComponent* Component : Components)
    {
        if (Component != nullptr)
        {
            Component->DestroyComponent();
        }
    }

    Components.Reset();
    SlotInUse.Reset();
    SlotHasCollision.Reset();
    FreeSlots.Reset();
}
