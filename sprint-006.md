Read `CLAUDE.md` completely before doing anything else.

Then inspect the repository and verify the outputs of Sprint 001 through Sprint 005.

We are beginning:

# Sprint 006 — Interstellar & Galactic Travel

The objective of this sprint is to make the procedural universe genuinely navigable beyond a single star system.

At the end of this sprint, the player should be able to:

```text
start on / near Planet A
↓
take off
↓
leave atmosphere
↓
leave planetary space
↓
travel through the local star system
↓
accelerate to interstellar / warp speed
↓
leave the star system continuously
↓
cross interstellar space
↓
approach another procedurally generated star
↓
enter that star system
↓
approach one of its planets
↓
land
↓
explore a completely different procedural world
```

The larger architecture must also establish the foundation for:

```text
galactic travel
↓
galaxy-scale representation
↓
leaving a galaxy
↓
intergalactic travel
↓
approaching another galaxy
```

We do NOT need the final production implementation of intergalactic travel in this sprint.

We DO need an architecture where nothing fundamentally prevents it.

---

# 0. PRECONDITION — VERIFY PREVIOUS SPRINTS

Before implementing Sprint 006, verify:

## Sprint 001

* hierarchical universe coordinates work
* deterministic seed hierarchy works
* star-system descriptors work
* astronomical logical movement works
* tests pass

## Sprint 002

* spherical procedural planets work
* deterministic terrain works
* LOD / streaming works
* tests pass

## Sprint 003

* seamless space → surface traversal works
* planetary coordinate-frame transitions work
* high-speed planet interception works
* tests pass

## Sprint 004

* living procedural environments work
* multiple planet/environment descriptors work
* environment streaming remains bounded
* tests pass

## Sprint 005

* persistence layer works
* player-created structures survive restart
* procedural removals survive restart
* world-state architecture works
* planet-local persistent identity works
* tests pass

Fix only blocking defects.

Do not rewrite working systems unnecessarily.

---

# 1. PRIMARY SPRINT OBJECTIVE

Implement the first complete interstellar traversal pipeline.

Conceptually:

```text
Universe
│
└── Galaxy
    │
    └── Galactic Sector
        │
        ├── Star System A
        │
        ├── Star System B
        │
        └── Star System C
```

The player should exist in one continuous canonical universe coordinate system.

Individual systems and planetary representations should be generated and streamed according to player relevance.

---

# 2. OUT OF SCOPE

Do NOT implement:

* MMO distributed backend
* multiplayer server handoff
* combat
* factions
* civilizations
* trading economy
* ship customization system
* giant star catalog UI
* procedural cities
* planetary industries
* hyperspace lanes
* wormhole gameplay
* complex relativistic physics
* scientifically exact orbital mechanics
* billions of active star objects
* rendering every star in a galaxy as an Unreal Actor
* production navigation UX

This sprint proves scale, travel and streaming.

---

# 3. FUNDAMENTAL SCALE PRINCIPLE

Never instantiate astronomical scale literally.

A galaxy with:

```text
100,000,000,000 stars
```

must NOT require:

```text
100,000,000,000 Actor instances
```

Instead:

```text
far galaxy
→ aggregate visual representation

closer galaxy
→ density field / point representation

local galactic region
→ generated sectors

nearby sector
→ generated stars

active star system
→ actual gameplay objects
```

Only information relevant to the player becomes high fidelity.

---

# 4. UNIVERSE HIERARCHY

Formalize the hierarchy.

Potential conceptual structure:

```text
Universe
↓
Galaxy
↓
Galactic Region / Sector
↓
Star System
↓
Astronomical Body
↓
Planet
↓
Planet Surface
```

Do not blindly add redundant layers.

Use the existing Sprint 001 architecture where possible.

Document all stable identifiers and ownership boundaries.

---

# 5. GALAXY DESCRIPTOR

Introduce a deterministic galaxy descriptor.

Potential properties:

```text
GalaxyId
GalaxySeed
GalaxyType
CenterUniversePosition
Radius
Orientation
StarCountEstimate
DensityParameters
SpiralParameters
CoreParameters
DiskThickness
Metallicity / generation bias later
```

Do not implement unnecessary astrophysical complexity.

The descriptor should define the large-scale distribution function from which local star sectors are reconstructed.

---

# 6. GALAXY TYPES

Support a minimal extensible set, for example:

```text
Spiral
Elliptical
Irregular
```

It is acceptable for Sprint 006 to primarily validate one type, such as spiral.

Architecture should not hardcode one galaxy shape permanently.

---

# 7. GALACTIC COORDINATE SYSTEM

Integrate galaxy-local coordinates with the canonical universe position model.

Conceptually:

