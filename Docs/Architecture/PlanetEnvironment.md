# Planet Environment

How a planet's physical properties become a place you can recognise.

Related: [ClimateSystem.md](ClimateSystem.md), [Biomes.md](Biomes.md),
[Weather.md](Weather.md), [ProceduralVegetation.md](ProceduralVegetation.md),
[ADR-005](../ADR/ADR-005-procedural-environment.md).

![A living world from space](../Sprints/Sprint-004/Biomes-FromOrbit.png)

---

## The chain

```
planet physical properties      radius, mass, orbit, star luminosity, rotation
          |
          v
environmental identity          temperature, atmosphere, ocean, biosphere
          |
          v
climate fields                  temperature and moisture at a point
          |
          v
biome classification            what those conditions produce
          |
          v
content                         ground colour, what grows, what flies, weather
```

**One-way, and that is the point.** A forest exists because the conditions
support a forest. Nothing places a forest, and nothing downstream may reach back
up and decide the climate.

Every arrow is a pure function of the layer above it and the planet seed. There
is no stored state anywhere in the chain, which is what lets any part of a
planet be evaluated in isolation, on any thread, in any order.

---

## Where it lives

| | |
| --- | --- |
| `PlanetEnvironment.h` | The descriptor: what kind of world this is |
| `PlanetClimate.h` | Temperature and moisture fields |
| `PlanetBiome.h` | The biome table and the classifier |
| `PlanetVegetation.h` | What each biome grows, and where each individual stands |
| `PlanetWeather.h` | What the sky is doing, as a function of place and time |
| `PlanetEnvironmentQuery.h` | One question, one answer |

All in `UniversePlanet`, so all of it runs in the standalone test harness, on
worker threads, and in a future headless server that has no renderer at all.

The Unreal layer — `UPlanetVegetationComponent`, `UPlanetWildlifeComponent`, the
sky and cloud components on `APlanetActor` — consumes this and adds nothing to
it.

---

## The environmental descriptor

Derived once from the astronomical descriptor and the seed, then read-only.

```cpp
FPlanetEnvironmentDescriptor Environment =
    FPlanetEnvironment::Resolve(Surface, Settings, Astronomy);
```

Everything in it is planet-wide. Nothing in it knows about a particular place.

| Field | Derived from |
| --- | --- |
| `MeanSurfaceTemperatureK` | equilibrium temperature + greenhouse × atmosphere density |
| `EquatorPoleDeltaK` | atmosphere density — thick air flattens the gradient |
| `LapseRateKPerKm` | surface gravity, as `g/c_p` with Earth's heat capacity |
| `AtmosphereDensity` | whether the body retains air, then seeded |
| `OceanRadiusMeters` | resolved from a coverage target — see below |
| `Biosphere`, `VegetationPotential` | air + water + a temperature life can work at |
| `HumidityBias` | ocean coverage; that is where atmospheric water comes from |
| `CloudCoverageBias`, `StormPotential` | humidity × atmosphere density |
| `RotationAxis`, `RotationPeriodSeconds` | carried from the astronomy |

A world that is nine-tenths ocean is humid in its interiors and cloudy
everywhere, and that is not a rule — it is `HumidityBias` being a function of
`OceanCoverage`.

### The ocean level is resolved from coverage, not chosen

```cpp
OceanRadiusMeters = FPlanetEnvironment::ResolveOceanRadius(
    Planet, Settings, TargetCoverage, OutActualCoverage);
```

A sea level picked directly means nothing without knowing the terrain histogram:
the same number gives an ocean world on one planet and a dry one on the next.
So the *coverage* is the authored quantity. 4,096 fixed directions are sampled,
the resulting radii sorted, and the level read off at the target percentile —
which is both exact for the sample set and far cheaper than a binary search that
would re-evaluate the terrain on every iteration.

The actual coverage achieved is reported alongside, because it differs from the
target when the terrain histogram is flat somewhere, and that is exactly the
case worth knowing about.

### Sample directions avoid transcendentals

