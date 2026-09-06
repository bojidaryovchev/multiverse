# Interstellar Travel

*Sprint 006. Source: `Source/UniverseGeneration/Public/InterstellarTravel.h`,
`Source/Universe/Public/UniverseProbePawn.h`.*

## The two problems speed creates

`NewPosition = OldPosition + Velocity * DeltaTime` is correct arithmetic and a
completely inadequate movement model at these speeds, for two separate reasons.

**Collision.** At warp a frame covers more distance than the diameter of a star.
The ship does not pass *through* a star so much as skip over it: on one side at
the start of the frame, clean past at the end, never once occupying the same
space. Any collision system that looks at where things *are* reports nothing.

**Arrival.** A ship closing at 10^15 m/s that begins braking when it is "close"
has already passed its destination by several light hours. Proximity is the
wrong trigger; the braking distance implied by the current speed is the right
one.

Both are solved analytically rather than by substepping. Substepping at these
speeds would take millions of steps per frame, and raising the physics rate is
worse.

## One state, several regimes

There is exactly **one canonical ship position and one canonical velocity**.

```cpp
struct FTravelState
{
    FUniversePosition Position;   // canonical, exact
    FVector3d VelocityMs;         // metres per second, universe axes, double
    EUniverseTravelMode Mode;     // derived label
};
```

Velocity is metres per second in universe axes - not centimetres, and not
Unreal's render frame, both of which would silently overflow single precision
long before warp speed.

The travel *mode* changes what the controls do. It changes nothing about where
the ship is. A second position that applied "while warping" would be a second
source of truth, and the point where the two disagree is the point where a
player watches their ship jump.

### Travel modes

| Mode | Applies | Max speed | Acceleration |
|---|---|---|---|
| `Surface` | Within 100 km of a surface | 10 km/s | 500 m/s² |
| `LocalSpace` | Within 10^9 m of a body | 10 km/s | 500 m/s² |
| `Interplanetary` | Within 10^13 m of a star | 0.01 c | 2e5 m/s² |
| `Interstellar` | Anything further | 0.5 c | 1e7 m/s² |
| `Warp` | Engaged by the player | 3.3e6 c | 5e13 m/s², braking 1e14 |

Every number is in `FTravelProfile`, which is data. Nothing in the travel
mathematics knows what the numbers are, which is how the sprint's requirement
that future civilisation technology can raise the maximum speed is met.

`FTravelProfile::IsValid()` rejects a profile whose limits are not monotonic. A
profile where interplanetary is faster than warp is not a balance choice, it is
a typo, and it would make the ship slower the further out it went.

**Mode is derived, not stored as authority.** `SelectMode` is a pure function of
where the ship is relative to the nearest body. A ship near a planet is in a
local regime *because it is near a planet*, not because something set a flag and
forgot to clear it. Warp is the one exception, because it is a thing the player
switches on.

### Two controllers, one state

The probe pawn keeps its Sprint 001-005 direct thrust model for sublight flight
and integrates through `FInterstellarTravel::Step` when warp is engaged. Two
controllers over one state is fine; duplicated state is not. Whichever ran this
frame, "where is the ship" is answered by the same anchor.

## Warp

Engaging warp is refused while landed, or while the regime is `Surface`.
Warping out of a gravity well from a standing start is not a manoeuvre: the
first step is light minutes long and the swept path leaves through the planet,
so the trajectory check stops the ship on the first frame and every frame after
it. Refusing and saying why is better than that.

Warp disengages on its own upon arrival. Leaving the drive engaged at a
destination means the next frame accelerates straight back out of the system the
player just spent a minute reaching.

`J` toggles it; `universe.Warp on|off` does the same from the console.

## Trajectory safety

Every warp step is swept against the astronomical broad phase before it is
committed. **There is no unswept variant of `Step`** - the clamp is the only
thing standing between a warp jump and flying through a star, so it is not
optional.

### Broad phase

```text
travel segment
  -> sectors crossed          3D DDA over the sector grid
  -> dilated by one sector    a star just outside can put a planet inside
  -> candidate systems        positions only, no generation
  -> system bounding sphere   2e13 m; rejects almost everything
  -> full system generated    only for survivors
  -> star and each planet     swept against a standoff sphere
```

Cost scales with the length of the path, not with the size of the galaxy. A
frame-long step at any speed below about five light years per frame touches a
single-digit number of sectors.

The dilation matters and is easy to get wrong. A system's bodies orbit up to
about 2 x 10^13 metres from their star, which is 0.04% of a 4.87 ly sector -
small, but not zero. A star just outside a crossed sector can still put a planet
inside it. Candidates are deduplicated, because consecutive crossed sectors
share most of their neighbours and the naive version would generate the same
system twenty-seven times.

### The sweep itself

`FUniverseSweep::SegmentSphere` in UniverseCore, shared with
`FPlanetTrajectory`. It solves the quadratic in the form that avoids
catastrophic cancellation: at interstellar range `b^2` is around 10^32 while
`4ac` is around 10^18, so the naive `(-b ± sqrt(b² - 4ac)) / 2a` throws away
everything that distinguishes a hit from a miss.

