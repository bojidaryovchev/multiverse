# Universe

An astronomically large, deterministic, procedural universe built on Unreal
Engine 5.8.

The guiding principle, from [CLAUDE.md](CLAUDE.md):

> The universe should be **enormous logically, tiny computationally wherever
> nobody is present, and extremely detailed around the player.**

Unreal renders and simulates the player's immediate surroundings. The canonical
universe is ours, is vastly larger than any Unreal coordinate space, and is
reconstructed from mathematics rather than stored.

---

## Status

**Sprint 008 - MVP Hardening & Playable Vertical Slice: complete. This is the
first playable build.**

A packaged Windows client, on a machine with no Unreal installation, plays the
whole thing without a single console command: spawn, fly, warp twelve light
years, land on a procedural planet, step out, walk, build, leave - then quit,
come back, and find it all still there.

```text
Tools\Build\VerifyAll.bat

[1/5] Deterministic core tests ...                          passed.
[2/5] The same tests inside Unreal ...                      passed.
[3/5] Planetary journey ...                                 passed.
[4/5] Interstellar journey ...                              passed.
[5/5] Golden Path, then verifying it from a new process ... passed.

ALL VERIFICATION PASSED
```

No release blockers. See [Sprint 008](Docs/Sprints/Sprint-008-Report.md), and
[the Golden Path](Docs/Testing/GoldenPath.md) for what must never break.

<details>
<summary>Sprint 007 - Multiplayer Universe Proof</summary>

**Complete.** Two players connect to a
dedicated server, exist in the same deterministic universe, see each other, land
on the same procedural planet, and see each other's buildings. Disconnect,
restart the server process, reconnect - and come back to the same cell with the
world intact. The universe itself is never replicated: both ends regenerate it
from the same seed, so only identity, dynamic state, persistent deltas and
nearby players cross the wire.

```text
Tools\Multiplayer\TwoPlayerTest.ps1        ALL CHECKS PASSED
Tools\Multiplayer\PersistenceTest.ps1      ALL CHECKS PASSED
```

</details>

<details>
<summary>Sprint 006 - Interstellar &amp; Galactic Travel</summary>

**Complete.** Fly from one star
system to another at three million times the speed of light, arrive, land, and
come back to find the first system regenerated to the identical content hash -
after every actor in it had been destroyed and rebuilt from its address. Leave
the galactic disk and the stars stop existing, because a galaxy is a density
function rather than a container. 79 automated tests (1,241,356 assertions) pass
identically standalone and in-engine.

```text
universe.InterstellarJourney

  Result        : PASS
  Home          : Zarelra-1252 (0x7AABB69FDC5E9456)
  Destination   : Aelonis-3673 (0xB0D054604A74A988)
  Leg distance  : 12.7140 ly
  Outbound      : 136.2 s      Return: 136.3 s
  Peak speed    : 3.336e+06 c
  Hazard stops  : 3 during the run
  Streaming     : 19 tracked, 18 generated, 31 transitions
```

</details>

No main menu, no audio, placeholder visuals, and one active star system at a
time. See the sprint reports for exactly what was built and validated, and what was not:
[Sprint 001](Docs/Sprints/Sprint-001-Report.md) ·
[Sprint 002](Docs/Sprints/Sprint-002-Report.md) ·
[Sprint 003](Docs/Sprints/Sprint-003-Report.md) ·
[Sprint 004](Docs/Sprints/Sprint-004-Report.md) ·
[Sprint 005](Docs/Sprints/Sprint-005-Report.md) ·
[Sprint 006](Docs/Sprints/Sprint-006-Report.md) ·
[Sprint 007](Docs/Sprints/Sprint-007-Report.md) ·
[Sprint 008](Docs/Sprints/Sprint-008-Report.md).

---

## Requirements

| | |
| --- | --- |
| Unreal Engine | 5.8.x |
| Compiler | Visual Studio 2022 Build Tools, MSVC 14.4x, "Desktop development with C++" |
| .NET Framework SDK | 4.6+ — VS component `Microsoft.Net.Component.4.8.SDK`. Unreal hard-requires it for any Editor target (`SwarmInterface.Build.cs` throws without it), and it is easy to miss on a Build Tools install. The Game target does not need it. |
| Windows SDK | 10.0.26100 or newer |
| Git | with Git LFS installed (`git lfs install`) |
| .NET | 8 or newer (UnrealBuildTool) |

The core tests need **only MSVC** - no engine required.

## Getting started

