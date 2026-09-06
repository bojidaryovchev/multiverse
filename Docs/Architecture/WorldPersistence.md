# World Persistence

How a universe that is not stored can nevertheless be permanently changed.

Related: [PersistentEntityIdentity.md](PersistentEntityIdentity.md),
[ADR-006](../ADR/ADR-006-delta-persistence.md).

![A beacon still standing after a process restart](../Sprints/Sprint-005/Persistence-AfterRestart.png)

---

## The equation

```
procedural base world  +  sparse deltas  =  current world
```

Nothing is saved because it exists. A planet has billions of trees and not one
of them is a row; the generator already knows where they are. A row appears only
when a player has made the world differ from what the generator would produce.

The property this buys is the one the whole project rests on:

> **Storage scales with player activity, not with universe size.**

Ten billion untouched planets cost the same as ten: nothing. A heavily built
settlement costs what was built there. Measured: an empty world is 20 KB, and a
thousand structures add 221 bytes each.

---

## The layers

```
gameplay
    |
    v
UWorldStateSubsystem          what the world is now
    |
    v
IWorldPersistenceStore        storage, abstracted
    |
    v
FSQLiteWorldPersistenceStore  the local backend
```

Everything that creates, removes or queries a persistent thing goes through the
world state layer. Nothing else opens a database and nothing else knows one
exists — SQLite is a **private** module dependency, so the build enforces that
rather than leaving it to discipline.

That is not tidiness. Sprint 007 replaces local authority with a server, and the
difference between "swap the store" and "rewrite every interaction" is whether
this layer exists now. It is also where authority will live: today
`CreateEntity` succeeds immediately, and on a server it becomes a request that
can be refused. Callers already treat the result as an answer rather than an
assumption.

---

## Regions

Deltas are partitioned spatially, so returning to a place loads that place's
changes and nothing else. One table for the universe would make visiting a
planet cost proportional to everything every player had ever done anywhere.

**A persistence region is not a terrain patch.** Terrain patches are chosen by
screen-space error and change with graphics settings; tying persistence to them
would mean a settings change relocated everybody's houses. Regions are a
cube-sphere address at a level chosen **once per planet from its radius**, to
target a constant 2 km region — so a small moon does not get a hundred times
more rows than it needs, and the level is stored in the id rather than
recomputed.

The region key packs face, level and coordinates rather than hashing them, and
the planet key stays a separate column. Two planets have regions at identical
addresses; a hash would be one birthday collision away from merging two
settlements.

---

## What a structure stores

Never an Unreal transform — that is relative to a render origin rebased every
ten kilometres, so it would mean something different every session.

| Field | Why |
| --- | --- |
| `Direction` | Unit vector from the planet centre. Full double precision, because a metre on a planet-sized sphere needs it. |
| `HeightAboveTerrainMeters` | A small number that stays small, and is re-applied to wherever the ground *is* on load. |
| `YawRadians` | About the **local up**, not a world quaternion. |
| `Scale` | |

The world rotation is rebuilt on load from the tangent frame at that direction.
That is what makes a structure survive the planet turning, an origin rebase,
being approached from another direction, and a future server migration: what was
preserved is its relationship to the ground, not a snapshot of a coordinate
system.

Pitch and roll are deliberately absent. A structure sits flat and the ground's
slope supplies any tilt; when something needs to be placed at an angle, this
grows a field rather than the meaning of yaw changing.

---

## Tombstones

Removing a procedural entity stores its **name and nothing else**:

```
entity_id   01112B37E6F7B55679DF48FDBF718E58
planet_key  ...
region_key  ...
is_removal  1
```

Sixteen bytes of identity. The generator can still produce the tree; the only
new information is that it should not be shown. Duplicating the tree's species,
position and scale to record its absence would defeat the entire point of
deriving identity.

Removals live in the same table as creations, not a separate one: a region load
wants both, and splitting them doubles the round trips to answer the only
question anybody asks — what is different here.

---

## Load order

