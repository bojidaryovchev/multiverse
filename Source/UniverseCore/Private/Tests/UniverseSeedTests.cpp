// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniverseCoreTestList.h"
#include "UniverseHash.h"
#include "UniverseRandom.h"
#include "UniverseSeed.h"

/**
 * Hash stability.
 *
 * The first two assertions are known-answer tests against the published
 * SplitMix64 test vectors (the algorithm seeded with 0 emits
 * 0xE220A8397B1DCDAF then 0x6E789E6AA1B965F4). They verify that our mixer is
 * genuinely SplitMix64 and not a mistyped variant of it - a transposed digit
 * in one of the multipliers would still produce plausible-looking noise and
 * would otherwise never be noticed, while silently making this universe
 * unreproducible by anyone else's implementation.
 *
 * The remaining assertions are the properties the seed hierarchy depends on.
 */
bool UniverseTest_HashStability(FUniverseTestResult& Result)
{
    // Published SplitMix64 vectors.
    UVERIFY_EQ_UINT(Result, UniverseHash::Mix64(0), 0xE220A8397B1DCDAFull);
    UVERIFY_EQ_UINT(Result, UniverseHash::Mix64(0x9E3779B97F4A7C15ull), 0x6E789E6AA1B965F4ull);

    // Purity: the same input always gives the same output within a run.
    for (uint64 Index = 0; Index < 1000; ++Index)
    {
        UVERIFY_EQ_UINT(Result, UniverseHash::Mix64(Index), UniverseHash::Mix64(Index));
    }

    // Avalanche: adjacent inputs must not produce adjacent outputs. Without
    // this, sector seeds derived from an index loop would be correlated and
    // neighbouring regions of space would visibly resemble each other.
    for (uint64 Index = 0; Index < 256; ++Index)
    {
        const uint64 A = UniverseHash::Mix64(Index);
        const uint64 B = UniverseHash::Mix64(Index + 1);
        uint64 Difference = A ^ B;
        int32 BitsChanged = 0;
        while (Difference != 0)
        {
            BitsChanged += static_cast<int32>(Difference & 1ull);
            Difference >>= 1;
        }
        // A good 64-bit mixer flips ~32 bits; anything under 8 indicates the
        // mixer is not avalanching and the constants are wrong.
        UVERIFY_TRUE(Result, BitsChanged >= 8);
    }

    // Argument order must matter: cell (3, 7, 0) is not cell (7, 3, 0).
    UVERIFY_TRUE(Result,
        UniverseHash::Hash(1, static_cast<int64>(3), static_cast<int64>(7))
        != UniverseHash::Hash(1, static_cast<int64>(7), static_cast<int64>(3)));

    // Negative and positive inputs must be distinguishable.
    UVERIFY_TRUE(Result,
        UniverseHash::Hash(1, static_cast<int64>(-5))
        != UniverseHash::Hash(1, static_cast<int64>(5)));

    // String hashing is stable and case-sensitive.
    UVERIFY_EQ_UINT(Result, UniverseHash::HashString(TEXT("andromeda")), UniverseHash::HashString(TEXT("andromeda")));
    UVERIFY_TRUE(Result, UniverseHash::HashString(TEXT("andromeda")) != UniverseHash::HashString(TEXT("Andromeda")));
    UVERIFY_TRUE(Result, UniverseHash::HashString(TEXT("a")) != UniverseHash::HashString(TEXT("b")));
    UVERIFY_TRUE(Result, UniverseHash::HashString(TEXT("")) == UniverseHash::HashString(TEXT("")));
    UVERIFY_TRUE(Result, UniverseHash::HashString(nullptr) == UniverseHash::HashString(TEXT("")));

    // ToUnitDouble must stay inside [0, 1) for extreme inputs, including all
    // bits set - a division-based implementation can return exactly 1.0 here,
    // which then indexes one past the end of a weight table.
    UVERIFY_TRUE(Result, UniverseHash::ToUnitDouble(0ull) == 0.0);
    UVERIFY_TRUE(Result, UniverseHash::ToUnitDouble(0xFFFFFFFFFFFFFFFFull) < 1.0);
    UVERIFY_TRUE(Result, UniverseHash::ToUnitDouble(0xFFFFFFFFFFFFFFFFull) > 0.999);

    return Result.Passed();
}

