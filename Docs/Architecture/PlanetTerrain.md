# Planet Terrain

**Status:** implemented, Sprint 002
**Code:** `Source/UniversePlanet/` (`PlanetNoise.h`, `PlanetTerrain.*`, `PlanetPatchMesh.*`),
`Source/Universe/` (`PlanetTerrainComponent.*`, `PlanetMeshBackend.*`, `PlanetActor.*`)

---

## 1. The terrain function

```
elevation = f(planet seed, unit direction)
position  = direction * (radius + elevation)
```

Note what is absent: no iterative erosion, no accumulation, no dependence on
neighbouring samples, no state between calls. A point's height is a pure
function of where it is. That is what makes terrain reconstructible from a seed
instead of stored, and what makes patches generatable in any order on any
thread.

Sampled as **3D noise at the direction**, never a 2D heightmap wrapped onto a
sphere. A wrapped map has to be cut somewhere and every cut is a seam to stitch;
a genuinely 3D field has no cut, so cube-face boundaries and the poles are
unremarkable places.

## 2. Why the noise is written out rather than pulled in

Sprint 002 asks explicitly whether to take a dependency such as FastNoise2. The
answer here is no, for a project-specific reason.

Every value in `PlanetNoise.h` comes from integer hashing plus add, multiply and
floor. **No transcendental function is involved anywhere on the terrain path** —
no `sin`, `cos`, `pow` or `log`. All of those operations are exactly specified by
IEEE-754, so terrain is bit-identical across platforms and compilers.

That is a *strictly stronger* guarantee than Sprint 001 achieved for star
generation, which calls into libm and is documented as an open cross-platform
risk in [ProceduralGeneration.md](ProceduralGeneration.md) section 7.

A SIMD noise library would very likely be faster. It would also make determinism
depend on which instruction set the machine selected at runtime, which for ground
players will eventually build structures on is the wrong trade. The performance
answer, if one is needed, is to take fewer samples rather than make each sample
unverifiable.

The one caveat is compiler contraction — an FMA would change the rounding of
`a*b + c`. The standalone build uses `/fp:strict`, which forbids it.

Two details in the noise that are easy to get wrong:

- **Quintic fade**, not cubic smoothstep. Its second derivative vanishes at both
  ends. Normals come from height differences, and a cubic fade leaves a
  curvature discontinuity at every lattice boundary that shows up as a faint
  grid of lighting creases across the whole planet.
- **`FloorToInt64`, not a cast.** Casting truncates toward zero, which mirrors
  the lattice about the origin and puts a visible discontinuity through the
  middle of the planet.

## 3. The layers

A single FBM at any amplitude gives uniform rolling hills — recognisably
artificial, with no distinction between a continent and a mountain range. So
terrain is composed:

```
continentalness    very low frequency, decides land against ocean basin
       |
       v
macro elevation    broad shape of the land continentalness selected
       |
       v
mountains          ridged and domain-warped, MASKED TO LAND so ranges run in
       |           lines on continents instead of rising out of open ocean
       v
detail             high frequency, small scale, everywhere
```

The masking is what makes this read as geography. Domain warping — offsetting
the mountain sample by another noise field — bends ridge lines into something
that curves and branches, instead of the visibly isotropic blobs raw ridged
noise produces.

Frequencies are **angular** (cycles per radian), not absolute, so features scale
with the planet: a body twice the radius gets continents twice as wide, not twice
as many. A small moon does not end up covered in Earth-sized ranges.

Each layer draws its own seed sub-stream, so a future change to the mountain
layer leaves continents exactly where they are.

### Octave count is fixed, deliberately

Level-dependent octave counts are the obvious optimisation and would let a low
LOD skip detail it cannot resolve. They also make a vertex's height depend on
*which LOD asked for it*, so the surface shifts slightly whenever a patch splits
or merges. A fixed count means a point on the planet has exactly one height, and
parent and child agree on it bit-for-bit. Correctness first; the optimisation is
recorded as future work.

