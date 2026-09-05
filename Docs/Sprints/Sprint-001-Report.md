# Sprint 001 - Universe Foundation

**Date:** 2026-09-05, updated 2026-09-06
**Status:** **Complete.** UE 5.8.2 was installed on 2026-09-06; the project
builds, runs, and all acceptance criteria are now verified.

---

## 1. Environment (Phase A findings)

Inspected before starting:

| | Found |
| --- | --- |
| Unreal Engine | Initially **not installed**; UE **5.8.2** (`++UE5+Release-5.8`) installed partway through the sprint at `C:\Program Files\Epic Games\UE_5.8`. |
| Unreal MCP | **Not available.** The only MCP server connected is `openseo` (an SEO toolset, unrelated to this project). |
| Compiler | Visual Studio 2022 Build Tools 17.14.39, MSVC 14.44.35207 (x64) |
| Windows SDK | 10.0.26100.0 |
| .NET | 9.0.305 |
| Git | 2.51.0.windows.1 |
| Git LFS | 3.7.0 |
| Repository | Empty - `main` with no commits, containing only `CLAUDE.md` and `initial-prompt.md` |

The sprint began with no engine available, so it proceeded on two tracks:

1. Write the complete UE 5.8 project - modules, build rules, config, gameplay
   code - so that it is ready to build the moment an engine is present.
2. Make the deterministic core **genuinely executable without an engine**, so
   the mathematics that everything else rests on is actually verified rather
   than merely asserted.

Track 2 is what makes this report able to distinguish "tested" from "written",
and it paid for itself twice over: when the engine did arrive, the first real
build took 87 seconds and produced only two defects, both in code the harness
structurally could not reach.

One further environment gap appeared when the engine was installed: the VS 2022
Build Tools lacked the .NET Framework SDK, which Unreal hard-requires (a bare
`throw` in `SwarmInterface.Build.cs`) for every Editor-type target. The Game
target does not depend on it, so all three modules were compiled and validated
before that was resolved by adding `Microsoft.Net.Component.4.8.SDK`.

---

## 2. Implemented

### Repository (Phase A)

- Git repository initialised with **Git LFS configured from the first commit**
  for `.uasset`, `.umap`, textures, meshes, audio, fonts and archives.
  Retrofitting LFS later requires a history rewrite, so it is set up before any
  binary content exists.
- `.gitignore` excludes every generated Unreal directory (`Binaries/`, `Build/`,
  `Intermediate/`, `Saved/`, `DerivedDataCache/`, IDE projects). A clean
  checkout regenerates all of them.
- `README.md`, `Docs/Architecture/`, `Docs/ADR/`, `Docs/Sprints/`.
- Strict one-way module dependency: `Universe -> UniverseGeneration -> UniverseCore`.

### Universe coordinates (Phases B, C)

`FUniversePosition` - `int64` cell index per axis plus a `double` local offset
in centimetres, canonically normalised to `[0, CELL_SIZE)`.

- **Cell size `2^40` cm** (1,099,511,627,776 cm; ~11 million km; 0.0735 AU).
- **Universe half-extent `2^103` cm = 1.07e13 light years per axis** - about
  230x the radius of the observable universe.
- **Local resolution 2.44 micrometres, everywhere**, independent of distance
  from the origin.

The power of two is the load-bearing choice: it makes normalisation *exact*
rather than merely fast, because multiplying by `2^-40` only adjusts an
exponent. A decimal cell size would make every position path-dependent, which
would destroy determinism at the foundation. The full analysis, including the
alternatives rejected, is in
[ADR-001](../ADR/ADR-001-universe-coordinate-system.md).

Implemented: floor-semantics normalisation, cm and metre displacement,
whole-cell jumps with overflow refusal, relative vectors with an explicit range
limit, distance computed in cell space (so it cannot overflow at galactic
scale), sector addressing, exact equality, stable 64-bit hashing, a fixed
48-byte little-endian wire format, and debug formatting.

### Deterministic generation (Phase D)

- `UniverseHash` - SplitMix64, verified against published test vectors.
- `FUniverseRandom` - PCG-XSH-RR 64/32, with Lemire's unbiased bounded integers
  and exact power-of-two scaling for `[0,1)` doubles.
- `FUniverseSeedHierarchy` - Universe -> Sector -> System -> Body -> Surface
  patch, every level domain-tagged, every descent a pure function of
  `(parent, tag, address)`.
