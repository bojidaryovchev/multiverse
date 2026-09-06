# ADR-004: Simulation Frames, Planetary Gravity and Continuous Traversal

- **Status:** Accepted
- **Date:** 2026-09-06
- **Sprint:** 003
- **Related:** [ADR-001](ADR-001-universe-coordinate-system.md),
  [ADR-003](ADR-003-planet-topology-and-lod.md),
  [SimulationFrames.md](../Architecture/SimulationFrames.md),
  [PlanetaryGravity.md](../Architecture/PlanetaryGravity.md),
  [PlanetaryTraversal.md](../Architecture/PlanetaryTraversal.md)

---

## Problem

Sprint 002 left two render spaces that did not meet: a 1e-7 scaled model for
astronomical bodies and true scale for terrain, with no defined moment at which
one gave way to the other. ADR-003 recorded this as "the central Sprint 003
problem". Anything crossing between them was silently reinterpreted by a factor
of ten million, which had already produced a real lighting bug.

Separately, nothing on a planet had gravity, an up direction, or a way to stand
on the ground, and the probe could pass through a planet without noticing.

---

## Decision 1: the frame is explicit state, with hysteresis

**Selected: two named frames — `Interstellar` and `Planetary` — chosen by a
stateful selector with separate enter and exit radii.**

### Options considered

**A continuous blend between the two spaces.** Rejected. There is no scale at
which a 1e-7 model and a 1:1 model are both approximately right, so a blend would
be wrong everywhere in the transition instead of only at its edges, and would
make every number in the region ambiguous rather than merely converted.

**A single distance threshold.** Rejected because the frame would then be a pure
function of position, and position is noisy. The boundary is a sphere; an
orbiting craft skims along it and lands on a different side each frame. A frame
change rebases the render origin, swaps the gravity model, and starts and stops
terrain streaming, so a craft parked on the boundary would do all of that several
times a second forever.

**Three frames, adding a "system" frame.** Rejected: near a star and far from any
planet, the simulation does exactly what it does between stars. A frame that
changes nothing is a name to keep in sync, not an abstraction.

**Two frames with hysteresis. [selected]** Entered at three planet radii, left at
4.5. The state depends on the history of the crossing rather than only on present
position, which is the whole point: inside the band both answers are stable.

### Why the band is measured in time

At Earth scale the gap is about 9,500 km, which low orbit takes twenty minutes to
cross. The band has to be wide in *time*, not metres — what it protects against
is rapid oscillation, and a band a craft can cross in a frame protects nothing.

The same device, for the same reason, is already used for LOD splits.

---

## Decision 2: hide scaled bodies in the planetary frame

**Selected: everything anchored in `ScaledAstronomical` is hidden while attached
to a planet.**

Scaled space is a 1e-7 model built around the render origin. Once the player
stands on a planet the render origin is where they stand, so the entire solar
system renders a few metres from their face as small discs occluding the sky.
This is not a scaling error — it is what a 1e-7 model *means* when the viewer is
inside it.

**Rejected: a separate far-field render pass** with its own depth range, so
distant bodies appear in the sky at true angular size. That is the answer that is
actually right, and it is a rendering feature rather than a coordinate one.
Approximating it now would mean inventing angular sizes, which is the "visual
scale becomes universe scale" mistake this architecture exists to avoid.

**Accepted cost, stated plainly: from a planet surface, other bodies in the
system are not visible.**

---

## Decision 3: gravity is a field computed from geometry

**Selected: `normalize(centre - position)`, inverse square outside the body,
linear to zero inside it.**

There is no world "down" and there cannot be one: two players on opposite sides
of a world have opposite up vectors and both are right. A hardcoded `-Z` passes
every test taken near the origin and fails the moment somebody walks over the
horizon.

**The inside-the-body branch is linear rather than an extended inverse square**,
which is both the correct physics under the shell theorem and the form that
cannot produce an infinity at `r = 0`. Something will eventually end up at a
planet centre — a debug teleport, a bad spawn, a projectile that tunnelled — and
a singularity inside the playable volume is a crash waiting for a reason.

Gravity is asked of the world subsystem, never of a planet directly, so a caller
cannot use a body the frame selector has already released.

---

## Decision 4: stock character movement with custom gravity

**Selected: `ACharacter` with `UCharacterMovementComponent::SetGravityDirection`,
driven per frame from the frame planet.**

UE 5.4 added arbitrary gravity direction to the CMC, with movement mode, floor
sweeps, step-up and jumping all expressed in a gravity-relative basis.

**Rejected: a custom movement component.** Reimplementing character movement
means reimplementing floor detection, step-up, crouching, deceleration and
network prediction — a large, thoroughly debugged piece of engine code — to gain
nothing the stock component does not already provide.

The consequence is that for a walking character the usual direction of authority
is **reversed**: the movement component owns the Unreal transform and the
universe position is read back from it once per frame, after physics. That is the
only place in the project where this happens, and it is guarded — while an anchor
is out of render range no transform is written at all, so reading one back
overwrites a correct position with a stale one. That defect put a freshly spawned
character 5 × 10¹² m from the planet it was standing on.

### The camera cannot use a world rotator

