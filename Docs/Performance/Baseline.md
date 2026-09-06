# Performance Baseline

*Sprint 008. Measured, not estimated. Reproduce with `universe.Perf`.*

## The machine

| | |
|---|---|
| CPU | 24 logical cores |
| OS | Windows 11 Pro 26200 |
| Engine | Unreal Engine 5.8.2 |
| Build | Development, packaged client |

## How to reproduce

```bat
UnrealEditor.exe Universe.uproject -game -unattended -nopause -nullrhi ^
    -ExecCmds="universe.After 8 universe.Perf,universe.After 14 universe.Land,universe.After 30 universe.Perf,universe.After 34 quit"
```

`universe.Perf` samples everything at once and prints it in a fixed order, so
two baselines diff cleanly. Taking the readings from five separate commands at
five separate moments would not be comparable, which is why it is one command.

## Numbers

### In space, one system streamed in

| Measure | Value |
|---|---|
| Memory | 1,596 MB |
| Systems tracked | 19 (8 generated) |
| Terrain patches | 6 visible |
| Terrain triangles | 52,224 |
| Patch selection | 0.01 ms/frame |
| Origin rebases | 2 |

### Landed on a planet

| Measure | Value |
|---|---|
| Memory | 1,774 MB (+178 MB) |
| Terrain patches | 189 visible, 2 with cooked collision |
| Deepest quadtree level | 11 |
| Terrain triangles | 1,645,056 |
| Terrain vertices | 847,665 |
| Vegetation instances | 9,929 across 18 patches |
| Patch selection | 0.25 ms/frame |
| Patch generation | 42.8 ms average, on a worker thread |
| Persistence region read | 1.7 ms |
| Origin rebases | 3 |

### Over a full planetary journey (orbit to surface and back)

| Measure | Value |
|---|---|
| Origin rebases | 1,064 |
| Patches generated | 330 |
| Patches released | 324 |
| Results discarded | 83 |
| Peak memory | 1,743 MB |

## Reading these honestly

**The frame time is not here, and that is deliberate.** Every automated run in
this project uses `-nullrhi`, so there is no renderer to measure, and every one
that uses `-benchmark` has a *fixed* delta - it reports exactly 33.33 ms however
hard the machine is working. `universe.Perf` now says so on the line itself
rather than leaving it as a trap:

```text
Frame        : 33.33 ms (30 fps)   [fixed by -benchmark; not a measurement]
```

Without the renderer and without a cap, the CPU side of a frame while standing
on a planet is about **0.5 ms**. That is a real number about a real workload -
terrain selection, streaming, environment queries, persistence - and it is not a
frame rate, because it excludes everything the GPU does with 1.6 million
triangles.

**Patch generation is 42.8 ms and that is fine.** It happens on a worker thread,
not the game thread; the game thread's share is the 0.25 ms selection and the
0.71 ms upload. What the number bounds is *latency* - how long after a patch is
needed before it can appear - not throughput.

**Memory is dominated by the engine, not by us.** A bare editor process is
around 1.5 GB. The interesting figure is the delta: **178 MB to stand on a
planet**, covering 1.6 million triangles of terrain, ten thousand vegetation
instances and the environment data behind them.

## What has a budget, and what it is

| Thing | Budget | Where |
|---|---|---|
| Selected patches | 256 | `UPlanetTerrainComponent::MaxSelectedPatches` |
| Patch uploads per frame | 4 | `MaxUploadsPerFrame` |
| Collision radius | 5,000 m | `CollisionRadiusMeters` |
| Visible star systems | 96 | `UStarSystemStreamingSubsystem::MaxVisualSystems` |
| Systems with planet placeholders | 4 | `MaxNearbySystems` |
| Active systems | **1** | Structural - see StarSystemStreaming.md |
| Persistence regions cached | 64 | `UWorldStateSubsystem` |
| Regions per network client | 64 | `AUniversePlayerController::MaxSubscribedRegions` |
| Galaxy cell cache | 16 entries, thread-local | `FStarSystemGenerator` |

Every one of these is a named constant with a comment saying why. None is a
magic number in an expression.

## What to watch for in a regression

| Symptom | Likely cause |
|---|---|
| Rebases climbing while stationary | The tracked anchor is following something that moves. |
| `Terrain churn: discarded` climbing | Patches are being selected and unselected faster than they generate - the LOD hysteresis is too tight. |
| Streaming transitions climbing while stationary | A streaming threshold has lost its hysteresis. |
| Storage read time growing | The WAL is not being checkpointed. It reached 26 ms at 1,100 records before Sprint 005 added `PRAGMA wal_checkpoint(PASSIVE)` every 256 writes; it is 1.7 ms now. |
| Memory growing over a long session | Something is not releasing on unload. Compare `TotalGenerated` against `TotalReleased`. |

## Known costs that are accepted

- **Warp takes 136 seconds for 12.7 light years.** That is the travel profile,
  not a performance problem: `WarpMaxSpeedMs` is 10^15 m/s and the ramps are
  most of the trip. It is data (`FTravelProfile`) and changing it changes
  nothing else.
- **Patch generation is ~43 ms.** Terrain is fractal noise evaluated per vertex
  at 129x129, transcendental-free for determinism. Making it faster means either
  fewer vertices or a different noise function, and the second changes the
  universe.
- **The first landing on a world is slower than later ones.** Nothing is cached
  between planets, by design: caching generated terrain across bodies is how a
  cache becomes a source of truth.

## Related

- [Testing.md](../Architecture/Testing.md)
- [GoldenPath.md](../Testing/GoldenPath.md)
- [PlanetLOD.md](../Architecture/PlanetLOD.md)
