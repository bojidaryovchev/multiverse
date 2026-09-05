// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"

/**
 * UniverseHash.h
 *
 * Stable 64-bit hashing. "Stable" here has a precise meaning: the same inputs
 * produce the same output on every machine, in every build configuration, in
 * every engine version, forever. The entire universe is reconstructed from
 * these values, so changing one is equivalent to destroying and regenerating
 * the universe.
 *
 * Consequently:
 *
 *  - Only unsigned 64-bit integer operations are used. Integer arithmetic is
 *    exactly specified by the language; floating point is not (x87 excess
 *    precision, FMA contraction and fast-math flags all change results), so no
 *    float ever participates in a hash.
 *  - Nothing derived from the process is admissible as input: no pointers, no
 *    UObject IDs, no FName indices, no iteration order of a hash map, no time.
 *    Docs/Architecture/ProceduralGeneration.md lists the banned inputs.
 *  - The mixing function is SplitMix64, whose constants are published and
 *    reproducible rather than invented here.
 */
namespace UniverseHash
{
    /**
     * SplitMix64 finaliser (Steele, Lea & Flood). A bijection on the 64-bit
     * space with good avalanche: a single input bit change flips about half
     * the output bits, which is what lets us derive many independent-looking
     * values from one seed.
     */
    inline uint64 Mix64(uint64 Value)
    {
        Value += 0x9E3779B97F4A7C15ull;                 // 2^64 / golden ratio
        Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
        Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
        return Value ^ (Value >> 31);
    }

    /**
     * Folds one value into a running hash.
     *
     * Note the rotation of the accumulator: without it, Combine(a, b) and
     * Combine(b, a) would be much closer than they should be, and coordinate
     * hashing is exactly the case where argument order carries meaning
     * (cell (3, 7, 0) must not collide with cell (7, 3, 0)).
     */
    inline uint64 Combine(uint64 Accumulator, uint64 Value)
    {
        const uint64 Rotated = (Accumulator << 27) | (Accumulator >> 37);
        return Mix64(Rotated ^ Mix64(Value));
    }

    /** Signed values enter the hash by their two's complement bit pattern. */
    inline uint64 Combine(uint64 Accumulator, int64 Value)
    {
        return Combine(Accumulator, static_cast<uint64>(Value));
    }

    inline uint64 Combine(uint64 Accumulator, int32 Value)
    {
        return Combine(Accumulator, static_cast<uint64>(static_cast<int64>(Value)));
    }

    inline uint64 Combine(uint64 Accumulator, uint32 Value)
    {
        return Combine(Accumulator, static_cast<uint64>(Value));
    }

    /** Variadic convenience: Hash(Seed, A, B, C) == Combine(Combine(Combine(...))). */
    template <typename TFirst, typename... TRest>
    inline uint64 Hash(uint64 Seed, TFirst First, TRest... Rest)
    {
        const uint64 Next = Combine(Seed, First);
        if constexpr (sizeof...(Rest) == 0)
        {
            return Next;
        }
        else
        {
            return Hash(Next, Rest...);
        }
    }

    /**
     * FNV-1a over the UTF-16 code units of a string, then mixed.
     *
     * Used to turn a human-typed universe seed ("andromeda") into a uint64.
     * Deliberately hashes code units rather than an encoded byte sequence so
     * the result does not depend on the platform's notion of wchar_t width or
     * on a locale-dependent conversion.
     */
    UNIVERSECORE_API uint64 HashString(const TCHAR* Text);

    /**
     * Hashes a compile-time domain tag. Domain separation is what stops a
     * sector seed from ever colliding with a system seed derived from the same
     * numbers; see FUniverseSeed.
     */
    inline uint64 Domain(uint64 DomainId, uint64 Seed)
    {
        return Combine(Seed, DomainId);
    }

    /**
     * Uniform double in [0, 1) from a 64-bit hash.
     *
     * Takes the top 53 bits and scales by 2^-53. Both the integer-to-double
     * conversion (the value fits exactly in the mantissa) and the scaling (a
     * power of two) are exact, so this mapping is bit-identical on every
     * IEEE-754 platform. Do not replace it with a division by UINT64_MAX,
     * which rounds and is not.
     */
    inline double ToUnitDouble(uint64 Hash)
    {
        return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);  // 2^-53
    }
}
