# Sprint 002 — Procedural Spherical Planet Foundation

**Date:** 2026-09-06
**Status:** Complete. Builds, runs, and all acceptance criteria verified except
geomorphing, which was explicitly optional and is deferred with reasons.

---

## 0. Precondition — Sprint 001 verified

Before starting: editor target built clean, and 25/25 tests with 144,024
assertions passed in both runners. No Sprint 001 defects blocked Sprint 002 and
nothing in Sprint 001 was rewritten. Sprint 001's coordinate system, seed
hierarchy and rebasing are used unchanged; the only Sprint 001 files touched were
the game module, to spawn and light the planet.

---

## 1. Implemented

### `UniversePlanet` — a new module of pure planet mathematics

No UObject, no reflection, no Engine dependency, matching `UniverseCore` and
`UniverseGeneration`. Terrain generation runs on worker threads and has to be
verifiable without an editor; making it structurally impossible to touch an Actor
is what guarantees both.

| | |
| --- | --- |
| `CubeSphere` | Six faces, canonical edge-correspondence table, exact seam mathematics |
| `FPlanetPatchId` | `(Face, Level, X, Y)`, hierarchy, seam-crossing neighbours, serialisation |
| `PlanetNoise` | 3D gradient noise, integer-hashed, no transcendentals |
| `FPlanetTerrain` | Layered terrain function and gradient normals |
| `FPlanetSurfaceDescriptor` | Planet definition, derived from Sprint 001's descriptor |
| `FPlanetPatchMeshBuilder` | Vertex/index generation with skirts and measured error |
| `FPlanetQuadtree` / `FPlanetQuadtreeSelector` | Screen-space-error LOD, hysteresis, balancing, horizon culling |

### `Universe` — the Unreal layer

`UPlanetMeshBackend` (abstract) + `UPlanetMeshBackend_ProceduralMesh`,
`UPlanetTerrainComponent` (streaming, async generation, collision),
`APlanetActor` (universe-coordinate integration and star lighting), terrain
debug modes, and the scripted stress path.

### Debug tooling

`universe.TerrainDebugMode` (elevation / LOD / cube face / patch checkerboard),
`universe.GotoAltitude`, `universe.TerrainStress`, `universe.TerrainLogInterval`,
`universe.ScreenshotAfterSeconds`, plus a HUD terrain section with patch counts,
triangles, culling, streaming totals and timings.

---

## 2. Architecture

The reasoning is in [ADR-003](../ADR/ADR-003-planet-topology-and-lod.md) and the
three architecture documents. The decisions that mattered most:

**Cube-sphere with an exact 0/±1 axis basis.** Cube vectors carry no rounding, so
two faces sharing an edge produce bit-identical directions. Seam continuity
becomes a property of the construction rather than something patched over with a
tolerance — the tests assert bit-exact equality across all twelve edges and all
eight corners.

**The dyadic constraint, discovered while testing.** Exactness holds only for UVs
of the form `k/2^m`, because seam handling needs `2u-1` and the reversal `1-u` to
be exact. **Patch resolution must therefore be `2^p + 1`**; `N = 50` would put a
one-ULP crack along every patch border on the planet. Found by writing the seam
test at `k/33` and watching it fail by exactly one ULP.

**The tangent warp was rejected on a one-ULP argument.** It would improve
tessellation uniformity from ~2.1× to ~1.16×, but `tan(π/4)` is
`0.9999999999999999` in IEEE-754, and that ULP would force every seam test onto a
tolerance — indistinguishable from a real crack that has not opened far enough
to see.

**Terrain uses no external noise library.** Every operation is integer hashing
plus add, multiply and floor — no transcendentals — so terrain is bit-identical
across platforms and compilers. That is *stronger* than Sprint 001's star
generation, which calls libm and remains an open cross-platform risk. A SIMD
library would be faster and would make determinism depend on runtime instruction
selection, which for ground players will build on is the wrong trade.

**LOD on screen-space error, not distance.** A distance threshold depends on
planet radius, patch resolution, FOV and screen height at once, so one tuned for
a given planet misbehaves on every other. Verified scale-invariant by comparing a
200 km body against a 6371 km one at the same relative viewpoint.

