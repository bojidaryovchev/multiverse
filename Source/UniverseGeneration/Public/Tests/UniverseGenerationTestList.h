// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "Tests/UniverseTestFramework.h"

/**
 * The registry of UniverseGeneration test bodies. Same X-macro arrangement as
 * UniverseCoreTestList.h so both runners stay in step.
 */
#define UNIVERSE_GENERATION_TEST_LIST(X) \
    X(SystemGenerationDeterminism) \
    X(SystemGenerationDifferentSeeds) \
    X(SystemGenerationOrderIndependence) \
    X(SystemIdStabilityAndSerialization) \
    X(SystemPhysicalPlausibility) \
    X(PlanetPlacementDeterminism) \
    X(SectorPopulationStatistics) \
    X(ProximityQueryDeterminism) \
    X(LeaveAndReturnReproduction) \
    X(GalaxyGenerationDeterminism) \
    X(MultiGalaxyIdentity) \
    X(GalaxyLocalRoundTrip) \
    X(GalaxyDensityInvariants) \
    X(GalacticStellarDensityBridge) \
    X(SectorIsolation) \
    X(InterstellarMovementIntegration) \
    X(TravelSectorTraversal) \
    X(TravelBodyIntersection) \
    X(TravelWarpOvershoot) \
    X(TravelCacheBounds) \
    X(TravelEstimatesAndModes)

#define UNIVERSE_DECLARE_GENERATION_TEST(Name) \
    UNIVERSEGENERATION_API bool UniverseTest_##Name(FUniverseTestResult& Result);

UNIVERSE_GENERATION_TEST_LIST(UNIVERSE_DECLARE_GENERATION_TEST)

#undef UNIVERSE_DECLARE_GENERATION_TEST
