# Simulation Frames

How the game decides *where you are*, in the sense that changes how physics
behaves.

Related: [UniverseCoordinates.md](UniverseCoordinates.md),
[PlanetaryGravity.md](PlanetaryGravity.md),
[PlanetaryTraversal.md](PlanetaryTraversal.md),
[ADR-004](../ADR/ADR-004-simulation-frames-and-planetary-traversal.md).

---

## The problem this exists to solve

Sprint 002 ended with two render spaces that never met.

| | Scale | Used for |
| --- | --- | --- |
| `ScaledAstronomical` | 1e-7 | Stars and planets seen from across a system |
| `Local` | 1:1 | Terrain you can stand on |

Both were correct in isolation. What was missing was any defined moment at which
one gave way to the other, so anything crossing between them — a position, a
velocity, a light intensity — was silently reinterpreted at a factor of ten
million. That had already produced one real bug: a point light in scaled space
illuminating true-scale terrain at 68 lux from orbit and 950,000 lux from four
kilometres up, because the distance in the falloff was a scaled distance and the
geometry was not.

The fix is not a cleverer conversion function. It is to make the frame an
explicit piece of state with a name, so that "which space is this number in" has
an answer instead of being re-derived by each caller from context.

---

## The two frames

```
EUniverseFrameKind::Interstellar     no body dominates
EUniverseFrameKind::Planetary        one planet dominates
```

**Interstellar** covers the overwhelming majority of the universe by volume.
Positions are universe coordinates, bodies are drawn scaled, and there is no
gravity, no ground and no up.

**Planetary** means a planet has claimed the observer. Its terrain is drawn at
true scale around them, gravity points at its centre, up is defined, and
altitude means something.

### Why not three

A separate "system" frame — near a star but far from any planet — was
considered and rejected. It would have no distinct behaviour: with no planet
close enough to matter, the simulation does exactly what it does between stars.
A frame that changes nothing is a name to keep in sync, not an abstraction.

---

## The transition, and why it has hysteresis

Two radii, both measured from the planet centre:

```
EnterRadius = max(3 x planetRadius, planetRadius + 10 x atmosphereHeight)
ExitRadius  = 1.5 x EnterRadius
```

The frame is **entered** at `EnterRadius` and only **left** at `ExitRadius`.

A single threshold would make the frame a pure function of position, and
position is noisy. The boundary is a sphere, an orbiting craft skims along it,
and each frame it lands on a different side. Switching frames is not free — it
rebases the render origin, changes the gravity model, and starts and stops
terrain streaming — so a craft parked on the boundary would do all of that
several times a second, forever.

With two radii the state depends on the *history* of the crossing rather than
only on where the observer is now. Inside the band both answers are stable, and
which one applies is decided by which way the observer was going when they
crossed.

The band has to be wide in **time**, not in metres. At Earth scale the gap is
about 9,500 km, which a craft in low orbit at 7.8 km/s takes twenty minutes to
cross flying straight through it. A band that could be crossed in a frame would
protect nothing.

The same device is used for LOD splits in `FPlanetQuadtreeSelector`, for the
same reason, and it is why both are stateful classes rather than free functions:
a threshold with memory is not a function of the present, however much tidier
that would look.

### Why three planet radii

Entering early is nearly free and gives the streaming prewarm time to work.
Entering late means arriving somewhere with no ground built yet. At three radii
gravity is already down to 11% of its surface value, so nothing physical is
being missed by claiming the observer that far out.

---

## Dominance, and two bodies at once

A planet's claim is expressed as a ratio rather than a distance:

```
dominance = distanceFromCentre / EnterRadius
```

Below 1 means close enough to claim; lower means a stronger claim. The ratio
matters where a moon sits inside its planet's influence sphere: comparing raw
kilometres would hand the observer to whichever body was physically nearer even
while they were standing on the other one.

Handing over between two overlapping bodies requires the rival to be **twice**
as dominant, so the boundary between them is itself hysteretic and a craft
flying between a planet and its moon changes frame once each way.

---

## Where it lives

| | |
| --- | --- |
| `SimulationFrame.h` (`UniversePlanet`) | The frames, the radii, the selector. Pure maths, no engine. |
| `UUniverseWorldSubsystem` | Owns the selector, the planet registry, and the answer to "which way is down". |
| `APlanetActor` | Offers itself as a candidate; converts universe positions into its own frame. |

Gravity is asked of the **subsystem**, never of a planet directly, so a caller
cannot accidentally use a body the selector has already released.

```cpp
FVector3d Gravity = Subsystem->GetGravityAccelerationMs2(Position);
FVector3d Up      = Subsystem->GetLocalUp(Position);
bool      bOnPlanet = Subsystem->IsInPlanetaryFrame();
```

`OnSimulationFrameChanged` broadcasts after a change, which is the signal to
rebase, re-parent or restart streaming.

---

## What the frame currently changes

| | Interstellar | Planetary |
| --- | --- | --- |
| Gravity | none | toward the planet centre, 1/r² |
| Up | undefined (+Z fallback) | away from the planet centre |
| Terrain streaming | idle | active around the observer |
| Scaled bodies | drawn | **hidden** |
| Sky | none | drawn, from inside the atmosphere |
| Character movement | n/a | custom-gravity `CharacterMovementComponent` |

### Scaled bodies are hidden, and this is a real cost

Scaled space is a 1e-7 model built around the render origin. Once the player is
standing on a planet, the render origin is where they are standing — so the
entire solar system is drawn a few metres in front of their face, as small discs
that occlude the sky. That is not a scaling error; it is what a 1e-7 model
*means* when the viewer is inside it.

Hiding them is the honest reconciliation available now. The consequence is
stated plainly: **from a planet surface, other bodies in the system are not
visible.** The fix that would actually be right — a separate far-field render
pass with its own depth range, so a planet appears in the sky at its true
angular size — is a rendering feature rather than a coordinate one, and is
recorded as future work rather than approximated.

---

## Testing

`Universe.Planet.SimulationFrameHysteresis` asserts the property that matters:
across 200 ticks of one-metre jitter on each boundary, the frame changes **zero**
times and the state is identical at every single tick. A count-only check would
pass on a pair of transitions that cancelled out, having rebased the origin four
hundred times.

`Universe.Planet.SimulationFrameHandover` covers two overlapping bodies and the
empty-candidate case.

`universe.Journey` checks it end to end in a real world: exactly two transitions
over a full descent and departure, with the release measured at 22,583.8 km
against an exit radius of 22,561.9 km.
