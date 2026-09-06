# Networking

*Sprint 007. Source: `Source/Universe/Public/UniverseNetTypes.h`,
`UniverseGameState.h`, `UniversePlayerState.h`, `UniversePlayerController.h`,
`UniverseNetSubsystem.h`.*

## The one idea

**Do not replicate the universe.**

The server and every client can already regenerate the same world from the same
seed, bit for bit. Terrain, biomes, vegetation, stars, planets, weather and the
time of day are all pure functions of `(address, seed, time)`. Sending any of
them would be sending data the receiver can compute.

What actually has to cross the wire is only the part that is not derivable:

```text
seed and version identity     so both ends agree which universe this is
authoritative dynamic state   where players are, how they are moving
persistent deltas             the differences from the generated world
relevant entities             what is near enough to matter
```

That is four things, and three of them are small. It is what makes a universe
10^10 light years across a tractable thing to share.

## World identity

`FUniverseWorldIdentity` carries the seed and every generation version:

| Field | Bumping it means |
|---|---|
| `SeedValue`, `SeedText` | A different universe entirely. |
| `GalaxyVersion` | Every star is somewhere else. |
| `SystemVersion` | Every planet is different. |
| `TerrainVersion` | The ground is a different shape. |
| `EnvironmentVersion` | Different climate, biomes and trees. |
| `PersistenceSchemaVersion` | The save format changed. |

The server publishes it; every client rebuilds what *its* build would produce
for the server's seed and compares.

**A mismatch is a refusal, not a warning.** There is no version of "mostly the
same universe" worth playing in, and a silent partial mismatch is worse than a
failed connection because it looks like it works: a client with a different
terrain version walks on ground the server does not think is there, falls
through it, and gets corrected into a hillside.

## Authority

| State | Owner |
|---|---|
| Player identity | Server |
| Canonical player position | Server (validated), client (simulated) |
| Structure placement and removal | Server, no prediction |
| Procedural object removal | Server, no prediction |
| Persistent world state | Server, exclusively |
| Terrain, biomes, vegetation | Nobody - both ends compute it |
| Time of day, weather | Derived from one replicated clock |

### Movement: client-simulated, server-validated

The textbook arrangement - client sends input, server simulates, server sends
back the result - does not work here, and the reason is interesting rather than
merely inconvenient.

Simulating a player means having their planet's terrain, environment,
vegetation and collision built on the server. That is affordable for one player
and it is exactly what Sprint 006 established a server cannot afford for many: a
server holding a fully streamed planet per player scales as player count times
planet cost, which is not the shape of a system that becomes an MMO.

So the client runs the deterministic simulation it already has, proposes where
it now is, and the server checks the proposal is *possible*:

```text
distance moved <= max plausible speed for the regime
                  x elapsed time
                  x tolerance factor (4)
```

The tolerance is not 1.0 and that is deliberate. A client's frame is not the
server's, packets arrive in bursts, and a legitimate move measured over a
slightly wrong interval looks slightly too fast. Four times the limit still
refuses a teleport by several orders of magnitude while never refusing an honest
player.

**What this stops and what it does not.** It stops a client claiming to be
anywhere in the galaxy, and it stops movement at a thousand times a drive's
capability. It does not stop a client walking through a wall. That is stated
plainly rather than dressed up: section 59 of the sprint asks for the principle
and the seam, and both exist - every check is in `ValidateProposedMove` and
nothing bypasses it. Tightening it means server-side terrain, which is the
scaling problem above.

`universe.NetTeleportTest` makes the rejection fire on demand. A security check
nobody has ever seen fire is a check that might not work.

### World edits: server-authoritative, no prediction

Building and removing are rare, deliberate, durable acts. Paying a round trip
for one is unnoticeable; getting one wrong writes a permanent lie into the world
database. So there is no client prediction at all, and the server re-derives
everything:

- The planet must be one the **server** actually has streamed, not one the
  client names.
- The placement must be valid.
- The player must be within range of where they are editing - **measured
  tangentially**, at their own distance from the planet centre. The first
  version measured to sea level and refused a player standing exactly on the
  spot, because they were on an 11,174 m mountain. Elevation is not distance
  from the thing you are building.

## Positions

`FUniversePosition` is an int64 cell index per axis plus a double local offset.
It cannot be replicated as an `FVector` - a float loses metre resolution at ten
thousand kilometres - and it cannot be replicated as itself, because it lives in
UniverseCore, which does not depend on the engine and must keep compiling
standalone. `FReplicatedUniversePosition` is the wire form.