- Per-aspect sub-streams, so adding a property to a planet later cannot shift
  the values of existing ones.
- `FStarSystemGenerator` - sector population matched to real stellar density
  (~0.004 stars/ly^3), IMF-weighted spectral classes, main-sequence
  mass-luminosity relations, frost-line-derived planet types, and mass/gravity
  derived from radius and density so a planet's numbers are mutually consistent.
- `FUniverseSystemId` - address-derived stable identity, not a GUID or pointer.

### Space prototype and diagnostics (Phases E, F, H)

All of the following is built, running and exercised in-engine:

- `UUniverseWorldSubsystem` - owns the render origin and the seed, rebases when
  the tracked viewpoint drifts past 10 km, shifting every anchored actor by the
  same delta in the same frame.
- `UUniverseAnchorComponent` - gives an Actor a canonical position; the Unreal
  transform is a derived value, never the source of truth. Hides its actor when
  out of representable range rather than placing it at a fabricated location.
- `AUniverseProbePawn` - 6DOF probe integrating velocity into its universe
  position, 15 thrust tiers (x10 each), a speed cap derived from the exact
  normalisation limit, and `WarpJump` using whole-cell integer arithmetic for
  travel too large for velocity integration.
- `AAstronomicalBodyActor` - placeholder spheres in scaled space.
- `AUniverseHUD` - the Phase H overlay plus labelled body markers, behind
  `universe.ShowDebug` (F1).
- Debug traversal harness: `universe.AutoPilot`, `universe.AutoPilotTier`,
  `universe.WarpJump <ly>` and `universe.LogStateInterval`, so a long
  large-distance run executes headless and leaves its evidence in the log.
- `AUniverseGameMode` - builds the entire scene from the generator at
  `StartPlay`. **No committed content of any kind**: no level, no assets, no
  input assets. A clean checkout builds and runs from source alone.

### The two render spaces

Rather than pretending the universe is small, there are two spaces:

- **Local** - 1:1 with reality, for anything the player touches (the probe).
- **ScaledAstronomical** - a *uniform* linear scale model. Positions and radii
  share one factor, so the model is geometrically exact: angular sizes and
  parallax are the real ones, and a star subtends precisely the angle it truly
  would from that distance.

Scaling distance and radius by different factors would have made the picture a
lie that later level design would bake in. The consequence is honest and worth
stating: from a vantage point framing a whole solar system, planets are dots -
exactly as they are from real interplanetary space. The probe therefore starts a
few planetary radii from the outermost planet, and the HUD marks the rest, so
bodies are findable without inflating them.

---

## 3. Validation - what was actually executed

### Builds (UE 5.8.2, MSVC 14.44.35207, Windows SDK 10.0.26100)

| Target | Result |
| --- | --- |
| `UniverseEditor Win64 Development` | **Succeeded**, zero warnings |
| `Universe Win64 Development` (game) | **Succeeded**, zero warnings, `Binaries\Win64\Universe.exe` |

Produces `UnrealEditor-UniverseCore.dll`, `UnrealEditor-UniverseGeneration.dll`
and `UnrealEditor-Universe.dll`.

### Automated tests - both runners, identical results

```
Tools\StandaloneTests\RunTests.bat        ->  25/25 tests, 144024/144024 assertions, exit 0
Automation RunTests Universe (in-engine) ->  25 succeeded, 0 failed, 144024 assertions, 0 failed
```

The two runners agreeing exactly is itself a result: the same test bodies give
the same answers against the standalone shim and against Unreal's real
`FVector3d`, `FString`, `TArray` and `FMath`.

Standalone build flags: `/std:c++20 /O2 /fp:strict /W4 /WX` - warnings as
errors, strict IEEE-754, zero warnings. Also verified from a clean `git clone`
and with `cl.exe` absent from `PATH`, confirming the MSVC auto-detection.

### Large-distance traversal, in-engine (Phase F)

Run headless via the debug autopilot:

```
UnrealEditor.exe <project> -game -benchmark -benchmarkseconds=40 -fps=30
  -ExecCmds="universe.AutoPilot 1, universe.AutoPilotTier 11, universe.LogStateInterval 5"
```

