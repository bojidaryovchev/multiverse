# Data Flow

*Sprint 008. What actually happens, in order, when the player does something.*

## Starting a session

```text
AUniverseGameMode::StartPlay
  |
  +-> UUniverseWorldSubsystem::SetUniverseSeedText("sprint-001")
  |     derives the seed hierarchy; nothing is generated yet
  |
  +-> FGalaxyGenerator::FindNearestGalaxy(origin)
  |     the universe origin is intergalactic, so the search starts inside a
  |     galaxy - half way out along its disk
  |
  +-> FindHabitableSystem(that place)
  |     scores candidates on cheap astronomy, resolves environments for the
  |     survivors, prefers one with life and coastline
  |
  +-> UWorldStateSubsystem::OpenWorld(seed)
  |     one database per universe seed; the client path opens nothing
  |
  +-> AUniverseGameState::SetWorldIdentity(seed, versions)
        replicated; every client checks it and disconnects on a mismatch

...then, on the first tick:

UStarSystemStreamingSubsystem::Tick
  |
  +-> Rescan around the tracked anchor
  |     addresses only - GetSystemPosition, not GenerateSystem
  |
  +-> ApplyStates
        distance and hysteresis decide DescriptorOnly / DistantVisual /
        NearbyVisual / Active, subject to budgets and exactly one Active
        |
        +-> BuildPlanetActor for the Active one
              APlanetActor::Initialise -> terrain, environment, vegetation
```

Nothing above touches a file, and nothing is stored. Every step is a function of
the seed and an address.

## A frame while flying

```text
AUniverseProbePawn::Tick
  |
  +-> UpdateTravelReadouts
  |     nearest body distance -> SelectMode -> Local/Interplanetary/Interstellar
  |
  +-> if warping:
  |     FInterstellarTravel::Step
  |       |
  |       +-> braking law: v^2/2a against distance to target
  |       +-> arrival clamp: never step past closest approach
  |       +-> FTravelBroadPhase::SweepAgainstBodies
  |       |     3D DDA over sectors crossed
  |       |       -> dilate by one sector
  |       |       -> candidate system positions (no generation)
  |       |       -> bounding sphere rejection
  |       |       -> generate survivors, sweep star and planets
  |       +-> clamp the step short of anything in the way
  |
  +-> else:
  |     thrust, brake, clamp to MaxSpeed, IntegratePlanetaryStep
  |       gravity, drag, swept terrain collision, landing
  |
  +-> Anchor->SetUniversePosition(next)
        |
        +-> UUniverseWorldSubsystem::RebaseIfNeeded
              if the tracked anchor has drifted past the rebase radius, move
              the render origin and shift *every* anchored actor by the same
              delta in the same frame
```

The rebase is where canonical coordinates become Unreal ones. It happens at the
point of movement rather than on a subsystem tick: one frame at 10^12 m/s covers
3.3 x 10^12 cm, so waiting would leave the transform thousands of times beyond
the bound for part of every frame.

## A frame while standing on a planet

```text
UPlanetTerrainComponent::Tick
  |
  +-> FPlanetQuadtree::SelectPatches(observer)
  |     screen-space error per patch, horizon culling, neighbour balancing
  |
  +-> for newly needed patches:
  |     async: FPlanetPatchMesh::Build
  |              cube-sphere position -> FPlanetTerrain::Evaluate
  |              (fractal noise, transcendental-free)
  |     game thread: UpdateSlot, cook collision within CollisionRadiusMeters
  |
  +-> release patches no longer selected

UPlanetVegetationComponent::Tick
  |
  +-> for patches near the observer:
  |     FPlanetVegetation::Scatter(patch, biome table)
  |       jittered grid, per-cell keep roll for the instance budget
  |       (the budget must not affect the grid - see PersistentEntityIdentity)
  |
  +-> for each instance:
        FPersistentEntityId::ForVegetation(planet, patch, layer, cell, versions)
        UWorldStateSubsystem::IsProceduralEntityRemoved(id) ? skip : draw

APlanetCharacter::Tick
  |
  +-> UUniverseWorldSubsystem::GetLocalUp(position)
  +-> Movement->SetGravityDirection(-up)
  +-> if no cooked collision below: hold, gravity off
        (visible for a fraction of a second; falling through a planet is not)
```

## Building something

```text
[B] or universe.Build
  |
  +-> find the ground below, check slope, check it is not under water,
  |   check nothing is already there
  |
  +-> single player / server:
  |     UWorldStateSubsystem::CreateEntity
  |       -> FPersistentEntityId::CreateNew
  |       -> IWorldPersistenceStore::SaveRecord   (synchronous, ~2 ms)
  |       -> OnEntityCreated broadcast
  |            -> UPlanetStructureComponent draws it
  |            -> AUniversePlayerController forwards it to subscribed clients
  |
  +-> client:
        AUniversePlayerController::ServerRequestBuild
          -> server re-derives the planet, the placement and the range
          -> server writes, then replicates to everyone subscribed to the region
```

A client never writes. Two independent guards enforce it: every mutating
function refuses without authority, and a client has no database open at all.

## Loading a region's changes

```text
something needs to know what is different here
  |
  +-> UWorldStateSubsystem::RequestRegion(regionId)
        |
        +-- server or single player:
        |     async LoadRegion from SQLite -> OnRegionLoaded
        |
        +-- client:
              ServerSubscribeRegion(regionId)
                -> server loads it and sends the whole region once
                -> ClientReceiveRegionDelta -> ApplyReplicatedRegion
                -> OnRegionLoaded, exactly as in single player
```

The routing is inside `RequestRegion` rather than at each call site, so
vegetation, structures and the debug commands all keep working unchanged in
multiplayer - the answer arrives through the same delegate, it just comes from
the server now.

## Moving, in multiplayer

```text
client                                    server
------                                    ------
simulate locally (terrain, gravity)
  |
  +-> every 100 ms:
        ServerUpdatePosition(canonical) --> ValidateProposedMove
                                             distance <= speed x time x 4 ?
                                             |
                                    yes -----+----- no
                                     |               |
                        SetAuthoritativeState   ClientCorrectPosition
                                     |               |
                                     v               v
                        replicated to all      client snaps back
                        relevant clients
                                     |
                                     v
other clients: ARemotePlayerAvatar reads the canonical position and places
               itself through *its own* anchor, in *its own* render space
```

## Quitting and coming back

```text
quit
  |
  +-> AUniverseGameMode::Logout writes the player's position as a world fact
  +-> SQLite closes; the deltas are already durable (written on the action)

start again
  |
  +-> same seed -> same universe, bit for bit
  +-> OpenWorld finds the database, migrates the schema if it is older
  +-> the streamer rebuilds the same systems from the same addresses
  +-> vegetation asks IsProceduralEntityRemoved and skips what was chopped
  +-> UPlanetStructureComponent draws what was built
```

Nothing was saved except the differences. Everything else was recomputed.

## What never flows

- **Render space never flows back into simulation.** Positions go canonical ->
  render, never the reverse, except through the one explicit
  `RenderLocationToUniverse` used by the render-origin owner itself.
- **Nothing with process lifetime flows into an identity.** Not an array index,
  not a pointer, not a budget, not the time.
- **A cache never flows into a result.** Every cache in the project can be
  cleared at any moment without changing any output.
- **A client never flows into the database.** Only the server writes.

## Related

- [MVPArchitecture.md](MVPArchitecture.md)
- [UniverseCoordinates.md](UniverseCoordinates.md)
- [WorldPersistence.md](WorldPersistence.md)
- [Networking.md](Networking.md)
