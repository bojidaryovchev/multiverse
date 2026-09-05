// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"
#include "PlanetTerrain.h"
#include "PlanetNoise.h"
#include "PlanetPatchId.h"
#include "StarSystemGenerator.h"

using CubeSphere::EFace;

namespace
{
    constexpr int32 FaceCount = CubeSphere::FaceCount;

    EFace FaceAt(int32 Index) { return static_cast<EFace>(Index); }

    /** A planet built directly, so tests do not depend on what the star
     *  generator happens to produce for a given seed. */
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

    bool ExactlyEqual(const FVector3d& A, const FVector3d& B)
    {
        return A.X == B.X && A.Y == B.Y && A.Z == B.Z;
    }
}

/**
 * The noise primitives must be reproducible, bounded and free of lattice
 * artefacts.
 *
 * The zero-at-lattice-points property is worth pinning: gradient noise is
 * exactly zero at every integer lattice point by construction, and a variant
 * that is not zero there is value noise rather than gradient noise and will
 * look quite different.
 */
bool UniverseTest_PlanetNoiseBasics(FUniverseTestResult& Result)
{
    constexpr uint64 Seed = 0x1234567800000001ull;

    // Purity.
    for (int32 Index = 0; Index < 200; ++Index)
    {
        const double T = static_cast<double>(Index) * 0.137;
        const FVector3d Point(T, T * 0.7 - 3.1, T * -0.31 + 11.0);
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            PlanetNoise::Noise3D(Point, Seed), PlanetNoise::Noise3D(Point, Seed));
    }

    // Gradient noise vanishes at integer lattice points.
    for (int32 X = -3; X <= 3; ++X)
    {
        for (int32 Y = -3; Y <= 3; ++Y)
        {
            for (int32 Z = -3; Z <= 3; ++Z)
            {
                const double Value = PlanetNoise::Noise3D(
                    FVector3d(static_cast<double>(X), static_cast<double>(Y), static_cast<double>(Z)), Seed);
                UVERIFY_EQ_DOUBLE_EXACT(Result, Value, 0.0);
            }
        }
    }

    // Bounded, varied, and roughly zero-mean.
    {
        double Sum = 0.0;
        double MinValue = 1.0e30;
        double MaxValue = -1.0e30;
        constexpr int32 Samples = 20000;

        for (int32 Index = 0; Index < Samples; ++Index)
        {
            const double T = static_cast<double>(Index);
            const FVector3d Point(T * 0.0137, T * 0.0219 + 5.0, T * -0.0071 - 2.0);
            const double Value = PlanetNoise::Noise3D(Point, Seed);

            UVERIFY_TRUE(Result, Value >= -1.2 && Value <= 1.2);
            UVERIFY_TRUE(Result, FMath::IsFinite(Value));

            Sum += Value;
            MinValue = FMath::Min(MinValue, Value);
            MaxValue = FMath::Max(MaxValue, Value);
        }

        UVERIFY_NEAR(Result, Sum / static_cast<double>(Samples), 0.0, 0.05);

        // A generator stuck near zero would pass the mean test while producing
        // a featureless planet.
        UVERIFY_TRUE(Result, MaxValue > 0.3);
        UVERIFY_TRUE(Result, MinValue < -0.3);
    }

    // Different seeds give different fields.
    {
        int32 Identical = 0;
        for (int32 Index = 0; Index < 100; ++Index)
        {
            const double T = static_cast<double>(Index) * 0.31;
            const FVector3d Point(T, T + 1.0, T - 1.0);
            if (PlanetNoise::Noise3D(Point, Seed) == PlanetNoise::Noise3D(Point, Seed ^ 0xFFFFull))
            {
                ++Identical;
            }
        }
        UVERIFY_TRUE(Result, Identical < 5);
    }

    // FBM and ridged FBM stay in range and stay finite.
    {
        constexpr int32 Samples = 4000;
        for (int32 Index = 0; Index < Samples; ++Index)
        {
            const double T = static_cast<double>(Index) * 0.017;
            const FVector3d Point(T, T * 1.3 - 2.0, T * -0.6 + 4.0);

            const double Fbm = PlanetNoise::FBM(Point, Seed, 6, 1.5);
            const double Ridged = PlanetNoise::RidgedFBM(Point, Seed, 6, 1.5);

            UVERIFY_TRUE(Result, FMath::IsFinite(Fbm) && Fbm >= -1.05 && Fbm <= 1.05);
            UVERIFY_TRUE(Result, FMath::IsFinite(Ridged) && Ridged >= -1.05 && Ridged <= 1.05);
        }
    }

    return Result.Passed();
}