/** The PCG32 stream is reproducible and well distributed. */
bool UniverseTest_RandomStreamStability(FUniverseTestResult& Result)
{
    // Two generators built from the same seed emit the same sequence.
    {
        FUniverseRandom A(12345);
        FUniverseRandom B(12345);
        for (int32 Index = 0; Index < 512; ++Index)
        {
            UVERIFY_EQ_UINT(Result, A.NextUInt32(), B.NextUInt32());
        }
    }

    // Different seeds diverge immediately, including adjacent seeds - the
    // reason the constructor mixes the seed before using it as LCG state.
    {
        FUniverseRandom A(1000);
        FUniverseRandom B(1001);
        int32 Identical = 0;
        for (int32 Index = 0; Index < 64; ++Index)
        {
            if (A.NextUInt32() == B.NextUInt32())
            {
                ++Identical;
            }
        }
        UVERIFY_TRUE(Result, Identical <= 1);
    }

    // NextUnit stays in [0, 1).
    {
        FUniverseRandom Random(777);
        double Sum = 0.0;
        const int32 Samples = 20000;
        for (int32 Index = 0; Index < Samples; ++Index)
        {
            const double Value = Random.NextUnit();
            UVERIFY_TRUE(Result, Value >= 0.0 && Value < 1.0);
            Sum += Value;
        }
        // Mean of a uniform [0,1) sample; a broken generator (stuck bits,
        // biased shift) shows up here immediately.
        UVERIFY_NEAR(Result, Sum / static_cast<double>(Samples), 0.5, 0.02);
    }

    // NextIntInclusive covers its range and never escapes it. The bias a
    // modulo implementation introduces would show as a systematically
    // over-represented low bucket.
    {
        FUniverseRandom Random(31337);
        int32 Buckets[8] = {};
        const int32 Samples = 80000;
        for (int32 Index = 0; Index < Samples; ++Index)
        {
            const int32 Value = Random.NextIntInclusive(0, 7);
            UVERIFY_TRUE(Result, Value >= 0 && Value <= 7);
            ++Buckets[Value];
        }
        for (int32 Index = 0; Index < 8; ++Index)
        {
            const double Share = static_cast<double>(Buckets[Index]) / static_cast<double>(Samples);
            UVERIFY_NEAR(Result, Share, 0.125, 0.01);
        }
    }

    // A degenerate range returns the single valid value rather than looping.
    {
        FUniverseRandom Random(1);
        UVERIFY_EQ_INT(Result, Random.NextIntInclusive(5, 5), 5);
        UVERIFY_EQ_INT(Result, Random.NextIntInclusive(9, 3), 9);
    }

    // Weighted picking respects zero weights and stays in range.
    {
        FUniverseRandom Random(4242);
        const double Weights[4] = { 0.0, 1.0, 0.0, 3.0 };
        int32 Counts[4] = {};
        for (int32 Index = 0; Index < 20000; ++Index)
        {
            const int32 Picked = Random.PickWeighted(Weights, 4);
            UVERIFY_TRUE(Result, Picked >= 0 && Picked < 4);
            ++Counts[Picked];
        }
        UVERIFY_EQ_INT(Result, Counts[0], 0);
        UVERIFY_EQ_INT(Result, Counts[2], 0);
        UVERIFY_NEAR(Result, static_cast<double>(Counts[3]) / 20000.0, 0.75, 0.02);
    }

    // Sub-streams derived from one seed are independent of each other.
    {
        const FUniverseSeed Base(0xABCDEF0123456789ull);
        FUniverseRandom Physical(Base.Stream(UniverseSeedDomain::StreamPhysical).Value);
        FUniverseRandom Orbital(Base.Stream(UniverseSeedDomain::StreamOrbital).Value);
        int32 Identical = 0;
        for (int32 Index = 0; Index < 64; ++Index)
        {
            if (Physical.NextUInt32() == Orbital.NextUInt32())
            {
                ++Identical;
            }
        }
        UVERIFY_TRUE(Result, Identical <= 1);
    }

    return Result.Passed();
}

