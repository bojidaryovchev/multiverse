Read `CLAUDE.md` completely before doing anything else.

Then inspect the repository and verify the outputs of Sprint 001, Sprint 002 and Sprint 003.

We are beginning:

# Sprint 004 — Living Planet Foundation

The objective of this sprint is to transform the technically traversable procedural planet into a **visibly alive procedural world**.

At the end of this sprint, the player should be able to:

```text
fly toward a procedural planet
↓
see oceans / continents
↓
enter atmosphere
↓
see clouds
↓
see recognizable climate regions
↓
land in a biome
↓
exit spacecraft
↓
walk through procedural vegetation
↓
see grass / trees / rocks / plants
↓
experience wind / rain / changing weather
↓
see basic wildlife such as birds
↓
move to a completely different region
↓
find a different environment
```

The world does not need production-quality AAA art.

The purpose of this sprint is to establish the **procedural environmental architecture** that can eventually generate enormous numbers of coherent planets.

This sprint is NOT about adding large quantities of handcrafted content.

The planet should derive its environmental identity from:

```text
Planet Seed
+
Planet Physical Properties
+
Location
+
Climate Fields
+
Biome Rules
+
Procedural Content Rules
```

---

# 0. PRECONDITION — VERIFY PREVIOUS SPRINTS

Before beginning:

Verify Sprint 001:

* universe-coordinate system works
* deterministic generation works
* tests pass

Verify Sprint 002:

* spherical procedural terrain works
* cube-sphere topology works
* terrain LOD works
* streaming works
* terrain continuity works
* tests pass

Verify Sprint 003:

* continuous space → planet → ground traversal works
* player can land
* player can exit spacecraft
* player can walk on spherical terrain
* planetary gravity works
* local simulation frame works
* terrain collision works
* origin rebasing works
* return to space works
* tests pass

If defects directly block environmental systems, fix them first.

Do not rewrite working foundational systems unnecessarily.

---

# 1. SPRINT OBJECTIVE

Build the first procedural environmental pipeline.

Conceptually:

```text
Planet Descriptor
      ↓
Planet Physical Properties
      ↓
Global Climate Model
      ↓
Local Environmental Fields
      ↓
Biome Classification
      ↓
Surface Material Rules
      ↓
Procedural Vegetation
      ↓
Weather
      ↓
Basic Wildlife
```

Environmental generation must be:

* deterministic where appropriate
* data-driven
* extensible
* streaming-friendly
* bounded in runtime cost
* independent of total planet size
* compatible with future multiplayer
* compatible with future persistence

---

# 2. OUT OF SCOPE

Do NOT implement:

* civilization
* cities
* NPC societies
* complex ecology
* predator/prey simulation
* advanced evolution
* farming
* harvesting economy
* inventory
* crafting
* combat
* advanced destruction
* terrain modification
* player building
* persistence deltas
* MMO backend
* procedural alien species generation at large scale
* realistic global atmospheric fluid simulation
* realistic ocean simulation
* full hydrological simulation
* plate tectonics simulation
* production-quality volumetric weather system unless trivial to integrate

This sprint should create the architecture and first convincing environmental result.

---

# 3. CORE ENVIRONMENTAL PRINCIPLE

Do NOT randomly scatter independent things everywhere.

The planet must feel internally coherent.

For example:

```text
high latitude
+
low temperature
+
moderate humidity
+
high altitude

→ alpine tundra
```

Another location:

```text
warm
+
very wet
+
low altitude

→ rainforest
```

Another:

```text
warm
+
very dry

→ desert
```

Environmental content should derive from environmental conditions.

Do NOT implement:

```text
random tree
random grass
random rock
random weather
```

with no underlying model.

---

# 4. PLANET ENVIRONMENT DESCRIPTOR

Extend the planet descriptor with environmental/physical parameters.

Potential properties include:

```text
PlanetRadius
Mass
SurfaceGravity
RotationPeriod
AxialTilt
OrbitalDistance
StarLuminosity
MeanSurfaceTemperature
AtmosphereDensity
AtmosphereCompositionCategory
OceanCoverageTarget
WaterLevel
HumidityBias
VolcanicActivity
VegetationPotential
BiosphereType
```

Do not blindly add every field above.

Design a clean minimal model.

The important part is that planet-wide environmental conditions exist independently of any one terrain patch.

Keep the data deterministic.

---

# 5. STAR ENERGY / PLANET TEMPERATURE MODEL

Create a simplified temperature model.

We do NOT need astrophysical perfection.

We DO want coherent relationships.

Temperature may depend upon:

```text
star energy
+
orbital distance
+
planet atmosphere
+
latitude
+
altitude
+
seasonal factors later
```

At minimum expose:

```cpp
GetTemperatureAtSurfaceLocation(...)
```

or equivalent.

The implementation must allow future improvements without rewriting biome logic.

---

# 6. GLOBAL CLIMATE FIELDS

Implement environmental fields queryable at any surface location.

