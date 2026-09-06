// Copyright Universe Project. All Rights Reserved.

#include "PlanetWeather.h"
#include "PlanetTerrain.h"
#include "PlanetNoise.h"
#include "CubeSphere.h"
#include "UniverseHash.h"

const TCHAR* LexToString(EWeatherState State)
{
    switch (State)
    {
    case EWeatherState::Clear:  return TEXT("Clear");
    case EWeatherState::Cloudy: return TEXT("Cloudy");
    case EWeatherState::Rain:   return TEXT("Rain");
    case EWeatherState::Storm:  return TEXT("Storm");
    case EWeatherState::Snow:   return TEXT("Snow");
    case EWeatherState::Fog:    return TEXT("Fog");
    default:                    return TEXT("Unknown");
    }
}

namespace
{
    double UnitFromHash(uint64 Hash)
    {
        return static_cast<double>(Hash >> 11) * (1.0 / 9007199254740992.0);
    }

    /** Smoothstep, for blending between periods without a visible corner. */
    double SmoothStep(double T)
    {
        const double X = FMath::Clamp(T, 0.0, 1.0);
        return X * X * (3.0 - 2.0 * X);
    }

    /**
     * Cloudiness in a cell during one period.
     *
     * The planet's cloud bias sets the level and the local climate moves it:
     * humid places are cloudier, dry places clearer. The random term is what
     * makes one day differ from the next in the same place.
     */
    double GetPeriodCloudiness(
        const FPlanetEnvironmentDescriptor& Environment,
        const FClimateSample& Climate,
        uint64 PeriodSeed)
    {
        const double Random = UnitFromHash(UniverseHash::Hash(PeriodSeed, 0x43u, 0));

        const double Base = Environment.CloudCoverageBias;
        const double Humid = (Climate.Humidity - 0.5) * 0.6;

        return FMath::Clamp(Base + Humid + (Random - 0.5) * 0.55, 0.0, 1.0);
    }
}

FPlanetPatchId FPlanetWeather::GetCell(const FVector3d& Direction)
{
    // A weather cell is just a shallow quadtree patch, so the existing patch
    // addressing does all the work - including behaving correctly on face
    // boundaries, which is exactly where a hand-rolled version would not.
    return FPlanetPatchId::FromDirection(Direction, CellLevel);
}