```text
Universe Position
↕
Galaxy-local Position
↕
Galactic Sector
↕
System-local Position
```

Requirements:

* deterministic
* reversible where practical
* serializable
* stable
* supports extreme distances
* does not rely on Unreal global coordinates
* future network/server-region compatible

Document:

```text
Docs/Architecture/GalacticCoordinates.md
```

---

# 8. GALACTIC SECTORS

Divide galaxies into deterministic spatial sectors.

A sector should be identified by stable integer coordinates.

For example:

```text
GalaxyId
SectorX
SectorY
SectorZ
```

Each sector determines local stellar content from:

```text
GalaxySeed
+
Sector Coordinates
```

Untouched sectors should require no persistent storage.

---

# 9. STELLAR DENSITY FUNCTION

Create a galaxy-level star-density function.

For a spiral galaxy, stellar probability/density may depend upon:

```text
distance from galactic center
disk radius
disk thickness
spiral arm function
central bulge
random seeded variation
```

Do not aim for astrophysical publication accuracy.

Aim for:

* visually plausible structure
* deterministic generation
* varying stellar density
* extensibility
* bounded generation cost

---

# 10. STAR SYSTEM GENERATION FROM SECTOR

Sector generation should yield a bounded set of star-system descriptors.

Conceptually:

```text
GalaxySeed
+
SectorId
↓
Candidate star locations
↓
deterministic filtering
↓
StarSystemDescriptors
```

Important:

The same sector must regenerate exactly the same stars.

System IDs must remain stable.

---

# 11. STAR SYSTEM POSITION

Each generated system must have a canonical position in:

```text
galaxy-local coordinates
```

and therefore a canonical universe position.

Do not derive gameplay identity from visual placement.

The position must remain stable across runs.

---

# 12. STAR SYSTEM STREAMING

Create a system-level streamer.

Conceptually:

```text
player position
↓
nearby galactic sectors
↓
generated system descriptors
↓
visual representations
↓
closest / active system
↓
full system simulation
```

Different states may include:

```text
Unknown
DescriptorOnly
DistantVisual
NearbyVisual
Prewarming
Active
Unloading
```

Do not keep every previously visited system instantiated forever.

---

# 13. SYSTEM ACTIVATION

When the player approaches a star:

```text
distant star representation
↓
system prewarm
↓
star + planets generated
↓
active system
```

The transition should not visibly replace the star with a completely different location/object.

Canonical position remains authoritative.

---

# 14. SYSTEM DEACTIVATION

When leaving:

```text
full planetary/system representation
↓
reduced system representation
↓
distant star representation
↓
descriptor only
```

Release expensive:

* planet terrain
* environment
* collision
* wildlife
* detailed meshes

Persistent deltas remain in storage.

---

# 15. MULTIPLE PROCEDURAL SYSTEMS

Generate multiple systems.

At minimum validate:

```text
System A
System B
System C
```

Each should have:

* unique deterministic system seed
* star
* planets
* stable canonical location
* different planetary descriptors

Visit at least two physically separate systems.

---

# 16. SYSTEM DISCOVERY

Introduce a lightweight discovery state.

A system may be:

```text
Undiscovered
Detected
Visited
```

Store discovery as player/world metadata if appropriate.

Do NOT build a full exploration progression system.

This is mainly useful for navigation and testing persistence across systems.

---

# 17. NAVIGATION TARGETING

Implement minimal target selection.

Player should be able to select:

```text
nearby star
```

and view:

```text
distance
relative direction
estimated travel time
```

No polished starmap required.

Debug UI is acceptable.

---

# 18. DISTANCE REPRESENTATION

Support useful units:

```text
meters
kilometers
AU
light-seconds
light-minutes
light-years
```

Do not lose precision merely converting to display values.

Canonical coordinates remain high precision.

---

# 19. LOGICAL VELOCITY

Extend the Sprint 003 movement system.

The spacecraft must support velocities far beyond local physics regimes.

Separate:

```text
logical travel velocity
```

from:

```text
local Chaos velocity
```

when required.

Travel should be integrated against canonical universe coordinates.

---

# 20. TRAVEL MODES

Create a clean multi-regime travel architecture.

Potential modes:

```text
Surface / Atmospheric
Local Space
Interplanetary
Interstellar
Warp / FTL
```

Do not create unrelated movement implementations with duplicated state.

There should be one canonical ship position and velocity state.

---

# 21. CONTINUOUS ACCELERATION EXPERIENCE

We want the player to experience meaningful speed changes.

Support:

```text
low speed
↓
high speed
↓
very high speed
↓
warp / FTL
```

Actual values are configurable.

The player should not need to select a loading-screen destination.

