Read `CLAUDE.md` completely before doing anything else.

Then inspect the entire repository and verify the output of Sprint 001 and Sprint 002.

We are beginning:

# Sprint 003 — Seamless Space → Atmosphere → Surface Traversal

The objective of this sprint is to turn the procedural spherical planet created in Sprint 002 into a truly traversable world.

At the end of this sprint, the player should be able to:

```text
deep space
↓
approach planet
↓
enter planetary proximity
↓
enter atmosphere
↓
descend
↓
reach terrain
↓
land
↓
exit spacecraft
↓
walk on the spherical surface
↓
return to spacecraft
↓
take off
↓
leave atmosphere
↓
return to space
```

This entire experience must occur without a visible loading screen or fake teleport.

This is primarily a systems/physics/coordinate-frame sprint.

Do NOT yet turn the planet into a rich ecosystem.

Trees, advanced vegetation, weather, wildlife, oceans and building systems belong to later sprints.

---

# 0. PRECONDITION — VERIFY PREVIOUS SPRINTS

Before implementing anything:

Verify Sprint 001:

* universe coordinate system works
* deterministic universe generation works
* large logical movement works
* tests pass

Verify Sprint 002:

* spherical procedural planet works
* cube-sphere topology works
* terrain patch IDs work
* quadtree works
* terrain LOD works
* streaming works
* patch boundaries are correct
* cube-face boundaries are correct
* deterministic terrain works
* nearby terrain collision works
* stress test does not exhibit unbounded growth
* tests pass

If defects directly block Sprint 003, fix them first.

Do not rewrite working systems unnecessarily.

---

# 1. SPRINT OBJECTIVE

Implement the systems necessary for continuous planetary traversal.

The player must be able to move between these regimes:

```text
Universe-space
↓
Star-system-space
↓
Planet-relative-space
↓
Planetary atmosphere
↓
Near-surface flight
↓
Grounded spacecraft
↓
Character movement on spherical terrain
```

The internal coordinate/simulation frame may change.

The PLAYER EXPERIENCE must remain continuous.

No visible coordinate jump.

No loading screen.

No explicit level transition.

---

# 2. OUT OF SCOPE

Do NOT add:

* realistic atmosphere rendering beyond minimal traversal cues
* production cloud systems
* advanced weather
* procedural vegetation
* wildlife
* oceans beyond placeholder visual/reference if required
* building
* persistence
* mining
* resource systems
* combat
* inventory
* crafting
* civilization
* multiplayer MMO infrastructure
* polished flight simulation
* detailed ship interiors unless necessary for entry/exit testing

Do not expand scope.

---

# 3. PRIMARY ACCEPTANCE EXPERIENCE

The sprint is complete only when this journey works:

```text
START IN SPACE

planet visible in distance

↓

fly toward planet

↓

planet grows continuously

↓

planet terrain transitions through LOD

↓

enter planetary local simulation region

↓

continue flying with no visible transition

↓

cross atmosphere boundary

↓

continue descending

↓

terrain collision becomes active

↓

land on surface

↓

ship remains stable against planetary gravity

↓

exit ship

↓

player stands upright relative to planet surface

↓

walk several kilometers

↓

terrain remains streamed around player

↓

walk across curved spherical terrain

↓

return to ship

↓

enter ship

↓

take off

↓

climb through atmosphere

↓

exit planetary local region

↓

continue into space
```

No fake level change.

---

# 4. COORDINATE FRAME ARCHITECTURE

Sprint 001 established astronomical coordinates.

Sprint 002 established planet-local coordinates.

Sprint 003 must connect them cleanly.

We need explicit coordinate frames.

Conceptually:

```text
Universe Frame
│
└── Star System Frame
    │
    └── Planet Frame
        │
        └── Local Simulation Frame
```

Do NOT allow these concepts to become implicit.

Implement an explicit frame/conversion architecture.

Potential conceptual objects:

```cpp
FUniversePosition
FSystemPosition
FPlanetLocalPosition
FSimulationFrame
```

Do not copy this blindly.

Design the smallest clean model.

Requirements:

* deterministic conversion
* no accumulated drift
* stable origin rebasing
* reversible conversion where practical
* debug visibility
* serialization-safe canonical position
* future multiplayer compatible
* player canonical position independent from transient Unreal world origin

