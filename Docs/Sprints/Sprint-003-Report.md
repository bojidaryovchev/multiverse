# Sprint 003 — Seamless Space → Atmosphere → Surface Traversal

**Status: complete.** Deep space to standing on the ground and back, with no
loading screen and no discontinuity, verified by a scripted run that checks
itself.

![Standing on a planet at 2.5 km, sun 35 degrees up](Sprint-003/Surface-Daylight.png)

*From orbit, terrain at 400 km:*

![Terrain from 400 km](Sprint-003/Terrain-FromOrbit.png)

---

## What was built

| | |
| --- | --- |
| **Simulation frames** | Explicit `Interstellar` / `Planetary` frames with hysteresis, closing the render-space gap ADR-003 called "the central Sprint 003 problem" |
| **Planetary gravity** | A real vector field: direction from geometry, inverse square outside the body, linear to zero inside it |
| **Surface query API** | Position-based surface queries, and three separately named altitudes |
| **Swept collision** | Analytic segment-vs-body intersection, so 10¹² m/s cannot tunnel through a planet |
| **Walking** | `APlanetCharacter` on UE 5.4+ custom gravity, with a gimbal-free camera |
| **Ship** | Gravity, atmospheric drag, landing that holds, step out and board again |
| **Atmosphere** | A logical boundary that also drives Unreal's planet-shaped sky |
| **Photometry** | Physical illuminance with camera exposure calibrated to it |
| **Streaming** | Reserved collision budget, and analytic arrival-point prewarm |
| **Journey test** | The whole traversal, unattended, checked against deadlines |

Full detail in [SimulationFrames.md](../Architecture/SimulationFrames.md),
[PlanetaryGravity.md](../Architecture/PlanetaryGravity.md),
[PlanetaryTraversal.md](../Architecture/PlanetaryTraversal.md) and
[ADR-004](../ADR/ADR-004-simulation-frames-and-planetary-traversal.md).

---

## Verification

### Automated tests

```
Tools\StandaloneTests\RunTests.bat
  57/57 tests passed, 625,064/625,064 assertions passed
```

Eight new test bodies, all running identically in the standalone harness and in
Unreal automation:

| Test | What it pins |
| --- | --- |
| `SurfaceQueryAltitudes` | The three altitudes agree, differ by exactly the elevation, and stay finite at the planet centre |
| `SurfaceQueryOrientation` | Up is exactly radial; normals are unit length and never inverted |
| `PlanetGravityField` | Direction antiparallel to up from 96 directions × 6 distances; correct law each side of the surface; zero at the centre; escape and orbit speeds against textbook Earth values |
| `SimulationFrameHysteresis` | **Zero** frame changes across 200 ticks of 1 m jitter on each boundary, with identical state at every tick |
| `SimulationFrameHandover` | Two overlapping bodies; one handover, no oscillation; empty candidate list falls back correctly |
| `TrajectoryHighSpeedIntersection` | Tunnelling caught at every tier to 10¹² m/s; contact point on the bounding sphere to 1e-6 of radius at 10¹⁰ m segment length |
| `TrajectoryTerrainRefinement` | Refined contact lands on the ground within 1 m; bounding-sphere false positives rejected |
| `AtmosphereBoundary` | Depth ramp and inside/outside, including airless bodies |

### The journey, end to end

```
> universe.Journey

=== Journey begins. PlanetActor_0, r=5013.8 km, starting 40110.0 km out (8.0 radii). ===
[   0.0s] -> DeepSpace
[   1.0s] -> Approach
          Simulation frame -> Planetary (dominance 0.999, transition 1)
[  54.8s] -> AtmosphericEntry
[  90.8s] -> Landed
[  93.8s] -> Disembarked
[  95.8s] -> Walking
          Furthest walked under own power at a waypoint: 5.89 m
[ 107.8s] -> Boarded
          Ship drift while unattended: 0.000000 m
[ 109.8s] -> Departure
          Simulation frame -> Interstellar (dominance 1.501, transition 2)
          Frame released at 22583.8 km from centre (exit radius 22561.9 km).
=== Journey complete. ===
  Frame transitions  : 2 (expected 2: one in, one out)
  Origin rebases     : 926
  Descent regressions: 0
--- PASS ---
```

The release at 22,583.8 km against an exit radius of 22,561.9 km is the
hysteresis working: the frame was entered at 15,041 km and held all the way back
out past it.

### Measured performance

Landed at +2353 m elevation on a 5013.8 km planet, held for 70 s:

```
alt=2357.1m  selected=192  visible=192  collision=20  tris=1,671,168  verts=861,120
deepest=L12  culled=6  balance=43  sel=0.27-0.35ms  upload=0.00ms  avgGen=38.6ms
```

