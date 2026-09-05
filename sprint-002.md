Read `CLAUDE.md` completely before doing anything else.

Then inspect the entire repository and the output of Sprint 001.

We are beginning:

# Sprint 002 — Procedural Spherical Planet Foundation

The purpose of this sprint is to build the foundational technology for a **full-scale, deterministic, spherical, procedurally generated planet**.

Do NOT attempt to add the full planetary environment yet.

This sprint is about:

* spherical world topology
* planetary coordinates
* terrain generation
* quadtree subdivision
* LOD
* patch streaming
* crack-free geometry
* deterministic reconstruction
* bounded runtime cost

At the end of this sprint, we should be able to place a camera/spacecraft around a procedurally generated spherical planet, approach it from space, descend toward the terrain, and see increasingly detailed procedural geography without the planet being one enormous prebuilt mesh.

The planet must be architecturally capable of eventually being Earth-sized or larger.

The planet must NOT be a tiny spherical level pretending to be a planet.

---

# 0. PRECONDITION — VERIFY SPRINT 001

Before implementing Sprint 002, verify that Sprint 001 actually satisfies its acceptance criteria.

At minimum verify:

* project builds
* automated tests run
* universe-coordinate system exists
* universe-coordinate tests pass
* deterministic seed hierarchy exists
* test star system generation works
* large logical movement works
* local Unreal coordinates remain safe
* architecture documentation exists

If there are defects that directly block Sprint 002, fix them first.

Do NOT rewrite Sprint 001 unnecessarily.

Preserve working architecture.

Document any Sprint 001 changes made.

---

# 1. SPRINT OBJECTIVE

Implement one procedural spherical planet with:

```text
Planet
│
├── 6 cube-sphere root faces
│
├── quadtree subdivision
│
├── deterministic surface patches
│
├── procedural elevation
│
├── continuous terrain across patch boundaries
│
├── continuous terrain across cube-face boundaries
│
├── runtime LOD
│
├── patch streaming
│
├── nearby collision
│
└── bounded active geometry
```

The player must be able to observe the same planet from:

```text
far space
↓
orbit
↓
high altitude
↓
low altitude
↓
near-surface
```

with progressively increasing geometric detail.

There should be no requirement to generate the whole planet at surface resolution.

---

# 2. OUT OF SCOPE

DO NOT add these yet unless a tiny temporary implementation is absolutely required to validate terrain:

* vegetation
* trees
* grass
* PCG scattering
* wildlife
* birds
* water rendering
* oceans
* rivers
* weather
* clouds
* atmosphere
* buildings
* construction
* persistence deltas
* mining
* resources
* inventory
* combat
* multiplayer
* civilization systems
* economy
* production gameplay
* polished spacecraft mechanics

Those belong to later sprints.

Do not get distracted.

---

# 3. FUNDAMENTAL PLANET RULE

The planet is NOT one giant mesh.

The planet is a mathematical object from which local surface geometry can be reconstructed.

Conceptually:

```text
Planet Definition
+
Planet Seed
+
Surface Patch Address
+
LOD
=
Terrain Patch
```

The full high-resolution planet should never need to exist simultaneously in memory.

At any moment:

```text
far side of planet
→ extremely low detail or absent

visible horizon
→ low/medium detail

area below observer
→ higher detail

immediate local region
→ maximum required detail
```

Runtime cost must depend primarily on the player's active region, NOT total planetary surface area.

---

# 4. PLANET DATA MODEL

Create a clean data representation for a generated planet.

It should contain at least the minimum properties required for this sprint.

Example conceptual data:

```cpp
FPlanetDescriptor
{
    StablePlanetId
    PlanetSeed

    Radius
    MaximumTerrainHeight

    // Future physical properties may be added later.
};
```

Do not blindly copy this exact structure.

Design appropriate Unreal/C++ types.

The descriptor must be:

* deterministic
* serializable
* independent of visual Actor lifetime
* extensible
* usable by future networking
* usable by future persistence
* usable without constructing terrain