/**
 * Terrain must be a pure function of position: same direction, same height,
 * every time and in any order.
 *
 * The interleaving matters. A generator that memoises, or that carries any
 * state between calls, passes a naive "sample twice" test and fails this one.
 */
bool UniverseTest_TerrainDeterminism(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    UVERIFY_TRUE(Result, Settings.IsValid());

    const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0xABCDEF0123456789ull, 6371000.0);

    // Collect a set of directions and their heights.
    constexpr int32 Samples = 400;
    TArray<FVector3d> Directions;
    TArray<double> Heights;
    Directions.Reserve(Samples);
    Heights.Reserve(Samples);

    for (int32 Index = 0; Index < Samples; ++Index)
    {
        const int32 FaceIndex = Index % FaceCount;
        const double U = static_cast<double>((Index * 37) % 101) / 100.0;
        const double V = static_cast<double>((Index * 61) % 103) / 102.0;

        const FVector3d Direction = CubeSphere::FaceUVToDirection(FaceAt(FaceIndex), U, V);
        Directions.Add(Direction);
        Heights.Add(FPlanetTerrain::GetElevationMeters(Planet, Settings, Direction));
    }

    // Re-sample in reverse, with unrelated work interleaved.
    for (int32 Index = Samples - 1; Index >= 0; --Index)
    {
        // Deliberate interference: sample a different planet in between.
        const FPlanetSurfaceDescriptor Other = MakeTestPlanet(static_cast<uint64>(Index) + 7ull, 1000000.0);
        (void)FPlanetTerrain::GetElevationMeters(Other, Settings, Directions[Index]);

        const double Again = FPlanetTerrain::GetElevationMeters(Planet, Settings, Directions[Index]);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Again, Heights[Index]);
    }

    // Different planet seeds must give different terrain.
    {
        const FPlanetSurfaceDescriptor A = MakeTestPlanet(1111, 6371000.0);
        const FPlanetSurfaceDescriptor B = MakeTestPlanet(2222, 6371000.0);

        int32 Differences = 0;
        for (int32 Index = 0; Index < Samples; ++Index)
        {
            if (FPlanetTerrain::GetElevationMeters(A, Settings, Directions[Index])
                != FPlanetTerrain::GetElevationMeters(B, Settings, Directions[Index]))
            {
                ++Differences;
            }
        }
        UVERIFY_EQ_INT(Result, Differences, Samples);
    }

    // The generation version participates in identity.
    {
        FPlanetSurfaceDescriptor V1 = MakeTestPlanet(3333, 6371000.0);
        FPlanetSurfaceDescriptor V2 = V1;
        V2.GenerationVersion = V1.GenerationVersion + 1;
        UVERIFY_TRUE(Result, V1.GetContentHash() != V2.GetContentHash());
    }

    return Result.Passed();
}

/**
 * Elevation must stay inside the planet's declared range, at every radius.
 *
 * The bound is what the renderer, the LOD metric and the future collision
 * system all rely on for bounding volumes. A single vertex outside it would
 * poke through a bounding sphere and be culled incorrectly.
 */
