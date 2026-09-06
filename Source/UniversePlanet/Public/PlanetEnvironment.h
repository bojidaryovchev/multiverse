// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetSurface.h"

/**
 * PlanetEnvironment.h
 *
 * What kind of world this is, as opposed to what shape it is.
 *
 * FPlanetSurfaceDescriptor says where the ground is. This says what the ground
 * is *like*: how much of it is under water, how warm it is, how wet, which way
 * it spins, what grows on it. Everything downstream - climate, biomes,
 * materials, vegetation, weather, wildlife - is derived from these numbers and
 * the planet seed, and from nothing else.
 *
 * The separation is deliberate and load-bearing. Terrain shape is frozen by
 * PlanetTerrainVersion the moment a player builds on it; the environment is
 * frozen by its own version, separately, because the two change for different
 * reasons and at different times. Retuning a humidity curve should not move a
 * mountain, and it should not have to.
 *
 *
 * THE CHAIN THIS SITS AT THE TOP OF
 *
 *     planet physical properties        (this file)
 *              |
 *              v
 *     climate fields                    (PlanetClimate.h)
 *              |
 *              v
 *     biome classification              (PlanetBiome.h)
 *              |
 *              v
 *     materials, vegetation, wildlife   (the Unreal layer)
 *
 * The direction is one-way and it is the point of the sprint. A forest exists
 * because the conditions support a forest, not because a forest was placed
 * there. Nothing further down may reach back up and decide the climate.
 *
 * Pure data. No UObject, no Actor, safe on a worker thread.
 */

/**
 * Environment generation version.
 *
 * Separate from PlanetTerrainVersion, and separate for a reason: bumping this
 * reclassifies every biome on every planet, which turns forests into deserts.
 * Around a persistent player settlement that is a considerably worse outcome
 * than a shifted contour, so it needs its own explicit version rather than
 * riding along with terrain.
 *
 * No migration machinery exists yet, and none is needed yet. What is needed now
 * is that this is not an invisible implementation detail later.
 */
namespace PlanetEnvironmentVersion
{
    inline constexpr uint32 Current = 1;
}

/**
 * Which family of life a planet grows.
 *
 * Not a biome list and not an asset list - a selector for *which* biome table
 * and which content archetypes apply. Sprint 004 asks that an alien world be
 * reachable by changing environmental rules rather than by rebuilding the
 * planet generator, and this is the seam where that happens: the climate model,
 * the ocean, the terrain and the streaming are all identical between the two,
 * and only the table they index changes.
 */
enum class EPlanetBiosphere : uint8
{
    /** No life. Rock, sand, ice and water. */
    Barren = 0,

    /** Earth-like: grasses, broadleaf and coniferous trees, shrubs. */
    Terrestrial = 1,

    /** Fungal-dominant: mushroom canopies, spore meadows, bioluminescence. */
    Fungal = 2,
};

UNIVERSEPLANET_API const TCHAR* LexToString(EPlanetBiosphere Biosphere);

/**
 * A planet's environmental identity.
 *
 * Derived once from the astronomical descriptor and the seed, then treated as
 * read-only. Everything in it is a *planet-wide* property; nothing here knows
 * about a particular place on the surface.
 */
struct UNIVERSEPLANET_API FPlanetEnvironmentDescriptor
{
    /** Which planet this describes. Zero means unset. */
    uint64 PlanetKey = 0;

    /** Environment seed, descended from the planet seed through its own stream. */
    FUniverseSeed Seed;

    // --- Rotation ---------------------------------------------------------

    /**
     * Unit rotation axis, in universe axes.
     *
     * Latitude is defined from this rather than from cube-face coordinates,
     * which is what stops the climate revealing the cube. A cube-derived
     * latitude would put six seams and eight corners into the temperature
     * field, in exactly the places ADR-003 worked hardest to make invisible.
     */
    FVector3d RotationAxis = FVector3d(0.0, 0.0, 1.0);

    /** Sidereal rotation period in seconds. Negative is retrograde. */
    double RotationPeriodSeconds = 86164.0;

    /**
     * Axial tilt in radians, relative to the orbital plane.
     *
     * Stored but not yet used: seasons need an orbital phase, which needs a
     * simulation clock tied to the orbit, which is not this sprint. It is here
     * because the rotation axis it would modify is already here, and adding it
     * later would mean revisiting every latitude call site.
     */
    double AxialTiltRadians = 0.0;

    // --- Thermal ----------------------------------------------------------

    /** Mean surface temperature in kelvin, planet-wide. */
    double MeanSurfaceTemperatureK = 288.0;

    /**
     * Temperature difference between the equator and the poles, in kelvin.
     *
     * Derived rather than fixed: a thick atmosphere redistributes heat and
     * flattens the gradient, a thin one lets the poles freeze. Earth is about
     * 50 K across this range; Venus, with ninety atmospheres, is nearly
     * isothermal.
     */
    double EquatorPoleDeltaK = 50.0;

