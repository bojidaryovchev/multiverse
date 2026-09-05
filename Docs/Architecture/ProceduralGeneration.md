# Procedural Generation

**Status:** implemented for astronomy, Sprint 001
**Code:** `Source/UniverseCore/Public/UniverseHash.h`, `UniverseRandom.h`, `UniverseSeed.h`;
`Source/UniverseGeneration/`
**Decision record:** [ADR-002](../ADR/ADR-002-seed-hierarchy.md)

---

## 1. The premise

> We are not trying to generate the whole universe at runtime. We are creating
> mathematics and rules from which the required part of the universe can always
> be reconstructed.

Nothing untouched is ever stored. A star system exists because its address and
the universe seed imply it, and it can be discarded from memory the instant the
player leaves because regenerating it is cheap and gives back exactly what was
there. Only *player modifications* will ever need persistence, as deltas layered
on top of the generated base.

This only works if regeneration is bit-for-bit reproducible. Everything below
exists to make that true and to keep it true as many hands touch the code.

## 2. The seed hierarchy

```
Universe seed          (from a text phrase, or a raw uint64)
  -> Sector seed       (integer sector coordinates)
    -> System seed     (index within the sector)
      -> Body seed     (index within the system; 0 is the star)
        -> Surface     (cube face, quadtree level, node U/V)   [Sprint 002+]
```

Each descent is

```cpp
child = Hash(parent, DOMAIN_TAG, address...)
```

Two structural properties make this work.

**Descent is pure.** A child seed is a function of `(parent seed, domain tag,
integer address)` and nothing else. There is no traversal state, no counter, no
allocation order, no cache. That is precisely why a sector can be generated in
isolation, on any thread, in any order, having never generated its neighbours -
which is what removes the need for a database of stars.

**Every level is domain-tagged.** Without a tag, `Sector(s, 1, 0, 0)` and
`System(s, 1, 0, 0)` would be the same hash of the same numbers, and structure
at one level would be visibly mirrored at another. The tags are 64-bit ASCII
constants (`"SECTOR"`, `"SYSTEM"`, ...) so a seed appearing in a log can be
traced to the level that produced it.

The domain constants are **frozen**. Changing one regenerates the universe.

### 2.1 Sub-streams

An object that needs several independent random sequences derives one stream per
aspect rather than drawing repeatedly from a shared generator:

```cpp
FUniverseRandom Physical(Seed.Stream(UniverseSeedDomain::StreamPhysical).Value);
FUniverseRandom Orbital (Seed.Stream(UniverseSeedDomain::StreamOrbital ).Value);
```

This is the single most important habit in the file. With one shared generator,
adding a new property to a planet shifts every draw that follows it, and every
planet in the universe silently changes. With per-aspect streams, adding a
property is additive: existing values are untouched.

## 3. Hashing

`UniverseHash::Mix64` is **SplitMix64** (Steele, Lea & Flood) - a published
algorithm with published constants, chosen so that this universe is reproducible
by any correct implementation rather than only by ours.
`UniverseTest_HashStability` asserts our output against the published test
vectors (`Mix64(0) == 0xE220A8397B1DCDAF`), which catches a transposed digit in
a multiplier - an error that would otherwise still produce plausible noise and
never be noticed.

`Combine` rotates the accumulator before mixing, so argument order matters:
cell `(3, 7, 0)` must not collide with `(7, 3, 0)`.

Only unsigned 64-bit integer operations participate in a hash. Integer
arithmetic is exactly specified by the language; floating point is not. **No
float ever enters a hash.**

## 4. Random numbers

