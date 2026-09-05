// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniverseGenerationTestList.h"
#include "StarSystemGenerator.h"
#include "UniverseRandom.h"
#include "UniverseScale.h"

namespace
{
    FUniverseSeedHierarchy MakeHierarchy(const TCHAR* Text)
    {
        return FUniverseSeedHierarchy::FromText(Text);
    }

    /** Finds a sector that actually contains at least one system. */
    bool FindPopulatedSector(
        const FUniverseSeedHierarchy& Hierarchy,
        int64& OutX, int64& OutY, int64& OutZ)
    {
        for (int64 X = 0; X < 40; ++X)
        {
            for (int64 Y = 0; Y < 40; ++Y)
            {
                if (FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, 0) > 0)
                {
                    OutX = X;
                    OutY = Y;
                    OutZ = 0;
                    return true;
                }
            }
        }
        return false;
    }
}

/**
 * The core determinism guarantee: generating a system twice yields identical
 * content, and re-generating it after generating hundreds of others still
 * yields identical content.
 *
 * The second part is the one that matters in practice. A generator that is
 * "deterministic" only when called on a fresh process is not deterministic at
 * all - the failure mode is a shared RNG or a cache whose contents depend on
 * what was generated before, and only a test that generates other things in
 * between will catch it.
 */
bool UniverseTest_SystemGenerationDeterminism(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeHierarchy(TEXT("sprint-001"));

    int64 SectorX = 0;
    int64 SectorY = 0;
    int64 SectorZ = 0;
    UVERIFY_TRUE(Result, FindPopulatedSector(Hierarchy, SectorX, SectorY, SectorZ));

    FStarSystemDescriptor First;
    UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, SectorX, SectorY, SectorZ, 0, First));
    UVERIFY_TRUE(Result, First.IsValid());

    // Immediate repeat.
    FStarSystemDescriptor Second;
    UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, SectorX, SectorY, SectorZ, 0, Second));
    UVERIFY_EQ_UINT(Result, Second.GetContentHash(), First.GetContentHash());

    // Repeat after generating a large number of unrelated systems.
    int32 Generated = 0;
    for (int64 X = 100; X < 140; ++X)
    {
        for (int64 Y = 100; Y < 140; ++Y)
        {
            const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, 7);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FStarSystemDescriptor Noise;
                if (FStarSystemGenerator::GenerateSystem(Hierarchy, X, Y, 7, Index, Noise))
                {
                    ++Generated;
                }
            }
        }
    }
    UVERIFY_TRUE(Result, Generated > 0);

    FStarSystemDescriptor Third;
    UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, SectorX, SectorY, SectorZ, 0, Third));
    UVERIFY_EQ_UINT(Result, Third.GetContentHash(), First.GetContentHash());

    // Field-level comparison as well, so a failure says which field diverged
    // rather than only that the hash differs.
    UVERIFY_TRUE(Result, Third.Id == First.Id);
    UVERIFY_TRUE(Result, Third.Position == First.Position);
    UVERIFY_TRUE(Result, Third.Name == First.Name);
    UVERIFY_EQ_INT(Result, static_cast<int32>(Third.Star.Class), static_cast<int32>(First.Star.Class));
    UVERIFY_EQ_DOUBLE_EXACT(Result, Third.Star.MassSolar, First.Star.MassSolar);
    UVERIFY_EQ_DOUBLE_EXACT(Result, Third.Star.LuminositySolar, First.Star.LuminositySolar);
    UVERIFY_EQ_INT(Result, Third.Planets.Num(), First.Planets.Num());

    for (int32 Index = 0; Index < First.Planets.Num(); ++Index)
    {
        UVERIFY_EQ_INT(Result,
            static_cast<int32>(Third.Planets[Index].Type),
            static_cast<int32>(First.Planets[Index].Type));
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            Third.Planets[Index].OrbitRadiusMeters, First.Planets[Index].OrbitRadiusMeters);
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            Third.Planets[Index].RadiusMeters, First.Planets[Index].RadiusMeters);
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            Third.Planets[Index].SurfaceGravityMs2, First.Planets[Index].SurfaceGravityMs2);
        UVERIFY_TRUE(Result, Third.Planets[Index].Name == First.Planets[Index].Name);
    }

    return Result.Passed();
}

