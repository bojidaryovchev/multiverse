// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "StarSystemDescriptor.h"
#include "UniverseSeed.h"

/**
 * FStarSystemGenerator
 *
 * Turns (universe seed, sector address, index) into a star system.
 *
 * Every function is static and pure. There is no cache, no registry and no
 * "world" object, because the generator must be callable from anywhere,
 * including several threads at once, without two callers being able to
 * influence each other's results. Caching belongs in a layer above this one,
 * where it can be measured and dropped without changing what gets generated.
 *
 * The generation contract, which the tests in
 * Source/UniverseGeneration/Private/Tests enforce:
 *
 *   - Same inputs, same output, byte for byte, forever.
 *   - No dependence on generation order, thread, call count or elapsed time.
 *   - No dependence on anything with process lifetime (pointers, UObject IDs,
 *     FName indices, hash-map iteration order).
 *   - Each generated aspect draws from its own seed stream, so adding a new
 *     property to a planet does not shift the values of the existing ones.
 */
class UNIVERSEGENERATION_API FStarSystemGenerator
{
public:
    /**
     * How many systems exist in the given sector.
     *
     * A sector is ~4.87 ly on a side and the solar-neighbourhood stellar
     * density is about 0.004 stars/ly^3, so the expectation is ~0.46 stars per
     * sector. The distribution below averages 0.47, which puts stars a few
     * light years apart on average - recognisably like the real local
     * neighbourhood rather than an arbitrary lattice.
     */
    static int32 GetSystemCountInSector(
        const FUniverseSeedHierarchy& Hierarchy,
        int64 SectorX, int64 SectorY, int64 SectorZ);

    /** The stable identity of a system, without generating it. */
    static FUniverseSystemId MakeSystemId(int64 SectorX, int64 SectorY, int64 SectorZ, int32 IndexInSector);

    /**
     * Where a system sits, without generating the rest of it.
     *
     * Separated from full generation because proximity queries need to test
     * many candidate positions and only expand the few that are close - the
     * difference between examining a neighbourhood and building it.
     */
    static FUniversePosition GetSystemPosition(
        const FUniverseSeedHierarchy& Hierarchy,
        int64 SectorX, int64 SectorY, int64 SectorZ, int32 IndexInSector);

    /**
     * Where a planet sits, in canonical universe coordinates.
     *
     * Lives here rather than in the rendering actor because the game mode, the
     * actor and any future navigation code must all agree on where a planet
     * is. Two implementations of "the planet is over there" is exactly the kind
     * of duplication that drifts apart and produces a world where the marker
     * and the object disagree.
     *
     * A circular orbit in the system plane, tilted by the inclination and
     * placed at the epoch phase. Not an ephemeris - orbits do not yet advance
     * with time - but deterministic and shared.
     */
    static FUniversePosition GetPlanetPosition(
        const FStarSystemDescriptor& System,
        const FPlanetDescriptor& Planet);

    /**
     * Generates a complete system. Returns false if IndexInSector is not less
     * than GetSystemCountInSector for that sector, i.e. the system does not
     * exist.
     */
    static bool GenerateSystem(
        const FUniverseSeedHierarchy& Hierarchy,
        int64 SectorX, int64 SectorY, int64 SectorZ, int32 IndexInSector,
        FStarSystemDescriptor& OutSystem);

    /** Convenience overload taking an identity. */
    static bool GenerateSystem(
        const FUniverseSeedHierarchy& Hierarchy,
        const FUniverseSystemId& Id,
        FStarSystemDescriptor& OutSystem);

    /**
     * Finds every system whose position is within RadiusLightYears of Centre,
     * by scanning the sectors that the sphere touches.
     *
     * Results are ordered by (sector X, Y, Z, index) - a fixed order derived
     * from the address, never from distance or from the order sectors happened
     * to be visited, so the returned array is itself deterministic.
     *
     * MaxResults bounds the work; the scan stops cleanly rather than
     * truncating mid-sector.
     */
    static void FindSystemsWithin(
        const FUniverseSeedHierarchy& Hierarchy,
        const FUniversePosition& Centre,
        double RadiusLightYears,
        TArray<FStarSystemDescriptor>& OutSystems,
        int32 MaxResults = 64);

    /**
     * The nearest system to a point, searching outward in sector shells.
     * Returns false if nothing is found within MaxRadiusLightYears.
     */
    static bool FindNearestSystem(
        const FUniverseSeedHierarchy& Hierarchy,
        const FUniversePosition& Centre,
        double MaxRadiusLightYears,
        FStarSystemDescriptor& OutSystem);

private:
    static void GenerateStar(const FUniverseSeed& SystemSeed, FStarDescriptor& OutStar);

    static void GeneratePlanets(
        const FUniverseSeed& SystemSeed,
        const FStarDescriptor& Star,
        TArray<FPlanetDescriptor>& OutPlanets);

    static FString GenerateName(const FUniverseSeed& NameSeed);
};