At minimum:

```text
Temperature
Moisture / Humidity
Elevation
Slope
Latitude-equivalent
Ocean Proximity if available
```

Potential future fields:

```text
continentalness
rainfall
wind exposure
soil type
geothermal activity
sun exposure
```

Do not overbuild now.

Environmental fields must be deterministic.

---

# 7. CLIMATE QUERY API

Provide a semantic API.

Conceptually:

```cpp
FClimateSample SampleClimate(
    PlanetId,
    SurfaceDirection
);
```

Potential output:

```text
Temperature
Humidity
Elevation
Slope
Rainfall
BiomeCandidate
```

Gameplay and PCG systems should consume this API rather than reproducing climate math themselves.

---

# 8. LATITUDE WITHOUT FLAT-MAP ASSUMPTIONS

Define latitude-equivalent values from the planet's rotation axis and surface direction.

Do not derive latitude from cube-face coordinates.

Conceptually:

```text
latitude =
asin(dot(surfaceDirection, rotationAxis))
```

or equivalent.

Make the rotation axis configurable.

This allows future axial tilt and seasonal systems.

---

# 9. TEMPERATURE FIELD

Temperature should vary smoothly and plausibly.

Use inputs such as:

```text
planet baseline temperature
+
latitude
+
elevation
+
procedural regional variation
```

Example conceptual relationship:

```text
equator
→ warmer

poles
→ colder

high mountains
→ colder
```

Avoid sharp random transitions.

---

# 10. HUMIDITY / MOISTURE FIELD

Implement a deterministic moisture field.

Inputs may include:

```text
planet humidity bias
+
large-scale procedural moisture patterns
+
distance to ocean later
+
elevation
+
wind effects later
```

For Sprint 004, an approximation is acceptable.

The field should create large coherent regions rather than TV-static noise.

---

# 11. OCEANS

Add an initial planetary ocean system.

The planet should support:

```text
reference terrain radius
+
ocean level
```

Any terrain below ocean level is ocean-covered.

Requirements:

* ocean level deterministic per planet
* ocean appears spherical
* no flat infinite plane
* works from space
* works at surface
* coastline derives naturally from terrain
* player can visually identify land vs water from orbit
* rendering works across planetary coordinate/rebase system

---

# 12. OCEAN REPRESENTATION

Evaluate a practical representation.

Potential options:

* separate sphere slightly above reference radius
* shell-based ocean rendering
* local ocean patches near player
* hybrid distant sphere + local detailed water

Architecture should support:

```text
far distance
→ cheap ocean representation

near surface
→ higher-detail water
```

Do not make the entire planet use expensive local water simulation.

---

# 13. WATER MATERIAL

Create an initial water material.

It should provide enough to communicate:

* water surface
* reflections/specular response
* depth coloration if practical
* wave normal animation
* transparency/refraction if practical

Do NOT spend excessive time on production ocean rendering.

The sprint goal is environmental systems.

---

# 14. UNDERWATER DETECTION

Implement basic logic:

```text
PlayerAltitudeRelativeToOcean < 0
→ underwater
```

or appropriate terrain/water query.

Expose:

```text
IsUnderwater
WaterDepth
```

This will support later swimming, underwater ecosystems, audio, fog and gameplay.

No full swimming system required this sprint.

---

# 15. BIOME SYSTEM

Create a deterministic biome classification system.

Start with a manageable set.

For example:

```text
Ocean
Coast
Grassland
TemperateForest
Rainforest
Desert
Tundra
Snow
Mountain
Wetland
```

Exact names may differ.

Do not implement fifty biomes.

The system should make adding new biomes data-driven.

---

# 16. BIOME DEFINITION DATA

Avoid giant hardcoded switch statements.

Create a data-driven biome definition.

A biome may define:

```text
Name
TemperatureRange
HumidityRange
ElevationRange
SlopeRange

GroundMaterial
VegetationProfile
RockProfile
WeatherProfile
WildlifeProfile
```

Use Unreal data assets/configuration where appropriate.

The procedural algorithm decides the biome.

The biome data decides its content.

---

# 17. BIOME TRANSITIONS

Do not create obvious hard boundaries like:

```text
forest | desert
```

unless naturally intended.

Support blending.

A climate sample should potentially provide biome weights.

Example:

```text
TemperateForest: 0.72
Grassland: 0.28
```

This can influence:

* materials
* vegetation density
* species selection

Start simple if necessary, but architecture should support smooth transitions.

---

# 18. TERRAIN MATERIAL SYSTEM

Upgrade the placeholder Sprint 002 material.

Terrain appearance should derive from environmental data.

Potential material layers:

```text
rock
soil
sand
grass
snow
wet ground
```

Inputs may include:

```text
biome
elevation
slope
temperature
humidity
water proximity
```

Avoid painting terrain by manually authored masks.

The planet must regenerate appearance procedurally.

---

# 19. SLOPE-BASED MATERIALS

