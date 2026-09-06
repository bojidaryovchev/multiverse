# Sprint 005 — World Interaction & Persistence

**Status: the defining demonstration works.** Place a structure, remove a
procedural tree, quit the process entirely, restart, return — the structure is
there and the tree is not, with the world around it regenerated from seed and
unchanged.

![A beacon still standing after a process restart](Sprint-005/Persistence-AfterRestart.png)

Several sprint items are not built, and they are listed explicitly below.

---

## The defining demonstration

```
Session 1
  universe.GotoBiome TemperateForest
  universe.Build beacon
    -> Built universe.structure.beacon.v1
       id 027F083ABBDDED026ABDA8DCB4D40EA6
       region P6EBD53F9921E179F/-Z/L12/4019/1309   elevation +3623 m
  universe.ChopTree 60
    -> Removed BroadleafTree
       id 01112B37E6F7B55679DF48FDBF718E58   7.1 m away

  [process exits]

Session 2  (a new process)
  World state open: 2 records, 20.0 KB
  universe.GotoBiome TemperateForest
    -> Structure 027F083ABBDDED026ABDA8DCB4D40EA6 now represented; 1 live
    -> Region P6EBD53F9921E179F/-Z/L12/4019/1309 brought 1 removal(s)
    -> Patch -Z L13 (8039, 2619) Canopy: suppressed 1 removed instance
```

Two rows describe every difference between the generated world and the current
one.

---

## What was built

| | |
| --- | --- |
| **Entity identity** | 128-bit, self-describing; derived for procedural entities, assigned for created ones |
| **Persistence regions** | Stable cube-sphere partition, sized per planet, independent of terrain LOD |
| **Delta model** | Created entities and removal tombstones, in one table |
| **Storage abstraction** | `IWorldPersistenceStore`, with SQLite as a *private* module dependency |
| **SQLite backend** | Versioned schema, WAL, transactions, periodic checkpointing, idempotent writes |
| **World state layer** | `UWorldStateSubsystem` — the server-authority seam, with a bounded region cache and events |
| **Building** | Two structure types, placement validation, immediate persistence, removal |
| **Procedural removal** | Trees, identified by re-running placement, suppressed before instancing |
| **Streaming** | Structures and removals load and unload with their region |
| **Version checking** | Schema, universe and generation versions, each with the right severity |
| **Tooling** | Six console commands, a HUD section, and a stress command that measures |

Architecture in [WorldPersistence.md](../Architecture/WorldPersistence.md),
[PersistentEntityIdentity.md](../Architecture/PersistentEntityIdentity.md) and
[ADR-006](../ADR/ADR-006-delta-persistence.md).

---

## Verification

### Automated tests

```
Tools\StandaloneTests\RunTests.bat
  67/67 tests passed, 1,186,413/1,186,413 assertions passed
```

Three new test bodies:

| Test | What it pins |
| --- | --- |
| `PersistenceRegionIdentity` | Determinism; level targeting at four planet radii; the packed key round-trips its address exactly; nearby points share a region; **two planets with the same radius give the same address but different regions** |
| `PersistentEntityIdentity` | 576 derived ids unique and reproducible; every input part of the name including cell order and both generation versions; 4096 created ids unique; text and binary round-trips; malformed text rejected rather than parsed as zero |
| `VegetationPersistentIdentity` | The real scatter, over six patches on five cube faces: every instance names itself the same way across a regeneration, and uniquely within its patch-layer |

### Manual and scripted tests actually run

- **Build and persist** — beacon placed, written in 1.48 ms, visible immediately.
- **Procedural removal** — tree found by re-running placement, tombstone written
  in 0.92 ms.
- **Process restart** — new process, database reopened with 2 records, structure
  represented, removal applied, tree suppressed. Run four times during
  development, including twice against a database written by an earlier build.
- **Universe isolation** — the database filename contains the universe seed, so a
  different seed opens a different file. Verified by inspection of
  `Saved/WorldState/Universe-BDEFD300CB477825.db`.
- **Storage scaling** — 100 and 1000 structures, below.

### Measured performance

```
Empty world                    20.0 KB
+ 100 structures    1.28 ms each   163.8 bytes/record   →  36.0 KB
+ 1000 structures  12.70 ms each   221.2 bytes/record   → 252.0 KB

Region read after   100 records:  0.057 ms
Region read after  1100 records:  0.047 ms

Single structure write (gameplay path):  1.48 ms
Single removal write:                    0.92 ms
```

The region read is **flat in record count**, which is the property that matters:
loading a place costs what is in that place, not what is in the universe.

Two honest notes on these numbers:

- **12.7 ms per structure at 1000 is the worst case, deliberately.** The stress
  command places each in its own transaction. `SaveRecords` batches and nothing
  in gameplay needs it — a player places one structure at a time, at 1.48 ms.