The canonical player location must NOT simply be:

```cpp
Actor->GetActorLocation()
```

The Unreal transform is only the current local representation.

---

# 5. LOCAL SIMULATION FRAME

Create a local simulation frame centered appropriately around the active player/ship.

Its purpose is to keep Unreal-space numbers numerically healthy.

Conceptually:

```text
Canonical universe position
        ↓
Choose active reference frame
        ↓
Map nearby objects into Unreal coordinates
```

During planetary traversal, the local frame may be:

```text
planet-centered
```

or:

```text
player-relative / rebased planet frame
```

depending on the architecture selected.

Analyze both.

Important considerations:

* full planet radius may be millions of meters
* local surface precision must remain excellent
* physics must remain stable
* terrain patch coordinates must remain deterministic
* transition from space to surface must be invisible

Document the selected strategy.

Create/update:

```text
Docs/Architecture/SimulationFrames.md
```

and an ADR if appropriate.

---

# 6. FRAME TRANSITION RULES

Define clear transition criteria between astronomical/system simulation and planet-relative simulation.

Example concept:

```text
far from planet
→ system frame

near planet
→ prewarm planet simulation

closer
→ planet becomes authoritative local reference

leave planet influence region
→ transition back to system frame
```

Do NOT perform transition exactly at the visible surface.

Use an overlap/preparation zone.

Transitions should support:

* preloading
* terrain streaming warmup
* collision warmup
* gravity activation
* reference-frame handoff

Use hysteresis.

Example:

```text
enter planet frame at D1
leave planet frame at D2

D2 > D1
```

to prevent oscillation.

---

# 7. PLANET APPROACH

The player must be able to approach the procedural planet from significant distance.

Ensure:

* planet representation works at astronomical distance
* terrain representation progressively becomes relevant
* local terrain system activates before it is visually required
* no obvious scale snap occurs
* no sudden repositioning occurs

If Sprint 002 used a debug presentation scale separate from logical scale, Sprint 003 must begin reconciling this.

The logical planet radius should drive traversal.

Avoid permanently fake/compressed spatial relationships.

Temporary debugging speed multipliers are allowed.

---

# 8. TRAVEL SPEED REGIMES

The player needs a usable flight model across very different scales.

Do NOT try to use identical low-speed rigid-body physics for all travel.

Implement a simple multi-regime velocity model.

Possible regimes:

```text
LOCAL FLIGHT
atmosphere / near-surface

↓

SPACE FLIGHT
planetary / orbital distance

↓

HIGH SPEED
interplanetary / debug traversal
```

Warp/FTL architecture should remain compatible, but true interstellar warp is not the focus of this sprint.

The objective is to enable practical testing of:

```text
space → planet → surface
```

without waiting hours.

Provide configurable debug speed multipliers.

---

# 9. VELOCITY REPRESENTATION

Separate:

```text
logical velocity
```

from:

```text
Unreal physics velocity
```

where necessary.

At high speed, canonical movement may be integrated in the higher-level position system rather than relying entirely on Chaos rigid-body velocity.

Near the terrain, conventional physics can take over.

Document the authority model.

Avoid having two systems simultaneously integrate the same movement.

---

# 10. HIGH-SPEED COLLISION SAFETY

At high travel speeds, frame-by-frame point collision is insufficient.

For astronomical bodies:

```text
previous position
→
new position
```

must be tested using swept/continuous trajectory logic.

At minimum prevent a high-speed spacecraft from accidentally passing directly through the planet.

A simple initial mathematical test may use:

```text
line segment / ray
vs
planet sphere
```

with appropriate terrain safety margins.

When potential intersection is detected:

* reduce travel step
* enter appropriate local simulation
* transition to detailed collision handling

Do not attempt full relativistic physics.

---

# 11. PLANETARY GRAVITY FIELD

Implement gravity as a planetary field.

For a point near the planet:

```text
gravityDirection =
normalize(planetCenter - objectPosition)
```

Magnitude should be configurable.

At minimum support:

* character
* spacecraft when appropriate
* physics test objects

Gravity should be defined from planet properties.

Example:

```text
SurfaceGravity
```

or:

```text
Mass + radius
```

