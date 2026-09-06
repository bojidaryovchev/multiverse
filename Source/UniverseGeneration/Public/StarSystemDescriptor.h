// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseCoordinates.h"
#include "UniverseSeed.h"
#include "UniverseSerialization.h"

/**
 * StarSystemDescriptor.h
 *
 * The generated *data* for a star system. Pure values: no UObject, no Actor,
 * no engine dependency, nothing that knows the system will ever be rendered.
 *
 * This separation is the point. A descriptor can be produced on a worker
 * thread, compared for determinism in a test, serialised into a save file, or
 * sent over a network, none of which is possible for something that owns
 * Actors. The game module turns descriptors into visible geometry and applies
 * presentation scaling; nothing in this file knows what a centimetre on screen
 * looks like.
 *
 * All physical quantities are in SI (metres, kilograms, seconds, kelvin) or in
 * conventional astronomical units where those are clearer (solar masses, solar
 * luminosities). They are the *logical* values - a gas giant here really is
 * 70,000 km in radius. Any visual compression happens later and separately, so
 * that shrinking the view never shrinks the universe.
 */

/**
 * Morgan-Keenan spectral classes, hottest to coolest.
 *
 * Plain enums rather than UENUMs: UniverseGeneration is deliberately free of
 * UnrealHeaderTool so that it compiles in the standalone verification harness.
 * The game module maps these to display data where reflection is needed.
 */
enum class EStarClass : uint8
{
    O = 0,
    B,
    A,
    F,
    G,
    K,
    M,
    Count
};

/** Coarse planet taxonomy. Extended, not replaced, as generation grows. */
enum class EPlanetType : uint8
{
    /** Tidally roasted rock, inside the inner edge. */
    Molten = 0,
    /** Airless or near-airless rock. */
    Rocky,
    /** Warm rock with a thin dry atmosphere. */
    Desert,
    /** Global ocean. */
    Ocean,
    /** Mixed land and water in the habitable zone. */
    Terrestrial,
    /** Frozen rock/ice beyond the frost line. */
    Ice,
    /** Hydrogen-helium giant. */
    GasGiant,
    /** Water/ammonia/methane giant. */
    IceGiant,
    Count
};

UNIVERSEGENERATION_API const TCHAR* ToString(EStarClass Class);
UNIVERSEGENERATION_API const TCHAR* ToString(EPlanetType Type);

/**
 * FUniverseSystemId
 *
 * The stable identity of a star system: its integer address in the universe,
 * plus a hash of that address for cheap comparison and map keying.
 *
 * Address-derived, deliberately. A GUID, a pointer, an FName index or a
 * spawn-order counter would all be stable only within one process; this is
 * stable across runs, machines and engine versions, which is what a
 * persistence key and a future network identifier actually require.
 */
/**
 * Star system generation version.
 *
 * Bumping it regenerates every star and every planet in the universe, which
 * invalidates every save and every structure a player has built. It is a named
 * constant carrying that sentence for the same reason the terrain, environment
 * and galaxy versions are: so that the consequence is visible at the point of
 * change rather than discovered afterwards.
 *
 * It is also part of the multiplayer handshake. A client generating stars by
 * different rules from the server would fly to a system that is not there.
 */
namespace StarSystemGeneratorVersion
{
    inline constexpr uint32 Current = 1;
}

struct UNIVERSEGENERATION_API FUniverseSystemId
{
    int64 SectorX = 0;
    int64 SectorY = 0;
    int64 SectorZ = 0;

    /** Index of this system within its sector. */
    int32 IndexInSector = 0;

    /** Stable hash of the four fields above. Never used as the identity
     *  itself - only as a fast comparison and hash-table key. */
    uint64 Hash = 0;

    FUniverseSystemId() = default;
    FUniverseSystemId(int64 InSectorX, int64 InSectorY, int64 InSectorZ, int32 InIndex);

    bool IsValid() const { return Hash != 0; }

