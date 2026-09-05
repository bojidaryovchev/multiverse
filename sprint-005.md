Read `CLAUDE.md` completely before doing anything else.

Then inspect the entire repository and verify the outputs of Sprint 001, Sprint 002, Sprint 003 and Sprint 004.

We are beginning:

# Sprint 005 — World Interaction & Persistence

The objective of this sprint is to prove that our enormous deterministic procedural universe can be **permanently modified by players without storing the generated universe itself**.

At the end of this sprint, the player must be able to:

```text
discover procedural planet
↓
land
↓
walk around
↓
place a structure
↓
remove / modify selected procedural objects
↓
leave planet
↓
travel elsewhere
↓
quit game completely
↓
restart
↓
return to exact same planet
↓
find exact same procedural terrain/environment
↓
find player-created structure still present
↓
find previously removed procedural objects still removed
```

This sprint proves one of the most important architectural principles in the entire project:

# Procedural Base World + Persistent Deltas = Current Universe

We must NOT save entire procedural planets.

We save only what changed.

---

# 0. PRECONDITION — VERIFY PREVIOUS SPRINTS

Before implementing Sprint 005, verify:

## Sprint 001

* universe coordinate system works
* deterministic seed hierarchy works
* star system generation works
* large logical movement works
* tests pass

## Sprint 002

* spherical terrain works
* stable planet patch IDs exist
* terrain is deterministic
* quadtree/LOD works
* streaming works
* tests pass

## Sprint 003

* continuous space → surface traversal works
* planetary coordinate frames work
* gravity works
* player can land / exit / walk / return
* origin rebasing works
* tests pass

## Sprint 004

* climate generation works
* biome generation works
* ocean/environment systems work
* vegetation streams
* weather works
* basic wildlife works
* environmental queries work
* environment remains deterministic where intended
* tests pass

Fix only issues that genuinely block persistence.

Do not rewrite working systems unnecessarily.

---

# 1. PRIMARY SPRINT OBJECTIVE

Implement persistent player modification.

The underlying world remains reconstructible from:

```text
Universe Seed
+
Generation Versions
+
Coordinates
```

Persistent storage contains only:

```text
player-created entities
player-destroyed entities
player-modified state
future terrain modifications
other world deltas
```

Conceptually:

```text
PROCEDURAL BASE STATE
        +
PERSISTENT DELTAS
        =
CURRENT WORLD STATE
```

This model must become a first-class architectural principle.

---

# 2. OUT OF SCOPE

Do NOT build:

* MMO backend
* cloud database
* AWS infrastructure
* distributed persistence
* account system
* commerce
* crafting economy
* complex building system
* factories
* cities
* NPC settlements
* civilizations
* factions
* resource economy
* multiplayer replication
* terrain deformation unless exceptionally easy
* complex destruction
* advanced inventories
* base power systems
* survival mechanics
* combat

The goal is persistence architecture, not a survival game.

---

# 3. PERSISTENCE PRINCIPLE

Never save something simply because it exists procedurally.

Example:

Do NOT store:

```text
tree #1
tree #2
tree #3
...
tree #3,918,274,821
```

Instead:

The procedural generator already knows that those trees exist.

If the player destroys:

```text
Tree ID 817291
```

store:

```text
Tree 817291 = Removed
```

Likewise, do not save mountains, oceans, climate fields or untouched terrain.

They are regenerated.

Save only deviation from deterministic base state.

---

# 4. WORLD IDENTITY MODEL

Before persistence can work, every persistent location must have stable identity.

Review existing identity systems.

At minimum the hierarchy should support stable identification of:

```text
Universe
Galaxy / Universe Region
Star System
Astronomical Body
Planet
Planet Surface Region / Patch
Entity
```

Do not depend on:

* Unreal Actor names
* object pointers
* spawn order
* transient GUIDs generated every run
* memory addresses

Create or consolidate stable identifier structures.

Possible conceptual hierarchy:

```text
FUniverseId
FSystemId
FPlanetId
FPlanetPatchId
FPersistentEntityId
```

Do not add redundant types if current architecture already solves part of this.

---

# 5. PERSISTENT ENTITY ID

Create a robust persistent entity identity system.

There are two main categories:

## Procedural entities

Identity should derive deterministically from:

```text
Planet Seed
Patch ID
Content Layer
Procedural Instance Index / Stable Placement Key
Generation Version
```

Example:

```text
Planet 91827
Patch +X/LOD14/281/991
VegetationLayer 3
Instance 182

→ stable persistent procedural entity ID
```

Repeated generation must yield the same identity.

## Player-created entities

Generate a stable unique ID when created.

For local Sprint 005, an appropriate random/UUID-style identifier is acceptable.

Future architecture must support server-authoritative creation.

Clearly distinguish these two categories.

---

# 6. DO NOT USE ARRAY ORDER CARELESSLY

