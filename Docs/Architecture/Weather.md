# Weather

What the sky is doing here, right now.

Related: [ClimateSystem.md](ClimateSystem.md),
[PlanetEnvironment.md](PlanetEnvironment.md).

---

## Identity versus state

Everything else in the environment is a planet's **identity**: terrain, climate,
biomes and vegetation are fixed properties of a seed, and asking twice gives the
same answer forever. Weather is the first thing that is not.

The contract chosen is:

> **Weather is a pure function of (planet, place, time).**

It evolves, but it is not simulated, and no state is carried between frames.
Three consequences follow, and all three are the reason for the choice:

- Two machines given the same planet and the same simulation time compute the
  same weather, with nothing to synchronise. When multiplayer arrives, the
  server needs to agree on a clock and nothing else.
- Weather can be queried for the past or the future as cheaply as for now, which
  a forecast, an approach planner and a debug scrub all want.
- Nothing accumulates. A player who flies away for an hour and returns finds
  weather that moved on, not weather that was paused or that drifted.

The cost is that weather cannot **respond** to anything - a storm can be caused
by the clock and by nothing else. That is the right trade now and it is the
thing to revisit first.

---

## Cells, not a global switch

Weather is evaluated on a coarse grid over the sphere: the same cube-sphere
quadtree as terrain, at level 3. A weather cell is literally an
`FPlanetPatchId`, so all the existing addressing - including behaving correctly
across cube faces and corners - works unchanged.

That is 6 x 64 = 384 cells, about 700 km across on an Earth-sized world, which is
roughly the scale of a real synoptic weather system. A single global state would
mean the whole planet raining at once; a cell per terrain patch would mean
crossing a rain boundary every few hundred metres.

Cells are blended across their boundaries by sampling a small ring of nearby
directions and averaging, which turns the line into a gradient a few kilometres
wide - about what a real front looks like from the ground. The offsets are in
**direction** space rather than cell space, so the blend behaves identically at
face boundaries where neighbours are not simply index arithmetic.

---

## Within a cell

Each cell holds one condition for a six-hour period, with the last quarter of
each period blended into the next by a smoothstep. Long enough to notice weather
persisting, short enough to wait out a storm in minutes of accelerated time, and
never changing between one frame and the next.

```
cloudiness    planet bias + humidity + per-period random
precipitation requires cloud AND moisture AND the planet's storm potential
frozen        decided by the temperature where it lands, not by the cell
fog           humid AND cool AND low AND calm - each a term, none random
wind          prevailing band + local noise advected through time
```

Rain in a desert is rare because a desert has neither the humidity nor usually
the cloud - the model doing its job, rather than a rule saying "deserts do not
rain".

Whether precipitation falls as snow is decided by the local temperature rather
than by the cell, so two places in the same cell at different altitudes get rain
and snow respectively. That is correct, and it is a consequence of keeping
climate and weather separate.

The dominant `EWeatherState` is **derived from** the continuous quantities rather
than chosen first and described afterwards. That ordering matters: a state picked
first would need every quantity forced to agree with it, and they would drift.

---

## Ground wetness, without state

`SurfaceWetness` looks back over the previous four periods and accumulates their
precipitation with a decay, so ground that was rained on an hour ago is still
damp.

Reconstructing history like that is possible precisely because weather is a
function of time. It is what gives the system memory while remaining stateless.

---

## Wind

Two terms.

**Prevailing.** Real planets have banded winds: trade winds blow west near the
equator, westerlies blow east in the mid-latitudes, polar easterlies again near
the poles. Three alternating bands, which is `cos(3 * latitude)` in shape,
approximated here from the latitude sine directly so the whole thing stays free
of trigonometry. No Coriolis force is computed; this is the *result* of one,
tabulated.

**Local.** Low-frequency noise advected through time, so gusts arrive and pass.

East is defined as the direction of rotation - the one direction on a rotating
sphere that is not arbitrary - and the wind vector is built in the local tangent
plane from it. A wind blowing into the ground or up into the sky would be a basis
error, and the tests assert it is tangential to 1e-9.

Speed scales with atmospheric density, because a thin atmosphere cannot carry
much momentum, and rises sharply with weather intensity: 3 m/s moves grass,
25 m/s bends trees.

---

## Testing

`Universe.Planet.PlanetWeather` asserts the properties rather than the values:

- **Regional, not global.** Over 512 points at one moment, at least two distinct
  states occur and no single state covers the entire planet. This is the
  assertion that would catch a system where the whole sky switches at once.
- **A pure function of time.** The same query twice is bit-identical.
- **It actually evolves.** Cloudiness varies by more than 0.15 across 60 periods
  at one place - otherwise "weather" is a constant with extra steps.
- **Transitions are gradual.** Stepping through two full periods at 30-second
  intervals, cloudiness never jumps more than 0.05 in a step.
- An airless world has no weather at all, and precipitation below freezing is
  snow.

---

## Debug

```
universe.EnvInfo            weather, wind and wetness at the player
universe.TimeScale <n>      accelerate weather and the day/night cycle
```

The HUD's ENVIRONMENT section shows state, cloud, precipitation, fog, wetness
and wind speed continuously.