**Renderer behind an abstraction.** UE 5.8 offers several ways to push generated
geometry, none obviously permanent. Everything above the backend speaks in plain
arrays.

**Crack prevention, in order:** exact base geometry, then ±1 neighbour balancing,
then skirts. Skirts hide a one-level T-junction; they are never a substitute for
correct geometry, which the seam tests prove independently.

---

## 3. Validation — exactly what was executed

### Builds (UE 5.8.2, MSVC 14.44.35207)

| Target | Result |
| --- | --- |
| `UniverseEditor Win64 Development` | Succeeded, zero warnings |
| `Universe Win64 Development` | Succeeded, zero warnings |
| Standalone MSVC harness (`/W4 /WX /fp:strict`) | Succeeded, zero warnings |

### Automated tests — both runners, identical results

```
Tools\StandaloneTests\RunTests.bat        ->  49/49 tests, 616168/616168 assertions, exit 0
Automation RunTests Universe (in-engine)  ->  49 succeeded, 0 failed, 616168 assertions, 0 failed
                                              (16 Core + 9 Generation + 24 Planet)
```

24 new tests this sprint. Coverage by sprint section:

| Requirement | Test |
| --- | --- |
| §37 face/UV ↔ direction, round trips, edge cases | `CubeSphereFaceBasis`, `CubeSphereRoundTrip` |
| §14 all cube edges, all corners | `CubeFaceSeamsExact`, `CubeCornersExact`, `CubeFaceAdjacencyTable` |
| §9 patch resolution constraint | `CubeSeamDyadicRequirement`, `PlanetSurfaceDescriptor` |
| §40 quadtree split/merge, coverage, neighbours | `PatchIdHierarchy`, `PatchIdChildCoverage`, `PatchIdNeighbours`, `PatchIdNeighbourGeometry` |
| §7 identity, hashing, serialisation, validation | `PatchIdSerialization` |
| §13 patch border determinism | `TerrainSeamContinuity`, `PatchMeshBorders` |
| §39 terrain determinism | `TerrainDeterminism`, `PlanetNoiseBasics` |
| §15 normals | `TerrainNormals` |
| §28 multiple planet radii | `TerrainBounds` (100 km – 25,000 km), `PatchAngularSize` |
| §41 LOD behaviour, bounded, hysteresis, balancing | `QuadtreeLodBehaviour`, `QuadtreeHysteresis`, `QuadtreeBalancing` |
| §21 horizon culling | `QuadtreeHorizonCulling` |
| §43 numerical safety | `PatchMeshValidity` |

Seam continuity is asserted **bit-exactly, with no tolerance** — 158,304 exact
assertions across every cube edge and patch border including parent/child.

### LOD response, measured in-engine

Planet radius 5013.8 km, patch resolution 65, split threshold 6 px, max level 14:

| Altitude | Deepest LOD | Patches | Triangles | Collision patches |
| --- | --- | --- | --- | --- |
| 400 km | 5 | 58 | 504,832 | 0 |
| 100 km | 7 | 80 | 696,320 | 0 |
| 10 km | 10 | 132 | 1,148,928 | 2 |
| 1 km | 13 | 204 | 1,340,416 | 44 |
| 200 m | 14 | 225 | 1,636,352 | 65 |

Patch count grows sub-linearly — 58 to 225 across four orders of magnitude of
altitude — which is the property that makes cost depend on the observer rather
than on surface area. Collision follows the observer exactly as intended: none
from orbit, 65 patches near the surface.

### Stress test (§42), 4 cycles / 40 teleports across all six faces

```
cycle=4 step=3 traverse (+Z/+X seam) | visible=89 pooled=0  built=114  released=25   tris=774656
cycle=3 step=3 traverse (+Z/+X seam) | visible=89 pooled=0  built=612  released=523  tris=774656
cycle=2 step=3 traverse (+Z/+X seam) | visible=88 pooled=1  built=1113 released=1025 tris=765952
cycle=1 step=3 traverse (+Z/+X seam) | visible=88 pooled=2  built=1610 released=1522 tris=765952
...
complete.  final: built=1955 released=1879 discarded=79
```

