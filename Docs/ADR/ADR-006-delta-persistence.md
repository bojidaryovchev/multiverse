# ADR-006: Delta Persistence, Entity Identity and the World State Seam

- **Status:** Accepted
- **Date:** 2026-09-06
- **Sprint:** 005
- **Related:** [ADR-002](ADR-002-seed-hierarchy.md),
  [ADR-005](ADR-005-procedural-environment.md),
  [WorldPersistence.md](../Architecture/WorldPersistence.md),
  [PersistentEntityIdentity.md](../Architecture/PersistentEntityIdentity.md)

---

## Problem

A universe reconstructed from mathematics cannot be saved — that is the point of
it. But a game in which nothing the player does survives a restart is not a game.

The two have to be reconciled without turning a huge procedural universe into a
huge database.

---

## Decision 1: store the difference, never the world

**Selected: `procedural base world + sparse deltas = current world`.**

A row exists only where a player has made the world differ from what the
generator would produce. Nothing is saved because it exists.

**Rejected: saving generated content on first visit.** It is the obvious
implementation and it converts the universe's size into the database's size —
one visit to a planet would write millions of rows describing trees the
generator can reproduce for free.

The invariant this preserves: **storage scales with player activity, not with
universe size.** An untouched galaxy costs nothing.

---

## Decision 2: procedural identity is derived, not assigned

**Selected: a procedural entity's id is a hash of the address the generator
placed it at.**

Nothing has to be written down for a tree to have a name. That is what makes
"tree 817291 is removed" a complete sixteen-byte statement about a world
containing billions of trees, and it is why a tombstone can carry the name and
nothing else.

**Rejected: assigning ids on first generation and storing them.** That is
saving the world again, one identifier at a time.

### The corollary, learned the hard way

A runtime budget was feeding the vegetation grid resolution, which made a tree's
name depend on a tuning value. A tree chopped with one budget came back under
another.

> **Nothing with process or configuration lifetime may participate in identity.**

The grid is now a function of the patch and the biome table alone; the budget is
a per-cell keep roll. Presence depends on the budget, identity does not.

### Generation versions are part of the name

A terrain or environment version bump changes which entities exist, so a removal
recorded under the old version **stops matching** rather than deleting whatever
now occupies that address. The removal becomes inert, which is the correct
outcome for a statement about a world that no longer exists.

---

## Decision 3: persistence regions are not terrain patches

**Selected: a cube-sphere address at a level chosen per planet from its radius,
targeting a constant 2 km region, with the level stored in the id.**

**Rejected: reusing terrain patch ids.** Terrain LOD is chosen by screen-space
error and changes with graphics settings. Persistence must be stable across an
LOD rewrite and across machines; tying them together would mean a settings change
relocated everybody's houses.

**Rejected: a fixed level for every planet.** It gives 2.4 km regions on an
Earth-sized world and 66 m ones on a small moon — a hundred times more rows than
that moon needs for the same amount of building.

The region key **packs** its address rather than hashing it, and the planet key
stays a separate column. Two planets have regions at identical addresses; a hash
would be one birthday collision from merging two settlements.

---

## Decision 4: a world state layer between gameplay and storage

**Selected: `gameplay -> UWorldStateSubsystem -> IWorldPersistenceStore ->
SQLite`, with SQLite as a private module dependency.**

Sprint 007 replaces local authority with a server. The difference between "swap
the store" and "rewrite every interaction" is whether this layer exists now.

Making SQLite private means the **build** enforces the seam rather than
discipline: a gameplay header cannot include a SQLite type and quietly form a
dependency on it.

The layer is also where authority will live. `CreateEntity` returns an id today
and will return a refusable request later; callers already treat the result as an
answer rather than an assumption.

---

## Decision 5: the store interface is synchronous; asynchrony is the caller's

**Selected: `IWorldPersistenceStore` is a set of blocking functions. The world
state layer runs reads on the thread pool.**

