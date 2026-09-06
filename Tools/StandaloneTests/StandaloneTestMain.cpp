// Copyright Universe Project. All Rights Reserved.

// ---------------------------------------------------------------------------
// StandaloneTestMain.cpp
//
// Runs the UniverseCore and UniverseGeneration test bodies without Unreal.
//
// The same test functions run inside the editor through the Unreal automation
// wrappers; this executable exists so that the deterministic core - the part
// the entire universe is reconstructed from - can be verified in a two-second
// compile-and-run loop, on a machine with no engine installed, and in CI.
//
// Build and run:  Tools\StandaloneTests\RunTests.bat
// ---------------------------------------------------------------------------

#include "UniverseCoreMinimal.h"
#include "Tests/UniverseCoreTestList.h"
#include "Tests/UniverseGenerationTestList.h"
#include "Tests/UniversePlanetTestList.h"

#include "StarSystemGenerator.h"
#include "UniverseCoordinates.h"
#include "UniverseScale.h"

#include <cstdio>
#include <cstring>

namespace
{
    struct FTestEntry
    {
        const char* Name;
        bool (*Function)(FUniverseTestResult&);
    };

    const FTestEntry Tests[] =
    {
#define UNIVERSE_TEST_ENTRY(Name) { #Name, &UniverseTest_##Name },
        UNIVERSE_CORE_TEST_LIST(UNIVERSE_TEST_ENTRY)
        UNIVERSE_GENERATION_TEST_LIST(UNIVERSE_TEST_ENTRY)
        UNIVERSE_PLANET_TEST_LIST(UNIVERSE_TEST_ENTRY)
#undef UNIVERSE_TEST_ENTRY
    };

    constexpr int TestCount = static_cast<int>(sizeof(Tests) / sizeof(Tests[0]));

    /**
     * Prints one worked example of the architecture in action, so that a test
     * run doubles as evidence that the numbers are what the documentation
     * claims. Purely informational - it asserts nothing.
     */
    void PrintArchitectureSummary()
    {
        std::printf("\n--- Universe scale ---------------------------------------------------\n");
        std::printf("  Cell size            : 2^%d cm = %.0f cm = %.3f AU\n",
            UniverseScale::CellShift, UniverseScale::CellSizeCmD, UniverseScale::AuPerCell);
        std::printf("  Sector size          : 2^%d cells = %.4f light years\n",
            UniverseScale::SectorShiftInCells, UniverseScale::SectorSizeLightYears);
        std::printf("  Local resolution     : %.6g cm (%.3f micrometres)\n",
            UniverseScale::LocalResolutionCm, UniverseScale::LocalResolutionCm * 1.0e4);

        const double HalfExtentLy =
            9223372036854775808.0 * UniverseScale::CellSizeCmD / UniverseScale::CmPerLightYear;
        std::printf("  Universe half-extent : %.4g light years per axis\n", HalfExtentLy);
        std::printf("                         (observable universe radius is ~4.65e10 ly)\n");
    }

    /** Generates and prints one system, as a readable smoke test of Phase D. */
    void PrintSampleSystem()
    {
        const FUniverseSeedHierarchy Hierarchy = FUniverseSeedHierarchy::FromText(TEXT("sprint-001"));

        std::printf("\n--- Sample deterministic generation (seed \"sprint-001\") --------------\n");
        std::printf("  Universe seed        : 0x%016llX\n",
            static_cast<unsigned long long>(Hierarchy.GetUniverseSeedValue()));

        int Printed = 0;
        for (int64 X = 0; X < 40 && Printed < 3; ++X)
        {
            for (int64 Y = 0; Y < 40 && Printed < 3; ++Y)
            {
                const int32 Count = FStarSystemGenerator::GetSystemCountInSector(Hierarchy, X, Y, 0);
                for (int32 Index = 0; Index < Count && Printed < 3; ++Index)
                {
                    FStarSystemDescriptor System;
                    if (!FStarSystemGenerator::GenerateSystem(Hierarchy, X, Y, 0, Index, System))
                    {
                        continue;
                    }
                    std::printf("\n  %s", System.ToDebugString().ToUtf8().c_str());
                    std::printf("  content hash         : 0x%016llX\n",
                        static_cast<unsigned long long>(System.GetContentHash()));
                    ++Printed;
                }
            }
        }
    }