They are physically moving through the canonical universe.

---

# 22. WARP / FTL MODEL

Implement the first functional warp model.

The physics do NOT need to obey relativity.

The goal is practical universe traversal.

Requirements:

* configurable acceleration into warp
* configurable maximum warp speed
* configurable deceleration
* canonical position changes continuously
* no teleport-only implementation
* player may steer where gameplay permits
* warp can cross interstellar distances
* high-speed safety works

The architecture must support future civilization technology increasing maximum speed.

---

# 23. SPEED SCALE

Do not hardcode final gameplay balance.

Provide data/config values such as:

```text
NormalMaxSpeed
HighSpeedMax
WarpAcceleration
WarpMaxSpeed
WarpDeceleration
```

Use debug controls to test extreme values.

---

# 24. HIGH-SPEED TRAJECTORY INTEGRATION

At warp speed, one frame may cover enormous distances.

Do NOT assume:

```text
newPosition =
oldPosition + velocity * dt
```

plus ordinary local collision is enough.

The movement layer must identify relevant trajectory intersections.

At minimum detect:

```text
stars
planets
major moons
active large bodies
```

along the swept path where necessary.

---

# 25. BROAD-PHASE ASTRONOMICAL INTERSECTION

Do not test against every object in the galaxy.

Use spatial hierarchy.

Conceptually:

```text
travel segment
↓
galactic sectors crossed
↓
systems potentially intersected
↓
active candidate bodies
↓
precise trajectory check
```

Efficiency should scale with path relevance.

---

# 26. SYSTEM INTERCEPTION

If the player is traveling toward a target system:

Prewarm it before arrival.

Conceptually:

```text
target star 5 LY away

↓

time-to-arrival reaches threshold

↓

system descriptor already known

↓

activate lightweight representation

↓

generate planetary system

↓

transition into active system
```

Avoid arriving in empty black space and waiting for generation.

---

# 27. HIGH-SPEED OVERSHOOT

At warp speed, a ship may move past its destination in one simulation step.

Implement robust arrival handling.

Possible strategy:

```text
distance-to-target
+
relative speed
+
deceleration capability
```

determines when to begin slowdown.

Provide optional:

```text
AutoBrakeToTarget
```

for prototype testing.

Manual travel should remain possible.

---

# 28. WARP EXIT

Warp exit should preserve:

* canonical position
* orientation
* intended residual velocity
* active system context

No coordinate reset visible to player.

---

# 29. SYSTEM BOUNDARY IS NOT A WALL

Do NOT implement:

```text
You are leaving the system.
Press E to travel.
```

There should be no physical system boundary.

A star system is simply a region of relevance around astronomical bodies.

Fly outward indefinitely.

The nearby system naturally becomes less important.

---

# 30. INTERSTELLAR VOID

The player should be able to exist between stars.

No active system should be required.

When in deep interstellar space:

```text
canonical universe position still valid
nearest systems known
star field visible
warp continues
```

This is a real navigable state.

---

# 31. STAR FIELD

Implement a scalable star-field representation.

Requirements:

* distant stars visible
* generated consistently from nearby/far galactic structure
* not millions of Actors
* stable under origin rebasing
* maintains correct broad direction as player moves
* supports varying density inside/outside galaxy

Potential techniques:

* GPU point rendering
* procedural sky/star buffer
* hierarchical star catalogs
* impostors

Choose based on current Unreal capabilities.

---

# 32. STAR VISUAL LOD

A star may transition through:

```text
single pixel / point
↓
bright billboard
↓
local luminous object
↓
full system star
```

Canonical position must remain consistent.

Avoid visible snapping.

---

# 33. GALAXY VISUALIZATION

From sufficiently far away, represent the galaxy as an aggregate object.

Potential representation:

* volumetric texture
* procedural point cloud
* density field
* billboard/volume hybrid
* generated GPU particles

The exact visual method may be temporary.

Architecture matters.

---

# 34. LEAVING THE GALACTIC DISK

Create a debug capability to travel out of the galaxy.

As the player leaves:

```text
stellar density decreases
↓
local stars become sparse
↓
galaxy becomes visible as a large structure behind player
```

This is a key architecture validation.

Do not require photorealistic rendering.

---

# 35. GALAXY ORIGIN INDEPENDENCE

A galaxy must have its own canonical universe position.

Do not assume:

```text
Galaxy center = Unreal origin
```

A future universe may contain many galaxies.

---

# 36. MULTIPLE GALAXY DESCRIPTORS

Implement at least a minimal universe-level galaxy generator.

For example:

```text
UniverseSeed
+
IntergalacticCell
↓
zero or more galaxy descriptors
```

No need to populate each galaxy fully until approached.

---