- **The read time is only flat because of checkpointing.** Before the periodic
  WAL checkpoint was added, a region read after 1100 records took **26.2 ms** —
  same query, same index, same rows. It was the write-ahead log, not the data.

---

## Defects found, and how

1. **The instance budget participated in identity.** The vegetation scatter
   capped its grid resolution by the caller's budget, so a tree's persistent
   name depended on a runtime tuning value. A tree chopped by a command using a
   budget of 4000 had a different name than the same tree generated by the
   streamer at 2000 — and came back after a restart. *Found by the chopped tree
   reappearing.* The grid is now a function of the patch and the biome table
   alone; the budget is a per-cell keep roll. **Presence depends on the budget;
   identity does not.**

2. **Vegetation and region loads raced.** Region loads are async and vegetation
   generation was not waiting, so on the first visit after a restart a scatter
   job routinely finished before its deltas did — exactly the flicker section 31
   names. Vegetation now rebuilds when a region carrying removals arrives.

3. **The write-ahead log grew unbounded**, turning a 0.8 ms region read into a
   26 ms one over a thousand writes. *Found by the stress command, which existed
   to find exactly this.*

4. **Two physical constants were defined in four anonymous namespaces**, and the
   unity build put two of them in one translation unit as soon as a new file
   shifted the grouping. Consolidated into `UniversePhysics`.

---

## What was *not* done

Each is an explicit Sprint 005 item.

- **No player or ship save state.** Quitting while standing on a planet does not
  restore that position; the game mode places the player as it always does.
  Sections 50–52.
- **No build-mode UI or placement preview.** Placement is a console command with
  validation — slope, water, overlap — but there is no ghost geometry, no
  valid/invalid colouring and no interactive rotation. Sections 21, 86.
- **No migration machinery.** Versions are recorded and mismatches detected and
  reported; nothing can be migrated. Section 17 asks only for the skeleton, and
  the skeleton is the version check.
- **The generation-version mismatch path has not been exercised.** The code
  compares and reports; no run has been done with a deliberately bumped version.
  Section 83.
- **`IsProceduralEntityRemoved` scans the loaded regions** rather than being
  given one. Cheap now — a bounded cache of mostly-empty regions — and it is a
  linear scan that would not survive a heavily built world.
- **Structures belong to exactly one region.** A structure spanning a boundary is
  not handled. Regions are 2 km and structures are metres, so it is theoretical;
  the eventual answer is a primary region plus a spatial overlap index.
  Section 63.
- **Region boundary and rapid-traversal stress tests were not run.** The
  double-representation guard exists and is commented, but sections 62, 64, 71
  and 72 describe scripted tests that were not performed.
- **No transaction-rollback test.** Rollback is implemented on write failure and
  is not covered by a test that forces a failure. Section 67.
- **Only vegetation can be removed.** Rocks and wildlife have identities in
  principle — the scatter names every layer — but only the canopy layer is
  searched by `universe.ChopTree`.
- **Structures do not participate in vegetation collision avoidance.** A beacon
  can be placed among trees and trees are not cleared around it.

---

## Commands added

| Command | Effect |
| --- | --- |
| `universe.Build [beacon\|foundation] [yawDeg]` | Place a structure where you are looking |
| `universe.Demolish` | Remove the nearest player-built structure |
| `universe.ChopTree [radius]` | Remove the nearest procedural tree |
| `universe.PersistenceInfo` | World state, storage, and this region's contents |
| `universe.PersistenceReset planet\|all` | Erase modifications; requires an explicit argument |
| `universe.PersistenceStress <n>` | Place N structures and report write and query cost |

---

## Recommended Sprint 006

Sprint 006 is interstellar travel, and the smallest thing that proves it is:

> Leave one procedurally generated star system under power, cross the interstellar
> void without a loading screen, arrive at a different generated star, and land on
> one of its planets.

The pieces that need to exist:

- **More than one system instantiated at a time**, or a defined handover as one
  is torn down and the next built — currently the game mode builds exactly one
  system at `StartPlay` and never changes it.
- **System-level streaming**, with the same shape as terrain streaming: a system
  becomes relevant, its bodies are generated, and it is released when nobody is
  near. Sprint 001 already generates systems from an address; nothing yet manages
  their lifetime.
- **A star field**, so the void is navigable. The scaled-space bodies are hidden
  on a planet surface (ADR-004) and there is nothing at all between systems.
- **Travel that takes a bounded time**, whether that is the existing thrust tiers
  or something explicitly faster. Four light years at the current top tier is
  about ten hours.
- **Arrival that is not a special case** — the frame selector already handles
  entering a planetary frame, and entering a *system* has no equivalent.

The temptation to avoid is making interstellar travel a menu. The coordinate
system already spans 10^13 light years exactly; what is missing is the streaming
that would let a player cross any of it continuously.
