// Copyright Universe Project. All Rights Reserved.

#include "PlanetSurface.h"
#include "UniverseHash.h"
#include "StarSystemGenerator.h"

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
}

FPlanetSurfaceDescriptor FPlanetSurfaceDescriptor::FromGeneratedPlanet(
    const FStarSystemDescriptor& System,
    int32 InOrbitIndex)
{
    FPlanetSurfaceDescriptor Surface;

    if (!System.Planets.IsValidIndex(InOrbitIndex))
    {
        return Surface;
    }

    const FPlanetDescriptor& Planet = System.Planets[InOrbitIndex];

    Surface.SystemId = System.Id;
    Surface.OrbitIndex = InOrbitIndex;
    Surface.PlanetKey =
        UniverseHash::Hash(0x504C414E45540001ull, System.Id.Hash, InOrbitIndex) | 1ull;

    // Terrain descends from the body seed through the Surface domain, so the
    // shape of the ground is independent of the star, mass and orbit values
    // drawn from the same body seed. Adding a physical property to a planet
    // later must not move its mountains.
    Surface.Seed = Planet.Seed.Stream(UniverseSeedDomain::StreamSurface);

    // The astronomical descriptor is the single source of truth for radius and
    // position; nothing here re-derives them.
    Surface.RadiusMeters = Planet.RadiusMeters;
    Surface.Position = FStarSystemGenerator::GetPlanetPosition(System, Planet);

    // Relief scales with the body rather than being a constant. A fixed 9 km
    // would give a small moon Himalayas and a gas giant a billiard-ball
    // surface.
    //
    // The fractions are Earth's: its highest peak is 0.139% of its radius and
    // its deepest trench 0.172%. An earlier version inflated these to make
    // relief more legible during validation, and that turned out to be a bad
    // trade - it put 15 km peaks on a 5000 km planet, so flying at a 12 km
    // "altitude" placed the observer inside a mountain. Physical values keep
    // altitude meaning what it says.
    constexpr double ElevationFraction = 0.00139;
    constexpr double DepthFraction = 0.00172;

    Surface.MaxElevationMeters = Planet.RadiusMeters * ElevationFraction;
    Surface.MaxDepthMeters = Planet.RadiusMeters * DepthFraction;

    // Gravity comes straight across. FPlanetDescriptor derived it from mass and
    // radius during system generation; recomputing it here from the same inputs
    // would be a second implementation of one fact, and the two would drift.
    Surface.SurfaceGravityMs2 = Planet.SurfaceGravityMs2;

    // Atmosphere height, when the body has one at all.
    //
    // Scaled by radius rather than fixed, like relief, and for the same reason.
    // Earth's Karman line sits at 100 km, which is 1.57% of its radius; that
    // ratio is what is applied. It is not physics - a real atmosphere's extent
    // depends on temperature, composition and gravity, not radius alone - but
    // it is a defensible single number that behaves sensibly from a moon to a
    // gas giant, and it is a simulation boundary rather than a claim about the
    // air. When atmospheric composition arrives it replaces this line.
    constexpr double AtmosphereFraction = 0.0157;

    Surface.AtmosphereHeightMeters =
        Planet.bHasAtmosphere ? (Planet.RadiusMeters * AtmosphereFraction) : 0.0;

    Surface.GenerationVersion = PlanetTerrainVersion::Current;

    return Surface;
}

void FPlanetSurfaceDescriptor::Serialize(FUniverseByteWriter& Writer) const
{
    SystemId.Serialize(Writer);
    Writer.WriteInt32(OrbitIndex);
    Writer.WriteUInt64(PlanetKey);
    Writer.WriteUInt64(Seed.Value);
    Position.Serialize(Writer);
    Writer.WriteDouble(RadiusMeters);
    Writer.WriteDouble(MaxElevationMeters);
    Writer.WriteDouble(MaxDepthMeters);
    Writer.WriteDouble(SurfaceGravityMs2);
    Writer.WriteDouble(AtmosphereHeightMeters);
    Writer.WriteUInt32(GenerationVersion);
}

bool FPlanetSurfaceDescriptor::Deserialize(FUniverseByteReader& Reader)
{
    FPlanetSurfaceDescriptor Candidate;

    if (!Candidate.SystemId.Deserialize(Reader))
    {
        return false;
    }

    Candidate.OrbitIndex = Reader.ReadInt32();
    Candidate.PlanetKey = Reader.ReadUInt64();
    Candidate.Seed = FUniverseSeed(Reader.ReadUInt64());

    if (!Candidate.Position.Deserialize(Reader))
    {
        return false;
    }

    Candidate.RadiusMeters = Reader.ReadDouble();
    Candidate.MaxElevationMeters = Reader.ReadDouble();
    Candidate.MaxDepthMeters = Reader.ReadDouble();
    Candidate.SurfaceGravityMs2 = Reader.ReadDouble();
    Candidate.AtmosphereHeightMeters = Reader.ReadDouble();
    Candidate.GenerationVersion = Reader.ReadUInt32();

    if (!Reader.IsValid() || !Candidate.IsValid())
    {
        return false;
    }

    *this = Candidate;
    return true;
}

uint64 FPlanetSurfaceDescriptor::GetContentHash() const
{
    // Everything that affects terrain shape, and nothing that does not.
    // GenerationVersion is included deliberately: a planet generated by a
    // different version of the algorithm is a different planet, and the hash
    // should say so.
    //
    // SurfaceGravityMs2 and AtmosphereHeightMeters are excluded on purpose:
    // they change how a planet behaves, not where its ground is. Folding them
    // in would make every cached patch invalid the first time an atmosphere
    // model is tuned.
    uint64 Hash = UniverseHash::Mix64(0x504C4E5453524600ull);  // "PLNTSRF"
    Hash = UniverseHash::Combine(Hash, PlanetKey);
    Hash = UniverseHash::Combine(Hash, Seed.Value);
    Hash = HashDouble(Hash, RadiusMeters);
    Hash = HashDouble(Hash, MaxElevationMeters);
    Hash = HashDouble(Hash, MaxDepthMeters);
    Hash = UniverseHash::Combine(Hash, GenerationVersion);
    return Hash;
}

FString FPlanetSurfaceDescriptor::ToDebugString() const
{
    return FString::Printf(
        TEXT("Planet %s #%d key=0x%016llX seed=0x%016llX  r=%.1f km  relief +%.0f/-%.0f m  ")
        TEXT("g=%.2f m/s2  atmo=%.1f km  gen v%u"),
        *SystemId.ToDebugString(),
        OrbitIndex,
        static_cast<unsigned long long>(PlanetKey),
        static_cast<unsigned long long>(Seed.Value),
        RadiusMeters / 1000.0,
        MaxElevationMeters,
        MaxDepthMeters,
        SurfaceGravityMs2,
        AtmosphereHeightMeters / 1000.0,
        GenerationVersion);
}
