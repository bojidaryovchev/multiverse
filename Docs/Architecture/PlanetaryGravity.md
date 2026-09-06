# Planetary Gravity

There is no world "down" in this project, and there cannot be one.

Related: [SimulationFrames.md](SimulationFrames.md),
[PlanetaryTraversal.md](PlanetaryTraversal.md),
[ADR-004](../ADR/ADR-004-simulation-frames-and-planetary-traversal.md).

---

## The rule

```
direction = normalize(planetCentre - objectPosition)
magnitude = g_surface * (R / r)^2        outside the body
            g_surface * (r / R)          inside it
```

Two players on opposite sides of a world have exactly opposite up vectors and
both are right. Any code that reaches for a fixed `-Z` is asserting that the
universe has a preferred axis, and the failure mode is not a compile error — it
is a player walking over the horizon and falling off the world. So the direction
is computed from geometry at every query, and `PlanetGravity.h` is the only
place a gravity vector is produced.

---

## Why the law changes inside the body

Outside, Newton's shell theorem makes a uniform sphere behave exactly like a
point mass at its centre, so `g(r) = g_surface · (R/r)²`. That is the physically
right answer and it is what an orbiting craft needs: orbits only close if the
falloff is correct.

Inside, the same theorem gives `g(r) = g_surface · (r/R)`. Only the mass beneath
you pulls, so gravity falls **linearly** to exactly zero at the centre.

Extending the inverse square inward instead would send the magnitude to infinity
at `r = 0`, and something will eventually end up there: a debug teleport, a badly
clamped spawn, a projectile that tunnelled. A singularity inside the playable
volume is a crash waiting for a reason. The linear form is both the correct
physics and the one that cannot blow up.

At exactly `r = R` the two branches agree to the last bit, because the outside
branch is written as a squared ratio rather than as `mu / r²` — the two forms
differ by rounding, and a discontinuity precisely at the surface is the one place
it would be noticed.

---

## Where the numbers come from

`FPlanetDescriptor::SurfaceGravityMs2` is derived from mass and radius during
star system generation, and is carried across to
`FPlanetSurfaceDescriptor::SurfaceGravityMs2` unchanged. Recomputing it from the
same inputs would be a second implementation of one fact, and the two would
drift.

Everything else is derived from that and the radius:

```cpp
FPlanetGravityField Field = FPlanetGravityField::FromPlanet(Planet);

Field.GetAccelerationMs2(PlanetLocalMeters);   // m/s^2, vector
Field.GetMagnitudeMs2(DistanceFromCentre);     // m/s^2, scalar
Field.GetEscapeVelocityMs();                   // sqrt(2 g R)
Field.GetCircularOrbitSpeedMs(r);              // sqrt(mu / r), 0 inside the body
Field.GetGravitationalParameter();             // mu = g R^2
```

Gravity is excluded from `FPlanetSurfaceDescriptor::GetContentHash`, because it
changes how a planet behaves rather than where its ground is. Folding it in would
invalidate every cached patch the first time a gravity model was tuned.

---

## Up is not the terrain normal

`FPlanetSurfaceSample` carries both, and the difference matters.

| | Defined by | Use it for |
| --- | --- | --- |
| `UpUnit` | position alone — away from the centre | standing, gravity, altitude, orientation |
| `NormalUnit` | the local slope | shading, deciding whether a slope is walkable |

Up is continuous everywhere. The normal swings around on rough ground; orienting
a character to it makes them lurch on every pebble.

---

## Characters

UE 5.4 added arbitrary gravity direction to `UCharacterMovementComponent`
(`SetGravityDirection`), with the movement mode, floor sweeps, step-up and
jumping all expressed in a gravity-relative basis. `APlanetCharacter` uses that
rather than reimplementing character movement, which is a large and thoroughly
debugged piece of engine code.

What is left for the character to do is comparatively small:

```cpp
Movement->SetGravityDirection(-LocalUp);
Movement->GravityScale = PlanetGravityMs2 * 100.0 / 980.0;
```

The magnitude goes through `GravityScale` — the CMC expresses gravity as a
multiple of the world's — rather than by changing world gravity, which is what
allows two characters on opposite sides of a planet, or on different planets, to
both be right.

### Gravity is withheld until the floor is real

Terrain that renders is not necessarily terrain you can stand on. A patch is
selected, generated, uploaded and only *then* cooked for collision, and in the
window between the last two the ground is visible, solid-looking and completely
intangible. A character walking onto such a patch does not stop at its edge —
they fall straight through the planet and keep going.

So `APlanetCharacter` asks `UPlanetTerrainComponent::HasCollisionAt` every frame
and holds position when the answer is no. Being visibly stuck for a fraction of a
second and then correcting itself is a far better failure than falling through a
world. The state is surfaced on the HUD so it reads as a wait rather than a hang.

The streamer helps from the other side: `ReservedCollisionGenerations` keeps part
of the generation budget for collision patches, so scenery — of which there is
always far more — cannot starve the ground under the player's feet.

---

## Testing

`Universe.Planet.PlanetGravityField` asserts, from 96 directions spread over the
whole sphere and at six distances each:

- the acceleration is antiparallel to the outward direction everywhere, to 1e-12
- it is exactly zero at the centre, with no invented direction
- the magnitude follows the right law on the right side of the surface
- nothing is ever NaN or infinite
- doubling the distance quarters the pull

The direction half is the one that matters most. A hardcoded world "down" passes
every test taken at the north pole, so the assertions are made from directions
spread over the sphere rather than from a convenient one.

Escape and circular-orbit speeds are checked against textbook Earth values —
11,180 m/s and 7,906 m/s — rather than against the implementation itself, so the
units are pinned to something outside this codebase.
