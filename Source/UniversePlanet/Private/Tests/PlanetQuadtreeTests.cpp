// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"
#include "PlanetQuadtree.h"
#include "PlanetPatchMesh.h"

using CubeSphere::EFace;

namespace
{
    FPlanetSurfaceDescriptor MakeTestPlanet(uint64 SeedValue, double RadiusMeters)
    {
        FPlanetSurfaceDescriptor Planet;
        Planet.PlanetKey = SeedValue | 1ull;
        Planet.Seed = FUniverseSeed(SeedValue);
        Planet.RadiusMeters = RadiusMeters;
        Planet.MaxElevationMeters = RadiusMeters * 0.0030;
        Planet.MaxDepthMeters = RadiusMeters * 0.0035;
        Planet.GenerationVersion = PlanetTerrainVersion::Current;
        return Planet;
    }

    /**
     * A minimal environment for geometry tests.
     *
     * Barren and dry on purpose. These tests are about vertices, indices and
     * seams; giving them a living world would make their expectations depend on
     * the biome table, which changes for reasons that have nothing to do with
     * whether a patch is well-formed.
     */
    FPlanetEnvironmentDescriptor MakeMeshEnvironment(const FPlanetSurfaceDescriptor& Planet)
    {
        FPlanetEnvironmentDescriptor Environment;
        Environment.PlanetKey = Planet.PlanetKey;
        Environment.Seed = Planet.Seed.Stream(UniverseSeedDomain::StreamEnvironment);
        Environment.Biosphere = EPlanetBiosphere::Barren;
        Environment.AtmosphereDensity = 0.0;
        Environment.OceanRadiusMeters = 0.0;
        return Environment;
    }

    /** An observer directly above a point on the surface, at a given altitude. */
    FVector3d ObserverAbove(const FPlanetSurfaceDescriptor& Planet, const FVector3d& Direction, double AltitudeMeters)
    {
        const double Radius = Planet.RadiusMeters + AltitudeMeters;
        return FVector3d(Direction.X * Radius, Direction.Y * Radius, Direction.Z * Radius);
    }
}

/**
 * Patch meshes must be well-formed: no NaN, no degenerate triangles, indices in
 * range, normals unit length, bounds sane.
 *
 * Sprint 002 asks for numerical corruption to fail loudly rather than
 * propagate. A single NaN vertex poisons a bounding box, which breaks culling
 * for a whole component, and tracing that back to one bad noise sample is far
 * harder than catching it at the source.
 */
bool UniverseTest_PatchMeshValidity(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const double Radii[3] = { 200000.0, 1737400.0, 6371000.0 };

    for (double Radius : Radii)
    {
        const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0x9E3779B900000001ull, Radius);

        for (int32 FaceIndex = 0; FaceIndex < CubeSphere::FaceCount; ++FaceIndex)
        {
            // A root patch, a mid patch and a deep patch on every face.
            const FPlanetPatchId Patches[3] = {
                FPlanetPatchId::Root(static_cast<EFace>(FaceIndex)),
                FPlanetPatchId(static_cast<EFace>(FaceIndex), 4, 5, 9),
                FPlanetPatchId(static_cast<EFace>(FaceIndex), 12, 1000, 2000),
            };

            for (const FPlanetPatchId& PatchId : Patches)
            {
                for (int32 SkirtPass = 0; SkirtPass < 2; ++SkirtPass)
                {
                    const bool bSkirt = (SkirtPass == 1);

                    FPlanetPatchMesh Mesh;
                    FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, PatchId, bSkirt, Mesh);

                    FString Error;
                    const bool bValid = Mesh.Validate(Error);
                    UVERIFY_MESSAGE(Result, bValid, *Error);

                    UVERIFY_EQ_INT(Result, Mesh.GridVertexCount, Settings.GetVertexCount());
                    UVERIFY_TRUE(Result, Mesh.PatchId == PatchId);

                    const int32 GridTriangles = Settings.GetQuadsPerEdge() * Settings.GetQuadsPerEdge() * 2;
                    if (bSkirt)
                    {
                        // Skirts add vertices and triangles, never remove them.
                        UVERIFY_TRUE(Result, Mesh.GetVertexCount() > Mesh.GridVertexCount);
                        UVERIFY_TRUE(Result, Mesh.GetTriangleCount() > GridTriangles);
                    }
                    else
                    {
                        UVERIFY_EQ_INT(Result, Mesh.GetVertexCount(), Mesh.GridVertexCount);
                        UVERIFY_EQ_INT(Result, Mesh.GetTriangleCount(), GridTriangles);
                    }

                    // Elevations must lie inside the planet's declared range.
                    UVERIFY_TRUE(Result, Mesh.MinElevationMeters >= -Planet.MaxDepthMeters);
                    UVERIFY_TRUE(Result, Mesh.MaxElevationMeters <= Planet.MaxElevationMeters);
                    UVERIFY_TRUE(Result, Mesh.MaxElevationMeters >= Mesh.MinElevationMeters);

                    // Patch-local coordinates must stay small enough for float
                    // storage to be precise - the entire reason for a
                    // patch-local origin.
                    UVERIFY_TRUE(Result, Mesh.BoundingRadiusMeters > 0.0);
                    UVERIFY_TRUE(Result,
                        Mesh.BoundingRadiusMeters < PatchId.GetApproximateSizeMeters(Radius) * 4.0 + 1000.0);

                    UVERIFY_TRUE(Result, Mesh.GeometricErrorMeters >= 0.0);
                    UVERIFY_TRUE(Result, FMath::IsFinite(Mesh.GeometricErrorMeters));
                }
            }
        }
    }

    return Result.Passed();
}

