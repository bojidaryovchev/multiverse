Read `CLAUDE.md` completely before doing anything else.

Then inspect the repository and verify the outputs of Sprint 001 through Sprint 006.

We are beginning:

# Sprint 007 — Multiplayer Universe Proof

The objective of this sprint is to convert the current single-player procedural universe into the first **authoritative shared-universe implementation**.

At the end of this sprint, at least two players should be able to:

```text
connect to the same dedicated server
↓
exist in the same deterministic universe
↓
see the same stars / systems / planets
↓
travel independently
↓
meet in the same star system
↓
land on the same procedural planet
↓
see one another
↓
walk around together
↓
see the same persistent world modifications
↓
place / remove structures authoritatively
↓
leave
↓
reconnect
↓
find shared state preserved
```

The sprint must also establish the architectural foundation for future:

```text
many players
↓
interest management
↓
regional authority
↓
multiple simulation servers
↓
seamless server handoff
↓
massively multiplayer universe
```

We are NOT implementing true MMO scale yet.

We are proving that the existing universe architecture can support it.

---

# 0. PRECONDITION — VERIFY SPRINTS 001–006

Before implementing multiplayer, verify the existing project.

## Sprint 001

* canonical universe coordinates work
* deterministic universe generation works
* large-distance movement works
* tests pass

## Sprint 002

* spherical planet terrain works
* patch streaming / LOD works
* tests pass

## Sprint 003

* seamless space → surface traversal works
* planetary gravity works
* character / ship transitions work
* tests pass

## Sprint 004

* climate / biomes / vegetation / weather / wildlife work
* environmental streaming works
* tests pass

## Sprint 005

* world-state layer exists
* persistence abstraction exists
* persistent structures work
* procedural removals work
* restart persistence works
* tests pass

## Sprint 006

* multiple systems work
* interstellar / warp travel works
* galaxy hierarchy works
* system streaming works
* persistent state survives interstellar travel
* tests pass

Fix only blocking defects.

Do not rewrite foundational systems merely because multiplayer exposes them.

---

# 1. PRIMARY OBJECTIVE

Convert world authority from:

```text
local client
```

to:

```text
authoritative dedicated server
```

for multiplayer-relevant state.

The server must become authoritative for at least:

* player identity
* player position / movement state
* spacecraft position / movement state
* current universe location
* world interactions
* structure placement
* structure removal
* procedural-object removal
* persistent world state

Clients may still generate deterministic visual content locally where safe.

This distinction is critical.

---

# 2. FUNDAMENTAL MULTIPLAYER PRINCIPLE

Do NOT replicate the entire procedural universe.

The server and clients already know how to regenerate the same deterministic world.

Replicate:

```text
seed/version identity
+
authoritative dynamic state
+
persistent deltas
+
relevant entities
```

Do NOT replicate:

```text
every terrain vertex
every tree
every biome sample
every star descriptor
every untouched planet object
```

unless a specific subsystem truly requires authoritative transfer.

The procedural universe itself is effectively shared mathematical data.

---

# 3. OUT OF SCOPE

Do NOT build:

* hundreds/thousands of concurrent users
* production matchmaking
* global account service
* monetization
* guilds / corporations
* chat platform
* voice chat
* distributed databases
* global persistence cluster
* production Kubernetes
* fleet combat
* anti-cheat production system
* giant MMO server mesh
* complex latency compensation
* production CDN
* global deployment

This sprint is about architecture and proof.

---

# 4. DEDICATED SERVER

Build and run an Unreal dedicated server.

Requirements:

* headless server target
* no rendering dependency
* server launches cleanly
* multiple clients can connect
* server owns canonical multiplayer state
* server can load/generate universe descriptors
* server can access persistence layer

Document build/run instructions.

Create a reproducible development command/script.

---

# 5. SERVER COMPATIBILITY AUDIT

Audit the current codebase.

Core systems should be usable without rendering.

At minimum:

```text
Universe generation
Galaxy generation
System generation
Planet descriptors
Climate / biome queries
World State
Persistence
Coordinates
Movement math
```

should not require a graphical viewport.

