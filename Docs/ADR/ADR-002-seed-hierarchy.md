# ADR-002: Deterministic Seed Hierarchy

- **Status:** Accepted
- **Date:** 2026-09-05
- **Sprint:** 001
- **Supersedes:** none
- **Related:** [ADR-001](ADR-001-universe-coordinate-system.md), [ProceduralGeneration.md](../Architecture/ProceduralGeneration.md)

---

## Problem

The universe must contain far more content than can ever be stored, and the
player must be able to leave a place and return years later to find it
unchanged. That means untouched content is never persisted - it is regenerated
on demand from a seed - and regeneration must be bit-for-bit reproducible.

"Reproducible" has to hold under conditions that are easy to violate by
accident: across runs, across machines, across engine upgrades, from any thread,
in any order, having generated any arbitrary set of other content first, and
after a future contributor adds a new property to a planet.

## Options considered

### 1. One global RNG seeded once, drawn from as generation proceeds

Simple and fast.

Rejected outright. Every value depends on the order and count of every preceding
draw, so generating a sector before or after its neighbour changes it, and
adding any new generated property shifts everything after it. It is not
regenerable in principle.

### 2. Hash the address directly: `value = Hash(universeSeed, x, y, z, property)`

Stateless and order-independent.

Rejected as the primary mechanism: it needs a distinct property tag for every
generated value, tags proliferate and collide, and there is no cheap way to draw
a *sequence* (twelve planets, each with a dozen properties) without inventing an
index scheme that is a seed hierarchy in disguise. Kept as the mechanism *for*
the hierarchy.

### 3. Store generated content in a database, generating each region once

Guarantees stability trivially and permits authored edits.

Rejected: billions of star systems is exactly the storage the architecture
exists to avoid, and it makes the server the source of truth for content that
should be derivable anywhere - including on a client that has never talked to
that server. Persistence is reserved for player *modifications*, as deltas over
the generated base.

### 4. Hierarchical seed derivation  **[selected]**

```
Universe -> Sector -> System -> Body -> Surface patch
child = Hash(parent, DOMAIN_TAG, address...)
```

Order-independent like option 2, but composable: a sequence is addressed by
index, and each level narrows the space naturally.

## Decision

Option 4, with:

- **`FUniverseSeed`**, a distinct type rather than a bare `uint64`, so a sector
  seed cannot be silently passed where a system seed is expected - a mistake
  that produces a plausible-looking but wrong universe and is otherwise almost
  impossible to spot.
- **Frozen 64-bit ASCII domain tags** (`"UNIVERSE"`, `"SECTOR"`, `"SYSTEM"`,
  `"BODY"`, `"SURFACE"`, `"FEATURE"`) at every level.
- **Per-aspect sub-streams** (`StreamPhysical`, `StreamOrbital`, `StreamName`,
  ...) rather than one generator per object.
- **SplitMix64** for mixing, verified against published test vectors.
- **PCG-XSH-RR 64/32** for random streams.
- **Address-derived identity**: `FUniverseSystemId` *is* the address; its hash
  is only a comparison key.

### Why domain tags

Without them, `Sector(s, 1, 0, 0)` and `System(s, 1, 0, 0)` are the same hash of
the same numbers. Structure at one level of the hierarchy would be visibly
mirrored at another, and the universe would contain correlations nobody put
there. ASCII tags rather than small integers so a seed in a log or save file can
be traced to the level that produced it.

### Why per-aspect sub-streams

This is the decision with the longest-lived consequences. With one generator per
object, a contributor who adds "atmospheric composition" to planets shifts every
draw after it and silently changes every planet in the universe - and no test
that only checks "generate twice, compare" would catch it, because it is
self-consistently wrong.

With per-aspect streams, adding a property is additive: it draws from a new
stream and existing values are untouched.

### Why not `FRandomStream` or `std::mt19937`

`FRandomStream` is an engine type whose algorithm Epic may change between
versions; a universe that regenerates differently after an engine upgrade is not
a persistent universe. The `std::` distributions are explicitly not required to
produce identical output across standard library implementations, so they cannot
be used for anything the world is reconstructed from.

SplitMix64 and PCG were chosen because they are published algorithms with
published constants - this universe is reproducible from the algorithm alone,
not only by our binary.

### Why identity is address-derived

A GUID, pointer, `FName` index or spawn-order counter is stable only within one
process. `FUniverseSystemId` is stable across runs, machines and engine
versions, which is what a persistence key and a future network identifier
actually require. Two machines that never communicate agree on what exists where.

Note the deliberate consequence: **identity is the same across different
universe seeds**. It names a place, not a thing. Two universes both have a system
at sector (3, 4, 5) index 0; they differ in content, not identity.

## Consequences

### Good

- Any region can be generated in isolation, on any thread, in any order, with no
  database and no coordination.
- Leaving and returning reproduces content exactly - asserted end to end by
  `UniverseTest_LeaveAndReturnReproduction`, which travels 500+ light years,
  generates unrelated regions, returns, and compares content hashes.
- Generation order genuinely cannot matter, asserted by
  `UniverseTest_SystemGenerationOrderIndependence`.
- Adding new generated properties does not disturb existing ones.
- The hierarchy already extends to surface patches, so Sprint 002 terrain has
  its seeding in place and tested.

### Bad / accepted costs

- **The domain tags and hash constants are frozen forever.** Changing one
  destroys and regenerates the universe. They are effectively part of the save
  format.
- **Discipline is required.** The banned-inputs list (pointers, `UObject` IDs,
  map iteration order, time) is a rule that a future contributor can break
  without the compiler objecting. The order-independence test is the guard.
- **Cross-platform determinism is not yet guaranteed.** Generators call `pow`,
  `log` and trigonometric functions, and libm is not bit-identical across
  platforms. Not observable in single player; a real problem for authoritative
  multiplayer and cross-platform saves. Structural decisions already avoid libm;
  derived payload values do not. Mitigation deferred to its own ADR - see
  [ProceduralGeneration.md section 7](../Architecture/ProceduralGeneration.md).
- **No caching.** Sprint 001 regenerates on every query so the determinism
  guarantee is exercised rather than hidden. This will need a measured cache
  before it is a shipping proposition.

### Neutral

- Hash cost is a handful of multiplies and shifts per level, negligible next to
  the work of building a system.

## Revisit if

- Authoritative multiplayer arrives - the libm question must be resolved first.
- Profiling shows regeneration cost dominates, at which point a cache layered
  *above* the generator (never inside it) is the answer.
- A generated property needs to depend on its neighbours (plate tectonics across
  patch boundaries, say), which pure descent cannot express and which needs its
  own decision record.