```
cell=[-1977860,-1608094,19228] unreal=|0.0|cm rebases=151  speed=3336c  nearest=Corokar-4013@0.000513ly
cell=[-1977447,-1607949,19104] unreal=|0.0|cm rebases=301  speed=3336c  nearest=Corokar-4013@0.001042ly
cell=[-1977034,-1607805,18980] unreal=|0.0|cm rebases=451  speed=3336c  nearest=Corokar-4013@0.001570ly
cell=[-1976621,-1607660,18856] unreal=|0.0|cm rebases=601  speed=3336c  nearest=Corokar-4013@0.002099ly
cell=[-1976208,-1607516,18732] unreal=|0.0|cm rebases=751  speed=3336c  nearest=Corokar-4013@0.002627ly
cell=[-1975795,-1607371,18608] unreal=|0.0|cm rebases=901  speed=3336c  nearest=Corokar-4013@0.003156ly
cell=[-1975382,-1607227,18484] unreal=|0.0|cm rebases=1051 speed=3336c  nearest=Corokar-4013@0.003684ly
```

At the 1e12 m/s speed cap (3336c) the global cell index sweeps through hundreds
of thousands of cells while the Unreal transform stays pinned at the origin, and
the proximity query tracks the receding system correctly.

**At normal flight speed** (tier 0) the bounded-drift behaviour is visible
directly - the Unreal position climbs to the rebase radius, snaps back, and
climbs again, while the canonical cell-local offset advances smoothly straight
through the rebase with no discontinuity:

```
unreal=|10144.4|cm  rebases=1
unreal=|250722.2|cm rebases=1
unreal=|641155.6|cm rebases=1
unreal=|811300.1|cm rebases=1
unreal=|0.0|cm      rebases=2      <- rebased at the 1e6 cm radius
unreal=|210144.5|cm rebases=2
unreal=|440288.9|cm rebases=2
```

**Warp jump** (whole-cell integer arithmetic), via `universe.WarpJump 1200`:

```
Warp jump: 1200.0000 ly to C[935724540,326587746,-281291491] L[298491633783.173,...]
origin_dist=1197.312487 ly
```

A single jump of 1200 light years, landing on a cell index of 9.36e8, exact.

### Runtime scene

`Docs/Sprints/Sprint-001-Screenshot.png` is a capture of the running game. It
shows the diagnostics overlay with every Phase H field populated, the generated
system **Corokar-4013** (class K, 2 planets), labelled body markers with true
distances, and the outer planet rendered as a lit crescent - correctly
illuminated from the side by its own star.

### NOT executed

- No automated regression test drives the Unreal-side classes. Rebasing, input
  and the HUD are verified by the scripted run above and by inspection, not by
  an assertion that would fail in CI.
- No performance profiling.

## 4. Files

| Path | What |
| --- | --- |
| `Source/UniverseCore/Public/UniverseScale.h` | Every constant, with its numerical justification |
| `Source/UniverseCore/Public/UniverseCoordinates.h` `Private/UniverseCoordinates.cpp` | `FUniversePosition` and the exactness argument |
| `Source/UniverseCore/Public/UniverseHash.h` | SplitMix64, domain-separated combination |
| `Source/UniverseCore/Public/UniverseRandom.h` | PCG32 with unbiased bounded draws |
| `Source/UniverseCore/Public/UniverseSeed.h` | The seed hierarchy |
| `Source/UniverseCore/Public/UniverseSerialization.h` | Explicit little-endian wire format |
| `Source/UniverseGeneration/Public/StarSystemDescriptor.h` | Generated data, SI units, stable identity |
| `Source/UniverseGeneration/Private/StarSystemGenerator.cpp` | The astronomy |
| `Source/Universe/Public/UniverseRenderSpace.h` | The two render spaces |
| `Source/Universe/Private/UniverseWorldSubsystem.cpp` | Render origin and rebasing |
| `Source/Universe/Private/UniverseProbePawn.cpp` | The probe |
| `Source/Universe/Private/UniverseHUD.cpp` | Diagnostics overlay and body markers |
| `Tools/StandaloneTests/` | The engine-free verification harness |
| `Docs/Architecture/UniverseCoordinates.md` | Coordinate analysis |
| `Docs/Architecture/ProceduralGeneration.md` | Determinism rules and generation |
| `Docs/Architecture/Testing.md` | Both runners, coverage and gaps |
| `Docs/ADR/ADR-001`, `ADR-002` | Decisions, options rejected, consequences |

---

## 5. Acceptance criteria

