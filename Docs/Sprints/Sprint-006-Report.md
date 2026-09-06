# Sprint 006 Report — Interstellar & Galactic Travel

*Unreal Engine 5.8.2, Windows 11. All figures below were produced by running the
commands quoted, not estimated.*

## What this sprint proves

**An astronomically large procedural universe can be navigated continuously
between star systems, and it is unchanged by having been left.**

```text
universe.InterstellarJourney

  Result        : PASS
  Home          : Zarelra-1252 (0x7AABB69FDC5E9456)
  Destination   : Aelonis-3673 (0xB0D054604A74A988)
  Leg distance  : 12.7140 ly
  Outbound      : 136.2 s
  Return        : 136.3 s
  Peak speed    : 3.336e+06 c
  Odometer      : 25.4279 ly
  Hazard stops  : 3 during the run
  Streaming     : 19 tracked, 18 generated, 31 transitions
  Discovery     : 13 detected, 2 visited
```

The line that matters is `Home ... 0x7AABB69FDC5E9456`. That content hash was
recorded before departure and checked again after the return, by which point
every actor in the home system had been destroyed and rebuilt from its address.
If procedural regeneration depended on anything but the seed and the address - a
shared RNG, a cache keyed on visit order - this is where it would differ, and
nothing short of actually leaving and coming back would catch it.

## The three layers built

### 1. The galaxy layer

A galaxy is a **density function, not a container**. It does not hold stars; it
says how likely a star is at a place, and sector generation multiplies its own
count by that. Outside every galaxy the density is exactly zero.

Two consequences, both demonstrated:

```text
universe.GalaxyInfo      (starting position)
  Galaxy     : Astrapha-3177  Spiral  r=51993 ly  disk +/-1614 ly  2 arms
  Galactic r : 26035 ly of 51993 (50.1% out)
  Above disk : -11 ly (disk half-thickness 1614)
  Density    : 0.0805 of core
  Sector     : [210773, -146135, -55837], 1 systems, density 0.0803

universe.WarpJump 60000 ; universe.GalaxyInfo
  Galaxy     : none - this is intergalactic space, and it is genuinely empty.
  Nearest    : Astrapha-3177, 57406 ly away
  Systems    : 0 tracked

universe.WarpJump 1200000 ; universe.GalaxyInfo
  Intergalactic cell: [1, -1, -1] of 1277836 ly each
  Galaxy     : none
  Nearest    : Astrapha-7716, 798970 ly away
```

That is Sprint 006 sections 71 and 72 - galaxy exit and a second galaxy -
without a scripted special case for either. The player left the disk and the
stars stopped existing because the density says they do not exist there.

The star generator was **not modified** to accommodate this. The density roll is
a separate draw from its own stream, so a sector's base count is unchanged by
whether a galaxy exists; without that, adding galaxies would have silently
renumbered every system in the universe.

### 2. The travel layer

One canonical position and one canonical velocity, five regimes, and two things
that only matter at speed:

- **Swept trajectories.** At warp a frame covers more than a star's diameter, so
  the ship skips over it rather than passing through it and no position-based
  collision check sees anything. Every warp step is intersected analytically
  against the sectors it crosses before it is committed. Three stops fired
  during the acceptance journey:

  ```text
  Warp step stopped short of Sector [210773, -146136, -55839] #0 (star) at 0.6454 AU
  Warp step stopped short of Sector [210773, -146135, -55837] #0 (star) at 0.5776 AU
  ```

- **Arrival, not proximity.** Braking begins from `v²/2a`, and an auto-braked
  step is additionally clamped to its closest approach to the target. See the
  defect log below for why the second is necessary as well as the first.

Cost scales with path length rather than with the size of the galaxy: a 3D DDA
walks the sectors the segment crosses, and candidate systems are rejected
against a bounding sphere before anything is generated.

### 3. The streaming layer

`Unknown → DescriptorOnly → DistantVisual → NearbyVisual → Prewarming → Active`,
with 1.25x hysteresis on every threshold and exactly one `Active` system.

One `Active` is not a performance budget: `Active` owns the streaming planet,
the environment queries and the simulation frame, and two of those at once would
be two answers to "which way is down".

19 systems tracked and 18 generated over the journey, with 31 transitions. The
transition count is the flapping detector - a number that climbs while the ship
is stationary means a hysteresis band is too narrow.

## Test results

| Suite | Result |
|---|---|
| Standalone (`Tools\StandaloneTests\RunTests.bat`) | **79/79 tests, 1,241,356 assertions** |
| Unreal automation (`Automation RunTests Universe`) | **79/79 tests, 0 failures** |
| `universe.InterstellarJourney` | **PASS** |
| `universe.Journey 1` (Sprint 003 planetary) | **PASS**, 2.26 m walked, 2 frame transitions, 0 descent regressions |
| Persistence | schema v1 → v2 migrated in place; structure written and read back, 1.80 ms write |

