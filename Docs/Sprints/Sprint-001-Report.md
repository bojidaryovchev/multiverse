# Sprint 001 - Universe Foundation

**Date:** 2026-09-05
**Status:** Core complete and verified. Unreal-side code written but **not
compiled or run** - no Unreal Engine is installed on this machine.

---

## 1. Environment (Phase A findings)

Inspected before starting:

| | Found |
| --- | --- |
| Unreal Engine | **Not installed.** No engine directory or `UnrealBuildTool.exe` anywhere on C:, D:, E: or H:. Only the Epic Games Launcher shell is present (`C:\Program Files\Epic Games\Launcher`), plus a stale `HKLM\SOFTWARE\EpicGames\Unreal Engine\4.0` registry key pointing at a non-existent directory. |
| Unreal MCP | **Not available.** The only MCP server connected is `openseo` (an SEO toolset, unrelated to this project). |
| Compiler | Visual Studio 2022 Build Tools 17.14.39, MSVC 14.44.35207 (x64) |
| Windows SDK | 10.0.26100.0 |
| .NET | 9.0.305 |
| Git | 2.51.0.windows.1 |
| Git LFS | 3.7.0 |
| Repository | Empty - `main` with no commits, containing only `CLAUDE.md` and `initial-prompt.md` |

Installing UE 5.8 is a ~100 GB download behind an interactive Epic account
login and could not be done autonomously. Per the instruction to work "as far as
the available local environment permits", the sprint proceeded on two tracks:

1. Write the complete UE 5.8 project - modules, build rules, config, gameplay
   code - so that it is ready to build the moment an engine is present.
2. Make the deterministic core **genuinely executable without an engine**, so
   the mathematics that everything else rests on is actually verified rather
   than merely asserted.

Track 2 is what makes this report able to distinguish "tested" from "written".

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

Written, not run:

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

### Executed and passing

```
> Tools\StandaloneTests\RunTests.bat
Building standalone core tests...
  [ PASS ] ScaleConstants                                8 checks
  [ PASS ] NormalizationBasic                           11 checks
  [ PASS ] NormalizationNegative                         8 checks
  [ PASS ] CellBoundaryExact                            14 checks
  [ PASS ] CellBoundaryNeighbourhood                    28 checks
  [ PASS ] LargeDisplacementAccumulation                 4 checks
  [ PASS ] LocalPrecisionAtExtremeCoordinates           10 checks
  [ PASS ] RelativeAndDistance                          14 checks
  [ PASS ] CellOffsetJumps                              12 checks
  [ PASS ] SectorAddressing                             33 checks
  [ PASS ] PositionSerializationRoundTrip               53 checks
  [ PASS ] PositionEqualityAndHash                       8 checks
  [ PASS ] HashStability                              1268 checks
  [ PASS ] RandomStreamStability                    120528 checks
  [ PASS ] SeedHierarchyDeterminism                    512 checks
  [ PASS ] SeedHierarchyDomainSeparation                49 checks
  [ PASS ] SystemGenerationDeterminism                  45 checks
  [ PASS ] SystemGenerationDifferentSeeds              101 checks
  [ PASS ] SystemGenerationOrderIndependence           256 checks
  [ PASS ] SystemIdStabilityAndSerialization            14 checks
  [ PASS ] SystemPhysicalPlausibility                12348 checks
  [ PASS ] PlanetPlacementDeterminism                  497 checks
  [ PASS ] SectorPopulationStatistics                 8003 checks
  [ PASS ] ProximityQueryDeterminism                    88 checks
  [ PASS ] LeaveAndReturnReproduction                  112 checks

  25/25 tests passed, 144024/144024 assertions passed
RESULT: ALL TESTS PASSED
(exit code 0)
```

Compiled with `cl.exe /std:c++20 /O2 /fp:strict /W4 /WX` - warnings as errors,
strict IEEE-754 semantics, **zero warnings**. `/fp:strict` matters: the
exactness arguments assume IEEE semantics exactly as written, and `/fp:fast`
would let the compiler reassociate them.

The run was also verified from a stripped `PATH` with `cl.exe` unavailable, to
confirm the script's MSVC auto-detection works on a clean machine.

### Large-distance traversal proof (Phase F), executed numerically

```
--- Large-distance traversal (Phase F, numeric) ----------------------
  Steps                : 250000
  Distance travelled   : 1.21095 light years
  Cells crossed (X)    : 909494
  Max |local| observed : 1.09951e+12 cm  (cell size 1.09951e+12 cm)
  Local stayed in cell : yes
  Round trip exact     : yes
```

909,494 cell boundaries crossed; local coordinates never left their cell; the
return journey landed on the exact starting position.

`UniverseTest_LargeDisplacementAccumulation` additionally applies 100,000 steps
of a quarter-cell each, lands on cell 25,000 with a local offset of exactly
zero, and returns to the origin exactly after 200,000 boundary-crossing
operations.

`UniverseTest_LocalPrecisionAtExtremeCoordinates` places a position ~10 billion
light years out and shows a 1 mm step is still *bit-exactly* representable and
recoverable there - against a single-double representation whose resolution at
that magnitude would be over 1 km.

`UniverseTest_LeaveAndReturnReproduction` flies 500+ light years away in
whole-cell jumps, generates the regions passed through, returns, and confirms
both an exact coordinate round trip and an identical system content hash.

### Sample generated output (executed)