If rendering dependencies have leaked into core logic, separate them carefully.

Do not rewrite functioning algorithms.

---

# 6. SERVER WORLD IDENTITY

At startup, the server must establish authoritative world metadata:

```text
World Save ID
Universe Seed
Universe Generator Version
Galaxy Generator Version
System Generator Version
Terrain Generator Version
Environment Generator Version
Persistence Schema Version
```

Clients must receive/validate required identity metadata.

A client must not silently join with incompatible generation rules.

---

# 7. CLIENT COMPATIBILITY HANDSHAKE

During connection, validate:

```text
protocol version
content/build version where required
generation versions
world identity
```

If incompatible:

disconnect clearly with a useful error.

Do not allow a client with different procedural rules to interpret the universe incorrectly.

---

# 8. PLAYER IDENTITY

Create a minimal multiplayer player identity.

For Sprint 007:

```text
PlayerId
DisplayName/debug name
Connection/session identity
```

is enough.

Do NOT build authentication.

The server assigns or validates authoritative identity.

Use stable local development identities if reconnect testing requires it.

---

# 9. PLAYER SESSION MODEL

Separate:

```text
persistent player identity
```

from:

```text
network connection
```

A disconnected player can reconnect with a new connection but retain their saved state.

Do not use network connection ID as permanent player identity.

---

# 10. AUTHORITATIVE PLAYER POSITION

The server must own canonical player location.

Do NOT make replicated `ActorLocation` the only source of truth.

Canonical state remains based on our universe hierarchy.

Conceptually:

```text
FUniversePosition
+
current frame/context
+
orientation
+
velocity
```

The local Unreal Actor transform is a representation.

---

# 11. MOVEMENT AUTHORITY

Choose a clear prototype model.

Recommended starting point:

```text
client input
↓
server
↓
authoritative movement simulation
↓
replicated state
↓
client prediction / smoothing where needed
```

For ordinary walking, leverage Unreal CharacterMovement networking where useful.

For custom astronomical movement, integrate it with our canonical movement architecture.

Do not create two incompatible networking systems.

---

# 12. MOVEMENT REGIMES

Network the existing travel regimes:

```text
Character Surface Movement
Spacecraft Local Flight
Interplanetary Travel
Interstellar Travel
Warp
```

The server must know:

```text
canonical position
canonical velocity
active travel mode
```

Do not let clients arbitrarily announce:

```text
"I am now in another galaxy."
```

without server validation.

---

# 13. CLIENT-SIDE PREDICTION

Use prediction only where it improves responsiveness.

Surface walking and local ship flight may need it.

Interstellar travel can tolerate simpler deterministic server correction initially.

Do not prematurely design a custom prediction engine for every mode.

Start with:

* authoritative state
* client interpolation
* basic prediction where Unreal already provides it

---

# 14. POSITION REPLICATION

Do NOT send huge redundant coordinate structures every frame if avoidable.

Design compact network serialization for canonical positions.

Potential concept:

```text
coarse cell identity
+
local position
```

Use custom NetSerialize where appropriate.

Measure packet size.

Document precision.

---

# 15. ORIENTATION / VELOCITY REPLICATION

Replicate only required state.

Use sensible quantization if appropriate.

Do not sacrifice correctness prematurely for a few bytes.

Measure before optimizing.

---

# 16. INTEREST MANAGEMENT

A player on Planet A should not receive continuous state for:

```text
Player B 80 million light-years away
```

Introduce explicit interest management.

Conceptually:

```text
Universe proximity hierarchy
↓
Galaxy relevance
↓
System relevance
↓
Planet relevance
↓
Local spatial relevance
```

Different entity classes can have different relevance ranges.

---

# 17. INTEREST REGIONS

Define a logical interest-region concept.

Examples:

```text
same local surface area
same planet
same star system
nearby interstellar area
```

Do NOT base all interest solely on Unreal's local world distance.

Canonical universe position must drive relevance.

---

# 18. PLAYER VISIBILITY

Example behavior:

## Same surface region

Replicate:

* detailed character
* ship
* structures
* local dynamic objects

## Same planet but opposite side

Probably do NOT replicate full character detail.