depending on current data model.

Do not hardcode global `-Z`.

---

# 12. GRAVITY FALLOFF

Decide how gravity behaves with altitude.

A simple physically motivated form is acceptable:

```text
g(r) ∝ 1 / r²
```

but gameplay simplification is allowed.

Requirements:

* stable near surface
* smooth with altitude
* no discontinuity at atmosphere boundary
* no sudden on/off switching
* configurable per planet

Document the choice.

---

# 13. CHARACTER ORIENTATION

The player character must orient relative to the planet.

Define:

```text
Up =
normalize(PlayerPosition - PlanetCenter)
```

Character forward/right vectors must remain tangent to the surface.

Walking should work across arbitrary surface orientation.

The player must be able to walk:

```text
"around the side" of the planet
```

without the world breaking because Unreal assumes global Z-up.

Use current Unreal custom gravity capabilities where suitable.

Do not create a hack that works only near one test location.

---

# 14. CHARACTER MOVEMENT

Implement a minimal walking character.

Requirements:

* walk
* run if easy
* jump
* grounded detection
* slope handling
* collision
* camera
* planetary gravity
* correct orientation

Do NOT build:

* advanced animation system
* combat
* inventory
* climbing
* swimming
* parkour

Use placeholders.

Correct movement is more important than visual quality.

---

# 15. CAMERA

The camera must behave correctly under changing gravity orientation.

Requirements:

* no sudden roll flips
* stable horizon relative to local terrain
* comfortable mouse look
* ability to look up toward space
* acceptable transition when entering/exiting vehicle

Avoid Euler-angle assumptions that break around arbitrary planetary orientation.

Prefer robust quaternion/vector-based orientation.

---

# 16. SPACECRAFT LOCAL FLIGHT

Implement or extend the spacecraft controller so near a planet it can:

* descend
* ascend
* rotate
* move forward/backward
* hover or stabilize if appropriate
* approach terrain safely
* land

Do NOT spend excessive time building a polished flight simulator.

A simple controllable craft is sufficient.

The architecture should later allow different ship flight models.

---

# 17. SPACECRAFT ORIENTATION RELATIVE TO PLANET

Near the planet, optional stabilization may orient the ship relative to local gravity.

Provide configurable behavior such as:

```text
gravity assist ON
```

so the ship naturally understands:

```text
up = away from planet center
```

This makes landing easier.

Do not permanently force all spacecraft to align with gravity.

Future spacecraft may intentionally fly freely.

---

# 18. LANDING

Implement a minimal reliable landing state.

Requirements:

* detect near-ground contact
* avoid persistent physics jitter
* ship remains stable after landing
* ship does not slowly slide through terrain
* ship remains correctly oriented relative to local surface/gravity

A simple landing gear/collider model is acceptable.

No production landing animation needed.

---

# 19. SHIP ENTRY / EXIT

Implement minimal state transition:

```text
Pilot spacecraft
↓
Land
↓
Exit
↓
Control character
```

and:

```text
Character approaches ship
↓
Enter
↓
Control spacecraft
```

This may initially use:

* interaction key
* simple proximity trigger
* placeholder entry point

Do not build elaborate interiors.

Acceptance is about changing gameplay modes cleanly.

---

# 20. SHIP POSITION PERSISTENCE DURING EXIT

When the player exits the ship:

* ship remains where it was landed
* terrain streaming continues correctly
* ship stays within the active local frame
* character and ship share consistent planetary coordinates

Do not make the ship disappear or teleport.

---

# 21. TERRAIN COLLISION INTEGRATION

Integrate Sprint 002 terrain collision with the new character/ship systems.

Requirements:

* collision active ahead of player near surface
* terrain must not visually exist without collision beneath player
* no falling through during LOD changes
* collision patch lifecycle must account for fast movement
* collision remains available around landed ship

Prioritize collision patches separately from purely visual patches where necessary.

---

# 22. COLLISION SAFETY ZONE

Create a near-player safety policy.

For example:

```text
Immediate region:
highest collision priority

Nearby region:
collision prewarmed

Distant region:
visual only
```

The player must not outrun terrain collision generation during normal surface gameplay.

At high flight speed, transition logic may limit or predict movement until appropriate collision data is ready.

---