Procedural entity persistence is dangerous if identity depends solely on unstable array position.

If modifying terrain generation or placement rules changes ordering:

```text
Tree 817
```

may suddenly refer to a different tree.

Design stable identity carefully.

Prefer identity based upon:

* deterministic placement cell
* deterministic candidate key
* stable hash
* generation version
* content layer
* spatial quantization

Document the strategy.

---

# 7. WORLD REGION ADDRESS

Persistent deltas should be spatially partitioned.

Do NOT create:

```text
one giant save file
containing every modification in universe
```

Create a logical persistence region.

For a planet, a region might correspond conceptually to:

```text
Planet
+
Persistent Region Coordinates
```

This does NOT necessarily need to equal terrain rendering LOD patches.

In fact, persistence partitioning should ideally remain stable even if visual LOD implementation changes.

Analyze this carefully.

Create:

```text
FPersistenceRegionId
```

or equivalent if required.

Requirements:

* stable
* deterministic
* serializable
* spatially queryable
* independent of temporary LOD
* supports future server ownership
* supports efficient load/unload

---

# 8. PERSISTENCE REGION SCALE

Choose a persistence region scale deliberately.

Consider:

* number of entities per region
* DB row/file count
* loading granularity
* building size
* future multiplayer ownership
* terrain patch changes
* spatial queries

Do not arbitrarily reuse current terrain patch size.

Document the reasoning.

---

# 9. WORLD DELTA TYPES

Define an extensible delta/state model.

Initial required concepts:

```text
CreatedEntity
RemovedProceduralEntity
UpdatedEntityState
```

Potential future types:

```text
TerrainModification
ResourceDepletion
VegetationState
ContainerInventory
DoorState
MachineState
DamageState
OwnershipChange
```

Do NOT implement all future deltas now.

But avoid an architecture requiring schema redesign for every new interaction.

---

# 10. PERSISTENT ENTITY STATE

Player-created entities need serializable state.

A minimal structure might conceptually contain:

```text
Entity ID
Entity Type
Planet ID
Persistence Region
Planet-local transform
Owner/Creator placeholder
Entity Version
Custom State
```

Do not blindly copy this structure.

Design a clean schema.

The transform must use canonical planet-local coordinates.

Do NOT save only an Unreal world transform.

Remember:

```text
Unreal coordinate
≠
canonical universe location
```

---

# 11. PLANET-LOCAL TRANSFORMS

A placed structure must remain correctly positioned even after:

* origin rebasing
* application restart
* landing from another direction
* changing local simulation frame
* moving around planet
* future server migration

Save its canonical planet-relative representation.

For example:

```text
Surface direction
Altitude / radial position
Orientation relative to local tangent frame
```

or an equivalent robust representation.

Analyze placement orientation carefully.

---

# 12. LOCAL TANGENT FRAME

Buildings placed on a spherical world need meaningful orientation.

Conceptually:

```text
Local Up = radial surface direction

Local Forward = tangent direction

Local Right = cross(...)
```

Persist orientation relative to this local frame where appropriate.

Do not make a house's rotation depend permanently on one Unreal world's XYZ orientation.

---

# 13. PERSISTENCE ABSTRACTION

Create a storage-independent persistence interface.

Core gameplay must not know it uses SQLite.

Conceptually:

```cpp
class IWorldPersistenceStore
{
public:
    LoadRegion(...);
    SaveCreatedEntity(...);
    SaveRemovedEntity(...);
    SaveEntityState(...);
    DeleteEntityState(...);
};
```

Do not blindly implement this interface.

Design appropriate asynchronous APIs and data types.

Requirements:

* storage backend independent
* future server backend replaceable
* testable
* asynchronous where disk I/O could block
* supports batch operations
* supports region queries
* supports transactions where appropriate

---

# 14. SQLITE LOCAL BACKEND

Implement the first persistence backend using:

# SQLite

unless repository constraints reveal a clearly better lightweight local solution.

SQLite should store world modification state.

Do NOT store:

* terrain meshes
* textures
* complete planet geometry
* generated forests
* weather history unless necessary
* untouched regions

Use transactions.

Use prepared statements.

Create schema/version management.

---

# 15. DATABASE LOCATION

Store database in an appropriate application save location.

Do not commit runtime databases to Git.

Provide a developer command for:

```text
ResetWorldPersistence
```

that deletes/resets local world modification state safely.

Do not make resetting procedural seed mandatory.

---

# 16. DATABASE SCHEMA

Design the smallest clean schema.

Potential conceptual tables:

```text
world_metadata

created_entities

removed_procedural_entities

entity_state
```

A normalized or unified entity/delta table may be better.

Analyze first.

Important fields may include:

```text
UniverseId
SystemId
PlanetId
RegionId
EntityId
EntityType
Transform
StatePayload
EntityVersion
CreatedAt / ModifiedAt
```

