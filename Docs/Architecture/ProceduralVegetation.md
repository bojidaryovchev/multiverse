# Procedural Vegetation and Wildlife

What grows where, where each individual stands, and why almost none of them
exist.

Related: [Biomes.md](Biomes.md), [PlanetEnvironment.md](PlanetEnvironment.md).

![A procedural forest](../Sprints/Sprint-004/Surface-Vegetation.png)

---

## Two separable problems

1. **What a biome grows** — a data table from biome to content categories and
   densities. No asset paths, no mesh names, no Unreal types.
2. **Where the individuals go** — a deterministic scatter over a patch that
   answers, for a given patch, exactly which things stand where.

The first is a designer's problem and the second is a mathematician's. Mixing
them is how a vegetation system ends up with tree species hardcoded into
placement loops.

---

## Archetypes, not assets

A profile says "coniferous canopy tree, 340 per hectare", never
`/Game/Trees/SM_Pine_02`. Which mesh represents a coniferous canopy tree is a
rendering decision that belongs in the Unreal layer, and keeping it there is what
lets `UniversePlanet` stay engine-free, run on a worker thread, and work in a
headless server that has no meshes at all.

It is also what makes an alien biosphere possible without touching the placement
code. A fungal world's forest is a `MushroomCanopy` at the same density in the
same biome; only the archetype changes. **Nothing in the placement layer believes
a tree is a tree.**

---

## Densities are real

| Biome | Canopy per hectare |
| --- | --- |
| Rainforest | 600 |
| TemperateForest | 412 |
| Taiga | 360 |
| Savanna | 22 |
| Grassland | 9 |
| Desert | 0 |

A managed temperate woodland runs 300–500 stems per hectare; a tropical
rainforest 400–600; a savanna perhaps 10–40. Using real numbers means the
*ratios* between biomes are right before anything is tuned for looks, and it
makes the streaming budgets meaningful rather than arbitrary.

Grass is the exception — real grass is tens of thousands of blades per hectare,
and one instance stands for a clump.

Per hectare rather than per patch, because a patch's area changes by a factor of
four at every LOD level and a per-patch density would make a forest thin out as
the player walked toward it.

---

## Placement is a pure function of the patch

```
instance = f(planet seed, patch id, layer, cell index)
```

A patch scattered twice produces the identical result, so a player who leaves a
forest and comes back finds the same trees in the same places. No state is kept
and nothing is written down — the same argument as the terrain function's,
applied one level up. It also means placement can run on any thread, in any
order, for any patch, which is what the streamer needs.

The walk is a **jittered grid**. Without the jitter the placement is a visible
lattice, and a lattice of trees reads as artificial from any distance at which
more than a few are visible.

Density is multiplied by the **dominant biome's weight**, so a forest thins as it
approaches a grassland rather than stopping at a line. This is where the biome
blend earns its keep.

One roll per cell decides whether anything is placed, then a second chooses among
the entries proportionally. Rolling each entry separately would let a biome with
four entries place four things in a cell meant to hold one.

### Budget-limited patches thin uniformly

At most one instance is placed per cell, so a grid of R × R can never exceed R²
instances. When the per-patch budget is smaller than the density calls for, the
**grid resolution** is capped rather than the walk being stopped early.

Truncation fills one corner of the patch and leaves the rest empty — a forest
with a straight edge through the middle of it, unmistakably a bug. Capping the
resolution makes the cells larger, the per-cell probability rise to compensate,
and the placement stay as dense as the budget allows across the whole patch.

---

## Streaming does not follow terrain patches

The obvious design is to hang vegetation off terrain patch lifetime. It is wrong,
because the two have different natural resolutions.

Terrain LOD is chosen by screen-space error, so from four kilometres up a single
patch can be two kilometres across — and that patch would need every tree on two
square kilometres, none individually visible. Meanwhile at walking height the
patches under the player are metres across, each carrying a handful of trees in
its own component: thousands of draw calls to render a forest.

So vegetation runs its own selection at a **fixed** patch level, and activates by
distance. Terrain decides how finely the ground is tessellated; vegetation
decides how far away a tree is worth existing.

### Layers have different ranges

| Layer | Contents | Radius | Collision |
| --- | --- | --- | --- |
| Canopy | trees | 1400 m | yes |
| Scatter | rocks, boulders | 700 m | boulders only |
| Understory | shrubs, ferns, cacti | 500 m | no |
| Ground | grass, flowers | 140 m | no |

Their costs and visibility differ by orders of magnitude; treating them the same
guarantees one of them is wrong. Grass at 7,500 clumps per hectare over even a
small radius is tens of thousands of instances.

