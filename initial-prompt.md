Read `CLAUDE.md` completely before doing anything else.

We are beginning implementation of the Universe project.

Your task is to execute **Sprint 001: Universe Foundation**.

Do not attempt to build the complete game.

Work autonomously through the sprint until the acceptance criteria are actually satisfied as far as the available local environment permits.

# Objective

Establish the Unreal C++ project and implement the first foundational systems required for an astronomically large deterministic procedural universe.

At the end of this sprint I want:

1. A clean Unreal Engine 5.8.x C++ project.
2. A repository structure suitable for long-term AI-assisted development.
3. A deterministic seed hierarchy.
4. A robust hierarchical universe-coordinate system.
5. Automated tests for determinism and coordinate behavior.
6. A minimal controllable spacecraft/probe in a black-space test environment.
7. A procedurally generated test star system represented using placeholder geometry.
8. Ability to move through large logical distances without Unreal coordinate precision becoming our universe limit.
9. Written architecture documentation explaining exactly how the foundation works.

No procedural terrain yet.

No vegetation.

No weather.

No wildlife.

No buildings.

No multiplayer.

Those come later.

---

# Phase A — Inspect and bootstrap

Inspect:

* repository state
* installed Unreal Engine version
* compiler/toolchain
* available Unreal MCP capabilities
* Git state
* Git LFS availability

Use Unreal Engine 5.8.x.

If Unreal MCP is available, configure/use it where it provides real leverage, but do not make the project runtime depend upon MCP.

Create an appropriate Unreal C++ project if one does not exist.

Configure source control appropriately for Unreal.

Create/update:

```text
.gitignore
.gitattributes
README.md
CLAUDE.md
Docs/
Docs/Architecture/
Docs/ADR/
```

Use Git LFS for Unreal binary asset types where appropriate.

Do not commit generated Unreal build/cache directories.

Make sure a clean checkout can regenerate project/build artifacts.

---

# Phase B — Architecture specification

Before implementing the core coordinate system, create:

```text
Docs/Architecture/UniverseCoordinates.md
Docs/Architecture/ProceduralGeneration.md
```

and:

```text
Docs/ADR/ADR-001-universe-coordinate-system.md
Docs/ADR/ADR-002-seed-hierarchy.md
```

Specify:

* global universe coordinate representation
* universe cell size
* normalization behavior
* local coordinate representation
* serialization strategy
* precision expectations
* distance calculations
* velocity integration strategy
* coordinate-frame rebasing
* future network implications
* deterministic seed hierarchy
* stable hashing approach

Do not choose values arbitrarily without analyzing numerical consequences.

The target is an effectively enormous universe while preserving excellent local precision.

---

# Phase C — Universe coordinate implementation

Implement the global coordinate primitives.

Requirements:

* astronomical logical range
* deterministic
* normalized representation
* local precision independent of distance from universe origin
* addition/subtraction of local displacement
* boundary crossing
* relative vector calculation where representable
* distance calculation suitable for enormous scales
* serialization
* equality
* hashing where required
* debug formatting

Separate the universe coordinate representation from Unreal Actor transforms.

Actors should remain near an appropriate local origin.

Implement rebasing / local-frame logic required for the prototype.

Do NOT simply move the spacecraft to gigantic Unreal coordinates.

---

# Phase D — Deterministic generation primitives

Implement a deterministic seed hierarchy.

At minimum:

```text
Universe Seed
→ Galactic/Sector Seed
→ Star System Seed
→ Astronomical Body Seed
```

Create stable deterministic hashing/generation utilities.

The same inputs must yield the same outputs across repeated runs.

Avoid unstable identifiers such as pointer values or transient Unreal object IDs.

Generate a simple star-system descriptor containing values such as:

* stable system ID
* star descriptor
* deterministic number of planets
* orbital distance
* planet radius
* planet type enum
* deterministic display/debug name if useful

This is data generation only.

Visuals may be placeholder spheres.

---

# Phase E — Minimal space prototype

Create a minimal test level.

Requirements:

* black/space environment
* basic camera
* controllable spacecraft or probe
* forward/reverse acceleration
* rotation
* current speed debug display
* current global universe coordinate debug display
* current local Unreal coordinate display
* ability to increase debug travel speed dramatically

Create one generated test system.

Represent:

* star
* planets

using simple placeholder geometry.

Planet scales may initially be visually compressed if necessary for this sprint, but logical astronomical values must remain separate from presentation scale.

Do not build a fake architecture where visual scale permanently defines universe scale.

---

# Phase F — Large-distance traversal proof

Create a debug mode capable of accelerating the spacecraft/probe across very large logical distances.

Prove that:

* global universe coordinates change correctly
* cell boundaries can be crossed repeatedly
* Unreal local coordinates remain numerically safe
* no visible jump occurs due solely to coordinate normalization/rebasing
* generation remains deterministic when leaving and returning

It is acceptable for stars/planets to use placeholder visual scaling at this stage.

The objective is proving the coordinate architecture.

---

# Phase G — Automated tests

Create automated tests covering at least:

## Coordinate normalization

Test positive and negative cell transitions.

## Large displacement

Apply extremely large cumulative movement and verify expected position.

## Local precision

Demonstrate that small local movements remain distinguishable even at enormous global coordinates.

## Determinism

Generate the same sector/system repeatedly and compare descriptors.

## Different seeds

Verify different seeds produce different expected descriptors.

## Serialization

Round-trip universe positions and generated identifiers.

## Boundary conditions

Test exact cell edges and values immediately either side of an edge.

Run the tests.

Do not merely write them.

---

# Phase H — Diagnostics

Create useful developer/debug visualization.

At minimum display:

```text
Universe Seed
Global Cell
Local Position
Logical Velocity
Current System if applicable
Nearest astronomical object
```

Developer diagnostics should be easy to disable for normal gameplay later.

---

# Phase I — Validate

Before declaring Sprint 001 complete:

1. Compile the complete project.
2. Launch it.
3. Run automated tests.
4. Exercise spacecraft movement.
5. Exercise large-distance movement.
6. Leave and return to generated regions.
7. Check logs for warnings/errors related to our implementation.
8. Fix failures rather than documenting them as success.

If Unreal MCP allows automated editor interaction or automation testing, use it where useful.

---

# Sprint 001 Acceptance Criteria

Sprint 001 is complete only if:

* [ ] UE 5.8.x C++ project builds.
* [ ] Repository is cleanly structured.
* [ ] Universe coordinates are documented.
* [ ] Universe coordinate system is implemented.
* [ ] Seed hierarchy is documented.
* [ ] Deterministic generation utilities exist.
* [ ] At least one deterministic star system is generated.
* [ ] Placeholder star/planets can be viewed.
* [ ] A spacecraft/probe can move.
* [ ] Global logical position can cross many coordinate cells.
* [ ] Local Unreal precision remains stable.
* [ ] Returning to the same generated location reproduces the same system.
* [ ] Automated tests pass.
* [ ] Relevant architecture documentation is current.

Do not begin planet terrain implementation until these criteria are satisfied.

---

# At completion

Provide a concise engineering report containing:

## Implemented

What actually works.

## Architecture

Important decisions made.

## Validation

Exact builds/tests executed and their results.

## Files

Important modules/docs created.

## Known limitations

Anything deliberately deferred or currently imperfect.

## Recommended Sprint 002

The smallest next step toward:

**a full spherical procedurally generated landable planet.**

Do not claim anything was tested if it was not actually executed.

Begin.
