// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "Tests/UniverseTestFramework.h"

/**
 * UniverseCoreTestList.h
 *
 * The single registry of UniverseCore test bodies, as an X-macro.
 *
 * Both runners expand this list: the Unreal automation wrapper generates one
 * IMPLEMENT_SIMPLE_AUTOMATION_TEST per entry, and the standalone runner builds
 * a dispatch table from it. A new test is therefore added in exactly one place
 * and cannot end up running in one harness but not the other - which is the
 * failure mode that quietly erodes a dual-runner setup.
 */
#define UNIVERSE_CORE_TEST_LIST(X) \
    X(ScaleConstants) \
    X(NormalizationBasic) \
    X(NormalizationNegative) \
    X(CellBoundaryExact) \
    X(CellBoundaryNeighbourhood) \
    X(LargeDisplacementAccumulation) \
    X(LocalPrecisionAtExtremeCoordinates) \
    X(RelativeAndDistance) \
    X(CellOffsetJumps) \
    X(SectorAddressing) \
    X(PositionSerializationRoundTrip) \
    X(PositionEqualityAndHash) \
    X(HashStability) \
    X(RandomStreamStability) \
    X(SeedHierarchyDeterminism) \
    X(SeedHierarchyDomainSeparation)

/** Declares bool UniverseTest_<Name>(FUniverseTestResult&) for every entry. */
#define UNIVERSE_DECLARE_TEST(Name) \
    UNIVERSECORE_API bool UniverseTest_##Name(FUniverseTestResult& Result);

UNIVERSE_CORE_TEST_LIST(UNIVERSE_DECLARE_TEST)

#undef UNIVERSE_DECLARE_TEST