Steep slopes should naturally expose rock.

Example:

```text
high slope
→ rock

flat + humid
→ vegetation ground

flat + dry
→ sand / dry soil
```

Provide reasonable blending.

---

# 20. SNOW

Add simple climate-driven snow coverage.

Snow presence may depend upon:

```text
temperature
+
elevation
```

Snow should appear naturally on:

* cold regions
* high mountains

This should be procedural.

No snow accumulation simulation required.

---

# 21. COASTS

Create distinct coastline behavior.

Potential features:

```text
sand
rocky coastline
wet ground
reduced vegetation
```

Use altitude relative to ocean level plus slope/biome.

Do not manually define coastline geometry.

---

# 22. PROCEDURAL CONTENT PIPELINE

Environmental objects must stream with terrain.

Conceptually:

```text
Active Terrain Patch
      ↓
Climate / Biome samples
      ↓
Procedural content rules
      ↓
trees / plants / rocks / grass
```

When terrain patch unloads:

```text
associated local environmental content
→ unload / pool
```

Do not generate vegetation for the entire planet.

---

# 23. UNREAL PCG

Use Unreal PCG where it materially helps.

The relationship should remain:

```text
OUR SYSTEM

Planet / terrain / climate / biome data

        ↓

PCG INPUT

        ↓

UNREAL PCG

local object placement
```

Do not allow PCG graphs to become the authoritative source of planetary climate or geography.

Planet identity must remain in our deterministic data systems.

---

# 24. PCG DETERMINISM

Where possible, PCG results should be deterministic from:

```text
Planet Seed
Patch ID
Biome
Content Layer
Generation Version
```

Repeated visits should not arbitrarily rearrange trees.

Test this where practical.

If Unreal PCG itself has determinism limitations, document them and introduce stable seeds.

---

# 25. VEGETATION PROFILE

Create a data representation for biome vegetation.

For example:

```text
TreeSpecies[]
ShrubSpecies[]
GroundPlants[]
GrassTypes[]

TreeDensity
ShrubDensity
GrassDensity

ScaleRanges
SlopeLimits
AltitudeLimits
```

Do not couple biome classification directly to specific asset paths throughout C++.

---

# 26. FIRST VEGETATION SET

We only need enough assets to validate the architecture.

Initial Earth-like content can include:

```text
2–4 tree variants
1–3 bushes
1–2 grass types
some flowers/plants
several rocks
```

Use placeholder/available assets if necessary.

Variety can come later.

The architecture must support far more.

---

# 27. TREES

Implement streamed procedural trees.

Requirements:

* terrain aligned
* biome constrained
* slope constrained
* deterministic placement where practical
* scalable density
* efficient rendering
* removed when region unloads
* not individual heavyweight Actors unless gameplay requires it

Prefer instancing / efficient foliage systems.

---

# 28. GRASS

Grass should be much denser than trees but cheaper.

Use appropriate Unreal foliage/PCG/material techniques.

Avoid spawning thousands of Actors.

Grass must not dominate CPU cost.

---

# 29. ROCKS

Procedurally scatter rocks according to:

```text
biome
slope
elevation
terrain characteristics
```

Rock placement should help break up terrain repetition.

---

# 30. FUTURE ALIEN FLORA SUPPORT

Do NOT deeply encode:

```text
Tree = Earth tree
```

into architecture.

Vegetation profiles should later support:

```text
GiantMushroom
CrystalGrowth
FungalTower
AlienShrub
BioluminescentPlant
```

The biome system should care about content categories/rules, not Earth taxonomy.

---

# 31. CONTENT STREAMING BUDGET

Introduce configurable environmental content budgets.

Potential metrics:

```text
maximum active tree instances
maximum shrub instances
maximum grass density
maximum PCG generation jobs
environment generation distance
```

The planet should remain computationally bounded.

---

# 32. ENVIRONMENT LOD

Environmental content needs LOD too.

Conceptually:

```text
far planet
→ no individual vegetation

high altitude
→ broad biome colors only

lower altitude
→ forest canopy representation

near ground
→ individual trees

very near player
→ high-detail vegetation
```

Do not require individual trees to exist while the player is in orbit.

---

# 33. LARGE-SCALE FOREST VISIBILITY

Avoid the problem:

```text
from orbit:
green ground

descend:

trees suddenly appear at 2 km
```

Investigate cheap intermediate representations such as:

* canopy textures
* biome albedo
* impostors
* HLOD
* low-detail foliage layers

Do not overbuild.

But create a clear strategy for environmental continuity across scale.

---

# 34. DAY / NIGHT CYCLE

Implement a basic planetary day/night system.

The planet should have:

```text
RotationPeriod
RotationAxis
```

and rotate relative to its star or simulate equivalent lighting progression.

Requirements:

* deterministic starting orientation
* configurable rotation speed
* day side / night side
* sunrise / sunset
* local sky responds appropriately

For debugging, time acceleration should be available.

---

# 35. STAR LIGHTING