**Built 1955, released 1879 — a difference of 76, exactly the visible count.
Nothing leaks.** Pooled slots stayed bounded at 81; visible patch counts and
triangle counts repeat at identical viewpoints across cycles. 79 stale results
were discarded, which is the cancellation path working: the stress path teleports
precisely because arriving somewhere instantly with every in-flight generation
stale is the streamer's worst case.

No crashes, no NaN reports, no task backlog.

### Restart determinism (§48.15–16)

Two independent process launches, same viewpoint:

```
RUN 1  key=0x1ECD585EA22D64B7 seed=0xFCE5A61F57E9B256  r=5013.8 km  relief +6969/-8624 m
RUN 1  selected=126 visible=126 tris=1096704 verts=565110 deepest=L8 culled=3 balance=28

RUN 2  key=0x1ECD585EA22D64B7 seed=0xFCE5A61F57E9B256  r=5013.8 km  relief +6969/-8624 m
RUN 2  selected=126 visible=126 tris=1096704 verts=565110 deepest=L8 culled=3 balance=28
```

Identical in every respect but timing.

### Visual

- `Terrain-Orbit.png` — oceans, inland seas, coastal bands, green lowland, tan
  highland, snow peaks. Recognisable geography.
- `Terrain-PatchBoundaries.png` — patch checkerboard mode; yellow borders meet
  cleanly at corners with no gaps.

---

## 4. Performance

Test hardware: 24-core CPU, 63 GB RAM, DX12.

| Measure | Value |
| --- | --- |
| Patch generation | **~43–56 ms per patch** (65×65 + skirt, single patch, one thread) |
| LOD selection | 0.08–0.45 ms per selection, at 58–225 patches |
| Patch upload (game thread) | < 0.01 ms per frame under a 2 ms budget |
| Concurrent generations | capped at 8; never exceeded |
| Active patches | 58 (400 km) – 225 (200 m) |
| Triangles | 505k – 1.64M |
| Component pool | bounded at ~90 total slots across the stress run |

Generation cost is dominated by normals: `GetSurfaceNormal` takes four extra full
terrain evaluations per vertex, so a 65×65 patch costs roughly five times the
terrain samples its vertex count suggests. That is the obvious optimisation
target and is listed as technical debt rather than done, because the current cost
is comfortably absorbed by the bounded worker pool.

No frame-time budget was established. Sprint 002 asks for measurable baselines
rather than promises, and these are the baselines; a frame-time budget needs a
target platform and a content load neither of which exists yet.

---

## 5. Acceptance criteria

**Architecture** — descriptor deterministic and Actor-independent ✔; planet
coordinate system ✔; cube-sphere conventions documented ✔; stable patch IDs ✔;
quadtree ✔; generation version ✔; rendering backend separated ✔.

**Geometry** — full sphere ✔; all six faces ✔; procedural elevation ✔;
configurable radius ✔ (tested 100 km – 25,000 km); configurable height range ✔;
patch boundaries match ✔ (bit-exact); cube-face boundaries match ✔ (bit-exact);
normals seam-continuous ✔ (bit-exact).

**LOD** — low complexity from space ✔ (6 patches at 1e9 m); progressive
subdivision ✔; merge/release on retreat ✔; hysteresis ✔ (zero churn from 1 m
jitter at 50 km); neighbour constraint enforced ✔; no holes at mixed LOD ✔
(balancing + skirts + exact base geometry); no thrashing ✔.

**Streaming** — explicit lifecycle ✔; generation off the game thread ✔; obsolete
tasks bounded ✔ (79 discarded, no backlog); patch count bounded ✔; resources
released ✔ (1955 built / 1879 released); rapid movement does not crash ✔.

**Collision** — nearby terrain provides collision ✔; distant terrain does not ✔
(0 patches from orbit, 65 near surface).

**Integration** — planet has a canonical `FUniversePosition` ✔; planet-local
coordinates integrate with Sprint 001 ✔; Unreal coordinates are not canonical ✔.

