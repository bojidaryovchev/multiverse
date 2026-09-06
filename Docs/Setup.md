# Setup

*How to get from a clean machine to a running universe.*

## If you only want to play

Download or build a packaged client and run it:

```text
Builds\Client\Windows\Universe.exe
```

Nothing else is required - no Unreal installation, no editor, no configuration.

**Single player** is the default. **Multiplayer**:

```bat
Universe.exe 127.0.0.1:7777?PlayerId=ada
```

The `PlayerId` is what makes you the same person when you reconnect. Pick
anything; it is how the server recognises you and where it files your position.

### Controls

| Key | In the ship | On foot |
|---|---|---|
| `W A S D` | fly | walk |
| `Q E` | up / down | - |
| `Z C` | roll | - |
| mouse | look | look |
| `Space` | brake | jump |
| `Shift` | - | run |
| `L` | land | - |
| `T` | target the star ahead | - |
| `N` | next target | - |
| `J` | engage / disengage warp | - |
| `F` | step outside (when landed) | board the ship |
| `B` | - | build |
| `X` | - | clear a tree |
| `F1` | developer diagnostics | developer diagnostics |
| `` ` `` | console | console |

The HUD tells you what to press next. Nothing in ordinary play needs the
console.

## If you want to build it

### Requirements

| | |
|---|---|
| Unreal Engine | 5.8.x |
| Compiler | Visual Studio 2022 Build Tools, MSVC 14.4x, "Desktop development with C++" |
| .NET Framework SDK | 4.6+ (`Microsoft.Net.Component.4.8.SDK`) - Unreal hard-requires it for any Editor target |
| Windows SDK | 10.0.26100 or newer |
| Git | with Git LFS (`git lfs install`) |
| .NET | 8 or newer, for UnrealBuildTool |

**The core tests need only MSVC.** No engine, no editor, no .NET Framework.

### First run

```bat
git clone <repo>
cd multiverse
git lfs install

REM 1. Verify the deterministic core. Ten seconds, no engine required.
Tools\StandaloneTests\RunTests.bat

REM 2. Build the editor target.
"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" ^
    UniverseEditor Win64 Development -Project="%CD%\Universe.uproject"

REM 3. Verify everything.
Tools\Build\VerifyAll.bat

REM 4. Package a client.
Tools\Build\Package.bat Development
```

Step 1 is the one to run first and the one to run most often. It compiles three
quarters of the project against a small shim and runs 1.24 million assertions in
about ten seconds, on any machine with a C++ compiler.

### Running a server

```bat
Tools\Multiplayer\RunServer.bat 7777
Tools\Multiplayer\RunClient.bat 127.0.0.1:7777 Ada
```

The server runs headless from the editor binary. `Source\UniverseServer.Target.cs`
is a correct dedicated-server target and is what a **source-built** engine should
use; the Epic Games Launcher build of UE 5.8 cannot compile one at all -
UnrealBuildTool refuses with "Server targets are not currently supported from
this engine distribution". The net mode, the authority model and the code are
identical either way; only the executable differs.

### Two-player and persistence tests

```bat
powershell -File Tools\Multiplayer\TwoPlayerTest.ps1
powershell -File Tools\Multiplayer\PersistenceTest.ps1
```

Both take minutes rather than seconds - they use real sockets and real elapsed
time - and both print a verdict.

## Where things are

```text
Source/UniverseCore/          coordinates, seeds, hashing, geometry
Source/UniverseGeneration/    galaxies, star systems, interstellar travel
Source/UniversePlanet/        terrain, climate, biomes, vegetation, weather
Source/Universe/              the only module that knows about Unreal

Tools/StandaloneTests/        the engine-free test harness
Tools/Multiplayer/            server, client and the multiplayer test scripts
Tools/Build/                  VerifyAll.bat and Package.bat

Docs/Architecture/            how each system works and why
Docs/ADR/                     decisions, with their prices
Docs/Sprints/                 what each sprint built and what broke
Docs/Testing/                 the Golden Path and the failure catalogue
Docs/Performance/             the measured baseline

Saved/WorldState/             the world databases - one per universe seed
```

## Configuration

There is very little, on purpose.

| Setting | Where | Default |
|---|---|---|
| Universe seed | `AUniverseGameMode::UniverseSeedText` | `"sprint-001"` |
| Travel speeds | `FTravelProfile` | see InterstellarTravel.md |
| Streaming thresholds | `UStarSystemStreamingSubsystem` | see StarSystemStreaming.md |
| Terrain budgets | `UPlanetTerrainComponent` | see the Baseline |

Changing the seed makes a different universe and orphans any existing world
database, which is refused at open rather than silently applied.

**Do not change** the cell size, the domain tags, the hash constants, or any
`*Version` constant. Each of them regenerates the universe and invalidates every
save; each is a named constant with that sentence written next to it.

## Troubleshooting

| Symptom | Cause |
|---|---|
| `RunTests.bat` cannot find a compiler | Visual Studio Build Tools without "Desktop development with C++". |
| Editor target fails on `SwarmInterface` | Missing .NET Framework 4.8 SDK. The Game target does not need it. |
| `Server targets are not currently supported` | Expected on a launcher engine. Use `RunServer.bat`. |
| A console command does nothing at startup | It ran on frame zero. Use `universe.After <seconds> <command>`. |
| The world will not open | Read the log: seed mismatch, or a schema newer than this build. |
| Everything is empty | You are in intergalactic space. `universe.GalaxyInfo`. |

## Related

- [Architecture/MVPArchitecture.md](Architecture/MVPArchitecture.md)
- [Testing/GoldenPath.md](Testing/GoldenPath.md)
- [AgentHandoff.md](AgentHandoff.md)