/**
 * A different universe seed must produce a different universe at the same
 * address - otherwise the root seed is decorative and the "same seed, same
 * world" contract is meaningless in the other direction.
 */
bool UniverseTest_SystemGenerationDifferentSeeds(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy AlphaHierarchy = MakeHierarchy(TEXT("alpha"));
    const FUniverseSeedHierarchy BetaHierarchy = MakeHierarchy(TEXT("beta"));

    int32 Compared = 0;
    int32 Differed = 0;
    int32 PopulationDiffered = 0;

    for (int64 X = 0; X < 25; ++X)
    {
        for (int64 Y = 0; Y < 25; ++Y)
        {
            const int32 AlphaCount = FStarSystemGenerator::GetSystemCountInSector(AlphaHierarchy, X, Y, 0);
            const int32 BetaCount = FStarSystemGenerator::GetSystemCountInSector(BetaHierarchy, X, Y, 0);

            if (AlphaCount != BetaCount)
            {
                ++PopulationDiffered;
            }

            if (AlphaCount > 0 && BetaCount > 0)
            {
                FStarSystemDescriptor Alpha;
                FStarSystemDescriptor Beta;
                if (FStarSystemGenerator::GenerateSystem(AlphaHierarchy, X, Y, 0, 0, Alpha)
                 && FStarSystemGenerator::GenerateSystem(BetaHierarchy, X, Y, 0, 0, Beta))
                {
                    ++Compared;
                    if (Alpha.GetContentHash() != Beta.GetContentHash())
                    {
                        ++Differed;
                    }
                    // The identity is address-derived and so is deliberately
                    // the SAME across universes; only the content differs.
                    UVERIFY_TRUE(Result, Alpha.Id == Beta.Id);
                }
            }
        }
    }

    UVERIFY_TRUE(Result, Compared > 0);
    UVERIFY_EQ_INT(Result, Differed, Compared);      // every comparison differed
    UVERIFY_TRUE(Result, PopulationDiffered > 0);    // even the star map differs

    // And a third seed differs from both.
    const FUniverseSeedHierarchy GammaHierarchy = MakeHierarchy(TEXT("gamma"));
    UVERIFY_TRUE(Result, GammaHierarchy.GetUniverseSeedValue() != AlphaHierarchy.GetUniverseSeedValue());
    UVERIFY_TRUE(Result, GammaHierarchy.GetUniverseSeedValue() != BetaHierarchy.GetUniverseSeedValue());

    return Result.Passed();
}

/**
 * Generation order must not matter. Systems generated forwards and backwards
 * through the same set of addresses must match pairwise.
 *
 * This is the direct test for the "no shared generator state" rule: any hidden
 * cross-talk between generations shows up here even when a simple
 * generate-twice test passes.
 */
