// Copyright Universe Project. All Rights Reserved.

#include "PlanetEnvironment.h"
#include "PlanetTerrain.h"
#include "UniverseHash.h"
#include "UniverseRandom.h"
#include "StarSystemDescriptor.h"

const TCHAR* LexToString(EPlanetBiosphere Biosphere)
{
    switch (Biosphere)
    {
    case EPlanetBiosphere::Terrestrial: return TEXT("Terrestrial");
    case EPlanetBiosphere::Fungal:      return TEXT("Fungal");
    case EPlanetBiosphere::Barren:      return TEXT("Barren");
    default:                            return TEXT("Unknown");
    }
}

namespace
{
    /** A uniform double in [0, 1) from a 64-bit hash. */
    double UnitFromHash(uint64 Hash)
    {
        // Top 53 bits, which is exactly the mantissa width, so every result is
        // representable and the mapping is uniform rather than merely close.
        return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);
    }

    /** A uniform double in [Min, Max) from a 64-bit hash. */
    double RangeFromHash(uint64 Hash, double Min, double Max)
    {
        return Min + UnitFromHash(Hash) * (Max - Min);
    }
}

FVector3d FPlanetEnvironment::GetSampleDirection(int32 Index, int32 Count)
{
    // Rejection sampling inside the unit cube, not a Fibonacci spiral.
    //
    // The obvious construction for evenly spread points on a sphere is a
    // Fibonacci spiral, and it is better distributed than this. It is also
    // built from sin and cos, and these directions feed the ocean level, which
    // decides every coastline on the planet. Anything on that path inherits
    // libm's cross-platform ambiguity - the open risk ADR-002 records for star
    // generation and that ADR-003 deliberately kept off the terrain path.
    //
    // Rejection sampling needs only multiply, compare and one square root, all
    // of which IEEE-754 specifies exactly. The distribution is genuinely
    // uniform, the loop terminates in 2.1 draws on average, and the iteration
    // count is itself a deterministic function of the hash. Slightly worse
    // spacing in exchange for a bit-identical coastline is the right trade.
    const uint64 Base = UniverseHash::Hash(0x454E56534D504C00ull, Index, Count);

    for (int32 Attempt = 0; Attempt < 64; ++Attempt)
    {
        const uint64 HashX = UniverseHash::Hash(Base, 0x1u, Attempt);
        const uint64 HashY = UniverseHash::Hash(Base, 0x2u, Attempt);
        const uint64 HashZ = UniverseHash::Hash(Base, 0x3u, Attempt);

        const double X = RangeFromHash(HashX, -1.0, 1.0);
        const double Y = RangeFromHash(HashY, -1.0, 1.0);
        const double Z = RangeFromHash(HashZ, -1.0, 1.0);

        const double LengthSquared = X * X + Y * Y + Z * Z;

        // The lower bound rejects points near the origin, where normalising
        // would amplify their rounding error into a direction that is no
        // longer uniformly distributed.
        if (LengthSquared > 1.0 || LengthSquared < 1.0e-6)
        {
            continue;
        }

        const double Scale = 1.0 / FMath::Sqrt(LengthSquared);

        return FVector3d(X * Scale, Y * Scale, Z * Scale);
    }

    // Sixty-four consecutive rejections has probability around 10^-33. Falling
    // back to an axis rather than looping forever means a pathological hash
    // produces a slightly worse sample set instead of a hang.
    return FVector3d(0.0, 0.0, 1.0);
}

double FPlanetEnvironment::MeasureOceanCoverage(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    double OceanRadiusMeters)
{
    if (OceanRadiusMeters <= 0.0)
    {
        return 0.0;
    }

    int32 Submerged = 0;

    for (int32 Index = 0; Index < OceanSampleCount; ++Index)
    {
        const FVector3d Direction = GetSampleDirection(Index, OceanSampleCount);
        const double SurfaceRadius =
            Planet.RadiusMeters + FPlanetTerrain::GetElevationMeters(Planet, Settings, Direction);

        if (SurfaceRadius < OceanRadiusMeters)
        {
            ++Submerged;
        }
    }

    return static_cast<double>(Submerged) / static_cast<double>(OceanSampleCount);
}

