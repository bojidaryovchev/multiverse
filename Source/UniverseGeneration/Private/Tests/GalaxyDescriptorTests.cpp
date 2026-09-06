// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniverseGenerationTestList.h"
#include "GalaxyDescriptor.h"
#include "StarSystemGenerator.h"
#include "UniverseScale.h"

// ---------------------------------------------------------------------------
// GalaxyDescriptorTests.cpp
//
// The galaxy layer decides where stars can exist at all, so a defect here is
// not a cosmetic one - it moves every star in the universe, and with it every
// structure a player has built on a planet around one. These tests are
// correspondingly blunt about invariants and deliberately quiet about
// appearances: "the disk looks good" is not a testable claim, and a test that
// asserts it is a test that gets deleted the first time the art changes.
// ---------------------------------------------------------------------------

namespace
{
    FUniverseSeedHierarchy MakeGalaxyHierarchy(const TCHAR* Text)
    {
        return FUniverseSeedHierarchy::FromText(Text);
    }

    /** Every galaxy in a block of intergalactic cells, for the survey tests. */
    void CollectGalaxies(
        const FUniverseSeedHierarchy& Hierarchy,
        int32 Reach,
        TArray<FGalaxyDescriptor>& OutGalaxies)
    {
        OutGalaxies.Reset();

        for (int64 Z = -Reach; Z <= Reach; ++Z)
        {
            for (int64 Y = -Reach; Y <= Reach; ++Y)
            {
                for (int64 X = -Reach; X <= Reach; ++X)
                {
                    const int32 Count = FGalaxyGenerator::GetGalaxyCountInCell(Hierarchy, X, Y, Z);

                    for (int32 Index = 0; Index < Count; ++Index)
                    {
                        FGalaxyDescriptor Galaxy;

                        if (FGalaxyGenerator::GenerateGalaxy(Hierarchy, X, Y, Z, Index, Galaxy))
                        {
                            OutGalaxies.Add(Galaxy);
                        }
                    }
                }
            }
        }
    }
}

/**
 * Galaxies are reconstructible: the same address under the same seed produces
 * the same galaxy, and generating hundreds of others in between changes
 * nothing.
 *
 * The "in between" half is the part that catches real defects. A generator that
 * is deterministic only on a fresh process is not deterministic at all, and the
 * failure mode - a shared RNG, or a cache keyed on something that changes - only
 * ever shows up when something else was generated first.
 */
bool UniverseTest_GalaxyGenerationDeterminism(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeGalaxyHierarchy(TEXT("galaxy-determinism"));

    TArray<FGalaxyDescriptor> Galaxies;
    CollectGalaxies(Hierarchy, 2, Galaxies);

    UVERIFY_TRUE(Result, Galaxies.Num() > 0);

    for (const FGalaxyDescriptor& First : Galaxies)
    {
        FGalaxyDescriptor Second;
        UVERIFY_TRUE(Result, FGalaxyGenerator::GenerateGalaxy(
            Hierarchy, First.Id.CellX, First.Id.CellY, First.Id.CellZ, First.Id.Index, Second));

        UVERIFY_TRUE(Result, Second.Id == First.Id);
        UVERIFY_TRUE(Result, Second.Type == First.Type);
        UVERIFY_TRUE(Result, Second.Position == First.Position);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Second.RadiusLightYears, First.RadiusLightYears);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Second.DiskThicknessLightYears, First.DiskThicknessLightYears);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Second.BulgeRadiusLightYears, First.BulgeRadiusLightYears);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Second.CoreDensity, First.CoreDensity);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Second.DiskNormal.X, First.DiskNormal.X);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Second.DiskNormal.Y, First.DiskNormal.Y);
        UVERIFY_EQ_DOUBLE_EXACT(Result, Second.DiskNormal.Z, First.DiskNormal.Z);
        UVERIFY_TRUE(Result, Second.Name == First.Name);
    }

    // The same again, after generating an unrelated block. Nothing may drift.
    TArray<FGalaxyDescriptor> Unrelated;
    CollectGalaxies(MakeGalaxyHierarchy(TEXT("something-else")), 2, Unrelated);

    for (const FGalaxyDescriptor& First : Galaxies)
    {
        FGalaxyDescriptor Third;
        UVERIFY_TRUE(Result, FGalaxyGenerator::GenerateGalaxy(
            Hierarchy, First.Id.CellX, First.Id.CellY, First.Id.CellZ, First.Id.Index, Third));

        UVERIFY_EQ_DOUBLE_EXACT(Result, Third.RadiusLightYears, First.RadiusLightYears);
        UVERIFY_TRUE(Result, Third.Position == First.Position);
    }

    // The orientation basis is orthonormal. It is stored as two vectors rather
    // than a quaternion precisely so this can be checked; a basis that drifts
    // out of orthonormality skews every density sample subtly rather than
    // failing outright.
    for (const FGalaxyDescriptor& Galaxy : Galaxies)
    {
        UVERIFY_NEAR(Result, Galaxy.DiskNormal.Size(), 1.0, 1.0e-12);
        UVERIFY_NEAR(Result, Galaxy.DiskRight.Size(), 1.0, 1.0e-12);
        UVERIFY_NEAR(Result,
            FVector3d::DotProduct(Galaxy.DiskNormal, Galaxy.DiskRight), 0.0, 1.0e-12);
        UVERIFY_NEAR(Result, Galaxy.GetDiskForward().Size(), 1.0, 1.0e-12);
    }

    return Result.Passed();
}