**Rejected: an async interface.** It would force every implementation to
reinvent a task system, and would make the backend far harder to test than it
needs to be — a synchronous store is a function, and a function can be called
from a test with no world, no tick and no waiting.

---

## Decision 6: writes are synchronous and immediate

**Selected: `CreateEntity` has written by the time it returns.**

A crash one frame later cannot lose it. A placement is a rare, deliberate act and
a millisecond for durability is the right trade.

**Rejected: a write queue flushed at exit or on a timer.** The classic
five-hours-then-save-everything failure, and the one section 37 explicitly warns
against.

WAL with `synchronous=NORMAL`: a crash loses at most the uncommitted tail without
an fsync per placement. **Checkpointed every 256 writes** — without that the log
grows all session and a region read went from 0.8 ms to 26 ms with the same
query, index and row count.

---

## Decision 7: a structure stores a direction, a height and a yaw

**Selected: canonical planet-relative placement, with the world rotation
re-derived on load from the local tangent frame.**

**Rejected: an Unreal transform.** It is relative to a render origin rebased
every ten kilometres, so it would mean something different every session.

**Rejected: a world-space quaternion.** It bakes in the planet's orientation at
the moment of placement, and this planet rotates. Storing yaw about the local up
preserves the building's relationship *to the ground it sits on*, which is the
thing that actually matters.

---

## Decision 8: existence is not representation

**Selected: a persistent entity's existence is a database row; its Actor is a
temporary rendering created when its region is relevant.**

If existence required an Actor, a world with ten thousand structures would spawn
ten thousand at startup and one with ten million could not be loaded at all.

---

## Consequences

### Good

- The defining demonstration works: place a beacon, chop a tree, quit the
  process, restart, return — the beacon is there and the tree is not.
- Storage is 20 KB empty and 221 bytes per structure; region reads are 0.047 ms
  and flat in record count.
- Two planets, and two universes, cannot contaminate each other — the second
  structurally, via the database filename.
- Version mismatches are detected and reported rather than silently corrupting.
- Deltas are inspectable: `SELECT * FROM entity_records` is a debugging tool,
  because the placement is columns rather than a blob.

### Bad / accepted costs

- **Vegetation and region loads race.** A chopped tree is visible for a fraction
  of a second on the first visit after a restart, until the region's deltas
  arrive and vegetation rebuilds. The honest fix is for the streamer to treat a
  region load as a precondition rather than a parallel task.
- **`IsProceduralEntityRemoved` scans loaded regions** rather than being given
  one. Cheap with a bounded cache of mostly-empty regions; it is a linear scan
  and would not survive a heavily built world without an index.
- **Rebuilding vegetation on a removal is blunt** — it discards every nearby
  patch to make one tree disappear.
- **No migration machinery.** Versions are recorded and mismatches reported;
  nothing can yet be migrated.
- **No player or ship save state.** The player restarts wherever the game mode
  puts them.
- **Structures belong to exactly one region**, so one spanning a boundary is not
  handled. Regions are 2 km and structures are metres, so it is theoretical —
  the eventual answer is a primary region plus a spatial overlap index.
- **No build-mode UI or placement preview.** Placement is a console command with
  validation; there is no ghost geometry and no rotation control beyond an
  argument.
- **Writes are one transaction each.** `SaveRecords` batches, and nothing uses
  it outside the stress command — 12.7 ms per structure when placing a thousand
  in a loop, against 1.3 ms for a hundred.

### Neutral

- Entity type ids are strings (`universe.structure.beacon.v1`) rather than enum
  ordinals, so a class rename cannot orphan rows and inserting a value cannot
  reinterpret them.

## Revisit if

- A heavily built region makes the removal scan visible — then a per-region
  removal index reached through the region the instance belongs to.
- Terrain or environment generation changes for real — then the migration path,
  which currently exists only as a detected mismatch.
- Multiplayer arrives — the seam is here; what is missing is that `CreateEntity`
  cannot yet fail for a reason other than storage.