The obvious way to spread points evenly on a sphere is a Fibonacci spiral. It is
better distributed than what is used here, and it is built from `sin` and `cos`.

These directions decide the ocean level, and the ocean level decides every
coastline on the planet. Anything on that path inherits libm's cross-platform
ambiguity — the open risk [ADR-002](../ADR/ADR-002-seed-hierarchy.md) records for
star generation and that [ADR-003](../ADR/ADR-003-planet-topology-and-lod.md)
deliberately kept off the terrain path.

So directions come from rejection sampling in the unit cube: multiply, compare,
one square root, all exactly specified by IEEE-754. It terminates in 2.1 draws on
average and the draw count is itself a function of the hash. Slightly worse
spacing, a bit-identical coastline.

---

## The unified query

```cpp
FEnvironmentSample Sample = FPlanetEnvironmentQuery::Sample(
    Planet, Environment, Settings, Direction, SimulationTimeSeconds);

Sample.GetBiome();            // EPlanetBiome
Sample.GetTemperatureCelsius();
Sample.GetHumidity();
Sample.IsOcean();
Sample.GetWaterDepthMeters();
Sample.GetWeatherState();
Sample.GetWindSpeedMs();
Sample.GetSurfaceWetness();
```

Everything downstream asks this rather than assembling the answer itself, for two
reasons.

**Consistency.** Five systems reproducing the same derivation will eventually
disagree about one of them, invisibly: the material says desert, the spawner says
grassland, and nobody notices until there are birds over the sand.

**Cost.** A climate sample is a terrain evaluation plus a normal — four noise
evaluations. A caller asking for temperature, then humidity, then biome pays for
it three times.

`SampleStatic` omits the weather for callers that do not care what the sky is
doing, and leaves `Weather.bValid` false so an unasked question is distinguishable
from a clear sky. `SampleWithTerrain` is for callers that already have the
elevation and normal — the patch mesher calls it per vertex, which is why the
ground is coloured by biome at no extra terrain cost.

---

## Versioning

`PlanetEnvironmentVersion` is separate from `PlanetTerrainVersion`, deliberately.
Bumping the environment version reclassifies every biome on every planet, turning
forests into deserts — a considerably worse outcome around a persistent player
settlement than a shifted contour, and one that happens for entirely different
reasons than a terrain change. They should not be able to force each other.

Gravity and atmosphere height are excluded from
`FPlanetSurfaceDescriptor::GetContentHash` for the same reason: they change how a
planet *behaves*, not where its ground is, and folding them in would invalidate
every cached patch the first time an atmosphere model was tuned.

---

## Choosing a world to show

The game mode searches nearby systems for a habitable planet rather than taking
the nearest one. Under the Sprint 002 rule — nearest system, outermost planet —
the demo landed on a 486 K airless rock. That is a perfectly correct output of
the generator and shows nothing whatever about climate, biomes or life.

This is a *presentation* decision and not a generation one. The universe is
unchanged, the barren rock is still there, and if no habitable planet is found
the nearest system is used and the fact is logged rather than hidden.

```
Habitable search: 48 systems, 196 planets considered, 43 environments resolved.
Chose Corurnar-3267 planet 3 (score 0.811).
Env  Terrestrial  T=280.0 K (dT 50)  atm=1.92  ocean 62% @ 4209.8 km
     humid=0.59  veg=0.92  cloud=0.92  day=32.9 h  env v1
```

---

## Inspecting a planet

```
universe.EnvInfo [samples]      the environment here, and a whole-planet survey
universe.GotoBiome <name>       find a named biome in daylight and go there
universe.TerrainDebugMode 5     temperature      6 humidity      7 biome index
universe.TimeScale <n>          accelerate day/night and weather
```

A survey of the demo world, 4,096 points:

```
Planet survey: -32.2 C to 32.3 C
  Ocean            61.6%
  TemperateForest  10.3%
  BarrenRock       10.0%
  Rainforest        5.6%
  Tundra            5.2%
  Taiga             3.5%
  Snow              2.7%
  Wetland           0.7%
  Coast             0.4%
  Grassland         0.0%
```
