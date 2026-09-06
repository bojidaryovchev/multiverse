# Planetary Traversal

Deep space to standing on the ground, and back, with nothing in between.

Related: [SimulationFrames.md](SimulationFrames.md),
[PlanetaryGravity.md](PlanetaryGravity.md),
[PlanetTerrain.md](PlanetTerrain.md),
[ADR-004](../ADR/ADR-004-simulation-frames-and-planetary-traversal.md).

![Standing on a planet at 2.5 km, sun 35 degrees up](../Sprints/Sprint-003/Surface-Daylight.png)

---

## The three altitudes

"Altitude" is ambiguous, and the ambiguity is not academic: on Earth two of these
differ by nearly nine kilometres, which is the difference between a safe approach
and flying into a mountain. The word never appears unqualified in this codebase.

| Function | Meaning | Cost |
| --- | --- | --- |
| `GetDistanceFromCentreMeters` | `\|position\|`. Always positive. What orbital mechanics uses. | free |
| `GetAltitudeAboveSeaLevelMeters` | `distance - RadiusMeters`. Datum is the smooth reference sphere. Negative inside it. | free |
| `GetAltitudeAboveTerrainMeters` | `distance - (RadiusMeters + elevation)`. Height above the actual ground below. | one terrain evaluation |

For someone standing on the summit of Everest: 6,379,848 m / 8,848 m / 0 m.

Anything that says merely "altitude" is a bug report waiting to be written. The
HUD shows all three at once, side by side, for the same reason.

---

## The surface query API

`FPlanetTerrain` answers questions about the terrain *function* — give it a unit
direction, get an elevation. That is the right shape for a mesh builder, which
already knows the direction it is sampling, and the wrong shape for everything
else. A character, a landing craft, a spawner and a collision safety check all
start from a **position** and want to know what is underneath it.

`FPlanetSurfaceQuery` is that layer. It owns the projection from position to
direction so no caller has to normalise by hand and get it subtly wrong at the
centre of a planet.

```cpp
FPlanetSurfaceSample Sample = FPlanetSurfaceQuery::SampleBelow(Planet, Settings, PositionMeters);
//  .Direction  .ElevationMeters  .SurfaceRadiusMeters
//  .SurfacePositionMeters  .NormalUnit  .UpUnit  .bValid

FVector3d Spawn = FPlanetSurfaceQuery::GetPositionAboveTerrain(Planet, Settings, Direction, 2.0);
```

Everything here is planet-local metres, in a frame centred on the planet centre
and aligned with universe axes. Converting universe coordinates into that frame
is `APlanetActor`'s job — concentrating it in one place is the point, since a
caller doing it itself is one sign error away from putting a character on the
wrong side of a world.

Note that `SampleBelow(P)` and `SampleDirection(dir)` agree to normalisation
precision, **not** bit-exactly: `(d·r)/|d·r|` is not `d`, and the last couple of
digits move. That is around a nanometre of elevation on a 6,371 km planet.
Everywhere else in this project the seam guarantees *are* bit-exact, so the
difference is worth being explicit about.

---

## Not tunnelling through a planet

A physics engine detects collisions by looking at where things are. That is sound
as long as nothing moves further in one step than the thickness of what it might
hit. This project breaks that assumption by design: the probe has fifteen thrust
tiers reaching 10¹² m/s, and at 30 Hz a single frame is then **3.3 × 10¹⁰
metres** — over five thousand Earth diameters. The craft is on one side of the
planet at the start of the frame and clean through to the other side at the end,
having never once been inside. Chaos sees two positions in empty space and
reports nothing.

No amount of substepping fixes this — it would take thousands of substeps per
frame — and raising the physics rate is worse. The fix is to ask the analytic
question directly, before the movement is committed:

```cpp
FPlanetSweepResult Sweep;
if (FPlanetTrajectory::TryClampStepToBounds(Planet, Start, End, StandoffMeters, Sweep))
{
    // End was moved to a point StandoffMeters clear of the bounding sphere.
}
```

Exact, O(1), and independent of speed.

### Two levels of answer

