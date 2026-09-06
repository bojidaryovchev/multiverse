# ADR-005: Procedural Environment — Climate, Biomes, Content and Weather

- **Status:** Accepted
- **Date:** 2026-09-06
- **Sprint:** 004
- **Related:** [ADR-003](ADR-003-planet-topology-and-lod.md),
  [ADR-004](ADR-004-simulation-frames-and-planetary-traversal.md),
  [PlanetEnvironment.md](../Architecture/PlanetEnvironment.md),
  [ClimateSystem.md](../Architecture/ClimateSystem.md),
  [Biomes.md](../Architecture/Biomes.md),
  [Weather.md](../Architecture/Weather.md),
  [ProceduralVegetation.md](../Architecture/ProceduralVegetation.md)

---

## Problem

Sprint 003 ended with a planet that could be flown to, landed on and walked
across, and which was the same grey rock everywhere. It needed to become a place
that could be recognised — and to become that for *every* generated planet,
without any of them being authored.

The risk to avoid was named in the sprint brief: scattering independent things
everywhere with no underlying model. Random trees, random weather, random
colours. That produces a world that is different in every square metre and the
same everywhere, which is the opposite of a place.

---

## Decision 1: a one-way causal chain

**Selected: physical properties → climate → biomes → content, each layer a pure
function of the one above and the seed.**

A forest exists because the conditions support a forest. Nothing places a forest,
and nothing downstream may reach back up and decide the climate.

The alternative — deciding biomes directly from noise and deriving climate from
them for display — was rejected. It is easier to art-direct and it makes every
downstream question unanswerable: a survival system asking "how cold is it here"
would get a number invented to justify a colour.

### Consequences of the chain being pure

No stored state anywhere in it. Any part of a planet can be evaluated in
isolation, on any thread, in any order — which is what the streamer and the
worker threads need, and what a future headless server needs.

---

## Decision 2: a biome table, not a classifier function

**Selected: biomes as boxes in climate space, scored by normalised distance,
returning a weighted blend.**

**Rejected: a cascade of `if` statements.** It works until the first person adds
a biome, at which point every threshold must be re-checked by hand against every
neighbour, because the branches are ordered and the order is load-bearing.

**Rejected: a single winner.** A hard line across the planet wherever two boxes
meet looks exactly as wrong as it sounds. The blend also lets density, colour and
population all interpolate from one mechanism instead of three.

The boxes are a coarse Whittaker diagram, which is where their shape comes from
rather than from taste. Overlaps are intentional; disjoint boxes would put a hard
line at every boundary.

**Water is decided before the table is consulted.** Submersion is geometry, and
letting a temperature range have an opinion about it would eventually put a
desert on a sea floor — invisible until someone flew over the right water.

---

## Decision 3: an alien biosphere is a second table

**Selected: `EPlanetBiosphere` selects which biome table and which content
archetypes apply. The climate model, ocean, terrain and streaming are identical
between them.**

This is the concrete answer to "prove the pipeline can produce a fungal world
without rewriting the planet generator". A fungal rainforest sits in the same
conditions as a terrestrial one and grows at a comparable density, because the
conditions set the density; only the archetype differs.

A fifth of habitable worlds are generated fungal, deliberately. An alternative
biosphere that only exists behind a debug flag is a feature that will quietly
rot.

---

## Decision 4: weather is a pure function of (planet, place, time)

**Selected: no simulation, no state, evaluated on a coarse cube-sphere grid with
six-hour periods and smoothstep transitions.**

**Rejected: a simulated weather system with state.** It would need
synchronisation for multiplayer, would drift or pause when a player leaves, and
could not be queried for a time other than now.

What the chosen contract buys:

- two machines agree given only a clock
- the past and future are as cheap to query as the present
- nothing accumulates while nobody is watching

What it costs, and this is the significant one: **weather cannot respond to
anything.** A storm can be caused by the clock and by nothing else. That is the
first thing to revisit.

Ground wetness looks *back* over previous periods rather than tracking them
forward, which gives the system memory while remaining stateless — possible only
because weather is a function of time.

---

## Decision 5: content categories, not asset paths

**Selected: vegetation profiles name archetypes ("coniferous canopy tree") and
densities; the Unreal layer maps archetypes to meshes.**

Keeping asset paths out of `UniversePlanet` is what lets placement run on a
worker thread, in the standalone harness, and eventually on a server with no
content mounted at all. It is also the seam that makes the alien biosphere a data
change.