```
Universe seed: "sprint-001" -> 0xBDEFD300CB477825

Quoenses-7499  Sector [0, 3, 0] #0  class M  0.44 Msun  0.058 Lsun  6 planet(s)
  [0] Desert       a=0.051 AU  r=10957.7 km  g=16.38 m/s2  T=603.3 K
  [1] Desert       a=0.113 AU  r=10067.9 km  g=14.74 m/s2  T=405.5 K
  [2] Terrestrial  a=0.228 AU  r= 5576.9 km  g= 7.83 m/s2  T=285.8 K
  [3] Rocky        a=0.354 AU  r= 4190.0 km  g= 5.33 m/s2  T=229.3 K
  [4] IceGiant     a=0.661 AU  r=29866.9 km  g=10.99 m/s2  T=168.0 K
  [5] GasGiant     a=1.465 AU  r=60867.2 km  g=14.80 m/s2  T=112.8 K
  content hash: 0x6A37AFB44198B9B2
```

An M dwarf with a tightly packed system and a close-in frost line - which is
what the luminosity-derived generation should produce, and a useful sanity check
that the astronomy is coupled rather than rolled independently.

### NOT executed

- **The Unreal project has never been compiled.** No engine is installed.
- **The game has never been launched.** No spacecraft has been flown, no
  rebasing has been observed, no HUD has been rendered.
- **The Unreal automation tests have never been run**, though they wrap the same
  test bodies that the standalone runner executes.

Nothing in this report claims otherwise.

---

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
| UE 5.8.x C++ project builds | **NOT VERIFIED** - project is complete and structured for 5.8, but no engine is installed to build it |
| Repository is cleanly structured | Met |
| Universe coordinates are documented | Met |
| Universe coordinate system is implemented | Met, verified by test |
| Seed hierarchy is documented | Met |
| Deterministic generation utilities exist | Met, verified by test |
| At least one deterministic star system is generated | Met, verified by test and printed above |
| Placeholder star/planets can be viewed | **NOT VERIFIED** - actors written, never rendered |
| A spacecraft/probe can move | **NOT VERIFIED** - pawn written, never run |
| Global logical position can cross many coordinate cells | Met - 909,494 crossings executed |
| Local Unreal precision remains stable | Met for the coordinate layer (local never leaves its cell); the rebasing that protects the *render* transform is unverified |
| Returning to the same generated location reproduces the same system | Met, verified by test |
| Automated tests pass | Met - 25 tests, 144,024 assertions, exit code 0 |
| Relevant architecture documentation is current | Met |

**Sprint 001 is therefore not complete.** Four criteria require an engine. The
work that does not require an engine is finished and verified; the rest is
written and waiting for a build.

---

## 6. Known limitations

1. **Nothing Unreal-side has been compiled.** The gameplay module is written
   against UE 5.8 APIs from knowledge, not from a compiler. Expect build fixes.
   The riskiest areas are the runtime-constructed Enhanced Input assets in
   `AUniverseProbePawn::BuildInputAssets` and the automation-test flag
   combination.

2. **Cross-platform determinism is not guaranteed.** Coordinate arithmetic is
   exact and portable, but the generators call `pow`, `log` and trigonometric
   functions, and libm is not bit-identical across platforms. Not observable in
   single player; a real problem for authoritative multiplayer and
   cross-platform saves. Structural decisions (sector population, planet count,
   spectral class) already avoid libm; derived payload values do not.
   See [ProceduralGeneration.md section 7](../Architecture/ProceduralGeneration.md).

3. **No caching.** Every query regenerates. Deliberate for Sprint 001 so the
   determinism guarantee is exercised rather than hidden, but not shippable.

4. **No performance budgets.** CLAUDE.md section 23 requires them; nothing is
   measured yet.

5. **Orbits do not advance with time.** Planets sit at their epoch phase. The
   orbital period is generated and stored, but nothing consumes it.

6. **Scaled space and local space do not yet bridge.** A body can be navigated
   toward but not approached and landed on. That transition is Sprint 002.

7. **`/Engine/Maps/Entry` is assumed to exist** as the default map. If it does
   not in a given install, any empty level works - the game mode builds the
   scene itself.

8. **Resolution floor of 2.44 um.** Displacements below it are absorbed rather
   than accumulated. Pinned by a test so it cannot change unnoticed.

9. **No golden-file regression test.** The suite asserts properties, not stored
   expected outputs, so a deliberate-looking but unintended generator change
   would not be caught. Worth adding once the generator settles.

---

## 7. Recommended Sprint 002

**Goal: one spherical, procedurally generated, landable planet.**

The smallest next step, and it should be exactly one planet - not a system of
them, and not terrain generation for its own sake. The hard problem is the
*transition*, and everything else is easier once that works.

Order of work:

1. **Install UE 5.8 and build.** Nothing else can be validated until this is
   done. Fix the build, run the automation tests in-editor, fly the probe, and
   confirm rebasing is genuinely invisible. Until then Sprint 001 is unverified
   in exactly four places.

2. **ADR-003: planet topology.** Cube-sphere with a quadtree per face is the
   CLAUDE.md preference; record why, and how patch seeds descend from
   `GetSurfacePatchSeed` (already implemented and tested).

3. **Cube-sphere mesh with quadtree LOD**, for one planet, no noise yet - just
   a smooth sphere subdividing correctly as the camera closes in. Test for
   crack-free patch borders before adding any height.

4. **ADR-004: bridging scaled space and local space.** The Sprint 001 gap. A
   planet must grow from a scaled-space dot into a 1:1 world without a visible
   seam, which likely means a second scaled-space camera compositing behind the
   near-field one, and a handover radius.

5. **Deterministic height from `GetSurfacePatchSeed`** - 3D noise sampled on the
   sphere, so there are no UV seams by construction. Continentalness and
   erosion can wait.

6. **Land on it.** Ship down, ship stopped on terrain. Walking, gravity,
   biomes, water and vegetation are all Sprint 003+.

Do not start vegetation, weather, wildlife or persistence until a ship can
descend from orbit and touch a procedurally generated surface without a loading
screen.
