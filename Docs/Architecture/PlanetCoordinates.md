# Planet Coordinates

**Status:** implemented, Sprint 002
**Code:** `Source/UniversePlanet/Public/CubeSphere.h`, `PlanetPatchId.h`, `PlanetSurface.h`
**Decision record:** [ADR-003](../ADR/ADR-003-planet-topology-and-lod.md)

---

## 1. Why a cube, not a globe

A sphere has no singularity-free square chart. A latitude/longitude grid is the
obvious first idea and degenerates at the poles: cells become infinitely thin,
neighbour lookups stop making sense, and every quadtree operation needs a
special case for two points on the planet. Those two points are exactly where a
naive implementation looks fine until somebody flies there.

Six cube faces cover the sphere with no singular point at all. Every face is an
ordinary square with ordinary neighbours, so one quadtree implementation works
everywhere including the poles, which are unremarkable interior points of two
faces.

## 2. Face axes

Each face has an outward `Forward` (its normal) and a right-handed `(Right, Up)`
basis with `Right x Up == Forward`:

| Face | Forward | Right (u) | Up (v) |
| --- | --- | --- | --- |
| PosX | (+1, 0, 0) | (0, +1, 0) | (0, 0, +1) |
| NegX | (-1, 0, 0) | (0, -1, 0) | (0, 0, +1) |
| PosY | (0, +1, 0) | (-1, 0, 0) | (0, 0, +1) |
| NegY | (0, -1, 0) | (+1, 0, 0) | (0, 0, +1) |
| PosZ | (0, 0, +1) | (0, +1, 0) | (-1, 0, 0) |
| NegZ | (0, 0, -1) | (0, +1, 0) | (+1, 0, 0) |

**Every component is exactly 0 or ±1, and that is the load-bearing property.**
The cube vector

```
Cube(face, s, t) = Forward + Right*s + Up*t        s, t in [-1, 1]
```

is then computed with no rounding whatsoever. Two faces sharing an edge produce
*bit-identical* cube vectors along it, so after normalisation they produce
bit-identical directions. Seam continuity is a property of the construction, not
something patched up afterwards with a tolerance.

`UniverseTest_CubeSphereFaceBasis` asserts orthonormality, right-handedness and
the 0/±1 property directly, because if someone "improves" an axis to a
normalised diagonal every guarantee below silently stops holding.

## 3. UV convention

Public UV is `[0, 1]²`, because that is what quadtree addressing wants: patch
`(L, X, Y)` covers `u ∈ [X/2^L, (X+1)/2^L]`. The `[-1, 1]` cube coordinate is an
internal detail, `s = 2u - 1`.

**"North" means +V and "East" means +U** throughout. Those words appear in
neighbour lookups and nowhere else, and they mean nothing geographic — a face's
+V is not the planet's north pole.

## 4. The dyadic requirement

Every exactness guarantee here holds for UVs of the form `k / 2^m`, and only for
those.

Seam handling needs three operations to be exact: `s = 2u - 1`, its inverse, and
the reversal `u → 1 - u` used where two faces meet with opposing edge
orientation. For a dyadic `u` all three are exact, because numerator and
denominator stay exactly representable integers and a power of two. For an
arbitrary double they are not — both round, and the two sides of a seam end up
one ULP apart.

This was found by writing the seam test, not by planning: the first version
sampled at `k/33` and failed by exactly one ULP on the reversed seams.

**Consequence: patch resolution must be `2^p + 1`** (33, 65, 129…), because a
patch vertex sits at `u = (X + i/(N-1)) / 2^L`, which is dyadic only when `N-1`
is a power of two. Choosing `N = 50` would put a one-ULP crack along every patch
border on the planet. `FPlanetTerrainSettings::IsValid` enforces it and
`UniverseTest_CubeSeamDyadicRequirement` pins the failure mode so the constraint
cannot quietly lapse.

## 5. Spherification

`Direction = normalize(Cube(face, s, t))`. Plain normalisation, not an
area-equalising warp.

The cost is tessellation uniformity. Differentiating `normalize(1, s, t)` gives
an angular rate of `1.0` per unit `s` at the face centre and only `√2/3 ≈ 0.47`
at a corner, so a UV cell near the centre covers roughly twice the angle of one
near a corner — **the corners are the finely tessellated regions**, which is the
opposite of the intuitive guess and was corrected during testing.

The obvious improvement is the tangent warp `s' = tan(s·π/4)`, which brings the
linear ratio down to about 1.16. It is deliberately **not** used, because
`tan(π/4)` evaluates to `0.9999999999999999` in IEEE-754. That single ULP would
destroy the exactness argument above: face edges would no longer coincide
bit-for-bit, and every seam test would need a tolerance hiding a real
discontinuity. Trading provable continuity for prettier triangle distribution is
the wrong trade at this stage.