/** Descent through the hierarchy is a pure function of its address. */
bool UniverseTest_SeedHierarchyDeterminism(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = FUniverseSeedHierarchy::FromText(TEXT("sprint-001"));

    // The same text always yields the same root.
    const FUniverseSeedHierarchy Again = FUniverseSeedHierarchy::FromText(TEXT("sprint-001"));
    UVERIFY_EQ_UINT(Result, Hierarchy.GetUniverseSeedValue(), Again.GetUniverseSeedValue());

    // Different text yields a different root.
    const FUniverseSeedHierarchy Other = FUniverseSeedHierarchy::FromText(TEXT("sprint-002"));
    UVERIFY_TRUE(Result, Hierarchy.GetUniverseSeedValue() != Other.GetUniverseSeedValue());

    // An empty/missing seed falls back to the fixed default rather than to an
    // arbitrary value, so a config typo cannot silently relocate the universe.
    UVERIFY_EQ_UINT(Result,
        FUniverseSeedHierarchy::FromText(nullptr).GetUniverseSeedValue(),
        FUniverseSeedHierarchy::FromText(TEXT("")).GetUniverseSeedValue());

    // Sector seeds are reproducible and address-dependent. Re-deriving a
    // sector after visiting many others must give the identical value: this is
    // the property that lets the player leave a region and come back to find
    // it unchanged, with nothing stored in between.
    const FUniverseSeed First = Hierarchy.GetSectorSeed(-4, 17, 3);
    for (int64 Wander = 0; Wander < 500; ++Wander)
    {
        (void)Hierarchy.GetSectorSeed(Wander, Wander * 7, -Wander);
    }
    const FUniverseSeed Revisited = Hierarchy.GetSectorSeed(-4, 17, 3);
    UVERIFY_EQ_UINT(Result, First.Value, Revisited.Value);

    // Neighbouring sectors must differ, including across the origin.
    UVERIFY_TRUE(Result, Hierarchy.GetSectorSeed(0, 0, 0) != Hierarchy.GetSectorSeed(1, 0, 0));
    UVERIFY_TRUE(Result, Hierarchy.GetSectorSeed(0, 0, 0) != Hierarchy.GetSectorSeed(-1, 0, 0));
    UVERIFY_TRUE(Result, Hierarchy.GetSectorSeed(1, 0, 0) != Hierarchy.GetSectorSeed(0, 1, 0));
    UVERIFY_TRUE(Result, Hierarchy.GetSectorSeed(1, 2, 3) != Hierarchy.GetSectorSeed(3, 2, 1));

    // A different universe seed must give a different sector seed at the same
    // address - otherwise the root seed would not actually control the world.
    UVERIFY_TRUE(Result, Hierarchy.GetSectorSeed(5, 5, 5) != Other.GetSectorSeed(5, 5, 5));

    // Descent: system and body seeds depend on their index and their parent.
    const FUniverseSeed SectorSeed = Hierarchy.GetSectorSeed(2, -2, 2);
    UVERIFY_TRUE(Result,
        FUniverseSeedHierarchy::GetSystemSeed(SectorSeed, 0)
        != FUniverseSeedHierarchy::GetSystemSeed(SectorSeed, 1));

    const FUniverseSeed SystemSeed = FUniverseSeedHierarchy::GetSystemSeed(SectorSeed, 0);
    for (int32 Index = 0; Index < 32; ++Index)
    {
        for (int32 Other2 = Index + 1; Other2 < 32; ++Other2)
        {
            UVERIFY_TRUE(Result,
                FUniverseSeedHierarchy::GetBodySeed(SystemSeed, Index)
                != FUniverseSeedHierarchy::GetBodySeed(SystemSeed, Other2));
        }
    }

    // Two systems in different sectors, both at index 0, must differ.
    const FUniverseSeed OtherSectorSeed = Hierarchy.GetSectorSeed(2, -2, 3);
    UVERIFY_TRUE(Result,
        FUniverseSeedHierarchy::GetSystemSeed(SectorSeed, 0)
        != FUniverseSeedHierarchy::GetSystemSeed(OtherSectorSeed, 0));

    // GetSectorSeedForCell must agree with explicit sector arithmetic,
    // including for negative cells (floor, not truncate).
    const int64 SectorCells = UniverseScale::SectorSizeInCells;
    UVERIFY_EQ_UINT(Result,
        Hierarchy.GetSectorSeedForCell(-1, 0, 0).Value,
        Hierarchy.GetSectorSeed(-1, 0, 0).Value);
    UVERIFY_EQ_UINT(Result,
        Hierarchy.GetSectorSeedForCell(SectorCells * 3 + 17, 0, 0).Value,
        Hierarchy.GetSectorSeed(3, 0, 0).Value);

    // Surface patch seeds (consumed in Sprint 002) descend correctly today.
    const FUniverseSeed BodySeed = FUniverseSeedHierarchy::GetBodySeed(SystemSeed, 3);
    UVERIFY_TRUE(Result,
        FUniverseSeedHierarchy::GetSurfacePatchSeed(BodySeed, 0, 4, 10, 20)
        != FUniverseSeedHierarchy::GetSurfacePatchSeed(BodySeed, 1, 4, 10, 20));
    UVERIFY_TRUE(Result,
        FUniverseSeedHierarchy::GetSurfacePatchSeed(BodySeed, 0, 4, 10, 20)
        != FUniverseSeedHierarchy::GetSurfacePatchSeed(BodySeed, 0, 4, 20, 10));
    UVERIFY_EQ_UINT(Result,
        FUniverseSeedHierarchy::GetSurfacePatchSeed(BodySeed, 2, 5, 1, 1).Value,
        FUniverseSeedHierarchy::GetSurfacePatchSeed(BodySeed, 2, 5, 1, 1).Value);

    return Result.Passed();
}