The active system star should provide the main directional lighting relationship.

Do not simply use a fixed global sun direction disconnected from astronomy.

Conceptually:

```text
star universe/system position
-
planet position
=
sun direction
```

Use an appropriate approximation if required for rendering.

---

# 36. MINIMAL ATMOSPHERIC VISUALS

Sprint 003 introduced the logical atmosphere.

Sprint 004 should provide convincing visuals.

Implement a practical atmospheric scattering solution using Unreal capabilities or a custom solution where appropriate.

Requirements:

* planet atmospheric glow visible from space
* sky color near surface
* horizon scattering
* day/night variation
* transition remains continuous

Do not spend the whole sprint on scientifically exact scattering.

---

# 37. ATMOSPHERE PARAMETERS

Atmospheric appearance should be driven by planet data where possible.

Potential parameters:

```text
density
color/scattering profile
height
haze
```

Architecture should eventually support:

```text
Earth-like blue atmosphere
thick yellow atmosphere
thin red atmosphere
alien atmospheres
```

Do not assume every habitable planet has identical blue skies.

---

# 38. CLOUD FOUNDATION

Add clouds.

The implementation can use Unreal's current volumetric cloud systems or another appropriate approach.

Requirements:

* visible from surface
* visible from space
* appropriate planetary scale
* move over time
* configurable coverage
* compatible with weather

Clouds should not be painted into a static sky texture.

---

# 39. GLOBAL CLOUD PARAMETERS

Planet descriptor/environment should define:

```text
CloudCoverageBias
CloudDensity
CloudAltitudeRange
CloudSpeed
StormPotential
```

Exact structure can differ.

These should influence local weather.

---

# 40. WEATHER SYSTEM

Create the first lightweight weather architecture.

Weather states may include:

```text
Clear
Cloudy
Rain
Storm
Fog
Snow
```

Not every biome needs every state.

Weather should depend on local climate.

For example:

```text
desert
→ rain rare

rainforest
→ rain common

cold biome
→ snow possible
```

---

# 41. WEATHER REGIONS

Do not make the entire planet switch from:

```text
CLEAR
```

to:

```text
RAIN
```

at once.

Implement regional weather.

A simple coarse weather grid/field over the planet is sufficient.

Weather regions may operate at much lower resolution than terrain.

Conceptually:

```text
Planet Weather Cells
      ↓
local weather state
      ↓
interpolated nearby conditions
```

---

# 42. WEATHER DETERMINISM VS DYNAMICS

Terrain and biome identity must be deterministic.

Weather can evolve with simulation time.

Separate:

```text
planet identity
```

from:

```text
current dynamic weather state
```

Future multiplayer will require authoritative weather timing.

For now, define the architecture cleanly.

---

# 43. WIND

Add a local wind field.

Wind may influence:

* grass
* trees
* particles
* rain
* clouds eventually
* birds

A simple direction + strength model is enough.

Expose:

```cpp
GetWindAtLocation(...)
```

or equivalent.

---

# 44. RAIN

Implement a simple local rain effect.

Requirements:

* only active around player where relevant
* driven by weather state
* does not require rendering rain across the entire planet
* responds to wind if practical
* can reduce visibility / alter atmosphere subtly

No hydrological accumulation required.

---

# 45. SNOW WEATHER

If local temperature allows:

```text
precipitation
+
temperature below threshold
→ snow
```

A basic particle/visual solution is enough.

No accumulation required.

---

# 46. FOG

Weather/local climate may influence fog.

Examples:

```text
humid forest morning
storm
coast
cold regions
```

Use restrained implementation.

Fog should not simply be random.

---

# 47. WEATHER DEBUG CONTROLS

Provide debug controls:

```text
ForceClear
ForceRain
ForceStorm
ForceSnow
ForceFog
ResumeProceduralWeather
```

Also expose:

```text
temperature
humidity
weather cell
current weather
wind
```

This is essential for iteration.

---

# 48. WILDLIFE FOUNDATION

Add extremely simple wildlife.

Start with one category:

# Birds

The goal is NOT sophisticated AI.

The goal is:

```text
planet feels alive
+
entity streaming architecture is exercised
```

---

# 49. BIRD SPAWNING

Bird spawning should depend upon:

```text
biome
weather
time of day
terrain
vegetation
```

at least at a simple level.

Example:

```text
forest / grassland
→ birds possible

storm
→ fewer flying birds

deep ocean
→ different/fewer birds
```

---

# 50. BIRD BEHAVIOR

Simple behaviors:

```text
spawn
↓
select local flight target
↓
fly
↓
avoid terrain
↓
wander
↓
occasionally perch / circle if easy
↓
react to player
↓
leave / despawn
```

Do not create elaborate behavior trees unless required.

---

# 51. BIRD MOVEMENT

Bird movement can be lightweight and approximate.

Requirements:

* no obvious flying through terrain
* no huge per-bird physics cost
* no simulation across entire planet
* only nearby meaningful entities exist