/**
 * Sprint 006 section 99: multiple galaxies exist, and their identities are
 * unique and stable.
 *
 * Identity is address-derived, so two galaxies at the same address under
 * different universe seeds deliberately share an id and differ only in content -
 * exactly as star systems do. What must never collide is two *different*
 * addresses.
 */
bool UniverseTest_MultiGalaxyIdentity(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeGalaxyHierarchy(TEXT("multi-galaxy"));

    TArray<FGalaxyDescriptor> Galaxies;
    CollectGalaxies(Hierarchy, 3, Galaxies);

    // A 7x7x7 block of cells at an expectation of about 0.15 galaxies each.
    UVERIFY_TRUE(Result, Galaxies.Num() > 10);

    TSet<uint64> Hashes;
    TSet<uint64> Positions;

    for (const FGalaxyDescriptor& Galaxy : Galaxies)
    {
        UVERIFY_TRUE(Result, Galaxy.IsValid());
        UVERIFY_FALSE(Result, Hashes.Contains(Galaxy.Id.Hash));
        Hashes.Add(Galaxy.Id.Hash);

        // Distinct positions too. Two galaxies sharing an id would be caught
        // above; two sharing a *position* would be a placement bug the id check
        // cannot see.
        const uint64 PositionKey = UniverseHash::Hash(
            UniverseHash::Hash(
                static_cast<uint64>(Galaxy.Position.CellX),
                static_cast<uint64>(Galaxy.Position.CellY)),
            static_cast<uint64>(Galaxy.Position.CellZ));

        UVERIFY_FALSE(Result, Positions.Contains(PositionKey));
        Positions.Add(PositionKey);
    }

    // All three types occur somewhere in a sample this size.
    bool bSawSpiral = false;
    bool bSawElliptical = false;
    bool bSawIrregular = false;

    for (const FGalaxyDescriptor& Galaxy : Galaxies)
    {
        bSawSpiral = bSawSpiral || (Galaxy.Type == EGalaxyType::Spiral);
        bSawElliptical = bSawElliptical || (Galaxy.Type == EGalaxyType::Elliptical);
        bSawIrregular = bSawIrregular || (Galaxy.Type == EGalaxyType::Irregular);
    }

    UVERIFY_TRUE(Result, bSawSpiral);
    UVERIFY_TRUE(Result, bSawElliptical);
    UVERIFY_TRUE(Result, bSawIrregular);

    // A different universe seed puts different galaxies at the same addresses.
    const FUniverseSeedHierarchy Other = MakeGalaxyHierarchy(TEXT("multi-galaxy-other"));

    int32 Compared = 0;
    int32 Differed = 0;

    for (const FGalaxyDescriptor& Galaxy : Galaxies)
    {
        FGalaxyDescriptor Alternate;

        if (!FGalaxyGenerator::GenerateGalaxy(
                Other, Galaxy.Id.CellX, Galaxy.Id.CellY, Galaxy.Id.CellZ, Galaxy.Id.Index, Alternate))
        {
            continue;
        }

        ++Compared;

        // Same address, therefore the same id - and content that has no reason
        // to match.
        UVERIFY_TRUE(Result, Alternate.Id == Galaxy.Id);

        if (Alternate.Position != Galaxy.Position
            || Alternate.RadiusLightYears != Galaxy.RadiusLightYears)
        {
            ++Differed;
        }
    }

    UVERIFY_TRUE(Result, Compared > 0);
    UVERIFY_EQ_INT(Result, Differed, Compared);

    return Result.Passed();
}

