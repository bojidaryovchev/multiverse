# Testing

**Status:** implemented, Sprint 001

---

## 1. Why there are two runners

The deterministic core is what the entire universe is reconstructed from. If it
is wrong, everything built on it is wrong, and the failure is silent - a
universe that regenerates *slightly* differently looks fine until someone
notices their base is gone.

That argues for running these tests constantly, which in turn argues for making
them cheap to run. So the test bodies are written once, as plain functions, and
executed by two runners:

| Runner | Needs | Speed | Purpose |
| --- | --- | --- | --- |
| `Tools/StandaloneTests/RunTests.bat` | MSVC only | ~10 s from cold | Fast inner loop, CI, and any machine without an engine |
| Unreal Automation (`Universe.Core.*`, `Universe.Generation.*`, `Universe.Planet.*`) | UE 5.8 editor | Minutes | Verifies the same logic against the real engine types |

A test body looks like this and knows about neither runner:

```cpp
bool UniverseTest_NormalizationBasic(FUniverseTestResult& Result)
{
    FUniversePosition P = FUniversePosition::FromCells(0, 0, 0);
    P = P.OffsetByCm(FVector3d(CellD, 0.0, 0.0));
    UVERIFY_EQ_INT(Result, P.CellX, 1);
    UVERIFY_EQ_DOUBLE_EXACT(Result, P.Local.X, 0.0);
    return Result.Passed();
}
```

Both runners expand the same X-macro registry
(`Tests/UniverseCoreTestList.h`, `Tests/UniverseGenerationTestList.h`,
`Tests/UniversePlanetTestList.h`), so a new
test is added in exactly one place and cannot end up running in one harness but
not the other.

## 2. The standalone harness

`Tools/StandaloneTests/Shim/UnrealShim.h` provides minimal stand-ins for the
handful of Unreal types the core uses - `int64`, `FVector3d`, `FString`,
`TArray` and `FMath`. It lives outside `Source/` so UnrealBuildTool and
UnrealHeaderTool never see it.

Note what it deliberately does **not** define: `USTRUCT`, `UCLASS`,
`UPROPERTY`, `GENERATED_BODY`. `UniverseCore` and `UniverseGeneration` contain
no reflection at all, and stripping those macros here would let someone add a
`USTRUCT` to those modules and have the fast harness keep building while the
Unreal build failed on a missing `.generated.h`. Leaving them undefined turns
that mistake into a loud error in the ten-second loop.

**It is a test harness, not an abstraction layer.** It is compiled only under
`UNIVERSE_STANDALONE=1` and never ships. If the core ever needs an Unreal
facility that is awkward to shim, that is a signal the code belongs in the game
module - not a signal to grow the shim.

The build uses `/fp:strict` deliberately. The exactness arguments in
`UniverseCoordinates.cpp` assume IEEE-754 semantics exactly as written;
`/fp:fast` would let the compiler reassociate them and quietly break
determinism. It also uses `/W4 /WX`, because this is core numeric code.

### Running it

```
Tools\StandaloneTests\RunTests.bat
Tools\StandaloneTests\RunTests.bat --verbose     REM print every failure
```

Exit code 0 means everything passed. On success it also prints the scale
constants, a sample generated system and a numeric large-distance traversal
proof, so a run doubles as evidence that the numbers match the documentation.

## 3. The Unreal automation tests

Inside the editor: **Tools > Session Frontend > Automation**, filter to
`Universe`.

Headless:

```
UnrealEditor-Cmd.exe <path>\Universe.uproject ^
  -ExecCmds="Automation RunTests Universe; Quit" ^
  -unattended -nopause -nullrhi -log
```

The wrappers translate each `FUniverseTestResult` failure into an `AddError`.
They also fail a test whose body executed **zero** assertions, so a body that is
accidentally emptied reports as broken rather than as passing.

## 4. What is covered

67 test bodies, 1,186,413 assertions, all passing.

**Coordinates** - constants and the power-of-two assumption; positive and
negative normalisation; exact cell edges and either side of them; boundary
neighbourhoods with no gap or overlap; a 100,000-step journey with an exact
return; millimetre resolution at 1e10 light years; relative vectors and their
refusal beyond range; distance across the entire universe without overflow;
whole-cell jumps and overflow refusal; sector floor division across the origin;
48-byte bit-exact serialisation with truncation and tampering rejected;
equality and hashing of positions reached by different routes.