Densities are real numbers — 412 stems per hectare in temperate forest — so the
ratios between biomes are right before anything is tuned, and the streaming
budgets mean something.

---

## Decision 6: vegetation streams independently of terrain

**Selected: its own selection at a fixed patch level, activated by layer-specific
radius.**

**Rejected: hanging vegetation off terrain patch lifetime.** Terrain LOD is
screen-space error, so a patch can be two kilometres across from altitude — and
would need every tree on two square kilometres, none individually visible — while
at walking height each patch would carry a handful of trees in its own component.

Terrain decides how finely the ground is tessellated. Vegetation decides how far
away a tree is worth existing. They are different questions and coupling them
answers neither.

---

## Decision 7: animals are a population, not a list

**Selected: wildlife density is a property of a biome; individuals exist only
where somebody is looking; they are instances, not Actors.**

The behaviour is trivial and is not the point. The point is that establishing
this now is reversible and establishing the opposite is not: once anything
depends on animals having identity, an Actor per animal becomes permanent.

**Accepted limit, stated plainly: this cannot support a bird the player has named
and tamed.** The place to revisit it is when something needs it.

---

## Decision 8: sample directions avoid transcendentals

**Selected: rejection sampling in the unit cube rather than a Fibonacci spiral.**

The spiral is better distributed and is built from `sin` and `cos`. These
directions decide the ocean level, and the ocean level decides every coastline on
the planet — so a spiral would put libm's cross-platform ambiguity onto the
coastline, the risk ADR-002 records for star generation and ADR-003 deliberately
kept off terrain.

`exp` *is* used, once, for the biome softmax. That is acceptable because biome
weights feed appearance and density, never geometry; which biome wins is decided
by an exact comparison.

---

## Decision 9: two suns, because one cannot do both jobs

**Selected: one directional light drives the atmosphere's scattering and
illuminates nothing; a second lights the world and is not an atmosphere sun.**

Unreal attenuates an atmosphere sun light by atmospheric transmittance. That is
physically right and, at planetary radii, numerically hopeless: the transmittance
is evaluated in single precision against a shell whose radius is millions of
units, and at ground level it came out as zero. The measured symptom was a forest
at noon under a correctly-lit blue sky, rendered pure black — and disabling the
atmosphere lit it perfectly.

Both lights point the same way, so the sun in the sky and the shadows on the
ground agree.

**Accepted cost: the atmosphere does not tint the ground light.** No red sunsets
on terrain, only in the sky. The alternative was a black planet.

---

## Consequences

### Good

- A planet's appearance, contents and weather all follow from its physics. Two
  planets differ because their orbits and masses differ, not because they were
  seeded differently for their own sake.
- Biomes blend, so there are no lines across the world.
- An alien biosphere is a table.
- Everything deterministic is testable without an engine: 7 new test bodies,
  1.1 million assertions.
- Vegetation and wildlife are bounded by construction and the bounds are visible
  on the HUD.

### Bad / accepted costs

- **Ocean proximity is approximated by altitude.** A dry inland basin below sea
  level is called humid. The fix is a coarse precomputed distance field.
- **No atmospheric circulation, ocean currents, rain shadows or seasons.** Axial
  tilt is stored and unused.
- **Weather cannot respond to events**, only to the clock.
- **No rendered water surface.** Oceans are classified, queried and coloured, but
  there is no water plane or shader — see the Sprint 004 report.
- **No precipitation or fog visuals.** The weather field is complete and drives
  nothing visible except cloud coverage.
- **The atmosphere does not tint ground lighting**, and there is still no
  atmospheric limb from orbit (ADR-004's deferral stands).
- **Placement is one instance per grid cell**, so a very dense layer is
  represented rather than fully populated at low LOD.
- **Vegetation has no LOD or impostors.** The canopy activation radius is a
  visible boundary at 1400 m.
- **`exp` on the biome path** is the one transcendental in an otherwise
  bit-exact chain.

### Neutral

- Twelve biomes is deliberately few. The table is the architecture; its length is
  content.

## Revisit if

- A survival or ecology system needs weather to respond to events — then weather
  needs authoritative state and the multiplayer clock argument has to be made
  properly.
- Anything needs an individual animal to persist — then the population
  abstraction needs an identity escape hatch, and it should be added deliberately
  rather than by making everything an Actor.
- Coastal climate becomes visibly wrong — then the precomputed distance field.
- Real vegetation assets arrive — the archetype seam is already the place they
  attach, and no placement code should need to change.