/**
 * Sprint 006 section 92: galaxy-local -> universe -> galaxy-local round trips
 * within tolerance.
 *
 * The tolerance is the interesting part, and it is worth being exact about
 * because the obvious guess is wrong by ten orders of magnitude. A galaxy-local
 * offset is carried in light years as a double, and one ulp at 50,000 ly is
 * about 7e-12 ly - which is roughly seventy kilometres, not a fraction of a
 * millimetre. Light years are a coarse unit and a double does not rescue them.
 *
 * That is fine for what this type is for: a density function over a structure
 * 50,000 ly across does not care about a kilometre, and anything that does care
 * uses FUniversePosition, which stays canonical and resolves 2.44 micrometres
 * anywhere in the universe. What the test asserts is therefore that the round
 * trip is exact to within a few ulps of the light-year representation - that no
 * precision is lost beyond what the caller's own choice of unit costs.
 */
bool UniverseTest_GalaxyLocalRoundTrip(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeGalaxyHierarchy(TEXT("galaxy-round-trip"));

    FGalaxyDescriptor Galaxy;
    UVERIFY_TRUE(Result, FGalaxyGenerator::FindNearestGalaxy(Hierarchy, FUniversePosition(), Galaxy));

    // A spread of offsets: the centre, the rim, above and below the disk, and a
    // handful of arbitrary interior points.
    const double R = Galaxy.RadiusLightYears;
    const double H = Galaxy.DiskThicknessLightYears;

    const FVector3d Offsets[] =
    {
        FVector3d(0.0, 0.0, 0.0),
        FVector3d(R * 0.5, 0.0, 0.0),
        FVector3d(0.0, R * 0.5, 0.0),
        FVector3d(0.0, 0.0, H * 0.5),
        FVector3d(0.0, 0.0, -H * 0.5),
        FVector3d(R * 0.7, -R * 0.3, H * 0.25),
        FVector3d(-R * 0.9, R * 0.2, -H * 0.4),
        FVector3d(R * 0.999, 0.0, 0.0),
    };

    // A few ulps at the scale being measured, rather than an absolute figure:
    // the representable precision of a light-year double varies by ten orders
    // of magnitude between the galactic centre and the rim.
    const double RelativeTolerance = 1.0e-14;

    const double ToleranceLy = FMath::Max(R, 1.0) * RelativeTolerance;

    for (const FVector3d& Offset : Offsets)
    {
        const FUniversePosition Position = FGalaxyGenerator::FromGalaxyLocal(Galaxy, Offset);

        UVERIFY_TRUE(Result, Position.IsNormalized());

        const FGalaxyLocalPosition Local = FGalaxyGenerator::ToGalaxyLocal(Galaxy, Position);

        UVERIFY_NEAR(Result, Local.OffsetLightYears.X, Offset.X, ToleranceLy);
        UVERIFY_NEAR(Result, Local.OffsetLightYears.Y, Offset.Y, ToleranceLy);
        UVERIFY_NEAR(Result, Local.OffsetLightYears.Z, Offset.Z, ToleranceLy);

        // And the derived cylindrical terms agree with the offset they came
        // from, which is what every density consumer actually reads.
        const double ExpectedHeight = FVector3d::DotProduct(Offset, Galaxy.DiskNormal);
        UVERIFY_NEAR(Result, Local.HeightLightYears, ExpectedHeight, ToleranceLy);

        const FVector3d InPlane = Offset - Galaxy.DiskNormal * ExpectedHeight;
        UVERIFY_NEAR(Result, Local.RadiusLightYears, InPlane.Size(), ToleranceLy);
    }

    // The centre round trips exactly - it is the galaxy's own position, and no
    // arithmetic should have happened to it at all.
    const FUniversePosition Centre = FGalaxyGenerator::FromGalaxyLocal(Galaxy, FVector3d::ZeroVector);
    UVERIFY_TRUE(Result, Centre == Galaxy.Position);

    return Result.Passed();
}

