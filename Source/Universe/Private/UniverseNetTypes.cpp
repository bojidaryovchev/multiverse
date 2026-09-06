// Copyright Universe Project. All Rights Reserved.

#include "UniverseNetTypes.h"

#include "GalaxyDescriptor.h"
#include "PlanetEnvironment.h"
#include "PlanetSurface.h"
#include "WorldPersistence.h"

const TCHAR* LexToString(ENetRelevanceClass Class)
{
    switch (Class)
    {
    case ENetRelevanceClass::Irrelevant: return TEXT("Irrelevant");
    case ENetRelevanceClass::SameSystem: return TEXT("SameSystem");
    case ENetRelevanceClass::SameRegion: return TEXT("SameRegion");
    case ENetRelevanceClass::Visible:    return TEXT("Visible");
    default:                             return TEXT("?");
    }
}

FUniverseWorldIdentity FUniverseWorldIdentity::MakeLocal(
    const FString& InSeedText, uint64 InSeedValue)
{
    FUniverseWorldIdentity Identity;

    Identity.SeedText = InSeedText;
    Identity.SeedValue = static_cast<int64>(InSeedValue);

    // Every generation version this build would use. Gathered here rather than
    // by each consumer, so that adding a new versioned generator means adding
    // it in one place and every handshake picks it up.
    Identity.GalaxyVersion = static_cast<int32>(GalaxyGeneratorVersion::Current);
    Identity.SystemVersion = static_cast<int32>(StarSystemGeneratorVersion::Current);
    Identity.TerrainVersion = static_cast<int32>(PlanetTerrainVersion::Current);
    Identity.EnvironmentVersion = static_cast<int32>(PlanetEnvironmentVersion::Current);
    Identity.PersistenceSchemaVersion = WorldPersistenceSchema::Version;

    return Identity;
}

FString FUniverseWorldIdentity::DescribeMismatch(const FUniverseWorldIdentity& Other) const
{
    TArray<FString> Differences;

    if (SeedValue != Other.SeedValue)
    {
        Differences.Add(FString::Printf(
            TEXT("universe seed 0x%016llX vs 0x%016llX (\"%s\" vs \"%s\")"),
            static_cast<unsigned long long>(SeedValue),
            static_cast<unsigned long long>(Other.SeedValue),
            *SeedText, *Other.SeedText));
    }
    else if (SeedText != Other.SeedText)
    {
        // The same derived seed from different text is not a mismatch that
        // matters - the universe is identical - but it is worth naming, because
        // it usually means somebody edited the phrase and got lucky.
        Differences.Add(FString::Printf(
            TEXT("seed phrase \"%s\" vs \"%s\" (same derived seed)"), *SeedText, *Other.SeedText));
    }

    if (GalaxyVersion != Other.GalaxyVersion)
    {
        Differences.Add(FString::Printf(
            TEXT("galaxy generator v%d vs v%d"), GalaxyVersion, Other.GalaxyVersion));
    }

    if (SystemVersion != Other.SystemVersion)
    {
        Differences.Add(FString::Printf(
            TEXT("system generator v%d vs v%d"), SystemVersion, Other.SystemVersion));
    }

    if (TerrainVersion != Other.TerrainVersion)
    {
        Differences.Add(FString::Printf(
            TEXT("terrain v%d vs v%d"), TerrainVersion, Other.TerrainVersion));
    }

    if (EnvironmentVersion != Other.EnvironmentVersion)
    {
        Differences.Add(FString::Printf(
            TEXT("environment v%d vs v%d"), EnvironmentVersion, Other.EnvironmentVersion));
    }

    if (PersistenceSchemaVersion != Other.PersistenceSchemaVersion)
    {
        Differences.Add(FString::Printf(
            TEXT("persistence schema v%d vs v%d"),
            PersistenceSchemaVersion, Other.PersistenceSchemaVersion));
    }

    if (Differences.Num() == 0)
    {
        return TEXT("no difference");
    }

    return FString::Join(Differences, TEXT("; "));
}

FString FUniverseWorldIdentity::ToDebugString() const
{
    return FString::Printf(
        TEXT("\"%s\" (0x%016llX)  galaxy v%d  system v%d  terrain v%d  environment v%d  schema v%d"),
        *SeedText, static_cast<unsigned long long>(SeedValue),
        GalaxyVersion, SystemVersion, TerrainVersion, EnvironmentVersion,
        PersistenceSchemaVersion);
}