double FPlanetEnvironment::ResolveOceanRadius(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    double TargetCoverage,
    double& OutActualCoverage)
{
    OutActualCoverage = 0.0;

    if (TargetCoverage <= 0.0)
    {
        return 0.0;
    }

    // Sample the terrain once and sort, rather than binary-searching the
    // radius.
    //
    // A binary search would re-evaluate the terrain function at every sample
    // on every iteration - twenty-odd iterations times four thousand samples,
    // each of which is a nineteen-octave noise evaluation. Sorting the sampled
    // radii instead evaluates the terrain exactly once per sample and then
    // reads the answer straight off: the radius at the target percentile *is*
    // the level that submerges that fraction. It is both faster and exact for
    // the sample set, where a search is only ever approximate.
    TArray<double> Radii;
    Radii.Reserve(OceanSampleCount);

    for (int32 Index = 0; Index < OceanSampleCount; ++Index)
    {
        const FVector3d Direction = GetSampleDirection(Index, OceanSampleCount);

        Radii.Add(Planet.RadiusMeters + FPlanetTerrain::GetElevationMeters(Planet, Settings, Direction));
    }

    Radii.Sort();

    const double Clamped = FMath::Clamp(TargetCoverage, 0.0, 1.0);

    const int32 CutIndex = FMath::Clamp(
        FMath::RoundToInt32(Clamped * static_cast<double>(OceanSampleCount)),
        0, OceanSampleCount - 1);

    const double OceanRadius = Radii[CutIndex];

    // Report what the level actually achieves rather than what was asked for.
    // They differ by up to one sample, and by more when the terrain histogram
    // is flat somewhere - which is exactly the case a caller would want to
    // know about.
    int32 Submerged = 0;

    for (double Radius : Radii)
    {
        if (Radius < OceanRadius)
        {
            ++Submerged;
        }
    }

    OutActualCoverage = static_cast<double>(Submerged) / static_cast<double>(OceanSampleCount);

    return OceanRadius;
}