It lives in UniverseCore rather than being copied because a second copy of a
quadratic this subtle is a copy that eventually drifts - in the direction of
silently letting a ship through a star.

### The standoff

Bodies are swept against a sphere of `max(radius * 3, 10^7 m)`. Three radii
rather than one: stopping exactly at a star's surface is stopping inside its
corona, and a margin costs nothing when the alternative is being told about the
collision after it has happened.

On a hit the step is clamped to the entry fraction, and only the velocity
component *heading into* the body is removed. Zeroing the whole velocity would
bring a ship that merely grazed a system to a dead stop, which is a worse
experience than the collision it is avoiding.

A step that *starts* inside a standoff sphere is reported but not clamped.
There is nothing useful to clamp to, and clamping would trap the ship where it
is.

## Braking and arrival

Two mechanisms, and both are needed.

### The braking law

```text
braking distance = v² / 2a
begin braking when distance_to_target <= braking_distance * (1 + margin)
```

The margin is a fraction rather than a distance because the slack needed scales
with the braking distance, which spans ten orders of magnitude between
manoeuvring and warp. Twenty percent absorbs a frame of decision latency, which
at warp is light minutes.

Retro-thrust is applied along the **heading**, not along the vector to the
target. The ship may be closing sideways, and thrusting at the target would
curve the path rather than slow it down.

### The arrival clamp

The braking law is correct in the limit, and the limit is not where the ship
lives. On the last frames of a warp approach it is still doing 10^12 m/s with an
arrival radius of 10^11 metres - it enters the radius and leaves it again inside
one frame. By the letter of the test it arrived; in fact it sailed straight
past.

So an auto-braked step is additionally clamped to the point of **closest
approach** to the target. That removes overshoot as a possibility at any frame
rate, which no tuning of the braking constants can, and it holds for a
multi-second hitch as well as for a 30 Hz frame. A clamped step then drops the
closing velocity, since keeping it would send the ship straight back out on the
next frame.

`UniverseTest_TravelWarpOvershoot` runs both: a full 2 ly approach at 30 Hz that
must stop near the target, and a single 100-second step at maximum warp aimed at
a target one light year away, where the unclamped step is thousands of times the
distance to the destination.

## System prewarming

The streamer's one transition that is not distance alone.

A ship closing on its target at warp crosses the entire `NearbyVisual` band in
less than a frame, so waiting for it to be near enough would mean arriving in
black space and watching a planet assemble. **Time to arrival** is the right
trigger, because it is the quantity that bounds how long there is to prepare.

```text
distance to target / closing speed <= PrewarmLeadSeconds (default 8 s)
  -> Prewarming
```

See [StarSystemStreaming.md](StarSystemStreaming.md).

## Target changes

Targets live on the streamer, not on the pawn, because the streamer is what has
to prewarm them. `universe.Target <index|name|ahead|clear>` sets one; `ahead`
picks whatever the ship is pointing at within about eleven degrees, which is the
starmap-free way to choose a destination.

Changing the target mid-warp is a normal operation: the travel step reads the
target fresh every frame, so the braking decision, the steering and the prewarm
all follow immediately. The old target simply stops being prewarmed and drops
back down the streaming states on its own.

## Navigation readouts

`universe.TravelInfo` and the HUD's TRAVEL panel report mode, speed, target,
distance, ETA, autopilot state and hazard stops.

Hazard stops are shown deliberately. A count that never moves across a session
spent flying through crowded space is a swept trajectory check that has quietly
stopped running, and that is not a failure any other reading would reveal.

Distances are formatted in metres, kilometres, AU or light years.
An earlier version had a light-minutes band, which reads well and is wrong: a
light minute is 0.12 AU, so it swallowed the entire system scale and reported
the Earth's orbit as "8.32 light minutes". True, and not what anybody navigating
a solar system wants to read.

## The scripted journey

`universe.InterstellarJourney` runs the whole thing and verifies itself: settle,
record the home system's content hash, pick the nearest other system, fly there
under autopilot, check the arrival, fly back, and check that home regenerated
identically.

The return leg is the assertion that matters. Every actor in the home system was
destroyed when the player left and rebuilt on the way back; if procedural
regeneration were order-dependent - a shared RNG, a cache keyed on visit order -
this is where it would show, and nothing short of actually leaving and returning
would catch it.

Every stage has a deadline taken from the travel layer's own estimate, so the
run works at any speed profile and doubles as a check on the estimate.

## Related

- [GalacticCoordinates.md](GalacticCoordinates.md) - the coordinate views
- [StarSystemStreaming.md](StarSystemStreaming.md) - what exists as you travel
- [PlanetaryTraversal.md](PlanetaryTraversal.md) - the same problem at planet scale
- [SimulationFrames.md](SimulationFrames.md) - which body owns "down"
