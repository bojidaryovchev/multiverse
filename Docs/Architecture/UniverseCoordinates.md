# Universe Coordinates

**Status:** implemented, Sprint 001
**Code:** `Source/UniverseCore/Public/UniverseScale.h`, `UniverseCoordinates.h`, `Private/UniverseCoordinates.cpp`
**Decision record:** [ADR-001](../ADR/ADR-001-universe-coordinate-system.md)

---

## 1. The problem

Unreal is not the universe. Unreal renders and simulates the small volume the
player currently occupies; the canonical universe is ours and is vastly larger
than any coordinate space the engine can hold.

A single double per axis cannot do the job. A double has a 53-bit mantissa, so
the spacing between representable values at magnitude `M` is approximately
`M x 2^-52`:

| Distance from origin | Double resolution there |
| --- | --- |
| 1 km (1e5 cm) | 0.02 nanometres |
| 1 AU (1.5e13 cm) | 3.3 micrometres |
| 1 light year (9.5e17 cm) | 0.2 millimetres |
| 1,000 light years | 21 centimetres |
| 100,000 ly (galaxy) | 21 metres |
| 2.5 million ly (Andromeda) | 525 metres |

By the time the player has crossed our own galaxy, a single double cannot tell
two points 20 metres apart from each other. Walking, landing and building all
become impossible - not gradually, but as a hard wall. Worse, the failure is
positional: the game works near the origin and breaks far from it, which is the
hardest kind of bug to find and the easiest kind to build a year of content on
top of before noticing.

## 2. The representation

```cpp
struct FUniversePosition
{
    int64     CellX, CellY, CellZ;   // which cell
    FVector3d Local;                 // centimetres inside that cell
};
```

with the invariant

```
absolute_cm = Cell * CELL_SIZE + Local        Local in [0, CELL_SIZE)
```

The large magnitude lives entirely in the integer part, where it costs nothing,
and the double only ever holds a value inside one cell. **Precision therefore
does not depend on distance from the universe origin.** A position ten billion
light years out has exactly the same local resolution as one at the origin.

## 3. Choosing the cell size

Two constraints pull in opposite directions:

- A **larger** cell means a larger universe (`2^63 cells x cell size`).
- A **smaller** cell means finer local resolution (`cell size x 2^-52`).

Both are so generous that the choice is comfortable rather than tight:

| Cell size | Universe half-extent per axis | Worst-case local resolution |
| --- | --- | --- |
| 2^30 cm (10,737 km) | 10.5 Gly | 2.4 nanometres |
| 2^35 cm (343,597 km) | 335 Gly | 78 nanometres |
| **2^40 cm (11.0 million km)** | **1.07e13 ly** | **2.44 micrometres** |
| 2^45 cm (352 million km) | 3.4e14 ly | 78 micrometres |
| 2^50 cm (11.3 billion km) | 1.1e16 ly | 2.5 millimetres |

**Selected: `CELL_SIZE = 2^40 cm = 1,099,511,627,776 cm`**
(1.0995e10 m, about 11 million km, or 0.0735 AU).

That gives:

- **Universe half-extent: 2^103 cm = 1.014e29 m = 1.07e13 light years per axis.**
  The observable universe has a radius of about 4.65e10 ly, so the addressable
  space is roughly 230x that radius in every direction. The logical universe
  will never be the limiting factor for anything this project builds.
- **Worst-case local resolution: 2^40 x 2^-52 = 2^-12 cm = 2.44 micrometres**,
  everywhere. That is four orders of magnitude finer than anything gameplay,
  physics or rendering needs.

A cell is also a physically sensible size: at 0.07 AU it is smaller than any
planetary orbit, so a star system spans thousands of cells and cells remain a
useful spatial index rather than a coarse bucket.

### 3.1 Why a power of two, specifically

This is the load-bearing decision, and it is not about speed.

Normalisation - folding an out-of-range `Local` back into `[0, CELL_SIZE)` and
carrying the excess into the cell index - happens on essentially every position
update in the project. If that operation loses precision, then a position
depends on the *path taken to reach it* rather than on where it is. Two players
arriving at the same point by different routes would disagree about where they
are, saved games would not reload identically, and determinism would be gone.

With `CELL_SIZE = 2^40`, normalisation is provably **exact**:

1. `Local * 2^-40` is exact. Multiplying by a power of two only adjusts the
   exponent field; not one mantissa bit is lost.
2. `floor()` of an exact value is exact.
3. `Carry * 2^40` is exact, for the same reason as (1).
4. `Local - Carry * 2^40` is exact for `|Local| <= 2^53`, because both operands
   are then integers below 2^53 scaled by a common power of two.

So normalisation is a lossless re-encoding of the same point. With a decimal
cell size such as 1e9 cm, step 1 alone would round, and every position in the
game would be quietly path-dependent.

