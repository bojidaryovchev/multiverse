# ADR-007: Galaxy as a density function, and streaming as relevance

- **Status**: Accepted
- **Sprint**: 006
- **Supersedes**: nothing. Extends [ADR-002](ADR-002-seed-hierarchy.md).

## Context

Sprint 001 placed 0.47 star systems in every sector of the universe, uniformly,
forever. Sprints 002 through 005 then built one real planet, in one system,
chosen by the game mode during `StartPlay` and owned by it for the life of the
session.

Both were correct for what they were proving and both stop working the moment
the player can travel between stars.

Uniform density gives a universe with no structure: nowhere crowded, nowhere
empty, no landmarks, and no reason for one direction to differ from another. A
game-mode-owned scene gives a world that cannot be left: the game mode holds
actors it has no way to release, and "the system" is a fact about the world
rather than a question about where the player is.

## Decision

### 1. A galaxy is a density function, not a container

`FGalaxyDescriptor` does not hold stars. It answers *how likely a star is at a
place*, and `GetSystemCountInSector` multiplies its own baseline count by that
value.

The density roll is drawn from a **separate stream** (`StreamOrbital`) from the
base count (`StreamPrimary`), so a sector's base count is unchanged by whether a
galaxy exists. Without that, introducing galaxies would have silently renumbered
every system in the universe.

Density is **exactly zero** outside the disk radius and outside the disk
thickness. Not asymptotically small - exactly zero. A galaxy that faded would
scatter a thin dusting of stars across the whole of intergalactic space, and
"empty" would never actually be empty.

### 2. Streaming is a relevance system, not a loading system

`UStarSystemStreamingSubsystem` owns every visual and simulated representation
of every system. Systems are never created or destroyed - they are always
reconstructible from their address - only more or less represented, along a
sliding scale from an address to a world with terrain underfoot.

Exactly one system may be `Active`. That is not a performance budget: `Active`
owns the streaming planet, the environment queries and the simulation frame, and
two of those at once would be two answers to "which way is down".

### 3. The game mode picks a home and gets out of the way

`AUniverseGameMode` searches for a habitable system, places the probe beside it,
and stops. `GetActiveSystem` and `GetPlanetActor` forward to the streamer. The
system around the player appears because they are near it, by exactly the same
rule that will build the next one.

### 4. One canonical position and velocity, two controllers

`FTravelState` is the only ship state. Warp integrates through
`FInterstellarTravel::Step` - swept, braked, arrival-clamped - and sublight keeps
the direct thrust model of Sprints 001 to 005. Two controllers over one state is
fine; duplicated state is not.

Travel mode is **derived** from where the ship is, not stored as authority. Warp
is the single exception, because it is a thing the player switches on.

### 5. World-scoped facts get their own table

Persistence schema v2 adds `world_facts`, a key/value table for things true of
the *world* rather than of a place in it. Discovery is the first consumer.

Storing discovery in `entity_records` would have meant inventing a planet key
for it - a shortcut that reads fine for a week and then makes "delete this
planet's data" silently forget where the player has been.

The v1 -> v2 migration upgrades in place rather than refusing. Every schema
change so far is additive and `CREATE TABLE IF NOT EXISTS` has already done the
work by the time the version is checked. Refusing would have told a player who
built a base in Sprint 005 that their world was incompatible - losing exactly
what the persistence work exists to protect, in exchange for nothing. A future
non-additive change is where a real per-version migration goes, and refusing is
the right answer for any step that has none.

## Consequences

### Good

- Intergalactic space is a real place. The universe has structure, and the
  player's position within a galaxy is a meaningful, reportable fact.
- The star generator was not modified to accommodate galaxies. It produces what
  it always produced and simply produces nothing where the galaxy is not.
- A journey out and back regenerates the home system to the identical content
  hash after every actor in it has been destroyed and rebuilt. That is the
  strongest determinism assertion the project has, and it is only reachable by
  actually leaving.
- Cost scales with path relevance. The broad phase walks the sectors a path
  crosses; the streamer scans addresses and generates only what it draws.

### Bad, or at least the price

- **The universe origin is now empty.** Every test and every startup path that
  searched from `FUniversePosition()` found nothing. Eight generation tests and
  one crash were the immediate cost; the fix - `FindPopulatedSectorNear` and
  `FindSystemNear` - is production API the game mode needed anyway.
- **A streamed world makes every "it will be ready by now" assumption a bug.**
  `-ExecCmds` runs on frame zero; the Sprint 003 walk test paced its waypoints
  on a fixed clock. Both were correct against a world that existed before play
  began and silently wrong against one that streams in.
- **Galaxy density uses `Atan2` and `Sin`**, putting it on the same
  cross-platform footing as star generation. See the open risk below.
- Two controllers over one movement state is a seam that has to be maintained.
  It is guarded by there being exactly one position and one velocity, but a
  third controller would be the point to stop and unify.

## Open risks

**Cross-platform libm (carried from ADR-002).** The spiral arm term uses
`Atan2` and `Sin`, so galaxy density shares star generation's exposure to libm
differences between platforms. This is a deliberate choice: the formulations
that avoid trigonometry do not produce spiral arms, and the fix - a
project-owned libm - solves star generation and galaxy density together. It is
one problem, not two.

The terrain and climate paths remain transcendental-free, which is where it
matters most: those decide ground somebody has built on.

**Galaxy density is a gameplay number.** 0.15 galaxies per intergalactic cell is
roughly twenty-five times the real density. Recorded in the header rather than
buried, because it is the kind of number a later reader would otherwise assume
was astronomy.

## Related

- [GalaxyGeneration.md](../Architecture/GalaxyGeneration.md)
- [GalacticCoordinates.md](../Architecture/GalacticCoordinates.md)
- [InterstellarTravel.md](../Architecture/InterstellarTravel.md)
- [StarSystemStreaming.md](../Architecture/StarSystemStreaming.md)
- [UniverseScaleRendering.md](../Architecture/UniverseScaleRendering.md)