Collision only on things a player can walk into. Collision on grass would be tens
of thousands of primitives for no gameplay benefit, and is the easiest way to
make a forest unplayable and a landing site impossible to find.

### Two things measured the right way

Both of these were bugs, and both were found by running it:

- **Relevance uses altitude above the terrain**, not above the ocean. Measured
  against the ocean, a player standing on a four-kilometre plateau reads as
  flying and the forest switches off underfoot. On a world whose terrain reaches
  six kilometres that is every mountain forest on the planet.
- **Patch distance is measured along the surface**, not through space. The 3D
  distance from the observer to a point on the reference sphere folds in the
  observer's own elevation, so a player on a two-kilometre plateau measures every
  patch — including the one under their feet — as two kilometres away.

---

## Threading and budgets

Placement is pure mathematics on plain data, so it runs on the thread pool. Only
the instance upload touches Unreal objects, on the game thread, budgeted per
frame. The same arrangement the terrain streamer uses.

Worker tasks capture a shared block by value, never `this`, so a job that
outlives the component drops its result rather than writing into freed memory. A
generation serial discards results for patches that were released and
re-activated while the job ran.

```
MaxTotalInstances         60000    hard ceiling across every layer
MaxInstancesPerPatchLayer  2000    bounds a single job
MaxConcurrentJobs             4
MaxUploadsPerFrame            2    upload is game-thread work
MaxObserverAltitudeMeters  3000    above this, nothing is generated at all
```

The altitude check is the coarsest relevance test and the one that makes fast
traversal cheap: a craft crossing a continent generates nothing, because every
job would be released before it was drawn.

Measured in a temperate forest: **51,779 instances across 40 patches**, holding
steady — 26,797 canopy, 9,238 understory, 7,564 ground, 8,180 rock. Placement
runs 12–75 ms per patch-layer on a worker thread.

---

## Wildlife

Deliberately trivial as behaviour, deliberately specific as architecture.

A population is a **number attached to a biome**, weighted across the blend and
reduced by weather. Individuals exist only where somebody is looking. A world
like the demo planet would plausibly have ten million birds; at most forty-eight
ever exist.

Establishing that now is the point, because the alternative — an Actor per
animal, spawned when the world loads — becomes impossible to reverse once
anything depends on animals having identity. **This component cannot support a
bird the player has named and tamed.** That is a deliberate limit of the
abstraction, and the place to revisit it is when something needs it.

Forty birds as Actors is forty ticking objects with transforms, components and
replication. Forty birds as instances in one component is one draw call and an
array of transforms updated in a loop. At this behavioural complexity the Actor
buys nothing.

Birds stay above the ground **by construction** rather than by avoidance: a
waypoint is chosen at a height above the terrain *under that point*, and the
position is floored every frame — because interpolating between two safe points
still flies through the ridge between them.

The densities are visible-in-flight numbers rather than population ones, and the
code says so. Real breeding-bird densities are far higher than what is ever in
the air at once, and implying a census that is not being taken would be worse
than stating the presentation choice.

---

## Placeholder content

Engine basic shapes: cones for conifers and mushroom canopies, spheres for
broadleaf and rocks, cylinders for dead trees and cacti, tinted per archetype.

What has to be proven is that the *pipeline* runs — a biome decides an archetype,
an archetype resolves to geometry, and the geometry streams and is bounded. A
cone standing in for a conifer proves all of that and takes no art time away from
the architecture it is meant to validate.

Two details that matter even for placeholders:

- **Non-uniform scale.** Uniform scaling makes a "tree" an eighteen-metre sphere,
  which touches its neighbours at any realistic stem density and turns a forest
  into a solid mass. Real crowns are a third to a half as wide as the tree is
  tall.
- **Trees stand along local up, rocks lie along the terrain normal.** Using the
  normal for a tree has it leaning down every hillside, which is the most obvious
  tell of a naive scatter.

---

## Testing

`Universe.Planet.VegetationPlacement`, over six patches on five cube faces at
levels 9–13, for all four layers:

- **Determinism, and not merely in count.** The same instances, in the same
  order, at the same positions, scales and yaws.
- On the surface, between the planet's minimum and maximum radii.
- **Inside the patch they were scattered for.** An instance leaking into a
  neighbour would be generated twice — once by each — and would flicker as they
  streamed independently.
- Never in the ocean; up and normal unit length; scale and yaw in range.
- A barren world grows nothing but rock, and rock still scatters, because rock is
  not alive.
- The fungal profile has comparable canopy density and entirely different
  archetypes — the conditions set how much grows, the biosphere sets what.