FPlanetEnvironmentDescriptor FPlanetEnvironment::Resolve(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FPlanetDescriptor& Astronomy)
{
    FPlanetEnvironmentDescriptor Environment;

    if (!Planet.IsValid())
    {
        return Environment;
    }

    Environment.PlanetKey = Planet.PlanetKey;
    Environment.Seed = Planet.Seed.Stream(UniverseSeedDomain::StreamEnvironment);

    FUniverseRandom Random(Environment.Seed.Value);

    // --- Rotation ---------------------------------------------------------
    //
    // Carried from the astronomical descriptor, not re-rolled. The rotation
    // period is already a physical property of the body; a second, different
    // value for it here would mean the day length and the Coriolis direction
    // disagreed with whatever the astronomy said.
    Environment.RotationPeriodSeconds = Astronomy.RotationPeriodSeconds;
    Environment.AxialTiltRadians = Astronomy.AxialTiltRadians;

    // The axis itself is drawn here, because Sprint 001 does not produce one -
    // it produces a tilt magnitude with no direction. A random unit vector is
    // the honest placeholder: the tilt is relative to an orbital plane that is
    // not yet modelled, so any axis is as defensible as any other, and drawing
    // it from the seed at least makes it stable.
    Environment.RotationAxis =
        GetSampleDirection(static_cast<int32>(Random.NextUInt32() & 0xFFFF), 65536);

    // --- Thermal ----------------------------------------------------------
    //
    // The equilibrium temperature is what the star delivers. An atmosphere
    // adds a greenhouse offset on top of it - Earth's is about 33 K, and it is
    // what makes the difference between a habitable planet and a frozen one.
    Environment.AtmosphereDensity = Astronomy.bHasAtmosphere
        ? Random.NextRange(0.3, 3.0)
        : 0.0;

    constexpr double EarthGreenhouseK = 33.0;

    Environment.MeanSurfaceTemperatureK =
        Astronomy.EquilibriumTemperatureK + EarthGreenhouseK * Environment.AtmosphereDensity;

    // A thick atmosphere moves heat from the equator to the poles and flattens
    // the gradient; an airless body has nothing to move it with, so its poles
    // are as cold as radiative balance allows.
    constexpr double AirlessDeltaK = 120.0;
    constexpr double EarthDeltaK = 50.0;

    Environment.EquatorPoleDeltaK =
        AirlessDeltaK - (AirlessDeltaK - EarthDeltaK) * FMath::Min(Environment.AtmosphereDensity, 1.0);

    // Lapse rate scales with gravity: g/c_p, with c_p held at Earth's. A heavy
    // world's mountains are colder for the same height.
    constexpr double EarthGravityMs2 = 9.81;
    constexpr double EarthLapseKPerKm = 6.5;

    Environment.LapseRateKPerKm =
        (Environment.AtmosphereDensity > 0.0)
            ? EarthLapseKPerKm * (Planet.SurfaceGravityMs2 / EarthGravityMs2)
            : 0.0;

    // --- Water ------------------------------------------------------------
    //
    // Liquid water needs an atmosphere to hold it down and a temperature it
    // can exist at. Outside that band the coverage target is zero, which is
    // not a tuning choice but the reason Mars has no oceans.
    constexpr double WaterFreezingK = 273.15;
    constexpr double WaterBoilingK = 373.15;

    const bool bCanHoldWater =
        Environment.AtmosphereDensity > 0.1
        && Environment.MeanSurfaceTemperatureK > WaterFreezingK - 40.0
        && Environment.MeanSurfaceTemperatureK < WaterBoilingK;

    const double CoverageTarget = bCanHoldWater ? Random.NextRange(0.15, 0.8) : 0.0;

    if (CoverageTarget > 0.0)
    {
        Environment.OceanRadiusMeters = ResolveOceanRadius(
            Planet, Settings, CoverageTarget, Environment.OceanCoverage);
    }

    // --- Life -------------------------------------------------------------
    //
    // Life needs air, water and a temperature it can work at. The bands are
    // generous - the point is that a barren rock and a living world differ
    // because of their physical properties, not because a flag was set.
    const bool bHabitable =
        Environment.AtmosphereDensity > 0.2
        && Environment.HasOcean()
        && Environment.MeanSurfaceTemperatureK > 250.0
        && Environment.MeanSurfaceTemperatureK < 330.0;

    if (bHabitable)
    {
        // A fifth of habitable worlds go fungal. Not a physical claim - there
        // is no model of biochemistry here - but a deliberate one: the
        // alternative biosphere has to occur naturally in generated systems,
        // or it is a debug feature that will quietly rot.
        Environment.Biosphere = (Random.NextUnit() < 0.2)
            ? EPlanetBiosphere::Fungal
            : EPlanetBiosphere::Terrestrial;

        Environment.VegetationPotential = Random.NextRange(0.4, 1.0);
    }
    else
    {
        Environment.Biosphere = EPlanetBiosphere::Barren;
        Environment.VegetationPotential = 0.0;
    }

    // --- Humidity and sky -------------------------------------------------
    //
    // Ocean coverage drives the planet-wide humidity, because that is where
    // atmospheric water comes from. A world that is nine-tenths ocean is humid
    // even in its interiors; a world with a few lakes is not.
    Environment.HumidityBias = FMath::Clamp(
        0.15 + Environment.OceanCoverage * 0.7 + Random.NextRange(-0.1, 0.1), 0.0, 1.0);

    Environment.CloudCoverageBias = FMath::Clamp(
        Environment.HumidityBias * Environment.AtmosphereDensity * 0.8, 0.0, 0.95);

    Environment.StormPotential = FMath::Clamp(
        Environment.CloudCoverageBias * Random.NextRange(0.3, 0.9), 0.0, 1.0);

    Environment.GenerationVersion = PlanetEnvironmentVersion::Current;

    return Environment;
}

FString FPlanetEnvironmentDescriptor::ToDebugString() const
{
    return FString::Printf(
        TEXT("Env key=0x%016llX  %s  T=%.1f K (dT %.0f)  atm=%.2f  ocean %.0f%% @ %.1f km  ")
        TEXT("humid=%.2f  veg=%.2f  cloud=%.2f  day=%.1f h  env v%u"),
        static_cast<unsigned long long>(PlanetKey),
        LexToString(Biosphere),
        MeanSurfaceTemperatureK,
        EquatorPoleDeltaK,
        AtmosphereDensity,
        OceanCoverage * 100.0,
        OceanRadiusMeters / 1000.0,
        HumidityBias,
        VegetationPotential,
        CloudCoverageBias,
        GetDayLengthSeconds() / 3600.0,
        GenerationVersion);
}
