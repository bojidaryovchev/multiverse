// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniverseGenerationTestList.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

/**
 * Unreal automation wrappers around the shared UniverseGeneration test bodies.
 * See UniverseCoreAutomationTests.cpp for the rationale.
 *
 * Run headless:
 *   UnrealEditor-Cmd.exe <path>\Universe.uproject -ExecCmds="Automation RunTests Universe.Generation; Quit" -unattended -nopause -nullrhi -log
 */

#define UNIVERSE_IMPLEMENT_GENERATION_TEST(Name)                                        \
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(                                                   \
        FUniverseGenerationAutomation##Name,                                            \
        "Universe.Generation." #Name,                                                   \
        EAutomationTestFlags::EditorContext                                             \
            | EAutomationTestFlags::ClientContext                                       \
            | EAutomationTestFlags::EngineFilter)                                       \
                                                                                        \
    bool FUniverseGenerationAutomation##Name::RunTest(const FString& Parameters)        \
    {                                                                                   \
        FUniverseTestResult Result;                                                     \
        UniverseTest_##Name(Result);                                                    \
                                                                                        \
        for (const FString& Message : Result.FailureMessages)                           \
        {                                                                               \
            AddError(Message);                                                          \
        }                                                                               \
                                                                                        \
        AddInfo(FString::Printf(TEXT("%d assertions, %d failed"),                       \
            Result.Checks, Result.Failures));                                           \
                                                                                        \
        if (Result.Checks == 0)                                                         \
        {                                                                               \
            AddError(TEXT("Test body executed no assertions."));                        \
            return false;                                                               \
        }                                                                               \
                                                                                        \
        return Result.Passed();                                                         \
    }

UNIVERSE_GENERATION_TEST_LIST(UNIVERSE_IMPLEMENT_GENERATION_TEST)

#undef UNIVERSE_IMPLEMENT_GENERATION_TEST

#endif  // WITH_DEV_AUTOMATION_TESTS