## 4. Relief scales with the body

`MaxElevation = radius × 0.00139`, `MaxDepth = radius × 0.00172` — Earth's own
ratios (its highest peak is 0.139% of its radius, its deepest trench 0.172%).

A fixed 9 km would give a small moon Himalayas and a gas giant a billiard-ball
surface. An earlier version inflated these to 0.30% "to make relief legible
during validation", which turned out to be a bad trade: it put 15 km peaks on a
5000 km planet, so flying at a 12 km altitude placed the observer *inside a
mountain*. Physical values keep altitude meaning what it says.

## 5. Normals

Derived from the **terrain gradient** by central differences along two tangents,
not from mesh triangles.

A triangle-derived normal depends on which triangles happen to exist, so two
patches meeting at a seam compute different normals for the same point and the
seam lights up as a visible crease. A gradient normal depends only on position,
so both sides agree — asserted bit-exactly by `UniverseTest_TerrainNormals`.

Central rather than forward differences, because a forward difference biases the
normal by half a step and tilts lighting consistently in one direction. The
sampling epsilon is tied to vertex spacing so normals describe the terrain at the
scale the patch actually represents; a fixed epsilon would sample sub-vertex
detail at low LOD and produce noisy shading of features the mesh cannot show.

The tangent basis picks its reference axis away from the direction. A fixed
reference would give a degenerate cross product at two points on every planet,
sitting exactly at the poles of the chosen axis.

## 6. Patch meshes

Output is plain `float`/`int32` arrays — no `UStaticMesh`, no component, no
UObject. That is what lets the expensive part (hundreds of thousands of noise
evaluations) run on a worker thread.

**Vertices are stored relative to the patch centre, not the planet centre.** At
Earth radius a vertex is 6.4e6 m out, where a float ULP is about 0.5 m, so a
patch a few metres across would collapse into a handful of distinct positions and
terrain would visibly quantise. Patch-local, magnitudes are metres and float is
sub-millimetre. The patch origin itself stays in double and is applied by the
component transform.

Geometric error is **measured** — the deviation between the patch's triangles and
the true terrain at each quad centre — not estimated from patch size. That is
what feeds LOD, and a size-based estimate over-subdivides flat ground and
under-subdivides mountains.

### Skirts

A rim of geometry dropped inward along the radius, depth proportional to patch
size so it covers the LOD gap at every level.

Skirts are used **strictly for LOD T-junctions, never to hide mismatched base
geometry**. Sprint 002 is explicit about that distinction and the seam tests
prove base geometry already matches exactly. Using skirts to paper over bad
alignment would surface much later as terrain not lining up with collision.

## 7. Streaming

```
Needed -> Queued -> Generating -> Ready -> Visible -> Releasing
```

Explicit states so behaviour is observable rather than inferred. "Terrain is
popping in slowly" has completely different causes if patches pile up in
`Queued` (generator saturated) versus `Ready` (game thread not draining), and
without states the only way to tell them apart is guesswork.

Nothing is created or destroyed per frame; slots are pooled. A descending
observer turns over hundreds of patches per second, and creating and destroying
components at that rate produces steady GC and render-thread churn that shows up
directly as hitching.

### Threading

Workers never touch a UObject. They read immutable copies of the planet
descriptor and settings, write into a heap-allocated mesh, and push it onto an
MPSC queue the game thread drains.

Tasks capture a **shared state block**, not the component. Capturing the
component would be a use-after-free waiting to happen: a generation launched just
before teardown finishes milliseconds later, and by then the component, its queue
and its counters are gone. A shutdown flag *on the component* would not fix that
either — it could be destroyed between the check and the enqueue. Only moving the
shared data out removes the window.

Generation timing is carried on the result and totalled by the consumer;
accumulating it in the worker would be a data race for a statistic.

