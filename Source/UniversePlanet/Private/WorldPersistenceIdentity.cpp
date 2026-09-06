// Copyright Universe Project. All Rights Reserved.

#include "WorldPersistenceIdentity.h"
#include "PlanetSurfaceQuery.h"
#include "UniverseHash.h"

const TCHAR* LexToString(EPersistentEntityKind Kind)
{
    switch (Kind)
    {
    case EPersistentEntityKind::Procedural: return TEXT("Procedural");
    case EPersistentEntityKind::Created:    return TEXT("Created");
    case EPersistentEntityKind::None:       return TEXT("None");
    default:                                return TEXT("Unknown");
    }
}

// --- FPersistenceRegionId ----------------------------------------------------

uint8 FPersistenceRegionId::GetLevelForRadius(double PlanetRadiusMeters)
{
    if (PlanetRadiusMeters <= 0.0)
    {
        return MinLevel;
    }

    // A patch at level L spans (pi/2) * R / 2^L. Solve for the level whose span
    // is closest to the target, then clamp.
    //
    // Rounded rather than floored, because the halfway point between two levels
    // is a factor of two either way and neither is obviously better - rounding
    // at least picks the nearer.
    const double RootSpanMeters = (PI * 0.5) * PlanetRadiusMeters;
    const double Ratio = RootSpanMeters / TargetSizeMeters;

    if (Ratio <= 1.0)
    {
        return MinLevel;
    }

    const int32 Level = FMath::RoundToInt32(FMath::Log2(Ratio));

    return static_cast<uint8>(FMath::Clamp(Level, static_cast<int32>(MinLevel), static_cast<int32>(MaxLevel)));
}

FPersistenceRegionId FPersistenceRegionId::FromDirection(
    const FPlanetSurfaceDescriptor& Planet,
    const FVector3d& Direction)
{
    FPersistenceRegionId Region;

    if (!Planet.IsValid())
    {
        return Region;
    }

    const uint8 Level = GetLevelForRadius(Planet.RadiusMeters);
    const FPlanetPatchId PatchId = FPlanetPatchId::FromDirection(Direction, Level);

    Region.PlanetKey = Planet.PlanetKey;
    Region.Face = static_cast<uint8>(PatchId.GetFace());
    Region.Level = PatchId.Level;
    Region.X = PatchId.X;
    Region.Y = PatchId.Y;

    return Region;
}

FPersistenceRegionId FPersistenceRegionId::FromPlanetLocal(
    const FPlanetSurfaceDescriptor& Planet,
    const FVector3d& PlanetLocalMeters)
{
    FVector3d Direction;

    if (!FPlanetSurfaceQuery::TryGetDirection(PlanetLocalMeters, Direction))
    {
        return FPersistenceRegionId();
    }

    return FromDirection(Planet, Direction);
}

FPlanetPatchId FPersistenceRegionId::ToPatchId() const
{
    return FPlanetPatchId(static_cast<CubeSphere::EFace>(Face), Level, X, Y);
}

double FPersistenceRegionId::GetSizeMeters(double PlanetRadiusMeters) const
{
    // GetApproximateSizeMeters is the diagonal; the edge is that over root two.
    constexpr double InverseRootTwo = 0.70710678118654752440;

    return ToPatchId().GetApproximateSizeMeters(PlanetRadiusMeters) * InverseRootTwo;
}

uint64 FPersistenceRegionId::GetLocalKey() const
{
    // Face, level and coordinates packed rather than hashed.
    //
    // A hash would be shorter to write and one birthday collision away from
    // silently merging two settlements that happen to collide. Packing is
    // exact: 3 bits of face, 5 of level, 28 each of X and Y covers every
    // addressable region up to level 16 with room to spare, and the key can be
    // taken apart again by anyone reading the database.
    return (static_cast<uint64>(Face & 0x7u) << 61)
         | (static_cast<uint64>(Level & 0x1Fu) << 56)
         | (static_cast<uint64>(X & 0x0FFFFFFFu) << 28)
         | static_cast<uint64>(Y & 0x0FFFFFFFu);
}

FString FPersistenceRegionId::ToString() const
{
    static const TCHAR* FaceNames[6] = {
        TEXT("+X"), TEXT("-X"), TEXT("+Y"), TEXT("-Y"), TEXT("+Z"), TEXT("-Z") };

    return FString::Printf(
        TEXT("P%016llX/%s/L%u/%u/%u"),
        static_cast<unsigned long long>(PlanetKey),
        (Face < 6) ? FaceNames[Face] : TEXT("??"),
        static_cast<uint32>(Level), X, Y);
}

// --- FPersistentEntityId -----------------------------------------------------

namespace
{
    /**
     * The kind lives in the top eight bits of the high word.
     *
     * Making the id self-describing means a delta cannot be applied to the
     * wrong kind of entity even if the tables that record kind get out of step -
     * and it costs eight of a hundred and twenty-eight bits.
     */
    constexpr int32 KindShift = 56;
    constexpr uint64 KindMask = 0xFFull << KindShift;

    uint64 WithKind(uint64 High, EPersistentEntityKind Kind)
    {
        return (High & ~KindMask) | (static_cast<uint64>(Kind) << KindShift);
    }
}