# 37. INTERGALACTIC CELLS

Introduce a coarse spatial partition above galaxies if not already present.

Conceptually:

```text
Universe
↓
Intergalactic Cell
↓
Galaxy Descriptor
```

Use integer coordinates.

Untouched cells should cost nothing.

---

# 38. GALAXY GENERATION

Given:

```text
UniverseSeed
IntergalacticCell
```

deterministically generate:

* galaxy count
* galaxy positions
* types
* seeds
* radii
* orientations

Keep counts bounded.

Do not generate billions of descriptors at once.

---

# 39. INTERGALACTIC TRAVEL PROOF

This sprint does not require a polished second galaxy experience.

But provide a debug validation:

```text
travel outside Galaxy A
↓
cross intergalactic space
↓
identify Galaxy B
↓
approach Galaxy B
```

Even if Galaxy B only supports preliminary local-sector generation.

This proves hierarchy extensibility.

---

# 40. NO HARDCODED SINGLE-GALAXY ASSUMPTIONS

Search the codebase for assumptions such as:

```text
GetGalaxy()
MilkyWaySingleton
CurrentGalaxy always 0
```

Avoid architectural constraints that make multiple galaxies impossible.

A current default galaxy may exist for convenience.

Identity must remain general.

---

# 41. SYSTEM MAP DEBUG VIEW

Create a minimal system map/debug view.

Show:

* star
* planets
* moons if present
* player
* canonical distances

This is development tooling.

No polished map required.

---

# 42. GALACTIC DEBUG VIEW

Create a debug galaxy visualization capable of showing a manageable local sample:

```text
player
nearby sectors
nearby systems
current target
```

Allow zooming/scaling.

Do not attempt to render entire billions-star galaxy individually.

---

# 43. UNIVERSE DEBUG VIEW

If practical, create a coarse debug view:

```text
Galaxy A
Galaxy B
Player
```

for validating universe-scale travel.

Schematic rendering is acceptable for development.

---

# 44. SPEED DEBUG HUD

Display:

```text
Current Speed
Speed Regime
m/s
km/s
fraction/multiple of c if useful
AU/s
LY/s
Warp Factor / multiplier
```

This helps tune travel.

Do not confuse display units with internal movement representation.

---

# 45. POSITION DEBUG HUD

Display:

```text
Universe Cell
Galaxy ID
Galaxy-local coordinates
Galactic Sector
Current System
System-local coordinates
Nearest Star
Target Star
Distance to Target
```

Toggleable.

---

# 46. STREAMING DEBUG HUD

Display:

```text
Active galaxy descriptors
Loaded galactic sectors
Generated systems
Distant system visuals
Active star systems
Active planets
Pending generation tasks
```

This should make runaway streaming obvious.

---

# 47. SYSTEM GENERATION VERSION

Introduce/version:

```text
StarSystemGeneratorVersion
GalaxyGeneratorVersion
UniverseGalaxyGeneratorVersion
```

where appropriate.

Changing stellar distribution later should not silently break persistent discovery/state.

Keep versioning explicit.

---

# 48. PERSISTENCE INTEGRATION

Sprint 005 persistence must remain correct across multiple systems.

Persistent modifications on:

```text
Planet A / System A
```

must remain while visiting:

```text
System B
```

Returning to System A should restore those changes.

Do NOT hold System A's runtime actors in memory simply to preserve state.

---

# 49. DISCOVERY PERSISTENCE

If discovery state is implemented:

Persist:

```text
visited system
visited planet
```

through the World State layer.

Do not deeply couple UI/navigation to SQLite.

---

# 50. MULTI-SYSTEM SAVE TEST

Required test:

```text
System A
↓
modify Planet A
↓
warp to System B
↓
modify Planet B
↓
quit
↓
restart
↓
return to A
↓
verify A state
↓
return to B
↓
verify B state
```

---

# 51. CANONICAL SHIP POSITION

The spacecraft canonical position must survive:

* system exit
* interstellar travel
* galaxy coordinate transitions
* warp
* origin rebasing
* system entry
* planet entry

Do not create mode-specific position variables that diverge.

---

# 52. CANONICAL VELOCITY

Similarly define canonical travel velocity/orientation clearly.

Avoid:

```text
SpaceVelocity
WarpVelocity
PlanetVelocity
```

all disagreeing about actual state.

Modes may interpret one canonical movement state differently.

---

# 53. ORIENTATION AT ASTRONOMICAL SCALE

Maintain orientation independently from local frame transitions.

When leaving System A and arriving at System B:

the ship should not arbitrarily rotate because the active frame changed.

---

# 54. ORIGIN REBASING DURING INTERSTELLAR TRAVEL

