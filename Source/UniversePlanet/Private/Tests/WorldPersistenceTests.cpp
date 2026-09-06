// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"
#include "WorldPersistenceIdentity.h"
#include "PlanetEnvironment.h"
#include "PlanetVegetation.h"
#include "CubeSphere.h"
#include "PlanetTerrain.h"

using CubeSphere::EFace;

namespace
{
    FPlanetSurfaceDescriptor MakePersistPlanet(uint64 SeedValue, double RadiusMeters)
    {
        FPlanetSurfaceDescriptor Planet;
        Planet.PlanetKey = SeedValue | 1ull;
        Planet.Seed = FUniverseSeed(SeedValue);
        Planet.RadiusMeters = RadiusMeters;
        Planet.MaxElevationMeters = RadiusMeters * 0.00139;
        Planet.MaxDepthMeters = RadiusMeters * 0.00172;
        Planet.SurfaceGravityMs2 = 9.81;
        Planet.AtmosphereHeightMeters = RadiusMeters * 0.0157;
        Planet.GenerationVersion = PlanetTerrainVersion::Current;
        return Planet;
    }
}

/**
 * Persistence region identity must be stable, unambiguous, and must not let two
 * planets collide.
 *
 * The collision case is the one worth caring about. Two planets have regions at
 * identical face and coordinates, so anything that keyed on the address alone
 * would apply one world's buildings to another - and would do it silently,
 * looking like a generation bug rather than a storage one.
 */
bool UniverseTest_PersistenceRegionIdentity(FUniverseTestResult& Result)
{
    const double Radii[4] = { 200000.0, 1737400.0, 4208700.0, 6371000.0 };

    for (double Radius : Radii)
    {
        const FPlanetSurfaceDescriptor Planet = MakePersistPlanet(0xB0A7ull, Radius);

        // The level is chosen to hit the target region size, and is clamped.
        const uint8 Level = FPersistenceRegionId::GetLevelForRadius(Radius);

        UVERIFY_TRUE(Result, Level >= FPersistenceRegionId::MinLevel);
        UVERIFY_TRUE(Result, Level <= FPersistenceRegionId::MaxLevel);

        for (int32 Index = 0; Index < 256; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 256);

            const FPersistenceRegionId Region =
                FPersistenceRegionId::FromDirection(Planet, Direction);

            UVERIFY_TRUE(Result, Region.IsValid());
            UVERIFY_EQ_UINT(Result, Region.PlanetKey, Planet.PlanetKey);
            UVERIFY_EQ_INT(Result, Region.Level, Level);
            UVERIFY_TRUE(Result, Region.Face < 6);

            // Deterministic: the same direction always lands in the same region.
            UVERIFY_TRUE(Result, FPersistenceRegionId::FromDirection(Planet, Direction) == Region);

            // The position form agrees with the direction form, at any radius.
            const double Distance = Radius * 1.001;
            const FVector3d Local(
                Direction.X * Distance, Direction.Y * Distance, Direction.Z * Distance);

            UVERIFY_TRUE(Result, FPersistenceRegionId::FromPlanetLocal(Planet, Local) == Region);

            // Region size is near the target, within a factor of two either
            // way - which is all a power-of-two level can guarantee.
            const double SizeMeters = Region.GetSizeMeters(Radius);

            UVERIFY_TRUE(Result, SizeMeters > 0.0);
            UVERIFY_TRUE(Result, SizeMeters < FPersistenceRegionId::TargetSizeMeters * 2.1);

            // Unless the level was clamped, in which case a small body's
            // regions are legitimately larger than the target relative to it.
            if (Level > FPersistenceRegionId::MinLevel && Level < FPersistenceRegionId::MaxLevel)
            {
                UVERIFY_TRUE(Result, SizeMeters > FPersistenceRegionId::TargetSizeMeters * 0.49);
            }

            // The packed key round-trips the address exactly - no hashing, so
            // no collisions.
            const uint64 Key = Region.GetLocalKey();

            UVERIFY_EQ_UINT(Result, (Key >> 61) & 0x7ull, static_cast<uint64>(Region.Face));
            UVERIFY_EQ_UINT(Result, (Key >> 56) & 0x1Full, static_cast<uint64>(Region.Level));
            UVERIFY_EQ_UINT(Result, (Key >> 28) & 0x0FFFFFFFull, static_cast<uint64>(Region.X));
            UVERIFY_EQ_UINT(Result, Key & 0x0FFFFFFFull, static_cast<uint64>(Region.Y));
        }
    }

    // --- Two planets do not collide -----------------------------------------
    {
        const FPlanetSurfaceDescriptor PlanetA = MakePersistPlanet(0xAAAAull, 6371000.0);
        const FPlanetSurfaceDescriptor PlanetB = MakePersistPlanet(0xBBBBull, 6371000.0);

        UVERIFY_TRUE(Result, PlanetA.PlanetKey != PlanetB.PlanetKey);

        int32 SameAddress = 0;

        for (int32 Index = 0; Index < 128; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 128);

            const FPersistenceRegionId A = FPersistenceRegionId::FromDirection(PlanetA, Direction);
            const FPersistenceRegionId B = FPersistenceRegionId::FromDirection(PlanetB, Direction);

            // Same planet radius, so the same direction gives the same *address*
            // - which is exactly the situation that would be dangerous if the
            // planet were not part of the identity.
            UVERIFY_EQ_UINT(Result, A.GetLocalKey(), B.GetLocalKey());
            ++SameAddress;

            // But the regions are different, because the planet is.
            UVERIFY_TRUE(Result, A != B);
        }

        UVERIFY_EQ_INT(Result, SameAddress, 128);
    }

    // --- Regions actually partition ------------------------------------------
    //
    // Adjacent points must sometimes fall in different regions and nearby
    // points must usually fall in the same one. A region system that put every
    // point in one region, or every point in its own, would pass every
    // assertion above.
    {
        const FPlanetSurfaceDescriptor Planet = MakePersistPlanet(0xC0DEull, 6371000.0);

        TSet<uint64> Distinct;

        for (int32 Index = 0; Index < 512; ++Index)
        {
            const FVector3d Direction = FPlanetEnvironment::GetSampleDirection(Index, 512);
            Distinct.Add(FPersistenceRegionId::FromDirection(Planet, Direction).GetLocalKey());
        }

        // 512 points spread over a planet with millions of regions: essentially
        // all distinct.
        UVERIFY_TRUE(Result, Distinct.Num() > 500);

        // And two points a few metres apart share a region.
        const FVector3d Base = FPlanetEnvironment::GetSampleDirection(7, 512);

        FVector3d TangentU;
        FVector3d TangentV;
        FPlanetTerrain::GetTangentBasis(Base, TangentU, TangentV);

        const double TenMetres = 10.0 / Planet.RadiusMeters;

        const FVector3d Nearby = FVector3d(
            Base.X + TangentU.X * TenMetres,
            Base.Y + TangentU.Y * TenMetres,
            Base.Z + TangentU.Z * TenMetres).GetSafeNormal();

        UVERIFY_TRUE(Result,
            FPersistenceRegionId::FromDirection(Planet, Base)
            == FPersistenceRegionId::FromDirection(Planet, Nearby));
    }

    return Result.Passed();
}