# 23. TERRAIN LOD + CHARACTER COHERENCE

LOD changes must not significantly alter the physical ground beneath the character.

Important:

A low-detail parent patch and high-detail child patches may differ geometrically.

If the player is standing there, replacing the terrain cannot:

* drop them through the world
* launch them upward
* change ground height dramatically

Define how collision uses terrain detail.

Potential strategies:

* collision always uses sufficiently high deterministic terrain samples
* visual geomorphing
* stable ground sampling independent of render LOD

Choose and document the strategy.

---

# 24. PLANET SURFACE QUERY API

Introduce a clean API for querying planetary surface information.

Conceptually:

```cpp
GetSurfaceHeight(direction)
GetSurfacePosition(direction)
GetSurfaceNormal(direction)
GetAltitude(position)
GetLocalUp(position)
```

Do not require gameplay systems to know quadtree internals.

The planet subsystem should provide semantic queries.

This API will later support:

* vegetation
* wildlife
* construction
* weather
* resources
* AI navigation

Keep it deterministic.

---

# 25. ALTITUDE

Define altitude consistently.

Example:

```text
altitude =
distanceFromPlanetCenter
-
planet reference radius / terrain surface
```

Be explicit whether an API means:

* altitude above reference sphere
* altitude above actual terrain
* distance from center

Do not overload one value with multiple meanings.

Create clear naming.

---

# 26. ATMOSPHERE BOUNDARY

Introduce a logical atmosphere.

The planet descriptor should contain something like:

```text
AtmosphereHeight
```

or equivalent.

This sprint does NOT require production atmosphere simulation.

However, the game should understand:

```text
outside atmosphere
inside atmosphere
near surface
```

Expose this state for future rendering/weather/flight behavior.

---

# 27. MINIMAL ATMOSPHERIC VISUAL CUE

Provide a minimal temporary atmospheric indication if straightforward.

Examples:

* simple atmospheric shell
* color/scattering approximation
* altitude-based fog
* debug visualization

Do NOT spend large amounts of time building production scattering.

The main goal is to make the traversal understandable.

---

# 28. PLANET DAY/NIGHT ORIENTATION

If straightforward, ensure the planet has a meaningful relationship with the system's star.

At minimum:

* star direction exists
* surface receives directional lighting
* one side can be day, one night

Planet rotation can remain minimal or static if necessary.

Do not let astronomy simulation derail this sprint.

---

# 29. ORIGIN REBASING / FRAME REPOSITIONING

Test long surface traversal.

The player may travel many kilometers.

The local Unreal frame should remain numerically healthy.

Implement or validate rebasing behavior such that:

```text
canonical planet position
```

remains stable while:

```text
Unreal local origin
```

can move.

Requirements:

* no visible pop
* no terrain identity change
* ship/character remain aligned
* physics objects remain consistent
* active terrain patches remap correctly

This is essential.

---

# 30. CROSSING CUBE FACES ON FOOT

The character must be able to move toward and across a cube-sphere face boundary.

The player should notice nothing.

Verify:

* terrain continuity
* gravity continuity
* character orientation
* collision
* camera
* patch streaming

No face-specific gameplay behavior should leak through.

---

# 31. CROSSING CUBE CORNERS

Test difficult topology locations.

Move character/ship near cube-sphere corners where three logical faces meet.

Verify:

* no cracks
* no orientation discontinuity
* no collision failure
* no streaming holes

These areas should be part of manual and automated validation.

---

# 32. PLANET CIRCUMNAVIGATION SUPPORT

We do not need to manually walk around an Earth-sized planet.

But architecturally prove that movement is not limited to one region.

Provide developer commands allowing controlled repositioning around the same planet.

Examples:

```text
TeleportPlanetLatLon
TeleportPlanetDirection
TeleportToPatch
```

Canonical position must update properly.

Use these to test multiple regions.

---

# 33. SURFACE DEBUG NAVIGATION

Create developer utilities for:

```text
surface point under ship
current cube face
current patch
altitude
gravity vector
planet-local direction
surface normal
current simulation frame
```

This data should be visible via debug HUD/logging.

---

# 34. FRAME DEBUG VISUALIZATION

Provide a debug view that shows:

* universe position
* system frame
* planet center
* local simulation origin
* player canonical planet position
* Unreal local transform

We must be able to diagnose coordinate issues quickly.

---

# 35. HIGH-SPEED APPROACH PREWARM

When approaching a planet at high speed:

do not wait until the player is 50 meters above the ground to generate terrain.

Use trajectory and velocity to estimate future relevance.

Conceptually:

```text
current position
+
velocity
+
time horizon
↓
predicted approach region
↓
prewarm terrain
```

The first implementation can be conservative.

The architecture matters more than perfect prediction.

---

# 36. STREAMING PRIORITIES

Patch generation priority should account for gameplay relevance.

Example priority:

```text
1. collision directly beneath/near player
2. predicted flight path
3. visible nearby terrain
4. horizon detail
5. distant cosmetic terrain
```

Do not use one FIFO queue for every terrain request if it causes critical patches to wait behind obsolete visual work.

---

# 37. TASK CANCELLATION

Rapid:

```text
space → surface → space
```

movement should not create stale work.

When the player leaves a region:

* obsolete terrain jobs should cancel or be discarded
* results should not attach after becoming irrelevant
* queues should remain bounded

Reuse Sprint 002 infrastructure.

Improve where necessary.

---

# 38. SPACECRAFT SPEED SAFETY

During normal gameplay near terrain, do not allow a debug speed to silently outrun every safety system and corrupt state.

Provide separate:

```text
Gameplay movement
```

and:

```text
Developer traversal
```

where appropriate.

Debug tools may teleport or accelerate aggressively.

Production movement should respect current simulation readiness.

---

# 39. PHYSICS AUTHORITY

Clearly define which layer controls each mode.

For example:

```text
Deep space:
logical kinematic movement

Near planet:
logical movement + local transform

Near surface:
Chaos / local flight integration

Grounded:
Chaos / landing state

Character:
CharacterMovement + planetary gravity
```

Exact implementation may differ.

Document it.

Avoid unclear mixed authority.

---

# 40. PHYSICS FRAME TRANSITION

Switching movement regimes must preserve:

* position
* orientation
* velocity
* angular orientation
* player control

No unexplained velocity loss or explosive acceleration.

If velocity must be converted between frames, do so explicitly.

Write tests for conversion math.

---

# 41. QUATERNION-BASED ORIENTATION

Use robust orientation math.

Planet traversal exposes weaknesses in naive Euler handling.

Use:

* normalized vectors
* quaternions
* orthonormal basis construction

where appropriate.

Avoid gimbal-lock-prone architecture.

Test unusual orientations.

---

# 42. PLAYER SPAWN ON PLANET

Provide a developer spawn mode:

```text
SpawnOnPlanetSurface
```

This is useful for rapid testing without flying from space every time.

The spawn system must:

* query terrain height
* position player safely above terrain
* align orientation to local up
* wait for collision readiness if required

This is a debug shortcut only.

---

# 43. PLAYER SPAWN IN SPACE

Keep a corresponding:

```text
SpawnInOrbit
```

or:

```text
SpawnInSpace
```

developer mode.

Allow fast iteration on the complete journey.

---

# 44. FULL JOURNEY AUTOMATION

Create a reproducible automated or semi-automated traversal test.

Example:

```text
start orbit
↓
approach
↓
descend
↓
reach surface threshold
↓
ascend
↓
return orbit
```

The test should monitor:

* frame transitions
* terrain task backlog
* active patches
* numerical validity
* collision readiness
* memory
* errors/asserts

It need not autonomously perform perfect landing.

---

# 45. NUMERICAL VALIDATION

Add assertions/diagnostics for:

* NaNs
* infinite coordinates
* invalid quaternions
* zero-length up vectors
* invalid gravity vectors
* velocity explosions
* invalid altitude
* invalid transform conversions
* frame mismatch
* terrain query failure beneath grounded player

Fail loudly in development.

---

# 46. CHARACTER GROUNDING STRESS TEST

Test:

```text
walk uphill
walk downhill
jump
land
cross patch boundary
cross LOD boundary
cross cube face
rebase local origin
```

Character should remain stable.

No falling through.

No violent correction.

---

# 47. SHIP LANDING STRESS TEST

Repeatedly:

```text
approach
land
exit
enter
take off
```