Use pooled/lightweight agents if appropriate.

---

# 52. WILDLIFE STREAMING

Wildlife must stream separately from terrain.

Conceptually:

```text
no players nearby
→ no actual bird Actors

player enters habitat
→ representative local population spawns

player leaves
→ birds despawn / return to pool
```

Do not simulate millions of animals individually.

This principle is essential for the future game.

---

# 53. ECOLOGICAL ABSTRACTION

Introduce, at most, a very small abstraction for future wildlife population simulation.

For example:

```text
BiomePopulationProfile
BirdDensity
```

Do NOT build ecological simulation.

Just avoid architecture that assumes every animal always exists as an Actor.

---

# 54. LOCAL ENVIRONMENT QUERY API

Create a central environmental query interface.

Gameplay systems should be able to ask something conceptually like:

```cpp
FEnvironmentSample Environment =
    PlanetEnvironment->Sample(Location);
```

Potential output:

```text
Biome
Temperature
Humidity
Elevation
Slope
IsOcean
WaterDepth
Weather
Wind
TimeOfDay
```

This API will later support:

* animals
* construction
* survival gameplay
* resources
* NPCs
* sound
* effects
* AI

Keep it clean.

---

# 55. ENVIRONMENT GENERATION VERSION

Just as terrain has a generation version, environmental generation must eventually be versioned.

Add something conceptually like:

```text
EnvironmentGeneratorVersion
```

Changing biome logic later could otherwise transform forests into deserts around persistent player settlements.

Do not build migration infrastructure yet.

Make versioning explicit.

---

# 56. STREAMING INTEGRATION

Environmental content should follow active planet patches but not necessarily use identical LOD boundaries.

Design clear ownership.

For example:

```text
Terrain Patch Active
↓
Environment Manager evaluates relevance
↓
Biome/content jobs scheduled
↓
vegetation/wildlife/weather local representation activated
```

Avoid tightly coupling everything to terrain mesh lifecycle.

---

# 57. ASYNCHRONOUS GENERATION

Large vegetation placement calculations should not freeze the game thread.

As with terrain:

```text
worker
→ pure placement calculations

game thread
→ safe Unreal object/instance updates
```

Respect UObject/thread rules.

Avoid uncontrolled PCG/task queues during fast traversal.

---

# 58. HIGH-SPEED TRAVEL

Remember the player may approach/leave planets rapidly.

When ship speed is high:

```text
do NOT generate individual grass / birds
```

Environmental detail should activate based upon:

* altitude
* distance
* speed
* predicted path
* relevance

If player immediately leaves:

```text
obsolete generation work
→ cancel/discard
```

---

# 59. ENVIRONMENT PRIORITY SYSTEM

Suggested priority:

```text
1. terrain/collision
2. terrain material
3. major water/ocean visibility
4. large vegetation
5. nearby vegetation
6. weather
7. wildlife
8. tiny ground clutter
```

Do not let cosmetic ground clutter block important environment generation.

---

# 60. PLAYER MOVEMENT THROUGH VEGETATION

Ensure trees/rocks do not break character movement.

Requirements:

* appropriate collision on large objects
* grass/small plants no expensive collision
* ship landing area does not become impossible due to dense generated trees

For now, landing may search for or use a sufficiently open location.

Advanced vegetation destruction can come later.

---

# 61. LANDING BIOME VALIDATION

Test landing in:

```text
forest
grassland
desert
mountain
coast
snow/tundra
```

where available.

Ensure:

* collision
* vegetation
* materials
* weather
* character movement

remain functional.

---

# 62. MULTIPLE REGIONS OF SAME PLANET

Use developer teleport tools to sample many locations.

The planet should not feel globally identical.

Validate:

```text
equatorial region
mid-latitude region
polar region
lowland
mountain
coast
```

The environmental system should generate meaningfully different outcomes.

---

# 63. MULTIPLE PLANETS

If Sprint 001 system generation supports multiple planets, generate at least a few different environmental descriptors.

For example:

```text
Planet A
warm / wet

Planet B
cold / dry

Planet C
temperate / ocean-heavy
```

It is NOT necessary to fully polish all of them.

We do need to validate that environmental generation is driven by planet data rather than one global Earth preset.

---

# 64. ALIEN PLANET TEST

Create at least one deliberately non-Earth-like environment profile to validate extensibility.

Example:

```text
PlanetSeed X

VegetationProfile =
FungalDominant
```

Even if assets are crude, demonstrate that the content pipeline can produce something like:

```text
giant mushrooms instead of trees
```

without rewriting the planet generator.

This is an architectural validation, not a content sprint.

---

# 65. AUDIO FOUNDATION

If straightforward, add minimal environmental audio hooks.

Examples:

```text
wind
rain
forest ambience
coast/ocean ambience
birds
```

Use biome/weather-driven selection.

Do not spend significant sprint time on sound design.

---

# 66. DEBUG ENVIRONMENT OVERLAY

