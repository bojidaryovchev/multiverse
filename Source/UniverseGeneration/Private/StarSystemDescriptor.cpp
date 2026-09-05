// Copyright Universe Project. All Rights Reserved.

#include "StarSystemDescriptor.h"
#include "UniverseHash.h"

const TCHAR* ToString(EStarClass Class)
{
    switch (Class)
    {
    case EStarClass::O: return TEXT("O");
    case EStarClass::B: return TEXT("B");
    case EStarClass::A: return TEXT("A");
    case EStarClass::F: return TEXT("F");
    case EStarClass::G: return TEXT("G");
    case EStarClass::K: return TEXT("K");
    case EStarClass::M: return TEXT("M");
    default:            return TEXT("?");
    }
}

const TCHAR* ToString(EPlanetType Type)
{
    switch (Type)
    {
    case EPlanetType::Molten:      return TEXT("Molten");
    case EPlanetType::Rocky:       return TEXT("Rocky");
    case EPlanetType::Desert:      return TEXT("Desert");
    case EPlanetType::Ocean:       return TEXT("Ocean");
    case EPlanetType::Terrestrial: return TEXT("Terrestrial");
    case EPlanetType::Ice:         return TEXT("Ice");
    case EPlanetType::GasGiant:    return TEXT("GasGiant");
    case EPlanetType::IceGiant:    return TEXT("IceGiant");
    default:                       return TEXT("Unknown");
    }
}

namespace
{
    /** Folds a double into a hash by its bit pattern, not its value. */
    uint64 HashDouble(uint64 Accumulator, double Value)
    {
        uint64 Bits = 0;
        const unsigned char* Source = reinterpret_cast<const unsigned char*>(&Value);
        for (size_t Index = 0; Index < sizeof(double); ++Index)
        {
            Bits |= static_cast<uint64>(Source[Index]) << (Index * 8);
        }
        return UniverseHash::Combine(Accumulator, Bits);
    }

    uint64 HashString(uint64 Accumulator, const FString& Value)
    {
        return UniverseHash::Combine(Accumulator, UniverseHash::HashString(*Value));
    }
}

FUniverseSystemId::FUniverseSystemId(int64 InSectorX, int64 InSectorY, int64 InSectorZ, int32 InIndex)
    : SectorX(InSectorX)
    , SectorY(InSectorY)
    , SectorZ(InSectorZ)
    , IndexInSector(InIndex)
{
    // The tag makes a system ID hash distinct from any other 4-tuple hash in
    // the project, and the "| 1" guarantees a non-zero value so that Hash == 0
    // can reliably mean "unset".
    Hash = UniverseHash::Hash(
        0x5359534944000001ull, SectorX, SectorY, SectorZ, IndexInSector) | 1ull;
}

void FUniverseSystemId::Serialize(FUniverseByteWriter& Writer) const
{
    Writer.WriteInt64(SectorX);
    Writer.WriteInt64(SectorY);
    Writer.WriteInt64(SectorZ);
    Writer.WriteInt32(IndexInSector);
    Writer.WriteUInt64(Hash);
}

bool FUniverseSystemId::Deserialize(FUniverseByteReader& Reader)
{
    const int64 InSectorX = Reader.ReadInt64();
    const int64 InSectorY = Reader.ReadInt64();
    const int64 InSectorZ = Reader.ReadInt64();
    const int32 InIndex = Reader.ReadInt32();
    const uint64 InHash = Reader.ReadUInt64();

    if (!Reader.IsValid())
    {
        return false;
    }

    // Recompute rather than trust the stored hash: the address is the identity
    // and the hash is derived from it, so a mismatch means the stream is
    // corrupt or was written by an incompatible version of the hash.
    const FUniverseSystemId Candidate(InSectorX, InSectorY, InSectorZ, InIndex);
    if (Candidate.Hash != InHash)
    {
        return false;
    }

    *this = Candidate;
    return true;
}