    bool operator==(const FUniverseSystemId& Other) const
    {
        return SectorX == Other.SectorX
            && SectorY == Other.SectorY
            && SectorZ == Other.SectorZ
            && IndexInSector == Other.IndexInSector;
    }

    bool operator!=(const FUniverseSystemId& Other) const { return !(*this == Other); }

    static constexpr int32 SerializedSizeBytes = 8 + 8 + 8 + 4 + 8;

    void Serialize(FUniverseByteWriter& Writer) const;
    bool Deserialize(FUniverseByteReader& Reader);

    FString ToDebugString() const;
};

inline uint32 GetTypeHash(const FUniverseSystemId& Id)
{
    return static_cast<uint32>(Id.Hash ^ (Id.Hash >> 32));
}

/** The central star of a system. */
struct UNIVERSEGENERATION_API FStarDescriptor
{
    FUniverseSeed Seed;

    EStarClass Class = EStarClass::G;

    /** Mass in solar masses. */
    double MassSolar = 1.0;

    /** Physical radius in metres. The Sun is 6.957e8 m. */
    double RadiusMeters = 6.957e8;

    /** Bolometric luminosity in solar luminosities. */
    double LuminositySolar = 1.0;

    /** Effective surface temperature in kelvin. */
    double SurfaceTemperatureK = 5772.0;

    /** Approximate linear sRGB colour, each channel in [0, 1]. */
    double ColorR = 1.0;
    double ColorG = 1.0;
    double ColorB = 1.0;

    FString Name;
};

/** One planet. Moons are not generated in Sprint 001. */
struct UNIVERSEGENERATION_API FPlanetDescriptor
{
    FUniverseSeed Seed;

    /** Ordinal from the star, 0 being innermost. */
    int32 OrbitIndex = 0;

    EPlanetType Type = EPlanetType::Rocky;

    /** Semi-major axis in metres. */
    double OrbitRadiusMeters = 1.4959787e11;

    /** Orbital inclination from the system plane, radians. */
    double OrbitInclinationRadians = 0.0;

    /** Mean anomaly at epoch, radians. Fixes where the planet sits at t=0. */
    double OrbitPhaseRadians = 0.0;

    /** Orbital period in seconds, from Kepler's third law. */
    double OrbitalPeriodSeconds = 3.1557e7;

    /** Physical radius in metres. Earth is 6.371e6 m. */
    double RadiusMeters = 6.371e6;

    /** Mass in kilograms. */
    double MassKg = 5.972e24;

    /** Surface gravity in m/s^2, derived from mass and radius. */
    double SurfaceGravityMs2 = 9.81;

    /** Sidereal rotation period in seconds. Negative means retrograde. */
    double RotationPeriodSeconds = 86164.0;

    /** Axial tilt in radians. */
    double AxialTiltRadians = 0.0;

    /** Equilibrium temperature in kelvin, from luminosity and orbit. */
    double EquilibriumTemperatureK = 255.0;

    /** Whether the planet retains a meaningful atmosphere. */
    bool bHasAtmosphere = false;

    FString Name;
};

/**
 * A fully generated star system.
 *
 * Produced entirely from (universe seed, sector address, index). Two calls
 * with the same inputs produce byte-identical descriptors, which is what the
 * determinism tests assert and what lets the game discard a system from memory
 * the moment the player leaves it.
 */
struct UNIVERSEGENERATION_API FStarSystemDescriptor
{
    FUniverseSystemId Id;

    FUniverseSeed Seed;

    /** The system barycentre, in canonical universe coordinates. */
    FUniversePosition Position;

    FStarDescriptor Star;

    TArray<FPlanetDescriptor> Planets;

    FString Name;

    bool IsValid() const { return Id.IsValid(); }

    /**
     * A stable hash of the entire generated content, used by the determinism
     * tests to compare two generations in one comparison instead of field by
     * field. Includes the floating-point payload by bit pattern, so a change
     * of a single least-significant bit anywhere is detected.
     */
    uint64 GetContentHash() const;

    FString ToDebugString() const;
};
