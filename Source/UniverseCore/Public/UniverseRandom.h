// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseHash.h"

/**
 * FUniverseRandom
 *
 * Deterministic pseudo-random stream for procedural generation.
 *
 * This is deliberately NOT FRandomStream or std::mt19937:
 *
 *  - FRandomStream is an engine type whose algorithm Epic may change between
 *    versions. A universe that regenerates differently after an engine upgrade
 *    is not a persistent universe.
 *  - The std:: distributions (uniform_real_distribution, normal_distribution)
 *    are explicitly not required to produce identical output across standard
 *    library implementations, so they cannot be used for anything the world is
 *    reconstructed from.
 *
 * The generator is PCG-XSH-RR 64/32 (O'Neill): a 64-bit LCG state with an
 * output permutation. Small, fast, statistically strong, and specified by
 * published constants so the sequence is reproducible from the algorithm alone.
 *
 * Streams are cheap. The correct pattern is to construct a fresh generator
 * from a seed derived through FUniverseSeed for each independent decision,
 * rather than threading one generator through the whole generator chain -
 * a shared generator makes every result depend on the order and count of every
 * preceding draw, which is how procedural generation silently loses stability
 * when someone adds a feature.
 */
struct UNIVERSECORE_API FUniverseRandom
{
public:
    explicit FUniverseRandom(uint64 InSeed)
    {
        // Run the seed through the mixer first: adjacent seeds (as produced by
        // an index loop) would otherwise start the LCG in adjacent states and
        // yield visibly correlated first draws.
        State = UniverseHash::Mix64(InSeed);
        Increment = (UniverseHash::Mix64(InSeed ^ 0xDA3E39CB94B95BDBull) << 1) | 1ull;  // must be odd
    }

    /** Next raw 32 bits. */
    uint32 NextUInt32()
    {
        const uint64 Previous = State;
        State = Previous * 6364136223846793005ull + Increment;
        const uint32 Xorshifted = static_cast<uint32>(((Previous >> 18) ^ Previous) >> 27);
        const uint32 Rotation = static_cast<uint32>(Previous >> 59);
        return (Xorshifted >> Rotation) | (Xorshifted << ((~Rotation + 1u) & 31u));
    }

    uint64 NextUInt64()
    {
        const uint64 High = static_cast<uint64>(NextUInt32()) << 32;
        return High | static_cast<uint64>(NextUInt32());
    }

    /**
     * Uniform double in [0, 1). Exact scaling by a power of two - see
     * UniverseHash::ToUnitDouble for why this must not be a division.
     */
    double NextUnit()
    {
        return static_cast<double>(NextUInt64() >> 11) * (1.0 / 9007199254740992.0);
    }

    /** Uniform double in [Min, Max). */
    double NextRange(double Min, double Max)
    {
        return Min + NextUnit() * (Max - Min);
    }

    /**
     * Uniform integer in [Min, Max] inclusive.
     *
     * Uses Lemire's multiply-shift reduction rather than a modulo. Modulo
     * introduces bias toward low values when the range does not divide 2^32,
     * which for something like "number of planets" would be a visible
     * statistical artefact across a universe of billions of systems.
     */
    int32 NextIntInclusive(int32 Min, int32 Max)
    {
        if (Max <= Min)
        {
            return Min;
        }
        const uint32 Span = static_cast<uint32>(Max - Min) + 1u;
        const uint64 Product = static_cast<uint64>(NextUInt32()) * static_cast<uint64>(Span);
        return Min + static_cast<int32>(Product >> 32);
    }

    /** True with the given probability. */
    bool NextBool(double Probability)
    {
        return NextUnit() < Probability;
    }

    /**
     * Standard normal deviate via the Box-Muller transform.
     *
     * Used for physically-motivated quantities (stellar mass scatter, orbital
     * eccentricity) where a uniform draw would look artificial. Note this calls
     * log and sqrt: libm is not bit-identical across platforms, so values from
     * this function must never feed a hash or an identity. They are safe for
     * descriptor payloads, which are regenerated locally on each machine from
     * integer seeds.
     */
    double NextGaussian()
    {
        // Guard against log(0), which the [0,1) range makes reachable.
        const double U1 = FMath::Max(NextUnit(), 2.3283064365386963e-10);  // 2^-32
        const double U2 = NextUnit();
        const double Radius = FMath::Sqrt(-2.0 * FMath::Loge(U1));
        return Radius * FMath::Cos(6.283185307179586476925286766559 * U2);
    }

    /**
     * A point drawn uniformly inside the unit cube, per-axis independent.
     * Used for placing objects within a cell or sector.
     */
    FVector3d NextPointInUnitCube()
    {
        const double X = NextUnit();
        const double Y = NextUnit();
        const double Z = NextUnit();
        return FVector3d(X, Y, Z);
    }

    /**
     * Picks an index from a weight table. Weights need not sum to one.
     * Returns the last index if the table is empty or all weights are zero,
     * so a caller can never get an out-of-range index from malformed data.
     */
    int32 PickWeighted(const double* Weights, int32 Count)
    {
        if (Count <= 0)
        {
            return 0;
        }
        double Total = 0.0;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Total += FMath::Max(Weights[Index], 0.0);
        }
        if (Total <= 0.0)
        {
            return Count - 1;
        }
        double Roll = NextUnit() * Total;
        for (int32 Index = 0; Index < Count; ++Index)
        {
            Roll -= FMath::Max(Weights[Index], 0.0);
            if (Roll < 0.0)
            {
                return Index;
            }
        }
        return Count - 1;
    }

private:
    uint64 State = 0;
    uint64 Increment = 1;
};