bool UniverseTest_SystemGenerationOrderIndependence(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeHierarchy(TEXT("order-test"));

    TArray<FUniverseSystemId> Addresses;
    for (int64 X = 0; X < 12; ++X)
    {
        for (int64 Y = 0; Y < 12; ++Y)
        {
            const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, 3);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                Addresses.Add(FStarSystemGenerator::MakeSystemId(X, Y, 3, Index));
            }
        }
    }
    UVERIFY_TRUE(Result, Addresses.Num() > 10);

    TArray<uint64> Forward;
    Forward.Reserve(Addresses.Num());
    for (int32 Index = 0; Index < Addresses.Num(); ++Index)
    {
        FStarSystemDescriptor System;
        UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, Addresses[Index], System));
        Forward.Add(System.GetContentHash());
    }

    // Backwards, and interleaved with unrelated work.
    TArray<uint64> Backward;
    Backward.SetNum(Addresses.Num());
    for (int32 Index = Addresses.Num() - 1; Index >= 0; --Index)
    {
        FUniverseRandom Distraction(static_cast<uint64>(Index));
        (void)Distraction.NextUInt64();

        FStarSystemDescriptor Interference;
        (void)FStarSystemGenerator::GenerateSystem(Hierarchy, 900 + Index, 900, 900, 0, Interference);

        FStarSystemDescriptor System;
        UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, Addresses[Index], System));
        Backward[Index] = System.GetContentHash();
    }

    for (int32 Index = 0; Index < Addresses.Num(); ++Index)
    {
        UVERIFY_EQ_UINT(Result, Backward[Index], Forward[Index]);
    }

    return Result.Passed();
}

/** System identity is address-derived, stable, and survives a round trip. */
bool UniverseTest_SystemIdStabilityAndSerialization(FUniverseTestResult& Result)
{
    const FUniverseSystemId A = FStarSystemGenerator::MakeSystemId(-17, 4, 2000000, 1);
    const FUniverseSystemId B = FStarSystemGenerator::MakeSystemId(-17, 4, 2000000, 1);

    UVERIFY_TRUE(Result, A == B);
    UVERIFY_EQ_UINT(Result, A.Hash, B.Hash);
    UVERIFY_TRUE(Result, A.IsValid());

    // Distinct addresses, including permutations and the origin edge.
    UVERIFY_TRUE(Result, A != FStarSystemGenerator::MakeSystemId(-17, 4, 2000000, 0));
    UVERIFY_TRUE(Result, A != FStarSystemGenerator::MakeSystemId(4, -17, 2000000, 1));
    UVERIFY_TRUE(Result,
        FStarSystemGenerator::MakeSystemId(0, 0, 0, 0).Hash
        != FStarSystemGenerator::MakeSystemId(-1, 0, 0, 0).Hash);

    // Even the all-zero address must produce a valid (non-zero) hash, so that
    // Hash == 0 remains a reliable "unset" marker.
    UVERIFY_TRUE(Result, FStarSystemGenerator::MakeSystemId(0, 0, 0, 0).IsValid());

    // Round trip.
    {
        FUniverseByteWriter Writer;
        A.Serialize(Writer);
        UVERIFY_EQ_INT(Result, Writer.Num(), FUniverseSystemId::SerializedSizeBytes);

        FUniverseByteReader Reader(Writer.GetBytes());
        FUniverseSystemId Restored;
        UVERIFY_TRUE(Result, Restored.Deserialize(Reader));
        UVERIFY_TRUE(Result, Restored == A);
        UVERIFY_EQ_UINT(Result, Restored.Hash, A.Hash);
        UVERIFY_TRUE(Result, Reader.AtEnd());
    }

    // A tampered hash must be rejected rather than trusted.
    {
        FUniverseByteWriter Writer;
        Writer.WriteInt64(1);
        Writer.WriteInt64(2);
        Writer.WriteInt64(3);
        Writer.WriteInt32(0);
        Writer.WriteUInt64(0xDEADBEEFDEADBEEFull);
        FUniverseByteReader Reader(Writer.GetBytes());
        FUniverseSystemId Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Reader));
    }

    // A truncated stream must be rejected.
    {
        FUniverseByteWriter Writer;
        A.Serialize(Writer);
        FUniverseByteReader Short(Writer.GetBytes().GetData(), 10);
        FUniverseSystemId Restored;
        UVERIFY_FALSE(Result, Restored.Deserialize(Short));
    }

    return Result.Passed();
}

/**
 * Generated systems must be physically coherent.
 *
 * Determinism alone is not enough: a generator that reproducibly emits a
 * planet with negative radius is reproducibly broken. These bounds are wide on
 * purpose - they catch sign errors, unit mix-ups and unit-conversion slips
 * without pinning the generator's artistic choices.
 */