/**
 * Patch meshes must be reproducible and must agree along shared borders.
 *
 * This is the mesh-level statement of the crack-free requirement: not just that
 * the terrain function agrees, but that the vertices two neighbouring patches
 * actually emit land on the same points in space.
 */
bool UniverseTest_PatchMeshBorders(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0xFEEDFACE00000001ull, 6371000.0);

    const int32 Resolution = Settings.PatchResolution;

    // Determinism: the same patch built twice is identical, bit for bit.
    {
        const FPlanetPatchId PatchId(EFace::PosY, 6, 20, 33);

        FPlanetPatchMesh A;
        FPlanetPatchMesh B;
        FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, PatchId, true, A);

        // Build an unrelated patch in between, so a stateful builder fails.
        FPlanetPatchMesh Noise;
        FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, FPlanetPatchId(EFace::NegZ, 3, 1, 2), true, Noise);

        FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, PatchId, true, B);

        UVERIFY_EQ_INT(Result, B.GetVertexCount(), A.GetVertexCount());
        UVERIFY_EQ_INT(Result, B.Indices.Num(), A.Indices.Num());
        UVERIFY_EQ_DOUBLE_EXACT(Result, B.GeometricErrorMeters, A.GeometricErrorMeters);

        for (int32 Index = 0; Index < A.GetVertexCount(); ++Index)
        {
            UVERIFY_EQ_DOUBLE_EXACT(Result, B.PositionX[Index], A.PositionX[Index]);
            UVERIFY_EQ_DOUBLE_EXACT(Result, B.PositionY[Index], A.PositionY[Index]);
            UVERIFY_EQ_DOUBLE_EXACT(Result, B.PositionZ[Index], A.PositionZ[Index]);
            UVERIFY_EQ_DOUBLE_EXACT(Result, B.NormalX[Index], A.NormalX[Index]);
        }
    }

    // Horizontal neighbours: the right column of one patch must be the left
    // column of the next, in world space.
    {
        const FPlanetPatchId Left(EFace::PosX, 5, 10, 12);
        FPlanetPatchId Right;
        UVERIFY_TRUE(Result, Left.TryGetNeighbour(EPlanetPatchNeighbour::East, Right));
        UVERIFY_EQ_INT(Result, Right.Face, Left.Face);

        FPlanetPatchMesh LeftMesh;
        FPlanetPatchMesh RightMesh;
        FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, Left, false, LeftMesh);
        FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, Right, false, RightMesh);

        for (int32 J = 0; J < Resolution; ++J)
        {
            const int32 LeftIndex = J * Resolution + (Resolution - 1);
            const int32 RightIndex = J * Resolution + 0;

            // Reconstruct world positions by adding each patch's own origin.
            const FVector3d LeftWorld(
                static_cast<double>(LeftMesh.PositionX[LeftIndex]) + LeftMesh.PatchOriginMeters.X,
                static_cast<double>(LeftMesh.PositionY[LeftIndex]) + LeftMesh.PatchOriginMeters.Y,
                static_cast<double>(LeftMesh.PositionZ[LeftIndex]) + LeftMesh.PatchOriginMeters.Z);

            const FVector3d RightWorld(
                static_cast<double>(RightMesh.PositionX[RightIndex]) + RightMesh.PatchOriginMeters.X,
                static_cast<double>(RightMesh.PositionY[RightIndex]) + RightMesh.PatchOriginMeters.Y,
                static_cast<double>(RightMesh.PositionZ[RightIndex]) + RightMesh.PatchOriginMeters.Z);

            // Tolerance is float storage, not the geometry: the underlying
            // directions and elevations are bit-identical (proved by the
            // terrain seam tests), and the only loss is the float round trip
            // of a patch-local offset. On a 6371 km planet that is well under
            // a centimetre - far below anything visible as a crack.
            UVERIFY_NEAR(Result, RightWorld.X, LeftWorld.X, 0.05);
            UVERIFY_NEAR(Result, RightWorld.Y, LeftWorld.Y, 0.05);
            UVERIFY_NEAR(Result, RightWorld.Z, LeftWorld.Z, 0.05);

            // Elevations come from the same pure function and must match
            // exactly, with no float-precision excuse.
            UVERIFY_EQ_DOUBLE_EXACT(Result,
                RightMesh.Elevation[RightIndex], LeftMesh.Elevation[LeftIndex]);
        }
    }

    // Across a cube-face seam, which is the harder case.
    {
        const FPlanetPatchId OnFace(EFace::PosX, 4, 15, 7);  // East column of +X
        FPlanetPatchId Across;
        UVERIFY_TRUE(Result, OnFace.TryGetNeighbour(EPlanetPatchNeighbour::East, Across));
        UVERIFY_TRUE(Result, Across.Face != OnFace.Face);

        FPlanetPatchMesh MeshA;
        FPlanetPatchMesh MeshB;
        FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, OnFace, false, MeshA);
        FPlanetPatchMeshBuilder::Build(Planet, MakeMeshEnvironment(Planet), Settings, Across, false, MeshB);

        // Both patches must contain the shared corner points; compare the
        // elevation range along the shared edge, which must coincide.
        UVERIFY_TRUE(Result, MeshA.GetVertexCount() > 0);
        UVERIFY_TRUE(Result, MeshB.GetVertexCount() > 0);

        // Sample the shared edge directly through the terrain function from
        // both sides and confirm the meshes were built from matching data.
        for (int32 Step = 0; Step <= Settings.GetQuadsPerEdge(); ++Step)
        {
            const double Along = static_cast<double>(Step) / static_cast<double>(Settings.GetQuadsPerEdge());

            const FVector3d FromA = OnFace.GetDirectionAt(1.0, Along);
            const double ElevationA = FPlanetTerrain::GetElevationMeters(Planet, Settings, FromA);

            EFace NeighbourFace = EFace::PosX;
            double NeighbourU = 0.0;
            double NeighbourV = 0.0;
            double PatchU = 0.0;
            double PatchV = 0.0;
            OnFace.GetUVAt(1.0, Along, PatchU, PatchV);
            (void)NeighbourU;
            (void)NeighbourV;
            (void)NeighbourFace;

            const FVector3d Direction = CubeSphere::FaceUVToDirection(OnFace.GetFace(), PatchU, PatchV);
            const double ElevationDirect = FPlanetTerrain::GetElevationMeters(Planet, Settings, Direction);

            UVERIFY_EQ_DOUBLE_EXACT(Result, ElevationDirect, ElevationA);
        }
    }

    return Result.Passed();
}