FWeatherSample FPlanetWeather::SampleCell(
    const FPlanetEnvironmentDescriptor& Environment,
    const FPlanetPatchId& Cell,
    const FClimateSample& Climate,
    double SimulationTimeSeconds)
{
    FWeatherSample Sample;

    if (!Environment.IsValid())
    {
        return Sample;
    }

    Sample.bValid = true;

    if (Environment.AtmosphereDensity <= 0.0)
    {
        // No air, no weather. Not a special case so much as the definition.
        Sample.State = EWeatherState::Clear;
        return Sample;
    }

    // Which six-hour period, and how far through it.
    const double Periods = SimulationTimeSeconds / CellPeriodSeconds;
    const int64 PeriodIndex = static_cast<int64>(FMath::FloorToDouble(Periods));
    const double PeriodFraction = Periods - static_cast<double>(PeriodIndex);

    const uint64 CellSeed = UniverseHash::Hash(
        Environment.Seed.Stream(UniverseSeedDomain::StreamWeather).Value,
        Cell.GetStableHash64(),
        0u);

    const uint64 ThisPeriod = UniverseHash::Hash(CellSeed, static_cast<uint64>(PeriodIndex));
    const uint64 NextPeriod = UniverseHash::Hash(CellSeed, static_cast<uint64>(PeriodIndex + 1));

    // Blend the last quarter of a period into the next one, so conditions
    // arrive and depart rather than appearing.
    const double BlendStart = 1.0 - TransitionFraction;

    const double Blend = (PeriodFraction < BlendStart)
        ? 0.0
        : SmoothStep((PeriodFraction - BlendStart) / TransitionFraction);

    const double CloudNow = GetPeriodCloudiness(Environment, Climate, ThisPeriod);
    const double CloudNext = GetPeriodCloudiness(Environment, Climate, NextPeriod);

    Sample.Cloudiness = FMath::Lerp(CloudNow, CloudNext, Blend);

    // --- Precipitation -----------------------------------------------------
    //
    // Requires cloud, moisture and the planet's storm potential. Rain in a
    // desert is rare because a desert has neither the humidity nor, usually,
    // the cloud - which is the model doing its job rather than a rule saying
    // "deserts do not rain".
    const double PrecipChance =
        Environment.StormPotential
        * FMath::Max(Sample.Cloudiness - 0.35, 0.0) / 0.65
        * FMath::Clamp(Climate.Humidity * 1.4, 0.0, 1.0);

    const double PrecipRollNow = UnitFromHash(UniverseHash::Hash(ThisPeriod, 0x50u, 0));
    const double PrecipRollNext = UnitFromHash(UniverseHash::Hash(NextPeriod, 0x50u, 0));

    const double IntensityNow = (PrecipRollNow < PrecipChance)
        ? UnitFromHash(UniverseHash::Hash(ThisPeriod, 0x51u, 0))
        : 0.0;

    const double IntensityNext = (PrecipRollNext < PrecipChance)
        ? UnitFromHash(UniverseHash::Hash(NextPeriod, 0x51u, 0))
        : 0.0;

    Sample.Precipitation = FMath::Lerp(IntensityNow, IntensityNext, Blend);

    // Frozen or not is decided by the temperature where it lands, not by the
    // weather cell. Two places in the same cell at different altitudes get snow
    // and rain respectively, which is correct and is a nice consequence of
    // keeping climate and weather separate.
    constexpr double FreezingK = 273.15;
    Sample.bFrozen = Climate.TemperatureK < FreezingK;

    // --- Fog ---------------------------------------------------------------
    //
    // Humid, cool, calm and low. Not random: fog forms where the air is near
    // saturation and nothing is stirring it, and each of those has a term.
    const double Saturation = FMath::Clamp((Climate.Humidity - 0.6) / 0.4, 0.0, 1.0);
    const double Cool = FMath::Clamp((295.0 - Climate.TemperatureK) / 25.0, 0.0, 1.0);
    const double Low = 1.0 - FMath::Clamp(Climate.AltitudeAboveOceanMeters / 1500.0, 0.0, 1.0);

    const double FogRoll = FMath::Lerp(
        UnitFromHash(UniverseHash::Hash(ThisPeriod, 0x52u, 0)),
        UnitFromHash(UniverseHash::Hash(NextPeriod, 0x52u, 0)),
        Blend);

    Sample.Fog = FMath::Clamp(Saturation * Cool * Low * FogRoll * 1.6, 0.0, 1.0);

    // --- Wind --------------------------------------------------------------
    GetWind(
        Environment, Climate, SimulationTimeSeconds,
        FMath::Max(Sample.Precipitation, Sample.Cloudiness * 0.5),
        Sample.WindDirection, Sample.WindSpeedMs);

    // --- The dominant state ------------------------------------------------
    //
    // Derived from the continuous quantities rather than chosen first and
    // described afterwards. That ordering matters: a state picked first would
    // then need every quantity forced to agree with it, and they would drift.
    if (Sample.Precipitation > 0.55 && Sample.WindSpeedMs > 14.0)
    {
        Sample.State = EWeatherState::Storm;
    }
    else if (Sample.Precipitation > 0.08)
    {
        Sample.State = Sample.bFrozen ? EWeatherState::Snow : EWeatherState::Rain;
    }
    else if (Sample.Fog > 0.35)
    {
        Sample.State = EWeatherState::Fog;
    }
    else if (Sample.Cloudiness > 0.45)
    {
        Sample.State = EWeatherState::Cloudy;
    }
    else
    {
        Sample.State = EWeatherState::Clear;
    }

    // --- Ground wetness ----------------------------------------------------
    //
    // Looks back over the previous few periods rather than tracking state, so
    // ground that was rained on an hour ago is still damp. Reconstructing the
    // history is possible precisely because weather is a function of time, and
    // it is what keeps this stateless while still having memory.
    double Wetness = 0.0;
    double Falloff = 1.0;

    for (int32 Back = 0; Back < 4; ++Back)
    {
        const uint64 PastPeriod =
            UniverseHash::Hash(CellSeed, static_cast<uint64>(PeriodIndex - Back));

        const double PastRoll = UnitFromHash(UniverseHash::Hash(PastPeriod, 0x50u, 0));

        if (PastRoll < PrecipChance)
        {
            Wetness += UnitFromHash(UniverseHash::Hash(PastPeriod, 0x51u, 0)) * Falloff;
        }

        Falloff *= 0.45;
    }

    // Ocean and submerged ground are simply wet.
    Sample.SurfaceWetness = Climate.bOcean ? 1.0 : FMath::Clamp(Wetness, 0.0, 1.0);

    return Sample;
}