Twelve test bodies were added this sprint: six for the galaxy layer
(determinism, multi-galaxy identity, local round trip, density invariants, the
density bridge to star generation, sector isolation) and six for travel
(integration over many sectors, sector traversal, body intersection, warp
overshoot, cache bounds, estimates and modes).

## Defects found, and what gave them away

Every one of these was found by *running* something, not by reading code.
That is the same pattern as every previous sprint and it is worth saying again.

### 1. Stellar density was zero everywhere, including inside galaxies

**Symptom:** the universe was completely empty. Every generation test that
searched from the origin failed, and `PlanetSurfaceDescriptor` crashed indexing
`Planets[0]` of a system it never found.

**Cause:** `ToGalaxyLocal` used `FUniversePosition::TryGetRelativeCm`, which is
exact only to about 0.01 light years - that is the entire reason the coordinate
type splits into an integer cell and a local offset. Every galaxy-scale query
therefore took the documented "astronomically far" fallback and returned a
radius past the disk, which makes every density term evaluate to zero.

Nothing in the code looked wrong. The number that came out was simply the
failure value, and the failure value was a plausible number.

**Fix:** work in cell space, which keeps full relative precision at any
separation. `FromGalaxyLocal` had the mirror problem - feeding 10^22 cm to
`OffsetByCm`, where a double's ulp is tens of kilometres - and now splits into
whole cells plus an exact remainder.

### 2. The braking law is correct and is not enough

**Symptom:** the ship reported `bArrived` while doing 8 x 10^12 m/s, and sailed
straight past.

**Cause:** `v²/2a` is correct in the limit and the limit is not where the ship
lives. On the last frames of a warp approach the ship is doing 10^12 m/s with an
arrival radius of 10^11 metres, so it enters the radius and leaves it again
inside one frame. By the letter of the test it arrived.

**Fix:** an auto-braked step is clamped to its point of closest approach to the
target. That removes overshoot as a possibility at any frame rate - including a
multi-second hitch - which no tuning of the braking constants can.

### 3. The autopilot steered but never opened the throttle

**Symptom:** 410 simulated seconds of `12.714 ly to go, 0.0 m/s`. The ship
pointed at its destination and sat there.

**Cause:** the throttle came from the thrust key, which nothing was holding.

**What made it take two runs to find:** the journey runner printed nothing for a
two-minute leg, which is indistinguishable from having stopped. It now reports
progress every ten seconds. A silent long-running test is a test you cannot
debug.

### 4. `universe.Autopilot` was a fatal startup error

Not a warning. `universe.AutoPilot` already existed as the Sprint 001 thrust
CVar, and a console *command* cannot replace a *variable* - Unreal calls
`appError` and the module fails to load. Renamed to `universe.FlyTo`, which
reads better anyway.

### 5. A streamed world makes every "it will be ready by now" assumption a bug

Two separate failures, same shape, both introduced the moment the game mode
stopped building the scene during `StartPlay`:

- **`-ExecCmds` runs on frame zero.** Every scripted run that began with
  `universe.Land` failed on an empty world. Fixed with
  `universe.After <seconds> <command>` - the piece that was actually missing -
  rather than a state machine in each of a dozen debug commands.
- **The Sprint 003 walk test paced its waypoints on a two-second clock.** A
  teleport lands somewhere with no cooked collision; on the planet the galactic
  search now starts beside - two and a half times larger than the old one -
  building a patch there takes longer than two seconds. The character was held
  for the entire stage and reported 0.00 m walked at all six waypoints.

  This one is worth dwelling on, because the obvious reading was "Sprint 006
  broke walking" and it was wrong. Walking was fine. The *test* had a hardcoded
  assumption about how fast the world becomes real, and that assumption had been
  true by luck. Waypoints now advance on progress rather than on a clock.

### 6. A schema bump nearly threw away every existing world

Adding the `world_facts` table bumped the persistence schema to v2, and the
existing v1 check refused to open older worlds. Correct behaviour for a
*breaking* change and wrong for this one: every change so far has been additive,
and `CREATE TABLE IF NOT EXISTS` had already done the work by the time the
version was checked.

Refusing would have told a player who built a base in Sprint 005 that their
world was incompatible - losing exactly what the persistence work exists to
protect, in exchange for nothing. v1 → v2 now migrates in place. A future
non-additive change is where a real per-version migration goes, and refusing is
the right answer for any step that has none.

## Acceptance criteria