FString FUniverseSystemId::ToDebugString() const
{
    return FString::Printf(
        TEXT("Sector [%lld, %lld, %lld] #%d (0x%016llX)"),
        static_cast<long long>(SectorX),
        static_cast<long long>(SectorY),
        static_cast<long long>(SectorZ),
        IndexInSector,
        static_cast<unsigned long long>(Hash));
}

uint64 FStarSystemDescriptor::GetContentHash() const
{
    uint64 Hash = UniverseHash::Mix64(0x53595354454D4844ull);  // "SYSTEMHD"

    Hash = UniverseHash::Combine(Hash, Id.Hash);
    Hash = UniverseHash::Combine(Hash, Seed.Value);
    Hash = UniverseHash::Combine(Hash, Position.GetStableHash64());
    Hash = HashString(Hash, Name);

    Hash = UniverseHash::Combine(Hash, Star.Seed.Value);
    Hash = UniverseHash::Combine(Hash, static_cast<uint32>(Star.Class));
    Hash = HashDouble(Hash, Star.MassSolar);
    Hash = HashDouble(Hash, Star.RadiusMeters);
    Hash = HashDouble(Hash, Star.LuminositySolar);
    Hash = HashDouble(Hash, Star.SurfaceTemperatureK);
    Hash = HashDouble(Hash, Star.ColorR);
    Hash = HashDouble(Hash, Star.ColorG);
    Hash = HashDouble(Hash, Star.ColorB);
    Hash = HashString(Hash, Star.Name);

    Hash = UniverseHash::Combine(Hash, static_cast<int32>(Planets.Num()));
    for (const FPlanetDescriptor& Planet : Planets)
    {
        Hash = UniverseHash::Combine(Hash, Planet.Seed.Value);
        Hash = UniverseHash::Combine(Hash, Planet.OrbitIndex);
        Hash = UniverseHash::Combine(Hash, static_cast<uint32>(Planet.Type));
        Hash = HashDouble(Hash, Planet.OrbitRadiusMeters);
        Hash = HashDouble(Hash, Planet.OrbitInclinationRadians);
        Hash = HashDouble(Hash, Planet.OrbitPhaseRadians);
        Hash = HashDouble(Hash, Planet.OrbitalPeriodSeconds);
        Hash = HashDouble(Hash, Planet.RadiusMeters);
        Hash = HashDouble(Hash, Planet.MassKg);
        Hash = HashDouble(Hash, Planet.SurfaceGravityMs2);
        Hash = HashDouble(Hash, Planet.RotationPeriodSeconds);
        Hash = HashDouble(Hash, Planet.AxialTiltRadians);
        Hash = HashDouble(Hash, Planet.EquilibriumTemperatureK);
        Hash = UniverseHash::Combine(Hash, static_cast<uint32>(Planet.bHasAtmosphere ? 1u : 0u));
        Hash = HashString(Hash, Planet.Name);
    }

    return Hash;
}

FString FStarSystemDescriptor::ToDebugString() const
{
    FString Text = FString::Printf(
        TEXT("%s  %s  class %s  %.2f Msun  %.3f Lsun  %d planet(s)\n  %s\n"),
        *Name,
        *Id.ToDebugString(),
        ToString(Star.Class),
        Star.MassSolar,
        Star.LuminositySolar,
        Planets.Num(),
        *Position.ToCompactString());

    for (const FPlanetDescriptor& Planet : Planets)
    {
        Text += FString::Printf(
            TEXT("    [%d] %-12s %-12s  a=%8.3f AU  r=%9.1f km  g=%5.2f m/s2  T=%6.1f K\n"),
            Planet.OrbitIndex,
            *Planet.Name,
            ToString(Planet.Type),
            Planet.OrbitRadiusMeters / UniverseScale::MetersPerAu,
            Planet.RadiusMeters / 1000.0,
            Planet.SurfaceGravityMs2,
            Planet.EquilibriumTemperatureK);
    }

    return Text;
}