Maybe only higher-level presence if gameplay later requires it.

## Same star system

Could replicate:

* ship proxy
* signal/contact state

## Different systems

No realtime gameplay replication required.

Document current prototype policy.

---

# 19. REPLICATION GRAPH / IRIS EVALUATION

Evaluate current Unreal networking options.

Use built-in systems where useful.

Potentially:

* Replication Graph
* Iris
* standard Actor replication

Do not adopt experimental complexity merely because it exists.

Choose the simplest robust solution for Sprint 007.

Keep our logical universe interest layer independent of one Unreal replication backend.

---

# 20. PROCEDURAL WORLD CLIENT GENERATION

Clients may independently regenerate:

```text
terrain
biomes
vegetation
distant star systems
```

from the authoritative seed/version state.

The server does NOT need to stream all geometry.

However, the server must be authoritative for interaction-relevant facts.

For example:

```text
Tree X was removed
Building Y exists
```

must come from World State.

---

# 21. SERVER TERRAIN KNOWLEDGE

The server should be able to perform deterministic surface queries without rendering.

It may need to validate:

* player is above terrain
* building placement
* slope
* water state
* location
* collision approximations

Do not require full visual terrain mesh generation to validate every action.

Use deterministic mathematical queries where possible.

---

# 22. SERVER-SIDE BUILDING PLACEMENT

Sprint 005 building placement becomes server-authoritative.

Flow:

```text
client proposes placement
↓
server validates
↓
server creates persistent entity ID
↓
World State commits it
↓
server replicates creation
↓
clients instantiate representation
```

The client must not directly write persistent storage.

---

# 23. BUILDING VALIDATION

Server validates:

```text
world identity
player location
planet
surface location
slope
allowed distance
overlap rules
water state where applicable
```

Keep rules simple.

The important thing is authority.

---

# 24. PROCEDURAL OBJECT REMOVAL

Convert:

```text
remove tree
```

into a server-authoritative interaction.

Flow:

```text
client requests removal(EntityId)
↓
server validates entity identity / relevance
↓
World State records tombstone
↓
server tells interested clients
↓
clients suppress/remove entity
```

Do not trust the client to decide stable identity unchecked.

---

# 25. SHARED PERSISTENCE

Two clients must observe identical current-world state.

Example:

```text
Player A places beacon

Player B sees beacon

Player B reconnects

beacon still exists
```

Persistent state is global for the server world save.

---

# 26. SHARED PROCEDURAL REMOVALS

Example:

```text
Player A removes tree X

Player B:
tree disappears

Player B reconnects:
tree remains absent
```

No client-local divergence.

---

# 27. WORLD STATE AUTHORITY

The architecture should now become:

```text
Clients
    ↓
Authoritative Gameplay Requests
    ↓
Server World State
    ↓
Persistence Store
    ↓
Replicated Relevant Changes
```

Do not let gameplay code query SQLite directly.

World State remains the semantic authority layer.

---

# 28. WORLD STATE EVENTS

Server-side World State should emit relevant events:

```text
EntityCreated
EntityRemoved
EntityUpdated
ProceduralEntityRemoved
```

Networking translates these into client-relevant replication.

Do not tightly couple SQLite callbacks to replication components.

---

# 29. PLAYER PERSISTENCE

Persist per-player state server-side.

At minimum:

```text
canonical position
orientation
current mode
ship state
```

When player reconnects:

```text
server loads state
↓
selects proper universe/system/planet context
↓
spawns player safely
```

---

# 30. RECONNECT

Required scenario:

```text
Player A on Planet X
↓
disconnect
↓
Player B remains online
↓
Player A reconnects
↓
returns to correct saved context
```

Do not require restarting server.

---

# 31. SERVER RESTART

Required scenario:

```text
A + B modify world
↓
disconnect
↓
stop dedicated server
↓
restart dedicated server
↓
A + B reconnect
```

Shared persistent state must remain.

---

# 32. JOIN-IN-PROGRESS

A new client joining an active region must receive current state.

Example:

```text
Player A has:
- placed 20 structures
- removed 5 procedural trees

Player B joins later
```

