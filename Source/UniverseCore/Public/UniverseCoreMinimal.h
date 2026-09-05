// Copyright Universe Project. All Rights Reserved.
#pragma once

/**
 * UniverseCoreMinimal.h
 *
 * Single include boundary for the deterministic core.
 *
 * UniverseCore contains the canonical mathematics of the project: universe
 * coordinates, stable hashing, deterministic RNG and the seed hierarchy. That
 * mathematics is the one part of the codebase that MUST be verifiable without
 * booting an editor, so the same translation units compile in two modes:
 *
 *   1. Inside Unreal (default) - against CoreMinimal.h, as a normal module.
 *   2. Standalone (UNIVERSE_STANDALONE=1) - against a small shim providing the
 *      handful of Unreal types the core actually uses, so the sources can be
 *      compiled and executed by a plain C++ compiler.
 *
 * Mode 2 exists ONLY as a verification harness (Tools/StandaloneTests). It is
 * never shipped and the runtime never depends on it. See
 * Docs/Architecture/Testing.md.
 *
 * The core deliberately restricts itself to: integral types, double, FVector3d,
 * FString, TArray and FMath. Nothing here may touch UObject, Actor lifetimes,
 * engine singletons or anything else non-deterministic.
 */

// The shim lives in Tools/StandaloneTests/Shim, outside any module, so that
// UnrealBuildTool and UnrealHeaderTool never see it. It is on the include path
// only for the standalone harness build.
#if defined(UNIVERSE_STANDALONE) && UNIVERSE_STANDALONE
    #include "UnrealShim.h"
#else
    #include "CoreMinimal.h"
#endif