**Bounding sphere** (`radius + maxElevation`) is cheap and conservative: no
terrain evaluation, no false negatives, occasional false positives over a
low-lying basin. That is the right trade for the movement path, which runs every
frame and only needs to know when to slow down.

**`SweepAgainstTerrain`** refines a bounding-sphere hit against the real terrain
by bisection, for cases that want the true contact point. Bisection rather than a
root find because the terrain function is fractal noise with no derivative worth
trusting, and bisection cannot diverge. 32 steps brings an Earth-diameter bracket
below three micrometres.

### The numerical part is not incidental

The quadratic is solved in the form that avoids catastrophic cancellation. The
naive `(-b ± sqrt(b² - 4ac)) / 2a` loses most of its significant digits when the
ray starts far from the sphere — which here is the normal case, not an edge case.
A craft 10¹³ m out has `b²` around 10²⁶ and `4ac` around 10¹³, so the subtraction
throws away everything that distinguishes a hit from a miss. The stable form
computes the root whose signs agree and recovers the other from the product of
the roots.

---

## Landing, and staying landed

A landed ship holds its universe position exactly. It does not integrate a
residual velocity and it does not re-settle against the collision mesh every
frame — both of which produce a craft that slides down a hill over the several
minutes a player might spend walking around it, and comes back to find it
somewhere else.

Touchdown is detected two ways, and both are needed:

- the **swept** test catches a descent that crosses the surface inside one step;
- the **end-of-step** altitude test catches slow arrivals, teleports onto the
  surface, and the tick after a landing.

The second is not redundant. `SweepAgainstTerrain` reports "started inside"
rather than a hit when a step *begins* below the landing margin, because there is
no entry point to return — and a ship at rest on the ground is permanently in
that state. Relying on the sweep alone made a landed ship unable to land: it fell
through its own resting position and reached terminal velocity against
atmospheric drag. That is what the first measurement of `universe.Land` showed,
and it is why the ship's held altitude is now asserted rather than assumed.

On contact the ship settles along the contact point's **direction**, not at the
contact point itself. The sweep stops the craft where it first touched, which on
a slope is partway up the hillside; a ship left there is intersecting the ground
it landed on.

---

## Walking on a sphere

`APlanetCharacter`. See [PlanetaryGravity.md](PlanetaryGravity.md) for the
gravity side.

The camera is the part that is not obvious. The usual
`AddControllerYawInput` / `AddControllerPitchInput` pair accumulates a
world-space `FRotator`, which encodes orientation as yaw about world Z and pitch
about the resulting Y. That is a gimbal, and it locks when the thing being
described is pitched 90° away from world Z — which is exactly what walking a
quarter of the way around a planet does. The classic symptom is a camera that
spins wildly as the player crosses the equator of whatever axis the rotator was
built around.

So look direction is not stored as a world rotator at all:

- **yaw** is a unit vector carried in the tangent plane, rotated about the
  current up. Storing a *vector* rather than an angle is what makes walking over
  the pole of any axis a non-event — an angle has to be measured from some
  reference direction, and every choice of reference degenerates somewhere.
- **pitch** is a scalar clamped to ±88°, which is where the only remaining
  degeneracy is.

The world rotation is rebuilt each frame from the current up and that forward
vector. The result is continuous everywhere on the sphere, including across cube
faces and over the poles of any axis anyone might name.

Movement input goes along the **tangent plane**, not along the camera's world
forward: on a sphere the two differ by the camera pitch, and using the camera
vector directly makes a character walk slower whenever they look at their feet.

---

## Position authority, and the one place it reverses

Everywhere else in this project the universe position is the truth and the Unreal
transform is derived from it. For a walking character that is reversed: the
movement component is authoritative over the transform while walking, and the
universe position is read *back* from it once per frame, after physics.

That is deliberate. Keeping the arrow pointing the usual way would mean
reimplementing floor sweeps, step-up, crouching and deceleration in universe
coordinates. Instead Chaos owns the metre-scale motion, the anchor owns the
universe-scale position, and they are reconciled in `TG_PostPhysics`.