bool UniverseTest_SystemPhysicalPlausibility(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeHierarchy(TEXT("plausibility"));

    int32 SystemsChecked = 0;
    int32 PlanetsChecked = 0;

    for (int64 X = 0; X < 30 && SystemsChecked < 200; ++X)
    {
        for (int64 Y = 0; Y < 30 && SystemsChecked < 200; ++Y)
        {
            const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, 0);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FStarSystemDescriptor System;
                if (!FStarSystemGenerator::GenerateSystem(Hierarchy, X, Y, 0, Index, System))
                {
                    continue;
                }
                ++SystemsChecked;

                // Star.
                UVERIFY_TRUE(Result, System.Star.MassSolar > 0.05 && System.Star.MassSolar < 100.0);
                UVERIFY_TRUE(Result, System.Star.RadiusMeters > 1.0e7 && System.Star.RadiusMeters < 1.0e12);
                UVERIFY_TRUE(Result, System.Star.LuminositySolar > 0.0);
                UVERIFY_TRUE(Result, FMath::IsFinite(System.Star.LuminositySolar));
                UVERIFY_TRUE(Result, System.Star.SurfaceTemperatureK > 1000.0
                                && System.Star.SurfaceTemperatureK < 60000.0);
                UVERIFY_TRUE(Result, System.Star.ColorR >= 0.0 && System.Star.ColorR <= 1.0);
                UVERIFY_TRUE(Result, System.Star.ColorG >= 0.0 && System.Star.ColorG <= 1.0);
                UVERIFY_TRUE(Result, System.Star.ColorB >= 0.0 && System.Star.ColorB <= 1.0);
                UVERIFY_FALSE(Result, System.Star.Name.IsEmpty());

                // The system must sit inside the sector that claims it.
                int64 ReportedX = 0;
                int64 ReportedY = 0;
                int64 ReportedZ = 0;
                System.Position.GetSector(ReportedX, ReportedY, ReportedZ);
                UVERIFY_EQ_INT(Result, ReportedX, X);
                UVERIFY_EQ_INT(Result, ReportedY, Y);
                UVERIFY_EQ_INT(Result, ReportedZ, 0);
                UVERIFY_TRUE(Result, System.Position.IsNormalized());

                // Planets.
                double PreviousOrbit = 0.0;
                for (const FPlanetDescriptor& Planet : System.Planets)
                {
                    ++PlanetsChecked;

                    UVERIFY_TRUE(Result, Planet.RadiusMeters > 1.0e5 && Planet.RadiusMeters < 1.0e9);
                    UVERIFY_TRUE(Result, Planet.MassKg > 0.0 && FMath::IsFinite(Planet.MassKg));
                    UVERIFY_TRUE(Result, Planet.SurfaceGravityMs2 > 0.0 && Planet.SurfaceGravityMs2 < 200.0);
                    UVERIFY_TRUE(Result, Planet.OrbitalPeriodSeconds > 0.0
                                    && FMath::IsFinite(Planet.OrbitalPeriodSeconds));
                    UVERIFY_TRUE(Result, Planet.EquilibriumTemperatureK > 0.0
                                    && Planet.EquilibriumTemperatureK < 20000.0);
                    UVERIFY_TRUE(Result, Planet.OrbitPhaseRadians >= 0.0
                                    && Planet.OrbitPhaseRadians <= 6.2832);
                    UVERIFY_TRUE(Result, FMath::Abs(Planet.OrbitInclinationRadians) < 0.5);
                    UVERIFY_TRUE(Result, FMath::Abs(Planet.RotationPeriodSeconds) > 3600.0);
                    UVERIFY_FALSE(Result, Planet.Name.IsEmpty());

                    // Orbits must be strictly increasing outward, and never
                    // inside the star itself.
                    UVERIFY_TRUE(Result, Planet.OrbitRadiusMeters > PreviousOrbit);
                    UVERIFY_TRUE(Result, Planet.OrbitRadiusMeters > System.Star.RadiusMeters);
                    PreviousOrbit = Planet.OrbitRadiusMeters;

                    // Giants must be beyond a rocky world's size, and rocky
                    // worlds must not be giant-sized - a mix-up here would
                    // mean the type/radius tables had drifted apart.
                    if (Planet.Type == EPlanetType::GasGiant)
                    {
                        UVERIFY_TRUE(Result, Planet.RadiusMeters > 4.0e7);
                        UVERIFY_TRUE(Result, Planet.bHasAtmosphere);
                    }
                    if (Planet.Type == EPlanetType::Rocky || Planet.Type == EPlanetType::Terrestrial)
                    {
                        UVERIFY_TRUE(Result, Planet.RadiusMeters < 2.0e7);
                    }
                }
            }
        }
    }

    UVERIFY_TRUE(Result, SystemsChecked > 20);
    UVERIFY_TRUE(Result, PlanetsChecked > 50);

    return Result.Passed();
}