Timestamps should not define deterministic identity.

Document schema.

---

# 17. SCHEMA VERSIONING

Introduce:

```text
PersistenceSchemaVersion
```

Database migrations must be possible later.

For Sprint 005:

* initial schema version
* schema initialization
* version validation
* migration framework skeleton if appropriate

Do not build elaborate migration infrastructure.

But do not hardcode an unversioned database.

---

# 18. GENERATION VERSION COMPATIBILITY

This is extremely important.

Existing systems should already have concepts such as:

```text
TerrainGeneratorVersion
EnvironmentGeneratorVersion
```

Persistent data may depend upon generated world geometry.

Store relevant generation-version metadata.

Example problem:

```text
Version 1:
house sits on hill

Terrain algorithm changes

Version 2:
hill becomes valley

house floats 300 meters
```

We need to recognize this problem.

For Sprint 005:

* record relevant generation versions
* detect incompatible world-generation version
* warn loudly
* do not silently corrupt persisted worlds

Full migration comes later.

---

# 19. SAVE WORLD METADATA

Persist metadata such as:

```text
Universe Seed
Universe Generation Version
Terrain Generation Version
Environment Generation Version
Persistence Schema Version
Creation Time
Last Save Time
```

Use appropriate values.

On load, validate compatibility.

---

# 20. BUILDING SYSTEM — MINIMUM POSSIBLE

Implement a deliberately tiny construction system.

Required:

```text
ONE placeable structure type
```

or a small set such as:

```text
Foundation
Wall
Beacon
```

Do NOT make a full building game.

The objective is testing persistent creation.

A simple test structure is sufficient.

---

# 21. BUILD MODE

Implement basic controls:

```text
Enter build mode
↓
show placement preview
↓
aim at terrain
↓
validate placement
↓
rotate
↓
place
```

Use placeholder geometry.

Requirements:

* visual preview
* valid/invalid state
* terrain placement
* planet-local orientation
* structure collision after placement

No fancy UI required.

---

# 22. BUILDING PLACEMENT QUERY

Placement must use the Planet Surface Query API.

Do not perform arbitrary assumptions about global Z.

Determine:

```text
surface point
surface normal
local up
slope
```

Validate slope.

Place structure aligned appropriately.

---

# 23. BUILDING TRANSFORM PERSISTENCE

Immediately after placement:

```text
generate stable Entity ID
↓
convert transform to canonical planet-local representation
↓
write persistence delta
↓
spawn/render entity
```

Do not rely upon saving later on application shutdown only.

Critical actions should persist promptly.

---

# 24. BUILDING LOADING

When a persistence region becomes relevant:

```text
generate procedural world
↓
load persistent region
↓
apply removed procedural entities
↓
spawn player-created entities
↓
apply entity state updates
```

Do not load all structures in the entire universe at startup.

Load spatially.

---

# 25. BUILDING STREAMING

Player-created structures must participate in spatial streaming.

Example:

```text
player 5000 km away
→ building does not need Actor representation

player approaches region
→ building representation spawns

player leaves
→ representation unloads

persistent database row remains
```

Separate:

```text
persistent existence
```

from:

```text
currently instantiated Unreal Actor
```

This distinction is fundamental.

---

# 26. REMOVE PLAYER-CREATED STRUCTURE

Allow a placed test structure to be removed.

Flow:

```text
player chooses structure
↓
remove
↓
delete/tombstone persisted entity state
↓
remove runtime representation
```

Ensure it remains absent after reload.

This tests both creation and deletion.

---

# 27. PROCEDURAL OBJECT REMOVAL

Implement interaction with at least one procedural entity type.

Ideal candidate:

```text
tree
```

Allow player to:

```text
look at tree
↓
interact/remove
↓
tree disappears
↓
removal delta saved
```

No chopping animation/resource reward necessary.

---

# 28. STABLE TREE / VEGETATION IDENTITY

This is an important architectural test.

The vegetation system from Sprint 004 must expose deterministic identity for removable procedural objects.

If current PCG placement cannot reliably reproduce stable individual identities, solve this deliberately.

Potential strategy:

```text
Planet
+
Persistence Region
+
Content Layer
+
quantized deterministic placement key
```

Do not use temporary PCG instance indices without confirming stability.

Document the solution.

---

# 29. PROCEDURAL REMOVAL APPLICATION

When the region reloads:

```text
procedural vegetation generation
↓
persistent removal set loaded
↓
remove/suppress matching entities
↓
final representation
```

Prefer suppressing removed objects before expensive runtime representation is created where practical.

Do not spawn and immediately destroy thousands of known-removed entities unnecessarily.

---

# 30. TOMBSTONES

Removed procedural content essentially creates a tombstone:

```text
Entity X
was part of procedural base world
but is removed in current world
```

Represent this efficiently.

Consider compact storage and indexing.

Do not duplicate full procedural entity data just to record removal.

