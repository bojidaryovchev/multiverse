# Universe Scale Rendering

*Sprint 006, consolidating Sprints 001-005. Source:
`Source/Universe/Public/UniverseRenderSpace.h`,
`Source/Universe/Private/UniverseWorldSubsystem.cpp`,
`Source/Universe/Public/AstronomicalBodyActor.h`.*

## The one rule

**Visual scale never becomes universe scale.**

Every rendering decision in this project is a lossy re-expression of a canonical
value for the benefit of a camera. None of them ever flows back. A body's
rendered radius, its rendered position, the scale factor applied to a whole
render space - all of them are derived, every frame, from values that do not
know rendering exists.

This is not fastidiousness. The alternative fails in a specific and unrecoverable
way: once a planet's *simulated* size has been adjusted so it looks right from
orbit, gravity is wrong, the horizon is wrong, the terrain quadtree is wrong, and
there is no longer any single answer to how big the planet is.

## Canonical coordinates

`FUniversePosition` - int64 cell per axis plus a double local offset in
centimetres. 2.44 micrometre resolution anywhere in a universe about 10^10 light
years across. See
[UniverseCoordinates.md](UniverseCoordinates.md).

Nothing renders from this directly. Unreal's transforms are single-precision
floats, which lose millimetre resolution at around 10 km and metre resolution at
around 10,000 km.

## The render origin

The universe position that Unreal's `(0,0,0)` currently represents.

When the tracked viewpoint drifts past the rebase radius, the origin moves and
**every anchored actor is shifted by the same delta in the same frame**. The
whole scene moves together, so no relative geometry changes and the camera sees
nothing happen.

Rebasing is triggered at the point of movement rather than on a subsystem tick.
At the probe's 10^12 m/s sublight cap one frame covers 3.3 x 10^12 cm, so if the
correction waited for a separate tick the actor transform would sit thousands of
times beyond the rebase radius for part of every frame, and whether anything
observed it would come down to tick ordering. Correcting where the movement
happens makes the bound hold structurally.

A typical planetary journey rebases about a thousand times. That number is on
the journey report precisely because it is invisible otherwise.

## Render spaces

```cpp
enum class EUniverseRenderSpace
{
    Local,          // 1:1 with reality. Anything the player can touch.
    ScaledAstronomy // Uniformly scaled down. Stars and distant planets.
};
```

`Local` is metres, unscaled, relative to the render origin. Terrain, characters,
structures, vegetation, the ship - anything with collision or contact - lives
here, at true scale.

`ScaledAstronomy` is a **uniform** scale applied to both position and radius.
Uniform is the whole point: a uniform scale preserves angles, so a body subtends
exactly the angle it would in reality. What you see is what is there, viewed
through a lens that shortens distances.

`TryGetRenderLocation` returns false when a position is too far from the origin
to express as a finite offset. Callers must handle that by **hiding the actor**,
never by clamping. An object drawn at a made-up location is worse than one that
is absent, because it looks like data.

## Visual proxies

`AAstronomicalBodyActor` is the placeholder for a star or a planet: an engine
sphere primitive, scaled through the astronomy factor, tinted from the
descriptor's colour.

It is a **pure view**. It holds a copy of the descriptor it was built from and
derives everything from those logical values. It never edits the descriptor and
never becomes the source of truth, so deleting every one of them loses nothing -
they can be rebuilt from the seed at any time.

Stars carry two lights, which is a Sprint 004 finding worth keeping written
down. A single directional light doing both jobs rendered the ground pure black,
because the atmosphere's sun-light transmittance is evaluated in single
precision and collapses at planetary radii. The fix was to split it:

- `StarLight` lights the world, with `bAtmosphereSunLight = false`.
- `AtmosphereSunLight` scatters the sky, with every lighting channel off.

## Active systems

Only one system is ever fully simulated - see
[StarSystemStreaming.md](StarSystemStreaming.md). What that means for rendering:

| Streaming state | What is drawn |
|---|---|
| `DescriptorOnly` | Nothing. |
| `DistantVisual` | One scaled sphere for the star, emissive, with a light. |
| `NearbyVisual` | Plus one scaled sphere per planet. |
| `Active` | Plus one real cube-sphere planet with streamed terrain patches. |

The placeholder for the promoted planet is destroyed rather than hidden. Two
representations of one body is where render/simulation confusion starts.

## Galaxy rendering

There is none, and that is deliberate.

A galaxy in this project is a density function, not a list of stars
([GalaxyGeneration.md](GalaxyGeneration.md)). Rendering one as geometry would
mean inventing a representation that no simulation reads - the exact thing this
document exists to forbid.

What exists instead is the star field: the systems the streamer is tracking,
drawn as the points of light they are. Fly toward one and it becomes a star,
then a system, then a world, continuously, because it was always there.

`universe.GalaxyInfo` and the HUD's GALAXY panel report the player's galactic
radius, height above the disk and local stellar density as *numbers*. That is an
honest presentation of a density function; a painted spiral would not be.

## Camera-relative representation

The camera is at the render origin, or near it. Everything drawn is expressed
relative to that, in floats, within the rebase radius.

This is what makes single-precision rendering safe at astronomical scale: the
float never holds a large number. The magnitude lives in the int64 cell index,
which is exact; the float only ever holds the small remainder.

## Precision strategy

The layered answer, from largest scale to smallest:

| Layer | Type | Range | Resolution |
|---|---|---|---|
| Universe | int64 cells | ±10^10 ly | one cell (0.0735 AU) |
| Within a cell | double cm | 2^40 cm | 2.44 µm |
| Galaxy-local view | double ly | 90,000 ly | ~70 km at the rim |
| Planet-local | double m | planet radius | sub-micrometre |
| Render | float cm | rebase radius | sub-millimetre |

Each layer is chosen so the number it holds is small relative to what the type
can represent. Nothing anywhere holds a large number in a float.

Three specific traps, all of which have actually bitten and all of which are now
guarded by a test:

- **`TryGetRelativeCm` is exact only to about 0.01 light years.** Galaxy-scale
  relative vectors must go through cell space. See
  [GalacticCoordinates.md](GalacticCoordinates.md).
- **Light years in a double resolve to tens of kilometres at galactic radius.**
  Fine for a density function, useless for anything you can stand on.
- **The naive quadratic root formula loses everything at interstellar range.**
  `FUniverseSweep` uses the stable form; `b²` is around 10^32 while `4ac` is
  around 10^18.

## Exposure

Camera exposure is calibrated to the dominant light source rather than to a
fixed value, in `UpdateCameraExposure`, every frame and in every frame of
reference - not only in the planetary one, which was a Sprint 003 defect.

`DepthOfFieldFstop` is left at its default deliberately: overriding it silently
enables physical depth of field, and the shutter then carries all the exposure.

## Related

- [UniverseCoordinates.md](UniverseCoordinates.md) - the canonical type
- [GalacticCoordinates.md](GalacticCoordinates.md) - the views of it
- [PlanetLOD.md](PlanetLOD.md) - terrain patch selection
- [StarSystemStreaming.md](StarSystemStreaming.md) - what exists at all
- [ADR-001](../ADR/ADR-001-universe-coordinate-system.md)
