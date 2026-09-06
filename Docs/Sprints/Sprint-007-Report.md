# Sprint 007 Report — Multiplayer Universe Proof

*Unreal Engine 5.8.2, Windows 11. Every figure below was produced by running the
scripts quoted, not estimated.*

## What this sprint proves

**Two players share one deterministic universe, and the server owns everything
that matters.**

```text
powershell -File Tools\Multiplayer\TwoPlayerTest.ps1

  (server assigned entity id 02FBB369DADB00477D569150FA3320E9)

  [PASS] Server listened
  [PASS] Server published identity
  [PASS] Both clients joined
  [PASS] Ada handshake accepted
  [PASS] Bora handshake accepted
  [PASS] Distinct persistent ids
  [PASS] Ada sees Bora
  [PASS] Bora sees Ada
  [PASS] Build was server-side
  [PASS] Bora sees that exact entity

ALL CHECKS PASSED
```

The last line is the strongest one and is written the way it is deliberately.
"Did the other client receive something" is easy to satisfy and proves little.
This pulls the 128-bit entity id the *server* assigned out of the server's log
and finds the same id in the *second client's* region listing: a specific
object, created on one machine at the request of a second, observed on a third.
Nothing short of the whole path working produces that.

```text
powershell -File Tools\Multiplayer\PersistenceTest.ps1

  Built entity      : 0247594977DFFC096E2321BAB02F01D8
  Remembered cell X : 884049953158
  Teleport rejected : True

  [PASS] Session 1 built a structure
  [PASS] Server rejected a teleport
  [PASS] Client saw the correction
  [PASS] Server remembered the player
  [PASS] New server reopened the world
  [PASS] Player returned, not respawned
  [PASS] Structure survived restart
  [PASS] Returned to the same cell

ALL CHECKS PASSED
```

A player builds, tries to move a hundred light years in one frame, is refused,
disconnects. The server *process* is stopped and a new one started. The same
player reconnects, is recognised, and is put back in the cell they left from -
with the structure still there.

## The architecture

### Replicate four things and nothing else

The server and every client regenerate the same universe from the same seed, bit
for bit. Terrain, biomes, vegetation, stars, planets and weather are pure
functions of `(address, seed, time)`. Sending any of them would be sending data
the receiver can compute.

```text
seed and version identity     so both ends agree which universe this is
authoritative dynamic state   where players are, how they are moving
persistent deltas             the differences from the generated world
relevant entities             what is near enough to matter
```

Three of the four are small. That is what makes a universe 10^10 light years
across a tractable thing to share.

### Positions are data, not transforms

**No two clients share a render origin.** Each rebases Unreal's origin around
its own viewpoint about a thousand times during a planetary descent. A
replicated pawn transform is a statement in *somebody else's* render space, and
applying it locally places the other player wherever the two origins have
diverged.

So the canonical `FUniversePosition` replicates as data on the player state, and
each client converts it into its own render space on receipt. Remote players are
local proxy actors driven by that data. This is what section 42 - "different
local origins must not break shared positions" - actually requires; a replicated
transform is simply the wrong representation at this scale.

### Movement is client-simulated and server-validated

The textbook model - client sends input, server simulates - would require the
server to hold a fully streamed planet per player. That scales as player count
times planet cost, which is not the shape of a system that becomes an MMO.

The client proposes; the server checks the proposal is *possible*:

```text
distance moved <= max plausible speed x elapsed time x 4
```

Stated plainly: this stops a client claiming to be anywhere in the galaxy. It
does not stop one walking through a wall. Section 59 asks for the principle and
the seam, and both exist - every check is in `ValidateProposedMove`, nothing
bypasses it, and `universe.NetTeleportTest` makes the rejection fire on demand,
because a security check nobody has seen fire is a check that might not work.

### Interest is structural before it is metric

Unreal's default relevancy is a distance check in world space - a different
origin on every client, over distances spanning twenty orders of magnitude.
"Within 15,000 units" is not a meaningful question about two players four light
years apart.

The first question is whether they are in the same star system at all: two
integer addresses, cheap and exact. Only then do the metric bands apply.

## Test results