Player B must not briefly see pristine base state as final truth.

Load/apply relevant deltas before or during representation activation.

---

# 33. RELEVANT STATE SNAPSHOT

Design a region snapshot or equivalent mechanism.

When a client becomes interested in a region:

```text
procedural seed/version already known
+
persistent/current dynamic region state
=
correct local world
```

Do not send every untouched procedural entity.

---

# 34. DELTA UPDATES

After initial region state:

send only changes.

For example:

```text
EntityCreated
EntityRemoved
EntityStateChanged
```

Avoid repeatedly sending the entire region snapshot.

---

# 35. REGION SUBSCRIPTION

Conceptually:

```text
client enters interest region
↓
subscribe
↓
receive current state
↓
receive deltas
↓
client leaves
↓
unsubscribe
```

This is a useful abstraction for future distributed simulation.

---

# 36. REGION VERSION / SEQUENCE

Consider a lightweight revision/sequence value for region dynamic state.

This may help detect missed/out-of-order updates.

Do not build a full consensus protocol.

A simple monotonically increasing server revision may be sufficient.

---

# 37. LATE / DUPLICATE MESSAGES

Network updates may arrive:

* late
* duplicated
* out of order

Design idempotent state application.

Examples:

```text
Remove Entity X twice
```

must be harmless.

---

# 38. MULTIPLAYER SPACE TRAVEL

Players should be able to independently travel.

Scenario:

```text
Player A stays on Planet A

Player B:
takeoff
→ warp
→ System B

Player A should not receive detailed Player B updates indefinitely.
```

Interest management must reduce traffic.

---

# 39. MEETING IN SPACE

Required test:

```text
Player A and B target same region
↓
approach one another
↓
enter mutual relevance range
↓
ship representations appear
```

No major positional disagreement.

---

# 40. MEETING ON PLANET

Required test:

```text
A lands first

B arrives later
↓
both see same terrain/environment
↓
both see each other
↓
both see same persistent structures
```

This is the main visual multiplayer proof.

---

# 41. PLANETARY FRAME NETWORKING

Players on a spherical planet may have different local Unreal origins.

Do NOT assume both clients share identical local floating-point transforms.

Replication should be based upon canonical shared position.

Each client converts:

```text
canonical position
↓
its own local simulation frame
↓
local Unreal transform
```

This is a crucial architectural requirement.

---

# 42. DIFFERENT CLIENT ORIGINS

Explicitly test:

```text
Client A origin rebase != Client B origin rebase
```

while both observe the same entity.

Their local actor coordinates may differ.

The entity should visually occupy the same canonical world location.

---

# 43. SERVER LOCAL FRAME

The dedicated server may not need the same local visual frame as a client.

Avoid assuming:

```text
server ActorLocation == client ActorLocation
```

for canonical state.

Keep universe coordinates authoritative.

---

# 44. SURFACE ENTITY REPLICATION

Persistent structures and players on planets should replicate canonical planet-relative transforms.

Do not send only transient local actor transforms if that breaks under rebasing.

---

# 45. CROSS-FACE MULTIPLAYER

Test players around cube-sphere face boundaries.

Example:

```text
A on face +X
B crosses into +Y
```

They should continue to see each other correctly if still within relevance.

No topology leakage.

---

# 46. WARP NETWORKING

Warp travel need not replicate at high frequency to irrelevant players.

For players near each other during warp:

replicate enough state for coherent motion.

For faraway players:

presence can become abstract or irrelevant.

Do not send 60 Hz transforms across the entire universe.

---

# 47. NETWORK FREQUENCY BY RELEVANCE

Introduce tiered update rates.

Example:

```text
very near:
high frequency

near:
moderate frequency

far but same system:
low frequency / proxy

different system:
none
```

Use measurements, not arbitrary complexity.

---

# 48. ENTITY RELEVANCY CLASSES

Different entities need different policies.

Potential classes:

```text
Player
Spacecraft
Building
Wildlife
Weather
Procedural Static Content
```

Do NOT replicate:

```text
every bird
```

globally.

---

# 49. WILDLIFE AUTHORITY