**Determinism** - SplitMix64 against published vectors, plus avalanche;
PCG32 reproducibility, uniformity and lack of modulo bias; seed descent purity;
domain separation between hierarchy levels.

**Planet** - cube-sphere face basis; exact seams across all twelve cube edges
and eight corners; the dyadic-UV constraint; patch hierarchy and child
coverage; neighbour symmetry across face seams; terrain determinism and bounds
at four planet radii; gradient normals; patch mesh validity and border
agreement; LOD response and scale invariance; hysteresis; neighbour balancing;
horizon culling.

**Traversal** - the three altitudes and their relationships; local up and
terrain normals; the gravity field's direction, law and finiteness from 96
directions at six distances; frame hysteresis, asserted as zero changes across
200 ticks of boundary jitter; handover between overlapping bodies; swept
intersection at every thrust tier to 10^12 m/s, including its numerical
stability at 10^13 m; terrain refinement of a bounding-sphere hit; the
atmospheric depth ramp.

**Environment** - the ocean level against its coverage target; climate gradients
and the lapse rate; humidity bounds; climate continuity across all twelve cube
edges; biome classification by climate family, and that blends actually blend;
land/water classification and depth at three planet radii; the environment query
returning nothing NaN and wind tangential to the surface; weather being regional
rather than global, evolving, and never jumping; vegetation placement
deterministic to the position and never leaking outside its patch.

**Persistence identity** - persistence regions are deterministic, sized per
planet and cannot collide between two planets at the same address; entity ids
are derived from the placement address, reproduce exactly across a
regeneration, include both generation versions, and reject malformed text rather
than parsing it as zero.

**Generation** - same address gives the same content even after hundreds of
unrelated generations; different universe seeds diverge while identity stays
address-derived; generation order cannot influence results; system identity
round-trips and rejects tampering; 200+ systems checked for physical coherence;
stellar density matches the solar neighbourhood; proximity queries are
order-stable; planet placement is deterministic and puts each planet at its
stated orbital radius; and the full leave-travel-return reproduction.

Both runners agree exactly: 67/67 tests and 1,186,413/1,186,413 assertions pass
standalone against the shim and in-engine against Unreal's real `FVector3d`,
`FString`, `TArray` and `FMath`. That agreement is itself a result - it means
the shim is a faithful stand-in and the fast loop can be trusted.

## 5. Runtime validation (not automated)

The Unreal-side classes are exercised by a scripted headless run rather than by
an automated test, because they need a world, a tick and a renderer:

```
UnrealEditor.exe <project> -game -benchmark -benchmarkseconds=40 -fps=30
  -ExecCmds="universe.AutoPilot 1, universe.AutoPilotTier 11, universe.LogStateInterval 5"
```

`universe.AutoPilot` holds forward thrust with nobody at the controls and
`universe.LogStateInterval` emits a state line, so a long traversal runs
unattended and leaves its evidence in the log. `universe.WarpJump <ly>` drives
the whole-cell jump path the same way. What this demonstrated is recorded in
[the Sprint 001 report](../Sprints/Sprint-001-Report.md).

Turning these observations into automated regression tests - a latent
automation test that flies the probe and asserts the Unreal transform stays
bounded - is worthwhile and not yet done.

## 6. What is *not* covered

- **No automated test of the Unreal-side classes.** Rebasing, input, the HUD and
  terrain streaming are verified by the scripted runs above and by inspection,
  not by assertions that would fail in CI. The Sprint 002 stress path
  (`universe.TerrainStress`) is the closest thing: it is repeatable and its
  numbers are checkable, but a human still reads them.
- **No cross-platform determinism test.** See
  [ProceduralGeneration.md section 7](ProceduralGeneration.md).
- **No performance budgets.** CLAUDE.md section 23 requires them; nothing here
  measures frame time or allocation.
- **No golden-file regression.** The tests assert properties and invariants, not
  stored expected outputs. A golden content-hash file would catch an
  *intentional-looking but unintended* change to the generator, and should be
  added once the generator settles.

## 7. Adding a test

1. Write `bool UniverseTest_MyThing(FUniverseTestResult& Result)` in the
   relevant module's `Private/Tests/`.
2. Add `X(MyThing)` to that module's test list header.

Both runners pick it up. Nothing else is needed.

Prefer `UVERIFY_EQ_DOUBLE_EXACT` wherever the design claims a result is bit-exact.
A tolerance there would hide exactly the defect the test exists to catch.
