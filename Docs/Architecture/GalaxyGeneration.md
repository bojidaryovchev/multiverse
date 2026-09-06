# Galaxy Generation

*Sprint 006. Source: `Source/UniverseGeneration/Public/GalaxyDescriptor.h`.*

## What a galaxy is

A galaxy in this project is **a density function, not a list of stars**.

It does not contain stars. It says how likely a star is at a place, and sector
generation multiplies its own count by that likelihood. This is the whole idea,
and everything else follows from it:

- A galaxy costs nothing to have. There is no catalogue to build, no list to
  load, and no upper bound on how many of them exist.
- Outside every galaxy the density is **exactly zero**, so intergalactic space
  is genuinely empty rather than a thinner version of the same thing.
- Adding the galaxy layer did not require changing the star generator. It kept
  producing what it always produced, and simply produces nothing where the
  galaxy is not.

Before Sprint 006 the universe had 0.47 star systems per sector everywhere,
uniformly, forever. That is a defensible placeholder and it is also wrong in a
way that matters: a universe with uniform stellar density has no structure, no
landmarks, nowhere crowded and nowhere empty, and no reason for one direction to
be different from another.

## The hierarchy

```text
Universe
  -> Intergalactic cell   2^40 universe cells, about 1.28 million ly
     -> Galaxy            15,000 to 90,000 ly in radius
        -> Sector         4.87 ly, from Sprint 001, unchanged
           -> System
              -> Planet
```

Every level is addressed by integers derived from position, and **nothing at any
level is stored**. An intergalactic cell containing no galaxy costs nothing, and
there are on the order of 10^21 of them.

The cell shift is a power of two (`FGalaxyGenerator::CellShiftInCells = 40`) for
the same reason the sector shift is: floor division is then an arithmetic shift
and therefore exact, so a position never lands in the wrong cell because of a
rounding error at a boundary.

## Galaxy descriptors

`FGalaxyDescriptor` is plain data, produced entirely from
`(universe seed, cell address, index)`. It carries:

| Field | Meaning |
|---|---|
| `Id` | Cell coordinates plus an index. Address-derived, stable, never stored. |
| `Type` | Spiral, Elliptical or Irregular. The density function branches on it. |
| `Position` | The galactic centre, in canonical universe coordinates. |
| `RadiusLightYears` | Beyond this, density is exactly zero. |
| `DiskThicknessLightYears` | Half-thickness at the centre. |
| `BulgeRadiusLightYears` | The central concentration. |
| `DiskNormal`, `DiskRight` | Orientation, as an orthonormal basis. |
| `ArmCount`, `ArmWindingTightness`, `ArmContrast` | Spiral structure. |
| `CoreDensity` | Peak density multiplier at the centre. |

Orientation is stored as **two vectors rather than a quaternion** because that
is what the density function needs - it projects a position onto the disk plane
and measures its height above it - and because a basis can be checked for
orthonormality by inspection, which a quaternion cannot. The test does exactly
that.

The basis is *built* rather than drawn: a random unit normal, then a right
vector made perpendicular to it by Gram-Schmidt. Two independently drawn vectors
are almost never orthogonal, and a basis that is not orthonormal skews every
density sample subtly rather than failing outright.

## Galaxy density

`GetGalaxyCountInCell` gives an expectation of about **0.15 galaxies per
intergalactic cell**, so neighbours are a few million light years apart.

Real galaxy density is about one per 3,000 cubic megaparsecs, which over a cell
this size is roughly one in a hundred and sixty. That is correct and it makes
finding a second galaxy a search over thousands of cells. The number here is
therefore **a gameplay decision, not an astronomical one**, and it is written
down rather than buried.

## Stellar density

`GetStellarDensity(Galaxy, Position)` returns a value in `[0, 1]`, where 1 means
as dense as the galactic core.

The invariants, all asserted by `UniverseTest_GalaxyDensityInvariants`:

- **Finite and bounded.** A NaN here would propagate into a sector's system
  count and from there into whether a player's home star exists.
- **Exactly zero outside the disk radius, and outside the disk thickness.** Not
  "small". A galaxy that faded asymptotically would scatter a thin dusting of
  stars across the whole of intergalactic space, and empty would never actually
  be empty.
- **Greatest at the core.**
- **Denser in the plane than out of it, at the same radius.** This is the disk,
  and it is the one shape claim worth asserting: getting it wrong turns a galaxy
  into a ball of stars. The Milky Way is 100,000 ly across and about 2,000
  thick, a ratio of fifty to one.
- **Continuous.** A step of one part in a thousand of the radius may not change
  density by more than a fifth. A discontinuity would appear in game as a wall
  of stars.

