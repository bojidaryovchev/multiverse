# ADR-001: Universe Coordinate System

- **Status:** Accepted
- **Date:** 2026-09-05
- **Sprint:** 001
- **Supersedes:** none
- **Related:** [ADR-002](ADR-002-seed-hierarchy.md), [UniverseCoordinates.md](../Architecture/UniverseCoordinates.md)

---

## Problem

The game must let a player travel continuously from a planet's surface to
another galaxy, and must retain sub-millimetre local precision at every point
along the way. Unreal's coordinate space cannot represent that range, and a
single double per axis cannot either: at galactic distance a double's resolution
is tens of metres, so walking, landing and building all become impossible.

The failure mode is what makes this urgent. Everything works near the origin and
degrades with distance, so the defect is invisible until a great deal of content
has been built on top of it.

We need a canonical position representation that is deterministic, serialisable,
network-friendly, supports astronomical distances and arbitrary velocity, and
does not degrade with distance from the origin.

## Options considered

### 1. Single `FVector3d` (double per axis)

Simplest, and directly usable as an Unreal transform.

Rejected: resolution is `M x 2^-52`. At 100,000 light years that is 21 metres;
at Andromeda, 525 metres. It cannot represent a person standing on a planet in
another galaxy. Position also becomes path-dependent, since every arithmetic
operation rounds relative to the magnitude.

### 2. 128-bit fixed point per axis

Uniform resolution everywhere, exact arithmetic, no normalisation step.

Rejected: no native 128-bit integer on MSVC x64, so every operation becomes a
software routine; it does not interoperate with Unreal's double-based transforms
without conversion anyway; and it fixes resolution and range against each other
at compile time with no way to trade them. The integer-cell scheme gets the same
distance-independence with hardware-native types.

### 3. Nested/relative coordinates (position relative to a parent body)

Positions expressed relative to a planet, which is relative to a star, and so on.
Excellent local precision and a natural fit for orbital mechanics.

Rejected as the *canonical* form: comparing two positions requires walking both
to a common ancestor, so distance is a graph traversal rather than arithmetic;
identity depends on the hierarchy, so re-parenting changes a position's
representation; and a ship in interstellar space has no meaningful parent. It
remains a good *derived* form for orbital simulation later, layered on top.

### 4. Integer cell index + double local offset  **[selected]**

```cpp
struct FUniversePosition { int64 CellX, CellY, CellZ; FVector3d Local; };
```

Large magnitude lives in the integer part, where it is free and exact. The
double only ever holds a value inside one cell, so precision is constant
everywhere. Hardware-native types throughout, and the local part hands straight
to an Unreal transform once made relative to a nearby origin.

## Decision

Option 4, with:

- **`CELL_SIZE = 2^40 cm`** (1,099,511,627,776 cm; 1.0995e10 m; ~11 million km;
  0.0735 AU)
- **`int64` cell indices**
- **Canonical form:** `Local` normalised to `[0, CELL_SIZE)`, floor semantics
- **Sector grid:** `2^22` cells = 4.8746 light years, for star placement
- **Render rebase radius:** 1e6 cm (10 km), independent of cell size

### Why 2^40, and why a power of two

Range and precision are both enormously generous at this size:

- Universe half-extent: `2^63 x 2^40 cm = 2^103 cm = 1.07e13 light years` per
  axis - about 230x the radius of the observable universe.
- Worst-case local resolution: `2^40 x 2^-52 = 2^-12 cm = 2.44 micrometres`,
  everywhere.

The power of two is the decision that actually matters, and it is not about
speed. Normalisation runs on essentially every position update. With a power-of-
two cell size it is provably **exact** for displacements up to `2^53 cm`:
multiplying by `2^-40` only adjusts the exponent, `floor` of an exact value is
exact, and the subtraction that follows is exact in that range.

With a decimal cell size such as 1e9 cm, every normalisation would round, and a
position would depend on the route taken to reach it rather than on where it is.
Two players arriving at the same point by different paths would disagree about
where they are, and saved games would not reload identically. Determinism would
be gone at the foundation.

Floor rather than truncation, for the same class of reason: truncation toward
zero would make cells -1 and 0 overlap and put a seam at the universe origin.

### Why the rebase radius is a separate knob

Cell size is bounded by **double** precision and defines the logical universe.
The rebase radius is bounded by **float** precision in the rendering and physics
paths and defines render error. Conflating them - rebasing once per cell - would
put actors 11 million km from the Unreal origin and the picture would fall apart.

## Consequences

### Good

- Precision is genuinely distance-independent. A millimetre step is resolvable
  ten billion light years out, which an automated test asserts.
- Coordinate arithmetic is integer or IEEE-754 exact, so it is deterministic on
  any platform.
- Canonical form makes equality, hashing and serialisation well defined, and
  makes the 48-byte wire format trivial.
- Cell indices are a natural sharding key for future authoritative servers.
- Rebasing is invisible: everything anchored shifts by the same delta in the
  same frame.

### Bad / accepted costs

- **A 2.44 um resolution floor.** Displacements below it are absorbed rather
  than accumulated. Any future sub-micrometre simulation must integrate in a
  local frame and commit periodically.
- **`OffsetByCm` is only exact to 2^53 cm** (6,013 AU). Larger jumps must use
  `TryOffsetByCells`. The API makes this explicit rather than silent, and the
  probe's speed cap keeps a frame's displacement three orders of magnitude
  inside the limit.
- **Two conversion paths to maintain** (universe to render, render to universe),
  and a rule that anchored actors must never be moved by setting their Unreal
  location.
- **Relative vectors can be refused.** Callers must handle `TryGetRelativeCm`
  returning false. This is deliberate: an actor that cannot be placed is hidden
  rather than drawn at a fabricated location.
- **Positions saturate at the universe edge rather than wrapping**, which is a
  silent clamp. Acceptable at 1.07e13 ly.

### Neutral

- Cell size and sector size are frozen constants. Changing either regenerates
  the universe, so they are effectively part of the save format.

## Revisit if

- Sub-micrometre simulation becomes a requirement.
- Profiling shows normalisation is hot enough to matter (it is a handful of
  instructions today).
- A platform without a fast 64-bit integer becomes a target.