/**
 * Planet placement is shared by the spawning code, the rendering actor and the
 * HUD markers, so it has to be deterministic and it has to actually put the
 * planet at its stated orbital radius. A sign error or a unit slip here would
 * put the marker and the body in different places - confusing in a way that is
 * hard to trace back to a single function.
 */
bool UniverseTest_PlanetPlacementDeterminism(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeHierarchy(TEXT("placement"));

    int32 PlanetsChecked = 0;

    for (int64 X = 0; X < 30 && PlanetsChecked < 120; ++X)
    {
        for (int64 Y = 0; Y < 30 && PlanetsChecked < 120; ++Y)
        {
            const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, 0);
            for (int32 Index = 0; Index < Count; ++Index)
            {
                FStarSystemDescriptor System;
                if (!FStarSystemGenerator::GenerateSystem(Hierarchy, X, Y, 0, Index, System))
                {
                    continue;
                }

                double PreviousDistance = 0.0;
                for (const FPlanetDescriptor& Planet : System.Planets)
                {
                    ++PlanetsChecked;

                    const FUniversePosition A = FStarSystemGenerator::GetPlanetPosition(System, Planet);
                    const FUniversePosition B = FStarSystemGenerator::GetPlanetPosition(System, Planet);

                    // Pure function: same inputs, identical output.
                    UVERIFY_TRUE(Result, A == B);
                    UVERIFY_TRUE(Result, A.IsNormalized());

                    // The planet really is its stated orbital radius from its
                    // star in canonical space. The rendering shrinks it; the
                    // data does not.
                    const double ActualMeters =
                        FUniversePosition::DistanceMeters(System.Position, A);

                    // Relative tolerance: orbital radii span five orders of
                    // magnitude, so a fixed epsilon would be meaningless.
                    UVERIFY_TRUE(Result,
                        FMath::Abs(ActualMeters - Planet.OrbitRadiusMeters)
                            <= Planet.OrbitRadiusMeters * 1.0e-9);

                    // Outer planets really are further out.
                    UVERIFY_TRUE(Result, ActualMeters > PreviousDistance);
                    PreviousDistance = ActualMeters;
                }
            }
        }
    }

    UVERIFY_TRUE(Result, PlanetsChecked > 50);

    return Result.Passed();
}

/**
 * Sector population must match the intended stellar density. This is what
 * keeps the galaxy from being either empty or a solid wall of stars, and it
 * pins the distribution so a future change to it is a deliberate act.
 */