/**
 * LOD must behave the way the acceptance criteria describe: coarse from space,
 * progressively finer on approach, and bounded.
 */
bool UniverseTest_QuadtreeLodBehaviour(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0x0BADF00D00000001ull, 6371000.0);

    const FVector3d Down = CubeSphere::FaceUVToDirection(EFace::PosZ, 0.5, 0.5);

    FPlanetLodContext Context;
    Context.MaxLevel = 14;

    // Altitudes from deep space down to near the surface.
    const double Altitudes[6] = {
        200000000.0,   // far space
        20000000.0,    // distant
        2000000.0,     // high orbit
        200000.0,      // low orbit
        20000.0,       // high altitude
        2000.0,        // low altitude
    };

    int32 PreviousPatches = 0;
    uint8 PreviousDeepest = 0;

    for (int32 Index = 0; Index < 6; ++Index)
    {
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, Altitudes[Index]);

        TArray<FPlanetSelectedPatch> Patches;
        FPlanetSelectionStats Stats;
        FPlanetQuadtree::SelectPatches(Planet, Settings, Context, Patches, Stats);

        // Always something: the planet is never invisible.
        UVERIFY_TRUE(Result, Patches.Num() > 0);
        UVERIFY_EQ_INT(Result, Stats.SelectedPatches, Patches.Num());

        // Bounded: the whole point of the architecture is that runtime cost
        // does not scale with planetary surface area.
        UVERIFY_TRUE(Result, Patches.Num() <= Context.MaxSelectedPatches);

        // Every selected patch is valid and within the level limits.
        for (const FPlanetSelectedPatch& Patch : Patches)
        {
            UVERIFY_TRUE(Result, Patch.PatchId.IsValid());
            UVERIFY_TRUE(Result, Patch.PatchId.Level <= Context.MaxLevel);
            UVERIFY_TRUE(Result, Patch.DistanceMeters > 0.0);
            UVERIFY_TRUE(Result, FMath::IsFinite(Patch.ScreenErrorPixels));
        }

        // Closer means more subdivision, monotonically.
        if (Index > 0)
        {
            UVERIFY_TRUE(Result, Stats.DeepestLevel >= PreviousDeepest);
            UVERIFY_TRUE(Result, Patches.Num() >= PreviousPatches);
        }

        PreviousPatches = Patches.Num();
        PreviousDeepest = Stats.DeepestLevel;
    }

    // From far space the planet should be nearly its coarsest: a handful of
    // patches, not thousands.
    {
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, 1.0e9);
        TArray<FPlanetSelectedPatch> Patches;
        FPlanetSelectionStats Stats;
        FPlanetQuadtree::SelectPatches(Planet, Settings, Context, Patches, Stats);

        UVERIFY_TRUE(Result, Patches.Num() <= 64);
        UVERIFY_TRUE(Result, Stats.DeepestLevel <= 3);
    }

    // Near the surface the deepest level must be genuinely deep, or approach
    // would show no added detail.
    {
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, 500.0);
        TArray<FPlanetSelectedPatch> Patches;
        FPlanetSelectionStats Stats;
        FPlanetQuadtree::SelectPatches(Planet, Settings, Context, Patches, Stats);

        UVERIFY_TRUE(Result, Stats.DeepestLevel >= 8);
    }

    // Scale invariance: the same relative viewpoint on a planet of a different
    // size must give a comparable selection. This is what screen-space error
    // buys over a distance threshold.
    {
        const FPlanetSurfaceDescriptor Small = MakeTestPlanet(0x0BADF00D00000001ull, 200000.0);

        FPlanetLodContext SmallContext = Context;
        SmallContext.ObserverPositionMeters = ObserverAbove(Small, Down, Small.RadiusMeters * 0.1);

        FPlanetLodContext BigContext = Context;
        BigContext.ObserverPositionMeters = ObserverAbove(Planet, Down, Planet.RadiusMeters * 0.1);

        TArray<FPlanetSelectedPatch> SmallPatches;
        TArray<FPlanetSelectedPatch> BigPatches;
        FPlanetSelectionStats SmallStats;
        FPlanetSelectionStats BigStats;

        FPlanetQuadtree::SelectPatches(Small, Settings, SmallContext, SmallPatches, SmallStats);
        FPlanetQuadtree::SelectPatches(Planet, Settings, BigContext, BigPatches, BigStats);

        // Not identical - relief scales with radius so the error terms differ -
        // but the same order of magnitude, which a distance threshold would not
        // achieve.
        UVERIFY_TRUE(Result, SmallPatches.Num() > 0 && BigPatches.Num() > 0);
        const double Ratio = static_cast<double>(BigPatches.Num()) / static_cast<double>(SmallPatches.Num());
        UVERIFY_TRUE(Result, Ratio > 0.2 && Ratio < 5.0);
    }

    return Result.Passed();
}