---

# 31. WORLD STATE APPLICATION ORDER

Define explicit region load ordering.

Example:

```text
1. determine procedural region identity

2. request persistent deltas

3. generate base state

4. suppress removed base entities

5. instantiate base representation

6. instantiate created entities

7. apply state overrides
```

Actual order may be optimized.

Document it.

Avoid race conditions where:

```text
tree briefly appears
then disappears
```

every time a saved region loads.

---

# 32. ASYNCHRONOUS REGION LOAD

Database I/O must not visibly freeze gameplay.

Implement asynchronous loading where necessary.

When approaching a region:

```text
predict region
↓
request deltas
↓
generate procedural environment
↓
combine states
↓
activate
```

Coordinate terrain/environment streaming with persistence streaming.

---

# 33. PERSISTENCE PREWARMING

The Sprint 003/004 streamer already predicts relevant areas.

Integrate persistent region loading.

Priority should roughly be:

```text
collision
persistent structural state
critical procedural removals
terrain/environment
cosmetic detail
```

A house must not suddenly appear after the player has already flown through where it should be.

Prewarm appropriately.

---

# 34. SAVE QUEUE

Implement a bounded persistence write queue if asynchronous writes are used.

Requirements:

* ordered where required
* coalesce repeated state updates where useful
* no unbounded growth
* errors surfaced
* safe shutdown flush
* critical writes not silently dropped

Do not create an overly complex event sourcing system.

---

# 35. TRANSACTIONS

Actions requiring multiple writes should be atomic where practical.

Example:

```text
create building
+
region index
+
entity state
```

should not leave half-created state if interrupted.

Use SQLite transactions appropriately.

---

# 36. CRASH SAFETY

Test basic crash/restart behavior.

At minimum:

* placed structures should not rely solely on graceful game exit
* previously committed changes should survive abnormal termination
* database should not become corrupted by ordinary interrupted sessions

Full disaster recovery is not required.

---

# 37. SAVE ON ACTION, NOT ONLY ON EXIT

Avoid the classic approach:

```text
player plays 5 hours
↓
save everything at exit
```

Persistence should occur incrementally.

For this sprint:

```text
place building
→ persist

remove tree
→ persist

remove building
→ persist
```

Application exit may flush pending work.

---

# 38. IDEMPOTENCY

Persistent operations should be safe against accidental retries.

For example:

```text
Remove Procedural Entity X
```

executed twice should not corrupt state.

Likewise entity creation IDs should prevent duplicate representation after reload/retry.

---

# 39. REGION CACHE

Add an appropriate in-memory cache for currently active persistence regions.

Avoid querying SQLite repeatedly every frame.

Conceptually:

```text
Persistence Region
↓
Loaded Delta State
↓
cached while relevant
↓
released when no longer needed
```

Bound the cache.

---

# 40. DIRTY STATE

If mutable entities gain state:

```text
door open/closed
health
etc.
```

support dirty-state tracking.

For Sprint 005, minimal entity state is enough.

Do not save unchanged entities every frame.

---

# 41. BUILDING OWNERSHIP PLACEHOLDER

Add minimal ownership metadata if useful:

```text
Creator/Owner ID
```

Single-player can use a stable local player ID.

Do not build authentication.

The field exists because future multiplayer needs ownership.

---

# 42. ENTITY TYPE REGISTRY

Player-created entities should use stable type identifiers.

Do not persist raw C++ class names as the sole long-term identity.

Create stable type IDs/configuration.

Example:

```text
universe.structure.foundation.v1
```

or equivalent enum/registry approach.

Architecture should survive class renaming.

---

# 43. ENTITY VERSIONING

Persist:

```text
EntityDataVersion
```

for player-created entity state where appropriate.

Future changes to building schemas must be migratable.

Keep implementation minimal.

---

# 44. BINARY VS STRUCTURED PAYLOAD

Evaluate persisted state representation.

Potential options:

* normalized columns
* JSON payload
* binary serialized data
* hybrid

Choose deliberately.

Requirements:

* versionable
* debuggable
* performant enough
* future-server-compatible

For simple Sprint 005 state, avoid premature optimization.

---

# 45. NO UObject SERIALIZATION AS CANONICAL FORMAT

Do NOT blindly serialize arbitrary UObject memory into the database as the canonical long-term format.

Persistence schema should remain deliberate and versionable.

Unreal runtime representation is not the database schema.

---

# 46. PROCEDURAL OBJECT STATE QUERY

Provide a clean API:

```text
IsProceduralEntityRemoved(EntityId)
```

or equivalent.

Environment representation should query the persistence state through an abstraction.

Do not let tree Actors perform SQLite queries directly.

---

# 47. WORLD STATE QUERY API

Create a semantic system through which gameplay can access current world state.

Conceptually:

```cpp
WorldState->CreateEntity(...)
WorldState->RemoveEntity(...)
WorldState->UpdateEntity(...)
WorldState->GetRegionState(...)
```

Storage remains underneath.

This World State layer becomes important later for multiplayer authority.

---

# 48. FUTURE AUTHORITATIVE SERVER SEAM

Design:

```text
Gameplay
↓
World State Service
↓
Persistence Backend
```

so Sprint 007 can replace:

```text
local client authority
```

with:

```text
server authority
```

without rewriting every gameplay interaction.

Do not prematurely implement networking.

Just preserve the seam.

---

# 49. WORLD MODIFICATION EVENTS

Use clean events/notifications when current-world state changes.

For example:

```text
EntityCreated
EntityRemoved
EntityUpdated
RegionLoaded
RegionUnloaded
```

These events can update runtime representations.

Avoid globally coupled event spaghetti.

---

# 50. PLAYER SAVE STATE

Persist minimal player state.

At minimum consider:

```text
canonical universe position
current planet/system
character/ship mode
orientation
ship position
```

The exact amount depends on current architecture.

Goal:

Quit while standing on planet.

Restart.

Spawn in a sensible equivalent location.

---

# 51. SAFE PLAYER SPAWN AFTER LOAD

On reload:

* ensure terrain is ready
* ensure collision is ready
* validate saved surface position
* avoid spawning under terrain
* avoid spawning inside player-created building
* handle incompatible generation version

Implement basic recovery fallback.

---

# 52. SPACECRAFT SAVE STATE

If player exits while a ship is landed:

persist enough state that:

```text
player returns
→ ship remains appropriately located
```

Do not yet build permanent fleets.

One current player ship is enough.

---

# 53. UNIVERSE RETURN TEST

This is the defining acceptance test.

Perform:

```text
Planet A
↓
land
↓
place Beacon X
↓
remove Tree Y
↓
take off
↓
travel sufficiently far away that region unloads
↓
return
```

Verify:

```text
Beacon X exists
Tree Y absent
```

Then:

```text
quit process completely
↓
restart
↓
return again
```

Verify the same state.

---

# 54. DIFFERENT PLANET ISOLATION

Create a structure on:

```text
Planet A
```

Then visit:

```text
Planet B
```

Ensure persistence keys do not collide.

A region with identical face/coordinates on two different planets must remain separate.

Test this explicitly.

---

# 55. DIFFERENT UNIVERSE SEED ISOLATION

If developers can change universe seed, persistence must not accidentally apply modifications to a different universe.

Include universe identity/seed in world metadata and key strategy as appropriate.

Warn or create separate save worlds.

---

# 56. MULTIPLE SAVE WORLDS

Introduce a lightweight concept of:

```text
World Save ID
```

or equivalent if useful.

We may later support multiple universes/save games.

Do not build save-selection UI.

Developer configuration is enough.

---

# 57. SAVE RESET TOOL

Provide developer commands such as:

```text
Persistence.ResetAll
Persistence.ResetPlanet
Persistence.ResetRegion
Persistence.InspectRegion
```

or equivalent.

Make testing convenient.

Require confirmation or development-only safeguards for destructive commands where appropriate.

---

# 58. PERSISTENCE DEBUG HUD

Expose:

```text
Current World Save
Persistence Schema Version
Current Planet
Current Persistence Region
Loaded Persistence Regions
Created Entities
Removed Procedural Entities
Dirty Entities
Pending Writes
Pending Reads
DB Write Latency
DB Read Latency
```

Toggleable.

---

# 59. PERSISTENCE INSPECTOR

If practical, provide an Unreal developer/debug view capable of inspecting current region deltas.

Example:

```text
Region X

Created:
- Beacon 8291
- Foundation 9182

Removed:
- Tree 12817

Updated:
- ...
```

No polished UI needed.

---

# 60. VISUAL DEBUG FOR PERSISTENT ENTITIES

Provide optional debug labels/colors indicating:

```text
procedural untouched
procedural removed
player-created
persistent loaded
temporary runtime-only
```

Useful during testing.

---

# 61. PERSISTENCE REGION VISUALIZATION

Show persistence-region boundaries.

This helps validate:

* streaming
* region transitions
* structure assignment
* entity lookup

Do not confuse them with terrain LOD boundaries.

---

# 62. REGION BOUNDARY TEST

Place structures directly near persistence-region edges.

Test:

```text
building on side A
building on side B
```

Move back and forth across boundary.

Ensure:

* no duplicates
* no disappearing incorrectly
* no region ownership ambiguity

---

# 63. LARGE STRUCTURE FUTURE SUPPORT

A future structure may cross multiple persistence regions.

Do not fully implement this now.

But document how the system will eventually handle:

```text
entity origin/primary region
+
spatial overlap index
```

or another approach.

Our tiny Sprint 005 structures can belong to one primary region.

---