FWeatherSample FPlanetWeather::Sample(
    const FPlanetEnvironmentDescriptor& Environment,
    const FClimateSample& Climate,
    double SimulationTimeSeconds)
{
    if (!Climate.bValid)
    {
        return FWeatherSample();
    }

    // Blend the cell the point is in with the cells it is near.
    //
    // Sampling the containing cell alone puts a visible line at every cell
    // boundary - one step and it is raining. Sampling a small ring of nearby
    // directions and averaging turns that line into a gradient a few kilometres
    // wide, which is about what a real weather front looks like from the
    // ground.
    //
    // The offsets are in *direction* space rather than in cell space, so the
    // blend behaves identically across cube faces and corners, where cell
    // neighbours are not simply index arithmetic.
    const FPlanetPatchId Centre = GetCell(Climate.Direction);

    FWeatherSample Result = SampleCell(Environment, Centre, Climate, SimulationTimeSeconds);

    if (!Result.bValid)
    {
        return Result;
    }

    FVector3d TangentU;
    FVector3d TangentV;
    FPlanetTerrain::GetTangentBasis(Climate.Direction, TangentU, TangentV);

    // A quarter of a cell's angular size. Half the sphere's quarter-turn
    // divided by the cell count per face, which is the angular width of a cell.
    const double CellAngle = (PI * 0.5) / static_cast<double>(1 << CellLevel);
    const double Offset = CellAngle * 0.35;

    const FVector3d Probes[4] = {
        FVector3d(Climate.Direction.X + TangentU.X * Offset,
                  Climate.Direction.Y + TangentU.Y * Offset,
                  Climate.Direction.Z + TangentU.Z * Offset),
        FVector3d(Climate.Direction.X - TangentU.X * Offset,
                  Climate.Direction.Y - TangentU.Y * Offset,
                  Climate.Direction.Z - TangentU.Z * Offset),
        FVector3d(Climate.Direction.X + TangentV.X * Offset,
                  Climate.Direction.Y + TangentV.Y * Offset,
                  Climate.Direction.Z + TangentV.Z * Offset),
        FVector3d(Climate.Direction.X - TangentV.X * Offset,
                  Climate.Direction.Y - TangentV.Y * Offset,
                  Climate.Direction.Z - TangentV.Z * Offset),
    };

    double Cloudiness = Result.Cloudiness;
    double Precipitation = Result.Precipitation;
    double Fog = Result.Fog;
    double Wetness = Result.SurfaceWetness;
    int32 Count = 1;

    for (const FVector3d& Probe : Probes)
    {
        const FPlanetPatchId Neighbour = GetCell(Probe.GetSafeNormal());

        if (Neighbour == Centre)
        {
            continue;
        }

        const FWeatherSample Other =
            SampleCell(Environment, Neighbour, Climate, SimulationTimeSeconds);

        Cloudiness += Other.Cloudiness;
        Precipitation += Other.Precipitation;
        Fog += Other.Fog;
        Wetness += Other.SurfaceWetness;
        ++Count;
    }

    if (Count > 1)
    {
        const double Inverse = 1.0 / static_cast<double>(Count);

        Result.Cloudiness = Cloudiness * Inverse;
        Result.Precipitation = Precipitation * Inverse;
        Result.Fog = Fog * Inverse;
        Result.SurfaceWetness = Wetness * Inverse;

        // The state is re-derived from the blended quantities, not carried over
        // from the centre cell, so it agrees with what is actually being
        // rendered at this point rather than with the cell it happens to be in.
        if (Result.Precipitation > 0.55 && Result.WindSpeedMs > 14.0)
        {
            Result.State = EWeatherState::Storm;
        }
        else if (Result.Precipitation > 0.08)
        {
            Result.State = Result.bFrozen ? EWeatherState::Snow : EWeatherState::Rain;
        }
        else if (Result.Fog > 0.35)
        {
            Result.State = EWeatherState::Fog;
        }
        else if (Result.Cloudiness > 0.45)
        {
            Result.State = EWeatherState::Cloudy;
        }
        else
        {
            Result.State = EWeatherState::Clear;
        }
    }

    return Result;
}