High-speed travel may cross local-coordinate boundaries extremely frequently.

Ensure rebasing logic can handle this efficiently.

Do not trigger expensive Unreal world operations for every tiny universe-cell change if avoidable.

Separate canonical coordinate normalization from expensive scene rebasing.

---

# 55. LARGE TIMESTEPS / INTEGRATION

At extreme speeds, displacement calculations may become numerically large.

Test:

```text
small dt
high velocity
large total displacement
```

without losing local precision.

Use appropriate double/integer decomposition.

---

# 56. TIME STEP SPIKES

Test movement under:

```text
normal frames
low FPS
temporary frame stalls
```

Warp travel should not explode due to one large delta time.

Clamp/substep logical integration where appropriate.

---

# 57. TARGET TRAJECTORY PREDICTION

Provide:

```text
ETA
braking distance
closest approach
```

where useful.

This does not need orbital mechanics.

Straight-line target travel is enough initially.

---

# 58. PLANETARY MOTION

If planets currently orbit dynamically, ensure travel systems use current body positions.

If orbits are not yet simulated, static system layouts are acceptable.

Do not introduce complex N-body simulation in this sprint.

Document current simplification.

---

# 59. STAR SYSTEM SCALE

Use actual logical astronomical distances where practical.

Visual systems may use LOD representation.

Do not permanently compress:

```text
1 AU → 10 Unreal meters
```

as canonical geometry.

The logical distance should remain physically meaningful.

---

# 60. GALAXY SCALE

Likewise galaxy scale should use meaningful logical distances.

Example order of magnitude:

```text
tens of thousands of light-years
```

Do not hardcode final Milky-Way realism.

Support configurable galaxy radius.

---

# 61. UNIVERSE SCALE

Universe generation should be effectively enormous.

Do not precompute finite global bounds unnecessarily.

Use sparse deterministic spatial generation.

A large finite integer-coordinate universe is acceptable.

It should be effectively unbounded for gameplay.

---

# 62. PROCEDURAL NAMING

If useful, provide deterministic debug names for:

```text
galaxies
stars
systems
planets
```

Do not spend time designing production naming lore.

Names must not define identity.

Stable IDs define identity.

---

# 63. DETECTION RANGE

Distant systems can become discoverable based upon simple range/visibility rules.

Do not add scanning gameplay.

This is for navigation/testing.

---

# 64. WARP ENTRY RULES

Keep initial rules simple.

Possible:

```text
cannot warp while landed
must leave immediate terrain proximity
```

Avoid gameplay complexity.

The main goal is preventing obvious traversal/collision problems.

---

# 65. WARP NEAR PLANETS

Prevent catastrophic behavior.

At very high speed near a planet:

* auto-limit warp
* block warp below configurable altitude
* or enforce trajectory safety

Choose a simple prototype rule.

Document it.

---

# 66. WARP THROUGH STARS

Trajectory safety must prevent blindly passing through stars.

At minimum:

```text
major body safety radius
```

should trigger:

* automatic warp exit
* collision avoidance
* or travel interruption

No combat/damage required.

---

# 67. AUTOPILOT TEST MODE

Provide a developer autopilot:

```text
Target Star
↓
Align
↓
Accelerate
↓
Warp
↓
Brake
↓
Arrive
```

This is extremely useful for repeatable testing.

It does not define final gameplay.

---

# 68. AUTOPILOT TO PLANET

If straightforward, extend:

```text
Target Planet
```

to:

```text
warp to system
↓
interplanetary approach
↓
stop at configurable orbital distance
```

Do not automate atmospheric landing yet unless existing systems make it trivial.

---

# 69. REPEATABLE TRAVEL TEST

Automate:

```text
System A
→ System B
→ System C
→ System A
```

repeatedly.

Monitor:

* canonical position
* active systems
* stale generation jobs
* memory
* actor counts
* persistence state
* frame times

---

# 70. LONG-DISTANCE STRESS TEST

Perform a logical journey of very large distance.

For example:

```text
thousands / millions of light-years
```

using debug speed.

Verify:

* coordinate integrity
* no overflow
* no precision collapse
* no unbounded loaded sectors
* no accumulating systems

---

# 71. GALAXY EXIT STRESS TEST

Travel:

```text
galactic core region
↓
outer disk
↓
galactic edge
↓
intergalactic void
```

Observe:

* star density
* streaming counts
* galaxy representation
* canonical coordinates

No crash / runaway generation.

---

# 72. SECOND GALAXY APPROACH TEST

Using debug traversal:

```text
Galaxy A
↓
intergalactic space
↓
Galaxy B
↓
enter local region of Galaxy B
↓
generate stars
```

This can be visually rough.

It must work architecturally.