on different terrain slopes.

Track:

* collision stability
* ship orientation
* character spawn safety
* terrain lifecycle
* physics jitter

---

# 48. FRAME REBASE STRESS TEST

Force many local-frame rebase operations during:

* flight
* walking
* landed ship state

Verify:

* canonical position remains correct
* relative character/ship relationship remains correct
* terrain stays visually stable
* no accumulating drift

---

# 49. TEST DIFFERENT PLANET SIZES

Use at least several test radii.

Example categories:

```text
small moon
small planet
Earth-scale planet
```

Exact radii can be configurable.

Verify:

* gravity/orientation math
* LOD integration
* local frame behavior
* approach logic
* altitude logic

No architecture should implicitly assume one planet radius.

---

# 50. AUTOMATED TESTS — FRAME CONVERSION

Add tests for:

* universe → system
* system → planet
* planet → local simulation
* reverse conversions
* repeated round trips
* large positions
* negative coordinates
* altitude
* arbitrary surface directions

Measure tolerances.

---

# 51. AUTOMATED TESTS — GRAVITY

Test:

* gravity direction at multiple surface locations
* opposite sides of planet
* poles
* cube-face edges
* cube corners
* altitude changes
* configurable gravity magnitude

The direction must always point correctly toward the planet center.

---

# 52. AUTOMATED TESTS — SURFACE QUERY

For deterministic planet seeds:

verify:

```text
SurfaceHeight(direction)
SurfacePosition(direction)
SurfaceNormal(direction)
```

remain deterministic.

Check continuity across patch/face boundaries.

---

# 53. AUTOMATED TESTS — VELOCITY CONVERSION

Where movement regime/frame conversion exists:

test that transitions preserve intended velocity.

Examples:

```text
system velocity
→ planet frame

planet frame
→ local simulation

local simulation
→ system frame
```

No large unexplained errors.

---

# 54. AUTOMATED TESTS — HIGH-SPEED INTERSECTION

Test trajectory vs planet.

Cases:

* direct hit
* near miss
* tangent
* moving away
* very large velocity
* starting inside planetary influence region

No tunneling through the reference planet sphere.

---

# 55. PERFORMANCE METRICS

Collect actual measurements where possible.

Track:

* frame time during approach
* frame time near surface
* terrain generation latency
* collision generation latency
* rebase cost
* active patch count
* task queue
* memory behavior
* character movement stability
* ship physics stability

Do not fabricate numbers.

---

# 56. VISUAL QUALITY REQUIREMENT

This is still an architecture sprint.

A successful screenshot may contain:

```text
simple ship
gray procedural mountains
plain sky
simple atmosphere tint
placeholder character
```

That is fine.

What matters is that the journey is real.

Do not waste time on art polish.

---

# 57. ARCHITECTURE DOCUMENTATION

Create/update at minimum:

```text
Docs/Architecture/SimulationFrames.md
Docs/Architecture/PlanetaryGravity.md
Docs/Architecture/PlanetaryTraversal.md
```

Document:

* authoritative coordinate layers
* frame transitions
* movement authority
* gravity system
* character orientation
* ship orientation
* terrain collision integration
* approach prewarming
* high-speed planet intersection
* origin rebasing
* entry/exit state

Add ADRs where appropriate.

---

# 58. MANUAL ACCEPTANCE TEST

Perform this manually before completion:

## Journey A — Space to Ground

1. Spawn in space.
2. Select/approach procedural planet.
3. Fly toward it.
4. Observe terrain LOD activate.
5. Enter local planetary region.
6. Continue with no visible jump.
7. Descend.
8. Reach near-surface altitude.
9. Land.
10. Exit ship.
11. Walk.
12. Jump.
13. Walk across terrain patch boundaries.
14. Return to ship.
15. Enter.
16. Take off.
17. Ascend.
18. Leave planet region.
19. Continue into space.

## Journey B — Different Planet Region

1. Reposition to another side of planet.
2. Approach surface.
3. Land.
4. Exit.
5. Verify gravity/orientation remain correct.

## Journey C — Topology Edge

1. Navigate near cube-face boundary.
2. Walk/fly across it.
3. Verify no visible/gameplay break.

## Journey D — Rebase