```bat
git clone <repo>
cd multiverse
git lfs install

REM 1. Verify the deterministic core. Takes about ten seconds, needs no engine.
Tools\StandaloneTests\RunTests.bat

REM 2. Generate the Unreal project files, then build.
REM    Right-click Universe.uproject -> "Generate Visual Studio project files",
REM    or run the engine's GenerateProjectFiles script against it.
REM    Then open Universe.sln and build the "UniverseEditor" target (Development Editor).

REM 3. Run.
REM    Open Universe.uproject and press Play.
```

There is no committed content. The whole test scene is built from code at
`StartPlay`, so a clean checkout builds and runs from source alone. If
`/Engine/Maps/Entry` is unavailable in your engine install, create any empty
level and set it as the default map in `Config/DefaultEngine.ini` - the game
mode does the rest.

## Controls

| Key | Action |
| --- | --- |
| `W` / `S` | Thrust forward / reverse |
| `A` / `D` | Strafe |
| `Q` / `E` | Lift down / up |
| `Z` / `C` | Roll |
| Mouse | Pitch and yaw |
| `Space` | Brake (flying) / jump (on foot) |
| `[` `]` or mouse wheel | Thrust tier, x10 per step, 15 tiers |
| `G` | Warp jump 5 light years forward, in whole cells |
| `F` | Step out of a landed ship, or board one you are standing beside |
| `F1` | Toggle the diagnostics overlay |

On foot: `W A S D` to walk, `Left Shift` to sprint, `Space` to jump.

The probe starts beside a fully streaming procedural planet. Use
`universe.GotoAltitude` to drop straight to a given altitude rather than flying
down by hand.

### Debug console commands

| Command | Effect |
| --- | --- |
| `universe.ShowDebug 0` | Hide the diagnostics overlay |
| `universe.AutoPilot 1` | Hold full forward thrust with nobody at the controls |
| `universe.AutoPilotTier <n>` | Thrust tier the autopilot forces |
| `universe.WarpJump <ly>` | Jump forward N light years in whole cells |
| `universe.LogStateInterval <s>` | Log probe state every N seconds (0 = off) |
| `universe.GotoAltitude <m>` | Place the probe at N metres above the planet, looking down |
| `universe.TerrainDebugMode <0-3>` | Terrain colour: elevation / LOD / cube face / patch checkerboard |
| `universe.TerrainLogInterval <s>` | Log terrain streaming state every N seconds |
| `universe.TerrainStress <cycles>` | Run the scripted orbit/descend/traverse/ascend stress path |
| `universe.ScreenshotAfterSeconds <s>` | Capture once streaming has settled |
| `universe.Land [index]` | Land the ship, here or at a seed-derived surface point |
| `universe.ExitShip` / `universe.EnterShip` | Step out / board, without pressing `F` |
| `universe.GotoSurface <index> [m]` | Teleport to a seed-derived surface point |
| `universe.GotoSubstellar [m] [deg]` | Teleport to a chosen solar elevation |
| `universe.FrameInfo` | Log the frame, all three altitudes, gravity, atmosphere |
| `universe.Journey` | Run and check the full space-to-surface journey |
| `universe.EnvInfo <n>` | The environment here, plus a whole-planet biome survey |
| `universe.GotoBiome <name>` | Find a named biome in daylight and go there |
| `universe.TimeScale <n>` | Accelerate the day/night cycle and weather |
| `universe.Build beacon` | Place a persistent structure where you are looking |
| `universe.ChopTree` | Remove the nearest procedural tree, permanently |
| `universe.PersistenceInfo` | World save state, storage size, this region's contents |

Together these let a long traversal run headless and leave its evidence in the
log, which is how the large-distance behaviour is actually validated:

```bat
UnrealEditor.exe %CD%\Universe.uproject -game -benchmark -benchmarkseconds=40 -fps=30 ^
  -ExecCmds="universe.AutoPilot 1, universe.AutoPilotTier 11, universe.LogStateInterval 5"
```

## Repository layout