Create a debug visualization that can display environmental fields over the planet/surface.

Useful modes:

```text
Biome
Temperature
Humidity
Elevation
Slope
Ocean
VegetationDensity
WeatherCell
```

False-color visualization is acceptable.

This is important for diagnosing procedural generation.

---

# 67. DEBUG HUD

Display:

```text
Planet ID
Planet Seed
Environment Version

Current Biome
Temperature
Humidity
Elevation
Slope

Ocean / Land
Water Depth

Weather
Wind

Active Trees
Active Vegetation Instances
Active Wildlife
Environment Tasks
```

Make it toggleable.

---

# 68. CLIMATE MAP DEBUG VIEW

Provide a way to inspect the entire planet at coarse resolution.

For example:

```text
temperature map
humidity map
biome map
```

This may be:

* generated texture
* developer visualization
* debug sphere overlay

Do not create a polished UI.

This is for validating global environmental coherence.

---

# 69. PERFORMANCE BUDGETS

Measure:

```text
active terrain patches
vegetation instances
tree instances
rock instances
wildlife entities
PCG jobs
environment jobs
frame time
memory
```

Moving indefinitely should not cause these counts to grow forever.

---

# 70. STRESS TEST — SURFACE TRAVERSAL

Create a repeatable scenario:

```text
land in forest
↓
travel several kilometers
↓
cross biome transition
↓
enter rain
↓
move into grassland
↓
ascend
↓
fly to coast
↓
land
↓
walk
↓
take off
```

Observe:

* streaming
* memory
* generation backlog
* visual holes
* deterministic content
* weather transitions
* wildlife lifecycle

---

# 71. STRESS TEST — RAPID PLANET APPROACH

Repeat:

```text
space
↓
high-speed approach
↓
low altitude
↓
leave rapidly
↓
approach elsewhere
```

Ensure:

* vegetation jobs do not accumulate
* PCG does not explode
* wildlife is not stranded
* weather/local systems clean up correctly
* memory remains bounded

---

# 72. STRESS TEST — DAY / NIGHT

Accelerate planetary time.

Observe multiple transitions:

```text
day
↓
sunset
↓
night
↓
sunrise
```

Ensure:

* lighting stable
* atmospheric visuals stable
* no massive resource accumulation
* weather remains functional

---

# 73. DETERMINISM TESTS

For fixed planet seed/location/environment version, verify deterministic:

* climate fields
* biome classification
* biome weights
* terrain material inputs
* vegetation placement seeds
* ocean level
* environmental descriptor

Dynamic weather does not need to remain permanently static.

But given identical authoritative simulation state/time, future architecture should allow reproducibility.

---

# 74. BIOME TESTS

Create tests for known synthetic climate inputs.

For example:

```text
hot + dry
→ desert-family biome

cold + dry
→ tundra/snow-family biome

warm + wet
→ forest/rainforest-family biome
```

Do not make tests overly dependent on exact tuning values if blending is used.

Test invariants.

---

# 75. OCEAN TESTS

Verify:

* terrain below ocean level reports water
* terrain above ocean level reports land
* water depth calculation works
* coastline threshold works
* multiple planet radii work

---

# 76. ENVIRONMENT QUERY TESTS

At many deterministic locations:

```text
SampleEnvironment(location)
```

must return valid values.

Check:

* no NaNs
* humidity bounded appropriately
* valid temperature
* valid biome
* sensible water state
* valid wind/weather values

---

# 77. CUBE-FACE ENVIRONMENT CONTINUITY

Environmental fields must not reveal cube topology.

Test climate sampling across:

* face edges
* face corners

There should be no artificial climate discontinuity caused by face-local coordinates.

Use direction-based global sampling.

---

# 78. TERRAIN + BIOME COHERENCE

Biome classification should use actual terrain/environment data.

Example:

```text
high steep mountain
```

should not casually generate:

```text
dense flat swamp
```

unless explicitly intended.

Create constraints.

---

# 79. WEATHER TRANSITION QUALITY

Weather regions should blend smoothly enough that the player does not cross an invisible line and instantly see:

```text
clear → hurricane
```

unless intentionally designed.

Use transition/interpolation zones.

---

# 80. WIND + VEGETATION

Where practical, connect local wind to vegetation animation.

At minimum:

```text
more wind
→ more visible vegetation motion
```

Do this efficiently.

Do not simulate individual branches physically.

---

# 81. RAIN + OCEAN / GROUND

If cheap, add simple visual responses:

```text
wetness parameter
```

during rain.

No puddle simulation required.

The architecture can expose:

```text
SurfaceWetness
```

for materials.

---

# 82. NIGHT VISIBILITY

Ensure nighttime is usable for testing.

Do not make pitch-black gameplay block traversal.

Temporary exposure/moon/ambient settings are acceptable.

Production lighting can come later.

---

# 83. SKY / STARS

When on the night side of a planet:

* star field should remain conceptually consistent with space
* nearby system star direction should make sense