EPersistentEntityKind FPersistentEntityId::GetKind() const
{
    const uint64 Raw = (High & KindMask) >> KindShift;

    switch (Raw)
    {
    case static_cast<uint64>(EPersistentEntityKind::Procedural):
        return EPersistentEntityKind::Procedural;

    case static_cast<uint64>(EPersistentEntityKind::Created):
        return EPersistentEntityKind::Created;

    default:
        return EPersistentEntityKind::None;
    }
}

FPersistentEntityId FPersistentEntityId::ForVegetation(
    uint64 PlanetKey,
    const FPlanetPatchId& PatchId,
    EVegetationLayer Layer,
    int32 CellX,
    int32 CellY,
    uint32 TerrainVersion,
    uint32 EnvironmentVersion)
{
    FPersistentEntityId Id;

    // Two independent hashes over the same address, with different domain tags.
    //
    // One 64-bit hash would collide at around four billion entities on a single
    // planet, which is genuinely reachable - a planet has more trees than that.
    // Two independent 64-bit hashes is a 128-bit name, and combining them from
    // the same inputs keeps it a pure derivation with nothing stored.
    const uint64 Base = UniverseHash::Hash(
        0x5645474944000001ull,               // "VEGID"
        PlanetKey,
        PatchId.GetStableHash64());

    const uint64 Address = UniverseHash::Hash(
        Base,
        (static_cast<uint64>(static_cast<uint32>(CellX)) << 32)
            | static_cast<uint64>(static_cast<uint32>(CellY)),
        static_cast<uint32>(Layer));

    // Generation versions are part of the name, on purpose. A bump to either
    // changes which vegetation exists, so a removal recorded under the old
    // version must stop matching rather than silently deleting whatever now
    // occupies that address.
    const uint64 Versioned = UniverseHash::Hash(Address, TerrainVersion, EnvironmentVersion);

    Id.High = WithKind(Versioned, EPersistentEntityKind::Procedural);
    Id.Low = UniverseHash::Hash(Versioned ^ 0x9E3779B97F4A7C15ull, PlanetKey, static_cast<uint32>(Layer));

    return Id;
}

FPersistentEntityId FPersistentEntityId::ForVegetation(
    uint64 PlanetKey,
    const FVegetationInstance& Instance,
    uint32 TerrainVersion,
    uint32 EnvironmentVersion)
{
    return ForVegetation(
        PlanetKey, Instance.PatchId, Instance.Layer, Instance.CellX, Instance.CellY,
        TerrainVersion, EnvironmentVersion);
}

FPersistentEntityId FPersistentEntityId::CreateNew(uint64 EntropyA, uint64 EntropyB)
{
    FPersistentEntityId Id;

    Id.High = WithKind(
        UniverseHash::Hash(0x4352454154450001ull, EntropyA, 0u),   // "CREATE"
        EPersistentEntityKind::Created);

    Id.Low = UniverseHash::Hash(EntropyB ^ 0x9E3779B97F4A7C15ull, EntropyA, 1u);

    return Id;
}

FString FPersistentEntityId::ToHexString() const
{
    return FString::Printf(
        TEXT("%016llX%016llX"),
        static_cast<unsigned long long>(High),
        static_cast<unsigned long long>(Low));
}

bool FPersistentEntityId::FromHexString(const FString& Text, FPersistentEntityId& OutId)
{
    if (Text.Len() != 32)
    {
        return false;
    }

    // Parsed by hand rather than with FParse, because a malformed row must be
    // rejected rather than silently producing zero - a zero id would read as
    // "kind None" and be skipped, which looks like data loss rather than the
    // corruption it is.
    auto ParseHalf = [](const TCHAR* Chars, uint64& OutValue) -> bool
    {
        uint64 Value = 0;

        for (int32 Index = 0; Index < 16; ++Index)
        {
            const TCHAR Character = Chars[Index];
            uint64 Digit = 0;

            if (Character >= TEXT('0') && Character <= TEXT('9'))
            {
                Digit = static_cast<uint64>(Character - TEXT('0'));
            }
            else if (Character >= TEXT('A') && Character <= TEXT('F'))
            {
                Digit = static_cast<uint64>(Character - TEXT('A')) + 10;
            }
            else if (Character >= TEXT('a') && Character <= TEXT('f'))
            {
                Digit = static_cast<uint64>(Character - TEXT('a')) + 10;
            }
            else
            {
                return false;
            }

            Value = (Value << 4) | Digit;
        }

        OutValue = Value;
        return true;
    };

    FPersistentEntityId Candidate;

    if (!ParseHalf(*Text, Candidate.High) || !ParseHalf(*Text + 16, Candidate.Low))
    {
        return false;
    }

    if (Candidate.GetKind() == EPersistentEntityKind::None)
    {
        return false;
    }

    OutId = Candidate;
    return true;
}

void FPersistentEntityId::Serialize(FUniverseByteWriter& Writer) const
{
    Writer.WriteUInt64(High);
    Writer.WriteUInt64(Low);
}

bool FPersistentEntityId::Deserialize(FUniverseByteReader& Reader)
{
    const uint64 ReadHigh = Reader.ReadUInt64();
    const uint64 ReadLow = Reader.ReadUInt64();

    if (!Reader.IsValid())
    {
        return false;
    }

    High = ReadHigh;
    Low = ReadLow;

    return true;
}