The exactness bound of `2^53 cm` (about 9.0e15 cm, or 6,013 AU) is the largest
single displacement that can be applied through `OffsetByCm`. For context, a
frame at 30 Hz travelling at the probe's speed cap of 1e12 m/s covers 3.3e10 m
- three orders of magnitude inside the limit. Anything larger goes through
`TryOffsetByCells`, which is pure integer arithmetic and exact at any distance.

### 3.2 Canonical form, and why `[0, CELL_SIZE)`

`Local` is always normalised into the half-open range `[0, CELL_SIZE)`, so every
point in the universe has exactly one representation. That uniqueness is what
makes equality, hashing and byte-level serialisation well defined. `operator==`
is a bit-exact comparison of a canonical value, not a "close enough" spatial
test; use `DistanceMeters` with a tolerance when proximity is what you mean.

Normalisation uses **floor** semantics, not truncation. Cell -1 covers
`[-2^40, 0)`, so cells tile the axis with no gap and no double-width cell
straddling the origin. Truncation toward zero - what C++ integer division and a
naive `(int64)(x / size)` both do - would put cells -1 and 0 partly on top of
each other and produce a visible seam at the universe origin. The same applies
to sector indices, which is why `FloorDivPow2` uses an arithmetic shift.

## 4. Sectors

Star placement uses a coarser grid layered on top of cells:

```
SECTOR = 2^22 cells = 2^62 cm = 4.8746 light years on a side
```

Sector coordinates are `CellX >> 22` (arithmetic, so it floors correctly for
negative cells).

The size is chosen from stellar density rather than convenience. The solar
neighbourhood holds roughly 0.004 stars per cubic light year; a sector of
4.8746 ly encloses 115.8 ly^3, giving an expectation of about 0.46 stars. The
generator's population distribution (0 systems 60%, 1 system 33%, 2 systems 7%)
averages 0.47, so a sector naturally holds zero or one system and star lookups
stay cheap. `UniverseTest_SectorPopulationStatistics` asserts the resulting
density lands between 0.002 and 0.008 stars/ly^3.

## 5. Operations

| Operation | Guarantee |
| --- | --- |
| `OffsetByCm(delta)` | Exact for \|delta\| <= 2^53 cm. Crosses cell boundaries automatically. |
| `OffsetByMeters(delta)` | As above, converted at 100 cm/m. |
| `TryOffsetByCells(dx,dy,dz)` | Pure integer. Exact at any magnitude. Returns false on int64 overflow rather than wrapping. |
| `TryGetRelativeCm(a,b,out)` | Exact cm vector, or false if the separation exceeds 2^53 cm. |
| `GetRelativeCells(a,b)` | Fractional cell vector. Always representable. |
| `DistanceCm/Meters/Au/LightYears` | Computed in cell units and scaled last. Never overflows. |
| `Serialize/Deserialize` | 48 bytes, fixed little-endian layout, bit-exact round trip. |
| `GetStableHash64` | Deterministic across runs, builds and machines. |

### 5.1 Distance is computed in cell space

`DistanceCm` converts to fractional cells, takes the magnitude, and scales to
centimetres at the very end. This is not a micro-optimisation - it is a
correctness requirement.

One light year is 9.46e17 cm. Squaring that gives 9e35, and summing three such
terms across galactic distances runs a double out of exponent range: a naive
`sqrt(dx*dx + dy*dy + dz*dz)` in centimetres returns infinity. In cell units the
same separation is only 8.6e5, so intermediates never grow.
`UniverseTest_RelativeAndDistance` measures a corner-to-corner span of the
universe and asserts the result is finite.

### 5.2 Out-of-range relative vectors are refused, not clamped

`TryGetRelativeCm` returns `false` beyond 2^53 cm - the point at which a
double's ULP reaches one centimetre and a centimetre-denominated vector stops
carrying centimetre meaning. Callers must handle the refusal.
`UUniverseAnchorComponent` hides its actor when this happens rather than
placing it at a fabricated location: a missing actor is far easier to diagnose
than a wrong one, and at these scales "somewhere plausible" has no meaning.

## 6. Serialisation

The wire format is fixed, explicit and independent of struct layout:

```
int64  CellX      (little-endian, two's complement)
int64  CellY
int64  CellZ
double Local.X    (IEEE-754 binary64, raw bits, little-endian)
double Local.Y
double Local.Z
                  = 48 bytes
```

`FArchive` support is implemented *on top of* this format rather than beside
it, so the engine path and the future network/persistence path cannot diverge.

Doubles are transported as raw bits, not text: a decimal round trip loses signed
zero and denormals, and this data has to reload bit-identically.

`Deserialize` **rejects** a non-canonical local offset. `Serialize` only ever
emits canonical values, so a non-canonical one arriving means a corrupt file or
a crafted packet; normalising it silently would accept the tampered value.

## 7. Rebasing: universe space to Unreal space

The canonical position never reaches an Actor transform directly. The
`UUniverseWorldSubsystem` holds a **render origin** - the universe position that
Unreal's `(0,0,0)` currently represents - and every anchored actor's transform is
computed relative to it.