Do not use an unrelated static fantasy sky if avoidable.

---

# 84. SAVE NOTHING YET

This sprint still does NOT require persistent world modifications.

Procedural environment should reconstruct from:

```text
seed
+
planet descriptor
+
environment generation version
```

Player changes are Sprint 005.

---

# 85. CODE ORGANIZATION

Inspect existing modules first.

Potential conceptual areas:

```text
PlanetEnvironment/
    Climate
    Biomes
    Ocean
    Vegetation
    Weather
    Wildlife
```

Do NOT create needless modules/classes.

Prefer clean dependencies:

```text
Planet Data
    ↓
Climate
    ↓
Biome
    ↓
Environment Queries
    ↓
Runtime Representation
```

Not:

```text
Tree Actor
↓
queries terrain actor
↓
queries weather actor
↓
random global singleton
```

---

# 86. DATA VS REPRESENTATION

Keep separate:

```text
Climate data
Biome data
Weather state
Vegetation placement data
```

from:

```text
Unreal Actors
Instances
Materials
Particles
Cloud components
```

This distinction will matter enormously for future multiplayer/server simulation.

---

# 87. SERVER COMPATIBILITY

We are still single-player.

However, pure environmental queries should be usable in a future headless server where possible.

Do not make core climate/biome identity depend entirely on rendering components.

The server may eventually need to know:

```text
Player is in desert
Current temperature
Current weather
Water depth
```

without rendering the planet.

---

# 88. ARCHITECTURE DOCUMENTATION

Create/update at least:

```text
Docs/Architecture/PlanetEnvironment.md
Docs/Architecture/ClimateSystem.md
Docs/Architecture/Biomes.md
Docs/Architecture/Weather.md
Docs/Architecture/ProceduralVegetation.md
Docs/Architecture/WildlifeStreaming.md
```

Add ADRs where decisions are significant.

Document:

* environmental field definitions
* climate inputs
* biome classification
* biome blending
* water architecture
* PCG integration
* deterministic seeding
* environment generation version
* weather model
* wildlife abstraction
* streaming lifecycle
* performance budgets

---

# 89. MANUAL ACCEPTANCE TEST — ORBIT

Start in space.

Approach the planet.

Verify:

* visible oceans
* visible land
* atmospheric appearance
* cloud presence
* day/night lighting
* broad environmental differences visible at useful scales

---

# 90. MANUAL ACCEPTANCE TEST — FOREST

Land in a forest biome.

Verify:

* terrain material
* trees
* grass
* rocks
* smaller plants
* wind
* birds where appropriate
* weather can occur
* character can move normally

---

# 91. MANUAL ACCEPTANCE TEST — DESERT

Navigate to a desert/dry region.

Verify:

* visibly different terrain
* minimal inappropriate vegetation
* different content profile
* climate query reports appropriate values

---

# 92. MANUAL ACCEPTANCE TEST — COLD REGION

Navigate to a polar/high-altitude cold region.

Verify:

* snow/cold terrain treatment
* appropriate vegetation reduction/change
* snow weather possible if implemented
* temperature consistent

---

# 93. MANUAL ACCEPTANCE TEST — COAST

Navigate to coastline.

Verify:

* ocean
* shoreline
* land/ocean classification
* appropriate terrain material
* water depth works
* environment changes plausibly

---

# 94. MANUAL ACCEPTANCE TEST — WEATHER

Remain in appropriate region and:

* allow procedural weather transition

and/or use debug weather controls.

Verify:

```text
clear
cloudy
rain
storm
snow/fog where applicable
```

Transitions should not catastrophically affect performance.

---

# 95. MANUAL ACCEPTANCE TEST — ALIEN CONTENT

Load the alien/fungal test planet/profile.

Verify the same system can replace:

```text
tree-dominant forest
```

with:

```text
mushroom/fungal-dominant environment
```

using data/content rules rather than custom planet code.

---

# 96. SPRINT 004 ACCEPTANCE CRITERIA

Sprint 004 is complete only if:

## Climate

* [ ] Planet environmental descriptor exists.
* [ ] Temperature field exists.
* [ ] Moisture/humidity field exists.
* [ ] Elevation/slope integrate into environment queries.
* [ ] Climate fields are deterministic.
* [ ] Climate sampling works across cube-face boundaries.
* [ ] Different planet parameters produce different climate outcomes.

## Oceans

* [ ] Ocean level exists.
* [ ] Planetary water is spherical.
* [ ] Oceans visible from space.
* [ ] Oceans visible near surface.
* [ ] Land/water classification works.
* [ ] Water depth query works.
* [ ] Coasts derive from terrain/ocean level.

## Biomes

* [ ] Data-driven biome definitions exist.
* [ ] Several distinct biomes work.
* [ ] Biome classification derives from climate.
* [ ] Biome transitions are not severely discontinuous.
* [ ] Biome system supports future alien biome content.

## Terrain appearance