Planet generation DATA must remain separate from the terrain's Unreal representation.

---

# 5. PLANET COORDINATE SYSTEM

Design and implement a robust planet-local coordinate model.

We need to be able to express a point on or near a planet independently of arbitrary Unreal world coordinates.

We should support conversions between concepts such as:

```text
planet-space Cartesian position

↕

normalized radial direction + altitude

↕

cube face + face-local UV + altitude

↕

terrain patch + patch-local coordinates
```

Potential conceptual representations include:

```text
Planet ID
Direction
Altitude
```

or:

```text
Planet ID
Cube Face
UV
Altitude
```

or an appropriate combination.

Requirements:

* deterministic
* stable across LOD levels
* supports entire spherical surface
* no singularity that breaks gameplay at poles
* maps cleanly into cube-sphere topology
* allows surface patch addressing
* allows future object persistence
* allows future multiplayer spatial partitioning
* supports conversion back to local Cartesian planet coordinates
* supports accurate distance calculations where appropriate

Document all conventions.

Create:

```text
Docs/Architecture/PlanetCoordinates.md
```

and an ADR if a significant architectural choice is made.

---

# 6. CUBE-SPHERE TOPOLOGY

Use a cube-sphere as the initial planetary topology unless empirical implementation evidence demonstrates a substantially better architecture.

The planet begins as six logical cube faces:

```text
        +Y
         │
   ┌──────────┐
   │   TOP    │
   └──────────┘

┌──────┬──────┬──────┬──────┐
│ LEFT │FRONT │RIGHT │ BACK │
└──────┴──────┴──────┴──────┘

   ┌──────────┐
   │  BOTTOM  │
   └──────────┘
```

Actual face-axis conventions must be explicitly documented.

Do not rely on implicit orientation assumptions.

Implement stable conversion:

```text
Cube Face + UV
        ↓
Cube Vector
        ↓
Normalized / spherified direction
        ↓
Planet Surface Position
```

Evaluate whether simple normalization or a more uniform cube-to-sphere mapping is appropriate.

Do not prematurely overcomplicate this.

Correctness and continuity matter more than perfectly uniform tessellation during Sprint 002.

---

# 7. PATCH ADDRESSING

Every terrain patch must have a stable logical identity.

Example conceptual identity:

```text
PlanetId
Face
LOD
X
Y
```

For example:

```text
Planet 98172
Face +X
LOD 9
X 281
Y 117
```

Patch identity must NOT depend on:

* pointer addresses
* runtime Actor names
* spawn ordering
* transient object IDs

Implement an appropriate:

```cpp
FPlanetPatchId
```

or equivalent.

Requirements:

* equality
* stable hashing
* serialization
* parent lookup
* child lookup
* neighbor lookup
* debug formatting
* validation
* conversion to surface region

A patch should have four children:

```text
Patch
├── NW
├── NE
├── SW
└── SE
```

or another explicitly documented consistent convention.

---

# 8. QUADTREE

Implement a quadtree independently for each cube face.

Conceptually:

```text
Face
└── LOD 0
    ├── LOD 1
    │   ├── LOD 2
    │   ├── LOD 2
    │   ├── LOD 2
    │   └── LOD 2
    ├── ...
```

The quadtree must support:

* split
* merge
* parent/child relationships
* neighbor relationships
* visibility evaluation
* LOD evaluation
* deterministic patch IDs
* runtime patch lifecycle

Do not let Actor hierarchy become the authoritative quadtree representation.

The logical quadtree should be a data structure.

Rendering objects represent its active leaves.

---

# 9. CONFIGURABLE PATCH RESOLUTION

Each active terrain patch should generate a regular grid.

Choose a sensible configurable starting resolution.

For example:

```text
33 × 33 vertices
```

or:

```text
65 × 65 vertices
```

Analyze the trade-off before selecting the default.

Do not hardcode assumptions throughout the system.

Patch resolution should be a configuration value owned by terrain settings.