`FUniverseRandom` is **PCG-XSH-RR 64/32** (O'Neill). Deliberately not:

- `FRandomStream`, because Epic may change its algorithm between engine
  versions, and a universe that regenerates differently after an engine upgrade
  is not a persistent universe.
- `std::mt19937` with `std::uniform_real_distribution`, because the standard
  distributions are explicitly *not* required to produce identical output across
  standard library implementations.

Two details matter:

- **The seed is mixed before use.** Adjacent seeds from an index loop would
  otherwise start the LCG in adjacent states and produce visibly correlated
  first draws.
- **`NextUnit` scales by 2^-53, it does not divide.** Both the integer-to-double
  conversion and the power-of-two scaling are exact, so the mapping is
  bit-identical on every IEEE-754 platform. A division by `UINT64_MAX` rounds,
  and can return exactly 1.0 - which then indexes one past the end of a weight
  table.
- **`NextIntInclusive` uses Lemire's multiply-shift, not modulo.** Modulo biases
  toward low values when the range does not divide 2^32; across a universe of
  billions of systems that would be a visible statistical artefact.

## 5. Banned inputs

Nothing with process lifetime may influence generation:

| Banned | Why |
| --- | --- |
| Pointer values | Differ every run. |
| `UObject` unique IDs, `FName` indices | Depend on load and registration order. |
| Hash-map iteration order | Depends on insertion order and capacity. |
| Wall-clock or frame time | Obviously. |
| Thread ID, core count | Generation must be thread-agnostic. |
| Actor spawn order | The generator must not know actors exist. |
| Any previously generated value not passed explicitly | Creates hidden order dependence. |

`UniverseTest_SystemGenerationOrderIndependence` enforces the last row directly:
it generates a set of systems forwards, then backwards with unrelated
generation interleaved, and requires the results to match pairwise. A generator
that is "deterministic" only on a fresh process passes a naive generate-twice
test and fails this one.

## 6. What is generated

### Sector population

0, 1 or 2 systems with weights 0.60 / 0.33 / 0.07 - mean 0.47 per 4.87 ly
sector, giving about 0.004 stars/ly^3, matching the solar neighbourhood.

### Stars

Spectral class is drawn from an initial-mass-function-shaped table (M dwarfs
76.5%, O stars 0.00003%). A flat distribution would fill the galaxy with blue
giants and make every system the same kind of spectacular. Mass and effective
temperature follow from the class; radius and luminosity from main-sequence
power laws (`L = M^3.5`, `R = M^0.8`). Colour is a cheap blackbody fit.

The power laws are approximate, and that is fine: what matters is that
luminosity, habitable zone, frost line and orbital periods stay *mutually
consistent*, so a system reads as a coherent place.

### Planets

Count is 0-12 from a weighted table. Orbits start at a luminosity-scaled inner
edge and step outward by a per-gap ratio drawn from `[1.4, 2.3]`, so spacing
varies between systems but stays dynamically plausible.

Type is **derived, not rolled**: equilibrium temperature
`T = 278.6 K x L^0.25 / sqrt(a_AU)` and position relative to the frost line
(`2.7 AU x sqrt(L)`) decide whether a planet is molten, rocky, temperate or a
giant. That is why a dim M dwarf gets a tightly packed system with a close-in
frost line and a bright F star gets a sprawling one - the systems differ in
character, not just in colour.

Radius and bulk density come from the type; mass follows from geometry and
density; surface gravity follows from mass and radius. Deriving in that order
keeps all three consistent, which matters because a player will eventually stand
on the surface and must feel the gravity the planet's own numbers imply.

`UniverseTest_SystemPhysicalPlausibility` checks all of this across 200+
systems: orbits strictly increasing and outside the star, positive finite
masses, gravity under 200 m/s^2, giants larger than rocky worlds, and every
system inside the sector that claims it.

### Names

Generated from a syllable table, not stored. Purely cosmetic, but navigating and
reporting bugs by name rather than by raw cell coordinates is worth a great deal.

## 7. Cross-platform determinism: an open risk

**Coordinate arithmetic is exact and deterministic anywhere.** Generation is
*not yet* guaranteed to be, and this is the most significant known gap in the
architecture.

The generators call `pow`, `log`, `sqrt`, `sin` and `cos`. Of these only `sqrt`
is required by IEEE-754 to be correctly rounded; the rest are libm
implementations that may differ by an ULP between platforms, compiler versions
and optimisation settings. An ULP difference in equilibrium temperature can, at
a type boundary, flip a planet from `Ocean` to `Terrestrial`, and everything
downstream changes.

Scope of the risk today:

- **Not observable in Sprint 001.** One machine reproduces its own universe
  exactly, which is what the determinism tests verify and what single-player
  persistence requires.
- **Would be observable** for authoritative multiplayer, for cross-platform
  saves, and for any server that generates content a client also generates.

Mitigation is deliberately deferred rather than forgotten:

1. Structural decisions that *change what exists* (sector population, planet
   count, spectral class) already come from `PickWeighted` over `NextUnit`,
   which is exact integer-to-double work with no libm involved. Only *derived
   payload* values pass through libm.
2. When multiplayer approaches, the fix is to replace the transcendental calls
   in the generation path with our own fixed polynomial implementations, and add
   a cross-platform golden-hash test. That warrants its own ADR.
3. `/fp:strict` is already enforced in the standalone test build, so the
   compiler may not reassociate or contract these expressions.

This is recorded as a limitation, not as solved.

## 8. Stable identity

```cpp
struct FUniverseSystemId { int64 SectorX, SectorY, SectorZ; int32 IndexInSector; uint64 Hash; };
```

The identity **is** the address; the hash is only a fast comparison key. A GUID,
a pointer or a spawn-order counter would be stable only within one process,
whereas this is stable across runs, machines and engine versions - which is what
a persistence key and a future network identifier actually need.

`Deserialize` recomputes the hash and rejects a mismatch rather than trusting
the stored value.

Note that the identity is deliberately **the same across different universe
seeds** - it names a place, not a thing. Two universes both have a system at
sector (3, 4, 5) index 0; they simply differ in content.
`UniverseTest_SystemGenerationDifferentSeeds` asserts exactly this: identical
IDs, different content hashes.

## 9. Verification

| Claim | Test |
| --- | --- |
| Same address, same content, even after generating hundreds of others | `SystemGenerationDeterminism` |
| Different universe seeds diverge; identity is seed-independent | `SystemGenerationDifferentSeeds` |
| Generation order cannot influence results | `SystemGenerationOrderIndependence` |
| Identity is address-derived and round-trips; tampering rejected | `SystemIdStabilityAndSerialization` |
| Generated systems are physically coherent | `SystemPhysicalPlausibility` |
| Stellar density matches the solar neighbourhood | `SectorPopulationStatistics` |
| Proximity queries are order-stable and correct | `ProximityQueryDeterminism` |
| Leave, travel 500+ ly, return, find the identical system | `LeaveAndReturnReproduction` |
| SplitMix64 matches published vectors; avalanche is real | `HashStability` |
| PCG32 stream is reproducible and unbiased | `RandomStreamStability` |
| Seed descent is pure and reproducible | `SeedHierarchyDeterminism` |
| Levels cannot collide | `SeedHierarchyDomainSeparation` |