---

# 73. GENERATION CACHING

Cache only useful generated descriptors.

Do not repeatedly regenerate the same sector every frame.

But keep caches bounded.

Potential caches:

```text
nearby galactic sectors
star system descriptors
galaxy descriptors
```

Define eviction behavior.

---

# 74. DETERMINISTIC REGENERATION

Evict a sector/system completely.

Return.

It must regenerate the same:

```text
star positions
system IDs
planet descriptors
```

according to generation version.

---

# 75. SYSTEM VISUAL POOLING

Avoid excessive Actor spawn/despawn overhead for distant star representations.

Use:

* instancing
* pooled visual proxies
* GPU-based representations

where appropriate.

Do not make distant stars full system actors.

---

# 76. GALACTIC VISUAL BUDGET

Introduce configurable budgets such as:

```text
MaxRenderedStarPoints
MaxGeneratedNearbySectors
MaxDetailedSystemVisuals
MaxActiveSystems
```

The number of conceptual stars must not control GPU/CPU cost directly.

---

# 77. ACTIVE SYSTEM BUDGET

For a single player, ideally only:

```text
0–1 fully active systems
```

plus lightweight nearby previews.

Do not keep 20 full planetary systems active because they are visually nearby.

---

# 78. PLANET ACTIVATION

Within the active system, planets should also retain their existing hierarchical detail model.

From far away:

```text
planet proxy
```

Approach:

```text
planet LOD
↓
terrain
↓
environment
```

Do not generate surface vegetation merely because the system activated.

---

# 79. TASK PRIORITIZATION

Generation priority should roughly follow:

```text
1. player's current active region
2. collision / safety
3. target destination prewarm
4. nearby system visuals
5. distant galaxy visuals
6. cosmetic background generation
```

Avoid one FIFO queue across all universe work.

---

# 80. TASK CANCELLATION

If player changes warp target:

```text
System B generation
```

may become irrelevant.

Cancel/discard obsolete destination work.

Do not allow target switching to create unlimited background work.

---

# 81. THREADING

Pure generation can occur asynchronously.

Respect Unreal UObject lifecycle rules.

Suggested flow:

```text
worker threads:
sector generation
system descriptors
galaxy samples

game thread:
runtime representation
Actor/component creation
```

Document ownership.

---

# 82. GALACTIC VISIBILITY / PRECISION

Far-away galaxy visuals should not depend on placing geometry millions of light-years away in Unreal coordinates.

Render in camera-relative/local representation.

This principle applies to:

* galaxies
* stars
* distant systems

---

# 83. CAMERA / VISUAL SCALE

At astronomical distances, apparent scale needs careful handling.

Do not use one camera clip range and literal geometry for everything if that produces precision/depth issues.

Investigate multi-scale rendering approaches where needed.

Examples:

```text
background astronomical layer
local system layer
near gameplay layer
```

Keep canonical positions shared.

---

# 84. DEPTH PRECISION

Monitor:

* z-fighting
* near/far clipping
* astronomical object depth
* local surface rendering

Do not destroy surface depth precision to render distant stars.

Use layered representations if necessary.

---

# 85. NO VISUAL TELEPORTS

Internal representations may switch.

Visible positions must match.

Examples:

```text
star point proxy
→ full star object

galaxy billboard
→ local density representation
```

The transition must preserve apparent direction/position.

---

# 86. DOCUMENT SCALE REPRESENTATION

Create:

```text
Docs/Architecture/UniverseScaleRendering.md
```

Explain:

* canonical coordinates
* visual proxies
* active systems
* galaxy rendering
* camera-relative representation
* precision strategy

---

# 87. DOCUMENT INTERSTELLAR TRAVEL

Create:

```text
Docs/Architecture/InterstellarTravel.md
```

Document:

* travel modes
* canonical velocity
* warp
* braking
* trajectory safety
* system prewarming
* arrival
* target changes

---

# 88. DOCUMENT GALAXY GENERATION

Create:

```text
Docs/Architecture/GalaxyGeneration.md
```

Document:

* galaxy descriptors
* galactic sectors
* star density
* deterministic generation
* generation versioning
* caching
* galaxy LOD

---

# 89. DOCUMENT SYSTEM STREAMING

Create:

```text
Docs/Architecture/StarSystemStreaming.md
```

Document:

```text
descriptor
→ distant visual
→ prewarming
→ active
→ unloading
```

---

# 90. AUTOMATED TEST — SECTOR DETERMINISM

For many:

```text
GalaxySeed
SectorId
```

generate repeatedly.

Verify stable star descriptors.

---

# 91. AUTOMATED TEST — SECTOR ISOLATION

Adjacent sectors must not accidentally produce duplicate system IDs.

