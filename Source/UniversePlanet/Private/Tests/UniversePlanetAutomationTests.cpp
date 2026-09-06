// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

/**
 * Unreal automation wrappers around the shared UniversePlanet test bodies.
 * See UniverseCoreAutomationTests.cpp for the rationale.
 *
 * Run headless:
 *   UnrealEditor-Cmd.exe <path>\Universe.uproject -ExecCmds="Automation RunTests Universe.Planet; Quit" -unattended -nopause -nullrhi -log
 */

#define UNIVERSE_IMPLEMENT_PLANET_TEST(Name)                                            \
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(                                                   \
        FUniversePlanetAutomation##Name,                                                \
        "Universe.Planet." #Name,                                                       \
        EAutomationTestFlags::EditorContext                                             \
            | EAutomationTestFlags::ClientContext                                       \
            | EAutomationTestFlags::EngineFilter)                                       \
                                                                                        \
    bool FUniversePlanetAutomation##Name::RunTest(const FString& Parameters)            \
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
        if (Result.Checks == 0)                                                          \
        {                                                                               \
            AddError(TEXT("Test body executed no assertions."));                        \
            return false;                                                               \
        }                                                                               \
                                                                                        \
        return Result.Passed();                                                         \
    }

UNIVERSE_PLANET_TEST_LIST(UNIVERSE_IMPLEMENT_PLANET_TEST)

#undef UNIVERSE_IMPLEMENT_PLANET_TEST

#endif  // WITH_DEV_AUTOMATION_TESTS