`FRotator`-based controller rotation encodes yaw about world Z, which gimbal-locks
when local up turns perpendicular to world Z — precisely what walking a quarter of
the way around a planet does. Look direction is therefore held as a tangent-plane
**vector** plus a clamped pitch scalar, and the world rotation is rebuilt from the
current up each frame. A vector has no reference direction to degenerate; an
angle always does.

---

## Decision 5: analytic swept collision, not substepping

**Selected: segment-versus-body intersection evaluated before each movement step
is committed.**

At the probe's top thrust tier one frame is 3.3 × 10¹⁰ m. Both endpoints of such
a step are in empty space with the planet squarely between them, so a
position-based check reports nothing.

**Rejected: substepping.** It would take thousands of substeps per frame at these
speeds, and raising the physics rate is worse.

**Rejected: capping speed to what Chaos can handle.** That would make the
coordinate system's whole point — travelling astronomical distances — unusable.

The analytic test is exact, O(1), and independent of speed. The quadratic is
solved in its numerically stable form, which matters here rather than in theory:
at 10¹³ m the naive discriminant subtracts two quantities agreeing to thirteen
digits and throws away everything that distinguishes a hit from a miss.

Bounding-sphere first for the movement path, terrain refinement by bisection for
callers that need the true contact point. Bisection rather than a root find
because fractal noise has no derivative worth trusting and bisection cannot
diverge.

---

## Decision 6: physical illuminance with calibrated exposure

**Selected: the star light carries its true illuminance; the camera's exposure is
computed from that same number.**

Sprint 002 clamped the star to 25,000 lux "for display", recording that real
photometric range was Sprint 003 work. The clamp existed because a physically-lit
scene has no meaning without a physically-set camera: auto-exposure has to guess,
and it guesses badly when a scene contains an emissive star, ground lit at a
million lux, and a scattering atmosphere filling most of the frame.

The fix is the one a photographer would use. The illuminance is known exactly, so
the exposure it calls for is known exactly: fixed ISO and aperture, shutter
derived from the scene. At 1 AU this lands within a third of a stop of the sunny
f/16 rule, which is a reassuring sign that the units are right rather than merely
self-consistent.

Aperture is deliberately not overridden. Unreal takes the exposure aperture from
`DepthOfFieldFstop` — the same field that drives physical depth of field — so
overriding it switches that model on, and with no focal distance set every bright
point becomes a large defocused disc near the centre of the screen. That disc
looks exactly like a distant object drawn in the wrong place.

---

## Decision 7: the atmosphere is a simulation boundary the renderer is handed

**Selected: `AtmosphereHeightMeters` on the descriptor, given directly to
`USkyAtmosphereComponent`.**

Unreal's sky atmosphere is one of the few stock rendering features that is
already planet-shaped: parameterised by a planet centre, a ground radius and an
atmosphere height. So the logical atmosphere — the one deciding drag and where a
flight model changes — is handed to it in the same kilometres rather than
approximated with height fog that assumes a flat world and a world-Z up.

The boundary a player can see and the boundary the simulation acts on are the
same field of the same descriptor and cannot drift apart. A visual cue that
drifted from the simulation would be worse than none.

**Accepted cost: the sky is drawn only from inside the air.** Unreal's
aerial-perspective LUT has a bounded depth range, and from orbit the
extrapolation beyond it hid the terrain entirely — a real regression against the
orbital view Sprint 002 had working. A planet therefore has no visible blue limb
from space, which is worth having and is recorded as future work.

---

## Consequences

### Good

- Scaled and true-scale space now meet at a defined, named, tested boundary.
- Gravity has exactly one implementation and no preferred axis anywhere.
- A craft at 10¹² m/s cannot pass through a planet.
- The full journey runs unattended and checks itself; it found three real
  defects that reading the code did not.
- Physical light and physical exposure, so the atmosphere and the ground are lit
  by the same number.
- Terrain streaming follows possession rather than a specific pawn class.

### Bad / accepted costs

- **No other bodies visible from a planet surface**, and **no atmospheric limb
  from orbit**. Both need a far-field render pass.
- **Day and night are a function of position, not time** — there is no rotation
  model, so the sub-stellar point is permanently noon.
- **Atmospheric drag is a single coefficient**, not aerodynamics. It has no
  density, cross-section or drag coefficient behind it, and is named for what it
  does rather than what it claims to be.
- **A hard impact and a gentle landing have the same outcome.** The threshold
  exists and is logged so a damage model has something to key off; there is no
  damage model.
- **The character reads its position back from its transform**, the one reversal
  of the project's usual authority direction. It is guarded, but it is a place
  where a future change could reintroduce drift.
- **`SampleBelow` and `SampleDirection` agree to normalisation precision, not
  bit-exactly** — unlike every seam guarantee elsewhere in the project.

### Neutral

- Ground radius is clamped by Unreal to 10,000 km for sky rendering. Every body
  generated so far is comfortably inside; a larger one renders without a sky
  rather than wrongly.

## Revisit if

- Bodies need to be visible from a surface, or a limb from orbit — then a
  far-field render pass, which also subsumes the scaled-body visibility rule.
- Planetary rotation arrives — then day and night become temporal, and the
  sub-stellar teleport becomes a time-of-day control instead.
- Multiple simultaneously-inhabited planets arrive — the frame selector already
  handles overlapping bodies, but the render origin follows one tracked anchor
  and would need to become per-player.