bool UniverseTest_TerrainBounds(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;

    // Several radii: a moon, an Earth, and a large body. The architecture must
    // not be tuned around one test planet.
    const double Radii[4] = { 100000.0, 1737400.0, 6371000.0, 25000000.0 };

    for (double Radius : Radii)
    {
        const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0x5A5A5A5A00000001ull, Radius);

        double LowestSeen = 1.0e30;
        double HighestSeen = -1.0e30;

        for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
        {
            constexpr int32 Steps = 21;
            for (int32 J = 0; J <= Steps; ++J)
            {
                for (int32 I = 0; I <= Steps; ++I)
                {
                    const double U = static_cast<double>(I) / static_cast<double>(Steps);
                    const double V = static_cast<double>(J) / static_cast<double>(Steps);

                    const FVector3d Direction = CubeSphere::FaceUVToDirection(FaceAt(FaceIndex), U, V);
                    const double Elevation = FPlanetTerrain::GetElevationMeters(Planet, Settings, Direction);

                    UVERIFY_TRUE(Result, FMath::IsFinite(Elevation));
                    UVERIFY_TRUE(Result, Elevation <= Planet.MaxElevationMeters);
                    UVERIFY_TRUE(Result, Elevation >= -Planet.MaxDepthMeters);

                    // The surface must never invert through the planet centre.
                    const FVector3d Surface =
                        FPlanetTerrain::GetSurfacePositionMeters(Planet, Settings, Direction);
                    const double SurfaceRadius = Surface.Size();
                    UVERIFY_TRUE(Result, SurfaceRadius > 0.0);
                    UVERIFY_TRUE(Result, SurfaceRadius >= Planet.GetMinRadiusMeters() - 1.0);
                    UVERIFY_TRUE(Result, SurfaceRadius <= Planet.GetMaxRadiusMeters() + 1.0);

                    LowestSeen = FMath::Min(LowestSeen, Elevation);
                    HighestSeen = FMath::Max(HighestSeen, Elevation);
                }
            }
        }

        // The terrain must actually use its range - a planet that is a smooth
        // sphere would satisfy every bound above and be useless.
        const double Range = Planet.GetElevationRangeMeters();
        UVERIFY_TRUE(Result, (HighestSeen - LowestSeen) > Range * 0.15);
        UVERIFY_TRUE(Result, HighestSeen > 0.0);
        UVERIFY_TRUE(Result, LowestSeen < 0.0);
    }

    return Result.Passed();
}

/**
 * Terrain continuity across every patch border and every cube-face seam.
 *
 * This is the requirement Sprint 002 calls non-negotiable, taken all the way
 * through the terrain function rather than stopping at the direction. Because
 * the shared directions are bit-identical (CubeFaceSeamsExact) and the terrain
 * function is pure, the elevations must be bit-identical too - so this is
 * asserted exactly, with no tolerance.
 */
