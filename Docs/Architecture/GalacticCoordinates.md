# Galactic Coordinates

*Sprint 006. Source: `Source/UniverseGeneration/Public/GalaxyDescriptor.h`,
`Source/UniverseCore/Public/UniverseCoordinates.h`.*

## One canonical system, several views

There is exactly one canonical position type in this project and it did not
change in Sprint 006. `FUniversePosition` - an int64 cell index per axis plus a
double local offset in centimetres - remains the answer to "where is this", from
a grain of sand on a planet to a galaxy on the far side of the universe.

Everything in this document is a **view** of that. A view is a lossy, convenient
re-expression for one consumer. It is never stored as authority, never round
tripped through, and never compared for equality.

```text
FUniversePosition          canonical, exact, 2.44 um resolution everywhere
  |
  +-- sector address       int64 triple; the unit star generation works in
  +-- intergalactic cell   int64 triple; the unit galaxy generation works in
  +-- galaxy-local         light years; radius, height, angle in a galaxy
  +-- planet-local metres  doubles relative to a planet centre
  +-- render location      float cm relative to the render origin
```

## The address hierarchy

Each level is the one above, shifted. All shifts are powers of two so the
division is an arithmetic shift and therefore exact - a position never lands in
the wrong bucket because of a rounding error at a boundary.

| Level | Shift | Size | Constant |
|---|---|---|---|
| Cell | - | 2^40 cm ≈ 0.0735 AU | `UniverseScale::CellShift` |
| Sector | 2^12 cells | 4.87 ly | `UniverseScale::SectorShiftInCells` |
| Intergalactic cell | 2^40 cells | 1.28 million ly | `FGalaxyGenerator::CellShiftInCells` |

An intergalactic cell is about half the distance to Andromeda. There are on the
order of 10^21 of them within the representable universe, and one containing no
galaxy costs nothing at all.

```cpp
// The three addresses of a position, all exact:
Position.GetSector(SectorX, SectorY, SectorZ);
FGalaxyGenerator::GetCellCoordinates(Position, CellX, CellY, CellZ);
const FUniverseSectorCoord Sector = FUniverseSectorCoord::FromPosition(Position);
```

## Galaxy-local coordinates

`FGalaxyLocalPosition` is the cylindrical view a galaxy wants:

| Field | Meaning |
|---|---|
| `RadiusLightYears` | Distance from the centre **in the disk plane**. |
| `HeightLightYears` | Signed distance above the disk plane. |
| `AngleRadians` | Around the disk, from the galaxy's right vector. |
| `OffsetLightYears` | The full offset in universe axes, in light years. |

`ToGalaxyLocal` and `FromGalaxyLocal` convert both ways.

### Why this is in cell space

`FUniversePosition::TryGetRelativeCm` returns a vector in centimetres and is
exact only out to about **0.01 light years**. That is not a limitation to work
around; it is the reason the coordinate type exists in the split form it does. A
double holds 2^53 centimetres exactly, which is 0.0095 ly, and beyond that the
integer cell index carries the magnitude.

So a galaxy-scale relative vector cannot go through centimetres at all.
`GetRelativeCells` takes the difference in cell space, where the numbers are
around 10^10 rather than 10^22, and keeps full relative precision at any
separation.

The first implementation of `ToGalaxyLocal` used `TryGetRelativeCm`, which
failed on every call and returned the documented "astronomically far" fallback.
The symptom was that stellar density was zero *everywhere*, including at the
centre of a galaxy, so the whole universe was empty. Nothing in the code looked
wrong; the number that came out was simply the failure value.

The reverse direction has the mirror problem. An offset of 25,000 ly is
2.4 x 10^22 cm, where a double's ulp is about 4 x 10^6 cm - forty kilometres.
`FromGalaxyLocal` therefore splits into whole cells plus a remainder small
enough to stay exact.

### What light years cost

| Scale | One ulp of a double, in light years | In metres |
|---|---|---|
| 1 ly | 2.2e-16 | 0.002 |
| 1,000 ly | 2.3e-13 | 2.2 |
| 50,000 ly | 7.3e-12 | 69,000 |

Light years are a coarse unit and a double does not rescue them. At galactic
radius the representable resolution is tens of kilometres.

This is fine, because nothing that needs better precision uses this view. A
density function over a structure 50,000 ly across does not care about a
kilometre. A player standing on a mountain uses `FUniversePosition`, which
resolves 2.44 micrometres there and everywhere else.

`UniverseTest_GalaxyLocalRoundTrip` therefore asserts a **relative** tolerance -
a few ulps at the scale being measured - rather than an absolute one. An
absolute tolerance would be either meaninglessly loose at the centre or
impossible at the rim.

## Sector traversal

`FTravelBroadPhase::GetSectorsCrossed` answers "which sectors does this path
pass through", using a 3D digital differential analyser - the same algorithm a
voxel raycast uses - over the sector grid.

It works in **fractional sectors relative to the start sector's corner**, so the
numbers are small and a double is exact enough for a traversal that only ever
needs to know which side of a boundary it is on.

It visits exactly the sectors the segment enters, in order, one axis step at a
time. `UniverseTest_TravelSectorTraversal` asserts that consecutive sectors
differ by exactly one in exactly one axis: a jump of two means a boundary was
skipped, which is the failure that lets a ship through a star.

It is bounded by `MaxSectors`, and a truncated result is a **correct prefix** of
the true answer rather than an approximation of it - which is what makes a
partial result safe for a caller to substep from.

## What is never done

- **Never store a view.** A galaxy-local position saved to disk would be wrong
  the moment the galaxy generator version changed.
- **Never compare views for equality.** Two positions that round to the same
  light-year offset are up to seventy kilometres apart.
- **Never convert canonical -> view -> canonical** as a way of moving. Every
  such round trip loses precision; offsetting the canonical position does not.
- **Never assume the universe has a preferred axis.** There is no galactic
  "north". The disk normal is a property of a galaxy, drawn per galaxy, and the
  `+Z` fallback in `GetLocalUp` is a fallback for code that must have *some*
  basis to build a rotation from, not a claim about the universe.

## Related

- [UniverseCoordinates.md](UniverseCoordinates.md) - the canonical type
- [GalaxyGeneration.md](GalaxyGeneration.md) - what lives at galaxy scale
- [InterstellarTravel.md](InterstellarTravel.md) - moving through it
- [ADR-001](../ADR/ADR-001-universe-coordinate-system.md) - why cells and offsets