void FPlanetWeather::GetWind(
    const FPlanetEnvironmentDescriptor& Environment,
    const FClimateSample& Climate,
    double SimulationTimeSeconds,
    double WeatherIntensity,
    FVector3d& OutDirection,
    double& OutSpeedMs)
{
    OutDirection = FVector3d(1.0, 0.0, 0.0);
    OutSpeedMs = 0.0;

    if (Environment.AtmosphereDensity <= 0.0 || !Climate.bValid)
    {
        return;
    }

    // The tangent plane at this point. East is defined as the direction of
    // rotation, which is the one direction on a rotating sphere that is not
    // arbitrary.
    const FVector3d East =
        FVector3d::CrossProduct(Environment.RotationAxis, Climate.Direction).GetSafeNormal();

    if (East.IsZero())
    {
        // Exactly at a pole, where east is undefined. Rare enough to be worth
        // handling rather than pretending it cannot happen.
        return;
    }

    const FVector3d North = FVector3d::CrossProduct(Climate.Direction, East).GetSafeNormal();

    // --- Prevailing component ----------------------------------------------
    //
    // Real planets have banded winds: trade winds blow west near the equator,
    // westerlies blow east in the mid-latitudes, polar easterlies again near
    // the poles. Three bands, alternating, which is `cos(3 * latitude)` in
    // shape. Approximated here from the latitude sine directly, so the whole
    // thing stays free of trigonometry.
    //
    // No Coriolis force is computed; this is the *result* of one, tabulated.
    const double L = Climate.LatitudeSin;
    const double Banding = 1.0 - 8.0 * L * L * (1.0 - L * L);

    const double Prevailing = Banding * (Environment.RotationPeriodSeconds < 0.0 ? -1.0 : 1.0);

    // --- Local component ---------------------------------------------------
    //
    // Low-frequency noise advected through time, so gusts arrive and pass.
    const uint64 WindSeed =
        Environment.Seed.Stream(UniverseSeedDomain::StreamWeather).Value ^ 0x57494E4400000001ull;

    const double TimePhase = SimulationTimeSeconds / (CellPeriodSeconds * 0.5);

    const FVector3d NoisePoint(
        Climate.Direction.X * 3.0 + TimePhase,
        Climate.Direction.Y * 3.0,
        Climate.Direction.Z * 3.0);

    const double LocalEast = PlanetNoise::FBM(NoisePoint, WindSeed, 2, 1.0);
    const double LocalNorth = PlanetNoise::FBM(NoisePoint, WindSeed ^ 0x9E37ull, 2, 1.0);

    const double EastComponent = Prevailing * 0.7 + LocalEast * 0.6;
    const double NorthComponent = LocalNorth * 0.6;

    FVector3d Direction(
        East.X * EastComponent + North.X * NorthComponent,
        East.Y * EastComponent + North.Y * NorthComponent,
        East.Z * EastComponent + North.Z * NorthComponent);

    Direction = Direction.GetSafeNormal();

    if (Direction.IsZero())
    {
        Direction = East;
    }

    OutDirection = Direction;

    // --- Speed --------------------------------------------------------------
    //
    // Scaled by atmosphere density, because a thin atmosphere cannot carry much
    // momentum, and raised sharply by weather intensity - a storm is mostly
    // wind. The magnitudes are chosen to read correctly: 3 m/s is a breeze that
    // moves grass, 25 m/s bends trees.
    const double Base = 2.0 + FMath::Abs(EastComponent) * 5.0;
    const double Gust = WeatherIntensity * WeatherIntensity * 26.0;

    OutSpeedMs = (Base + Gust) * FMath::Min(Environment.AtmosphereDensity, 2.0);
}