### Why positions are on the player state, not the pawn

**No two clients share a render origin.** Each rebases Unreal's origin around
its own viewpoint - about a thousand times during a single planetary descent. A
replicated pawn transform is expressed in *someone else's* render space, and
applying it locally places the other player wherever the two origins happen to
have diverged.

Replicating the canonical position instead makes that impossible. Each client
converts to its own render space on receipt and both are right. This is what
section 42 - "different local origins must not break shared positions" -
actually requires; it is not a workaround.

It also puts the position on the thing that survives pawn changes. Boarding a
ship replaces the pawn; it does not move the player.

### Remote players are local proxies

`ARemotePlayerAvatar` is spawned by each client, never replicated, and driven
from the replicated canonical position. Between updates it extrapolates by the
replicated velocity - deliberately not smoothed toward the last received
position, because at warp the extrapolation is light-seconds long and easing
into it would draw the other player consistently behind where they are.

## Interest management

Unreal's default relevancy is a distance check in world space, which is useless
here: world space is a different origin on every client, and the distances span
twenty orders of magnitude. "Within 15,000 units" is not a meaningful question
about two players four light years apart.

The relevant question is **structural before metric**:

| Class | Test | Sent |
|---|---|---|
| `Irrelevant` | Different system, or none | nothing |
| `SameSystem` | Same system, far apart | position, slowly |
| `SameRegion` | Within 10^9 m | position and heading |
| `Visible` | Within 5 x 10^4 m | everything, and an avatar |

Same-system is a comparison of two integer addresses - cheap and exact, where
subtracting two positions four light years apart is arithmetic on numbers near
10^10. A player in another system is not "far away" in a sense a distance
captures; they are somewhere this player cannot see, reach quickly, or interact
with.

## Region subscription

Persistent deltas are the one kind of world data that genuinely has to be sent,
because they are not derivable from anything.

```text
client streams terrain
  -> asks WorldState for a region  (unchanged since Sprint 005)
  -> on a client that becomes ServerSubscribeRegion
  -> server loads it and sends the whole region once
  -> later changes arrive as single-entity messages
```

The routing is inside `UWorldStateSubsystem::RequestRegion` rather than at each
call site. Vegetation, structures and the debug commands all already asked this
subsystem for a region before drawing anything, and every one of them keeps
working unchanged - the answer arrives through the same `OnRegionLoaded`
delegate it always did, it simply comes from the server now. Doing it per caller
would have been a dozen edits and a dozen chances to forget one, and the one
forgotten would be a client quietly drawing a tree somebody else chopped down.

The server bounds what it will honour (64 regions per client). A whole region is
sent on subscription and never again, so a client cannot end up with half of
one; subsequent changes are single entities, and a re-sent creation is a no-op
because the apply path replaces by id rather than appending.

## Persistence authority

Writes are server-only, guarded twice:

1. Every mutating function on `UWorldStateSubsystem` refuses without authority.
2. A client has **no database open at all**. `OpenWorld` is a no-op there, so a
   write that slipped past the checks would fail at the store.

Two independent reasons for the same guarantee is deliberate: one of them will
eventually be edited by somebody who does not know about the other.

`IsOpen()` and `IsUsable()` are different questions. `IsOpen()` asks "is there a
database here", and on a client the answer is correctly no. But a client can
still read what the server sent and still request changes, so every caller that
guarded on `IsOpen()` was refusing to work on a client for a reason that does
not apply there - which is exactly what `universe.Build` did, reporting "world
persistence is unavailable" on a client that was connected, landed and perfectly
able to ask.

## Player identity

`PersistentId` is stable across reconnects; Unreal's `PlayerId` is a
connection-lifetime integer it reuses. Everything durable keys on the former and
nothing durable keys on the latter.

It comes from the connection URL option `?PlayerId=`, read in `InitNewPlayer`
before anything can replace it. Three earlier attempts failed and the reasons
are worth keeping:

- **A fresh GUID per connection** is perfectly stable and completely useless: a
  reconnecting player is a stranger, so they respawn at the start point and
  their remembered position is filed under a name nobody will present again.
- **`GetPlayerName()` at controller `BeginPlay`** is empty, because the login
  has not run yet.
- **`GetPlayerName()` after login** holds a machine-generated nickname like
  `blizz-A8C9B75B425288` that differs every session. So does the `?Name=` URL
  option, because the engine rewrites it with that same nickname on the way out.

An account service replaces exactly those four lines. Nothing else in the
project knows how the string was made.

## The shared clock

