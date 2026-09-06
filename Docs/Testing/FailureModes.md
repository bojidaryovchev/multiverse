# Failure Modes

*Sprint 008. What goes wrong in this project, what it looks like from the
outside, and what it usually is.*

This is written from defects that actually happened. Every entry below was found
by running something, not by reading code, and most of them looked like
something else first.

## The general shape

Three patterns account for nearly every defect across eight sprints:

1. **A number that is a plausible number.** Failure paths that return a
   fallback, precision limits that silently truncate, quantities measured in the
   wrong frame. Nothing crashes and nothing warns; the value is simply wrong and
   looks fine.
2. **Something that works because there is exactly one of it.** One player, one
   pawn, one viewpoint, one process, one log file.
3. **An assumption that something is ready.** True on a fast machine, on a small
   planet, in a single process, on the second frame.

## By symptom

### The universe is empty

**Seen:** every generation test failing at once; `PlanetSurfaceDescriptor`
faulting on `Planets[0]`.

**Usually:** something is searching from `FUniversePosition()`. Since galaxies
became a density function, the universe origin is intergalactic space and has no
stars anywhere near it.

**Check:** `universe.GalaxyInfo`. If it says "intergalactic space", that is the
answer. Use `FStarSystemGenerator::FindSystemNear`.

**Also seen once as:** stellar density reporting zero *inside* a galaxy, because
`ToGalaxyLocal` used `TryGetRelativeCm`, which is exact only to ~0.01 light
years, and took its documented "astronomically far" fallback on every call.

### The ship flies through a planet or a star

**Usually:** a movement path that is not swept. At warp a frame is longer than a
star, so a position-based check sees empty space at both ends.

**Check:** the hazard-stop count on the HUD or in `universe.TravelInfo`. **A
count that never moves across a session spent flying through crowded space is a
swept check that has quietly stopped running** - which is exactly why the number
is on the HUD.

### The ship arrives and keeps going

**Usually:** braking distance is being computed but the step is not clamped. The
law `v^2/2a` is correct in the limit, and on the last frames of a warp approach
the ship is doing 10^12 m/s inside an arrival radius of 10^11 m - it enters and
leaves inside one frame.

**Check:** `bArrivalClamped` in `FTravelStepResult`.

### The player falls through the planet

**Usually:** collision has not been cooked under them. Terrain that renders is
not terrain you can stand on: a patch is selected, generated, uploaded and only
then cooked, and in the window between the last two it is visible and
intangible.

**Guarded by:** `APlanetCharacter` withholds gravity until
`HasCollisionAt` is true. Being held in place for a fraction of a second is a
visibly odd state that corrects itself; falling through a planet is not
recoverable.

**Seen as:** a character reporting zero metres walked at every waypoint - held
for the entire stage because the test moved it faster than collision could
follow.

### The screen is black

**Usually:** lighting, and specifically the atmosphere. A single directional
light doing both jobs renders the ground black, because the sky atmosphere's sun
transmittance is evaluated in single precision and collapses at planetary radii.

**Fixed by:** two lights - one that lights the world with
`bAtmosphereSunLight = false`, one that scatters the sky with every lighting
channel off.

**Note:** no automated test can catch this. Everything runs `-nullrhi`. It took
screenshots.

### A chopped tree comes back

**Usually:** something with process or configuration lifetime got into a
persistent identity. It happened with a vegetation *budget*: the scatter grid
depended on `MaxInstances`, so a tree chopped at one budget was a different tree
at another.

**Rule:** nothing that can differ between two runs of the same build may
participate in an identity. Not an index, not a tuning value, not a budget.

**Also seen as:** a race - a chopped tree reappearing on the first visit after a
restart, because vegetation was placed before the region's removals had loaded.
Fixed by subscribing to `OnRegionLoaded`.

### A seam or a crack in the terrain

**Usually:** a patch resolution that is not `2^p + 1`. Seam arithmetic is exact
only for dyadic UVs; any other value puts a one-ULP crack along every patch
border on the planet.

Guarded by an assertion and a test.

### Two players cannot see each other

**Seen:** both players reporting no star system at all, while standing on a
planet.

**Usually:** something claimed the tracked anchor that should not have. Every
replicated pawn used to claim it in `BeginPlay`, so another player's pawn - never
placed locally, therefore at the origin - took over this client's viewpoint and
the streamer went to look at intergalactic space.

**Check:** `universe.NetPlayers` on each machine. It prints in a fixed format
precisely so two logs can be diffed. **A multiplayer bug is almost never visible
from one side**: "the other player is in the wrong place" is, from each
machine's point of view, a claim that the *other* machine is wrong.

### A client can do something it should not

**Check:** every write goes through `AUniversePlayerController`. There is no
second path. `universe.NetTeleportTest` makes the movement rejection fire on
demand - **a security check nobody has ever seen fire is a check that might not
work.**

### A reconnecting player starts over

**Usually:** the persistent id is not stable. Three separate causes have been
seen:

- A fresh GUID per connection - stable and useless.
- `GetPlayerName()` read at controller `BeginPlay` - empty, because login has
  not run.
- `GetPlayerName()` after login - a generated nickname that differs every
  session. `?Name=` on the URL is no better; the engine rewrites it.

Identity comes from `?PlayerId=`, read in `InitNewPlayer`.

### Nothing happens when a console command runs

**Usually:** it ran on frame zero, before the world streamed in. Use
`universe.After <seconds> <command>`.

**Also:** `-PlayerName` is not an Unreal switch and is silently ignored.

### The ship lands under water

**Usually:** it landed where it was. On a world two thirds ocean that is the sea
floor - the first automated Golden Path run landed 6,246 m down and then could
not build anything.

**Now:** landing searches outward in geometric rings for dry ground and says how
far it moved. If a world is all ocean it refuses and says so.

### Storage reads get slower over a session

**Usually:** the WAL is not being checkpointed. It reached 26 ms at 1,100
records before `PRAGMA wal_checkpoint(PASSIVE)` every 256 writes; it is 1.7 ms
now.

### A save will not open

**Check the log.** A schema older than this build migrates in place if the
change was additive. A schema *newer* than this build is refused, deliberately:
the one thing worse than failing to open a world is opening it and dropping the
half it does not understand.

A universe seed mismatch is always refused. Deltas from one universe must never
be applied to another.

## What no test here can catch

- **Anything visual.** Every automated run is `-nullrhi`.
- **Anything about feel.** Whether warp is satisfying, whether the ship handles
  well, whether a planet is interesting to stand on.
- **Cross-platform determinism.** Star and galaxy generation use `libm`
  transcendentals, so two platforms may disagree. Terrain and climate are
  transcendental-free, which is where it matters most - those decide ground
  somebody has built on. Open risk since ADR-002.
- **Long-run memory behaviour** beyond the soak tests actually run.

## Related

- [GoldenPath.md](GoldenPath.md) - the release gate
- [../Architecture/MVPArchitecture.md](../Architecture/MVPArchitecture.md)
- [../Performance/Baseline.md](../Performance/Baseline.md)