When the tracked viewpoint drifts more than `RebaseRadiusCm` (default 1e6 cm =
10 km) from the Unreal origin, the origin moves to the viewpoint and every
anchored actor is shifted by the same delta in the same frame. Because
everything moves together and the camera is on the tracked actor, nothing
changes relative to the viewer and **the rebase is invisible**.

The rebase radius and the cell size are independent knobs and must stay that
way:

- Cell size bounds the **logical universe** and is set by double precision.
- Rebase radius bounds **render error** and is set by *float* precision in the
  rendering and physics paths. At 1e6 cm a float ULP is about 0.06 cm, safely
  under visible vertex jitter and Z-fighting.

Conflating the two - rebasing once per cell, say - would put actors up to 11
million km from the Unreal origin and the picture would fall apart.

The render origin **translates but never rotates**. Universe axes and Unreal
axes are therefore the same basis, which is what makes converting a direction
vector between them a plain copy rather than a frame transform.

## 8. Velocity integration

Velocity is a `FVector3d` in **metres per second**, held on the pawn, in
universe axes. Each frame:

```
velocity += acceleration * dt
position  = position.OffsetByMeters(velocity * dt)
```

The Unreal transform is then recomputed from the position. The ordering matters:
position is integrated in canonical space and the transform follows, never the
reverse. A pawn that accumulated position in an `FVector` would drift into
enormous coordinates and start juddering; here the Unreal location is bounded by
the rebase radius no matter how far the probe travels.

Speed is capped at 1e12 m/s so a frame's displacement stays inside the exact
normalisation range (section 3.1). Travel faster than that uses `WarpJump`,
which moves in whole cells.

Movement is kinematic, not Chaos-driven. At these speeds discrete rigid-body
integration would tunnel straight through anything in its path; sweep and
trajectory-intersection mathematics replace it when collision arrives.

## 9. Known limits

| Limit | Value | Consequence |
| --- | --- | --- |
| Universe half-extent | 1.07e13 ly per axis | Positions saturate rather than wrap at the edge. |
| Local resolution | 2.44 um | Displacements below this are absorbed, not accumulated. |
| Exact `OffsetByCm` | 2^53 cm (6,013 AU) | Larger jumps must use `TryOffsetByCells`. |
| Relative cm vector | 2^53 cm | Beyond this, use cell-space deltas or distance. |
| Render placement | 8e12 cm from origin | Actors beyond are hidden. |
| Sector index | `Sector * 2^22` must fit int64 | Sectors are limited to +/-2^41. |

The 2.44 um resolution floor deserves emphasis: an object cannot accumulate
motion in steps smaller than that. Any future sub-micrometre simulation must
integrate in a local frame and commit to the universe position periodically,
rather than writing every step through `OffsetByCm`.
`UniverseTest_CellBoundaryExact` pins this behaviour so it cannot change
unnoticed.

## 10. Network implications

Nothing here is decided yet, but the representation was chosen not to foreclose
anything:

- The 48-byte format is already a wire format, and is far smaller than the
  text or `FArchive` alternatives.
- Cell indices are a natural sharding key. A server can own a range of sectors,
  and authority transfer at a sector boundary is an integer comparison.
- All identity is address-derived, so two machines that never communicate agree
  on what exists where.
- Cell and local can be quantised independently for replication: distant
  objects need cell precision only, nearby ones need the full local offset.

The one real constraint is section 11.

## 11. Cross-platform determinism: an open risk

Coordinate arithmetic is integer and IEEE-754 exact, so it is deterministic
anywhere. **Generation is not fully guaranteed to be**, because the generators
call `pow`, `log`, `sqrt` and the trigonometric functions, and libm is not
required to be bit-identical between platforms or compiler versions.

For single-player Sprint 001 this is not observable: one machine reproduces its
own universe exactly, which the determinism tests verify. It becomes a real
problem for authoritative multiplayer and for cross-platform saves. See
[ProceduralGeneration.md section 7](ProceduralGeneration.md) for the analysis
and the intended mitigation.

## 12. Verification

Every claim above is asserted by an automated test that is actually executed -
see [Testing.md](Testing.md).

| Claim | Test |
| --- | --- |
| Constants are as documented; cell size is a power of two | `ScaleConstants` |
| Positive normalisation and boundary roll-over | `NormalizationBasic` |
| Floor semantics across the origin | `NormalizationNegative` |
| Exact cell edges and either side of them | `CellBoundaryExact` |
| No gap or overlap at a boundary | `CellBoundaryNeighbourhood` |
| 100,000-step journey, exact return | `LargeDisplacementAccumulation` |
| 1 mm step resolvable at 1e10 ly | `LocalPrecisionAtExtremeCoordinates` |
| Distance finite across the whole universe | `RelativeAndDistance` |
| Cell jumps exact; overflow refused | `CellOffsetJumps` |
| Sector floor division across the origin | `SectorAddressing` |
| 48-byte bit-exact round trip; bad input rejected | `PositionSerializationRoundTrip` |
| Different routes to a point compare and hash equal | `PositionEqualityAndHash` |