Test boundary conditions.

---

# 92. AUTOMATED TEST — SYSTEM POSITION ROUND TRIP

Convert:

```text
galaxy-local system position
→ universe position
→ galaxy-local
```

Verify tolerance.

---

# 93. AUTOMATED TEST — INTERSTELLAR MOVEMENT

Integrate a high-speed path over many sectors.

Verify expected canonical final position.

---

# 94. AUTOMATED TEST — WARP OVERSHOOT

Test extreme:

```text
speed
delta time
distance
```

Ensure target interception/braking logic handles overshoot safely.

---

# 95. AUTOMATED TEST — BODY INTERSECTION

Test warp trajectories against:

* star
* planet
* no intersection
* tangent
* multiple possible objects

No catastrophic tunneling.

---

# 96. AUTOMATED TEST — GALAXY DENSITY

Sample galaxy positions.

Verify invariants such as:

* no invalid density values
* disk/bulge structure exists where intended
* points outside configured radius behave appropriately

Do not create brittle art tests.

---

# 97. AUTOMATED TEST — CACHE BOUNDS

Simulate movement through many sectors.

Verify caches remain bounded.

---

# 98. AUTOMATED TEST — SYSTEM LIFECYCLE

Move:

```text
far
→ near
→ active
→ far
```

Verify expected state transitions.

No duplicate active system.

---

# 99. AUTOMATED TEST — MULTI-GALAXY IDENTITY

Generate multiple galaxies/cells.

Verify stable unique galaxy/system identities.

---

# 100. PREVIOUS TESTS

All Sprint 001–005 tests must continue passing.

Interstellar work must not break:

* planetary traversal
* environments
* persistence
* building
* world deltas

---

# 101. MANUAL ACCEPTANCE A — TWO SYSTEMS

Perform:

```text
start System A
↓
select System B
↓
accelerate
↓
leave A
↓
enter interstellar void
↓
warp
↓
approach B
↓
exit warp
↓
enter System B
```

No loading screen.

---

# 102. MANUAL ACCEPTANCE B — LAND ON SECOND PLANET

Continue:

```text
System B
↓
choose planet
↓
approach
↓
enter atmosphere
↓
land
↓
exit ship
↓
walk
```

Planet B must be different from Planet A.

---

# 103. MANUAL ACCEPTANCE C — RETURN

Return to System A.

Verify:

* deterministic system identity
* same planets
* prior persistent modifications still exist

---

# 104. MANUAL ACCEPTANCE D — CHANGE TARGET MID-WARP

During travel:

```text
target B
↓
switch to C
```

Verify:

* no stale generation explosion
* movement remains valid
* target prewarm changes correctly

---

# 105. MANUAL ACCEPTANCE E — GALAXY EXIT

Use debug warp speed.

Travel outside galaxy.

Verify:

* stellar density decreases
* galaxy remains representable behind player
* canonical movement remains valid
* no hard boundary exists

---

# 106. MANUAL ACCEPTANCE F — SECOND GALAXY

Use debug tools to approach a second generated galaxy.

Verify:

* galaxy descriptor generated
* local galactic sectors can activate
* stars can be generated in that galaxy
* IDs remain isolated

This may be a technical validation, not a polished player experience.

---

# 107. PERFORMANCE REPORT

Measure where possible:

```text
sector generation latency
system generation latency
system activation time
system unload time
warp movement CPU cost
star visual count
galactic sector cache size
active system count
pending generation jobs
memory during repeated travel
```

Do not fabricate measurements.

---

# 108. STORAGE SCALING

Verify interstellar generation does not write untouched systems to persistence.

Traveling past:

```text
1000 untouched systems
```

should not create enormous persistent storage.

Only discovery metadata / player modifications should grow where intentionally saved.

---

# 109. SPRINT 006 ACCEPTANCE CRITERIA

Sprint 006 is complete only if:

## Galaxy Architecture

* [ ] deterministic GalaxyDescriptor exists
* [ ] galaxy-local coordinates exist
* [ ] galactic sectors exist
* [ ] deterministic stellar density exists
* [ ] system generation from sectors exists
* [ ] galaxy generation version is tracked
* [ ] multiple galaxies are architecturally supported

## Star Systems

* [ ] multiple deterministic star systems exist
* [ ] stable canonical system positions exist
* [ ] systems stream between lightweight and active states
* [ ] active systems unload correctly
* [ ] returning regenerates identical systems

## Travel

* [ ] player can leave a system without a menu/loading screen
* [ ] player can exist in interstellar space
* [ ] interstellar travel works
* [ ] warp/FTL works
* [ ] canonical movement remains continuous
* [ ] player can target another star
* [ ] braking/arrival works
* [ ] system entry is seamless enough for prototype