Changing it should not change the logical identity of the planet.

---

# 10. PROCEDURAL TERRAIN FUNCTION

Implement a deterministic terrain-height function.

Conceptually:

```cpp
double GetTerrainHeight(
    PlanetSeed,
    UnitSphereDirection
);
```

The important part is that the function depends on a spherical direction, NOT a flat wrapped heightmap.

For example:

```text
direction.x
direction.y
direction.z
planet seed
```

may feed deterministic 3D noise.

Start simple but create a composable terrain pipeline.

A first version might conceptually combine:

```text
continental shape
+
large mountain structure
+
medium terrain detail
+
small terrain detail
```

Do not create random unrelated noise at every scale.

We eventually need plausible geography.

Potential conceptual layers:

```text
continentalness
      ↓
macro elevation

domain warped noise
      ↓
mountain / valley structure

higher-frequency detail
      ↓
local terrain variation
```

The implementation should allow these layers to evolve independently later.

---

# 11. NOISE LIBRARY DECISION

Inspect existing Sprint 001 dependencies first.

If no suitable deterministic noise implementation exists, evaluate options.

FastNoise2 may be considered.

If using an external noise library:

* justify the dependency
* pin/version it appropriately
* document integration
* verify licensing
* test determinism
* do not assume SIMD implementations yield bit-identical floating-point values across every architecture

If exact cross-platform deterministic output is not currently guaranteed, document the limitation and architect a future solution.

Terrain identity must not silently change between launches on the same supported environment.

---

# 12. TERRAIN POSITION

For every generated terrain vertex:

```text
direction =
CubeSphereDirection(face, u, v)

height =
TerrainFunction(seed, direction)

position =
direction × (planetRadius + height)
```

Do not accumulate terrain deformation iteratively.

Planet terrain should be reconstructible directly from:

```text
seed
+
coordinates
```

---

# 13. PATCH BORDER DETERMINISM

Adjacent patches MUST mathematically agree along shared boundaries.

This requirement is non-negotiable.

The same physical point queried from either patch must produce the same:

* spherical direction
* elevation
* world surface location

Do not hide incorrect border coordinates using skirts.

Skirts may eventually help hide LOD transitions, but basic patch geometry itself must be mathematically continuous.

Write automated tests for this.

---

# 14. CUBE-FACE SEAMS

Special attention is required where two different cube faces touch.

Example:

```text
+X face
touching
+Y face
```

Their shared edge must represent exactly the same physical curve on the planet.

Create a canonical face-adjacency table.

Test ALL cube edges.

Test ALL cube corners.

There should be no cracks or coordinate disagreement when moving between faces.

Create automated tests that generate matching edge samples from both faces and compare their resulting planet-space coordinates within an explicitly justified tolerance.

---

# 15. NORMALS

Terrain lighting must not reveal obvious patch seams.

Generate appropriate normals.

Investigate whether normals should be:

* derived from neighboring height samples
* derived analytically/approximately from terrain function gradients
* generated from mesh triangles with border support

Patch-edge normals must remain visually compatible.

Do not calculate normals in complete isolation in a way that produces visible seams.

Perfect production shading is not required.

Continuous-looking terrain is required.

---

# 16. LOD SYSTEM

Implement runtime terrain LOD.

LOD selection should depend upon a meaningful error/distance metric.

Do NOT simply write:

```text
if distance < arbitrary value:
    split
```

without documenting the reasoning.

A screen-space error strategy is preferred if practical.

At minimum consider:

* observer distance
* patch angular size
* planet radius
* terrain height range
* screen resolution/FOV where relevant
* patch geometric error

The algorithm should behave sensibly for planets of different radii.

---

# 17. LOD HYSTERESIS

Prevent constant split/merge oscillation.

For example:

```text
split threshold != merge threshold
```

or use another well-defined hysteresis mechanism.

When hovering around an LOD boundary, patches should not repeatedly rebuild every frame.

Create instrumentation that makes LOD churn observable.

---