/**
 * Entity identity must be derivable, stable, distinguishable by kind, and must
 * survive a round trip through text.
 */
bool UniverseTest_PersistentEntityIdentity(FUniverseTestResult& Result)
{
    constexpr uint64 PlanetKey = 0x1234567890ABCDEFull;
    constexpr uint32 TerrainVersion = 1;
    constexpr uint32 EnvironmentVersion = 1;

    TSet<uint64> SeenHigh;
    TSet<uint64> SeenLow;

    int32 Generated = 0;

    for (int32 FaceIndex = 0; FaceIndex < CubeSphere::FaceCount; ++FaceIndex)
    {
        for (int32 LayerIndex = 0; LayerIndex < static_cast<int32>(EVegetationLayer::Count); ++LayerIndex)
        {
            const EVegetationLayer Layer = static_cast<EVegetationLayer>(LayerIndex);

            for (int32 Cell = 0; Cell < 24; ++Cell)
            {
                const FPlanetPatchId PatchId(
                    static_cast<EFace>(FaceIndex), 13, 4000u + Cell, 900u + Cell * 3);

                const FPersistentEntityId Id = FPersistentEntityId::ForVegetation(
                    PlanetKey, PatchId, Layer, Cell, Cell * 2,
                    TerrainVersion, EnvironmentVersion);

                UVERIFY_TRUE(Result, Id.IsValid());
                UVERIFY_TRUE(Result, Id.GetKind() == EPersistentEntityKind::Procedural);

                // Derived, so recomputing gives exactly the same name.
                const FPersistentEntityId Again = FPersistentEntityId::ForVegetation(
                    PlanetKey, PatchId, Layer, Cell, Cell * 2,
                    TerrainVersion, EnvironmentVersion);

                UVERIFY_TRUE(Result, Id == Again);

                // Distinct addresses give distinct names. With 576 samples in a
                // 128-bit space, any repeat is a construction error rather than
                // a birthday collision.
                UVERIFY_FALSE(Result, SeenHigh.Contains(Id.High) && SeenLow.Contains(Id.Low));

                SeenHigh.Add(Id.High);
                SeenLow.Add(Id.Low);

                ++Generated;

                // Text round trip, which is how it reaches the database.
                FPersistentEntityId Parsed;
                UVERIFY_TRUE(Result, FPersistentEntityId::FromHexString(Id.ToHexString(), Parsed));
                UVERIFY_TRUE(Result, Parsed == Id);
                UVERIFY_EQ_INT(Result, Id.ToHexString().Len(), 32);

                // Binary round trip.
                FUniverseByteWriter Writer;
                Id.Serialize(Writer);

                UVERIFY_EQ_INT(Result, Writer.GetBytes().Num(),
                    FPersistentEntityId::SerializedSizeBytes);

                FUniverseByteReader Reader(Writer.GetBytes());
                FPersistentEntityId Restored;
                UVERIFY_TRUE(Result, Restored.Deserialize(Reader));
                UVERIFY_TRUE(Result, Restored == Id);
            }
        }
    }

    UVERIFY_EQ_INT(Result, Generated, 6 * static_cast<int32>(EVegetationLayer::Count) * 24);

    // --- Every input is part of the name ------------------------------------
    {
        const FPlanetPatchId PatchId(EFace::PosZ, 13, 100, 200);

        const FPersistentEntityId Base = FPersistentEntityId::ForVegetation(
            PlanetKey, PatchId, EVegetationLayer::Canopy, 5, 7, 1, 1);

        // A different planet.
        UVERIFY_FALSE(Result, Base == FPersistentEntityId::ForVegetation(
            PlanetKey + 1, PatchId, EVegetationLayer::Canopy, 5, 7, 1, 1));

        // A different patch.
        UVERIFY_FALSE(Result, Base == FPersistentEntityId::ForVegetation(
            PlanetKey, FPlanetPatchId(EFace::PosZ, 13, 101, 200),
            EVegetationLayer::Canopy, 5, 7, 1, 1));

        // A different layer.
        UVERIFY_FALSE(Result, Base == FPersistentEntityId::ForVegetation(
            PlanetKey, PatchId, EVegetationLayer::Scatter, 5, 7, 1, 1));

        // A different cell.
        UVERIFY_FALSE(Result, Base == FPersistentEntityId::ForVegetation(
            PlanetKey, PatchId, EVegetationLayer::Canopy, 5, 8, 1, 1));

        // Cells are not symmetric - (5,7) is not (7,5).
        UVERIFY_FALSE(Result, Base == FPersistentEntityId::ForVegetation(
            PlanetKey, PatchId, EVegetationLayer::Canopy, 7, 5, 1, 1));

        // And a generation version bump changes every name, which is the point:
        // a removal recorded under version 1 must not silently delete whatever
        // version 2 puts at the same address.
        UVERIFY_FALSE(Result, Base == FPersistentEntityId::ForVegetation(
            PlanetKey, PatchId, EVegetationLayer::Canopy, 5, 7, 2, 1));

        UVERIFY_FALSE(Result, Base == FPersistentEntityId::ForVegetation(
            PlanetKey, PatchId, EVegetationLayer::Canopy, 5, 7, 1, 2));
    }

    // --- Created ids are distinguishable and unique -------------------------
    {
        TSet<uint64> Highs;

        for (int32 Index = 0; Index < 4096; ++Index)
        {
            const FPersistentEntityId Id =
                FPersistentEntityId::CreateNew(0xF00Dull + Index, 0xBEEFull + Index * 7);

            UVERIFY_TRUE(Result, Id.IsValid());
            UVERIFY_TRUE(Result, Id.GetKind() == EPersistentEntityKind::Created);

            UVERIFY_FALSE(Result, Highs.Contains(Id.High));
            Highs.Add(Id.High);

            FPersistentEntityId Parsed;
            UVERIFY_TRUE(Result, FPersistentEntityId::FromHexString(Id.ToHexString(), Parsed));
            UVERIFY_TRUE(Result, Parsed == Id);
        }
    }

    // --- Malformed text is rejected rather than parsed as zero --------------
    {
        FPersistentEntityId Parsed;

        UVERIFY_FALSE(Result, FPersistentEntityId::FromHexString(TEXT(""), Parsed));
        UVERIFY_FALSE(Result, FPersistentEntityId::FromHexString(TEXT("abc"), Parsed));
        UVERIFY_FALSE(Result, FPersistentEntityId::FromHexString(
            TEXT("ZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZZ"), Parsed));

        // Well-formed hex whose kind byte is not a valid kind: a zero id would
        // read as "None" and be quietly skipped, which looks like data loss
        // rather than the corruption it is.
        UVERIFY_FALSE(Result, FPersistentEntityId::FromHexString(
            TEXT("00000000000000000000000000000000"), Parsed));
    }

    // --- The instance overload agrees with the address overload -------------
    {
        FVegetationInstance Instance;
        Instance.PatchId = FPlanetPatchId(EFace::NegY, 13, 77, 88);
        Instance.Layer = EVegetationLayer::Canopy;
        Instance.CellX = 3;
        Instance.CellY = 9;

        UVERIFY_TRUE(Result,
            FPersistentEntityId::ForVegetation(PlanetKey, Instance, 1, 1)
            == FPersistentEntityId::ForVegetation(
                PlanetKey, Instance.PatchId, Instance.Layer, 3, 9, 1, 1));
    }

    return Result.Passed();
}