bool UniverseTest_SectorPopulationStatistics(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeHierarchy(TEXT("statistics"));

    int64 TotalSystems = 0;
    int64 TotalSectors = 0;
    int32 MaxInAnySector = 0;

    for (int64 X = -10; X < 10; ++X)
    {
        for (int64 Y = -10; Y < 10; ++Y)
        {
            for (int64 Z = -10; Z < 10; ++Z)
            {
                const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, Z);
                UVERIFY_TRUE(Result, Count >= 0 && Count <= 2);
                TotalSystems += Count;
                ++TotalSectors;
                MaxInAnySector = FMath::Max(MaxInAnySector, Count);
            }
        }
    }

    const double Mean = static_cast<double>(TotalSystems) / static_cast<double>(TotalSectors);

    // Intended expectation is 0.47 systems per sector (weights 0.60/0.33/0.07).
    UVERIFY_NEAR(Result, Mean, 0.47, 0.05);
    UVERIFY_EQ_INT(Result, MaxInAnySector, 2);

    // Convert to a stellar density and check it against the solar
    // neighbourhood's ~0.004 stars/ly^3. Getting this wrong by an order of
    // magnitude would make interstellar travel either trivial or impossible.
    const double SectorVolumeLy3 =
        UniverseScale::SectorSizeLightYears
        * UniverseScale::SectorSizeLightYears
        * UniverseScale::SectorSizeLightYears;
    const double DensityPerLy3 = Mean / SectorVolumeLy3;
    UVERIFY_TRUE(Result, DensityPerLy3 > 0.002 && DensityPerLy3 < 0.008);

    return Result.Passed();
}

/** Proximity queries return the same set, in the same order, every time. */
bool UniverseTest_ProximityQueryDeterminism(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeHierarchy(TEXT("proximity"));

    const FUniversePosition Centre = FUniversePosition::FromSectorCorner(5, 5, 5);

    TArray<FStarSystemDescriptor> FirstPass;
    FStarSystemGenerator::FindSystemsWithin(Hierarchy, Centre, 12.0, FirstPass, 64);
    UVERIFY_TRUE(Result, FirstPass.Num() > 0);

    TArray<FStarSystemDescriptor> SecondPass;
    FStarSystemGenerator::FindSystemsWithin(Hierarchy, Centre, 12.0, SecondPass, 64);

    UVERIFY_EQ_INT(Result, SecondPass.Num(), FirstPass.Num());
    for (int32 Index = 0; Index < FirstPass.Num() && Index < SecondPass.Num(); ++Index)
    {
        UVERIFY_TRUE(Result, SecondPass[Index].Id == FirstPass[Index].Id);
        UVERIFY_EQ_UINT(Result, SecondPass[Index].GetContentHash(), FirstPass[Index].GetContentHash());
    }

    // Every returned system must genuinely be inside the radius.
    for (const FStarSystemDescriptor& System : FirstPass)
    {
        UVERIFY_TRUE(Result,
            FUniversePosition::DistanceLightYears(Centre, System.Position) <= 12.0);
    }

    // A larger radius must be a superset.
    TArray<FStarSystemDescriptor> Wider;
    FStarSystemGenerator::FindSystemsWithin(Hierarchy, Centre, 20.0, Wider, 512);
    UVERIFY_TRUE(Result, Wider.Num() >= FirstPass.Num());

    // Nearest-system search must agree with the brute-force scan.
    FStarSystemDescriptor Nearest;
    if (FStarSystemGenerator::FindNearestSystem(Hierarchy, Centre, 20.0, Nearest))
    {
        double BestDistance = 1.0e30;
        FUniverseSystemId BestId;
        for (const FStarSystemDescriptor& System : Wider)
        {
            const double Distance = FUniversePosition::DistanceLightYears(Centre, System.Position);
            if (Distance < BestDistance)
            {
                BestDistance = Distance;
                BestId = System.Id;
            }
        }
        UVERIFY_TRUE(Result, Nearest.Id == BestId);
    }

    return Result.Passed();
}

