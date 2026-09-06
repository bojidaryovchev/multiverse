# MVP Architecture

*Sprint 008. The whole thing, in one document, for somebody who has just
arrived.*

## The one sentence

**The universe is enormous logically, costs nothing where nobody is, and is
extremely detailed around the player** - because everything is a pure function
of a seed and an address, and only the differences a player has made are ever
stored.

## The shape

```text
                    UniverseCore          no engine dependency
                       |                  compiles and tests standalone
                       |  FUniversePosition, seeds, hashing, sweeps
                       v
                  UniverseGeneration      no engine dependency
                       |                  galaxies, systems, planets, travel
                       |  FGalaxyGenerator, FStarSystemGenerator,
                       |  FInterstellarTravel
                       v
                   UniversePlanet         no engine dependency
                       |                  terrain, climate, biomes, vegetation,
                       |                  weather, gravity, trajectories,
                       |                  persistent identity
                       v
                     Universe             the only module that knows about
                                          Actors, rendering, input and network
```

**The dependency runs one way and never back.** That is not tidiness: it is what
lets a million assertions run in ten seconds with no editor, on a machine with
no Unreal installation at all. Three quarters of this project is testable that
way, and it is the three quarters where a bug is expensive.

## The layers of the world

| Layer | Size | Addressed by |
|---|---|---|
| Universe | ~10^10 ly | int64 cell per axis, 2^40 cm each |
| Intergalactic cell | 1.28 million ly | 2^40 cells |
| Galaxy | 15,000-90,000 ly | cell + index |
| Sector | 4.87 ly | 2^12 cells |
| Star system | ~10^13 m | sector + index |
| Planet | 10^6-10^7 m | system + orbit index |
| Terrain patch | metres to hundreds of km | cube face + quadtree level + x/y |
| Persistence region | ~2 km | planet + face + level + x/y |

Every level is integer-addressed and derived from position. **Nothing at any
level is stored.** An empty intergalactic cell costs nothing, and there are
10^21 of them.

## The five ideas

### 1. Position is an integer and an offset

`FUniversePosition` is an int64 cell index per axis plus a double local offset
in centimetres. 2.44 micrometre resolution anywhere in a universe 10^10 light
years across.

A float loses metre resolution at ten thousand kilometres. A double loses
millimetres at a light year. Neither works; the split does, because the integer
carries the magnitude and the double only ever holds a small remainder.

See [UniverseCoordinates.md](UniverseCoordinates.md),
[ADR-001](../ADR/ADR-001-universe-coordinate-system.md).

### 2. Everything is a function of a seed and an address

There is no world generation step and no world file. A star, its planets, their
terrain, climate, biomes, trees and weather are all computed from
`(universe seed, address)` on demand, identically on every machine.

The seed hierarchy gives each level its own stream, so adding a field to a
planet cannot change where the stars are.

See [ProceduralGeneration.md](ProceduralGeneration.md),
[ADR-002](../ADR/ADR-002-seed-hierarchy.md).

### 3. What a player changes is a sparse delta

```text
procedural base world  +  sparse deltas  =  the current world
```

A placed structure is a row. A chopped tree is a *tombstone* - sixteen bytes
saying "not this one" - because the generator can still produce the tree and the
only new information is that it should not appear.

Identity is derived from the address the generator placed it at, never from an
index in an output array. Nothing with process or configuration lifetime may
participate in a persistent identity; a vegetation *budget* briefly did, and a
chopped tree came back.

See [WorldPersistence.md](WorldPersistence.md),
[PersistentEntityIdentity.md](PersistentEntityIdentity.md),
[ADR-006](../ADR/ADR-006-delta-persistence.md).

### 4. Relevance, not loading

Nothing is ever created or destroyed - it is always reconstructible - so there
is only a sliding amount of representation:

```text
Unknown -> DescriptorOnly -> DistantVisual -> NearbyVisual -> Prewarming -> Active
```

Exactly one system may be `Active`, because `Active` owns the simulation frame,
and two of those would be two answers to "which way is down".

See [StarSystemStreaming.md](StarSystemStreaming.md),
[SimulationFrames.md](SimulationFrames.md).

