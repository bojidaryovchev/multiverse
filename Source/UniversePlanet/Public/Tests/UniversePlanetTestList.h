// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "Tests/UniverseTestFramework.h"

/**
 * The registry of UniversePlanet test bodies. Same X-macro arrangement as the
 * other modules, so the standalone runner and the Unreal automation runner
 * stay in step automatically.
 */
#define UNIVERSE_PLANET_TEST_LIST(X) \
    X(CubeSphereFaceBasis) \
    X(CubeSphereRoundTrip) \
    X(CubeFaceAdjacencyTable) \
    X(CubeFaceSeamsExact) \
    X(CubeSeamDyadicRequirement) \
    X(CubeCornersExact)

#define UNIVERSE_DECLARE_PLANET_TEST(Name) \
    UNIVERSEPLANET_API bool UniverseTest_##Name(FUniverseTestResult& Result);

UNIVERSE_PLANET_TEST_LIST(UNIVERSE_DECLARE_PLANET_TEST)

#undef UNIVERSE_DECLARE_PLANET_TEST