/**
 * The player-facing guarantee, stated as a test:
 *
 *   fly to a system, record it, travel an enormous distance away, generate
 *   entirely different regions, come back, and find exactly what was there.
 *
 * Nothing is stored between the two visits. The system is reconstructed from
 * its address alone, which is the whole premise of the architecture.
 */
bool UniverseTest_LeaveAndReturnReproduction(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeHierarchy(TEXT("sprint-001"));

    // Find a home system.
    int64 HomeX = 0;
    int64 HomeY = 0;
    int64 HomeZ = 0;
    UVERIFY_TRUE(Result, FindPopulatedSector(Hierarchy, HomeX, HomeY, HomeZ));

    FStarSystemDescriptor Home;
    UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, HomeX, HomeY, HomeZ, 0, Home));

    const uint64 HomeContentHash = Home.GetContentHash();
    const FUniversePosition HomePosition = Home.Position;
    const FString HomeName = Home.Name;
    const int32 HomePlanetCount = Home.Planets.Num();

    // Travel: cross an enormous distance in whole-cell jumps, generating the
    // regions passed through so that anything order-dependent has every chance
    // to corrupt the result.
    FUniversePosition Traveller = HomePosition;
    for (int32 Leg = 0; Leg < 50; ++Leg)
    {
        FUniversePosition Next;
        UVERIFY_TRUE(Result, Traveller.TryOffsetByCells(
            UniverseScale::SectorSizeInCells * 3,
            UniverseScale::SectorSizeInCells * 2,
            UniverseScale::SectorSizeInCells,
            Next));
        Traveller = Next;

        int64 SectorX = 0;
        int64 SectorY = 0;
        int64 SectorZ = 0;
        Traveller.GetSector(SectorX, SectorY, SectorZ);

        const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, SectorX, SectorY, SectorZ);
        for (int32 Index = 0; Index < Count; ++Index)
        {
            FStarSystemDescriptor Passing;
            (void)FStarSystemGenerator::GenerateSystem(Hierarchy, SectorX, SectorY, SectorZ, Index, Passing);
        }
    }

    // We really did travel a long way.
    const double TravelledLy = FUniversePosition::DistanceLightYears(HomePosition, Traveller);
    UVERIFY_TRUE(Result, TravelledLy > 500.0);

    // Come back the same way and confirm the coordinate round trip is exact.
    for (int32 Leg = 0; Leg < 50; ++Leg)
    {
        FUniversePosition Next;
        UVERIFY_TRUE(Result, Traveller.TryOffsetByCells(
            -UniverseScale::SectorSizeInCells * 3,
            -UniverseScale::SectorSizeInCells * 2,
            -UniverseScale::SectorSizeInCells,
            Next));
        Traveller = Next;
    }
    UVERIFY_TRUE(Result, Traveller == HomePosition);

    // Regenerate the home system from its address alone.
    FStarSystemDescriptor Returned;
    UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, HomeX, HomeY, HomeZ, 0, Returned));

    UVERIFY_EQ_UINT(Result, Returned.GetContentHash(), HomeContentHash);
    UVERIFY_TRUE(Result, Returned.Position == HomePosition);
    UVERIFY_TRUE(Result, Returned.Name == HomeName);
    UVERIFY_EQ_INT(Result, Returned.Planets.Num(), HomePlanetCount);

    // And regenerating from a serialised identity - the persistence path -
    // gives the same result too.
    {
        FUniverseByteWriter Writer;
        Home.Id.Serialize(Writer);
        FUniverseByteReader Reader(Writer.GetBytes());

        FUniverseSystemId RestoredId;
        UVERIFY_TRUE(Result, RestoredId.Deserialize(Reader));

        FStarSystemDescriptor FromSavedId;
        UVERIFY_TRUE(Result, FStarSystemGenerator::GenerateSystem(Hierarchy, RestoredId, FromSavedId));
        UVERIFY_EQ_UINT(Result, FromSavedId.GetContentHash(), HomeContentHash);
    }

    return Result.Passed();
}