bool UniverseTest_TerrainSeamContinuity(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;

    // Several planets rather than one hand-picked seed.
    const uint64 Seeds[4] = { 1, 0xDEADBEEFull, 0x1234567890ABCDEFull, 0xFFFFFFFFFFFFFFFFull };

    for (uint64 SeedValue : Seeds)
    {
        const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(SeedValue, 6371000.0);

        // --- Cube-face seams -------------------------------------------------
        for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
        {
            const EFace Face = FaceAt(FaceIndex);

            for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
            {
                const CubeSphere::EFaceEdge Edge = static_cast<CubeSphere::EFaceEdge>(EdgeIndex);

                // Dyadic sample positions, as the seam guarantees require.
                constexpr int32 Samples = 32;
                for (int32 Step = 0; Step <= Samples; ++Step)
                {
                    const double Along = static_cast<double>(Step) / static_cast<double>(Samples);

                    double U = 0.0;
                    double V = 0.0;
                    CubeSphere::GetEdgePointUV(Edge, Along, U, V);
                    const FVector3d Here = CubeSphere::FaceUVToDirection(Face, U, V);

                    EFace NeighbourFace = EFace::PosX;
                    double NeighbourU = 0.0;
                    double NeighbourV = 0.0;
                    CubeSphere::MapEdgePointToNeighbour(Face, Edge, Along, NeighbourFace, NeighbourU, NeighbourV);
                    const FVector3d There =
                        CubeSphere::FaceUVToDirection(NeighbourFace, NeighbourU, NeighbourV);

                    UVERIFY_TRUE(Result, ExactlyEqual(Here, There));

                    const double ElevationHere = FPlanetTerrain::GetElevationMeters(Planet, Settings, Here);
                    const double ElevationThere = FPlanetTerrain::GetElevationMeters(Planet, Settings, There);

                    UVERIFY_EQ_DOUBLE_EXACT(Result, ElevationThere, ElevationHere);

                    // And the resulting world positions coincide, which is what
                    // "no crack" actually means geometrically.
                    const FVector3d PosHere =
                        FPlanetTerrain::GetSurfacePositionMeters(Planet, Settings, Here);
                    const FVector3d PosThere =
                        FPlanetTerrain::GetSurfacePositionMeters(Planet, Settings, There);
                    UVERIFY_TRUE(Result, ExactlyEqual(PosHere, PosThere));
                }
            }
        }

        // --- Patch borders within a face, including across LOD levels --------
        for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
        {
            for (uint8 Level = 1; Level <= 4; ++Level)
            {
                const uint32 GridSize = 1u << Level;

                for (uint32 Y = 0; Y < GridSize; ++Y)
                {
                    for (uint32 X = 0; X + 1 < GridSize; ++X)
                    {
                        const FPlanetPatchId Left(FaceAt(FaceIndex), Level, X, Y);
                        const FPlanetPatchId Right(FaceAt(FaceIndex), Level, X + 1, Y);

                        // Dyadic positions along the shared border.
                        for (int32 Step = 0; Step <= 8; ++Step)
                        {
                            const double LocalV = static_cast<double>(Step) / 8.0;

                            const FVector3d FromLeft = Left.GetDirectionAt(1.0, LocalV);
                            const FVector3d FromRight = Right.GetDirectionAt(0.0, LocalV);
                            UVERIFY_TRUE(Result, ExactlyEqual(FromLeft, FromRight));

                            UVERIFY_EQ_DOUBLE_EXACT(Result,
                                FPlanetTerrain::GetElevationMeters(Planet, Settings, FromLeft),
                                FPlanetTerrain::GetElevationMeters(Planet, Settings, FromRight));
                        }

                        // Parent/child: a coarse patch and its child must agree
                        // on the surface, so splitting does not move terrain.
                        const FPlanetPatchId Child = Left.GetChild(EPlanetPatchQuadrant::SouthWest);
                        const FVector3d FromParent = Left.GetDirectionAt(0.25, 0.25);
                        const FVector3d FromChild = Child.GetDirectionAt(0.5, 0.5);
                        UVERIFY_TRUE(Result, ExactlyEqual(FromParent, FromChild));
                        UVERIFY_EQ_DOUBLE_EXACT(Result,
                            FPlanetTerrain::GetElevationMeters(Planet, Settings, FromParent),
                            FPlanetTerrain::GetElevationMeters(Planet, Settings, FromChild));
                    }
                }
            }
        }
    }

    return Result.Passed();
}

/**
 * Normals must be well-formed, outward-facing and continuous across seams.
 *
 * Continuity is the reason normals come from the terrain gradient rather than
 * from mesh triangles: a triangle-derived normal depends on which triangles
 * exist, so two patches meeting at a seam disagree and the seam lights up.
 * Here the normal depends only on position, so both sides agree exactly.
 */
