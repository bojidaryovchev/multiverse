# Sprint 004 — Living Planet Foundation

**Status: substantially complete, with named gaps.** A planet's physics now
produce its climate, its climate produces its biomes, and its biomes produce what
you see and walk through. Several visual systems the sprint asked for are not
built, and they are listed explicitly below rather than glossed.

![A living world from space](Sprint-004/Biomes-FromOrbit.png)

*Standing in a temperate forest, with the environment HUD:*

![A procedural forest](Sprint-004/Surface-HUD.png)

---

## What was built

| | |
| --- | --- |
| **Environmental descriptor** | A planet's environmental identity, derived from its astronomy |
| **Climate fields** | Temperature and moisture, from latitude, altitude, ocean and noise |
| **Biome system** | A data-driven table of boxes in climate space, returning weighted blends |
| **Oceans** | Level resolved from a coverage target; land/water and depth queries |
| **Terrain appearance** | Ground colour from the biome blend, sampled per vertex during meshing |
| **Vegetation** | Archetype profiles per biome; deterministic scatter; streamed, bounded |
| **Wildlife** | Population-density birds, existing only near the player |
| **Weather** | A pure function of (planet, place, time), on a coarse cube-sphere grid |
| **Wind** | Prevailing latitude bands plus local gusts |
| **Atmosphere and clouds** | Planet-shaped sky and volumetric clouds, from the descriptor |
| **Day/night** | The planet rotates; the star does not move |
| **Environment query** | One call answering everything, usable headless |
| **Alien biosphere** | A second biome and content table, generated naturally on 20% of habitable worlds |

Architecture in
[PlanetEnvironment.md](../Architecture/PlanetEnvironment.md),
[ClimateSystem.md](../Architecture/ClimateSystem.md),
[Biomes.md](../Architecture/Biomes.md),
[Weather.md](../Architecture/Weather.md),
[ProceduralVegetation.md](../Architecture/ProceduralVegetation.md), and
[ADR-005](../ADR/ADR-005-procedural-environment.md).

---

## Verification

### Automated tests

```
Tools\StandaloneTests\RunTests.bat
  64/64 tests passed, 1,139,810/1,139,810 assertions passed
```

Seven new test bodies, running identically standalone and in Unreal automation:

| Test | What it pins |
| --- | --- |
| `PlanetEnvironmentDescriptor` | Ocean level hits its coverage target within one sample and is monotonic in it; sample directions are unit, deterministic and uniform over the sphere |
| `ClimateFields` | Latitude, the temperature gradient and lapse rate, humidity bounds, and **cube-face continuity across all twelve edges** |
| `BiomeClassification` | Climate families map to biome families; steep ground is rock; submerged is ocean; blends are well-formed and actually blend |
| `PlanetOcean` | Land/water classification and depth agree with a single comparison, at three planet radii; a dry world has no water anywhere |
| `EnvironmentQuery` | 512 points: nothing NaN, everything bounded, wind tangential to 1e-9, determinism bit-exact |
| `PlanetWeather` | Regional not global; a pure function of time; it evolves; transitions never jump; airless worlds have none |
| `VegetationPlacement` | Determinism to the position; inside the patch; never in the ocean; barren grows only rock; fungal substitutes archetypes at comparable density |

The cube-face continuity assertion is the one worth singling out. Climate is
sampled at millions of points per planet, and a latitude derived from face
coordinates would put a visible discontinuity along every cube edge — exactly
where ADR-003 worked hardest to make the topology invisible. Measured worst case
across all twelve edges: **under 0.5 K and under 0.02 humidity**, between points
30 m apart.

### Whole-planet survey

`universe.EnvInfo 4096` on the demo world:

```
Env  Terrestrial  T=280.0 K (dT 50)  atm=1.92  ocean 62% @ 4209.8 km
     humid=0.59  veg=0.92  cloud=0.92  day=32.9 h  env v1

Planet survey over 4096 points: -32.2 C to 32.3 C
  Ocean            61.6%       Taiga             3.5%
  TemperateForest  10.3%       Snow              2.7%
  BarrenRock       10.0%       Wetland           0.7%
  Rainforest        5.6%       Coast             0.4%
  Tundra            5.2%       Grassland         0.0%
```

Ten biomes occur on one planet, spanning 64 K of temperature. Ocean coverage
matches the 62% the descriptor resolved.

### Measured performance

Standing in a temperate forest, 25 m above the ground, holding steady:

```
Vegetation     51,779 instances in 40 patches
               canopy 26,797   understory 9,238   ground 7,564   rock 8,180
Env streaming  0 jobs in flight   51,779 placed   0 released
Terrain        178 patches   1,549,312 triangles   798,330 vertices   LOD 11
               select 0.25 ms   upload 0.00 ms   avg patch gen 47.11 ms
Wildlife       1 bird (a 37%-forest blend, in rain)
```

Vegetation placement runs **12–75 ms per patch-layer** on a worker thread.
Terrain patch generation rose from 40 ms to 47 ms with per-vertex environment
sampling added — about 18%, for ground that is coloured by what it is.

Counts hold constant while stationary and are bounded by construction:
`MaxTotalInstances` is 60,000 and `MaxObserverAltitudeMeters` is 3,000, above
which no environmental content is generated at all.

---

## Defects found, and how

Every one was found by running the game and reading a number or a screenshot.
None was found by reading the code.

1. **The demo landed on a 486 K airless rock.** The Sprint 002 rule — nearest
   system, outermost planet — is a correct output of the generator and shows
   nothing about climate or life. The startup search now scores nearby systems
   for habitability.