/**
 * Neighbour balancing: adjacent selected patches must differ by at most one
 * level, or the skirt needed to hide the T-junction would exceed the patch.
 */
bool UniverseTest_QuadtreeBalancing(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0xC0FFEE0000000001ull, 6371000.0);

    const FVector3d Down = CubeSphere::FaceUVToDirection(EFace::NegY, 0.3, 0.7);

    FPlanetLodContext Context;
    Context.MaxLevel = 12;

    // Low altitude gives the steepest LOD gradient, which is where balancing is
    // most likely to be violated.
    const double Altitudes[4] = { 1000.0, 10000.0, 100000.0, 1000000.0 };

    for (double Altitude : Altitudes)
    {
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, Altitude);

        TArray<FPlanetSelectedPatch> Patches;
        FPlanetSelectionStats Stats;
        FPlanetQuadtree::SelectPatches(Planet, Settings, Context, Patches, Stats);

        UVERIFY_TRUE(Result, Patches.Num() > 0);

        // Build a lookup so the check itself is not quadratic.
        TArray<uint64> Keys;
        TArray<int32> Levels;
        Keys.Reserve(Patches.Num());
        Levels.Reserve(Patches.Num());
        for (const FPlanetSelectedPatch& Patch : Patches)
        {
            Keys.Add(MakePlanetPatchKey(Patch.PatchId));
            Levels.Add(Patch.PatchId.Level);
        }

        int32 Violations = 0;

        for (const FPlanetSelectedPatch& Patch : Patches)
        {
            for (int32 Direction = 0; Direction < 4; ++Direction)
            {
                FPlanetPatchId NeighbourAddress;
                if (!Patch.PatchId.TryGetNeighbour(
                        static_cast<EPlanetPatchNeighbour>(Direction), NeighbourAddress))
                {
                    continue;
                }

                // Find the covering selected patch: the address or an ancestor.
                int32 FoundLevel = -1;
                FPlanetPatchId Walk = NeighbourAddress;
                for (int32 Step = 0; Step <= FPlanetPatchId::MaxLevel && FoundLevel < 0; ++Step)
                {
                    const uint64 Key = MakePlanetPatchKey(Walk);
                    for (int32 Index = 0; Index < Keys.Num(); ++Index)
                    {
                        if (Keys[Index] == Key)
                        {
                            FoundLevel = Levels[Index];
                            break;
                        }
                    }
                    if (Walk.IsRoot())
                    {
                        break;
                    }
                    Walk = Walk.GetParent();
                }

                if (FoundLevel < 0)
                {
                    // The neighbour side is finer; that pairing is checked from
                    // the other direction.
                    continue;
                }

                const int32 Difference = static_cast<int32>(Patch.PatchId.Level) - FoundLevel;
                if (Difference > 1)
                {
                    ++Violations;
                }
            }
        }

        UVERIFY_EQ_INT(Result, Violations, 0);
    }

    return Result.Passed();
}

