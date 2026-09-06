# Star System Streaming

*Sprint 006. Source: `Source/Universe/Public/StarSystemStreamingSubsystem.h`.*

## A relevance system, not a loading system

A galaxy has a hundred billion stars and the player is near one of them. Every
star that exists is reconstructible from its address at any moment, so the
question is never "does this system exist" - it always does - but **"how much of
it is worth building right now"**.

That makes this a relevance system. There is no point at which a system is
created or destroyed; there is only a sliding amount of representation, from an
address that costs nothing through a point of light to a fully simulated world
with terrain under the player's feet. Nothing is lost by dropping back down,
because everything above the address was derived from it.

## The states

```text
Unknown          Not in range. No memory of it at all.
  |
DescriptorOnly   Address and position known. A few hundred bytes.
  |
DistantVisual    A star, drawn as a point of light in scaled space.
  |
NearbyVisual     Star plus placeholder bodies for its planets.
  |
Prewarming       Building the real planet, before the player arrives.
  |
Active           Full simulation: terrain, environment, collision, persistence.
  |
Unloading        Releasing the expensive parts.
```

Transitions are driven by distance, with one exception - `Prewarming`, below.

### Thresholds

| Transition | Default | Why |
|---|---|---|
| Scan radius | 25 ly | Addresses only; over a thousand sectors. |
| `DistantVisual` | 20 ly | A star worth drawing. |
| `NearbyVisual` | 0.05 ly | Its planets are resolvable as points. |
| `Active` | 0.01 ly | Roughly 630 AU; inside the system. |

All of them have **hysteresis**: a system leaves a state at 1.25x the distance
it entered it. Without that, a ship hovering on a boundary rebuilds and destroys
a solar system every frame - which is not so much a performance problem as a
visible one, since the planet under the player would blink.

### What is generated when

Nothing above an address is built until it is needed:

- The scan produces **positions only**. `GetSystemPosition` is a handful of
  random draws; full generation builds a star and up to twelve planets. A 25 ly
  radius is over a thousand sectors, and generating every star in them to decide
  which are worth drawing would be exactly the "loop over the galaxy" the sprint
  forbids.
- `FStarSystemDescriptor` is generated on the way into `DistantVisual`.
- `APlanetActor` - terrain, environment, vegetation, wildlife, collision - is
  built only for `Prewarming` and `Active`.

## Budgets

| Budget | Default | Why |
|---|---|---|
| Visual systems | 96 | Star actors on screen. |
| Nearby systems | 4 | Systems with planet placeholders. |
| **Active systems** | **1** | Not a performance limit. See below. |

Exactly one system may be `Active`, and that is a statement about what `Active`
means rather than a tuning decision. It owns the streaming planet, the
environment queries and the simulation frame, and two of those at once would be
two answers to "which way is down".

Budgets are applied **after** the distance decision, and systems are kept sorted
nearest-first, so a system close enough to matter is never denied by a budget
that a further one has already spent.

## Prewarming

The one transition that is not distance alone.

A ship closing on its target at warp covers the entire `NearbyVisual` band in
less than a frame. Waiting for it to be near enough would mean arriving in black
space and watching a planet assemble - which is the failure this whole layer
exists to prevent.

So the navigation target is promoted early on **time to arrival**:

```text
distance to target / closing speed <= PrewarmLeadSeconds
```

Time rather than distance because time is the quantity that actually bounds how
long there is to prepare.

## What is *not* released

**Persistent deltas.** A chopped tree and a placed structure live in the world
store, not in the actor. Dropping a system back to `DescriptorOnly` does not
touch them - that is precisely the point of the Sprint 005 architecture:
procedural base world plus sparse deltas equals the current world.

**Discovery state.** "Have I been here" is a fact about the player, not about
what is currently loaded. It is written to the world store as a world fact and
survives both unloading and restarting.

## Discovery

Three states and no progression system:

| State | When |
|---|---|
| `Undiscovered` | Never been in visual range. |
| `Detected` | Reached `DistantVisual` - the star has been seen. |
| `Visited` | Reached `Active` - the player has been there. |

Discovery only ever moves forward. Leaving a system does not un-visit it.

It exists so that navigation has something to list and so that persistence has
something to remember across sessions, not as the beginning of an exploration
metagame, which the sprint explicitly rules out.

Storage is `world_facts`, a small key/value table added to the persistence
schema in Sprint 006, keyed `sys.<system hash>`. It is a separate table from
`entity_records` because it genuinely is a different thing: discovery is not
attached to a planet or a region, and storing it as an entity would mean
inventing a planet key for it - the kind of shortcut that reads fine for a week
and then makes "delete this planet's data" silently forget where the player has
been.

## Which planet becomes a world

`ChooseStreamingPlanet` picks the most temperate world, by distance of its
equilibrium temperature from liquid water. A system with no such world still
gets one - the outermost, as Sprint 002 chose - because "nothing to stand on" is
a worse answer than "somewhere cold".

The placeholder sphere for the promoted body is **destroyed**, not hidden. Two
representations of one planet is the beginning of the render/simulation
confusion the architecture exists to avoid, and a hidden one is still a thing
that can be un-hidden by accident.

## What changed in the game mode

Sprints 002 through 005 had `AUniverseGameMode` build the scene: one star, its
planets as placeholders, and one of them promoted to a real streaming world.

That was right while there was exactly one system, and it stops being right the
moment the player can leave it. A game mode that spawns the scene owns actors it
has no way to release, and "the system" stops being a fact about the world and
becomes a question about where the player is.

So the game mode now picks a **home system** - the search that finds a habitable
world, started from inside a galaxy - places the probe beside it, and gets out
of the way. `GetActiveSystem` and `GetPlanetActor` forward to the streamer.
The system around the player appears because they are near it, by exactly the
same rule that will build the next one.

## Consequences worth knowing about

Two things broke when the world stopped existing before play began, and both are
the same shape:

- **Console commands run on frame zero.** `-ExecCmds` fires before anything has
  streamed in, so every scripted run that started with `universe.Land` failed on
  an empty world. `universe.After <seconds> <command>` defers one; the journey
  subsystem waits for a planet rather than refusing.
- **Fixed timings became wrong.** The Sprint 003 walk test advanced its
  waypoints on a two-second clock. A teleport lands somewhere with no cooked
  collision, and on the larger planet the galactic search now starts beside,
  building a patch there takes longer than two seconds - so the character was
  held for the entire stage and never took a step. Waypoints now advance on
  progress rather than on a clock.

The general lesson: **a streamed world makes every "it will be ready by now"
assumption a bug**, and those assumptions are invisible until the thing being
waited for gets slower.

## Diagnostics

| Command | Reports |
|---|---|
| `universe.Systems [n]` | Nearby systems, distance, state, discovery, target |
| `universe.Target <n\|name\|ahead\|clear>` | Selects a destination |
| `universe.FlyTo <target>` | Steers and brakes onto it under warp |
| `universe.TravelInfo` | Mode, speed, target, ETA, hazard stops |
| `universe.GalaxyInfo` | Galactic radius, height, stellar density |
| `universe.InterstellarJourney` | The scripted, self-verifying acceptance run |

The HUD's SYSTEM STREAMING panel shows the tracked count, the per-state counts,
discovery counts and the nearest five systems continuously.

`GetTransitionCount` is the flapping detector: a number that climbs steadily
while the ship is stationary means a hysteresis band is too narrow.

## Related

- [InterstellarTravel.md](InterstellarTravel.md) - moving between systems
- [WorldPersistence.md](WorldPersistence.md) - what survives unloading
- [SimulationFrames.md](SimulationFrames.md) - why only one system is Active
- [UniverseScaleRendering.md](UniverseScaleRendering.md) - how it is drawn