bool UniverseTest_TerrainNormals(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTestPlanet(0x2468ACE000000001ull, 6371000.0);

    constexpr double Epsilon = 1.0e-6;

    // Tangent basis is orthonormal everywhere, including near the axes where a
    // fixed reference vector would degenerate.
    {
        // Computed rather than written as a decimal literal: a truncated
        // 0.577350269 is not exactly unit length, and TangentV inherits the
        // input's length, so a hand-typed diagonal fails a strict test for a
        // reason that has nothing to do with the code under test.
        const double Diagonal = 1.0 / FMath::Sqrt(3.0);

        const FVector3d Probes[8] = {
            FVector3d(1, 0, 0), FVector3d(-1, 0, 0),
            FVector3d(0, 1, 0), FVector3d(0, -1, 0),
            FVector3d(0, 0, 1), FVector3d(0, 0, -1),
            FVector3d(Diagonal, Diagonal, Diagonal),
            FVector3d(-Diagonal, Diagonal, -Diagonal),
        };

        for (const FVector3d& Direction : Probes)
        {
            FVector3d TangentU;
            FVector3d TangentV;
            FPlanetTerrain::GetTangentBasis(Direction, TangentU, TangentV);

            UVERIFY_NEAR(Result, TangentU.Size(), 1.0, 1.0e-12);
            UVERIFY_NEAR(Result, TangentV.Size(), 1.0, 1.0e-12);
            UVERIFY_NEAR(Result, TangentU.Dot(TangentV), 0.0, 1.0e-12);
            UVERIFY_NEAR(Result, TangentU.Dot(Direction), 0.0, 1.0e-12);
            UVERIFY_NEAR(Result, TangentV.Dot(Direction), 0.0, 1.0e-12);
        }
    }

    // Normals are unit length and point outward.
    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        constexpr int32 Steps = 9;
        for (int32 J = 0; J <= Steps; ++J)
        {
            for (int32 I = 0; I <= Steps; ++I)
            {
                const double U = static_cast<double>(I) / static_cast<double>(Steps);
                const double V = static_cast<double>(J) / static_cast<double>(Steps);

                const FVector3d Direction = CubeSphere::FaceUVToDirection(FaceAt(FaceIndex), U, V);
                const FVector3d Normal =
                    FPlanetTerrain::GetSurfaceNormal(Planet, Settings, Direction, Epsilon);

                UVERIFY_NEAR(Result, Normal.Size(), 1.0, 1.0e-9);

                // Outward: terrain slopes are far from vertical at this
                // sampling scale, so the normal stays close to radial.
                const double Outward = Normal.Dot(Direction);
                UVERIFY_TRUE(Result, Outward > 0.0);
            }
        }
    }

    // Continuity across cube-face seams: the same point sampled from either
    // face must give the same normal.
    for (int32 FaceIndex = 0; FaceIndex < FaceCount; ++FaceIndex)
    {
        const EFace Face = FaceAt(FaceIndex);

        for (int32 EdgeIndex = 0; EdgeIndex < 4; ++EdgeIndex)
        {
            const CubeSphere::EFaceEdge Edge = static_cast<CubeSphere::EFaceEdge>(EdgeIndex);

            for (int32 Step = 1; Step < 8; ++Step)
            {
                const double Along = static_cast<double>(Step) / 8.0;

                double U = 0.0;
                double V = 0.0;
                CubeSphere::GetEdgePointUV(Edge, Along, U, V);
                const FVector3d Here = CubeSphere::FaceUVToDirection(Face, U, V);

                EFace NeighbourFace = EFace::PosX;
                double NeighbourU = 0.0;
                double NeighbourV = 0.0;
                CubeSphere::MapEdgePointToNeighbour(Face, Edge, Along, NeighbourFace, NeighbourU, NeighbourV);
                const FVector3d There =
                    CubeSphere::FaceUVToDirection(NeighbourFace, NeighbourU, NeighbourV);

                const FVector3d NormalHere =
                    FPlanetTerrain::GetSurfaceNormal(Planet, Settings, Here, Epsilon);
                const FVector3d NormalThere =
                    FPlanetTerrain::GetSurfaceNormal(Planet, Settings, There, Epsilon);

                // Bit-exact: the directions are identical and the normal is a
                // pure function of direction.
                UVERIFY_EQ_DOUBLE_EXACT(Result, NormalThere.X, NormalHere.X);
                UVERIFY_EQ_DOUBLE_EXACT(Result, NormalThere.Y, NormalHere.Y);
                UVERIFY_EQ_DOUBLE_EXACT(Result, NormalThere.Z, NormalHere.Z);
            }
        }
    }

    return Result.Passed();
}