| # | Criterion | Evidence |
|---|---|---|
| Galaxy descriptors | Three types, deterministic, versioned | `UniverseTest_GalaxyGenerationDeterminism`, `MultiGalaxyIdentity` |
| Galactic coordinates | Universe ↔ galaxy-local round trip | `UniverseTest_GalaxyLocalRoundTrip` |
| Stellar density | Bounded, zero outside, disk structure | `UniverseTest_GalaxyDensityInvariants` |
| Sector determinism | Same address, same system, any order | `SystemGenerationDeterminism` |
| Sector isolation | No duplicate identities across sectors | `UniverseTest_SectorIsolation` |
| Multiple systems | Two visited, physically separate | Journey: Zarelra-1252 → Aelonis-3673, 12.71 ly |
| System streaming | Descriptor → visual → active → released | 19 tracked, 18 generated, 31 transitions |
| System discovery | Detected / visited, persisted | 13 detected, 2 visited; `world_facts` |
| Travel modes | Five regimes over one state | `UniverseTest_TravelEstimatesAndModes` |
| Warp | Continuous, configurable, steerable | 3.34e6 c peak; `universe.Warp`, `J` |
| Trajectory safety | No tunnelling through stars or planets | 3 hazard stops; `UniverseTest_TravelBodyIntersection` |
| Overshoot | Robust arrival at any step size | `UniverseTest_TravelWarpOvershoot` |
| Interstellar void | Navigable, no active system needed | 0 tracked systems 57,406 ly out |
| Galaxy exit | Stars stop existing outside the disk | `universe.WarpJump 60000` |
| Second galaxy | Another exists and is distinct | Astrapha-7716, 798,970 ly away |
| Cache bounds | Bounded across many sectors | `UniverseTest_TravelCacheBounds` |
| Deterministic regeneration | Home unchanged after a round trip | Content hash identical: `0x7AABB69FDC5E9456` |
| Persistence integration | Deltas survive travel and restart | Schema v2 migration; structure round trip |
| Previous sprints | 001-005 still pass | 79/79 both harnesses; `universe.Journey 1` PASS |

## Storage scaling

```text
universe.PersistenceInfo
  Universe "sprint-001" (0xBDEFD300CB477825)  terrain v1  environment v1  schema v2
  Storage: 1 records, 28.00 KB.  Cache: 6 regions, 1 created, 0 removed
  Latency: last read 1.23 ms, last write 1.80 ms
  Here: region P15E36090F5C4A593/-X/L13/5529/5324 (2361 m across)
```

Storage is proportional to what the player has *changed*, not to where they have
been. Visiting a second system costs one row of discovery state - about forty
bytes - and nothing else. The 25 ly round trip added no entity records at all.

## Documentation

New:
- `Docs/Architecture/GalaxyGeneration.md`
- `Docs/Architecture/GalacticCoordinates.md`
- `Docs/Architecture/InterstellarTravel.md`
- `Docs/Architecture/StarSystemStreaming.md`
- `Docs/Architecture/UniverseScaleRendering.md`
- `Docs/ADR/ADR-007-galaxy-density-and-system-streaming.md`

## Known limitations and open risks

- **Cross-platform libm (carried from ADR-002).** The spiral arm term uses
  `Atan2` and `Sin`, so galaxy density shares star generation's exposure to
  platform libm differences. Deliberate: the formulations that avoid
  trigonometry do not produce spiral arms, and the fix - a project-owned libm -
  solves both at once. Terrain and climate remain transcendental-free, which is
  where it matters most.
- **Galaxy density is a gameplay number.** 0.15 galaxies per intergalactic cell
  is roughly twenty-five times reality. Written down rather than buried.
- **Orbits do not advance with time.** Planet positions are a deterministic
  function of epoch phase, not an ephemeris. Section 58 asks only that motion be
  representable; it is, and nothing yet moves.
- **No galaxy geometry is rendered.** A galaxy is a density function, and
  drawing one as a mesh would mean inventing a representation no simulation
  reads. The star field and the numeric readouts are the honest presentation.
- **Two movement controllers over one state.** Warp and sublight both write the
  same position and velocity. This is guarded by there being exactly one of
  each, but a third controller would be the point to stop and unify.
- **The broad phase is bounded at 512 sectors per step.** A truncated result is
  a correct prefix rather than an approximation, so a caller can substep from
  it; nothing currently does, because a frame never crosses that many.

## Commands added

| Command | Purpose |
|---|---|
| `universe.Systems [n]` | Nearby systems, distance, streaming state, discovery |
| `universe.Target <n\|name\|ahead\|clear>` | Select a navigation target |
| `universe.FlyTo <target>` | Steer and brake onto it under warp |
| `universe.Warp [on\|off]` | Engage the warp drive (also `J`) |
| `universe.GalaxyInfo` | Galactic position and local stellar density |
| `universe.TravelInfo` | Mode, speed, target, ETA, hazard stops |
| `universe.InterstellarJourney [oneway\|abort]` | The self-verifying acceptance run |
| `universe.After <seconds> <command>` | Defer a command past frame zero |