**The read-back is guarded**, and the guard is load-bearing. While an anchor is
out of render range, `SyncTransformToOrigin` declines to place the actor at all
rather than inventing a location — so `GetActorLocation` still holds whatever it
was last set to. Reading that back overwrites a correct universe position with
one derived from a stale transform, and the error is not small: it puts the
character wherever the render origin happened to be. A freshly spawned character
ended up 5 × 10¹² m from the planet it was standing on. When out of range, the
authoritative position wins and the transform is re-derived from it.

---

## Streaming ahead of arrival

A craft descending at a kilometre a second reaches the ground in seconds; a patch
takes tens of milliseconds to generate. Without prewarming, arrival and the
ground being ready are a race that arrival frequently wins.

`APlanetActor` predicts the arrival point analytically — the intersection of the
current velocity with the body, eight seconds ahead — and feeds it to the terrain
component as a second observer. Look-ahead is expressed in **seconds** rather
than metres because what matters is how long the streamer has to prepare.

The prewarm pass is stateless, using `FPlanetQuadtree` directly rather than the
stateful `FPlanetQuadtreeSelector`. Two observers sharing one hysteresis history
would flicker against each other, so the prewarm contributes membership only and
never a split decision for the real observer. It is capped at a quarter of the
patch budget: insurance should not degrade the view the player actually has.

Slow movement produces no prediction at all — there the observer's own position
is already the right answer.

---

## Getting around, for debugging

| Command | Effect |
| --- | --- |
| `universe.Land [index]` | Land the ship below its current position, or at a seed-derived point |
| `universe.ExitShip` / `universe.EnterShip` | Step out / board, same as `F` |
| `universe.GotoSurface <index> [height]` | Teleport to a seed-derived surface point |
| `universe.GotoSubstellar [height] [elevationDeg]` | Teleport to a chosen solar elevation |
| `universe.FrameInfo` | Log the frame, all three altitudes, gravity, atmosphere |
| `universe.Journey` | Run and check the whole traversal unattended |

Spawn indices are hashed from the planet seed, so index 7 is the same place on
the same planet on every machine and in every run. That is what makes "the seam
at index 7 is wrong" a reproducible bug report rather than an anecdote, and it is
how the cube-face and corner crossings get exercised — the spread is uniform over
the sphere, so a handful of indices reliably includes points near edges and
corners.

`universe.GotoSubstellar` exists because day and night are currently a function
of *where* you are rather than *when*: there is no rotation model, so the
sub-stellar point is local noon and its antipode is midnight. It is also the only
reliable way to photograph the atmosphere, which is nearly black at a grazing sun
and blue overhead — a distinction that looks like a rendering failure until you
know which side of the planet you landed on.

---

## The journey test

`universe.Journey` drives the whole sequence unattended and checks each
transition against a deadline:

```
DeepSpace -> Approach -> AtmosphericEntry -> Landed -> Disembarked
          -> Walking -> Boarded -> Departure -> Complete
```

It lives in a world subsystem because it changes which pawn the player possesses
partway through — that is most of the point of it — so it cannot live on either
pawn.

It cannot see that the horizon looks wrong. It *can* see that the frame was
entered exactly once, that altitude decreased monotonically through the descent,
that the character ended up on the ground rather than inside it, that they moved
under real movement input at six points spread over the sphere, and that the ship
was still exactly where it was left — which is the part a person is worst at
checking.

The approach and departure use a rate proportional to the distance remaining
rather than a fixed speed. A fixed speed cannot work across the range involved:
the approach starts eight planet radii out — 40,000 km here, proportionally more
on a gas giant — and ends 60 km up. One speed that crosses the first distance in
a reasonable time arrives at the second having covered several planet diameters
in a single frame; one slow enough to arrive gently never finishes. A
proportional rate is scale-free, and its per-frame step is always a small
fraction of the distance remaining.

**Latest result:**

```
Final stage        : Complete
Elapsed            : 117.8 s
Frame transitions  : 2 (expected 2: one in, one out)
Origin rebases     : 926
Descent regressions: 0
Ship drift         : 0.000000 m
Frame released at 22,583.8 km (exit radius 22,561.9 km)
Furthest walked under movement input: 5.89 m
```
