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

**Sprint 003 - Seamless Space to Surface Traversal: complete.** Fly in from deep
space, enter the atmosphere, land, step out and walk around - with no loading
screen and no discontinuity anywhere. Gravity points at the planet centre from
everywhere on it, and a craft at 10^12 m/s cannot pass through a world.
57 automated tests (625,064 assertions) pass identically standalone and
in-engine, and the whole journey runs unattended and checks itself.

![Standing on a planet](Docs/Sprints/Sprint-003/Surface-Daylight.png)

*The same planet from 400 km:*

![Terrain from orbit](Docs/Sprints/Sprint-003/Terrain-FromOrbit.png)

No water, biomes, vegetation, weather, wildlife, persistence or multiplayer yet;
other bodies are not visible from a surface, and day and night are a function of
where you stand rather than of time. See the sprint reports for exactly what was
built and validated, and what was not:
[Sprint 001](Docs/Sprints/Sprint-001-Report.md) ·
[Sprint 002](Docs/Sprints/Sprint-002-Report.md) ·
[Sprint 003](Docs/Sprints/Sprint-003-Report.md).

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

**4. Content is derived from its address, never stored.**

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

Read [CLAUDE.md](CLAUDE.md) first - it is the master specification.

Then, before changing anything in `Source/UniverseCore` or
`Source/UniverseGeneration`:

1. Read [ADR-001](Docs/ADR/ADR-001-universe-coordinate-system.md),
   [ADR-002](Docs/ADR/ADR-002-seed-hierarchy.md),
   [ADR-003](Docs/ADR/ADR-003-planet-topology-and-lod.md) and
   [ADR-004](Docs/ADR/ADR-004-simulation-frames-and-planetary-traversal.md).
2. Run `Tools\StandaloneTests\RunTests.bat` before and after.
3. Understand that the cell size, the domain tags, the hash constants and
   `PlanetTerrainVersion` are **frozen**. Changing any of them regenerates the
   universe and invalidates every save.
4. Patch resolution must be `2^p + 1`. This is not a style preference: seam
   arithmetic is exact only for dyadic UVs, and any other value puts a one-ULP
   crack along every patch border on the planet.
5. Never write a hardcoded "down" vector, and never say "altitude" without
   saying which of the three you mean. Both rules exist because breaking them
   produces bugs that look fine near the origin and fail over the horizon. See
   [PlanetaryGravity.md](Docs/Architecture/PlanetaryGravity.md).
6. Never introduce a generation input with process lifetime - pointers,
   `UObject` IDs, `FName` indices, map iteration order, time. The list and the
   reasoning are in
   [ProceduralGeneration.md](Docs/Architecture/ProceduralGeneration.md).

Record significant architectural decisions as new ADRs.