For Sprint 007, choose a simple approach.

Possible:

```text
wildlife is client-local cosmetic
```

or:

```text
server owns only interaction-relevant wildlife
```

Since wildlife currently exists mostly to make worlds feel alive, client-local deterministic cosmetic wildlife is acceptable if it has no gameplay authority.

Document the choice.

---

# 50. WEATHER AUTHORITY

Dynamic weather should eventually be shared.

For this sprint:

server should at least own enough weather state/time seed so clients in the same region do not see completely contradictory conditions.

Do not replicate every raindrop/cloud particle.

Replicate/query the authoritative weather state.

---

# 51. DAY/NIGHT SYNC

Players on the same planet should agree on:

```text
planetary time
star direction
day/night state
```

Server owns simulation time.

Clients render from it.

---

# 52. SHARED SIMULATION CLOCK

Introduce server-authoritative simulation time if not already present.

Use it for:

* day/night
* weather
* future simulation

Do not depend on each client's wall clock.

---

# 53. NETWORK CLOCK SYNC

Provide enough clock synchronization/interpolation for smooth shared dynamic systems.

Do not overbuild distributed time protocols.

---

# 54. NETWORKING DEBUG HUD

Display:

```text
Role:
Server / Client

Player ID
Connection ID

Ping
Packet loss if available

Canonical player position
Local actor position

Current interest region
Subscribed regions

Relevant replicated entities

Incoming bytes/sec
Outgoing bytes/sec
Replication rate
Corrections
```

Toggleable.

---

# 55. SERVER DEBUG HUD / LOGGING

Server should expose/log:

```text
Connected players
Current systems occupied
Current planets occupied
Active interest regions
Entity counts
Region subscriptions
Persistence operations
Network throughput
```

Use development-only diagnostics.

---

# 56. NETWORK VISUALIZATION

If practical, visualize:

* interest radius
* canonical positions
* region boundaries
* relevant entities

This is invaluable for debugging.

---

# 57. LAG SIMULATION

Use Unreal/network tools or custom debug settings to test:

```text
50 ms latency
100 ms
200 ms
packet loss
jitter
```

The prototype should remain understandable.

Do not require competitive shooter quality.

---

# 58. CLIENT CORRECTION

When client prediction diverges:

server corrects.

Avoid instant huge visual snaps where smoothing is possible.

Astronomical movement corrections may require custom smoothing in canonical space.

---

# 59. CHEAT-RESISTANT AUTHORITY PRINCIPLE

Do not trust clients for gameplay-changing facts.

Clients may request:

```text
move
place
remove
interact
```

Server decides validity.

Do not build production anti-cheat yet.

---

# 60. MOVEMENT VALIDATION

At minimum validate impossible movement.

For example:

```text
client claims 500 LY jump in one tick
while not in warp mode
```

server rejects/corrects.

Keep rules simple.

---

# 61. BUILDING SPAM SAFETY

Add basic rate limiting for structure placement requests.

This is primarily to protect server state during testing.

No advanced abuse system required.

---

# 62. PROCEDURAL OBJECT REMOVAL SAFETY

Validate:

* entity exists logically
* player is close enough
* entity isn't already removed

Server applies idempotently.

---

# 63. PERSISTENCE WRITES ARE SERVER-ONLY

Clients must never directly mutate canonical persistence.

Audit for accidental local writes.

All multiplayer world changes go through server World State.

---

# 64. LOCAL SINGLE-PLAYER COMPATIBILITY

Preserve an easy single-player development mode.

Possible architecture:

```text
single-player
→ local listen/embedded authority

multiplayer
→ dedicated authoritative server
```

Do not make every developer task require manually deploying a remote server.

---

# 65. TWO-CLIENT DEVELOPMENT SCRIPT

Create convenient development tooling.

Example goal:

```text
StartDedicatedServer
StartClientA
StartClientB
```

or clear documented commands.

Reduce friction for repeated multiplayer testing.

---

# 66. AUTOMATED SERVER STARTUP TEST

Verify dedicated server:

* launches
* initializes persistence
* generates world
* accepts connection
* shuts down cleanly

---