# 64. PROCEDURAL REMOVAL REGION BOUNDARY TEST

Remove vegetation near region boundaries.

Unload/reload repeatedly.

Verify removed objects remain removed.

---

# 65. DETERMINISTIC BASE + DELTA TEST

Create automated test:

```text
generate deterministic region A

record hash/state

apply:
create entity X
remove procedural Y

compute current-world state

restart persistence layer

regenerate region A

reload deltas

compute current-world state again
```

Results must match.

---

# 66. DATABASE ROUND-TRIP TESTS

Test:

```text
create
read
update
delete
```

for persisted entity records.

Test serialization round-trips for:

* IDs
* planet-local position
* orientation
* entity type
* state
* region ID

---

# 67. TRANSACTION TESTS

Test transaction rollback where practical.

Ensure partial writes do not become visible after intentionally failed transaction.

---

# 68. DUPLICATE OPERATION TEST

Test:

```text
remove same tree twice
place same persistent entity ID twice
delete same entity twice
```

System should remain consistent.

---

# 69. PLAYER POSITION ROUND-TRIP TEST

Save canonical position.

Destroy/restart runtime representation.

Reload position.

Convert through simulation frame.

Verify expected error tolerance.

Test multiple points:

* equator
* poles
* cube-face edges
* opposite side of planet
* orbit
* surface

---

# 70. BUILDING ORIENTATION ROUND-TRIP

Place building at several points around spherical world.

Save.

Reload.

Verify:

* surface position
* local up orientation
* yaw/tangent orientation

No structure should rotate strangely because it was placed on another part of sphere.

---

# 71. LOAD ORDER STRESS TEST

Repeatedly approach/leave a modified region quickly.

Verify:

* only one runtime instance of each persistent building exists
* removed objects do not flicker back incorrectly
* async callbacks from stale loads are discarded
* no task buildup

---

# 72. RAPID PLANET TRAVEL TEST

Perform:

```text
Planet A modified region
↓
space
↓
Planet B
↓
space
↓
Planet A
```

repeatedly.

Monitor:

* DB tasks
* region cache
* runtime entity count
* memory
* duplicate entities

---

# 73. MANY STRUCTURES TEST

Use developer tooling to generate many test structures.

For example:

```text
100
1,000
10,000
```

where practical.

Do not require rendering all simultaneously.

Measure:

* insert time
* region query time
* relevant-region spawn time
* DB size
* runtime count

This is not an MMO benchmark.

It validates basic scaling characteristics.

---

# 74. MANY REMOVALS TEST

Generate a modified region with many removed procedural objects.

Measure suppression/query performance.

Avoid O(all modifications in universe) behavior when loading one region.

---

# 75. DATABASE INDEXING

Create appropriate indexes.

Likely key access paths include:

```text
World
Planet
Persistence Region
Entity ID
```

Inspect query plans if necessary.

Do not add indexes blindly.

---

# 76. STORAGE SIZE OBSERVABILITY

Track database growth.

Report approximately:

```text
empty world DB
after 100 structures
after 1,000 structures
after 1,000 procedural removals
```

Measured results only.

The objective is demonstrating that untouched universe size does not affect storage size.

---

# 77. CORE SCALABILITY INVARIANT

This must remain true:

```text
10 planets generated but untouched

vs

10 billion planets mathematically possible but untouched
```

should require almost no meaningful additional persistence storage.

Storage scales primarily with:

```text
player activity
```

not:

```text
procedural universe size
```

Add documentation and tests supporting this architecture.

---

# 78. THREADING

Database reads/writes should not violate Unreal thread rules.

Pure persistence operations may happen off the game thread.

Runtime Actors/UObjects must be updated safely.

Clearly document:

```text
Persistence thread/task
↓
World State data
↓
Game-thread representation
```

---

# 79. SHUTDOWN

Handle graceful application shutdown.

Requirements:

* stop new persistence jobs
* flush required pending writes
* close SQLite cleanly
* avoid callback execution into destroyed game systems

Test repeated start/stop.

---

# 80. DATABASE FAILURE HANDLING

Do not silently ignore DB errors.

Log clearly.

Critical errors should surface in development HUD/logging.

Examples:

* cannot open DB
* schema mismatch
* write failure
* malformed record
* incompatible generation version

Do not crash on every recoverable error, but do not hide them.

---

# 81. CORRUPT RECORD HANDLING

If a single malformed entity is encountered:

* report it
* avoid taking down the entire planet where possible
* skip/quarantine corrupted state appropriately

No elaborate repair tool required.

---

# 82. WORLD SAVE COMPATIBILITY CHECK

At startup:

```text
load world metadata
↓
compare generation versions
↓
compare persistence schema
```

If incompatible:

Do NOT silently proceed.

Provide a clear development error/warning and an explicit reset/migration path.

---

# 83. GENERATION VERSION TEST

Intentionally simulate a generator version mismatch.