# 18. NEIGHBOR LOD CONSTRAINT

Adjacent active patches should preferably differ by no more than one LOD level unless an alternative crack-resolution architecture safely supports otherwise.

Implement appropriate balancing.

Example invalid condition:

```text
LOD 12 patch
touching
LOD 5 patch
```

if the mesh strategy cannot reconcile that boundary.

Provide automated tests for quadtree balancing.

---

# 19. CRACK-FREE LOD TRANSITIONS

Different LODs create T-junction problems.

Implement a deliberate solution.

Evaluate approaches such as:

### Edge stitching

Modify boundary topology based on neighboring LOD.

### Skirts

Add downward/inward geometry below patch borders.

### Geomorphing

Morph between LOD representations.

### Combination

A practical combination may be best.

Requirements:

* no obvious holes
* no seeing into planet interior
* transitions acceptable during movement
* system remains understandable and maintainable

Do not use skirts as a substitute for incorrect base patch alignment.

Document the selected strategy:

```text
Docs/Architecture/PlanetLOD.md
```

and, if appropriate:

```text
Docs/ADR/ADR-003-planet-topology-and-lod.md
```

---

# 20. GEOMORPHING

Investigate whether geometric morphing between parent and child terrain is appropriate for the prototype.

The goal is avoiding visually distracting terrain "popping."

If this is reasonably achievable during Sprint 002, implement it.

If not, keep the architecture compatible with adding it later and clearly document the limitation.

Do not spend the entire sprint polishing morphing if core streaming/LOD is not yet correct.

Correctness first.

---

# 21. HORIZON CULLING

A spherical planet gives us useful visibility information.

Terrain far behind the horizon should not need detailed generation.

Implement or plan an efficient horizon-culling test.

At high altitude, large portions of the planet may be potentially visible.

Near the surface, most of the opposite hemisphere is definitely not.

Use mathematically correct sphere/horizon reasoning rather than arbitrary hemisphere tests.

The implementation may be conservative.

Never incorrectly remove terrain that should be visible.

---

# 22. FRUSTUM CULLING

Only generate/render patches relevant to the observer where appropriate.

Use Unreal's built-in rendering culling capabilities where they already solve the problem.

Do not reinvent engine systems unnecessarily.

However, logical terrain generation should avoid producing thousands of high-detail patches simply because the renderer would later discard them.

---

# 23. PATCH STREAMING

Implement a patch lifecycle.

Conceptually:

```text
Needed
↓
QueuedForGeneration
↓
Generating
↓
Ready
↓
Visible
↓
NoLongerNeeded
↓
Released
```

Avoid uncontrolled Actor creation/destruction every frame.

Create an explicit patch manager.

Consider pooling where beneficial.

Patch state must be observable in debug tooling.

---

# 24. ASYNCHRONOUS TERRAIN GENERATION

High-resolution terrain generation must not freeze the game thread.

Move expensive pure-math generation work off the game thread where appropriate.

Important Unreal rule:

BACKGROUND THREADS MUST NOT UNSAFELY MANIPULATE UOBJECTS.

Prefer:

```text
worker thread
    ↓
pure terrain data generation
    ↓
vertex/index/normal data
    ↓
game/render thread
    ↓
safe Unreal mesh update
```

Implement:

* task cancellation where practical
* generation IDs/versioning
* safe handling when a patch becomes irrelevant before generation completes
* bounded task queues
* prevention of duplicate generation requests

Rapid movement must not create an infinite backlog.

---

# 25. HIGH-SPEED OBSERVER BEHAVIOR

Remember the eventual game includes high-speed spacecraft.

If the observer rapidly changes position, the terrain streamer must not attempt to finish every obsolete patch crossed during travel.

Example:

```text
Observer approaches planet

LOD jobs begin

Observer warps away

→ obsolete jobs should be cancelled or discarded
```

The streaming architecture must prioritize current relevance.

This becomes important later for seamless space-to-surface traversal.

---

# 26. TERRAIN RENDERING BACKEND

