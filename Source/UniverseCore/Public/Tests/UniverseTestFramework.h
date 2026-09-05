// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"

/**
 * UniverseTestFramework.h
 *
 * A deliberately tiny assertion harness so that a test body is a plain
 * function of a result object:
 *
 *     bool UniverseTest_Something(FUniverseTestResult& Result);
 *
 * Test bodies written this way are written once and executed twice - by the
 * Unreal automation runner inside the editor, and by the standalone MSVC
 * runner in Tools/StandaloneTests. That matters more than it might appear:
 * the core mathematics is what the entire universe is reconstructed from, and
 * being able to verify it in a two-second compile-and-run loop, with no editor
 * in the way, is what makes it practical to actually run the tests on every
 * change rather than occasionally.
 *
 * It intentionally does not try to be a general test framework. Anything that
 * needs a World, an Actor or a tick belongs in a normal Unreal automation test.
 */
struct UNIVERSECORE_API FUniverseTestResult
{
    /** Total assertions evaluated. */
    int32 Checks = 0;

    /** Assertions that failed. */
    int32 Failures = 0;

    /** One human-readable line per failure, in the order they occurred. */
    TArray<FString> FailureMessages;

    void Report(bool bCondition, const FString& Description)
    {
        ++Checks;
        if (!bCondition)
        {
            ++Failures;
            FailureMessages.Add(Description);
        }
    }

    bool Passed() const { return Failures == 0; }
};

// The UVERIFY_ prefix is deliberate. Unreal's own Misc/AutomationTest.h defines
// UTEST_TRUE and UTEST_FALSE with different signatures, and a collision there
// does not merely warn - it silently replaces Epic's macros for any translation
// unit that includes both, which would break unrelated automation tests in a
// baffling way. Anything added here must stay clear of that header's namespace.

/** Asserts a boolean expression; the expression text becomes the message. */
#define UVERIFY_TRUE(Result, Expression) \
    (Result).Report((Expression), FString::Printf(TEXT("line %d: expected true: %s"), \
        __LINE__, TEXT(#Expression)))

#define UVERIFY_FALSE(Result, Expression) \
    (Result).Report(!(Expression), FString::Printf(TEXT("line %d: expected false: %s"), \
        __LINE__, TEXT(#Expression)))

/** Integer equality, printing both values so a failure is diagnosable. */
#define UVERIFY_EQ_INT(Result, Actual, Expected) \
    do { \
        const long long UT_A = static_cast<long long>(Actual); \
        const long long UT_E = static_cast<long long>(Expected); \
        (Result).Report(UT_A == UT_E, FString::Printf( \
            TEXT("line %d: %s == %s  (actual %lld, expected %lld)"), \
            __LINE__, TEXT(#Actual), TEXT(#Expected), UT_A, UT_E)); \
    } while (0)

#define UVERIFY_EQ_UINT(Result, Actual, Expected) \
    do { \
        const unsigned long long UT_A = static_cast<unsigned long long>(Actual); \
        const unsigned long long UT_E = static_cast<unsigned long long>(Expected); \
        (Result).Report(UT_A == UT_E, FString::Printf( \
            TEXT("line %d: %s == %s  (actual 0x%016llX, expected 0x%016llX)"), \
            __LINE__, TEXT(#Actual), TEXT(#Expected), UT_A, UT_E)); \
    } while (0)

/**
 * Exact double equality. Used where the design claims a result is bit-exact
 * (normalisation round trips, serialisation) - a tolerance there would hide
 * exactly the defect the test exists to catch.
 */
#define UVERIFY_EQ_DOUBLE_EXACT(Result, Actual, Expected) \
    do { \
        const double UT_A = (Actual); \
        const double UT_E = (Expected); \
        (Result).Report(UT_A == UT_E, FString::Printf( \
            TEXT("line %d: %s exactly == %s  (actual %.17g, expected %.17g)"), \
            __LINE__, TEXT(#Actual), TEXT(#Expected), UT_A, UT_E)); \
    } while (0)

/** Double equality within an absolute tolerance. */
#define UVERIFY_NEAR(Result, Actual, Expected, Tolerance) \
    do { \
        const double UT_A = (Actual); \
        const double UT_E = (Expected); \
        const double UT_T = (Tolerance); \
        (Result).Report(FMath::Abs(UT_A - UT_E) <= UT_T, FString::Printf( \
            TEXT("line %d: %s ~= %s  (actual %.17g, expected %.17g, tol %.17g, diff %.17g)"), \
            __LINE__, TEXT(#Actual), TEXT(#Expected), UT_A, UT_E, UT_T, \
            FMath::Abs(UT_A - UT_E))); \
    } while (0)

/** Free-form failure with a caller-supplied message. */
#define UVERIFY_MESSAGE(Result, Condition, Message) \
    (Result).Report((Condition), FString::Printf(TEXT("line %d: %s"), __LINE__, Message))
