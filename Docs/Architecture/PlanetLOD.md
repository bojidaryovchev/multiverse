# Planet LOD

**Status:** implemented, Sprint 002
**Code:** `Source/UniversePlanet/Public/PlanetQuadtree.h`, `Private/PlanetQuadtree.cpp`
**Decision record:** [ADR-003](../ADR/ADR-003-planet-topology-and-lod.md)

---

## 1. The quadtree is data, not Actors

`FPlanetQuadtree` creates no Actors and owns no meshes. It is evaluated from an
observer position and returns a list of patch addresses; the streaming layer
above reconciles what it currently has with that list.

Sprint 002 is explicit that the Actor hierarchy must not become the
authoritative quadtree. Keeping this side free of engine types is what enforces
it — it is not *possible* to accidentally make an Actor the source of truth from
in here.

Selection output is sorted by **patch address, never by distance**, so a caller
diffing successive frames sees a stable sequence rather than a set that
reshuffles whenever the observer moves.

## 2. Screen-space error, not distance

Splitting on raw distance is the obvious approach and is wrong in a way that only
shows up later: the right threshold depends on planet radius, patch resolution,
field of view and screen height all at once, so a distance tuned for one planet
misbehaves on every other one.

Instead a patch splits when the detail it fails to represent would be visible:

```
screen error (px) = geometric error (m) × projection / distance (m)

projection = screen height (px) / (2 · tan(vertical FOV / 2))
```

Split above 6 px by default. Below about 2 px the subdivision cost climbs
steeply for detail at the edge of perceptibility.

**This is automatically scale-invariant.** A planet ten times larger subtends ten
times the angle at the same distance and subdivides correspondingly, with no
per-planet tuning. `UniverseTest_QuadtreeLodBehaviour` asserts it by comparing a
200 km body against a 6371 km one at the same relative viewpoint and requiring
the patch counts to stay within a factor of five.

### Geometric error

Two contributions, both physical:

- **Curvature.** Even over perfectly flat terrain a chord across one quad departs
  from the sphere by roughly `s² / 8R` — the sagitta. This is what makes a planet
  look faceted from orbit and dominates at low LOD.
- **Relief.** Terrain varies within a patch by up to roughly the relief scaled by
  patch level.

Added rather than maximised, deliberately: over-estimating costs frame time,
under-estimating leaves faceting that no later pass can repair.

Distance is measured to the patch's *nearest* point, not its centre — otherwise a
large patch the observer is standing on reports a huge distance and never
subdivides.

## 3. Hysteresis

Split and merge use different thresholds; merge is 0.5× split by default.

With a single threshold an observer hovering at the boundary makes a patch split,
which immediately satisfies the merge condition, which splits again — rebuilding
meshes every frame forever. This is the single most visible way a terrain LOD
system misbehaves.

Hysteresis needs **memory**, which a stateless selector cannot have.
`FPlanetQuadtreeSelector` carries the set of nodes split last frame and holds
them split until error falls to the merge threshold. The stateless
`SelectPatches` remains available and has no hysteresis, which is the honest
behaviour for a function with no memory.

Measured: jittering an observer by **one metre at 50 km altitude produces zero
selection changes over twenty frames**, while a real descent from 5000 km to
5 km still increases the patch count and a subsequent ascent releases it again.

## 4. Neighbour balancing

Adjacent selected patches differ by at most one level.

Skirts hide a one-level T-junction comfortably. A five-level difference would
need a skirt deeper than the patch is wide, which would be visible from orbit.

Two things were wrong in the first implementation and are worth recording:

- It split **one patch per pass over eight passes**, which caps total splits at
  eight and leaves most violations in place — the selection then looks balanced
  only where the algorithm happened to reach. It now collects every violation per
  pass and applies them together, with deduplication so a patch violating against
  two neighbours is not split twice.
- Neighbour lookup was a **linear scan per query**, which is quadratic — roughly
  16 million comparisons per pass at the 4096-patch cap, every frame. A patch's
  neighbour address may be covered by that patch or any of its ancestors, so the
  query now walks at most 24 ancestors against a sorted key index.

Passes are bounded rather than run to a fixed point: a hang is worse than a
missed split, and since splitting raises a patch one level the worst case is the
initial level spread.

## 5. Horizon culling

Uses the real sphere-horizon condition, not a hemisphere test.

The asymmetry matters and drives the implementation: **terrain wrongly culled is
a hole in the planet; terrain wrongly kept is some wasted triangles.** So the
test is deliberately conservative — it uses the planet's *smallest* radius for
the occluding sphere and its *largest* for what is being tested, the pessimistic
pairing, so a mountain just over the geometric horizon stays visible. The patch's
own angular radius is added, so a patch only partly over the horizon is kept.

Root patches are never culled: removing one would remove a sixth of the planet,
and the coarse levels are cheap enough that keeping them is the safer trade.

An observer inside the planet culls nothing, rather than producing a NaN from an
out-of-domain `acos`.

## 6. Crack prevention

Three mechanisms, in order of importance:

1. **Exact base geometry.** Patch borders and cube-face seams match
   bit-for-bit — see [PlanetCoordinates.md](PlanetCoordinates.md). Nothing else
   here would help if this were wrong.
2. **±1 neighbour balancing**, so the worst T-junction is one level.
3. **Skirts**, which hide that one-level gap.

Skirts are *not* a substitute for (1). Sprint 002 says so explicitly, and using
them that way would surface much later as terrain not lining up with collision.

Geomorphing is **not** implemented. The architecture is compatible with adding it
— parent and child agree on the surface exactly, which is the precondition — but
correctness of streaming and LOD came first, as the sprint instructs.

## 7. Measured behaviour

Planet radius 5013.8 km, patch resolution 65, split threshold 6 px, max level 14,
1280×720:

| Observer altitude | Deepest LOD | Patches | Triangles | Collision patches |
| --- | --- | --- | --- | --- |
| 400 km | 5 | 58 | 504,832 | 0 |
| 100 km | 7 | 80 | 696,320 | 0 |
| 10 km | 10 | 132 | 1,148,928 | 2 |
| 1 km | 13 | 204 | 1,340,416 | 44 |
| 200 m | 14 | 225 | 1,636,352 | 65 |

Selection cost 0.08–0.45 ms; horizon culling removes 8–24 subtrees; balancing
adds 9–49 splits.

Patch count grows sub-linearly with proximity — 58 to 225 across four orders of
magnitude of altitude — which is the property that makes runtime cost depend on
the observer rather than on planetary surface area.

## 8. Verification

| Claim | Test |
| --- | --- |
| Coarse from space, finer on approach, bounded, scale-invariant | `QuadtreeLodBehaviour` |
| Adjacent patches never differ by more than one level | `QuadtreeBalancing` |
| Jitter at an LOD boundary causes no churn; real movement still responds | `QuadtreeHysteresis` |
| Horizon culling never removes visible terrain; handles degenerate observers | `QuadtreeHorizonCulling` |