Keep the logical planet system independent of one Unreal mesh component implementation.

Create an abstraction between:

```text
Planet terrain patch data
```

and:

```text
Unreal renderable/collidable representation
```

Evaluate current Unreal Engine 5.8 options using official documentation.

Possible prototype options may include:

* UDynamicMeshComponent
* ProceduralMeshComponent if appropriate
* generated/static mesh strategies
* custom rendering later
* other current engine mechanisms

Do NOT make the planet data model dependent on the selected renderer.

Important:

Unreal Engine 5.8 Mesh Terrain is currently Experimental.

It may be investigated or prototyped separately, but the foundational planet architecture must not depend on Mesh Terrain unless strong empirical evidence justifies that decision.

Likewise, if using UDynamicMeshComponent for the prototype, remember its current rendering limitations and keep it replaceable.

Document the rendering-backend decision.

---

# 27. COLLISION

Implement collision only where necessary.

Do NOT generate high-detail collision for the entire planet.

Initial strategy should conceptually resemble:

```text
distant terrain
→ rendering only

nearby terrain
→ collision enabled
```

Collision should follow active high-detail terrain near the observer.

Collision generation must not create unacceptable game-thread stalls.

At minimum, validate that a test Pawn/object can interact with terrain near the surface.

Full character movement and spherical gravity belong primarily to Sprint 003.

---

# 28. PLANET SCALE

Do not hardcode the system around one tiny test planet.

Support configurable:

```text
PlanetRadius
MaximumTerrainHeight
```

The architecture should be capable of logical radii comparable to real planets.

Example:

```text
Earth radius ≈ 6,371 km
```

Do NOT interpret this as an instruction to immediately create a six-thousand-kilometre high-detail mesh.

The entire purpose of the patch system is to avoid that.

It is acceptable to use a smaller test radius during debugging if clearly separated from architectural scale.

Test at multiple radii.

---

# 29. PLANET ORIGIN / LOCAL FRAME

Integrate Sprint 002 with the universe-coordinate architecture created in Sprint 001.

The planet must have a canonical universe-space position.

Terrain must be generated in a planet-local coordinate frame.

Do not bake the planet permanently into global Unreal coordinates.

Conceptually:

```text
Universe Position of Planet
        +
Planet-local position
        ↓
Current local simulation frame
        ↓
Unreal transform
```

This is critical for future seamless space traversal.

Document how the coordinate layers interact.

---

# 30. DETERMINISM

Given:

```text
Universe Seed
Planet ID
Planet Seed
Patch ID
terrain configuration version
```

the terrain generator must reproduce the same logical terrain.

Generate the same patch repeatedly and verify:

* positions
* elevations
* topology
* normals within intended determinism guarantees

Do not use:

* current time
* frame count
* transient UObject IDs
* non-seeded random functions

inside deterministic generation.

---

# 31. GENERATION VERSIONING

Introduce a terrain-generation version concept now.

For example:

```text
TerrainGeneratorVersion = 1
```

We will eventually have persistent player structures.

Changing the procedural algorithm underneath existing structures could move mountains and bury buildings.

Therefore procedural generation must eventually be versionable.

Do NOT build a huge migration system now.

Simply make generation version an explicit part of architecture rather than an invisible implementation detail.

Document the issue.

---

# 32. DEBUG MODES

Build excellent developer visualization.

Provide toggles for at least:

### Patch boundaries

Show each active patch boundary.

### LOD visualization

Visually distinguish LOD levels.

### Cube faces

Show which of the six cube faces a patch belongs to.

### Patch IDs

Display/select useful patch identity information.

### Streaming state

Show:

```text
queued
generating
ready
visible
releasing
```

### Terrain metrics

Display:

```text
Active patches
Visible patches
Pending generation tasks
Triangles
Vertices
Highest active LOD
Generation time
Frame time
Observer altitude
Planet radius
```

Debug tooling should make problems obvious.

Do not require reading raw logs for every terrain issue.

---

