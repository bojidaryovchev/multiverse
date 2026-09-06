# Agent Handoff

*For whoever picks this up next, human or otherwise. Read
[CLAUDE.md](../CLAUDE.md) first - it is the specification. This is what eight
sprints of building against it actually taught.*

## Start here

```bat
Tools\StandaloneTests\RunTests.bat     REM 10 s, no engine. Run this first.
Tools\Build\VerifyAll.bat              REM ~3 min. Run this before and after.
```

Then read, in this order:

1. [Architecture/MVPArchitecture.md](Architecture/MVPArchitecture.md) - the
   whole thing in one page
2. [Architecture/DataFlow.md](Architecture/DataFlow.md) - what happens in a
   frame
3. [Testing/GoldenPath.md](Testing/GoldenPath.md) - what must never break
4. [Testing/FailureModes.md](Testing/FailureModes.md) - what breaks, and how it
   looks from outside

The ADRs are the decisions and their prices. Read the one for whatever you are
about to change.

## The rules that are not negotiable

These are not style preferences. Each exists because breaking it produced a
defect that was expensive to find.

1. **Never break the module dependency chain.**
   `Universe -> UniversePlanet -> UniverseGeneration -> UniverseCore`, one way.
   The three lower modules must keep compiling with no engine, because that is
   what makes 1.24 million assertions run in ten seconds.

2. **Never let anything with process or configuration lifetime into a
   persistent identity.** Not an array index, not a pointer, not a tuning value,
   not a budget, not the time. A vegetation budget briefly did, and a chopped
   tree came back.

3. **Never write a hardcoded "down" vector.** Gravity comes from one subsystem
   and points at a planet centre. A hardcoded axis works near the origin and
   fails over the horizon.

4. **Never say "altitude" without saying which one.** Distance from centre,
   above sea level, and above terrain are three different numbers and the
   difference is kilometres.

5. **Patch resolution must be `2^p + 1`.** Seam arithmetic is exact only for
   dyadic UVs; anything else cracks every patch border on the planet.

6. **The universe origin is intergalactic space.** Anything needing a star must
   ask `FStarSystemGenerator::FindSystemNear`, not search from
   `FUniversePosition()`.

7. **Nothing exists on frame zero.** The world streams in around the player.
   Any "it will be ready by now" is a bug waiting for a slower machine or a
   larger planet. `universe.After <seconds> <command>` exists for scripts.

8. **Anything that works because there is exactly one of something is a bug
   waiting for the second one.** One player, one pawn, one viewpoint, one
   process, one log file. Sprint 007 found three defects of exactly this shape
   in one afternoon.

9. **Never replicate what both ends can compute, and never replicate a
   transform.** Two clients do not share a render origin.

10. **Never let a cache affect a result.** Caches exist everywhere here and
    every one of them can be cleared at any moment without changing any output.

11. **Do not change the frozen constants.** Cell size, domain tags, hash
    constants, `PlanetTerrainVersion`, `PlanetEnvironmentVersion`,
    `GalaxyGeneratorVersion`, `StarSystemGeneratorVersion`. Each regenerates the
    universe and invalidates every save. Each carries that sentence in its
    header.

## How to work on this

**Run things.** Every defect across eight sprints was found by running
something, not by reading code. The reports record the symptom for each one
precisely because the symptom was almost never what the cause looked like.

**Distrust plausible numbers.** The most expensive bugs here returned a
value that looked fine: a failure-path fallback, a precision limit, a quantity
measured in the wrong frame. "Under water 6,246 m" and "8,344,688 km from where
you were" both looked like data.

**Write the reason down at the point of the decision.** Not in a commit message,
which nobody reads at the point of confusion. The comments in this project are
long on purpose; a future reader needs the *why*, and the why is almost always
about a constraint that is not visible from the code.

**Prefer one code path.** Single player is the same classes as multiplayer with
`HasAuthority()` always true. The keys call the same functions the console
commands do. There is one segment-sphere sweep serving planets and stars. Every
one of those was a deliberate refusal to add a second implementation.

**Fix the test's assumption, not the code, when the test is what is wrong.**
The Sprint 006 walk "regression" was a test with a hardcoded two-second wait
that had been true by luck. It looked exactly like a broken walk.

## Where the sharp edges are

| Area | What to know |
|---|---|
| Coordinates | `TryGetRelativeCm` is exact only to ~0.01 ly. Galaxy-scale vectors go through cell space. |
| Galaxy-local | Light years in a double resolve to ~70 km at galactic radius. |
| Quadratics | The naive root formula loses everything at interstellar range. Use `FUniverseSweep`. |
| Terrain | Patch generation is ~43 ms on a worker thread. Collision is cooked *after* upload. |
| Lighting | A star needs two directional lights. One renders the ground black. |
| Networking | The tracked anchor is the *local* player's, always. |
| Persistence | Writes are server-only, guarded twice. `IsOpen()` and `IsUsable()` differ. |
| Unreal | `-PlayerName` is not a switch. `?Name=` gets rewritten. Use `?PlayerId=`. |

## What is not built

Deliberately, and listed so nobody assumes otherwise:

- No combat, mining, crafting, inventory, economy, factions, NPCs, quests.
- No orbital motion. Planet positions are a function of epoch phase, not an
  ephemeris.
- No galaxy geometry. A galaxy is a density function; drawing one as a mesh
  would invent a representation nothing reads.
- No client prediction beyond linear extrapolation of other players.
- No interest management beyond one server and one active system.
- No main menu or connect UI. The client connects from the command line.
- No audio.
- No cross-platform determinism for star and galaxy generation - `libm`.

## The obvious next steps

In rough order of value:

1. **Regional authority.** The pieces are shaped for it: address-derived
   identity, canonical positions, structural interest, an abstract store,
   explicit region subscription. The line that changes first is the server's
   streaming viewpoint, which is currently "the first player to join".
2. **Server-side terrain for the systems it hosts**, which is what would let
   movement validation become a collision check rather than a speed bound.
3. **A project-owned libm**, which closes the cross-platform determinism risk
   for star and galaxy generation in one move.
4. **Orbital motion.** The descriptors already carry periods and phases; nothing
   advances them.
5. **A main menu and connect UI.** The only reason the client still needs a
   command line.

## Related

- [../CLAUDE.md](../CLAUDE.md) - the specification
- [Setup.md](Setup.md)
- [Architecture/MVPArchitecture.md](Architecture/MVPArchitecture.md)
- [Sprints/](Sprints/) - what each sprint built, and what broke
