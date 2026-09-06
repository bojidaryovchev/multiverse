// Copyright Universe Project. All Rights Reserved.

#include "PlanetPatchMesh.h"

void FPlanetPatchMesh::Reset()
{
    PositionX.Reset();
    PositionY.Reset();
    PositionZ.Reset();
    NormalX.Reset();
    NormalY.Reset();
    NormalZ.Reset();
    Elevation.Reset();
    Indices.Reset();

    GridVertexCount = 0;
    BoundingRadiusMeters = 0.0;
    MinElevationMeters = 0.0;
    MaxElevationMeters = 0.0;
    GeometricErrorMeters = 0.0;
}

bool FPlanetPatchMesh::Validate(FString& OutError) const
{
    const int32 VertexCount = GetVertexCount();

    if (VertexCount == 0)
    {
        OutError = TEXT("patch has no vertices");
        return false;
    }

    if (PositionY.Num() != VertexCount || PositionZ.Num() != VertexCount
        || NormalX.Num() != VertexCount || NormalY.Num() != VertexCount || NormalZ.Num() != VertexCount
        || Elevation.Num() != VertexCount)
    {
        OutError = TEXT("vertex stream lengths disagree");
        return false;
    }

    if (Indices.Num() % 3 != 0)
    {
        OutError = TEXT("index count is not a multiple of three");
        return false;
    }

    for (int32 Index = 0; Index < VertexCount; ++Index)
    {
        const float Px = PositionX[Index];
        const float Py = PositionY[Index];
        const float Pz = PositionZ[Index];

        if (!FMath::IsFinite(Px) || !FMath::IsFinite(Py) || !FMath::IsFinite(Pz))
        {
            OutError = FString::Printf(TEXT("non-finite position at vertex %d"), Index);
            return false;
        }

        const float Nx = NormalX[Index];
        const float Ny = NormalY[Index];
        const float Nz = NormalZ[Index];

        if (!FMath::IsFinite(Nx) || !FMath::IsFinite(Ny) || !FMath::IsFinite(Nz))
        {
            OutError = FString::Printf(TEXT("non-finite normal at vertex %d"), Index);
            return false;
        }

        const double NormalLength = FMath::Sqrt(
            static_cast<double>(Nx) * Nx + static_cast<double>(Ny) * Ny + static_cast<double>(Nz) * Nz);

        // Float storage of a unit vector, so the tolerance allows for
        // round-tripping through single precision but nothing more.
        if (NormalLength < 0.99 || NormalLength > 1.01)
        {
            OutError = FString::Printf(TEXT("normal at vertex %d is not unit length (%.6f)"), Index, NormalLength);
            return false;
        }

        if (!FMath::IsFinite(Elevation[Index]))
        {
            OutError = FString::Printf(TEXT("non-finite elevation at vertex %d"), Index);
            return false;
        }
    }

    for (int32 Index = 0; Index < Indices.Num(); Index += 3)
    {
        const int32 A = Indices[Index];
        const int32 B = Indices[Index + 1];
        const int32 C = Indices[Index + 2];

        if (A < 0 || B < 0 || C < 0 || A >= VertexCount || B >= VertexCount || C >= VertexCount)
        {
            OutError = FString::Printf(TEXT("index out of range in triangle %d"), Index / 3);
            return false;
        }

        // A triangle referencing the same vertex twice has no area and would
        // produce a NaN when anything tries to derive a normal from it.
        if (A == B || B == C || A == C)
        {
            OutError = FString::Printf(TEXT("degenerate triangle %d"), Index / 3);
            return false;
        }
    }

    if (!FMath::IsFinite(BoundingRadiusMeters) || BoundingRadiusMeters <= 0.0)
    {
        OutError = TEXT("invalid bounding radius");
        return false;
    }

    if (!FMath::IsFinite(GeometricErrorMeters) || GeometricErrorMeters < 0.0)
    {
        OutError = TEXT("invalid geometric error");
        return false;
    }

    return true;
}