# 67. AUTOMATED CONNECT TEST

Start server.

Connect client.

Verify:

* handshake
* world identity
* spawn
* canonical position

---

# 68. AUTOMATED TWO-PLAYER TEST

Where practical:

```text
server
+
client A
+
client B
```

Verify both players registered.

If full automated graphical clients are difficult, use functional/integration tests at lower layer.

---

# 69. DETERMINISTIC WORLD CONSISTENCY TEST

Given server and two clients:

For selected coordinates, compare:

```text
system descriptor
planet descriptor
terrain query
climate query
```

according to expected deterministic guarantees.

Mismatch should fail loudly.

---

# 70. BUILDING REPLICATION TEST

Test:

```text
A requests building create
↓
server accepts
↓
A receives state
↓
B receives state
```

Then reconnect B.

Building remains.

---

# 71. REMOVAL REPLICATION TEST

Test procedural tree removal across A/B.

Both clients converge.

---

# 72. REGION SUBSCRIPTION TEST

Player B leaves region.

Verify:

* irrelevant updates stop/reduce
* B can unload representation
* server subscription removed

B returns.

Current state restored.

---

# 73. DIFFERENT SYSTEM TEST

A remains System A.

B travels to System B.

Verify:

* both remain connected
* B can continue playing
* A receives little/no irrelevant B state
* world-state isolation correct

---

# 74. SAME SYSTEM RETURN TEST

B returns to A.

Relevant representations reactivate.

No duplicate player entity.

---

# 75. SERVER RESTART TEST

Mandatory:

```text
A + B modify Planet A
↓
stop server
↓
restart
↓
A reconnect
↓
B reconnect
```

Verify shared state.

---

# 76. DISCONNECT DURING WRITE

Simulate client disconnect during:

```text
structure placement
```

Server-side committed state must remain consistent.

No half-authoritative client state.

---

# 77. SERVER SHUTDOWN DURING ACTIVITY

Graceful shutdown should:

* stop accepting interactions
* flush persistence
* close DB
* terminate cleanly

Test it.

---

# 78. CLIENT DISCONNECT CLEANUP

Disconnected player's runtime representation must eventually be removed.

Persistent player data remains.

No ghost connections.

---

# 79. TIMEOUT

Add reasonable development connection timeout handling.

Do not overbuild.

---

# 80. BANDWIDTH OBSERVABILITY

Measure actual bandwidth in key scenarios:

```text
2 players beside each other on surface
2 players same planet far apart
2 players different systems
warp
building changes
```

Report measurements only.

---

# 81. PACKET SIZE

Inspect custom canonical-position/state serialization.

Avoid obviously wasteful payloads.

But correctness first.

---

# 82. ACTIVE ENTITY COUNTS

Track:

```text
server replicated players
ships
structures
dynamic world entities
```

Do not accidentally replicate static procedural content as Actors.

---

# 83. SCALE TEST — 4 CLIENTS

If local hardware/environment allows, test:

```text
4 clients
```

in one region.

This is not a requirement for MMO scale.

It helps detect hardcoded two-player assumptions.

---

# 84. SCALE TEST — HEADLESS BOT CLIENTS

If practical, create lightweight simulated connections/bots.

Use them for basic:

* connection
* movement
* region transition

Do not build sophisticated gameplay bots.

This will help Sprint 008 performance testing.

---

# 85. FUTURE REGIONAL AUTHORITY

Now document the next architectural step.

Eventually:

```text
Universe Router
↓
Region Authority
↓
Simulation Server
```

A single server currently owns the entire development universe.

Future infrastructure must be able to partition ownership by:

```text
galaxy / galactic region
system
planet
surface region
```

Do not implement distributed ownership yet.

---

# 86. REGION OWNERSHIP IDENTITY

Make sure logical regions already have stable identity suitable for future ownership.

Avoid server-specific identifiers becoming world identity.

---

# 87. SERVER HANDOFF ARCHITECTURE

Document future flow:

```text
Player approaching region boundary

Server A owns current region
Server B owns next region

↓
B prewarms state

↓
client establishes/receives route

↓
authority transfers

↓
A releases
```

Do not implement it yet.

