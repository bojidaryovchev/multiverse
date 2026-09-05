// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniverseCoreTestList.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

/**
 * Unreal automation wrappers around the shared UniverseCore test bodies.
 *
 * The bodies themselves live in UniverseCoordinateTests.cpp and
 * UniverseSeedTests.cpp and know nothing about the automation framework, which
 * is what lets Tools/StandaloneTests run the identical assertions with no
 * engine present. This file only adapts them to the editor's test runner.
 *
 * Run from the editor:   Tools > Session Frontend > Automation > Universe.Core
 * Run headless:
 *   UnrealEditor-Cmd.exe <path>\Universe.uproject -ExecCmds="Automation RunTests Universe.Core; Quit" -unattended -nopause -nullrhi -log
 */

#define UNIVERSE_IMPLEMENT_CORE_TEST(Name)                                              \
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(                                                   \
        FUniverseCoreAutomation##Name,                                                  \
        "Universe.Core." #Name,                                                         \
        EAutomationTestFlags::EditorContext                                             \
            | EAutomationTestFlags::ClientContext                                       \
            | EAutomationTestFlags::EngineFilter)                                       \
                                                                                        \
    bool FUniverseCoreAutomation##Name::RunTest(const FString& Parameters)              \
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
        /* A test body that made no assertions at all is a broken test, not a   */      \
        /* passing one - guard against a body being accidentally emptied.       */      \
        if (Result.Checks == 0)                                                         \
        {                                                                               \
            AddError(TEXT("Test body executed no assertions."));                        \
            return false;                                                               \
        }                                                                               \
                                                                                        \
        return Result.Passed();                                                         \
    }

UNIVERSE_CORE_TEST_LIST(UNIVERSE_IMPLEMENT_CORE_TEST)

#undef UNIVERSE_IMPLEMENT_CORE_TEST

#endif  // WITH_DEV_AUTOMATION_TESTS