### 5. Replicate almost nothing

Both ends regenerate the same universe from the same seed. What crosses the wire
is identity, dynamic state, persistent deltas, and who is nearby.

Canonical positions replicate as *data*, never as transforms - no two clients
share a render origin, so a transform is a statement in somebody else's
coordinate space.

See [Networking.md](Networking.md),
[ADR-008](../ADR/ADR-008-server-authority-and-replication.md).

## The runtime pieces

| Piece | Owns |
|---|---|
| `UUniverseWorldSubsystem` | The render origin, the seed, the simulation frame, gravity |
| `UStarSystemStreamingSubsystem` | Which systems have anything real behind them |
| `UWorldStateSubsystem` | Persistent deltas; the server-authority seam |
| `UUniverseNetSubsystem` | Who else is here and how relevant they are |
| `AUniverseGameMode` | Picking a home system, placing joining players |
| `AUniverseGameState` | World identity, the shared clock |
| `AUniversePlayerState` | One player's canonical position |
| `AUniversePlayerController` | The authority seam: every client request |
| `AUniverseProbePawn` | The ship. One canonical position and velocity |
| `APlanetCharacter` | On foot, with gravity toward a planet centre |
| `APlanetActor` | One streaming world: terrain, environment, vegetation, structures |

## What is deliberately absent

- **No committed content.** No maps, no meshes beyond engine primitives, no
  materials beyond engine defaults. A clean checkout builds and runs from source
  alone, and the scene is always a faithful view of what the generator produces
  rather than something frozen into a map.
- **No world generation step.** Nothing to bake, nothing to ship, nothing to
  version except the generator itself.
- **No cache that affects results.** Caches exist (galaxy lookup, terrain
  patches, persistence regions) and every one of them is invisible to output.
- **No hardcoded down vector.** Anywhere. Gravity comes from one subsystem and
  points at a planet centre.

## Where the bodies are buried

Things that look wrong and are not, or looked right and were not:

- **The universe origin is intergalactic space.** Since galaxies became a
  density function, `FUniversePosition()` has no stars near it. Anything that
  needs a star must ask `FindSystemNear`.
- **`TryGetRelativeCm` is exact only to about 0.01 light years.** That is the
  whole reason the coordinate type has two halves. Galaxy-scale vectors go
  through cell space.
- **Light years in a double resolve to ~70 km at galactic radius.** Fine for a
  density function, useless for anything you stand on.
- **Patch resolution must be `2^p + 1`.** Seam arithmetic is exact only for
  dyadic UVs; any other value puts a one-ULP crack along every patch border.
- **A star needs two directional lights.** One to light the world, one to
  scatter the sky. A single light doing both renders the ground black, because
  atmospheric transmittance is evaluated in single precision at planetary radii.
- **Nothing exists on frame zero.** The world streams in around the player, so
  every "it will be ready by now" assumption is a bug waiting for a slower
  machine.
- **Anything that works because there is exactly one of something** is a bug
  waiting for the second one. One player, one pawn, one viewpoint, one process.

## How it is verified

| Level | What | Cost |
|---|---|---|
| Pure functions | 79 tests, 1.24 M assertions, standalone and in-engine | ~10 s |
| Planetary session | `universe.Journey 1` | ~30 s |
| Interstellar session | `universe.InterstellarJourney` | ~30 s |
| The whole game | `universe.GoldenPath`, then `verify` in a new process | ~60 s |
| Two players | `Tools\Multiplayer\TwoPlayerTest.ps1` | ~2 min |
| Reconnect and restart | `Tools\Multiplayer\PersistenceTest.ps1` | ~4 min |

`Tools\Build\VerifyAll.bat` runs the first four, cheapest first.

## Related

- [DataFlow.md](DataFlow.md) - how a frame actually happens
- [../Testing/GoldenPath.md](../Testing/GoldenPath.md) - the release gate
- [../Testing/FailureModes.md](../Testing/FailureModes.md) - what breaks and how it looks
- [../Performance/Baseline.md](../Performance/Baseline.md)