void FPlanetPatchMeshBuilder::Build(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FPlanetPatchId& PatchId,
    bool bGenerateSkirt,
    FPlanetPatchMesh& OutMesh)
{
    OutMesh.Reset();
    OutMesh.PatchId = PatchId;

    if (!PatchId.IsValid() || !Settings.IsValid())
    {
        return;
    }

    const int32 Resolution = Settings.PatchResolution;
    const int32 QuadsPerEdge = Resolution - 1;
    const int32 GridVertices = Resolution * Resolution;

    // The patch origin is the terrain surface at the patch centre. Anchoring on
    // the surface rather than the sea-level sphere keeps the local coordinates
    // as small as possible, which is the whole point of a patch-local origin.
    const FVector3d CentreDirection = PatchId.GetCentreDirection();
    OutMesh.PatchOriginMeters =
        FPlanetTerrain::GetSurfacePositionMeters(Planet, Settings, CentreDirection);

    // Normal sampling epsilon: a fraction of the spacing between vertices,
    // expressed as an angle. Tying it to vertex spacing means normals describe
    // the terrain at the scale this patch actually represents - a fixed epsilon
    // would sample sub-vertex detail at low LOD and produce noisy shading of
    // features the mesh cannot show.
    const double PatchAngularSize = 2.0 * PatchId.GetAngularRadius();
    const double VertexAngularSpacing = PatchAngularSize / static_cast<double>(QuadsPerEdge);
    const double NormalEpsilon = VertexAngularSpacing * 0.5;

    const int32 SkirtVertices = bGenerateSkirt ? (4 * Resolution) : 0;
    const int32 TotalVertices = GridVertices + SkirtVertices;

    OutMesh.PositionX.Reserve(TotalVertices);
    OutMesh.PositionY.Reserve(TotalVertices);
    OutMesh.PositionZ.Reserve(TotalVertices);
    OutMesh.NormalX.Reserve(TotalVertices);
    OutMesh.NormalY.Reserve(TotalVertices);
    OutMesh.NormalZ.Reserve(TotalVertices);
    OutMesh.Elevation.Reserve(TotalVertices);
    OutMesh.Indices.Reserve(QuadsPerEdge * QuadsPerEdge * 6 + (bGenerateSkirt ? QuadsPerEdge * 4 * 6 : 0));

    double MinElevation = 1.0e30;
    double MaxElevation = -1.0e30;
    double MaxDistanceSquared = 0.0;

    // Keep the world-space grid so geometric error can be measured against it.
    TArray<FVector3d> GridWorld;
    GridWorld.Reserve(GridVertices);

    // --- Main grid ---------------------------------------------------------
    const double InvQuads = 1.0 / static_cast<double>(QuadsPerEdge);

    for (int32 J = 0; J < Resolution; ++J)
    {
        for (int32 I = 0; I < Resolution; ++I)
        {
            // Dyadic because QuadsPerEdge is a power of two - the property the
            // exact seam guarantees depend on. At I == 0 and I == QuadsPerEdge
            // these are exactly 0.0 and 1.0, so a border vertex computes the
            // identical UV from either adjoining patch.
            const double LocalU = static_cast<double>(I) * InvQuads;
            const double LocalV = static_cast<double>(J) * InvQuads;

            const FVector3d Direction = PatchId.GetDirectionAt(LocalU, LocalV);

            const FPlanetTerrain::FTerrainSample Sample =
                FPlanetTerrain::SampleDetailed(Planet, Settings, Direction);

            const double Radius = Planet.RadiusMeters + Sample.ElevationMeters;
            const FVector3d World(Direction.X * Radius, Direction.Y * Radius, Direction.Z * Radius);
            GridWorld.Add(World);

            const FVector3d Local(
                World.X - OutMesh.PatchOriginMeters.X,
                World.Y - OutMesh.PatchOriginMeters.Y,
                World.Z - OutMesh.PatchOriginMeters.Z);

            OutMesh.PositionX.Add(static_cast<float>(Local.X));
            OutMesh.PositionY.Add(static_cast<float>(Local.Y));
            OutMesh.PositionZ.Add(static_cast<float>(Local.Z));

            const FVector3d Normal =
                FPlanetTerrain::GetSurfaceNormal(Planet, Settings, Direction, NormalEpsilon);

            OutMesh.NormalX.Add(static_cast<float>(Normal.X));
            OutMesh.NormalY.Add(static_cast<float>(Normal.Y));
            OutMesh.NormalZ.Add(static_cast<float>(Normal.Z));

            OutMesh.Elevation.Add(static_cast<float>(Sample.ElevationMeters));

            MinElevation = FMath::Min(MinElevation, Sample.ElevationMeters);
            MaxElevation = FMath::Max(MaxElevation, Sample.ElevationMeters);
            MaxDistanceSquared = FMath::Max(MaxDistanceSquared, Local.SizeSquared());
        }
    }

    OutMesh.GridVertexCount = GridVertices;

    // --- Triangles ---------------------------------------------------------
    // Counter-clockwise when viewed from outside the planet, so front faces
    // point outward with Unreal's default winding.
    for (int32 J = 0; J < QuadsPerEdge; ++J)
    {
        for (int32 I = 0; I < QuadsPerEdge; ++I)
        {
            const int32 BottomLeft = J * Resolution + I;
            const int32 BottomRight = BottomLeft + 1;
            const int32 TopLeft = BottomLeft + Resolution;
            const int32 TopRight = TopLeft + 1;

            OutMesh.Indices.Add(BottomLeft);
            OutMesh.Indices.Add(TopLeft);
            OutMesh.Indices.Add(BottomRight);

            OutMesh.Indices.Add(BottomRight);
            OutMesh.Indices.Add(TopLeft);
            OutMesh.Indices.Add(TopRight);
        }
    }

    // --- Geometric error ---------------------------------------------------
    // Measured, not estimated: sample the terrain at each quad centre and
    // compare against the flat interpolation the two triangles actually
    // produce there. This is what feeds screen-space LOD, and a size-based
    // estimate would over-subdivide flat ground and under-subdivide mountains.
    {
        double MaxError = 0.0;

        for (int32 J = 0; J < QuadsPerEdge; ++J)
        {
            for (int32 I = 0; I < QuadsPerEdge; ++I)
            {
                const int32 BottomLeft = J * Resolution + I;
                const int32 BottomRight = BottomLeft + 1;
                const int32 TopLeft = BottomLeft + Resolution;
                const int32 TopRight = TopLeft + 1;

                const FVector3d& A = GridWorld[BottomLeft];
                const FVector3d& B = GridWorld[BottomRight];
                const FVector3d& C = GridWorld[TopLeft];
                const FVector3d& D = GridWorld[TopRight];

                const FVector3d Interpolated(
                    (A.X + B.X + C.X + D.X) * 0.25,
                    (A.Y + B.Y + C.Y + D.Y) * 0.25,
                    (A.Z + B.Z + C.Z + D.Z) * 0.25);

                const double LocalU = (static_cast<double>(I) + 0.5) * InvQuads;
                const double LocalV = (static_cast<double>(J) + 0.5) * InvQuads;
                const FVector3d TrueDirection = PatchId.GetDirectionAt(LocalU, LocalV);
                const FVector3d TrueSurface =
                    FPlanetTerrain::GetSurfacePositionMeters(Planet, Settings, TrueDirection);

                const FVector3d Delta(
                    TrueSurface.X - Interpolated.X,
                    TrueSurface.Y - Interpolated.Y,
                    TrueSurface.Z - Interpolated.Z);

                MaxError = FMath::Max(MaxError, Delta.Size());
            }
        }

        OutMesh.GeometricErrorMeters = MaxError;
    }

    // --- Skirt -------------------------------------------------------------
    if (bGenerateSkirt)
    {
        // Depth proportional to patch size, so the skirt covers the gap a
        // coarser neighbour leaves at every level.
        const double PatchSizeMeters = PatchId.GetApproximateSizeMeters(Planet.RadiusMeters);
        const double SkirtDepth = PatchSizeMeters * FPlanetPatchMeshBuilder::SkirtDepthFraction;

        // Rim vertices in order: south edge, north edge, west edge, east edge.
        // Each duplicates a grid vertex, pushed inward along the radius, and is
        // stitched to its grid original with two triangles.
        struct FRimSpec
        {
            int32 Start;
            int32 Stride;
        };

        const FRimSpec Rims[4] = {
            { 0,                                    1 },           // south, v = 0
            { (Resolution - 1) * Resolution,        1 },           // north, v = 1
            { 0,                                    Resolution },  // west,  u = 0
            { Resolution - 1,                       Resolution },  // east,  u = 1
        };

        for (int32 RimIndex = 0; RimIndex < 4; ++RimIndex)
        {
            const FRimSpec& Rim = Rims[RimIndex];
            const int32 SkirtBase = OutMesh.PositionX.Num();

            for (int32 Step = 0; Step < Resolution; ++Step)
            {
                const int32 GridIndex = Rim.Start + Step * Rim.Stride;

                const FVector3d World = GridWorld[GridIndex];
                const double WorldLength = World.Size();
                const double InvLength = (WorldLength > 0.0) ? (1.0 / WorldLength) : 0.0;

                // Straight down along the radius, so the skirt is hidden by the
                // terrain rather than sticking out sideways.
                const FVector3d Dropped(
                    World.X - World.X * InvLength * SkirtDepth,
                    World.Y - World.Y * InvLength * SkirtDepth,
                    World.Z - World.Z * InvLength * SkirtDepth);

                const FVector3d Local(
                    Dropped.X - OutMesh.PatchOriginMeters.X,
                    Dropped.Y - OutMesh.PatchOriginMeters.Y,
                    Dropped.Z - OutMesh.PatchOriginMeters.Z);

                OutMesh.PositionX.Add(static_cast<float>(Local.X));
                OutMesh.PositionY.Add(static_cast<float>(Local.Y));
                OutMesh.PositionZ.Add(static_cast<float>(Local.Z));

                // Reuse the rim vertex's normal so the skirt shades like the
                // terrain it hangs from and stays invisible when it is seen.
                //
                // Copied into locals first, deliberately. Passing
                // OutMesh.NormalX[GridIndex] straight to Add hands it a
                // reference INTO the array being appended to, which dangles the
                // moment Add reallocates. TArray asserts on exactly this;
                // std::vector has the same undefined behaviour but happens not
                // to complain, which is why the standalone harness ran it
                // hundreds of times without noticing.
                const float RimNormalX = OutMesh.NormalX[GridIndex];
                const float RimNormalY = OutMesh.NormalY[GridIndex];
                const float RimNormalZ = OutMesh.NormalZ[GridIndex];
                const float RimElevation = OutMesh.Elevation[GridIndex];

                OutMesh.NormalX.Add(RimNormalX);
                OutMesh.NormalY.Add(RimNormalY);
                OutMesh.NormalZ.Add(RimNormalZ);
                OutMesh.Elevation.Add(RimElevation);

                MaxDistanceSquared = FMath::Max(MaxDistanceSquared, Local.SizeSquared());
            }

            // Stitch. Winding is flipped for two of the rims because the grid
            // edge runs the opposite way around the patch boundary; getting
            // this wrong leaves back-facing skirts that are invisible exactly
            // when they are needed.
            const bool bFlip = (RimIndex == 1 || RimIndex == 2);

            for (int32 Step = 0; Step + 1 < Resolution; ++Step)
            {
                const int32 GridA = Rim.Start + Step * Rim.Stride;
                const int32 GridB = Rim.Start + (Step + 1) * Rim.Stride;
                const int32 SkirtA = SkirtBase + Step;
                const int32 SkirtB = SkirtBase + Step + 1;

                if (bFlip)
                {
                    OutMesh.Indices.Add(GridA);
                    OutMesh.Indices.Add(SkirtB);
                    OutMesh.Indices.Add(SkirtA);

                    OutMesh.Indices.Add(GridA);
                    OutMesh.Indices.Add(GridB);
                    OutMesh.Indices.Add(SkirtB);
                }
                else
                {
                    OutMesh.Indices.Add(GridA);
                    OutMesh.Indices.Add(SkirtA);
                    OutMesh.Indices.Add(SkirtB);

                    OutMesh.Indices.Add(GridA);
                    OutMesh.Indices.Add(SkirtB);
                    OutMesh.Indices.Add(GridB);
                }
            }
        }
    }

    OutMesh.MinElevationMeters = MinElevation;
    OutMesh.MaxElevationMeters = MaxElevation;
    OutMesh.BoundingRadiusMeters = FMath::Sqrt(MaxDistanceSquared);

    // A patch of perfectly flat ground would otherwise report a zero radius and
    // be culled immediately.
    if (OutMesh.BoundingRadiusMeters <= 0.0)
    {
        OutMesh.BoundingRadiusMeters = 1.0;
    }
}