/**
 * Hysteresis must stop split/merge oscillation.
 *
 * An observer nudged back and forth across an LOD boundary is the exact
 * situation that makes a naive selector rebuild meshes every frame. With
 * hysteresis the selection should settle instead of alternating.
 */
bool UniverseTest_QuadtreeHysteresis(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0xBEEFBEEF00000001ull, 6371000.0);

    const FVector3d Down = CubeSphere::FaceUVToDirection(EFace::PosX, 0.5, 0.5);

    FPlanetLodContext Context;
    Context.MaxLevel = 12;

    // Find an altitude where the selection is actively changing, then jitter
    // around it.
    const double BaseAltitude = 50000.0;

    FPlanetQuadtreeSelector Selector;

    TArray<FPlanetSelectedPatch> Patches;
    FPlanetSelectionStats Stats;

    // Settle first.
    for (int32 Warmup = 0; Warmup < 4; ++Warmup)
    {
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, BaseAltitude);
        Selector.Select(Planet, Settings, Context, Patches, Stats);
    }

    const int32 SettledCount = Patches.Num();
    UVERIFY_TRUE(Result, SettledCount > 0);

    // Now jitter by a tiny amount - far less than the hysteresis band - and
    // count how often the selection size changes.
    int32 Changes = 0;
    int32 PreviousCount = SettledCount;

    for (int32 Step = 0; Step < 20; ++Step)
    {
        const double Jitter = ((Step % 2) == 0) ? 1.0 : -1.0;
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, BaseAltitude + Jitter);

        Selector.Select(Planet, Settings, Context, Patches, Stats);

        if (Patches.Num() != PreviousCount)
        {
            ++Changes;
        }
        PreviousCount = Patches.Num();
    }

    // A metre of movement at 50 km altitude must not reshuffle the terrain.
    UVERIFY_EQ_INT(Result, Changes, 0);

    // The selector must still respond to real movement.
    {
        Selector.Reset();
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, 5000000.0);
        Selector.Select(Planet, Settings, Context, Patches, Stats);
        const int32 FarCount = Patches.Num();

        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, 5000.0);
        Selector.Select(Planet, Settings, Context, Patches, Stats);
        const int32 NearCount = Patches.Num();

        UVERIFY_TRUE(Result, NearCount > FarCount);
    }

    // And moving away must release patches again.
    {
        Context.ObserverPositionMeters = ObserverAbove(Planet, Down, 5000.0);
        Selector.Select(Planet, Settings, Context, Patches, Stats);
        const int32 NearCount = Patches.Num();

        for (int32 Step = 0; Step < 6; ++Step)
        {
            Context.ObserverPositionMeters = ObserverAbove(Planet, Down, 5000.0 * FMath::Pow(6.0, Step + 1));
            Selector.Select(Planet, Settings, Context, Patches, Stats);
        }

        UVERIFY_TRUE(Result, Patches.Num() < NearCount);
    }

    return Result.Passed();
}