```
Config/                     Unreal project configuration
Content/                    (empty - the scene is built from code, not assets)
Docs/
  Architecture/             How things work
    UniverseCoordinates.md    The coordinate system and its numerical analysis
    ProceduralGeneration.md   Determinism, seeding, what gets generated
    PlanetCoordinates.md      Cube-sphere topology, patch addressing, seams
    PlanetTerrain.md          Terrain function, meshing, streaming, threading
    PlanetLOD.md              Screen-space error, hysteresis, balancing, culling
    SimulationFrames.md       Which frame the simulation is in, and why
    PlanetaryGravity.md       Gravity as a field; walking on a sphere
    PlanetaryTraversal.md     Altitudes, swept collision, landing, streaming ahead
    PlanetEnvironment.md      Physics to climate to biomes to content
    ClimateSystem.md          Temperature and moisture fields
    Biomes.md                 The biome table, blending, alien biospheres
    Weather.md                Weather as a function of place and time
    ProceduralVegetation.md   What grows where, and why animals are a number
    WorldPersistence.md       Base world + sparse deltas = current world
    PersistentEntityIdentity.md  Names that survive a restart
    Testing.md                The two test runners and what they cover
  ADR/                      Why things are the way they are
  Sprints/                  Per-sprint reports
Source/
  UniverseCore/             Deterministic mathematics. Depends only on Core.
  UniverseGeneration/       Astronomical generation. Pure data, no Actors.
  UniversePlanet/           Planet maths: cube-sphere, terrain, quadtree, meshing.
  Universe/                 Gameplay: Actors, rendering, input, streaming.
Tools/
  StandaloneTests/          MSVC-only test runner for the core
```

The dependency direction is strict and one-way:

```
Universe  ->  UniversePlanet  ->  UniverseGeneration  ->  UniverseCore
```

None of `UniverseCore`, `UniverseGeneration` or `UniversePlanet` references
`Engine`, an `Actor`, a `World` or a tick. That is what lets them be compiled and
executed without an engine at all - which is how the core is verified, and what
makes it safe to run terrain generation on worker threads.

## The three ideas worth knowing

**1. A position is an integer cell plus a local offset.**

```cpp
struct FUniversePosition
{
    int64     CellX, CellY, CellZ;   // cell size = 2^40 cm ~ 11 million km
    FVector3d Local;                 // centimetres inside that cell, [0, 2^40)
};
```

The large magnitude lives in the integer part, so **precision does not degrade
with distance**: 2.44 micrometre resolution at the origin and 2.44 micrometres
ten billion light years away. The addressable universe is 1.07e13 light years
per axis, roughly 230x the radius of the observable universe.

The cell size is a power of two so that normalisation is *exact* rather than
merely fast. See [ADR-001](Docs/ADR/ADR-001-universe-coordinate-system.md).

**2. A planet is a function, not a mesh.**

Earth's surface is 510 million km squared; at one vertex per 10 m that is 5e15
vertices. So a planet is six cube faces, a quadtree, and a pure function from
direction to elevation - only the patches the observer can perceive ever exist.
From 400 km altitude that is 58 patches; from 200 m, 225.

The cube face basis is built from exact 0 and +/-1 components, which makes
neighbouring faces produce bit-identical directions along a shared edge. Seam
continuity is a property of the construction, not a tolerance. See
[ADR-003](Docs/ADR/ADR-003-planet-topology-and-lod.md).

**3. Where you are decides how physics behaves.**

Scaled astronomical space is a 1e-7 model; terrain is 1:1. They meet at a named
boundary rather than blending, and the boundary has two radii - entered at three
planet radii, left at 4.5 - so a craft parked on it cannot rebase the origin and
restart terrain streaming several times a second.

Inside the planetary frame, gravity is `normalize(centre - position)` with a 1/r^2
falloff. There is no world "down" and there cannot be one: two players on
opposite sides of a world have opposite up vectors and both are right. See
[ADR-004](Docs/ADR/ADR-004-simulation-frames-and-planetary-traversal.md).

**4. A planet's physics decide what lives on it.**

```
radius, mass, orbit, star  ->  temperature, atmosphere, ocean, biosphere
                           ->  climate fields at a point
                           ->  biome classification
                           ->  ground colour, vegetation, wildlife, weather
```

One-way. A forest exists because the conditions support a forest; nothing places
a forest. Biomes are a table of boxes in climate space returning weighted blends,
so there are no hard lines across a world, and an alien biosphere is a second
table rather than a second code path. See
[ADR-005](Docs/ADR/ADR-005-procedural-environment.md).

**5. The world is a function; only the difference is stored.**

```
procedural base world  +  sparse deltas  =  current world
```

A planet has billions of trees and not one of them is a row. Remove one and
sixteen bytes of identity records that it is gone, because the generator can
still produce it and the only new information is that it should not be shown.

Storage therefore scales with player activity, not with universe size: an empty
world is 20 KB, a thousand structures add 221 bytes each, and ten billion
untouched planets cost the same as ten. See
[ADR-006](Docs/ADR/ADR-006-delta-persistence.md).

**6. Content is derived from its address, never stored.**

```
Universe seed -> Sector -> System -> Body -> Surface patch
child = Hash(parent, DOMAIN_TAG, address...)
```

Descent is a pure function, so any region can be generated in isolation, on any
thread, in any order. Leave a system, travel five hundred light years, come
back, and it is identical - with nothing written down in between. See
[ADR-002](Docs/ADR/ADR-002-seed-hierarchy.md).

