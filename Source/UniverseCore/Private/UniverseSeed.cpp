// Copyright Universe Project. All Rights Reserved.

#include "UniverseSeed.h"

FString FUniverseSeed::ToDebugString() const
{
    // Hex, because seeds are compared by eye against logs and save files and a
    // decimal 20-digit number is unreadable at a glance.
    return FString::Printf(TEXT("0x%016llX"), static_cast<unsigned long long>(Value));
}

FUniverseSeedHierarchy FUniverseSeedHierarchy::FromText(const TCHAR* SeedText)
{
    // An empty or missing seed phrase must not produce an arbitrary universe:
    // a config typo would otherwise silently move every player to a different
    // world. Fall back to a single named default instead.
    if (SeedText == nullptr || *SeedText == 0)
    {
        return FUniverseSeedHierarchy(UniverseHash::HashString(TEXT("UNIVERSE-DEFAULT-SEED")));
    }

    return FUniverseSeedHierarchy(UniverseHash::HashString(SeedText));
}