# 33. FREE-FLIGHT DEBUG CAMERA

Provide or extend a debug spacecraft/camera so the developer can rapidly inspect the planet.

Controls should allow:

* orbiting
* approaching
* retreating
* high-speed altitude changes
* pausing movement
* jumping to useful debug altitudes if necessary

A debug altitude command may include locations such as:

```text
deep space
high orbit
low orbit
100 km
10 km
1 km
100 m
```

Debug teleport commands are acceptable.

They are development tools and do not define final gameplay.

They should help repeatedly test LOD and streaming.

---

# 34. PLANET MATERIAL

Use a deliberately simple material.

The goal is terrain readability, not environmental art.

Useful initial visualization might derive color from:

```text
elevation
slope
LOD debug mode
cube face debug mode
```

Avoid spending time creating production textures.

No biome system yet.

No PCG vegetation yet.

---

# 35. MEMORY BUDGET

Instrument memory/resource behavior.

Moving around the planet indefinitely must not continuously increase:

* patch count
* mesh count
* collision bodies
* task queue size
* memory consumption

Old patches must actually be released or reused.

Create a repeatable stress scenario.

For example:

```text
orbit around planet repeatedly

+
descend / ascend repeatedly

+
move across multiple cube faces
```

Observe whether resource counts converge to a bounded range.

---

# 36. PERFORMANCE

Do not make arbitrary promises such as "must support an Earth-sized planet at 120 FPS" without measurements.

Instead establish measurable baseline metrics.

Record test hardware where possible.

Measure:

* frame time
* terrain generation time
* active patch count
* triangle count
* task queue length
* patch creation rate
* patch destruction/reuse rate
* collision generation cost

Add profiling hooks.

The system should not noticeably stall when routine LOD subdivision occurs.

---

# 37. AUTOMATED TESTS — PLANET COORDINATES

Create tests for:

* face/UV → sphere direction
* sphere direction → face/UV where implemented
* round trips
* patch coordinate boundaries
* parent/child relationships
* neighbor relationships
* cube face adjacency
* cube corners
* altitude conversion
* multiple planet radii

Test edge cases.

---

# 38. AUTOMATED TESTS — TERRAIN CONTINUITY

For many deterministic seeds:

Generate adjacent patch edges.

Verify that equivalent samples produce equal surface positions/elevations within the documented tolerance.

Test:

* horizontal neighbors
* vertical neighbors
* different LOD neighbors
* all cube-face boundaries
* cube corners

Randomly sample many locations.

Do not test only one hand-selected planet.

---

# 39. AUTOMATED TESTS — DETERMINISM

For a fixed:

```text
PlanetSeed
PatchId
GenerationVersion
```

generate the patch multiple times.

Verify identical logical results according to the project's determinism guarantees.

Test multiple process-independent runs where practical.

---

# 40. AUTOMATED TESTS — QUADTREE

Verify:

* splitting creates correct children
* merging restores parent state
* child coordinates cover exactly the parent region
* no gaps
* no overlaps beyond intentional shared borders
* neighbors resolve correctly
* balancing constraints hold
* invalid IDs are rejected

---

# 41. AUTOMATED TESTS — LOD

Construct deterministic observer positions.

Verify expected qualitative behavior:

```text
far observer
→ low subdivision

near observer
→ high subdivision
```

Verify:

* active patch count remains bounded under configured conditions
* hysteresis prevents obvious split/merge thrashing
* balancing works
* moving away causes patches to merge/release

Avoid brittle pixel-level tests.

Test invariant behavior.

---

# 42. STRESS TEST

Create a repeatable automated or developer-triggered stress path.

For example:

```text
high orbit
↓
rapid descent
↓
surface traverse
↓
cross cube-face boundary
↓
rapid ascent
↓
orbit to opposite side
↓
repeat
```

Run it repeatedly.

Look for:

* crashes
* leaks
* invalid mesh data
* NaNs
* visible holes
* task backlog explosion
* stale patches
* collision errors
* excessive game-thread stalls