## 6. Patch addressing

```cpp
struct FPlanetPatchId { uint8 Face; uint8 Level; uint32 X, Y; };
```

Level 0 is the whole face; level `L` divides it into `2^L × 2^L`. Max level 24 —
at Earth radius that is patches about 0.6 m across, far past any plausible
gameplay resolution, and it keeps `2^Level` arithmetic safely inside int64.

Deliberately **planet-local**. A patch ID says *where on a planet*, not *which
planet*; the planet's identity lives in `FPlanetSurfaceDescriptor`. Baking a
planet ID into every quadtree node would put eight redundant bytes into the
hottest data structure in the system, since a quadtree only ever spans one
planet. `GetPersistenceKey(PlanetSeed)` mixes the planet in where storage needs
it.

The identity is arithmetic — never a pointer, spawn index or Actor name. A patch
generated today, on another machine, after an engine upgrade carries the same
address. That is what will let a player's structure stay pinned to its piece of
ground.

### Children

```
 v
 ^   +--------+--------+
 |   |   NW   |   NE   |     NW = (2X,   2Y+1)     SW = (2X,   2Y)
 |   +--------+--------+     NE = (2X+1, 2Y+1)     SE = (2X+1, 2Y)
 |   |   SW   |   SE   |
 |   +--------+--------+
 +-----------------------> u
```

Children tile their parent exactly, asserted without tolerance: shared borders
must give bit-identical directions from either child *and* from the parent, so
splitting a patch does not move the surface.

### Neighbours across seams

Knowing *which* face lies across an edge is not enough. The neighbour meets it on
some particular edge of its own, and in four of the twelve cube edges the
along-edge direction is **reversed**. Getting that wrong yields terrain that is
continuous on eight seams and mirrored on four — a genuinely nasty bug to chase.

The canonical table records `(NeighbourFace, NeighbourEdge, bReverse)` and is
asserted against the geometry and for symmetry rather than trusted. Neighbour
lookup itself is pure integer arithmetic: the along-edge index is reversed with
`(GridSize - 1) - i`, never through doubles.

The strongest test is symmetry — if A's neighbour is B then A must be among B's
four neighbours. That single property catches essentially every way a seam table
can be wrong, and it is backed by a geometric check so that self-consistent but
wrong addressing cannot pass.

## 7. How this meets Sprint 001

```
universe position of the planet centre     FUniversePosition  (Sprint 001)
                 +
planet-local metres                        terrain            (Sprint 002)
                 v
render origin relative transform           rebasing           (Sprint 001)
                 v
Unreal actor transform
```

The planet centre is an `FUniversePosition` on an anchor component, exactly like
the probe. Terrain is generated in planet-local metres and never knows where the
planet sits in the universe. Patch components are children of the planet actor,
so a render-origin rebase moves the whole planet by moving one actor — no
per-patch fixup, and no possibility of a patch being left at a stale origin.

The observer position handed to the streamer is computed in planet-local space
*from universe coordinates* (`APlanetActor::UniverseToPlanetLocalMeters`), never
from an Unreal transform, so it stays exact regardless of how far the player has
travelled or where the render origin currently sits.

**Terrain renders in Local space at 1:1**, not the scaled astronomical space the
placeholder bodies use. A planet you can land on has to be actual size. Bridging
the two continuously is the Sprint 003 problem — and section 9 of
[PlanetTerrain.md](PlanetTerrain.md) records what that mismatch already cost.

## 8. Verification

| Claim | Test |
| --- | --- |
| Face basis is orthonormal, right-handed, exact | `CubeSphereFaceBasis` |
| Face/UV ↔ direction round-trips | `CubeSphereRoundTrip` |
| Adjacency table matches the geometry | `CubeFaceAdjacencyTable` |
| All 12 cube edges match bit-exactly, both orientations | `CubeFaceSeamsExact` |
| All 8 corners agree across three faces | `CubeCornersExact` |
| Non-dyadic UV breaks exactness by one ULP, no more | `CubeSeamDyadicRequirement` |
| Parent/child/containment | `PatchIdHierarchy` |
| Children tile parents exactly | `PatchIdChildCoverage` |
| Neighbour symmetry across seams | `PatchIdNeighbours` |
| Neighbours are geometrically adjacent | `PatchIdNeighbourGeometry` |
| Patch IDs round-trip; malformed rejected | `PatchIdSerialization` |
| Angular size behaves; distortion is as documented | `PatchAngularSize` |
