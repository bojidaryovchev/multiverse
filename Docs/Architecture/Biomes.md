# Biomes

Which environment a place is, decided by what its conditions are.

Related: [ClimateSystem.md](ClimateSystem.md),
[ProceduralVegetation.md](ProceduralVegetation.md).

---

## A table of boxes, not a cascade of ifs

The obvious implementation is a chain of `if` statements, and it works exactly
until the first person wants to add a biome. Then every threshold has to be
re-checked by hand against every neighbour, because the branches are ordered and
the order is load-bearing.

A table of **boxes in climate space** removes the ordering:

```cpp
{ EPlanetBiome::TemperateForest,
  /* temperature */ C(3.0),  C(24.0),
  /* humidity    */ 0.45,    0.85,
  /* altitude    */ 0.0,     2600.0,
  /* min slope   */ 0.45,
  ... }
```

Each biome declares the range of conditions it lives in. A point is scored against
every box by how far outside it lies — **in units of the range's own width**, so a
biome with a narrow temperature band is not automatically beaten by one with a
wide band. The nearest wins.

Adding a biome is adding a row. A row that overlaps a neighbour produces a blend
rather than a contradiction. Overlaps in the table are intentional; a table of
disjoint boxes would put a hard line at every boundary.

The temperature and moisture boxes are a coarse **Whittaker diagram** — the
standard biologists' plot of biome against mean temperature and annual
precipitation — which is where their shape comes from rather than from taste.
Altitude and slope are added because a planet has mountains and a Whittaker
diagram does not.

---

## The result is a blend

```
TemperateForest 37%  +  BarrenRock 37%  +  Snow 23%  +  Rainforest 4%
```

A single winner puts a hard line across the planet wherever two boxes meet, and
hard lines between a forest and a desert look exactly as wrong as they sound. So
the classifier returns the best four with weights, and every consumer — terrain
colour, vegetation density, wildlife population — interpolates rather than
switching.

The weights come from a softmax over the box distances, which has the useful
property that a point deep inside one box gets essentially all the weight while a
point on a boundary gets an even split, with a smooth transition and no tuning to
make it so.

Four entries because that is how many can plausibly meet at a point in a
four-dimensional climate space, and because four fits a vertex colour and a
material's layer blend without further compression.

### The one transcendental

`exp` is used for the softmax, and it is acceptable here in a way it would not be
on the terrain path: biome weights feed appearance and density, never geometry, so
a last-bit difference between platforms changes a blend imperceptibly rather than
moving ground somebody built on. **Which biome wins** is decided by an exact
comparison, not by the exponential.

---

## Water is geometry, not climate

Submersion is decided *before* the table is consulted:

```cpp
if (Climate.bOcean) { return Ocean, weight 1.0; }
```

Letting a temperature range have an opinion about it would eventually put a desert
on a sea floor, and that failure would be invisible until someone flew over the
right stretch of water.

---

## Steep ground is rock

`BarrenRock`'s slope requirement is inverted relative to every other row: it wants
ground too steep for soil to stay on. The slope shortfall is weighted four times
heavier than the other terms, because it is what puts rock on cliffs and it should
not be outvoted by a good temperature match.

This is what stops a cliff face growing a swamp, and it is asserted by test.

---

## An alien biosphere is a second table

`EPlanetBiosphere` selects which table applies. `Barren` and `Terrestrial` share
climate boxes and differ only in what grows; `Fungal` shifts the boxes slightly —
a fungal biosphere plausibly tolerates cold and low light better — and changes the
colours entirely.

**The important part is that the shape of the system does not change between
them.** A fungal rainforest sits in the same conditions as a terrestrial one, and
grows at a comparable density, because the *conditions* set the density. Only the
content differs.

That is the whole of section 64's "prove an alien world is a data change, not a
code change", and it is asserted by test: the same climate classifies as
`TemperateForest` under both biospheres, with measurably different appearance and
entirely different vegetation archetypes.

---

## The biomes

`Ocean`, `Coast`, `Desert`, `Grassland`, `Savanna`, `TemperateForest`,
`Rainforest`, `Wetland`, `Taiga`, `Tundra`, `Snow`, `BarrenRock`.

Deliberately a small set. Fifty biomes is a content decision dressed up as an
architecture, and the architecture that matters is the table, not its length.

Names are *families* rather than specific ecosystems — "Rainforest" covers
anything hot and very wet, whatever the biosphere fills it with. That is what lets
one enumeration serve a terrestrial world and a fungal one.

---

## Testing

`Universe.Planet.BiomeClassification` is written as invariants over climate
*families* rather than exact assignments, because the tuning values will change
and a test that pinned them would be testing the table against itself.

- hot and dry is a desert; hot and very wet is a rainforest or wetland
- temperate and moderately wet is a forest
- freezing is snow whatever the moisture
- steep ground is rock whatever the climate
- submerged is ocean, weight exactly 1, no other entries

Over a 21 × 21 sweep of climate space: weights sum to one, are ordered
descending, are bounded, the blend is never empty, and the appearance is a valid
colour.

And the one that catches a classifier that has quietly become a switch: walking a
humidity gradient must pass through **meaningfully blended** samples — at least 20
of 201 with a second entry above 10%.