## Galaxy-local coordinates

`FGalaxyLocalPosition` expresses a universe position relative to a galaxy, in
light years: radius in the disk plane, signed height above it, angle around it,
and the full offset vector.

**This is a view, not a source of truth.** Canonical position remains
`FUniversePosition`.

The conversion works in **cell space**, not centimetres, and this is the single
subtlest thing in the layer. `FUniversePosition::TryGetRelativeCm` is exact only
out to about 0.01 light years - that is the entire reason the coordinate type
splits into an integer cell and a local offset - so asking it for a vector
across a galaxy always fails. The first implementation did exactly that, took
the "astronomically far" fallback on every query, and reported zero density
everywhere including inside the galaxy. Cell differences are taken in double and
keep full relative precision at any separation, which is what a galaxy-scale
offset actually needs.

The inverse has the mirror-image problem: a galactic offset is around 10^22 cm,
where a double's ulp is tens of kilometres. `FromGalaxyLocal` splits it into
whole cells plus an exact remainder.

### What the light-year representation costs

One ulp of a double at 50,000 ly is about 7e-12 ly, which is roughly **seventy
kilometres**. Light years are a coarse unit and a double does not rescue them.

That is fine for what this type is for - a density function over a structure
50,000 ly across does not care about a kilometre - and anything that does care
uses `FUniversePosition`, which resolves 2.44 micrometres anywhere in the
universe. The round-trip test asserts agreement to a few ulps of the light-year
representation rather than to an absolute figure, because the absolute figure
varies by ten orders of magnitude between the galactic centre and the rim.

## Deterministic generation

Identical in kind to star systems, and for the same reasons:

- Everything descends from `(universe seed, address)`. There is no cache that
  affects results, no registry, and no mutable global state.
- Identity is **address-derived**, so two galaxies at the same address under
  different universe seeds deliberately share an id and differ only in content.
  What must never collide is two *different* addresses.
- Generating hundreds of unrelated galaxies in between changes nothing. That
  half of the determinism test is the one that catches real defects: a generator
  which is deterministic only on a fresh process is not deterministic at all.

## Generation versioning

`GalaxyGeneratorVersion::Current` sits alongside the terrain and environment
versions. **Bumping it rearranges every star in existence**, which is why it is
a named constant with that sentence next to it rather than an implicit
consequence of editing a weight.

## Caching

`FStarSystemGenerator::GetSectorStellarDensity` memoises which galaxy applies to
an intergalactic cell.

A sector scan over a light year of space asks about thousands of sectors that
all fall in one intergalactic cell, and resolving the galaxy means generating up
to twenty-seven cells' worth of candidates. The cache turns that from the
dominant cost of a scan into one lookup.

It is:

- **Thread-local**, because sector queries run on worker threads and a shared
  cache would need a lock on the hottest path in generation.
- **Bounded at sixteen entries and cleared wholesale** rather than evicted
  individually. The working set is a handful of cells, and a cache that needs an
  eviction policy at this size is more machinery than the problem deserves.
- **Invalidated on a universe re-seed**, by comparing the stored seed value.

`UniverseTest_TravelCacheBounds` walks ten thousand distinct sectors across many
intergalactic cells and asserts that the answer at the starting sector is
unchanged afterwards. A cache that grew rather than evicting would still pass
that; one that evicted incorrectly would not.

## Galaxy LOD

There is no galaxy mesh and no star catalogue. What exists at each scale:

| Player is | What a galaxy is |
|---|---|
| Inside a system | Irrelevant. The streamer's nearby systems are what matter. |
| Between stars | A density that decides which sectors have stars in them. |
| Above the disk | Still a density; the disk thins and stars stop existing. |
| Intergalactic | A position and a name, from `FindNearestGalaxy`. |

`universe.GalaxyInfo` reports which galaxy contains the player, how far out
along the disk they are, how far above it, and the stellar density there. The
HUD's GALAXY panel shows the same four numbers continuously.

## The transcendental risk

The spiral arm term uses `Atan2` and `Sin`, which puts galaxy density on the
same cross-platform footing as Sprint 001's star generation - the open libm risk
[ADR-002](../ADR/ADR-002-seed-hierarchy.md) records.

That is a deliberate choice rather than an oversight. The formulations that
avoid trigonometry do not produce spiral arms, and if cross-platform determinism
is ever required the fix is a project-owned libm, which solves star generation
and galaxy density together. It is one problem, not two.

The terrain and climate paths remain transcendental-free, and that is where it
matters most: those decide ground somebody has built on.
