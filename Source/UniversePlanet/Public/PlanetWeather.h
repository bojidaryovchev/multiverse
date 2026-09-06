// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetClimate.h"
#include "PlanetPatchId.h"

/**
 * PlanetWeather.h
 *
 * What the sky is doing here, right now.
 *
 *
 * IDENTITY VERSUS STATE
 *
 * Everything else in this module is a planet's *identity*: terrain, climate,
 * biomes and vegetation are fixed properties of a seed, and asking twice gives
 * the same answer forever. Weather is the first thing that is not. It changes,
 * and a system that changes needs a different contract.
 *
 * The contract chosen is: **weather is a pure function of (planet, place,
 * time)**. It evolves, but it is not simulated, and no state is carried between
 * frames. That has three consequences worth stating:
 *
 *   - Two machines given the same planet and the same simulation time compute
 *     the same weather, with nothing to synchronise. When multiplayer arrives,
 *     the server needs to agree on a clock and nothing else.
 *   - Weather can be queried for the past or the future as cheaply as for now,
 *     which a forecast, an approach planner or a debug scrub all want.
 *   - Nothing accumulates. A player who flies away for an hour and returns
 *     finds weather that moved on, not weather that was paused or that drifted.
 *
 * The cost is that weather cannot *respond* to anything - a storm cannot be
 * caused by an event, only by the clock. That is the right trade for now and it
 * is recorded as the thing to revisit.
 *
 *
 * CELLS, NOT A GLOBAL SWITCH
 *
 * Weather is evaluated on a coarse grid over the sphere - the same cube-sphere
 * quadtree as terrain, at a fixed shallow level, so a "weather cell" is just an
 * FPlanetPatchId and all the existing addressing works unchanged.
 *
 * At level 3 that is 6 x 64 = 384 cells, each about 700 km across on an
 * Earth-sized world, which is roughly the scale of a real synoptic weather
 * system. A single global state would mean the whole planet raining at once; a
 * cell per terrain patch would mean crossing a rain boundary every few hundred
 * metres.
 *
 * Neighbouring cells are blended, so crossing a cell boundary is a gradient
 * rather than a line.
 *
 * Pure mathematics on plain data. No engine types, no tick, no state.
 */

/** What the sky is doing. */
enum class EWeatherState : uint8
{
    Clear = 0,
    Cloudy,
    Rain,
    Storm,
    Snow,
    Fog,

    Count
};

UNIVERSEPLANET_API const TCHAR* LexToString(EWeatherState State);

/**
 * The weather at a point.
 *
 * Continuous quantities rather than only a state, because a renderer wants to
 * fade rather than switch, and because "how hard is it raining" is a more
 * useful question than "is it raining".
 */
struct UNIVERSEPLANET_API FWeatherSample
{
    /** The dominant state, for logic and for picking effects. */
    EWeatherState State = EWeatherState::Clear;

    /** Cloud cover, [0, 1]. */
    double Cloudiness = 0.0;

    /** Precipitation intensity, [0, 1]. Zero when not precipitating. */
    double Precipitation = 0.0;

    /** True when precipitation falls as snow rather than rain. */
    bool bFrozen = false;

    /** Fog density, [0, 1]. */
    double Fog = 0.0;

    /** Wind direction, unit, in the local tangent plane. */
    FVector3d WindDirection = FVector3d(1.0, 0.0, 0.0);

    /** Wind speed in metres per second. */
    double WindSpeedMs = 0.0;

    /**
     * How wet the ground is, [0, 1]. Lags precipitation, so ground stays wet
     * for a while after rain stops - which is what makes rain feel like it
     * happened rather than like it was switched off.
     */
    double SurfaceWetness = 0.0;

    bool bValid = false;
};

class UNIVERSEPLANET_API FPlanetWeather
{
public:
    /**
     * Quadtree level the weather grid lives at.
     *
     * 3 gives 384 cells over the sphere. On a 6,371 km planet each is about
     * 700 km across, which is the scale of a real weather system; on a small
     * moon they are proportionally smaller, which is arguably wrong but is at
     * least consistent, and a fixed *angular* size keeps the cell count - and
     * therefore the cost - independent of planet size.
     */
    static constexpr uint8 CellLevel = 3;

    /**
     * How long a weather cell holds one condition, in seconds.
     *
     * Six hours. Long enough that a player notices weather persisting rather
     * than flickering, short enough that waiting out a storm is measured in
     * minutes of accelerated time rather than hours.
     */
    static constexpr double CellPeriodSeconds = 6.0 * 3600.0;

    /**
     * Fraction of a period spent transitioning between conditions.
     *
     * A quarter, so weather spends most of its time settled and the rest
     * visibly changing. A hard cut at the period boundary would make the sky
     * change between one frame and the next.
     */
    static constexpr double TransitionFraction = 0.25;

    /** The cell containing a surface direction. */
    static FPlanetPatchId GetCell(const FVector3d& Direction);

    /**
     * Weather in one cell at one moment, before neighbour blending.
     *
     * Exposed mainly for testing and for the debug overlay; gameplay should ask
     * Sample, which blends.
     */
    static FWeatherSample SampleCell(
        const FPlanetEnvironmentDescriptor& Environment,
        const FPlanetPatchId& Cell,
        const FClimateSample& Climate,
        double SimulationTimeSeconds);

    /**
     * Weather at a point, blended across the cell boundary.
     */
    static FWeatherSample Sample(
        const FPlanetEnvironmentDescriptor& Environment,
        const FClimateSample& Climate,
        double SimulationTimeSeconds);

    /**
     * Wind at a point.
     *
     * Two terms: a prevailing component that depends on latitude, and a local
     * component that varies with place and time. The prevailing term is the
     * reason wind is not simply noise - real winds run east or west depending
     * on the band you are in, and having that be true makes a planet feel
     * rotationally coherent even without a circulation model.
     */
    static void GetWind(
        const FPlanetEnvironmentDescriptor& Environment,
        const FClimateSample& Climate,
        double SimulationTimeSeconds,
        double WeatherIntensity,
        FVector3d& OutDirection,
        double& OutSpeedMs);
};