Altitude held at exactly 2357.1 m — the 2353 m ground elevation plus the 4 m
resting clearance — across every sample. Patch generation remains ~40 ms per
patch on a worker thread, unchanged from Sprint 002.

---

## Defects found, and how

**Every one of these was found by running the journey or reading a screenshot.
None was found by reading the code.**

1. **The character clobbered its own universe position.** It read the position
   back from its Unreal transform unconditionally, but while an anchor is out of
   render range `SyncTransformToOrigin` deliberately writes no transform at all.
   A freshly spawned character therefore adopted a position derived from the
   identity transform and ended up 5 × 10¹² m from the planet it was standing on.
   *Symptom: frame dominance of 333,333 — exactly the `UniverseToPlanetLocalMeters`
   failure fallback.*

2. **Ship exit possessed before placing.** Possession points the render origin at
   the character, so possessing one that had not been placed yet dragged the
   origin to universe cell zero and put the planet out of render range.

3. **The tracked anchor was only ever set in `BeginPlay`.** After boarding, the
   render origin and the frame selector kept following the character standing on
   the ground, so the ship could fly away without ever leaving the planetary
   frame. Input mapping contexts had the same problem — both pawns' bindings
   stayed live, so `W` drove the ship and the person who had just got into it.

4. **A landed ship could never re-land.** `SweepAgainstTerrain` reports "started
   inside" rather than a hit when a step begins below the landing margin, which
   is permanently true of a ship at rest. It fell through its own resting
   position and reached terminal velocity against atmospheric drag.
   *Symptom: `universe.Land` followed by a steady 58 m/s descent through the
   planet.*

5. **The scaled solar system rendered in the player's face.** Scaled space is a
   1e-7 model built around the render origin, and standing on a planet puts the
   render origin where you are standing.

6. **The atmosphere hid the planet from orbit.** Unreal's aerial-perspective LUT
   has a bounded depth range; beyond it the scattering is extrapolated, and
   several hundred kilometres of extrapolation turned terrain that was legible
   at 400 km in Sprint 002 into flat blue haze.

7. **A black disc dead centre of every screenshot**, which looked convincingly
   like a distant body drawn in the wrong place. It was the probe's own hull,
   four metres in front of its camera: under Sprint 002's clamped lighting it
   read as a chase-camera silhouette, and under physical exposure its unlit side
   is genuinely black.

Two wrong expectations in newly written tests were also corrected rather than
worked around — `SampleBelow` agrees with `SampleDirection` to normalisation
precision rather than bit-exactly, and a segment aimed through a planet's centre
always intersects regardless of speed.

---

## What was *not* done

Stated so the gaps are known rather than discovered later.

- **No other bodies are visible from a planet surface**, and **no atmospheric
  limb from orbit.** Both need a far-field render pass with its own depth range.
  This is the largest remaining piece of the render-space problem.
- **Day and night are a function of position, not time.** There is no rotation
  model, so the sub-stellar point is permanently noon.
  `universe.GotoSubstellar` exists to make this testable.
- **Atmospheric drag is a single coefficient scaled by depth**, not aerodynamics.
  It has no density, cross-section or drag coefficient behind it. It makes
  entering an atmosphere at interplanetary speed impossible, which is what was
  wanted; it does not claim to be physics.
- **A hard impact and a gentle landing have the same outcome.** The threshold
  exists and is logged so a damage model has something to key off.
- **No terrain shadows.** A directional shadow across a 5,000 km body needs
  cascades tuned for planetary scale.
- **The character has no mesh**, and the ship is a debug sphere. Neither is
  content work this sprint asked for.
- **No automated test of the Unreal-side classes** beyond the journey. The
  journey is repeatable and its numbers are checkable, but a human still reads
  them.
- **Walking is exercised at six points by the journey but only for a second at
  each.** A long unattended walk across a cube-face boundary has not been run.

---

## Controls added

| Key | Action |
| --- | --- |
| `F` | Step out of a landed ship, or board one you are standing beside |
| `W A S D` | Walk (on foot) |
| `Space` | Jump |
| `Left Shift` | Sprint |

| Command | Effect |
| --- | --- |
| `universe.Land [index]` | Land the ship below its position, or at a seed-derived point |
| `universe.ExitShip` / `universe.EnterShip` | Console equivalents of `F` |
| `universe.GotoSurface <index> [height]` | Teleport to a seed-derived surface point |
| `universe.GotoSubstellar [height] [elevationDeg]` | Teleport to a chosen solar elevation |
| `universe.FrameInfo` | Log frame, all three altitudes, gravity, atmosphere |
| `universe.Journey` | Run and check the full traversal |