2. **Camera exposure was applied only inside the planetary frame**, so any view
   from outside the influence radius fell back to auto-exposure and rendered a
   sunlit planet as a white disc. Exposure follows the light, not the frame.

3. **Vegetation relevance measured altitude above the ocean**, so standing on a
   four-kilometre plateau read as flying and switched the forest off underfoot.
   On a world whose terrain reaches six kilometres, that is every mountain
   forest on the planet.

4. **Patch distance was measured through space rather than along the surface**,
   folding the observer's own elevation into every measurement — so a player on a
   plateau measured the patch under their feet as kilometres away, and a
   1,400 m activation radius selected nothing at all.

5. **Hitting the per-patch instance budget truncated the scatter**, filling one
   corner of the patch and leaving the rest empty — a forest with a straight edge
   through the middle of it. Capping the *grid resolution* by the budget thins
   uniformly instead.

6. **The atmosphere rendered the ground pure black.** An atmosphere sun light is
   attenuated by transmittance, evaluated in single precision against a shell of
   millions of units, and at ground level that evaluated to zero: a forest at
   noon under a correctly-lit blue sky, black. Two false leads were eliminated
   first (volumetric clouds, then aerial perspective), and origin-relative
   atmosphere transform made it worse because it assumes world +Z is up. The fix
   splits the roles: one light scatters the sky and illuminates nothing, one
   lights the world.

7. **A black disc dead centre of every screenshot**, which looked convincingly
   like a body drawn in the wrong place. It was the probe's own hull four metres
   in front of its camera — legible as a silhouette under Sprint 002's clamped
   lighting, and genuinely black under physical exposure.

8. **Overriding `DepthOfFieldFstop` for exposure silently enables physical depth
   of field**, and with no focal distance every bright point becomes a large
   defocused disc. Aperture is now left alone and the shutter carries the whole
   exposure adjustment.

---

## What was *not* done

Stated so the gaps are known rather than discovered later. Each is an explicit
Sprint 004 acceptance item.

- **No rendered water surface.** Oceans are resolved, classified, queried for
  depth, coloured on the terrain and visible from orbit — but there is no water
  plane, no water material, no reflections and no waves. Underwater detection
  (`IsUnderwater`, `WaterDepth`) works; nothing draws it.
- **No precipitation or fog visuals.** The weather field is complete and
  correct — state, intensity, frozen, fog density, wetness, wind — and drives
  nothing visible except cloud coverage. No rain particles, no fog volume, no
  wetness on materials.
- **No wind response in vegetation.** `GetWindAtLocation` exists and is on the
  HUD; nothing bends.
- **No vegetation LOD, impostors or canopy representation.** The canopy
  activation radius is a visible boundary at 1,400 m, exactly the "trees appear
  at 2 km" problem section 33 asks to avoid. Below the altitude cut-off there is
  full detail; above it, nothing.
- **No environmental audio.**
- **No PCG integration.** Placement is bespoke. Unreal PCG was not used, so its
  determinism characteristics were not evaluated.
- **The terrain material is still vertex colour.** Slope, snow and coast
  treatments come from the *biome classifier* rather than from material layers,
  so they are correct but flat — no textures, no blending, no wet-ground
  response.
- **Ocean proximity is approximated by altitude**, so a dry inland basin below
  sea level is classified humid.
- **No seasons.** Axial tilt is stored and unused.
- **Day/night does not move the stars**, only the sun — the scaled-space bodies
  are hidden on a surface (ADR-004) and would need the deferred far-field pass.
- **The alien profile has not been visually validated.** The fungal table is
  implemented, generated on 20% of habitable worlds and asserted by test, but no
  fungal planet has been landed on and photographed.
- **Manual acceptance tests for desert, cold region and coast were not run.**
  `universe.GotoBiome` exists to run them; forest and grassland were validated.

---

## Commands added

| Command | Effect |
| --- | --- |
| `universe.EnvInfo [samples]` | Environment here, plus a whole-planet biome survey |
| `universe.GotoBiome <name> [height] [minSun]` | Find a named biome in daylight and go there |
| `universe.GotoSubstellar [height] [elevationDeg]` | Go to a chosen solar elevation |
| `universe.TimeScale <n>` | Accelerate day/night and weather |
| `universe.Vegetation 0/1`, `universe.Wildlife 0/1` | Toggle environmental content |
| `universe.VegetationLogInterval <s>` | Log vegetation streaming state |
| `universe.TerrainDebugMode 0/5/6/7` | Biome / temperature / humidity / biome index |

---

## Recommended Sprint 005

Sprint 005 is persistence, and the smallest thing that proves it is:

> Place a structure on a planet, leave, quit, restart, return, and find it still
> there — with the procedural world around it regenerated from seed, unchanged.

The pieces that need to exist:

- a persistent entity identity that survives a restart and is not a pointer, a
  `FName` index or an array position;
- a world-delta model: what the player changed, stored *against* the procedural
  world rather than replacing it;
- region-scoped delta loading, keyed the same way patches are, so returning to a
  place loads only that place's changes;
- generation-version compatibility, since `PlanetTerrainVersion` and
  `PlanetEnvironmentVersion` both already exist and both invalidate saved worlds
  when bumped — the machinery to detect that is the point, not migration itself;
- a local store (SQLite or equivalent) behind an abstraction, because the same
  interface has to work against a server later.

The temptation to avoid is making persistence a save of the *world*. The world is
a function of a seed and must stay that way; only the difference is stored.
