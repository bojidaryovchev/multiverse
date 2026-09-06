# ADR-008: Server authority, and replicating almost nothing

- **Status**: Accepted
- **Sprint**: 007
- **Builds on**: [ADR-001](ADR-001-universe-coordinate-system.md),
  [ADR-006](ADR-006-delta-persistence.md),
  [ADR-007](ADR-007-galaxy-density-and-system-streaming.md)

## Context

Sprints 001 to 006 built a single-player universe in which everything is either
derived from a seed or recorded as a sparse delta. Sprint 007 has to make two
players share it without abandoning either property.

The obvious approaches both fail:

- **Replicate the world.** Terrain vertices, trees, biome samples, star
  descriptors. Enormous, and pointless: both ends compute all of it identically
  from the same seed.
- **Simulate every player on the server.** The textbook authoritative model. It
  requires the server to hold a fully streamed planet per player, which scales
  as player count times planet cost - the shape of a system that never becomes
  an MMO.

There is also a structural obstacle that is specific to this project: **no two
clients share a render origin.** Each rebases Unreal's origin around its own
viewpoint, about a thousand times during a planetary descent. Every default
Unreal networking mechanism that moves a transform is therefore meaningless
here, because a transform is a statement in somebody's render space.

## Decision

### 1. Replicate four things and nothing else

```text
seed and version identity
authoritative dynamic state
persistent deltas
relevant entities
```

The universe is shared mathematical data, not shared state.

### 2. Identity is checked, and a mismatch is a refusal

`FUniverseWorldIdentity` carries the seed and all five generation versions. A
client rebuilds what its own build would produce and compares. Any difference
disconnects it with the reason.

There is no version of "mostly the same universe" that is worth playing in, and
a silent partial mismatch is worse than a failed connection because it looks
like it works.

### 3. Canonical positions replicate as data, on the player state

Not as pawn transforms. Each client converts the canonical position into its own
render space on receipt, and both are right. Remote players are local proxy
actors driven by that data.

This is not a workaround for a limitation; a replicated transform is simply the
wrong representation for a universe 10^10 light years across.

### 4. Movement is client-simulated and server-validated

The client runs the deterministic simulation it already has and proposes a
position. The server checks the proposal is *possible* - distance against the
regime's speed limit over elapsed time, with a factor-of-four tolerance for
frame and packet jitter - and corrects it otherwise.

This is stated as what it is. It prevents a client claiming to be anywhere in
the galaxy; it does not prevent one walking through a wall. Every check lives in
`ValidateProposedMove` and nothing bypasses it, so tightening it later is a
change in one place - but tightening it means server-side terrain, which is the
scaling problem above.

### 5. World edits are fully server-authoritative, with no prediction

Rare, deliberate, durable acts. A round trip is unnoticeable; a wrong one is a
permanent lie in the database. The server re-derives the planet, the placement
and the range rather than trusting any of them.

### 6. Persistence writes are server-only, guarded twice

Every mutating function refuses without authority, *and* a client has no
database open at all. Two independent reasons for one guarantee, because one of
them will eventually be edited by somebody unaware of the other.

### 7. Interest is structural before it is metric

"Same star system" - two integer addresses - is asked before any distance. A
player in another system is not far away in a sense a distance captures.

### 8. Identity comes from the connection URL, not from the connection

`?PlayerId=` read in `InitNewPlayer`. Everything durable keys on that string and
nothing knows how it was made, so an account service replaces four lines.

## Consequences

### Good

- Bandwidth is proportional to the number of nearby players and the number of
  changes, not to the size of the world. A player alone in a system costs a
  position update ten times a second and nothing else.
- Single player is unchanged. A standalone session is its own server; the same
  classes run and `HasAuthority()` is always true. There is one code path rather
  than a networked one and a "simple" one that diverges.
- The pieces that regional authority will need are already the right shape:
  address-derived identity, canonical positions, structural interest, an
  abstract persistence store, and explicit region subscription.
- Two players meeting on a planet, building, and seeing each other's work is
  demonstrated end to end by a script that compares a 128-bit entity id across
  three processes' logs.

### Bad, or the price

- **Movement validation is a speed bound.** Honest, bounded, and not a
  collision check.
- **The server's streaming viewpoint is the first player to join.** A
  single-region-server assumption, and the line that changes for regional
  authority.
- **Exactly one system may be Active on the server**, so players must be in the
  same one to interact fully. That limit comes from Sprint 006 and is about the
  simulation frame, not about networking.
- **A dedicated server target cannot be compiled on a launcher engine.** The
  target file is committed and correct; the run script uses the editor binary in
  the same net mode and says so at length.
- Two representations of the persistence types - the real ones and their wire
  forms - because the real ones live in engine-free modules and cannot be
  `USTRUCT`s. The wire forms hold no judgement of their own, but they are a
  place where a field can be forgotten.

## What this sprint taught

Three of the sprint's defects were the same shape, and none was visible from any
single process:

- Every replicated pawn claimed the tracked anchor.
- A dedicated server has no locally controlled pawn, so nothing claimed it.
- Clients spawned at the origin because the pawn asks the server-only game mode.

**Anything that works because there is exactly one of something is a bug waiting
for the second one.** One player, one pawn, one viewpoint, one process. That is
the rule this sprint added to the project's standing list.

## Related

- [Networking.md](../Architecture/Networking.md)
- [WorldPersistence.md](../Architecture/WorldPersistence.md)
- [StarSystemStreaming.md](../Architecture/StarSystemStreaming.md)