Fix root causes.

---

# 43. NUMERICAL SAFETY

Check generated values aggressively in development builds.

Detect:

```text
NaN
Infinity
invalid normals
degenerate triangles
invalid patch bounds
negative radii
impossible terrain values
```

Fail loudly in development rather than allowing numerical corruption to propagate.

---

# 44. THREADING SAFETY

Terrain generation is likely to become heavily parallel.

Establish clear ownership now.

Document:

* which data is immutable
* which data can be accessed by workers
* which systems belong to the game thread
* how completed generation results return safely
* how tasks are cancelled
* how shutdown is handled

Tests and assertions should catch unsafe lifecycle behavior where possible.

---

# 45. CODE STRUCTURE

Inspect the existing Sprint 001 module structure before adding new modules.

Potential conceptual areas:

```text
Planet/
    PlanetDescriptor
    PlanetCoordinates
    CubeSphere
    PlanetPatch
    PlanetQuadtree
    TerrainGenerator
    TerrainStreaming
    TerrainRendering
```

Do NOT automatically create one file/module per bullet.

Choose clean boundaries.

Keep pure mathematical systems as independent from Actor/UObject code as practical.

A good dependency direction is:

```text
Planet Math
      ↓
Terrain Generation
      ↓
Terrain Streaming
      ↓
Unreal Rendering
```

NOT:

```text
Actor
↓
Actor
↓
Actor
↓
random procedural logic everywhere
```

---

# 46. ARCHITECTURE DOCUMENTATION

At minimum produce/update:

```text
Docs/Architecture/PlanetCoordinates.md
Docs/Architecture/PlanetTerrain.md
Docs/Architecture/PlanetLOD.md
```

Add appropriate ADRs.

Documentation should contain useful diagrams and concrete conventions.

Document:

* cube-face orientations
* UV conventions
* patch identity
* quadtree addressing
* terrain function
* generation version
* LOD algorithm
* crack solution
* streaming lifecycle
* renderer abstraction
* relationship to UniversePosition

Do not write documentation that simply restates class names.

Explain why the architecture works.

---

# 47. DO NOT PREMATURELY BUILD CONTENT

During Sprint 002, a successful planet may look visually ugly.

Something like:

```text
gray spherical planet
+
mountains
+
valleys
+
LOD
```

is enough.

This sprint is a SUCCESS if the underlying technology is excellent.

It is NOT a failure because there are no trees.

We will make it beautiful later.

---

# 48. MANUAL ACCEPTANCE EXPERIENCE

At completion, I should be able to launch the project and:

1. See the procedural planet from space.

2. Fly/orbit around it.

3. Observe that it is a full sphere.

4. Approach any visible region.

5. Watch terrain detail increase.

6. Move toward mountains and valleys.

7. Reach near-surface altitude.

8. See detailed terrain without loading the complete planet.

9. Move long distances across the surface.

10. Cross between cube faces without a visible seam.

11. Move back to orbit.

12. Observe terrain detail decrease and resources being released.

13. Approach another side of the same planet.

14. See terrain appropriate to the same deterministic planet seed.

15. Restart the application.

16. Observe that the same terrain regenerates.

No production art is required.

---

# 49. SPRINT 002 ACCEPTANCE CRITERIA

Sprint 002 is complete only if:

## Architecture

* [ ] Planet descriptor is deterministic and independent of Actor lifetime.
* [ ] Planet coordinate system exists.
* [ ] Cube-sphere conventions are explicitly documented.
* [ ] Stable patch IDs exist.
* [ ] Quadtree architecture exists.
* [ ] Terrain generation version exists.
* [ ] Rendering backend is separated from logical terrain data.

## Geometry

* [ ] Full spherical planet can be represented.
* [ ] All six cube faces generate correctly.
* [ ] Procedural elevation works.
* [ ] Planet radius is configurable.
* [ ] Terrain height range is configurable.
* [ ] Patch boundaries mathematically match.
* [ ] Cube-face boundaries mathematically match.
* [ ] Normals do not create severe patch seams.