/** Planet descriptor construction, serialisation and validation. */
bool UniverseTest_PlanetSurfaceDescriptor(FUniverseTestResult& Result)
{
    // Built from a real generated system, exercising the bridge to Sprint 001.
    const FUniverseSeedHierarchy Hierarchy = FUniverseSeedHierarchy::FromText(TEXT("sprint-002"));

    FStarSystemDescriptor System;
    bool bFound = false;
    for (int64 X = 0; X < 40 && !bFound; ++X)
    {
        for (int64 Y = 0; Y < 40 && !bFound; ++Y)
        {
            const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, 0);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FStarSystemDescriptor Candidate;
                if (FStarSystemGenerator::GenerateSystem(Hierarchy, X, Y, 0, Index, Candidate)
                    && Candidate.Planets.Num() > 0)
                {
                    System = Candidate;
                    bFound = true;
                    break;
                }
            }
        }
    }
    UVERIFY_TRUE(Result, bFound);

    const FPlanetSurfaceDescriptor Planet = FPlanetSurfaceDescriptor::FromGeneratedPlanet(System, 0);
    UVERIFY_TRUE(Result, Planet.IsValid());

    // Radius comes from the astronomical descriptor, not re-derived. One source
    // of truth: the ground and the astronomy must describe the same world.
    UVERIFY_EQ_DOUBLE_EXACT(Result, Planet.RadiusMeters, System.Planets[0].RadiusMeters);

    // Position matches the generator's own placement.
    UVERIFY_TRUE(Result,
        Planet.Position == FStarSystemGenerator::GetPlanetPosition(System, System.Planets[0]));

    // Relief scales with the body rather than being a constant.
    UVERIFY_TRUE(Result, Planet.MaxElevationMeters > 0.0);
    UVERIFY_TRUE(Result, Planet.MaxElevationMeters < Planet.RadiusMeters * 0.01);
    UVERIFY_TRUE(Result, Planet.GetMaxRadiusMeters() > Planet.RadiusMeters);
    UVERIFY_TRUE(Result, Planet.GetMinRadiusMeters() < Planet.RadiusMeters);
    UVERIFY_TRUE(Result, Planet.GetMinRadiusMeters() > 0.0);

    // Rebuilding is deterministic.
    const FPlanetSurfaceDescriptor Again = FPlanetSurfaceDescriptor::FromGeneratedPlanet(System, 0);
    UVERIFY_EQ_UINT(Result, Again.GetContentHash(), Planet.GetContentHash());

    // Different orbits are different planets.
    if (System.Planets.Num() > 1)
    {
        const FPlanetSurfaceDescriptor Second = FPlanetSurfaceDescriptor::FromGeneratedPlanet(System, 1);
        UVERIFY_TRUE(Result, Second.PlanetKey != Planet.PlanetKey);
        UVERIFY_TRUE(Result, Second.GetContentHash() != Planet.GetContentHash());
    }

    // Serialisation round trip.
    {
        FUniverseByteWriter Writer;
        Planet.Serialize(Writer);
        UVERIFY_EQ_INT(Result, Writer.Num(), FPlanetSurfaceDescriptor::SerializedSizeBytes);

        FUniverseByteReader Reader(Writer.GetBytes());
        FPlanetSurfaceDescriptor Restored;
        UVERIFY_TRUE(Result, Restored.Deserialize(Reader));
        UVERIFY_EQ_UINT(Result, Restored.GetContentHash(), Planet.GetContentHash());
        UVERIFY_EQ_DOUBLE_EXACT(Result, Restored.RadiusMeters, Planet.RadiusMeters);
        UVERIFY_TRUE(Result, Restored.Position == Planet.Position);
        UVERIFY_TRUE(Result, Reader.AtEnd());
    }

    // A truncated stream is rejected.
    {
        FUniverseByteWriter Writer;
        Planet.Serialize(Writer);
        FUniverseByteReader Short(Writer.GetBytes().GetData(), 20);
        FPlanetSurfaceDescriptor Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Short));
    }

    // Patch resolution validation: only 2^p + 1 is acceptable.
    {
        FPlanetTerrainSettings Settings;
        const int32 Valid[4] = { 5, 33, 65, 129 };
        for (int32 Value : Valid)
        {
            Settings.PatchResolution = Value;
            UVERIFY_TRUE(Result, Settings.IsValid());
        }

        const int32 Invalid[5] = { 50, 64, 100, 2, 1000 };
        for (int32 Value : Invalid)
        {
            Settings.PatchResolution = Value;
            UVERIFY_FALSE(Result, Settings.IsValid());
        }
    }

    return Result.Passed();
}