Create:

```text
Docs/Architecture/ServerHandoff.md
```

---

# 88. ROUTER ABSTRACTION

If clean, create a minimal conceptual seam such as:

```text
IUniverseAuthorityResolver
```

or equivalent.

Current implementation:

```text
Everything → this dedicated server
```

Future implementation:

```text
position → responsible simulation server
```

Do not over-engineer.

---

# 89. NETWORKING DOCUMENTATION

Create/update:

```text
Docs/Architecture/MultiplayerAuthority.md
Docs/Architecture/InterestManagement.md
Docs/Architecture/NetworkCoordinates.md
Docs/Architecture/SharedWorldState.md
Docs/Architecture/ServerHandoff.md
```

Add ADRs for major choices.

---

# 90. MANUAL ACCEPTANCE A — MEET ON PLANET

Perform:

```text
Server starts

A connects
B connects

A lands Planet X
B lands Planet X

A exits ship
B exits ship

A walks toward B
B sees A

both move around
```

Validate transforms and gravity.

---

# 91. MANUAL ACCEPTANCE B — BUILD TOGETHER

```text
A places structure
↓
B sees it

B places structure
↓
A sees it
```

Both persist.

---

# 92. MANUAL ACCEPTANCE C — REMOVE PROCEDURAL OBJECT

```text
A removes tree
↓
tree disappears for A
↓
tree disappears for B
```

Reconnect B.

Tree remains absent.

---

# 93. MANUAL ACCEPTANCE D — DIFFERENT SYSTEMS

```text
A remains Planet A

B:
takeoff
warp
System B
land Planet B
```

Both remain connected and functional.

---

# 94. MANUAL ACCEPTANCE E — REUNITE

B returns to A.

A sees B's ship approaching if/when relevant.

They meet again.

---

# 95. MANUAL ACCEPTANCE F — RECONNECT

Disconnect A.

B stays online.

Reconnect A.

A returns correctly.

---

# 96. MANUAL ACCEPTANCE G — SERVER RESTART

Stop server completely.

Restart.

Reconnect both.

Shared modifications remain.

---

# 97. MANUAL ACCEPTANCE H — DIFFERENT LOCAL ORIGINS

Force/observe different client rebasing states.

Verify shared entity positions remain visually correct.

This is mandatory.

---

# 98. SPRINT 007 ACCEPTANCE CRITERIA

Sprint 007 is complete only if:

## Server

* [ ] dedicated server builds
* [ ] dedicated server runs headlessly
* [ ] server generates/loads universe state
* [ ] server owns World State
* [ ] server owns persistence writes
* [ ] server restart preserves world

## Connection

* [ ] multiple clients connect
* [ ] compatibility handshake works
* [ ] world seed/version identity verified
* [ ] player identity separate from connection identity
* [ ] reconnect works

## Position

* [ ] server authoritative canonical player position exists
* [ ] client local actor positions can differ safely
* [ ] position replication works across origin rebasing
* [ ] movement works on planets
* [ ] spacecraft movement works
* [ ] interstellar travel works

## Interest Management

* [ ] universe-aware relevance exists
* [ ] different systems do not receive full detailed updates
* [ ] local-region entities replicate appropriately
* [ ] region subscription/unsubscription works
* [ ] bandwidth reduces with irrelevance

## World State

* [ ] building placement is server-authoritative
* [ ] player-created entities replicate
* [ ] procedural removal is server-authoritative
* [ ] world deltas replicate
* [ ] persistent state remains shared
* [ ] join-in-progress receives correct region state

## Shared Simulation

* [ ] day/night state is coherent
* [ ] weather is coherent enough for same-region players
* [ ] procedural worlds agree between server/clients

## Persistence

* [ ] client cannot directly mutate canonical persistence
* [ ] player state persists
* [ ] world state persists
* [ ] server restart preserves changes
* [ ] multiple systems remain isolated correctly

## Testing

* [ ] dedicated server startup test passes
* [ ] connection tests pass
* [ ] deterministic consistency tests pass
* [ ] building replication test passes
* [ ] removal replication test passes
* [ ] region-interest tests pass
* [ ] different-system test passes
* [ ] reconnect test passes
* [ ] previous Sprint 001–006 tests still pass
* [ ] full project/server targets build

