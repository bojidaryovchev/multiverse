# Sprint 008 Report — MVP Hardening & Playable Vertical Slice

*Unreal Engine 5.8.2, Windows 11. Every figure below was produced by running the
command quoted.*

## What this sprint proves

**Somebody else can play this.**

A packaged Windows client, on a machine with no Unreal installation, runs the
whole experience without a single console command:

```text
Builds\Client\Windows\Universe.exe

=== Golden Path report ===
  Result       : PASS
  Mode         : full session
  Elapsed      : 160.4 s
  Home         : Zarelra-1252
  Destination  : Aelonis-3673
  Warp         : 12.714 ly in 136.2 s
  Planet       : 0x36B0783FBDC1F1F7
  Built        : 02DA18E17AF745C269BF298B99977371
--- PASS WITH WARNINGS ---
```

Then, in a **new process**:

```text
=== Golden Path: verifying a previous run ===
  The structure from the previous run is still there:
  02F9B63BF1F63EA5333A88D0255ECBD7
--- PASS ---
```

That is the release gate: spawn, fly, warp twelve light years, arrive, land on a
procedural planet, step out, walk, build, board, leave, quit, come back, find it
still there.

## Verification

```text
Tools\Build\VerifyAll.bat

[1/5] Deterministic core tests ...                          passed.
[2/5] The same tests inside Unreal ...                      passed.
[3/5] Planetary journey ...                                 passed.
[4/5] Interstellar journey ...                              passed.
[5/5] Golden Path, then verifying it from a new process ... passed.

ALL VERIFICATION PASSED
```

```text
Tools\Multiplayer\TwoPlayerTest.ps1      ALL CHECKS PASSED  (10 checks)
Tools\Multiplayer\PersistenceTest.ps1    ALL CHECKS PASSED  (8 checks)
```

| Suite | Result |
|---|---|
| Standalone core | **79/79 tests, 1,241,356 assertions** |
| Unreal automation | **79/79 tests, 0 failures** |
| Planetary journey (Sprint 003) | **PASS** |
| Interstellar journey (Sprint 006) | **PASS** |
| Golden Path + verify (new) | **PASS** |
| Two players (Sprint 007) | **ALL CHECKS PASSED** |
| Reconnect and server restart (Sprint 007) | **ALL CHECKS PASSED** |
| Packaged client running the Golden Path | **PASS** |

## What changed

This was a hardening sprint and the feature freeze held. Nothing was added that
was not needed to make what already existed coherent.

### Playable without a console

Landing, targeting and building were console commands through Sprints 003 to
007. That was right while the only person playing was the one writing it, and
it is the single biggest barrier to anybody else trying it.

| Key | Does |
|---|---|
| `L` | land |
| `T` | target the star ahead |
| `N` | next target |
| `J` | engage / disengage warp |
| `F` | step out of the ship, or board it |
| `B` | build |
| `X` | clear a procedural object |

**Every one calls the same code the command does.** The commands still work and
are unchanged. A second implementation of "build" would have been a second
implementation of the multiplayer authority rules, which is the one thing this
project must not have two of.

### The HUD is split

The player's panel is always on and shows at most seven things, one of which is
what to press next. The eight hundred pixels of cell indices, patch counts and
hash values are what `F1` toggles now.

That is the opposite of how it was through Sprint 007, and it is the right way
round for a build somebody else runs: the developer's information on request,
the player's by default.

### The Golden Path

One test that runs the whole session and returns a verdict, plus a second
process that checks the world survived. It is the release gate, it is documented
in `Docs/Testing/GoldenPath.md`, and it is stage five of `VerifyAll.bat`.

### One command verifies everything

`Tools\Build\VerifyAll.bat` runs the five automated suites cheapest-first: a
broken hash fails in ten seconds rather than four minutes into a run that was
never going to work.

`Tools\Build\Package.bat` produces a distributable client - **and runs the core
tests first, refusing to package if they fail.** That check is inside the
packaging script rather than beside it deliberately: the reliable way to ensure
a build that goes to somebody else has passed its tests is for packaging to be
unable to happen otherwise.

## Defects found, and what gave them away

### 1. The ship landed under water

**Symptom:** the very first automated Golden Path run failed at the build stage
with `universe.Build: that spot is under water (6246 m deep)` - repeated once a
frame for forty-five seconds.

**Cause:** landing put the ship wherever it happened to be over. On a world two
thirds ocean, that is the sea floor. Every subsystem worked perfectly and the
session was unplayable.

**This is exactly what a player would have done**, and no unit test would ever
have asked the question. It is the case for having a test that plays the game.

**Fix:** landing searches outward in geometric rings for dry ground, reports how
far along the surface it moved, and refuses with a plain sentence when a world
has no land at all:

```text
Landed on PlanetActor_1 - 2000 km from where you were, the water below had
no bottom worth landing on.
```

### 2. The tangent-plane approximation stopped being true

**Cause:** the first version of that search offset the landing direction in the
tangent plane and renormalised. That is exact to parts per million at 50 km and
half a percent wrong at 2,000 km, which is an eleventh of a radian on a
10,000 km world.