**Testing** — coordinate ✔, determinism ✔, quadtree ✔, patch-edge ✔, all
cube-face boundary ✔, LOD ✔, serialisation ✔ tests pass; full project builds ✔.

**Manual validation** — viewable from space ✔; observer can approach arbitrary
regions ✔; terrain progressively gains detail ✔; surface inspectable at high
detail ✔; cube-face boundaries crossed ✔ (stress path); return to orbit ✔; same
terrain after restart ✔; no major cracks ✔; no unbounded growth ✔.

**Not met:** geomorphing (§20), which the sprint marks as optional — "if not,
keep the architecture compatible and clearly document the limitation".

---

## 6. Known limitations

1. **No geomorphing.** LOD transitions pop, bounded by the 6 px split threshold.
   The architecture is compatible — parent and child agree on the surface exactly,
   which is the precondition — but streaming correctness came first, as
   instructed.

2. **Terrain and scaled space do not bridge.** Terrain renders at 1:1 in Local
   space; the placeholder bodies render in a uniform scale model. This is the
   central Sprint 003 problem and it has already bitten once — see §7 below.

3. **Exposure is not properly handled.** Terrain at a physical 950,000 lux
   alongside an emissive star overwhelms the tonemapper, so star illuminance is
   clamped to 25,000 lux for display. Real photometric range belongs with the
   atmosphere work.

4. **Generation cost dominated by normals** — four extra terrain evaluations per
   vertex, ~5× the sample count a patch's vertices imply.

5. **Fixed octave count** costs performance at low LOD, generating detail the
   mesh cannot show. Deliberate: level-dependent octaves would make a vertex's
   height depend on which LOD asked for it.

6. **No frame-time budget**, only component measurements.

7. **Patch resolution constrained to `2^p + 1`.** Enforced and tested, but a real
   constraint on anyone tuning it.

8. **Directional-light shadows disabled.** Cascades tuned for a 5000 km body are
   Sprint 003 work; terrain reads fine from diffuse shading.

9. **Single streaming planet.** The architecture supports any number — each
   `APlanetActor` is independent — but only one is promoted, deliberately, so
   measurements are readable.

---

## 7. Technical debt

- **Normals recompute terrain four times per vertex.** Caching the grid samples
  would cut generation cost several-fold without changing results.
- **`FPlanetQuadtree` recomputes the whole selection** each interval rather than
  incrementally updating. Cheap now (0.45 ms at 225 patches) but grows.
- **`EstimateGeometricErrorMeters` is an estimate**, while `FPlanetPatchMesh`
  already measures the true value. Feeding the measurement back would tighten
  LOD; the plumbing exists but is unused.
- **Star illuminance clamp** is a display hack with a named constant.
- **Sprint 001's libm determinism gap** is still open; terrain does not share it.
- **Two lighting channels** encode the render-space split. It works, but it is a
  workaround for a structural issue Sprint 003 must resolve properly.

---

## 8. Recommended Sprint 003

**Goal: seamless space → atmosphere → surface traversal.**

The smallest step is not "add atmosphere" — it is **making the two render spaces
one continuous space**. Everything else in Sprint 003 depends on it, and Sprint
002 has already shown what happens when they are treated as separable: a light in
one space illuminating geometry in the other at a meaningless intensity.

Order:

1. **ADR-004: scaled ↔ local transition.** How a planet grows from a
   scaled-space dot into a 1:1 world with no visible seam. Likely a second
   scaled-space camera compositing behind the near-field one, with a handover
   radius, and lights that live in a single defined space.

2. **Implement the transition** for the one streaming planet: approach from
   interplanetary distance to low altitude with no discontinuity, and remove the
   lighting-channel workaround.

3. **Planet-relative simulation frame.** The probe should be able to hold station
   relative to a rotating planet; the render origin should follow the planet
   near the surface.

4. **Planetary gravity and ground collision.** Gravity toward the planet centre;
   the probe rests on terrain instead of passing through it. Collision cooking
   already follows the observer.

5. **Land, and stop.** Ship on the ground, stationary, stable.

Walking, exiting the ship, biomes, water and vegetation stay out of scope until
a ship can descend from orbit and touch a procedurally generated surface without
a loading screen.