## Manual

* [ ] two players can meet on same planet
* [ ] see each other
* [ ] walk together
* [ ] see same structures
* [ ] modify shared world
* [ ] separate to different systems
* [ ] reunite
* [ ] reconnect
* [ ] server restart preserves history
* [ ] different local origins do not break shared positions

---

# 99. THE DEFINING DEMONSTRATION

Before declaring Sprint 007 complete, perform this full sequence:

```text
START DEDICATED SERVER

↓

PLAYER A CONNECTS

PLAYER B CONNECTS

↓

both exist in same procedural universe

↓

A is on Planet A

↓

B warps into System A

↓

B approaches Planet A

↓

A looks into sky

↓

B's ship becomes relevant / visible

↓

B enters atmosphere

↓

B lands near A

↓

B exits ship

↓

A and B stand on same procedural world

↓

A places structure

↓

B immediately sees structure

↓

B removes procedural tree

↓

A immediately sees tree disappear

↓

A enters ship

↓

A warps to another system

↓

B remains on Planet A

↓

both continue playing independently

↓

A returns

↓

they meet again

↓

both disconnect

↓

SERVER STOPS

↓

SERVER RESTARTS

↓

A + B reconnect

↓

same planet
same procedural terrain
same environment
same player-created structure
same removed tree
```

If this works, we have proven:

# THIS IS NO LONGER A SINGLE-PLAYER PROCEDURAL ENGINE.

It is the beginning of a persistent shared universe.

---

# 100. DO NOT BEGIN SPRINT 008

Sprint 008 will be:

# MVP HARDENING & PLAYABLE VERTICAL SLICE

It will take everything from Sprint 001–007 and turn it into one stable, convincing, distributable prototype.

Expected scope:

```text
performance profiling
↓
streaming hardening
↓
network hardening
↓
crash/error recovery
↓
visual transitions
↓
UX / controls
↓
graphics settings
↓
save/version robustness
↓
packaged dedicated server
↓
packaged Windows client
↓
automated end-to-end test
↓
first external playtest build
```

Sprint 008 should NOT add civilization/economy/combat systems.

It is about making the existing foundation reliable and demonstrable.

---

# 101. COMPLETION REPORT

At completion provide:

## Implemented

Exactly what genuinely works.

## Authority Architecture

Explain:

* server authority
* client responsibilities
* World State ownership
* persistence ownership
* procedural generation responsibilities

## Networking

Explain:

* canonical position replication
* origin-rebase handling
* interest management
* region subscriptions
* movement
* warp
* entity replication

## Shared World

Explain:

* building creation
* procedural removals
* join-in-progress
* reconnect
* server restart

## Future Scaling

Explain clearly what remains required for:

```text
hundreds of players
thousands of players
distributed regional servers
seamless handoff
```

Do not claim Sprint 007 is MMO scale.

## Validation

List exact:

* server builds
* client builds
* automated tests
* multiplayer tests
* latency tests
* restart tests
* manual journeys

actually executed.

## Performance

Report measured:

* server frame/tick time
* client frame time
* bandwidth
* entity counts
* packet rates
* correction counts
* persistence timings
* memory

Measured values only.

## Known Limitations

Be explicit.

## Technical Debt

List shortcuts.

## Recommended Sprint 008

Define the smallest final hardening sprint needed to produce a robust external MVP build.

Do not implement it yet.

---

# FINAL PRINCIPLE

The network should transmit history and change — not the whole universe.

The procedural universe already exists mathematically on every compatible machine.

The server exists to answer:

```text
Where are the players?

What has changed?

Who is authoritative?

What is currently happening?

```

The server should NOT spend bandwidth telling every client where every untouched mountain, tree, star and planet is.

Those are regenerated from shared deterministic rules.

If two players stand on the same procedural planet, they should experience one shared world even if:

```text
their local Unreal origins differ
their rendering detail differs
their PCs load different LODs
```

Canonical universe state remains shared.

Begin Sprint 007.
