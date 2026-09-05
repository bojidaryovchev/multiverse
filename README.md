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

**Sprint 001 - Universe Foundation: complete.** The coordinate system, the
deterministic seed hierarchy, astronomical generation and a flyable probe.
Builds and runs against UE 5.8.2; 25 automated tests (144,024 assertions) pass
both standalone and in-engine.

![The running prototype](Docs/Sprints/Sprint-001-Screenshot.png)

No planet terrain, vegetation, weather, wildlife, buildings, persistence or
multiplayer yet. See [Docs/Sprints/Sprint-001-Report.md](Docs/Sprints/Sprint-001-Report.md)
for exactly what was built and validated, and what was not.

---

## Requirements

| | |
| --- | --- |
| Unreal Engine | 5.8.x |
| Compiler | Visual Studio 2022 Build Tools, MSVC 14.4x, "Desktop development with C++" |
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
| `Space` | Brake |
| `[` `]` or mouse wheel | Thrust tier, x10 per step, 15 tiers |
| `G` | Warp jump 5 light years forward, in whole cells |
| `F1` | Toggle the diagnostics overlay |

Console: `universe.ShowDebug 0` also hides the overlay.

## Repository layout

```
Config/                     Unreal project configuration
Content/                    (empty - Sprint 001 commits no binary assets)
Docs/
  Architecture/             How things work
    UniverseCoordinates.md    The coordinate system and its numerical analysis
    ProceduralGeneration.md   Determinism, seeding, what gets generated
    Testing.md                The two test runners and what they cover
  ADR/                      Why things are the way they are
  Sprints/                  Per-sprint reports
Source/
  UniverseCore/             Deterministic mathematics. Depends only on Core.
  UniverseGeneration/       Astronomical generation. Pure data, no Actors.
  Universe/                 Gameplay: Actors, rendering, input.
Tools/
  StandaloneTests/          MSVC-only test runner for the core
```

The dependency direction is strict and one-way:

```
Universe  ->  UniverseGeneration  ->  UniverseCore
```

`UniverseCore` and `UniverseGeneration` never reference `Engine`, an `Actor`, a
`World` or a tick. That is what lets them be compiled and executed without an
engine at all, which is how the core is verified.

## The two ideas worth knowing

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

**2. Content is derived from its address, never stored.**

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

1. Read [ADR-001](Docs/ADR/ADR-001-universe-coordinate-system.md) and
   [ADR-002](Docs/ADR/ADR-002-seed-hierarchy.md).
2. Run `Tools\StandaloneTests\RunTests.bat` before and after.
3. Understand that the cell size, the domain tags and the hash constants are
   **frozen**. Changing any of them regenerates the universe and invalidates
   every save.
4. Never introduce a generation input with process lifetime - pointers,
   `UObject` IDs, `FName` indices, map iteration order, time. The list and the
   reasoning are in
   [ProceduralGeneration.md](Docs/Architecture/ProceduralGeneration.md).

Record significant architectural decisions as new ADRs.