## LOD

* [ ] Planet starts with low geometric complexity from space.
* [ ] Approaching terrain causes progressive subdivision.
* [ ] Moving away causes merge/release.
* [ ] LOD hysteresis exists.
* [ ] Neighbor LOD constraints are enforced where required.
* [ ] No visible holes occur at mixed LOD boundaries.
* [ ] Terrain does not constantly pop/thrash around thresholds.

## Streaming

* [ ] Terrain patches have explicit lifecycle management.
* [ ] Expensive generation is moved off the game thread where appropriate.
* [ ] Obsolete tasks do not create unlimited backlog.
* [ ] Patch count remains bounded.
* [ ] Leaving regions releases or reuses resources.
* [ ] Rapid movement does not crash the terrain system.

## Collision

* [ ] Nearby terrain can provide collision.
* [ ] Distant terrain does not unnecessarily generate expensive collision.

## Integration

* [ ] Planet has a canonical UniversePosition.
* [ ] Planet-local coordinates integrate with Sprint 001 coordinate architecture.
* [ ] Unreal local coordinates do not become our canonical planet representation.

## Testing

* [ ] Coordinate tests pass.
* [ ] Determinism tests pass.
* [ ] Quadtree tests pass.
* [ ] Patch-edge tests pass.
* [ ] ALL cube-face boundary tests pass.
* [ ] LOD tests pass.
* [ ] Serialization tests pass where relevant.
* [ ] Full project builds successfully.

## Manual validation

* [ ] Planet can be viewed from space.
* [ ] Observer can approach arbitrary regions.
* [ ] Terrain progressively gains detail.
* [ ] Surface can be inspected at high detail.
* [ ] Observer can cross cube-face boundaries.
* [ ] Observer can return to orbit.
* [ ] Same planet regenerates after restart.
* [ ] No major terrain cracks are visible.
* [ ] No unbounded memory/task growth is observed during the defined stress test.

---

# 50. DO NOT BEGIN SPRINT 003

Do not proceed into:

* atmosphere
* seamless atmospheric transition work
* polished ship landing
* character planetary gravity
* walking around the sphere
* water
* biomes
* vegetation

until Sprint 002 acceptance criteria have been validated.

If there is spare time, improve Sprint 002.

Do not expand scope.

---

# 51. AT COMPLETION

Provide an engineering report with:

## Implemented

Exactly what works.

## Architecture

Important choices such as:

* planet coordinates
* cube-sphere mapping
* quadtree
* terrain function
* LOD algorithm
* streaming
* crack prevention
* rendering backend

## Validation

List the exact:

* builds
* automated tests
* stress tests
* manual tests

that were actually executed.

Include results.

## Performance

Report measured:

* patch counts
* triangle counts
* terrain generation timings
* frame timings where available
* memory behavior
* task queue behavior

Do not fabricate measurements.

## Known limitations

Be explicit.

## Technical debt

Identify anything intentionally temporary.

## Recommended Sprint 003

Define the smallest next sprint required to achieve:

# SEAMLESS SPACE → ATMOSPHERE → PLANET SURFACE TRAVERSAL

Sprint 003 will primarily introduce:

* true continuous approach to the planet
* spacecraft transition through planetary scales
* planet-relative local simulation frame
* planetary gravity
* ground collision integration
* player exit/walking
* surface orientation
* safe surface-to-space transition

Do NOT implement Sprint 003 yet.

---

# FINAL PRINCIPLE

Never solve planetary scale by generating more of the planet.

Solve planetary scale by generating **less**.

At every moment, compute only the portion of the planet that the observer can meaningfully perceive or interact with.

A planet can have:

```text
510,000,000 km² of surface
```

while only a tiny number of terrain patches exist at gameplay resolution.

The player's location determines detail.

The planet seed determines identity.

The quadtree determines spatial resolution.

Streaming determines runtime cost.

That is the foundation we need.

Begin Sprint 002.