| Suite | Result |
|---|---|
| `TwoPlayerTest.ps1` | **ALL CHECKS PASSED** (10 checks, 3 processes) |
| `PersistenceTest.ps1` | **ALL CHECKS PASSED** (8 checks, 4 processes) |
| Standalone (`RunTests.bat`) | **79/79 tests, 1,241,356 assertions** |
| Unreal automation | **79/79 tests, 0 failures** |
| `universe.Journey 1` (Sprint 003) | **PASS**, 2.26 m walked, 2 frame transitions |
| `universe.InterstellarJourney` (Sprint 006) | **PASS**, 136.2 s out, 136.3 s back, 3 hazard stops |

Single player is unchanged. A standalone session is its own server: the same
game state, player state and controller classes run, `HasAuthority()` is always
true, and the persistence path is the direct one it always was. There is one
code path to reason about rather than a networked one and a "simple" one that
quietly diverges.

## Defects found, and what gave them away

Every one was found by running three processes and comparing their logs. **None
was visible from any single one of them** - which is the whole reason
`universe.NetPlayers` prints in a fixed format: so two logs can be diffed rather
than read.

### 1. Every replicated pawn claimed the tracked anchor

**Symptom:** both players reported being in no star system at all, while
standing on a planet.

**Cause:** `AUniverseProbePawn::BeginPlay` unconditionally called
`SetTrackedAnchor`. Correct in single player, where there is one pawn. With two
players, the other player's pawn replicates in, its `BeginPlay` runs, it takes
the anchor - and this client starts streaming the universe around somebody who
has never been placed locally and is therefore at the universe origin. The
streamer duly deactivated the system they were standing in and went to look at
intergalactic space.

**Fix:** locally controlled pawns only. The tracked anchor is "the viewpoint
this client renders around", which is by definition the local player's.

### 2. A dedicated server had no viewpoint at all

**Symptom:** a client would land on a planet, ask to build, and be told the
planet was "not active on the server". True, and true of every planet.

**Cause:** the fix above. A dedicated server has no locally controlled pawn, so
nothing claimed the anchor and the streamer had nowhere to stream around.

**Fix:** the first player to join becomes the server's streaming viewpoint. That
is honestly a single-region-server assumption, written down as such, and it is
the line that changes when regional authority arrives.

### 3. Clients spawned in intergalactic space

**Symptom:** both players at cell `[0,0,0]`, no system, nothing to land on.

**Cause:** the probe pawn takes its start pose from the game mode, which exists
only on the server. On a client it got nothing - and since Sprint 006 the
universe origin is intergalactic space.

**Fix:** the server decides where a player goes, publishes it on the player
state, and the client adopts it once. Once only: after that the client is the
simulation authority for its own movement, and re-adopting on every replication
would fight the player's controls.

### 4. The build range check measured elevation as distance

**Symptom:** `Build refused: 11.2 km from the player, limit 1000 m` - from a
player standing exactly on the spot.

**Cause:** the check computed the build point at *sea level*. The player was on
an 11,174 m mountain.

**Fix:** measured tangentially, at the player's own distance from the planet
centre. Elevation is not distance from the thing you are building.

### 5. Durable identity took three attempts

Each failure is worth keeping because each looked like it worked:

- **A fresh GUID per connection** is perfectly stable and completely useless. A
  reconnecting player was a stranger, so they respawned at the start point and
  their remembered position was filed under a name nobody would present again.
- **`GetPlayerName()` at controller `BeginPlay`** is empty: the login has not
  run yet, so the name path never fired and the GUID fallback always did.
- **`GetPlayerName()` after login** holds a generated nickname like
  `blizz-A8C9B75B425288`, different every session. So does the `?Name=` URL
  option, because the engine rewrites it with that same nickname on the way out.

Identity now comes from `?PlayerId=`, a URL option nothing in the engine claims,
read in `InitNewPlayer` before anything can replace it.

Also: `-PlayerName` is not an Unreal command-line switch. It is silently
ignored, which is why two reconnect runs failed for a reason having nothing to
do with reconnecting.

### 6. `IsOpen()` and `IsUsable()` are different questions

**Symptom:** `universe.Build` reported "world persistence is unavailable" on a
client that was connected, landed, and perfectly able to ask the server.

**Cause:** every caller guarded on `IsOpen()`, which asks "is there a database
here". On a client the answer is correctly no - and irrelevant, because a client
reads what the server sent it and requests changes rather than making them.

