# ADR-003: Planet Topology, Terrain and LOD

- **Status:** Accepted
- **Date:** 2026-09-06
- **Sprint:** 002
- **Related:** [ADR-001](ADR-001-universe-coordinate-system.md), [ADR-002](ADR-002-seed-hierarchy.md),
  [PlanetCoordinates.md](../Architecture/PlanetCoordinates.md),
  [PlanetTerrain.md](../Architecture/PlanetTerrain.md),
  [PlanetLOD.md](../Architecture/PlanetLOD.md)

---

## Problem

A planet must be a full sphere, capable of being Earth-sized or larger, viewable
from deep space down to walking distance, with terrain that is deterministic,
crack-free, and whose runtime cost depends on where the observer is rather than
on the planet's surface area.

Earth's surface is about 510 million km². At one vertex per 10 m that is 5×10¹⁵
vertices. The planet cannot be a mesh; it has to be a function from which the
required part is reconstructed.

---

## Decision 1: cube-sphere topology

**Selected: six cube faces, spherified by plain normalisation.**

### Options considered

**Lat/long grid.** Rejected: degenerates at the poles, where cells become
infinitely thin and neighbour lookup stops meaning anything. Every quadtree
operation would need a polar special case, and those two points are exactly where
a naive implementation looks fine until somebody flies there.

**Icosphere / geodesic subdivision.** Genuinely more uniform than a cube-sphere.
Rejected for now: triangular subdivision gives a less natural patch addressing
scheme, twelve pentagonal defect vertices still need special handling, and the
uniformity advantage does not pay for the complexity while correctness is still
being established.

**Cube-sphere with an area-equalising tangent warp.** Reduces the linear
distortion ratio from about 2.1 to about 1.16. **Rejected, and this is the
interesting one:** `tan(π/4)` evaluates to `0.9999999999999999` in IEEE-754. That
single ULP means face edges no longer coincide bit-for-bit, so every seam test
would need a tolerance — and a tolerance there is indistinguishable from a real
crack that has not opened far enough to see yet.

**Cube-sphere, plain normalisation. [selected]**

### Why the axis basis is built from 0 and ±1

Every face basis component is exactly `0` or `±1`, so the cube vector
`Forward + Right·s + Up·t` carries no rounding of its own. Two faces sharing an
edge produce bit-identical cube vectors, hence bit-identical directions.

**Seam continuity becomes a property of the construction rather than something
patched up afterwards.** The tests assert bit-exact equality across all twelve
edges and all eight corners — no tolerance anywhere.

### The dyadic constraint (discovered, not planned)

Exactness holds only for UVs of the form `k/2^m`. Seam handling needs `2u-1`, its
inverse, and the reversal `1-u` to be exact; all three are exact for dyadic
values and lossy otherwise.

**Consequence: patch resolution must be `2^p + 1`.** `N = 50` would put a
one-ULP crack along every patch border on the planet. This was found by writing
the seam test at `k/33` and watching it fail by exactly one ULP; it is now
enforced in code and pinned by its own test.

---

## Decision 2: terrain as pure 3D noise, written out

**Selected: own gradient-noise implementation, integer-hashed, sampled in 3D.**

3D sampling at the direction rather than a wrapped 2D heightmap, because a
wrapped map must be cut somewhere and every cut is a seam.

**No external noise library**, against Sprint 002's suggestion of FastNoise2 —
justified as follows. Every operation on the terrain path is integer hashing plus
add, multiply and floor. No transcendentals. All are exactly specified by
IEEE-754, so terrain is bit-identical across platforms and compilers, which is
*stronger* than what Sprint 001 achieved for star generation (documented as an
open libm risk).

A SIMD library would be faster and would make determinism depend on runtime
instruction selection. For ground that players will build on, that is the wrong
trade. If performance demands it, the answer is fewer samples, not unverifiable
ones.

### Fixed octave count

Level-dependent octave counts are the obvious optimisation but make a vertex's
height depend on which LOD asked for it, so the surface shifts on every split.
A fixed count means a point has exactly one height and parent and child agree
bit-for-bit. Correctness first.

---

## Decision 3: screen-space-error LOD with hysteresis and ±1 balancing

**Selected: split when projected geometric error exceeds a pixel threshold.**

Distance thresholds were rejected because the correct distance depends on planet
radius, patch resolution, FOV and screen height simultaneously — one tuned for a
given planet misbehaves on every other. Screen-space error is scale-invariant by
construction, asserted by comparing a 200 km body against a 6371 km one.

Geometric error is measured against the terrain function rather than estimated
from patch size, so flat ground is not over-subdivided nor mountains
under-subdivided.

Hysteresis requires state, so a stateful selector carries the previously-split
set. Balancing keeps neighbours within one level so skirts remain proportional.

### Crack strategy: exact geometry, then balancing, then skirts

In that order of importance. Skirts hide a one-level T-junction; they are never a
substitute for correct base geometry, and the seam tests prove the base is exact.
Geomorphing is not implemented — the architecture permits it (parent and child
agree exactly, which is the precondition) but streaming correctness came first.

---

## Decision 4: renderer behind an abstraction

**Selected: `UPlanetMeshBackend`, with a `UProceduralMeshComponent`
implementation.**

UE 5.8 offers several ways to push generated geometry and none is obviously
permanent: ProceduralMeshComponent is stock and stable but not fastest,
DynamicMeshComponent is more modern with known rendering limitations, Mesh
Terrain is Experimental. Committing the quadtree, streamer and terrain function
to any of them would make replacing it a rewrite instead of a swap.

Geometry is addressed by integer slot so the streamer cannot hold a pointer to
something the backend has recycled.

---

## Consequences

### Good

- Seam continuity is bit-exact and proven, not toleranced.
- Terrain determinism is cross-platform, better than Sprint 001's.
- Runtime cost tracks the observer: 58 patches from 400 km, 225 from 200 m.
- Streaming converges — 1955 built against 1879 released over a 40-teleport
  stress path, the difference being exactly the visible count.
- Renderer, terrain function and quadtree can each be replaced independently.

### Bad / accepted costs

- **Patch resolution is constrained to `2^p + 1`.** Non-negotiable; enforced.
- **Tessellation is ~2.1× denser at face corners than centres**, the price of
  refusing the tangent warp.
- **Fixed octave count costs performance** at low LOD, where detail is generated
  that the mesh cannot show. ~45 ms per patch, single-threaded per patch.
- **No geomorphing**, so LOD transitions pop, bounded by the split threshold.
- **Terrain and scaled-space bodies are separate render spaces** that do not yet
  bridge. This already caused a real bug (a point light in one space illuminating
  geometry in the other with meaningless intensity) and is the central Sprint 003
  problem. *Resolved in [ADR-004](ADR-004-simulation-frames-and-planetary-traversal.md):
  the two spaces now meet at an explicit, hysteretic frame boundary, and scaled
  bodies are hidden inside the planetary frame. The remaining gap - showing
  distant bodies in the sky at true angular size - needs a far-field render pass.*
- **`GenerationVersion` is frozen once players build.** Changing terrain
  generation moves mountains under existing structures.

### Neutral

- Max LOD level 24 is a limit in name only — 0.6 m patches at Earth radius.

## Revisit if

- Tessellation uniformity becomes a measured bottleneck — then the tangent warp,
  with `|s| == 1` special-cased and a new generation version.
- Terrain generation dominates streaming latency — then level-dependent octaves,
  accepting sub-threshold vertex movement across LOD changes.
- Cross-platform multiplayer arrives — terrain is already safe; star generation
  is not (see ADR-002).