| Criterion | Status |
| --- | --- |
| UE 5.8.x C++ project builds | **Met** - editor and game targets, zero warnings |
| Repository is cleanly structured | Met |
| Universe coordinates are documented | Met |
| Universe coordinate system is implemented | Met, verified by test |
| Seed hierarchy is documented | Met |
| Deterministic generation utilities exist | Met, verified by test |
| At least one deterministic star system is generated | Met - Corokar-4013, generated at runtime |
| Placeholder star/planets can be viewed | **Met** - see the screenshot |
| A spacecraft/probe can move | **Met** - flown in-engine via autopilot and warp jump |
| Global logical position can cross many coordinate cells | Met - hundreds of thousands of cells in-engine, 909,494 in the numeric harness |
| Local Unreal precision remains stable | **Met** - transform stays within the 1e6 cm rebase radius at all speeds |
| Returning to the same generated location reproduces the same system | Met, verified by test |
| Automated tests pass | Met - 25 tests, 144,024 assertions, both runners |
| Relevant architecture documentation is current | Met |

**Sprint 001 is complete.**

## 6. Known limitations

1. **Manual input has not been exercised.** The probe has been flown only by
   the debug autopilot and the warp console command. The Enhanced Input
   bindings compile and the mapping context is applied, but no human has held
   W and moved the mouse, so the key bindings themselves are unproven.

2. **Star brightness is calibrated by eye, not physically.** A real stellar
   surface is roughly four orders of magnitude brighter than a planet lit by
   it; using the true figure makes auto-exposure crush every planet to black.
   `StarEmissiveBrightness` is an explicit presentation compromise and is
   documented as such.

3. **Cross-platform determinism is not guaranteed.** Coordinate arithmetic is
   exact and portable, but the generators call `pow`, `log` and trigonometric
   functions, and libm is not bit-identical across platforms. Not observable in
   single player; a real problem for authoritative multiplayer and
   cross-platform saves. Structural decisions (sector population, planet count,
   spectral class) already avoid libm; derived payload values do not.
   See [ProceduralGeneration.md section 7](../Architecture/ProceduralGeneration.md).

4. **No caching.** Every query regenerates. Deliberate for Sprint 001 so the
   determinism guarantee is exercised rather than hidden, but not shippable.

5. **No performance budgets.** CLAUDE.md section 23 requires them; nothing is
   measured yet.

6. **Orbits do not advance with time.** Planets sit at their epoch phase. The
   orbital period is generated and stored, but nothing consumes it.

7. **Scaled space and local space do not yet bridge.** A body can be navigated
   toward but not approached and landed on. That transition is Sprint 002.

8. **`/Engine/Maps/Entry` is assumed to exist** as the default map. If it does
   not in a given install, any empty level works - the game mode builds the
   scene itself.

9. **Resolution floor of 2.44 um.** Displacements below it are absorbed rather
   than accumulated. Pinned by a test so it cannot change unnoticed.

10. **No golden-file regression test.** The suite asserts properties, not stored
   expected outputs, so a deliberate-looking but unintended generator change
   would not be caught. Worth adding once the generator settles.

---

## 7. Recommended Sprint 002

**Goal: one spherical, procedurally generated, landable planet.**

The smallest next step, and it should be exactly one planet - not a system of
them, and not terrain generation for its own sake. The hard problem is the
*transition*, and everything else is easier once that works.

Order of work:

1. **ADR-003: planet topology.** Cube-sphere with a quadtree per face is the
   CLAUDE.md preference; record why, and how patch seeds descend from
   `GetSurfacePatchSeed` (already implemented and tested).

2. **Cube-sphere mesh with quadtree LOD**, for one planet, no noise yet - just
   a smooth sphere subdividing correctly as the camera closes in. Test for
   crack-free patch borders before adding any height.

3. **ADR-004: bridging scaled space and local space.** The Sprint 001 gap. A
   planet must grow from a scaled-space dot into a 1:1 world without a visible
   seam, which likely means a second scaled-space camera compositing behind the
   near-field one, and a handover radius.

4. **Deterministic height from `GetSurfacePatchSeed`** - 3D noise sampled on the
   sphere, so there are no UV seams by construction. Continentalness and
   erosion can wait.

5. **Land on it.** Ship down, ship stopped on terrain. Walking, gravity,
   biomes, water and vegetation are all Sprint 003+.

Do not start vegetation, weather, wildlife or persistence until a ship can
descend from orbit and touch a procedurally generated surface without a loading
screen.