/**
 * Domain separation.
 *
 * Without domain tags, a sector seed and a system seed derived from the same
 * numeric address would be the same value, and structure at one level of the
 * hierarchy would be visibly mirrored at another. This test pins the tags in
 * place: it fails if anyone removes a domain constant from a derivation.
 */
bool UniverseTest_SeedHierarchyDomainSeparation(FUniverseTestResult& Result)
{
    const uint64 Base = 0x1234567890ABCDEFull;

    // Every domain tag must be distinct.
    const uint64 Domains[] = {
        UniverseSeedDomain::Universe,
        UniverseSeedDomain::Sector,
        UniverseSeedDomain::System,
        UniverseSeedDomain::Body,
        UniverseSeedDomain::Surface,
        UniverseSeedDomain::Feature,
    };
    const int32 DomainCount = static_cast<int32>(sizeof(Domains) / sizeof(Domains[0]));
    for (int32 Index = 0; Index < DomainCount; ++Index)
    {
        for (int32 Other = Index + 1; Other < DomainCount; ++Other)
        {
            UVERIFY_TRUE(Result, Domains[Index] != Domains[Other]);
            UVERIFY_TRUE(Result,
                UniverseHash::Hash(Base, Domains[Index]) != UniverseHash::Hash(Base, Domains[Other]));
        }
    }

    // The same integer address at two different levels must not collide.
    const FUniverseSeedHierarchy Hierarchy(Base);
    const FUniverseSeed AsSector = Hierarchy.GetSectorSeed(1, 0, 0);

    const FUniverseSeed Parent(Base);
    const FUniverseSeed AsSystem = FUniverseSeedHierarchy::GetSystemSeed(Parent, 1);
    const FUniverseSeed AsBody = FUniverseSeedHierarchy::GetBodySeed(Parent, 1);

    UVERIFY_TRUE(Result, AsSector != AsSystem);
    UVERIFY_TRUE(Result, AsSector != AsBody);
    UVERIFY_TRUE(Result, AsSystem != AsBody);

    // Sub-streams within one object must be distinct from each other and from
    // the object seed itself.
    const FUniverseSeed Object(0xFEEDFACECAFEBEEFull);
    const uint64 Streams[] = {
        UniverseSeedDomain::StreamPrimary,
        UniverseSeedDomain::StreamName,
        UniverseSeedDomain::StreamPhysical,
        UniverseSeedDomain::StreamOrbital,
        UniverseSeedDomain::StreamSurface,
    };
    const int32 StreamCount = static_cast<int32>(sizeof(Streams) / sizeof(Streams[0]));
    for (int32 Index = 0; Index < StreamCount; ++Index)
    {
        UVERIFY_TRUE(Result, Object.Stream(Streams[Index]) != Object);
        for (int32 Other = Index + 1; Other < StreamCount; ++Other)
        {
            UVERIFY_TRUE(Result, Object.Stream(Streams[Index]) != Object.Stream(Streams[Other]));
        }
    }

    // The root is domain-tagged: the hierarchy's universe seed must not be the
    // raw value handed to the constructor.
    UVERIFY_TRUE(Result, Hierarchy.GetUniverseSeedValue() != Base);

    return Result.Passed();
}