    /**
     * Temperature drop per kilometre of altitude, in kelvin.
     *
     * Earth's environmental lapse rate is 6.5 K/km. It scales with gravity and
     * inversely with heat capacity; only the gravity term is modelled here,
     * which is enough to make a mountain on a heavy world colder than the same
     * mountain on a light one.
     */
    double LapseRateKPerKm = 6.5;

    // --- Atmosphere and water ---------------------------------------------

    /** Surface atmospheric density relative to Earth. Zero means airless. */
    double AtmosphereDensity = 1.0;

    /**
     * Ocean surface radius, in metres from the planet centre.
     *
     * An absolute radius rather than an offset, because that is what every
     * consumer wants: land-or-water is a comparison against this number, and
     * water depth is a subtraction from it. Zero means a dry world.
     *
     * Resolved from OceanCoverageTarget by search - see
     * FPlanetEnvironment::Resolve - so that the *coverage* is the authored
     * quantity and the level is derived, rather than the other way round. A
     * level chosen directly would give wildly different coverage on two planets
     * with the same number, since it depends entirely on the terrain histogram.
     */
    double OceanRadiusMeters = 0.0;

    /** Fraction of the surface below OceanRadiusMeters, as actually measured. */
    double OceanCoverage = 0.0;

    // --- Life -------------------------------------------------------------

    EPlanetBiosphere Biosphere = EPlanetBiosphere::Barren;

    /**
     * Planet-wide humidity offset, before any local term. In [0, 1].
     *
     * A dry world biases every region dry; a wet one biases every region wet.
     * This is what makes two planets with identical terrain feel different.
     */
    double HumidityBias = 0.5;

    /**
     * How much life the world supports at all, in [0, 1]. Scales every
     * vegetation density downstream, so a marginal world is sparse everywhere
     * rather than being lush in its few good spots.
     */
    double VegetationPotential = 0.0;

    // --- Sky --------------------------------------------------------------

    /** Planet-wide cloud coverage bias, in [0, 1]. */
    double CloudCoverageBias = 0.4;

    /** Baseline chance that a weather cell is precipitating, in [0, 1]. */
    double StormPotential = 0.2;

    /** The version that produced this. See PlanetEnvironmentVersion. */
    uint32 GenerationVersion = PlanetEnvironmentVersion::Current;

    bool IsValid() const { return PlanetKey != 0 && GenerationVersion != 0; }

    bool HasOcean() const { return OceanRadiusMeters > 0.0; }

    bool HasLife() const { return Biosphere != EPlanetBiosphere::Barren && VegetationPotential > 0.0; }

    /** Length of one solar day, in seconds. Absolute, so retrograde counts. */
    double GetDayLengthSeconds() const { return FMath::Abs(RotationPeriodSeconds); }

    FString ToDebugString() const;
};

/**
 * Builds the environmental descriptor, and resolves the ocean level.
 */
class UNIVERSEPLANET_API FPlanetEnvironment
{
public:
    /**
     * Number of directions sampled when resolving the ocean level.
     *
     * A fixed, seed-derived set, so the resolved level is deterministic - which
     * matters, because the ocean level decides the coastline and the coastline
     * decides where biomes sit. A sampling count that varied by machine would
     * make the same planet a different planet.
     *
     * 4096 gives roughly 1.5% standard error on the coverage estimate, which is
     * finer than the difference between any two coverage targets that would be
     * generated, and costs a few milliseconds once per planet.
     */
    static constexpr int32 OceanSampleCount = 4096;

    /**
     * Builds the environment from the planet's astronomy and terrain.
     *
     * Needs the terrain settings because resolving the ocean level requires
     * evaluating the terrain function. That is the only place the environment
     * depends on terrain *tunables* rather than terrain shape, and it is why
     * PatchResolution is excluded from what the terrain function returns - the
     * ocean level must not change when someone lowers their detail setting.
     */
    static FPlanetEnvironmentDescriptor Resolve(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        const struct FPlanetDescriptor& Astronomy);

    /**
     * Finds the radius below which the given fraction of the surface lies.
     *
     * Binary search on radius, evaluating the terrain at a fixed set of
     * directions. Exposed separately so tests can drive it with a known
     * coverage and check the result rather than inferring it.
     */
    static double ResolveOceanRadius(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        double TargetCoverage,
        double& OutActualCoverage);

    /** Fraction of sampled directions whose terrain lies below a radius. */
    static double MeasureOceanCoverage(
        const FPlanetSurfaceDescriptor& Planet,
        const FPlanetTerrainSettings& Settings,
        double OceanRadiusMeters);

    /**
     * The n-th of a fixed set of directions spread evenly over the sphere.
     *
     * A Fibonacci spiral, which is as close to uniform as a deterministic
     * sequence gets and - unlike a lat/long grid - does not concentrate its
     * samples at two points on an axis this planet has no particular reason to
     * care about.
     */
    static FVector3d GetSampleDirection(int32 Index, int32 Count);
};