## High-Speed Safety

* [ ] major astronomical bodies use swept trajectory safety
* [ ] target overshoot is handled
* [ ] warp near planet/star has defined safe behavior
* [ ] large delta-time spikes do not catastrophically break movement

## Streaming

* [ ] target systems prewarm
* [ ] stale destination work is cancelled/discarded
* [ ] system/sector caches remain bounded
* [ ] only relevant systems reach high fidelity
* [ ] planetary environments remain inactive until relevant

## Galaxy Scale

* [ ] player can debug-travel outside galactic disk
* [ ] star density changes plausibly
* [ ] galaxy aggregate representation exists
* [ ] intergalactic position works
* [ ] at least a second galaxy descriptor can be approached/generated

## Persistence

* [ ] modifications in System A survive travel to B
* [ ] modifications in B remain isolated
* [ ] restart preserves both
* [ ] untouched systems are not fully persisted

## Testing

* [ ] sector determinism tests pass
* [ ] identity tests pass
* [ ] coordinate tests pass
* [ ] warp tests pass
* [ ] trajectory intersection tests pass
* [ ] lifecycle/cache tests pass
* [ ] multi-galaxy tests pass
* [ ] Sprint 001–005 tests still pass
* [ ] project builds successfully

## Manual

* [ ] System A → System B works continuously
* [ ] player can land on a planet in B
* [ ] player can return to A
* [ ] old persistent changes remain
* [ ] galaxy exit works in debug traversal
* [ ] second galaxy architecture is demonstrated
* [ ] no major leak / runaway generation occurs

---

# 110. THE DEFINING DEMONSTRATION

Before declaring Sprint 006 complete, perform this:

```text
START ON PLANET A

↓
walk past previously built persistent structure

↓
enter ship

↓
take off

↓
leave atmosphere

↓
leave Star System A

↓
increase speed

↓
enter warp

↓
cross interstellar space

↓
Star System B becomes visible / active

↓
decelerate

↓
enter System B

↓
choose procedural Planet B

↓
approach

↓
enter atmosphere

↓
land

↓
exit ship

↓
walk through a completely different procedural environment

↓
return to ship

↓
take off

↓
warp back to System A

↓
land on original Planet A

↓
original persistent structure is still there
```

No explicit loading-screen destination travel.

If this works, we have proven:

# AN ENORMOUS PROCEDURAL UNIVERSE CAN BE CONTINUOUSLY NAVIGATED AND REMEMBER PLAYER HISTORY ACROSS STAR SYSTEMS.

---

# 111. DO NOT BEGIN SPRINT 007

Sprint 007 will be:

# MULTIPLAYER UNIVERSE PROOF

It will convert the current single-player architecture into the first authoritative shared-universe implementation.

Expected scope:

```text
dedicated server
↓
2+ clients
↓
same universe seed
↓
same procedural systems
↓
same planet
↓
shared world state
↓
shared persistent buildings
↓
server-authoritative movement/interactions
↓
interest management
↓
initial universe-region ownership architecture
```

Do NOT implement Sprint 007 now.

---

# 112. COMPLETION REPORT

At completion provide:

## Implemented

Exactly what genuinely works.

## Universe Architecture

Explain:

* galaxy generation
* galaxy coordinates
* galactic sectors
* system generation
* system streaming
* multiple galaxy hierarchy

## Travel Architecture

Explain:

* logical velocity
* warp
* braking
* targeting
* high-speed collision safety
* system entry/exit

## Rendering / Scale

Explain:

* star representation
* galaxy representation
* astronomical visual LOD
* camera-relative strategy

## Persistence

Explain how world history remains correct across system unloading/reloading.

## Validation

List exact:

* builds
* automated tests
* interstellar journeys
* galaxy-exit tests
* persistence tests
* stress tests

actually executed.

## Performance

Report measured values only.

## Known Limitations

Be explicit.

## Technical Debt

List temporary representations or shortcuts.

## Recommended Sprint 007

Define the smallest next sprint necessary to prove:

# TWO PLAYERS CAN SHARE THIS SAME PROCEDURAL PERSISTENT UNIVERSE.

Do not implement it yet.

---

# FINAL PRINCIPLE

A star system is not a level.

A planet is not a level.

A galaxy is not a level.

They are increasingly detailed procedural representations of locations within one canonical universe.

The player should be able to point the ship toward something and eventually reach it.

Internally we may:

* stream
* generate
* unload
* change LOD
* change simulation frames
* change rendering techniques

But from the player's perspective:

# SPACE IS CONTINUOUS.

There is always somewhere farther to go.

Begin Sprint 006.