1. Travel far enough locally to trigger rebasing repeatedly.
2. Verify no coordinate instability.

---

# 59. SPRINT 003 ACCEPTANCE CRITERIA

Sprint 003 is complete only if:

## Coordinate Frames

* [ ] Universe/system/planet/local coordinate roles are explicitly defined.
* [ ] Canonical player position does not depend on Unreal world origin.
* [ ] Frame conversions work.
* [ ] Local simulation frame remains numerically safe.
* [ ] Rebase behavior is stable.

## Space → Planet

* [ ] Player can approach a planet from space.
* [ ] Planet terrain streams progressively.
* [ ] Planet frame activates without visible teleport.
* [ ] High-speed trajectory cannot trivially tunnel through the planet.
* [ ] Approach terrain is prewarmed appropriately.

## Gravity

* [ ] Planetary gravity exists.
* [ ] Gravity direction points toward planet center.
* [ ] Gravity varies correctly/configurably with altitude.
* [ ] Gravity works across the full spherical surface.

## Character

* [ ] Character can stand on spherical terrain.
* [ ] Character up direction follows local planet normal/gravity.
* [ ] Character can walk.
* [ ] Character can jump.
* [ ] Character can traverse patch boundaries.
* [ ] Character can traverse cube-face boundaries.
* [ ] Camera remains stable.

## Spacecraft

* [ ] Ship can fly near planet.
* [ ] Ship can descend.
* [ ] Ship can land.
* [ ] Ship remains stable while landed.
* [ ] Player can exit.
* [ ] Player can re-enter.
* [ ] Ship can take off.
* [ ] Ship can return to space.

## Terrain Integration

* [ ] Collision loads before it is required.
* [ ] Character does not fall through during normal traversal.
* [ ] Ship does not pass through loaded terrain during landing.
* [ ] LOD changes do not catastrophically alter ground beneath player.

## Streaming

* [ ] High-speed movement does not create unbounded task backlog.
* [ ] Obsolete jobs are cancelled/discarded.
* [ ] Terrain remains bounded in memory.
* [ ] Landing keeps necessary terrain active.

## Testing

* [ ] Frame conversion tests pass.
* [ ] Gravity tests pass.
* [ ] surface-query tests pass.
* [ ] high-speed intersection tests pass.
* [ ] velocity/frame tests pass where applicable.
* [ ] full project builds.
* [ ] previous Sprint 001/002 tests still pass.

## Manual

* [ ] Complete space → surface → space journey works.
* [ ] No visible loading screen is required.
* [ ] No major coordinate jump is visible.
* [ ] Another region of the same planet works.
* [ ] Cube-face traversal works.
* [ ] Repeated origin rebasing works.
* [ ] No major crash/leak/task explosion occurs.

---

# 60. DO NOT BEGIN SPRINT 004

Do NOT proceed into environmental richness until Sprint 003 has been validated.

Sprint 004 will be:

# LIVING PLANET FOUNDATION

It will add:

```text
oceans
atmospheric visuals
day/night
climate fields
biomes
terrain materials
vegetation
trees
grass
rocks
weather
basic birds / wildlife
```

Do NOT implement these now except for minimal placeholders needed to validate traversal.

---

# 61. COMPLETION REPORT

At completion provide:

## Implemented

What genuinely works.

## Architecture

Explain:

* simulation frames
* canonical player positioning
* local origin strategy
* gravity
* movement regimes
* ship/character authority
* surface-query API
* collision strategy
* high-speed planet interception

## Validation

List exact:

* builds
* tests
* manual journeys
* stress tests

that were actually run.

## Performance

Report measured values only.

## Known Limitations

Be explicit.

## Technical Debt

Identify temporary solutions.

## Recommended Sprint 004

Define the smallest next sprint needed to transform this technically traversable world into a visibly living procedural planet.

---

# FINAL PRINCIPLE

The player should never care which coordinate frame is currently active.

They should experience:

```text
"I saw a planet in space,
flew toward it,
landed,
got out,
walked around,
got back in,
and flew away."
```

Internally we may change:

* scale
* coordinate frame
* movement model
* terrain fidelity
* collision fidelity
* simulation authority

But all of those transitions must be invisible.

The player experiences one universe.

Begin Sprint 003.