```
1. vegetation selection asks for the region      RequestRegion
2. the region loads on a worker thread           LoadRegion
3. procedural content is generated               Scatter
4. removed entities are filtered out             IsProceduralEntityRemoved
5. survivors become instances
6. created entities become structures            OnRegionLoaded
```

Step 4 happens **between** the worker's result and the instance upload, so a
removed tree never becomes an Unreal instance at all — section 29's requirement
not to spawn and immediately destroy known-removed entities.

Steps 2 and 3 race, and that race is real: on the first visit after a restart a
scatter job routinely finishes before its region's deltas do, and the chopped
tree is standing there again. **Vegetation rebuilds when a region carrying
removals arrives**, which closes it. The tree is visible for the fraction of a
second between the two — better than blocking vegetation on a database read, and
short of the honest fix, which is for the streamer to treat a region load as a
precondition rather than a parallel task.

---

## Existence is not representation

A beacon on the far side of a planet **exists**: it is a row and it is part of
the current world. It has no Actor, no transform, no collision and no cost,
because nobody is near it.

Conflating the two is what makes persistent worlds impossible to scale. If
existence required an Actor, a world with ten thousand structures would spawn
ten thousand Actors at startup and a world with ten million could not be loaded
at all. Here the row is the truth and the Actor is a temporary rendering of it,
created when a region is relevant and destroyed when it is not.

`UPlanetStructureComponent` subscribes to region load and unload rather than
polling, so the ordering is guaranteed rather than a race between timers, and it
refuses to represent an entity twice — which is what makes rapid region
traversal safe, since a region loaded, evicted and loaded again broadcasts
twice.

---

## Durability

Writes are **synchronous and immediate**. Section 37 asks that changes persist
on the action rather than at exit, and the strongest form of that is that
`CreateEntity` has already written by the time it returns — a crash one frame
later cannot lose it. A placement is a rare, deliberate act; a millisecond for
durability is the right trade.

SQLite runs in WAL mode with `synchronous=NORMAL`: a crash loses at most the
uncommitted tail and every completed transaction is recoverable, without an
fsync on every placement.

**The write-ahead log is checkpointed every 256 writes.** Without that it grows
for the life of the session and every read consults it as well as the main file.
Measured: a region read after a thousand individual writes took **26.2 ms**,
against 0.8 ms after a hundred — same query, same index, same row count. It was
the log, not the data. With periodic checkpointing it is **0.047 ms at 1100
records**, flat in record count.

Writes are idempotent by construction: `entity_id` is the primary key and
inserts are `INSERT OR REPLACE`, so a retried placement replaces rather than
doubling a building. Deleting something absent succeeds, because the caller's
intent — that it not be there — is already satisfied.

---

## Version compatibility

Three versions are recorded and checked at startup:

| | If it differs |
| --- | --- |
| Schema version | **Fatal.** The session runs without persistence rather than misreading rows. |
| Universe seed | **Fatal.** One universe's deltas must never be applied to another. |
| Generation versions | **Loud, not fatal.** |

The generation case is the interesting one. A terrain version bump moves
mountains, and a house recorded on a hill that is now a valley floats three
hundred metres. The world still loads — the structures are where they were
recorded — and the mismatch is reported in the log and on the HUD. Refusing to
start would be worse, and silently proceeding would be much worse.

Universe isolation is also **structural**: the database filename contains the
seed, so changing the seed cannot reach the old data because it is not looking at
the same file. The metadata check then catches the remaining case, a file whose
name says one thing and whose contents say another.

---

## Commands

```
universe.Build [beacon|foundation] [yawDeg]   place a structure where you look
universe.Demolish                             remove the nearest structure
universe.ChopTree [radius]                    remove the nearest procedural tree
universe.PersistenceInfo                      state, storage, this region's contents
universe.PersistenceReset planet|all          erase modifications (needs an argument)
universe.PersistenceStress <n>                place N structures and measure
```

`universe.ChopTree` finds its target by **re-running the placement** for the
surrounding patches rather than tracing against rendered instances. That is the
point of deriving identity: the generator can say what is there without anything
being rendered, so a removal is recorded against the thing the generator would
produce — which is the same thing it will produce next session.