/**
 * Sprint 006 section 96: the density function's invariants.
 *
 * Invariants, not appearances. That density is finite, bounded, exactly zero
 * outside the configured radius and greatest at the core are properties the
 * rest of the engine relies on. That the arms are pretty is not something a
 * test can usefully assert, nor something worth breaking a build over.
 */
bool UniverseTest_GalaxyDensityInvariants(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeGalaxyHierarchy(TEXT("galaxy-density"));

    TArray<FGalaxyDescriptor> Galaxies;
    CollectGalaxies(Hierarchy, 2, Galaxies);
    UVERIFY_TRUE(Result, Galaxies.Num() > 0);

    for (const FGalaxyDescriptor& Galaxy : Galaxies)
    {
        const double R = Galaxy.RadiusLightYears;
        const double H = Galaxy.DiskThicknessLightYears;

        // --- Bounded and finite everywhere ----------------------------------
        //
        // A NaN here would propagate into a sector's system count, and from
        // there into whether a player's home star exists.
        for (int32 Step = 0; Step <= 40; ++Step)
        {
            const double Fraction = static_cast<double>(Step) / 40.0;

            for (int32 HeightStep = -4; HeightStep <= 4; ++HeightStep)
            {
                const FVector3d Offset =
                    Galaxy.DiskRight * (R * Fraction * 1.5)
                    + Galaxy.DiskNormal * (H * static_cast<double>(HeightStep) * 0.5);

                const FUniversePosition Position = FGalaxyGenerator::FromGalaxyLocal(Galaxy, Offset);
                const double Density = FGalaxyGenerator::GetStellarDensity(Galaxy, Position);

                UVERIFY_TRUE(Result, FMath::IsFinite(Density));
                UVERIFY_TRUE(Result, Density >= 0.0);
                UVERIFY_TRUE(Result, Density <= 1.0);
            }
        }

        // --- Zero outside the configured radius, exactly --------------------
        //
        // Not "small". A galaxy that faded asymptotically would scatter a thin
        // dusting of stars across the whole of intergalactic space, and empty
        // would never actually be empty.
        const double Outside[] = { 1.001, 1.5, 4.0, 100.0 };

        for (double Multiple : Outside)
        {
            const FUniversePosition Position = FGalaxyGenerator::FromGalaxyLocal(
                Galaxy, Galaxy.DiskRight * (R * Multiple));

            UVERIFY_EQ_DOUBLE_EXACT(Result, FGalaxyGenerator::GetStellarDensity(Galaxy, Position), 0.0);
        }

        // Likewise far above the disk.
        {
            const FUniversePosition Position = FGalaxyGenerator::FromGalaxyLocal(
                Galaxy, Galaxy.DiskNormal * (H * 50.0));

            UVERIFY_EQ_DOUBLE_EXACT(Result, FGalaxyGenerator::GetStellarDensity(Galaxy, Position), 0.0);
        }

        // --- The core is the densest place in the galaxy --------------------
        const double CoreDensity = FGalaxyGenerator::GetStellarDensity(Galaxy, Galaxy.Position);
        UVERIFY_TRUE(Result, CoreDensity > 0.0);

        for (int32 Step = 1; Step <= 10; ++Step)
        {
            const FVector3d Offset = Galaxy.DiskRight * (R * static_cast<double>(Step) / 10.0);
            const FUniversePosition Position = FGalaxyGenerator::FromGalaxyLocal(Galaxy, Offset);

            UVERIFY_TRUE(Result, FGalaxyGenerator::GetStellarDensity(Galaxy, Position) <= CoreDensity);
        }

        // --- Structure exists where it is meant to --------------------------
        //
        // In the plane is denser than out of it, at the same radius. This is
        // the disk, and it is the one shape claim worth asserting, because
        // getting it wrong turns a galaxy into a ball of stars.
        {
            const double SampleRadius = R * 0.4;

            const FUniversePosition InPlane = FGalaxyGenerator::FromGalaxyLocal(
                Galaxy, Galaxy.DiskRight * SampleRadius);

            const FUniversePosition OutOfPlane = FGalaxyGenerator::FromGalaxyLocal(
                Galaxy, Galaxy.DiskRight * SampleRadius + Galaxy.DiskNormal * (H * 1.5));

            UVERIFY_TRUE(Result,
                FGalaxyGenerator::GetStellarDensity(Galaxy, InPlane)
                > FGalaxyGenerator::GetStellarDensity(Galaxy, OutOfPlane));
        }

        // --- Continuity in the interior -------------------------------------
        //
        // A step of one part in a thousand of the radius may not change density
        // by more than a fifth. A discontinuity here would appear in game as a
        // wall of stars.
        {
            double Previous = -1.0;

            for (int32 Step = 0; Step <= 1000; ++Step)
            {
                const double Fraction = static_cast<double>(Step) / 1000.0;

                const FUniversePosition Position = FGalaxyGenerator::FromGalaxyLocal(
                    Galaxy, Galaxy.DiskRight * (R * 0.98 * Fraction));

                const double Density = FGalaxyGenerator::GetStellarDensity(Galaxy, Position);

                if (Previous >= 0.0)
                {
                    UVERIFY_TRUE(Result, FMath::Abs(Density - Previous) < 0.2);
                }

                Previous = Density;
            }
        }
    }

    return Result.Passed();
}