/**
 * A scattered instance must be able to name itself, and the same instance must
 * get the same name across a regeneration.
 *
 * This is the test that makes "remove tree 817291" meaningful. It runs the real
 * scatter, names every instance, and checks the names are unique within a patch
 * and reproduced exactly when the patch is scattered again.
 */
bool UniverseTest_VegetationPersistentIdentity(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakePersistPlanet(0x7A6E7ull, 6371000.0);

    FPlanetEnvironmentDescriptor Environment;
    Environment.PlanetKey = Planet.PlanetKey;
    Environment.Seed = Planet.Seed.Stream(UniverseSeedDomain::StreamEnvironment);
    Environment.RotationAxis = FVector3d(0.0, 0.0, 1.0);
    Environment.MeanSurfaceTemperatureK = 288.0;
    Environment.EquatorPoleDeltaK = 50.0;
    Environment.LapseRateKPerKm = 6.5;
    Environment.AtmosphereDensity = 1.0;
    Environment.OceanRadiusMeters = Planet.RadiusMeters;
    Environment.OceanCoverage = 0.5;
    Environment.Biosphere = EPlanetBiosphere::Terrestrial;
    Environment.HumidityBias = 0.5;
    Environment.VegetationPotential = 1.0;
    Environment.GenerationVersion = PlanetEnvironmentVersion::Current;

    const FPlanetPatchId Patches[3] = {
        FPlanetPatchId(EFace::PosX, 13, 4096, 4096),
        FPlanetPatchId(EFace::NegZ, 12, 1000, 2000),
        FPlanetPatchId(EFace::PosY, 13, 77, 6000),
    };

    int32 TotalNamed = 0;

    for (const FPlanetPatchId& PatchId : Patches)
    {
        for (int32 LayerIndex = 0; LayerIndex < static_cast<int32>(EVegetationLayer::Count); ++LayerIndex)
        {
            const EVegetationLayer Layer = static_cast<EVegetationLayer>(LayerIndex);

            TArray<FVegetationInstance> First;
            TArray<FVegetationInstance> Second;
            bool bHitBudget = false;

            FPlanetVegetation::Scatter(
                Planet, Environment, Settings, PatchId, Layer, 2000, First, bHitBudget);

            FPlanetVegetation::Scatter(
                Planet, Environment, Settings, PatchId, Layer, 2000, Second, bHitBudget);

            UVERIFY_EQ_INT(Result, First.Num(), Second.Num());

            TSet<uint64> Names;

            for (int32 Index = 0; Index < First.Num(); ++Index)
            {
                const FPersistentEntityId IdA = FPersistentEntityId::ForVegetation(
                    Planet.PlanetKey, First[Index],
                    Planet.GenerationVersion, Environment.GenerationVersion);

                const FPersistentEntityId IdB = FPersistentEntityId::ForVegetation(
                    Planet.PlanetKey, Second[Index],
                    Planet.GenerationVersion, Environment.GenerationVersion);

                // The same instance names itself the same way across a
                // regeneration. Without this, a removal recorded in one session
                // would fail to match in the next.
                UVERIFY_TRUE(Result, IdA == IdB);
                UVERIFY_TRUE(Result, IdA.GetKind() == EPersistentEntityKind::Procedural);

                // Unique within the patch-layer: two instances sharing a name
                // would mean removing one removed both.
                UVERIFY_FALSE(Result, Names.Contains(IdA.High));
                Names.Add(IdA.High);

                // And the instance carries the address the name is built from.
                UVERIFY_TRUE(Result, First[Index].PatchId == PatchId);
                UVERIFY_TRUE(Result, First[Index].Layer == Layer);

                ++TotalNamed;
            }
        }
    }

    UVERIFY_MESSAGE(Result, TotalNamed > 0,
        TEXT("No vegetation was placed, so no identity was exercised."));

    return Result.Passed();
}