Verify detection works.

---

# 84. BUILDING COLLISION

Player-created structure needs basic collision.

Ensure collision streams with runtime representation.

Do not build complex destructible geometry.

---

# 85. BUILDING VISUALS

Use simple placeholder assets.

For example:

```text
foundation block
wall panel
beacon
```

Architecture matters more than appearance.

---

# 86. BUILDING PREVIEW

Placement ghost must not itself become persistent.

Distinguish:

```text
Preview Entity
```

from:

```text
Committed Persistent Entity
```

Test cancellation.

---

# 87. PLACEMENT VALIDATION

Initial rules:

* must hit terrain
* slope below threshold
* not obviously overlapping another structure
* not underwater unless allowed
* enough collision readiness

Keep rules simple.

---

# 88. UNDO IS NOT REQUIRED

Do not implement editor-style undo.

Removal serves as player deletion.

---

# 89. RESOURCE COST IS NOT REQUIRED

Building does not require materials yet.

This sprint tests persistence.

Player may place structures freely in developer/prototype mode.

---

# 90. WORLD STATE DOCUMENTATION

Create/update:

```text
Docs/Architecture/WorldPersistence.md
Docs/Architecture/WorldState.md
Docs/Architecture/PersistentEntityIdentity.md
Docs/Architecture/PersistenceRegions.md
```

Add ADRs for significant decisions.

At minimum likely ADRs:

```text
ADR — Delta Persistence Model
ADR — Persistence Region Partitioning
ADR — Persistent Entity Identity
ADR — Local Persistence Backend
```

Document:

* what is procedural
* what is persisted
* entity identity
* region identity
* load order
* write order
* SQLite schema
* generation versions
* server-authority seam
* async lifecycle

---

# 91. MANUAL ACCEPTANCE TEST A — BUILD

Perform:

```text
launch game
↓
fly to Planet A
↓
land
↓
exit ship
↓
place structure
↓
walk away
↓
return
```

Structure must remain.

---

# 92. MANUAL ACCEPTANCE TEST B — STREAMING

```text
place structure
↓
travel far enough that region unloads
↓
verify runtime representation unloads
↓
return
↓
verify structure reloads exactly once
```

---

# 93. MANUAL ACCEPTANCE TEST C — PROCEDURAL REMOVAL

```text
locate deterministic tree
↓
remove it
↓
leave region
↓
return
```

Tree must remain absent.

---

# 94. MANUAL ACCEPTANCE TEST D — PROCESS RESTART

This is mandatory.

```text
place structure
remove procedural tree
save
quit game completely
restart process
return to planet
```

Verify:

```text
structure exists
tree absent
```

---

# 95. MANUAL ACCEPTANCE TEST E — LEAVE PLANET

```text
Planet A
↓
modify world
↓
take off
↓
travel elsewhere
↓
return to Planet A
↓
land in original region
```

Changes must remain.

---

# 96. MANUAL ACCEPTANCE TEST F — SPHERICAL POSITIONING

Place structures at:

```text
equatorial region
polar region
opposite side of planet
cube-face boundary
```

Save/restart.

Verify correct orientation/location.

---

# 97. MANUAL ACCEPTANCE TEST G — MULTIPLE PLANETS

Modify:

```text
Planet A
Planet B
```

Restart.

Verify modifications remain isolated and correct on both planets.

---

# 98. PERFORMANCE REPORTING

Measure and report where practical:

```text
SQLite open/init time
region load latency
region save latency
building write latency
removed-object write latency
region entity query time
memory cache size
DB file size
```

Also test sample modification counts.

Do not fabricate values.

---

# 99. SPRINT 005 ACCEPTANCE CRITERIA

Sprint 005 is complete only if:

## Identity

* [ ] Stable persistence-region identity exists.
* [ ] Stable player-created entity IDs exist.
* [ ] Stable removable procedural entity identity exists.
* [ ] Identity does not depend on transient Unreal runtime ordering.
* [ ] Multiple planets cannot collide accidentally.

## Architecture

* [ ] World State layer exists.
* [ ] Storage abstraction exists.
* [ ] SQLite backend exists.
* [ ] Persistence schema is versioned.
* [ ] generation-version compatibility is tracked.
* [ ] canonical planet-local transforms are persisted.
* [ ] runtime Actors are not treated as canonical world state.

## Building

* [ ] Player can enter simple build mode.
* [ ] Placement preview works.
* [ ] At least one structure can be placed.
* [ ] Placement respects spherical terrain orientation.
* [ ] Structure receives stable persistent ID.
* [ ] Structure collision works.
* [ ] Structure can be removed.

## Streaming

* [ ] Structures load spatially.
* [ ] Structures unload spatially.
* [ ] Whole-universe state is not loaded at startup.
* [ ] Region caching is bounded.
* [ ] rapid region traversal does not cause duplicates.
* [ ] stale async loads cannot resurrect old representation.