/**
 * The bridge from the galaxy layer to star generation: a sector inside a galaxy
 * has stars, a sector in intergalactic space has none, and the boundary between
 * them is exactly where the density function says it is.
 */
bool UniverseTest_GalacticStellarDensityBridge(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeGalaxyHierarchy(TEXT("density-bridge"));

    FGalaxyDescriptor Galaxy;
    UVERIFY_TRUE(Result, FGalaxyGenerator::FindNearestGalaxy(Hierarchy, FUniversePosition(), Galaxy));

    // --- Inside: stars exist ------------------------------------------------
    const FUniversePosition Inside = FGalaxyGenerator::GetInhabitedPosition(Galaxy);

    int64 SectorX = 0;
    int64 SectorY = 0;
    int64 SectorZ = 0;
    UVERIFY_TRUE(Result,
        FStarSystemGenerator::FindPopulatedSectorNear(Hierarchy, Inside, SectorX, SectorY, SectorZ));

    UVERIFY_TRUE(Result,
        FStarSystemGenerator::GetSectorStellarDensity(Hierarchy, SectorX, SectorY, SectorZ) > 0.0);

    // --- Outside: none, and not merely fewer --------------------------------
    //
    // Twenty radii out along the disk normal, which is well clear of this
    // galaxy and - at 1.28 million light years per intergalactic cell - still
    // nowhere near another.
    const FUniversePosition Outside = FGalaxyGenerator::FromGalaxyLocal(
        Galaxy, Galaxy.DiskNormal * (Galaxy.RadiusLightYears * 20.0));

    int64 OutX = 0;
    int64 OutY = 0;
    int64 OutZ = 0;
    Outside.GetSector(OutX, OutY, OutZ);

    int32 SystemsFound = 0;

    for (int64 X = -4; X <= 4; ++X)
    {
        for (int64 Y = -4; Y <= 4; ++Y)
        {
            for (int64 Z = -4; Z <= 4; ++Z)
            {
                SystemsFound += FStarSystemGenerator::GetSystemCountInSector(
                    Hierarchy, OutX + X, OutY + Y, OutZ + Z);
            }
        }
    }

    UVERIFY_EQ_INT(Result, SystemsFound, 0);

    // --- FindGalaxyAt agrees with the density function ----------------------
    FGalaxyDescriptor Containing;
    UVERIFY_TRUE(Result, FGalaxyGenerator::FindGalaxyAt(Hierarchy, Inside, Containing));
    UVERIFY_TRUE(Result, Containing.Id == Galaxy.Id);

    FGalaxyDescriptor None;
    UVERIFY_FALSE(Result, FGalaxyGenerator::FindGalaxyAt(Hierarchy, Outside, None));

    // --- The cache does not change the answer -------------------------------
    //
    // GetSectorStellarDensity memoises which galaxy applies to an intergalactic
    // cell. A cache that returned a different answer on a later call would be
    // indistinguishable from working, right up until a player's home system
    // stopped existing.
    const double FirstAnswer =
        FStarSystemGenerator::GetSectorStellarDensity(Hierarchy, SectorX, SectorY, SectorZ);

    for (int32 Pass = 0; Pass < 4; ++Pass)
    {
        // Interleaved with far-away lookups, so the cache is forced to evict.
        for (int64 Far = 0; Far < 40; ++Far)
        {
            (void)FStarSystemGenerator::GetSectorStellarDensity(
                Hierarchy, SectorX + Far * 100000, SectorY, SectorZ);
        }

        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FStarSystemGenerator::GetSectorStellarDensity(Hierarchy, SectorX, SectorY, SectorZ),
            FirstAnswer);
    }

    FStarSystemGenerator::ResetGalaxyCache();

    UVERIFY_EQ_DOUBLE_EXACT(Result,
        FStarSystemGenerator::GetSectorStellarDensity(Hierarchy, SectorX, SectorY, SectorZ),
        FirstAnswer);

    return Result.Passed();
}