**Fix:** rotate the direction by the arc angle. One sine and one cosine, exact
at every radius.

### 3. "How far did I move" measured the wrong thing

**Symptom:** `Landed on PlanetActor_1 - 8344688 km from where you were`.

**Cause:** it measured from where the *ship* was - in orbit, after a warp
arrival - rather than the lateral distance the search moved the landing site.
8.3 million kilometres is not a distance on a planet, and a player reading it
would reasonably conclude the game was broken.

### 4. The frame-time reading was not a measurement

**Symptom:** `universe.Perf` reporting exactly 33.33 ms in every run, on every
workload.

**Cause:** `-benchmark -fps=30` *fixes* the frame delta. The number was reading
back the command line.

**Fix:** the line says so:

```text
Frame        : 33.33 ms (30 fps)   [fixed by -benchmark; not a measurement]
```

A performance baseline that quoted that number would have been fiction, and it
would have looked like a measurement to everybody who read it afterwards.

## Performance baseline

Measured, from `universe.Perf`. Full detail in
`Docs/Performance/Baseline.md`.

| Standing on a planet | |
|---|---|
| Memory over baseline | **+178 MB** |
| Terrain patches | 189 visible, 2 with cooked collision |
| Triangles | 1,645,056 |
| Vegetation instances | 9,929 across 18 patches |
| Patch selection | 0.25 ms/frame (game thread) |
| Patch generation | 42.8 ms average (worker thread) |
| Persistence region read | 1.7 ms |
| CPU frame time, no renderer | ~0.5 ms |

| A full planetary journey | |
|---|---|
| Origin rebases | 1,064 |
| Patches generated / released | 330 / 324 |
| Peak memory | 1,743 MB |

Every budget in the project is a named constant with a comment saying why. The
baseline lists them and says what to watch for in a regression.

## Release gate

| Blocker | Status |
|---|---|
| Frequent crash on the Golden Path | **No.** Clean across every run this sprint. |
| Persistence corrupts | **No.** Structures and removals survive process restarts; the schema migrates additively and refuses newer versions. |
| Multiplayer cannot reconnect | **No.** Verified by `PersistenceTest.ps1`. |
| Planet traversal frequently breaks | **No.** Both journeys pass. |
| Warp frequently corrupts position | **No.** Round trip regenerates the home system to an identical content hash. |
| Packaged client does not run | **No.** It runs the Golden Path on a machine with no editor. |
| Server does not persist the world | **No.** Verified across a server process restart. |

**No blockers. This build can go to somebody else.**

## Known limitations

Stated, not hidden. None is a blocker; all are Phase 2.

- **No main menu or connect UI.** The client connects from the command line.
  It is the only remaining reason a player needs anything but keys.
- **No audio at all.**
- **Placeholder visuals.** Engine primitives for ships, characters and distant
  bodies. Terrain is real; everything standing on it is a cylinder.
- **One active star system.** Players must be in the same one to interact fully.
- **Movement validation is a speed bound, not a collision check.** It refuses
  the impossible, not the improbable.
- **The server's streaming viewpoint is the first player to join.** A
  single-region-server assumption, and the first line that changes for regional
  authority.
- **No orbital motion.** Planet positions are a deterministic function of epoch
  phase; nothing advances with time.
- **Cross-platform determinism is unproven** for star and galaxy generation,
  which use `libm` transcendentals. Terrain and climate are transcendental-free,
  which is where it matters most - those decide ground somebody has built on.
  Open since ADR-002; a project-owned libm closes it in one move.
- **Nothing visual is automatically tested.** Every automated run is `-nullrhi`.
  The Sprint 004 black-screen defects needed screenshots and would need them
  again.

## Documentation added

```text
Docs/Testing/GoldenPath.md           the release gate, and how to read a failure
Docs/Testing/FailureModes.md         what breaks, how it looks, what it usually is
Docs/Architecture/MVPArchitecture.md the whole thing in one page
Docs/Architecture/DataFlow.md        what happens, in order, when you do something
Docs/Performance/Baseline.md         measured numbers and what to watch
Docs/Setup.md                        clean machine to running universe
Docs/AgentHandoff.md                 for whoever picks this up next
```

`FailureModes.md` is written entirely from defects that actually happened, and
it opens with the three patterns that account for nearly all of them:

1. A number that is a plausible number.
2. Something that works because there is exactly one of it.
3. An assumption that something is ready.

Every defect in this sprint was one of the three. The first three were all
pattern one.

## The state of the MVP

Eight sprints, and the thing they were building is now a thing somebody can be
handed:

```text
a universe 10^10 light years across, generated from a seed
galaxies as density functions, so intergalactic space is genuinely empty
star systems, planets, terrain, oceans, climate, biomes, vegetation, weather
seamless space-to-surface traversal with no loading screen
walking on a sphere with real gravity
interstellar travel at three million c, with swept collision
building and clearing that persists
a dedicated server, two players, shared persistence
79 automated tests, 1.24 million assertions, in ten seconds without an engine
one command that verifies all of it
a packaged client that plays it
```

The universe is enormous logically, costs nothing where nobody is, and is
detailed around the player - which is what the specification asked for on the
first day.