* [ ] Terrain material responds to biome/environment.
* [ ] Slope affects terrain material.
* [ ] Snow/cold treatment works.
* [ ] Coast treatment works.
* [ ] Terrain does not reveal cube-face seams.

## Vegetation

* [ ] Trees generate procedurally.
* [ ] Grass generates procedurally.
* [ ] Rocks generate procedurally.
* [ ] Vegetation depends on biome.
* [ ] Environmental content streams.
* [ ] Vegetation does not exist at full detail across entire planet.
* [ ] Runtime counts remain bounded.
* [ ] Placement is deterministic where intended.

## Atmosphere / Sky

* [ ] Planet atmosphere has convincing basic visuals.
* [ ] Atmosphere works from space and surface.
* [ ] Day/night relationship works.
* [ ] Lighting corresponds to system star direction.
* [ ] Clouds exist.

## Weather

* [ ] Regional weather architecture exists.
* [ ] At least clear/cloud/rain work.
* [ ] Other weather such as storm/snow/fog implemented where practical.
* [ ] Weather derives from climate/biome rules.
* [ ] Weather transitions are reasonably smooth.
* [ ] Wind field exists.
* [ ] Debug weather controls exist.

## Wildlife

* [ ] At least one basic wildlife type exists.
* [ ] Birds or equivalent can spawn.
* [ ] Wildlife uses lightweight behavior.
* [ ] Wildlife streams around players.
* [ ] No planet-wide individual wildlife simulation occurs.

## Environment API

* [ ] Semantic environment query API exists.
* [ ] Biome query works.
* [ ] temperature query works.
* [ ] humidity query works.
* [ ] water query works.
* [ ] weather query works.
* [ ] wind query works.

## Performance

* [ ] Environmental content remains bounded while traveling.
* [ ] Fast space/surface transitions do not create infinite job backlog.
* [ ] Old vegetation/wildlife/environment representations are released.
* [ ] No obvious uncontrolled Actor explosion occurs.
* [ ] Measurements are documented.

## Tests

* [ ] Climate tests pass.
* [ ] Biome tests pass.
* [ ] Environment query tests pass.
* [ ] Ocean tests pass.
* [ ] Determinism tests pass.
* [ ] Cube-face continuity tests pass.
* [ ] Previous Sprint 001–003 tests still pass.
* [ ] Full project builds successfully.

## Manual validation

* [ ] Planet is visibly identifiable as a living world from space.
* [ ] Player can land in multiple distinct biomes.
* [ ] Forest environment works.
* [ ] dry/desert environment works.
* [ ] cold environment works.
* [ ] coastline/ocean works.
* [ ] weather works.
* [ ] wildlife is visible.
* [ ] alien/fungal environmental profile proves extensibility.
* [ ] player can take off and return to space.

---

# 97. DO NOT BEGIN SPRINT 005

Do not proceed into construction/persistent modification yet.

Sprint 005 will be:

# WORLD INTERACTION + PERSISTENCE

It will add the first meaningful persistent player impact:

```text
place structure
↓
save world delta
↓
leave planet
↓
quit game
↓
restart
↓
return
↓
structure still exists
```

Likely Sprint 005 areas:

```text
persistent entity IDs
world delta model
local persistence abstraction
SQLite or equivalent local implementation
building placement
destroyed procedural-object persistence
region delta loading
generation-version compatibility
save/reload tests
```

Do NOT implement Sprint 005 during this sprint.

---

# 98. COMPLETION REPORT

At completion provide:

## Implemented

Exactly what genuinely works.

## Planet Environment Architecture

Explain:

* planet environmental properties
* climate
* biomes
* ocean
* terrain material
* vegetation
* atmosphere
* clouds
* weather
* wildlife
* environment query API

## Procedural Rules

Explain what is:

* deterministic
* dynamic
* data-driven
* streamed

## Validation

List exact:

* builds
* automated tests
* manual tests
* stress tests

that were actually run.

## Performance

Report measured:

* active vegetation counts
* tree counts
* wildlife counts
* environment generation times
* frame times where available
* environment task queue
* memory behavior

Never fabricate measurements.

## Known Limitations

Be explicit.

## Technical Debt

Identify temporary implementations.

## Recommended Sprint 005

Define the smallest next sprint necessary to prove:

# PERSISTENT PLAYER MODIFICATION OF A PROCEDURAL WORLD

Do not implement it yet.

---

# FINAL PRINCIPLE

The goal is not to place lots of objects.

The goal is to create a system where a planet's physical properties cause climate, climate causes biomes, and biomes cause recognizable environments.

The player should begin to feel:

> "This is not a terrain generator. This is a planet."

A warm wet world should naturally feel different from a cold dry world.

A forest should exist because the conditions support a forest.

A desert should exist because the conditions support a desert.

An alien fungal planet should be achievable by changing environmental rules and content profiles rather than rebuilding the engine.

Build systems, not handcrafted worlds.

Begin Sprint 004.