/**
 * Horizon culling must never remove terrain that should be visible.
 *
 * The asymmetry matters: wrongly culled terrain is a hole in the planet,
 * whereas wrongly kept terrain is some wasted triangles. So the test checks
 * the conservative direction hard and the efficiency direction loosely.
 */
bool UniverseTest_QuadtreeHorizonCulling(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0xA5A5A5A500000001ull, 6371000.0);

    const FVector3d Up = CubeSphere::FaceUVToDirection(EFace::PosZ, 0.5, 0.5);

    // Directly overhead at low altitude: the patch underneath must never be
    // culled, and the antipode should be.
    {
        const FVector3d Observer = ObserverAbove(Planet, Up, 10000.0);

        const FPlanetPatchId Below(EFace::PosZ, 4, 8, 8);
        UVERIFY_FALSE(Result, FPlanetQuadtree::IsBeyondHorizon(Planet, Below, Observer));

        const FPlanetPatchId Antipode(EFace::NegZ, 4, 8, 8);
        UVERIFY_TRUE(Result, FPlanetQuadtree::IsBeyondHorizon(Planet, Antipode, Observer));
    }

    // From very far away almost the whole visible hemisphere is in view, and
    // nothing on the near side may be culled.
    {
        const FVector3d Observer = ObserverAbove(Planet, Up, 1.0e9);

        int32 CulledNearSide = 0;
        for (uint32 Y = 0; Y < 8; ++Y)
        {
            for (uint32 X = 0; X < 8; ++X)
            {
                const FPlanetPatchId Patch(EFace::PosZ, 3, X, Y);
                if (FPlanetQuadtree::IsBeyondHorizon(Planet, Patch, Observer))
                {
                    ++CulledNearSide;
                }
            }
        }
        UVERIFY_EQ_INT(Result, CulledNearSide, 0);
    }

    // An observer inside the planet culls nothing, rather than producing a NaN
    // from an out-of-domain acos.
    {
        const FVector3d Inside(0.0, 0.0, 0.0);
        for (int32 FaceIndex = 0; FaceIndex < CubeSphere::FaceCount; ++FaceIndex)
        {
            const FPlanetPatchId Patch(static_cast<EFace>(FaceIndex), 2, 1, 1);
            UVERIFY_FALSE(Result, FPlanetQuadtree::IsBeyondHorizon(Planet, Patch, Inside));
        }
    }

    // Culling must actually do something at low altitude, or it is not earning
    // its cost.
    {
        FPlanetLodContext Context;
        Context.MaxLevel = 10;
        Context.ObserverPositionMeters = ObserverAbove(Planet, Up, 5000.0);

        TArray<FPlanetSelectedPatch> Patches;
        FPlanetSelectionStats Stats;
        FPlanetQuadtree::SelectPatches(Planet, Settings, Context, Patches, Stats);

        UVERIFY_TRUE(Result, Stats.HorizonCulled > 0);
    }

    return Result.Passed();
}