Weather and time of day were already pure functions of `(place, time)` from
Sprint 004, so making them coherent between players is not a replication problem
at all - it is the problem of agreeing on `time`.

`AUniverseGameState` replicates one number every five seconds and both ends
advance it locally in between. A clock that only moved when a packet arrived
would make the sun stutter; one never corrected would drift apart over a
session. Corrections are snapped rather than eased: the sun and the weather
change on a scale of minutes, so a few hundred milliseconds is invisible, and
easing would mean the two players are never actually agreed, only converging.

## Single player is unchanged

A standalone session is its own server. The same classes run, `HasAuthority()`
is always true, the handshake is trivially satisfied, and the persistence path
is the direct one it always was. There is one code path to reason about rather
than a networked one and a "simple" one that quietly diverges.

## What a streamed world broke, and what that taught

Three defects in this sprint were the same shape, and none was visible from any
single process:

- **Every replicated pawn claimed the tracked anchor in `BeginPlay`.** Correct
  with one player; with two, another player's pawn takes over this client's
  viewpoint, and the streamer starts streaming the universe around somebody who
  has never been placed locally and is therefore at the origin. The symptom was
  both players reporting no star system at all.
- **A dedicated server has no locally controlled pawn**, so after that fix
  nothing claimed the anchor and the server streamed around nothing. A client
  would land on a planet and be told the planet was "not active on the server".
- **Clients spawned at the universe origin**, because the pawn takes its start
  pose from the game mode and the game mode is server-only.

The general lesson: **anything that works because there is exactly one of
something is a bug waiting for the second one.** One player, one pawn, one
viewpoint, one process.

## Scaling path

This sprint is one server, one Active system, and a handful of players. What is
already in the right shape for more:

- **Identity is address-derived and position is canonical.** Two servers
  simulating different systems would agree about both without talking.
- **Interest is structural.** "Same system" is the natural unit of regional
  authority, and it is already the first question asked.
- **Persistence is behind an interface.** `IWorldPersistenceStore` has one
  SQLite implementation and no caller knows that.
- **Region subscription is explicit.** A client already asks for regions by
  name, which is what a router would forward.

What is not, and is called out rather than hidden:

- The server's streaming viewpoint is **the first player to join**. That is a
  single-region-server assumption, and it is the line that changes when regional
  authority arrives.
- Exactly one system may be `Active` on the server, so all players must be in
  the same one to interact fully.
- Movement validation is a speed bound, not a collision check.

## Running it

```bat
Tools\Multiplayer\RunServer.bat [port] [seconds] [logtag]
Tools\Multiplayer\RunClient.bat [address] [name] [seconds] [execcmds] [logtag]

powershell -File Tools\Multiplayer\TwoPlayerTest.ps1
powershell -File Tools\Multiplayer\PersistenceTest.ps1
```

`Source/UniverseServer.Target.cs` is a correct dedicated-server target and is
what a source-built engine should use. The Epic Games Launcher build of UE 5.8
cannot compile it - UnrealBuildTool refuses with "Server targets are not
currently supported from this engine distribution" - so `RunServer.bat` runs the
same code in the same net mode (`NM_DedicatedServer`, no local player, clients
over the net driver) from the editor binary.

Both scripts run in **real time**, not `-benchmark`. Every other automated run
in this project advances a fixed 1/30 s per frame as fast as the machine will
go; the network runs in real time regardless, so an accelerated server and a
real-time socket disagree about how long a second is and every timeout in the
stack starts firing.

## Diagnostics

| Command | Reports |
|---|---|
| `universe.NetInfo` | Net mode, identity, connections, players, interest, bandwidth |
| `universe.NetPlayers` | Every player's canonical position, in a fixed format for diffing |
| `universe.NetTeleportTest` | Claims an impossible move so the rejection can be seen |
| `universe.After <s> <cmd>` | Defers a command past frame zero |

The HUD's NETWORK panel shows role, handshake, clock drift, ping, bandwidth,
other players by relevance, and the correction count.

A multiplayer bug is almost never visible from one side: "the other player is in
the wrong place" is, from each machine's point of view, a statement that the
*other* machine is wrong. `universe.NetPlayers` prints in a fixed format
precisely so that two logs can be diffed rather than read.

## Related

- [WorldPersistence.md](WorldPersistence.md) - what is durable
- [StarSystemStreaming.md](StarSystemStreaming.md) - what exists at all
- [UniverseCoordinates.md](UniverseCoordinates.md) - the canonical position
- [ADR-008](../ADR/ADR-008-server-authority-and-replication.md)