## Procedural Deltas

* [ ] At least one procedural content type can be removed.
* [ ] Removal is persisted as a delta/tombstone.
* [ ] Removed procedural object remains absent after region reload.
* [ ] Removed procedural object remains absent after process restart.
* [ ] unchanged procedural content is NOT saved individually.

## Persistence

* [ ] Changes save incrementally.
* [ ] transaction use is appropriate.
* [ ] duplicate operations are safe.
* [ ] pending writes flush safely.
* [ ] startup compatibility checks work.
* [ ] database errors are surfaced.
* [ ] world-reset development tooling exists.

## Player State

* [ ] Minimal player position/state can survive restart.
* [ ] ship state survives appropriately where implemented.
* [ ] player loads safely with terrain/collision readiness.

## Tests

* [ ] DB round-trip tests pass.
* [ ] persistent transform tests pass.
* [ ] world delta reconstruction tests pass.
* [ ] procedural removal tests pass.
* [ ] region boundary tests pass.
* [ ] generation-version mismatch test passes.
* [ ] previous Sprint 001–004 tests still pass.
* [ ] full project builds successfully.

## Manual Validation

* [ ] structure survives leaving/re-entering region.
* [ ] structure survives complete process restart.
* [ ] removed procedural object remains absent.
* [ ] planet A and planet B state remain isolated.
* [ ] structures work around different parts of spherical planet.
* [ ] no obvious duplicate persistent entities appear.
* [ ] storage remains proportional to modifications rather than universe size.

---

# 100. THE DEFINING DEMONSTRATION

Before declaring Sprint 005 complete, capture or perform this full sequence:

```text
START GAME

↓
fly through space

↓
approach procedural planet

↓
enter atmosphere

↓
land in procedural forest

↓
exit spacecraft

↓
walk between trees

↓
place a beacon / simple structure

↓
remove one procedural tree

↓
return to ship

↓
take off

↓
leave region / planet

↓
QUIT GAME COMPLETELY

↓
START GAME

↓
return to same procedural system

↓
approach same planet

↓
land in same region

↓
same mountains
same coastline
same biome
same forest

BUT:

placed structure exists

AND

removed tree is still absent
```

If this works, then we have proven:

# A PERSISTENT, PLAYER-MODIFIABLE, PROCEDURALLY GENERATED PLANET INSIDE AN ENORMOUS UNIVERSE.

That is the entire purpose of Sprint 005.

---

# 101. DO NOT BEGIN SPRINT 006

Do not proceed into the next major system during this sprint.

Sprint 006 will be:

# INTERSTELLAR & GALACTIC TRAVEL

Its objective will be to move beyond one local planetary system and make our enormous universe genuinely navigable.

Expected areas include:

```text
multiple generated star systems
↓
system discovery
↓
stellar spatial distribution
↓
system-level streaming
↓
interstellar coordinates
↓
high-speed flight
↓
warp / FTL
↓
continuous system exit
↓
interstellar void
↓
arrival at another star
↓
planet discovery
↓
landing on another procedural planet
```

It should also establish the beginnings of:

```text
galaxy representation
galactic sectors
galaxy LOD
intergalactic architecture
```

without attempting to render billions of stars individually.

Do NOT implement Sprint 006 now.

---

# 102. COMPLETION REPORT

At completion provide:

## Implemented

Exactly what genuinely works.

## Persistence Architecture

Explain:

* World State layer
* persistence abstraction
* persistence regions
* stable IDs
* SQLite schema
* delta model
* player-created state
* procedural removals
* generation version compatibility

## Runtime Lifecycle

Explain:

```text
region prediction
→ persistent load
→ procedural generation
→ delta merge
→ runtime representation
→ unload
```

## Validation

List exact:

* builds
* automated tests
* manual tests
* restart tests
* stress tests

actually performed.

## Performance

Report measured values only.

## Storage Scaling

Report sample DB sizes / query timings where measured.

## Known Limitations

Be explicit.

## Technical Debt

List temporary solutions.

## Recommended Sprint 006

Define the smallest possible next sprint required to prove:

# CONTINUOUS TRAVEL FROM ONE PROCEDURALLY GENERATED STAR SYSTEM TO ANOTHER.

Do not implement it yet.

---

# FINAL PRINCIPLE

The procedural universe is the immutable mathematical foundation.

Player actions create a sparse layer of history on top of it.

Never turn:

```text
huge procedural universe
```

into:

```text
huge database
```

The storage requirement should depend on how much players have changed, not how much universe mathematically exists.

An untouched galaxy should cost essentially nothing to persist.

A heavily populated city should cost according to the history created there.

That property is what eventually allows us to support:

```text
billions of planets
+
player settlements
+
cities
+
civilizations
+
years of persistent history
```

without storing every untouched rock and tree in existence.

Begin Sprint 005.