## Testing

```bat
Tools\StandaloneTests\RunTests.bat            REM MSVC only, ~10s, exit code 0 = pass
Tools\StandaloneTests\RunTests.bat --verbose
```

In the editor: **Tools > Session Frontend > Automation**, filter `Universe`.

Headless:

```bat
UnrealEditor-Cmd.exe %CD%\Universe.uproject ^
  -ExecCmds="Automation RunTests Universe; Quit" -unattended -nopause -nullrhi -log
```

The same test bodies run in both. See [Docs/Architecture/Testing.md](Docs/Architecture/Testing.md).

## Source control

Git LFS is configured from the first commit for `.uasset`, `.umap`, textures,
meshes, audio and other binary types - see `.gitattributes`. Retrofitting LFS
later requires a history rewrite, so it is set up before any binary content
exists.

Generated directories (`Binaries/`, `Build/`, `Intermediate/`, `Saved/`,
`DerivedDataCache/`) are never committed. A clean checkout regenerates all of
them.

## For AI agents working on this repository

Read [CLAUDE.md](CLAUDE.md) first - it is the master specification. Then
[Docs/AgentHandoff.md](Docs/AgentHandoff.md), which is what eight sprints of
building against it actually taught.

Then, before changing anything in `Source/UniverseCore` or
`Source/UniverseGeneration`:

1. Read [ADR-001](Docs/ADR/ADR-001-universe-coordinate-system.md),
   [ADR-002](Docs/ADR/ADR-002-seed-hierarchy.md),
   [ADR-003](Docs/ADR/ADR-003-planet-topology-and-lod.md) and
   [ADR-004](Docs/ADR/ADR-004-simulation-frames-and-planetary-traversal.md) and
   [ADR-005](Docs/ADR/ADR-005-procedural-environment.md) and
   [ADR-006](Docs/ADR/ADR-006-delta-persistence.md) and
   [ADR-007](Docs/ADR/ADR-007-galaxy-density-and-system-streaming.md) and
   [ADR-008](Docs/ADR/ADR-008-server-authority-and-replication.md).
2. Run `Tools\StandaloneTests\RunTests.bat` before and after.
3. Understand that the cell size, the domain tags, the hash constants,
   `PlanetTerrainVersion`, `PlanetEnvironmentVersion` and
   `GalaxyGeneratorVersion` are **frozen**. Changing any of them regenerates the
   universe and invalidates every save.
4. Patch resolution must be `2^p + 1`. This is not a style preference: seam
   arithmetic is exact only for dyadic UVs, and any other value puts a one-ULP
   crack along every patch border on the planet.
5. Never let anything with process or configuration lifetime participate in a
   persistent identity - not an array index, not a tuning value, not a budget.
   A vegetation budget briefly did, and a chopped tree came back. See
   [PersistentEntityIdentity.md](Docs/Architecture/PersistentEntityIdentity.md).
6. Never write a hardcoded "down" vector, and never say "altitude" without
   saying which of the three you mean. Both rules exist because breaking them
   produces bugs that look fine near the origin and fail over the horizon. See
   [PlanetaryGravity.md](Docs/Architecture/PlanetaryGravity.md).
7. Never introduce a generation input with process lifetime - pointers,
   `UObject` IDs, `FName` indices, map iteration order, time. The list and the
   reasoning are in
   [ProceduralGeneration.md](Docs/Architecture/ProceduralGeneration.md).
8. The universe origin is intergalactic space and contains no stars. Anything
   that needs a star to start from must ask
   `FStarSystemGenerator::FindSystemNear` rather than searching from
   `FUniversePosition()`. See
   [GalaxyGeneration.md](Docs/Architecture/GalaxyGeneration.md).
9. The world streams in around the player, so nothing exists on frame zero.
    Any assumption that something "will be ready by now" is a bug waiting for a
    slower machine or a larger planet; `universe.After <seconds> <command>`
    exists for scripted runs. See
    [StarSystemStreaming.md](Docs/Architecture/StarSystemStreaming.md).
10. Anything that works because there is exactly one of something is a bug
    waiting for the second one. One player, one pawn, one viewpoint, one
    process, one log file - Sprint 007 found three defects of exactly that
    shape in one afternoon. See
    [Networking.md](Docs/Architecture/Networking.md).
11. Never replicate anything both ends can compute, and never replicate a
    transform. Two clients do not share a render origin, so a transform is a
    statement in somebody else's coordinate space; canonical positions are the
    only thing that means the same on both machines.

Record significant architectural decisions as new ADRs.