### Outrunning the streamer

A spacecraft can cross the region a patch was queued for long before it
finishes. Every request carries a serial, and a result is dropped unless the
patch is still selected and the serial still matches. In-flight generations are
capped rather than letting the queue grow.

The stress path teleports rather than flies, precisely because arriving somewhere
instantly with every in-flight generation stale is the streamer's worst case.
Measured: 79 results discarded across 40 teleports, and no backlog.

## 8. Rendering backend

Everything above the backend speaks in `FPlanetPatchMesh`; only
`UPlanetMeshBackend_ProceduralMesh` knows what a component is.

UE 5.8 offers several ways to push generated geometry and none is obviously
permanent — `UProceduralMeshComponent` is stock and stable but not the fastest,
`UDynamicMeshComponent` is more modern with known rendering limitations, Mesh
Terrain is Experimental. Committing the quadtree, streamer and terrain function
to any of them would make replacing it a rewrite instead of a swap.

Slots rather than components: the streamer refers to geometry by an integer
handle, so it cannot hold a raw pointer to something the backend has recycled.

## 9. Lighting, and a render-space trap

Terrain is lit by a **directional light on the planet actor**, with illuminance
computed from the real star separation in universe coordinates.

This was not the first attempt, and the failure is worth recording because it is
exactly the kind of mistake the two-render-space design invites. The star's point
light lives in `ScaledAstronomical` space, where distances are shrunk by 1e-7,
while terrain lives in `Local` space at 1:1. Their Unreal separation is therefore
**meaningless** — measured illuminance on terrain was 68 lux from 400 km altitude
and 950,000 lux from 4 km, purely as an artefact of the mismatch.

A star seen from a planet is a directional light: its rays are parallel to well
within any measurable tolerance, and a directional light needs no distance, so it
is immune to the mismatch. The star's point light is now restricted to lighting
channel 1 along with the scaled-space bodies it was computed for.

Illuminance is clamped to 25,000 lux for display. The physical value at this
planet's 0.23 AU orbit is about 950,000 lux — seven times Earth noon — and
feeding that to the tonemapper alongside an emissive star washes the terrain out.
Real photometric range belongs with the atmosphere and scaled-space camera work.

## 10. Debug visualisation

`universe.TerrainDebugMode` selects what vertex colour encodes:

| Mode | Shows |
| --- | --- |
| 0 | Elevation ramp — ocean, coast, lowland, highland, snow |
| 1 | LOD level, categorical colours |
| 2 | Cube face |
| 3 | Patch checkerboard with yellow borders |

Three of Sprint 002's required visualisations for the price of a vertex colour,
with no authored material.

`universe.GotoAltitude <m>` and `universe.TerrainStress <cycles>` teleport rather
than fly — a hand-flown test cannot be repeated identically, so two runs cannot
be compared and a slow leak is invisible.

## 11. Generation versioning

`PlanetTerrainVersion::Current` is part of a planet's identity and its content
hash.

Players will eventually build structures anchored to particular ground. If the
algorithm changes silently underneath them, mountains move and buildings end up
buried. No migration machinery exists yet and none is needed yet; what is needed
now is that the version is not an invisible implementation detail later.

## 12. Verification

| Claim | Test |
| --- | --- |
| Noise is reproducible, bounded, lattice-correct | `PlanetNoiseBasics` |
| Terrain is a pure function, order-independent | `TerrainDeterminism` |
| Elevation stays in range at four planet radii | `TerrainBounds` |
| Terrain matches exactly across every seam and patch border | `TerrainSeamContinuity` |
| Normals are unit, outward and seam-continuous | `TerrainNormals` |
| Descriptor derives from Sprint 001, round-trips | `PlanetSurfaceDescriptor` |
| Meshes are valid: no NaN, no degenerate triangles | `PatchMeshValidity` |
| Mesh borders coincide in world space | `PatchMeshBorders` |