/**
 * Sprint 006 section 91: adjacent sectors must not produce duplicate system
 * identities, including across the sign boundary at zero.
 *
 * The failure this guards against is a hash that folds coordinates together
 * before mixing them - (X + Y) rather than Hash(X, Y) - which makes (3, 5) and
 * (5, 3) the same sector and puts one star in two places.
 */
bool UniverseTest_SectorIsolation(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeGalaxyHierarchy(TEXT("sector-isolation"));

    FGalaxyDescriptor Galaxy;
    UVERIFY_TRUE(Result, FGalaxyGenerator::FindNearestGalaxy(Hierarchy, FUniversePosition(), Galaxy));

    int64 BaseX = 0;
    int64 BaseY = 0;
    int64 BaseZ = 0;
    FGalaxyGenerator::GetInhabitedPosition(Galaxy).GetSector(BaseX, BaseY, BaseZ);

    TSet<uint64> SystemHashes;
    TSet<uint64> ContentHashes;

    int32 Generated = 0;

    // A block inside the galaxy, and a block spanning the origin. The sign
    // boundary is where an arithmetic-shift mistake shows up, and it costs
    // nothing to check even though those sectors are empty.
    const int64 Bases[2][3] =
    {
        { BaseX, BaseY, BaseZ },
        { -3, -3, -3 },
    };

    for (int32 Which = 0; Which < 2; ++Which)
    {
        for (int64 X = 0; X < 7; ++X)
        {
            for (int64 Y = 0; Y < 7; ++Y)
            {
                for (int64 Z = 0; Z < 7; ++Z)
                {
                    const int64 SectorX = Bases[Which][0] + X;
                    const int64 SectorY = Bases[Which][1] + Y;
                    const int64 SectorZ = Bases[Which][2] + Z;

                    const int32 Count =
                        FStarSystemGenerator::GetSystemCountInSector(Hierarchy, SectorX, SectorY, SectorZ);

                    for (int32 Index = 0; Index < Count; ++Index)
                    {
                        FStarSystemDescriptor System;

                        if (!FStarSystemGenerator::GenerateSystem(
                                Hierarchy, SectorX, SectorY, SectorZ, Index, System))
                        {
                            continue;
                        }

                        ++Generated;

                        UVERIFY_FALSE(Result, SystemHashes.Contains(System.Id.Hash));
                        SystemHashes.Add(System.Id.Hash);

                        // Content hashes may legitimately collide in principle;
                        // over a sample this small they must not.
                        UVERIFY_FALSE(Result, ContentHashes.Contains(System.GetContentHash()));
                        ContentHashes.Add(System.GetContentHash());

                        // And the system really is in the sector that claims it.
                        int64 ReportedX = 0;
                        int64 ReportedY = 0;
                        int64 ReportedZ = 0;
                        System.Position.GetSector(ReportedX, ReportedY, ReportedZ);

                        UVERIFY_EQ_INT(Result, ReportedX, SectorX);
                        UVERIFY_EQ_INT(Result, ReportedY, SectorY);
                        UVERIFY_EQ_INT(Result, ReportedZ, SectorZ);
                    }
                }
            }
        }
    }

    UVERIFY_TRUE(Result, Generated > 10);

    return Result.Passed();
}