### 7. Two harness bugs worth recording

- `Start-Process cmd.exe /c "<quoted command>"` re-quotes an argument containing
  spaces, turning a correct command line into one cmd parses differently. The
  symptom was not an error: it was a client that silently never started and a
  log file that never appeared.
- Both reconnect sessions wrote to the same client log, so session two deleted
  the very evidence session one had produced. Name and log tag are separate
  parameters now, because they answer different questions.

## The lesson

Three of these are the same shape:

> **Anything that works because there is exactly one of something is a bug
> waiting for the second one.**

One player, one pawn, one viewpoint, one process, one log file. That has been
added to the standing rules in the README.

## Acceptance criteria

| Area | Criterion | Evidence |
|---|---|---|
| Server | Runs headlessly, owns world state and persistence writes | `RunServer.bat`; both scripts |
| Server | Restart preserves the world | PersistenceTest: structure survived |
| Connection | Multiple clients, handshake, identity verified | TwoPlayerTest: both accepted |
| Connection | Player identity separate from connection identity | `?PlayerId=`, survives reconnect |
| Connection | Reconnect works | PersistenceTest: returned to the same cell |
| Position | Server-authoritative canonical position | `AUniversePlayerState` |
| Position | Client origins may differ safely | Canonical data + local proxies |
| Position | Movement works on planets and in space | Both journeys still PASS |
| Interest | Universe-aware relevance; different systems get nothing | `ClassifyRelevance`, structural first |
| Interest | Region subscription and unsubscription | `ServerSubscribeRegion`, 64-region bound |
| World state | Building and removal server-authoritative | Server log: "built ... at <id>" |
| World state | Deltas replicate; join-in-progress gets region state | Whole region on subscription |
| Shared sim | Coherent day/night and weather | One replicated clock, 5 s interval |
| Persistence | Client cannot mutate canonical state | Guarded twice; no client database |
| Persistence | Player and world state persist across restart | PersistenceTest |
| Testing | Startup, connection, consistency, replication, reconnect | Both scripts |
| Testing | Sprints 001-006 still pass | 79/79 both harnesses; both journeys PASS |

## Known limitations, stated rather than hidden

- **Movement validation is a speed bound, not a collision check.** It refuses
  the impossible, not the improbable. Tightening it requires server-side
  terrain, which is the scaling problem the whole design avoids.
- **The server's streaming viewpoint is the first player to join.** Players in
  different systems are correctly irrelevant to each other, but the server only
  has one system fully built - so world edits are validated only in that one.
- **Exactly one system may be `Active`.** From Sprint 006, and about the
  simulation frame rather than networking.
- **A dedicated server target cannot be built on a launcher engine.**
  `UniverseServer.Target.cs` is committed and correct; `RunServer.bat` runs the
  same code in the same net mode from the editor binary and explains why.
- **No client-side prediction or reconciliation for other players' motion**
  beyond linear extrapolation. At the speeds involved, smoothing would draw
  people behind where they are.
- **Player position is written on logout, not continuously.** A crash loses a
  position and nothing else - a trade made explicitly, because persisting it at
  frame rate would be thousands of writes a minute to record something read
  once.
- **The wire forms of the persistence types are a second representation.** They
  hold no judgement of their own, but they are a place a field can be forgotten.

## Files added

```text
Source/UniverseServer.Target.cs
Source/Universe/Public/UniverseNetTypes.h            + .cpp
Source/Universe/Public/UniverseGameState.h           + .cpp
Source/Universe/Public/UniversePlayerState.h         + .cpp
Source/Universe/Public/UniversePlayerController.h    + .cpp
Source/Universe/Public/UniverseNetSubsystem.h        + .cpp
Source/Universe/Public/RemotePlayerAvatar.h          + .cpp
Source/Universe/Private/UniverseNetCommands.cpp
Source/Universe/Private/UniverseDeferredCommands.cpp
Tools/Multiplayer/RunServer.bat
Tools/Multiplayer/RunClient.bat
Tools/Multiplayer/TwoPlayerTest.ps1
Tools/Multiplayer/PersistenceTest.ps1
Docs/Architecture/Networking.md
Docs/ADR/ADR-008-server-authority-and-replication.md
```
