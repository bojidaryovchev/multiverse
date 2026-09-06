# The Golden Path

*Sprint 008. The release gate. Source:
`Source/Universe/Public/GoldenPathSubsystem.h`.*

## What it is

One session, start to finish, that must work before any build goes to anybody
else:

```text
spawn in a procedural universe
  -> fly under normal thrust
  -> target another star
  -> warp to it
  -> arrive, and find it identical to what was predicted before departure
  -> descend and land on a procedural planet
  -> step out of the ship
  -> walk on the surface
  -> build a structure
  -> clear a procedural object
  -> board the ship
  -> leave the planet
  -> quit
  -> start again
  -> find the structure still there
```

**If this fails, nothing ships.** Every other test in the project can pass while
this one fails, and that is not hypothetical: it is what happens when two
subsystems each work correctly and disagree about a handoff between them.

## Why it is one long test

By Sprint 008 the project has 79 unit tests, four scripted single-process runs
and two multiplayer scripts. Between them they cover every subsystem.

What none of them covers is **the session**. The product is not a coordinate
system, a terrain generator and a persistence layer; it is what happens when a
person uses all of them in sequence for three minutes. That sequence has its own
failure modes, and they live in the joins.

The very first automated run demonstrated the point. Every subsystem worked
perfectly, and the run failed: the ship landed 6,246 metres under water, because
"land below me" on a world that is two thirds ocean lands on the sea floor. No
unit test would ever have asked.

## Running it

### Automated

```bat
REM The session, from a clean world:
UnrealEditor.exe Universe.uproject -game -benchmark -benchmarkseconds=900 ^
    -fps=30 -unattended -nopause -nosplash -nullrhi ^
    -ExecCmds="universe.PersistenceReset all,universe.GoldenPath"

REM Then, in a NEW process, that the changes survived:
UnrealEditor.exe Universe.uproject -game -benchmark -benchmarkseconds=900 ^
    -fps=30 -unattended -nopause -nosplash -nullrhi ^
    -ExecCmds="universe.GoldenPath verify"
```

Or, with everything else the project can check:

```bat
Tools\Build\VerifyAll.bat
```

The second process is the whole point of the verify step. A verification that
ran in the same process would prove only that memory still holds what was put
in it.

### By hand, from a packaged build

```text
1.  Builds\Client\Windows\Universe.exe
2.  Wait for the system to stream in. The HUD names it.
3.  W to fly. Watch the speed climb.
4.  T to target the star ahead, or N to cycle through the ones nearby.
5.  J to warp. It disengages itself on arrival.
6.  When the planet is ahead, L to land. If you are over ocean it will find the
    nearest dry ground and tell you how far it moved you.
7.  F to step outside.
8.  WASD to walk, Space to jump.
9.  B to build. X to clear a tree.
10. F to board the ship, W to lift off.
11. Alt+F4.
12. Launch again, fly back, and find what you built.
```

No console commands. That is the requirement, not a convenience: a vertical
slice that needs `universe.Land` is a slice only its author can play.

## What it asserts

| Stage | Assertion |
|---|---|
| Waking | A system is active and a planet has been built. |
| Flying | Ordinary thrust moves the canonical position. |
| Warping | The drive engages, crosses light years, and disengages on arrival. |
| Arrived | The destination is the one targeted, and its content hash matches what was computed before departure. |
| Landing | The ship reaches the ground - on land, not under water. |
| Landed | The player can leave the ship. |
| OnFoot | The character stands on the sphere and can walk. |
| Working | A structure is created and its id recorded. |
| Aboard | The player can walk back and board. |
| Leaving | The planetary frame releases. |
| Recording | The run's outcome is written into the world's own database. |
| *verify* | A **new process** finds the recorded structure in the recorded region. |

Each stage has a deadline. A session that takes four minutes instead of three
has failed even though it arrived - a stage that hangs is a defect whether or
not it eventually recovers.

The outcome is recorded in the world database rather than a side file, so a
verify run cannot accidentally check a record belonging to a different universe:
the record and the world it describes are the same file.

## Expected output

```text
=== Golden Path ===
[   0.0s] -> Waking
  Spawned in Zarelra-1252, with PlanetActor_0 built.
[   0.5s] -> Flying
  Warping to Aelonis-3673, 12.714 ly.
[   3.5s] -> Warping
  Arrived after 136.2 s.
[ 139.7s] -> Arrived
  Aelonis-3673 is here and identical to what was predicted before departure.
[ 139.7s] -> Landing
  Landed on PlanetActor_1 - 2000 km from where you were, the water below had
  no bottom worth landing on.
[ 140.7s] -> Landed
[ 142.7s] -> OnFoot
[ 145.7s] -> Working
  Built 02F9B63BF1F63EA5333A88D0255ECBD7 in region P36B0.../+X/L13/6867/4535.
[ 145.9s] -> Leaving
  Left the planet.
[ 160.4s] -> Recording
--- PASS ---
```

Then, in the next process:

```text
=== Golden Path: verifying a previous run ===
  The structure from the previous run is still there:
  02F9B63BF1F63EA5333A88D0255ECBD7
--- PASS ---
```

## Reading a failure

The report names the stage and the reason. The three most likely, and what they
usually mean:

| Failure | Usually |
|---|---|
| `Waking exceeded its deadline` | The streamer found no system. Check the seed and `universe.GalaxyInfo` - the universe origin is intergalactic space. |
| `Landing exceeded its deadline` | Nothing dry within the search radius, or the terrain never finished. Check `LogUniverseProbe`. |
| `Working exceeded its deadline` | The build was refused. `LogWorldPersistCmd` says why: under water, too steep, or something already there. |
| `the destination regenerated differently` | Procedural generation is order-dependent. This is the most serious failure the project can produce. |

`PASS WITH WARNINGS` is a pass. The common warning is "nothing removable within
range of the landing site", which means the ship landed somewhere without
vegetation - a legitimate outcome on a cold or rocky world, not a defect.

## What it deliberately does not cover

- **Multiplayer.** Three processes and real elapsed time; see
  `Tools\Multiplayer\TwoPlayerTest.ps1` and `PersistenceTest.ps1`.
- **Rendering.** It runs with `-nullrhi`. Nothing here can catch a black screen,
  which is why the Sprint 004 lighting defects needed screenshots.
- **Input.** It calls the same functions the keys call, not the keys themselves.
- **Frame times.** See the performance baseline.

## Related

- [Testing.md](../Architecture/Testing.md) - the two-harness arrangement
- [Networking.md](../Architecture/Networking.md) - the multiplayer equivalents
- [FailureModes.md](FailureModes.md) - what goes wrong and how it looks