    /**
     * Demonstrates the large-distance traversal claim numerically: accumulate
     * a very large journey in small steps and show that local coordinates
     * never leave their cell while the global position advances correctly.
     */
    void PrintTraversalProof()
    {
        std::printf("\n--- Large-distance traversal (Phase F, numeric) ----------------------\n");

        FUniversePosition Position;
        const FUniversePosition Start = Position;

        // 0.25 c for 100,000 steps of 1/60 s: about 41 light-seconds of travel
        // per second of simulated time, repeated until we cross many cells.
        const double StepCm = 4.0e12;   // ~40 million km per step
        const int Steps = 250000;

        double MaxLocalSeen = 0.0;
        for (int Index = 0; Index < Steps; ++Index)
        {
            Position = Position.OffsetByCm(FVector3d(StepCm, StepCm * 0.5, -StepCm * 0.25));
            const double Largest = FMath::Max(FMath::Abs(Position.Local.X),
                FMath::Max(FMath::Abs(Position.Local.Y), FMath::Abs(Position.Local.Z)));
            MaxLocalSeen = FMath::Max(MaxLocalSeen, Largest);
        }

        std::printf("  Steps                : %d\n", Steps);
        std::printf("  Distance travelled   : %.6g light years\n",
            FUniversePosition::DistanceLightYears(Start, Position));
        std::printf("  Cells crossed (X)    : %lld\n", static_cast<long long>(Position.CellX));
        std::printf("  Max |local| observed : %.6g cm  (cell size %.6g cm)\n",
            MaxLocalSeen, UniverseScale::CellSizeCmD);
        std::printf("  Local stayed in cell : %s\n",
            (MaxLocalSeen < UniverseScale::CellSizeCmD) ? "yes" : "NO");

        // Return journey must be exact.
        for (int Index = 0; Index < Steps; ++Index)
        {
            Position = Position.OffsetByCm(FVector3d(-StepCm, -StepCm * 0.5, StepCm * 0.25));
        }
        std::printf("  Round trip exact     : %s\n", (Position == Start) ? "yes" : "NO");
    }
}

int main(int argc, char** argv)
{
    // Unbuffered, so that a crash inside a test still leaves behind the name of
    // the test that was running. A buffered run that faults prints nothing at
    // all, which turns a five-second diagnosis into a bisection.
    std::setvbuf(stdout, nullptr, _IONBF, 0);

    bool bVerbose = false;
    for (int Index = 1; Index < argc; ++Index)
    {
        if (std::strcmp(argv[Index], "--verbose") == 0 || std::strcmp(argv[Index], "-v") == 0)
        {
            bVerbose = true;
        }
    }

    std::printf("=====================================================================\n");
    std::printf(" Universe - deterministic core verification (standalone, no engine)\n");
    std::printf("=====================================================================\n\n");

    int PassedTests = 0;
    int FailedTests = 0;
    int TotalChecks = 0;
    int TotalFailures = 0;

    for (int Index = 0; Index < TestCount; ++Index)
    {
        FUniverseTestResult Result;

        if (bVerbose)
        {
            std::printf("  [ .... ] %s\n", Tests[Index].Name);
        }

        Tests[Index].Function(Result);

        TotalChecks += Result.Checks;
        TotalFailures += Result.Failures;

        if (Result.Passed())
        {
            ++PassedTests;
            std::printf("  [ PASS ] %-40s %6d checks\n", Tests[Index].Name, Result.Checks);
        }
        else
        {
            ++FailedTests;
            std::printf("  [ FAIL ] %-40s %6d checks, %d failed\n",
                Tests[Index].Name, Result.Checks, Result.Failures);

            // Cap the printed failures: a systematically broken invariant can
            // produce thousands of identical lines and bury everything else.
            const int32 MaxShown = bVerbose ? Result.FailureMessages.Num() : 10;
            for (int32 Message = 0; Message < Result.FailureMessages.Num() && Message < MaxShown; ++Message)
            {
                std::printf("             %s\n", Result.FailureMessages[Message].ToUtf8().c_str());
            }
            if (Result.FailureMessages.Num() > MaxShown)
            {
                std::printf("             ... %d more (run with --verbose)\n",
                    Result.FailureMessages.Num() - MaxShown);
            }
        }
    }

    std::printf("\n---------------------------------------------------------------------\n");
    std::printf("  %d/%d tests passed, %d/%d assertions passed\n",
        PassedTests, TestCount, TotalChecks - TotalFailures, TotalChecks);
    std::printf("---------------------------------------------------------------------\n");

    if (FailedTests == 0)
    {
        PrintArchitectureSummary();
        PrintSampleSystem();
        PrintTraversalProof();
    }

    std::printf("\n%s\n", (FailedTests == 0) ? "RESULT: ALL TESTS PASSED" : "RESULT: FAILURES PRESENT");

    return (FailedTests == 0) ? 0 : 1;
}
