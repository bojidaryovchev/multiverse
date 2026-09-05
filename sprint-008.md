Read `CLAUDE.md` completely before doing anything else.

Then inspect the repository and verify the outputs of Sprint 001 through Sprint 007.

We are beginning:

# Sprint 008 — MVP Hardening & Playable Vertical Slice

This is the final sprint of the first Universe Exploration MVP track.

The objective is NOT to add major new gameplay systems.

The objective is to take everything already built and transform it into a:

# STABLE, REPEATABLE, DISTRIBUTABLE, PLAYABLE VERTICAL SLICE

At the end of this sprint, another person should be able to:

```text
install / launch client
↓
connect to local or remote dedicated server
↓
spawn in the procedural universe
↓
fly through space
↓
warp to another star system
↓
approach procedural planet
↓
enter atmosphere
↓
land
↓
exit ship
↓
explore living procedural environment
↓
see another player
↓
build persistent structure
↓
modify procedural world
↓
leave planet
↓
travel elsewhere
↓
return
↓
find persistent changes intact
```

without developer intervention.

The project must become coherent enough to serve as:

* first real playable prototype
* investor/demo build
* external playtest build
* foundation for Phase 2 development
* performance baseline
* architectural validation

Do NOT expand sideways into civilizations, economy, combat, crafting or large-scale MMO infrastructure.

---

# 0. PRECONDITION — VERIFY ALL PREVIOUS SPRINTS

Before hardening anything, verify Sprint 001–007 acceptance criteria.

Do not assume previous sprint reports are correct.

Actually build and test.

At minimum verify:

## Sprint 001

* universe coordinates
* deterministic seeds
* astronomical movement
* tests

## Sprint 002

* full spherical procedural planet
* terrain LOD
* patch streaming
* terrain continuity
* tests

## Sprint 003

* space → surface traversal
* gravity
* ship landing
* character walking
* surface → space
* tests

## Sprint 004

* climate
* biomes
* oceans
* atmosphere
* vegetation
* weather
* wildlife
* tests

## Sprint 005

* World State
* SQLite/local persistence
* building placement
* procedural removals
* restart persistence
* tests

## Sprint 006

* multiple star systems
* interstellar travel
* warp
* galactic architecture
* return-to-system persistence
* tests

## Sprint 007

* dedicated server
* 2+ clients
* shared world state
* authoritative interactions
* interest management
* reconnect
* server restart
* tests

Create an explicit verification report.

Any previous acceptance criterion that is not actually satisfied should be treated as an open defect.

Fix defects that materially affect the vertical slice.

---

# 1. SPRINT PRINCIPLE

SPRINT 008 IS NOT A FEATURE SPRINT.

Feature freeze.

Do not add major systems unless absolutely necessary to make the existing experience coherent.

Priorities:

```text
correctness
↓
stability
↓
performance
↓
usability
↓
visual continuity
↓
distribution
↓
polish
```

NOT:

```text
more features
```

---

# 2. FEATURE FREEZE

The current MVP feature set is:

```text
procedural universe
procedural galaxies
procedural star systems
procedural planets

space flight
high-speed travel
warp / FTL

seamless planet approach
atmospheric traversal
landing
ship entry / exit

spherical character movement

terrain
oceans
climate
biomes
vegetation
weather
basic wildlife

persistent building placement
persistent procedural-object removal

dedicated authoritative server
shared multiplayer universe
shared persistence
```

Do not add:

* combat
* weapons
* mining
* crafting
* inventory economy
* civilization simulation
* factions
* NPC societies
* quests
* story
* advanced ship customization
* multiplayer chat platform
* monetization
* player marketplace
* terraforming
* full ecosystem simulation
* procedural cities

These belong to Phase 2+.

---

# 3. PRIMARY PLAYER EXPERIENCE

The main vertical slice must support a coherent session.

Target flow:

```text
LAUNCH

↓

Main Menu

↓

Play

↓

Connect / Host Development Universe

↓

spawn near a starting planet or station/space position

↓

learn basic controls

↓

enter spacecraft if not already inside

↓

fly around local system

↓

select nearby star system

↓

warp

↓

arrive

↓

select interesting procedural planet

↓

approach planet

↓

enter atmosphere

↓

land

↓

exit ship

↓

explore

↓

see:
terrain
water
vegetation
weather
wildlife

↓

place simple persistent structure

↓

remove procedural object

↓

return to ship

↓

take off

↓

warp elsewhere

↓

return later

↓

persistent state remains
```

For multiplayer:

```text
Player B joins
↓
travels to Player A
↓
meets them
↓
sees same persistent world
↓
both modify shared world
```

This is the demo.

Optimize around it.

---

# 4. DEFINE A GOLDEN PATH

Create one explicit automated/manual "Golden Path" test.

This should be the canonical experience that must remain functional after every major change.

Document:

```text
Docs/Testing/GoldenPath.md
```

Include exact steps.

The Golden Path becomes a release gate.

---

# 5. NO DEVELOPER CONSOLE REQUIRED FOR NORMAL PLAY

The vertical slice must be playable without:

* debug teleports
* console commands
* editor manipulation
* manually editing config
* manually changing universe IDs

Developer tools may remain available.

But ordinary flow must work from UI/input.

---

# 6. FIRST-LAUNCH EXPERIENCE

Provide minimal first-launch configuration.

Examples:

```text
Display Mode
Resolution
Graphics Preset
Mouse Sensitivity
Master Volume
```

Do not create a giant settings screen.

Use sensible defaults.

---

# 7. MAIN MENU

Implement a minimal polished main menu.

Required:

```text
Play
Settings
Quit
```

For development multiplayer, Play may expose:

```text
Connect to Server
Single Player / Local Authority
```

Do not overbuild.

---

# 8. SERVER CONNECT UI

Provide a minimal way to connect to dedicated server.

For MVP:

```text
Server Address
Connect
```

Optional:

```text
recent server
localhost shortcut
```

No matchmaking infrastructure required.

---

# 9. CONNECTION STATES

Provide visible states:

```text
Connecting
Connected
Connection Failed
Disconnected
Reconnecting if implemented
```

Do not fail silently.

Display useful development/user-facing errors.

---

# 10. LOADING / PREPARATION UX

The game should avoid fake world loading screens during traversal.

However, initial application startup/server connection may require preparation.

If initial loading takes noticeable time, show:

```text
Generating Universe
Loading World State
Connecting
Preparing Local Simulation
```

Do not show a frozen window.

---

# 11. INPUT CLEANUP

Audit controls.

Create coherent default mappings for:

## Character

```text
WASD
Mouse Look
Jump
Interact
Enter Ship
Build Mode
```

## Spacecraft

```text
Thrust
Reverse
Pitch
Yaw
Roll if applicable
Boost / Speed Control
Warp
Target / Navigation
Exit Ship when landed
```

Avoid conflicting keys.

---

# 12. INPUT DEVICE SUPPORT

Keyboard + mouse is mandatory.

Controller support is optional for this sprint unless already easy.

Do not delay MVP over gamepad polish.

---

# 13. CAMERA HARDENING

Audit:

* character camera
* spacecraft camera
* atmosphere
* warp
* planet landing
* entering/exiting ship

Fix:

* sudden camera flips
* clipping
* extreme FOV jumps
* orientation discontinuities
* camera ending inside geometry

---

# 14. SHIP CONTROL HARDENING

The spacecraft must feel controllable enough for external users.

Tune:

```text
acceleration
deceleration
rotation
high-speed transition
warp entry
warp exit
landing assist
```

Do not attempt realistic simulation unless already implemented.

Usability > realism for MVP.

---

# 15. WARP UX

Make warp understandable.

At minimum show:

```text
Target
Distance
Current Speed
Warp State
ETA
```

Provide obvious:

```text
Enter Warp
Exit Warp
```

or equivalent controls.

Do not make testers guess.

---

# 16. NAVIGATION UX

Provide enough navigation to intentionally reach another system.

Minimum viable:

```text
nearby system list / target selector
current target marker
distance
```

No elaborate galaxy map required.

---

# 17. PLANET TARGETING

Allow selection of planets in active system.

Show basic generated information:

```text
Planet Name/ID
Type
Radius
Temperature category
Biome/environment category if known
Distance
```

Do not create encyclopedia UX.

---

# 18. LANDING EXPERIENCE

Harden:

```text
approach
↓
terrain streaming
↓
collision readiness
↓
landing
```

Prevent common failures:

* falling through ground
* ship exploding/jittering
* landing on unloaded terrain
* ship hovering unpredictably
* player exiting into terrain

---

# 19. SAFE SHIP EXIT

On exit:

find a valid nearby character spawn point.

Validate:

* ground
* collision
* no wall overlap
* not underwater unless intended
* adequate headroom

If unsafe:

do not eject player into invalid state.

---

# 20. SAFE SHIP ENTRY

Entry should work predictably.

No pixel-perfect interaction requirement.

Provide:

* interaction prompt
* reasonable range
* clear feedback

---

# 21. BUILDING UX

Keep construction tiny but usable.

Provide:

```text
Build Mode
Structure Selection
Placement Preview
Rotate
Place
Cancel
Remove
```

No production construction UI required.

---

# 22. PERSISTENCE FEEDBACK

When persistent change occurs, optionally show subtle feedback:

```text
Structure Placed
World Updated
```

Do not expose raw DB operations.

---

# 23. PLAYER DEATH

If no death system exists:

do NOT add one.

Handle catastrophic falling/out-of-bounds situations with a developer-safe recovery mechanism.

Examples:

```text
Respawn at ship
Restore last safe position
```

The vertical slice must not become permanently stuck.

---

# 24. STUCK RECOVERY

Provide a user-accessible:

```text
Recover / Unstuck
```

mechanism if necessary.

It should return the player to a recent safe state.

Do not require console commands.

---

# 25. WORLD SAFETY

Add safety checks for impossible states.

Examples:

```text
player inside planet
ship NaN position
player no active frame
invalid planet reference
warp state with no canonical velocity
```

Recover gracefully where possible.

Log everything.

---

# 26. CRASH AUDIT

Review crash reports/logs from all stress tests.

Fix reproducible crashes.

Do not classify recurring crashes as "known limitation" if they affect Golden Path.

---

# 27. ASSERTION AUDIT

Development assertions are useful.

Shipping build should not crash unnecessarily on recoverable conditions.

Audit:

```text
check()
ensure()
exceptions/errors
```

Use appropriate severity.

---

# 28. LOGGING

Standardize useful logging categories.

Potential:

```text
LogUniverse
LogGalaxy
LogSystem
LogPlanet
LogTerrain
LogEnvironment
LogTravel
LogWorldState
LogPersistence
LogNetwork
```

Avoid spam.

---

# 29. ERROR CONTEXT

Errors should include relevant IDs.

Example:

```text
Failed to load PersistenceRegion
Planet=...
Region=...
WorldSave=...
```

instead of:

```text
Load failed.
```

---

# 30. PERFORMANCE TARGETS

Create explicit initial MVP performance targets.

Do not invent impossible guarantees.

Measure first.

Example target categories:

```text
1080p playable target
mid/high-range development hardware
stable surface frame time
stable space frame time
no multi-second terrain stalls
bounded streaming queues
```

Document actual hardware.

---

# 31. PERFORMANCE TEST MATRIX

Test at least:

```text
Deep Space
Active Star System
High Orbit
Atmosphere Entry
Surface — Sparse Biome
Surface — Dense Forest
Storm / Weather
Two Players Same Region
Two Players Different Systems
Warp
```

Collect consistent metrics.

---

# 32. CPU PROFILING

Profile:

```text
game thread
render thread
terrain generation
environment generation
PCG
wildlife
network
persistence
```

Use Unreal profiling tools.

Find actual bottlenecks.

Do not optimize based solely on intuition.

---

# 33. GPU PROFILING

Profile:

```text
terrain
vegetation
atmosphere
clouds
water
Lumen
Nanite where used
shadows
post-processing
```

Create scalable quality settings.

---

# 34. MEMORY PROFILING

Measure memory during:

```text
start
planet approach
surface
leave planet
warp
multiple systems
return
```

Memory should not continually increase after repeated cycles.

Identify leaks/caches that fail to evict.

---

# 35. STREAMING STRESS TEST

Repeat:

```text
space
→ planet
→ surface
→ space
→ warp
→ another planet
→ surface
```

many times.

Monitor:

```text
terrain patches
environment instances
galactic sectors
active systems
persistence regions
network regions
async jobs
memory
```

Counts should remain bounded.

---

# 36. TERRAIN STREAMING HARDENING

Fix:

* cracks
* missing patches
* stale patches
* holes
* patch flicker
* LOD thrashing
* collision/visual mismatch
* slow async jobs attaching late

Do not add new terrain features.

---

# 37. ENVIRONMENT STREAMING HARDENING

Fix:

* trees popping incorrectly
* vegetation staying after unload
* duplicate PCG content
* wildlife leaking
* weather components accumulating
* alien biome content mismatches

---

# 38. SYSTEM STREAMING HARDENING

Fix:

* duplicate stars
* duplicate system activation
* stale target systems
* unloaded systems leaving actors
* target prewarming backlog

---

# 39. GALAXY STREAMING HARDENING

Ensure:

```text
galaxy point representation
sector generation
star visualization
```

remain bounded under extreme warp/debug travel.

---

# 40. HIGH-SPEED TRAVEL HARDENING

Stress:

```text
very large speed
low FPS
direction changes
target changes
system approach
planet approach
```

Fix:

* overshoot
* precision issues
* trajectory tunneling
* incorrect frame transition
* invalid arrival

---

# 41. FRAME TRANSITION HARDENING

Audit every transition:

```text
Universe
↔ Galaxy
↔ System
↔ Planet
↔ Local Surface
```

Verify:

* canonical position preserved
* velocity preserved
* orientation preserved
* no visual pop
* no simulation discontinuity

---

# 42. ORIGIN REBASE HARDENING

Stress many rebase operations.

Verify:

* player
* ship
* buildings
* other players
* wildlife
* terrain
* effects

remain coherent.

---

# 43. PERSISTENCE HARDENING

Test:

```text
rapid save actions
restart
server crash/restart
region unload during write
duplicate operations
many structures
many removals
```

Fix integrity issues.

---

# 44. SAVE VERSION UX

When save is incompatible:

show clear message.

Example:

```text
This world was created with an incompatible generation version.
```

Provide development options:

```text
Reset
Cancel
Migration if implemented
```

Do not silently load broken state.

---

# 45. DATABASE BACKUP

For external MVP testing, consider lightweight backup/rotation of local/server persistence.

Example:

```text
world.db
world.backup.db
```

Do not build enterprise backup infrastructure.

Protect playtests from trivial corruption.

---

# 46. DATABASE INTEGRITY CHECK

On startup, perform appropriate lightweight validation.

If SQLite supports integrity checks practical for this usage, expose them in development tools.

---

# 47. SERVER HARDENING

Dedicated server must:

* start cleanly
* fail clearly on bad config
* load persistence
* accept clients
* survive disconnect/reconnect
* shut down cleanly
* not require Unreal Editor

---

# 48. SERVER CONFIGURATION

Move important server settings into config.

Potential:

```text
UniverseSeed
WorldSave
ListenPort
MaxPlayersDevelopment
Autosave / flush settings
Logging
```

Do not require recompilation.

---

# 49. CLIENT CONFIGURATION

Expose configurable:

```text
Server
Graphics
Input
Audio
```

Keep minimal.

---

# 50. NETWORK STRESS

Test:

```text
latency
jitter
packet loss
disconnect
reconnect
```

Fix catastrophic behavior.

The prototype does not require esports networking.

It should remain playable.

---

# 51. INTEREST MANAGEMENT HARDENING

Verify traffic drops appropriately when:

```text
players same location
same planet far apart
same system
different systems
different galaxies
```

Do not replicate irrelevant actors continuously.

---

# 52. NETWORK BANDWIDTH BUDGET

Record:

```text
bytes/sec/client
packets/sec
replicated actor counts
```

for key scenarios.

Identify obvious waste.

---

# 53. PROCEDURAL CONTENT NETWORK AUDIT

Ensure network is NOT accidentally replicating:

* terrain vertices
* grass
* static rocks
* stars
* entire environment state

unless needed.

Shared seeds + deltas remain the model.

---

# 54. WEATHER NETWORK HARDENING

Players in same region should observe compatible:

```text
weather
wind
day/night
```

without replicating rendering particles.

---

# 55. CLIENT GENERATION CONSISTENCY

Add checksums/hash diagnostics for deterministic descriptors.

Useful systems:

```text
system
planet
terrain region
environment
```

When server/client disagree, log clearly.

---

# 56. AUTOMATED END-TO-END TEST

Build the strongest practical automated E2E test.

Target flow:

```text
start server
↓
connect client
↓
spawn
↓
travel
↓
modify persistent world
↓
disconnect
↓
restart server
↓
reconnect
↓
verify state
```

Full graphical automation may be difficult.

Use the strongest feasible combination of:

* Unreal functional tests
* headless server tests
* automation tests
* scripted clients

---

# 57. GOLDEN PATH MULTIPLAYER TEST

Create a second Golden Path specifically for multiplayer.

Document:

```text
Docs/Testing/GoldenPathMultiplayer.md
```

---

# 58. SOAK TEST

Run an extended automated/semiautomated session.

The goal is detecting:

* leaks
* stale tasks
* growing caches
* DB queue growth
* network ghost state

Use a repeatable traversal loop.

Report actual duration.

Do not fabricate.

---

# 59. CHAOS / FAILURE TESTING

Intentionally create failures:

```text
disconnect client
restart server
cancel warp target
leave while terrain generating
quit during persistence activity
rapidly switch planets
```

The game should either recover or fail clearly.

---

# 60. GRAPHICS PRESETS

Create at least:

```text
Low
Medium
High
Epic
```

or an equivalent small set.

Control expensive systems such as:

```text
vegetation density
shadow quality
cloud quality
Lumen
view distance
terrain detail
water quality
```

Do not make Low alter procedural identity.

Only representation quality changes.

---

# 61. PROCEDURAL IDENTITY MUST NOT DEPEND ON GRAPHICS SETTINGS

Critical invariant:

```text
Low graphics
High graphics
```

must produce the same canonical:

```text
planet
terrain identity
tree identities where persistent
structures
world state
```

Only visual fidelity/instance density representations may differ where safe.

Test this.

---

# 62. RESOLUTION SCALABILITY

Support common desktop resolutions.

Test:

```text
1080p
1440p
```

where hardware allows.

No need for exhaustive monitor certification.

---

# 63. FRAME RATE INDEPENDENCE

Audit simulation for frame-rate dependence.

Particularly:

* warp movement
* weather
* day/night
* wildlife
* procedural streaming
* ship controls

Test varying frame rates.

---

# 64. SAVE / NETWORK TIME INDEPENDENCE

Use server simulation time where authoritative.

Do not tie important persistent state to client frame time.

---

# 65. VISUAL TRANSITION POLISH

Improve the most visible transitions.

Prioritize:

```text
star proxy → system star
planet proxy → detailed planet
orbit → atmosphere
atmosphere → terrain
terrain LOD
warp entry/exit
day/night
weather transition
```

Do not chase microscopic graphical imperfections.

---

# 66. PLANET-FROM-SPACE QUALITY

The planet should look convincingly planet-like.

From space, player should see:

```text
land
ocean
atmosphere
clouds
day/night
broad biome coloration
```

No need for final AAA rendering.

---

# 67. SURFACE QUALITY

On surface:

```text
terrain
vegetation
water
sky
weather
wildlife
```

should form one coherent scene.

Fix placeholder artifacts that severely damage the demo.

---

# 68. ALIEN PLANET QUALITY

Keep at least one alien/fungal planet profile.

It should visibly demonstrate:

```text
same engine
different planetary identity
```

This remains strategically important.

---

# 69. WORLD VARIETY TEST

Generate multiple planet seeds.

Identify pathological outputs:

* all ocean
* tiny islands only
* absurd mountains
* no usable landing zones
* biome monotony
* extreme climate values

Add bounds/validation where appropriate.

Do not make every planet equally pleasant.

But avoid broken generation.

---

# 70. GENERATION SANITY CHECK

Implement validation for generated descriptors.

Examples:

```text
PlanetRadius > 0
AtmosphereHeight valid
OceanLevel sane
TerrainHeight < safe fraction of radius
Star system planets not invalid
```

Reject/regenerate pathological candidates deterministically if necessary.

Document rules.

---

# 71. STARTING SYSTEM

Create a controlled default starting experience.

The universe remains procedural.

But choose/validate a deterministic starting seed/system that contains:

```text
usable star
habitable planet
land
water
vegetation
reasonable gravity
```

This avoids external testers spawning in nonsense.

Do not hardcode the entire planet manually.

---

# 72. STARTING LOCATION

Provide a safe initial spawn.

Potential:

```text
ship in low orbit
```

or:

```text
ship on surface
```

I recommend:

# start in orbit / near planet

because it immediately demonstrates scale.

---

# 73. INITIAL ORIENTATION

Point player toward something interesting.

Example:

```text
starting planet
```

should be obvious.

Do not spawn facing empty black space.

---

# 74. MINIMAL ONBOARDING

Add very lightweight prompts.

Example:

```text
WASD — Move
Mouse — Look
Shift — Increase thrust
T — Target
Warp — ...
```

Do not build tutorial missions.

---

# 75. CONTEXTUAL PROMPTS

Show only when relevant:

```text
Press E to Exit Ship
Press E to Enter Ship
Press B to Build
```

No giant tutorial overlay.

---

# 76. OBJECTIVE PROMPT

For external playtest, optionally show simple non-binding goals:

```text
1. Land on a planet.
2. Explore the surface.
3. Place a structure.
4. Travel to another system.
```

This helps testers understand what to try.

---

# 77. HUD CLEANUP

Separate:

```text
Player HUD
```

from:

```text
Developer Debug HUD
```

Normal build should show only useful player information.

Debug details toggleable.

---

# 78. PLAYER HUD

Potential minimum:

```text
Speed
Travel Mode
Target
Distance
Altitude near planet
Interaction prompt
Build mode
Connection state
```

Avoid clutter.

---

# 79. DEBUG HUD

Keep powerful diagnostics accessible behind a development toggle.

Do not remove debugging infrastructure built in earlier sprints.

---

# 80. AUDIO PASS

Add or clean up basic:

```text
ship engine
warp
atmosphere entry
wind
rain
ocean
birds
footsteps
```

Placeholder audio is acceptable.

Silence severely harms perceived quality.

Do not spend sprint on full sound design.

---

# 81. MUSIC

Optional.

Do not delay release for music.

If already available, use subtle background ambience.

---

# 82. SHIP VISUAL PLACEHOLDER

If current spacecraft is a primitive cube, improve it enough that external testers understand:

```text
this is a spacecraft
```

Use legally appropriate placeholder/Fab asset if useful.

Do not begin modeling a custom fleet.

---

# 83. CHARACTER VISUAL PLACEHOLDER

Likewise use an understandable character representation.

No custom character creator.

---

# 84. CONTENT LICENSING AUDIT

Audit third-party assets/libraries.

Record:

```text
source
license
version
usage
```

Do not distribute unlicensed content.

Create:

```text
THIRD_PARTY.md
```

if appropriate.

---

# 85. DEPENDENCY AUDIT

Review external C++/Unreal dependencies.

Remove unused ones.

Pin versions where practical.

Document build requirements.

---

# 86. REPOSITORY CLEANUP

Remove:

* dead experimental code
* obsolete duplicate systems
* temporary giant logs
* unused assets
* accidentally committed build outputs

Do NOT aggressively refactor stable code solely for aesthetics.

---

# 87. COMPILER WARNING AUDIT

Review warnings.

Fix meaningful warnings.

Do not suppress globally unless justified.

---

# 88. STATIC ANALYSIS

Run available Unreal/compiler static-analysis tools where practical.

Prioritize:

* lifetime issues
* nullability
* thread safety
* integer overflow
* serialization
* async callbacks

---

# 89. THREADING AUDIT

Review asynchronous systems:

```text
terrain
environment
galaxy
system generation
persistence
network callbacks
```

Verify:

* no unsafe UObject access
* cancellation works
* shutdown works
* stale result guards work

---

# 90. RACE CONDITION TESTING

Stress:

```text
rapid travel
server disconnect
world unload
game exit
```

while async jobs are active.

Fix repeatable races.

---

# 91. PACKAGED WINDOWS CLIENT

Produce a packaged Windows client.

It must run outside Unreal Editor.

Test from packaged build.

Do not declare success based only on PIE/editor.

---

# 92. PACKAGED DEDICATED SERVER

Produce dedicated-server package/build.

It must run independently.

Provide command-line/config examples.

---

# 93. CLEAN-MACHINE TEST

Where feasible, test packaged client on a machine/environment without the full Unreal development setup.

If unavailable, document runtime prerequisites carefully.

Do not assume compiler/editor installation.

---

# 94. BUILD SCRIPT

Create one or more scripts for reproducible:

```text
build client
build server
run tests
package client
package server
```

Use platform-appropriate scripting.

Do not require remembering a long manual command sequence.

---

# 95. CI

Set up basic CI if practical.

At minimum:

```text
compile relevant modules
run non-editor deterministic/unit tests
```

Full Unreal packaging CI may be expensive.

Do not spend the sprint wrestling with cloud CI if it blocks local MVP.

But leave a clean path.

---

# 96. VERSION NUMBER

Introduce explicit project build/version information.

Example:

```text
Universe MVP 0.1.0
```

Display in menu/debug diagnostics.

Server/client compatibility should reference appropriate protocol/build versions.

---

# 97. RELEASE CONFIG

Create a clear configuration distinction:

```text
Development
Test
Shipping
```

Debug cheats/tools should not accidentally dominate normal external build.

---

# 98. SECURITY BASICS

This is not a production MMO security sprint.

But ensure:

* clients cannot directly write DB
* obvious malformed RPCs validated
* server doesn't trust arbitrary file paths/input
* development admin commands can be disabled

---

# 99. NETWORK EXPOSURE

If external server testing is intended:

document:

```text
port
firewall requirement
server address
```

Do not automatically deploy public infrastructure unless asked.

---

# 100. CRASH LOG COLLECTION

Make it easy for external testers to return:

```text
logs
crash information
build version
```

Document location.

---

# 101. TELEMETRY INTERFACE

Create a lightweight abstraction for future telemetry if useful.

Do NOT add invasive analytics/cloud services yet.

Local performance/event logging is enough.

Potential events:

```text
GameStarted
Connected
PlanetLanded
WarpCompleted
StructurePlaced
Crash
```

---

# 102. PLAYTEST FEEDBACK

Provide a minimal mechanism/document instructing testers what feedback matters.

Focus:

```text
Did you understand what to do?
Did anything break?
Did travel feel seamless?
Did planet feel alive?
Where were performance drops?
Did multiplayer feel coherent?
```

No backend feedback portal required.

---

# 103. PERFORMANCE BASELINE DOCUMENT

Create:

```text
Docs/Performance/MVPBaseline.md
```

Include:

* test hardware
* build configuration
* graphics preset
* scenarios
* frame times
* memory
* server tick
* bandwidth
* generation times

Measured results only.

---

# 104. MVP ARCHITECTURE DOCUMENT

Create/update:

```text
Docs/Architecture/MVPArchitecture.md
```

It should explain the entire system at a useful high level:

```text
Universe generation
↓
Galaxies
↓
Systems
↓
Planets
↓
Terrain/environment
↓
Travel
↓
World State
↓
Persistence
↓
Networking
```

Include module ownership.

---

# 105. DATA FLOW DOCUMENT

Document:

```text
seed
↓
deterministic descriptors
↓
runtime representation
↓
player interaction
↓
server authority
↓
World State
↓
persistent delta
↓
replication
```

This becomes important for Phase 2.

---

# 106. FAILURE MODES DOCUMENT

Create:

```text
Docs/Testing/KnownFailureModes.md
```

Track:

* reproduction
* severity
* workaround
* status

Do not hide known issues.

---

# 107. BUG SEVERITY

Classify:

## Blocker

Cannot complete Golden Path.

## Critical

Crashes/corrupts state/common major issue.

## Major

Serious but workaround exists.

## Minor

Cosmetic/low impact.

Sprint 008 should close all Blockers and as many Criticals as practical.

---

# 108. RELEASE GATE — BLOCKERS

No external MVP build if:

* frequent crash on Golden Path
* persistence corrupts
* multiplayer cannot reconnect
* planet traversal frequently breaks
* warp frequently corrupts position
* packaged client doesn't run
* server doesn't persist world

---

# 109. GOLDEN PATH TEST — SINGLE PLAYER

Perform from packaged build:

```text
launch
↓
start/connect
↓
spawn
↓
fly
↓
warp
↓
arrive another system
↓
land on procedural planet
↓
exit
↓
explore
↓
place structure
↓
remove procedural object
↓
take off
↓
leave
↓
quit
↓
restart
↓
return
↓
verify persistence
```

Mandatory.

---

# 110. GOLDEN PATH TEST — MULTIPLAYER

Perform with packaged server/client:

```text
start server

↓

A connects
B connects

↓

A on Planet A

↓

B warps to A

↓

B lands

↓

A + B meet

↓

A builds

↓

B sees it

↓

B removes procedural object

↓

A sees it

↓

A warps away

↓

A returns

↓

both disconnect

↓

server restarts

↓

both reconnect

↓

shared modifications remain
```

Mandatory.

---

# 111. GOLDEN PATH TEST — ALIEN PLANET

Visit at least one distinctly alien/fungal world.

Verify:

* terrain
* atmosphere
* environmental profile
* vegetation
* travel
* multiplayer where applicable

This demonstrates system generality.

---

# 112. GOLDEN PATH TEST — GALAXY SCALE

Using development/debug travel:

```text
leave local system
↓
leave galactic region
↓
exit galaxy
↓
approach second galaxy
```

This can remain an engineering demonstration rather than normal gameplay speed.

Verify no architectural failure.

---

# 113. SOAK TEST — SERVER

Run server for extended test period with bots/clients if feasible.

Track:

```text
memory
tick time
persistence queue
entity counts
subscriptions
```

Document actual duration.

---

# 114. SOAK TEST — CLIENT

Run repeated:

```text
planet
→ warp
→ planet
→ warp
```

for extended time.

Track memory/resource stability.

---

# 115. SAVE STRESS

Create many world modifications.

Restart server repeatedly.

Verify integrity.

---

# 116. NETWORK STRESS

Use simulated:

```text
100 ms
200 ms
packet loss
```

Run Golden Path portions.

Document limitations.

---

# 117. LOW PERFORMANCE HARDWARE STRATEGY

Even if only high-end hardware is currently tested, ensure graphics settings can reduce:

```text
foliage
clouds
Lumen
view distance
terrain detail
```

Do not promise low-end support without testing.

---

# 118. PERFORMANCE FALLBACKS

If a subsystem is too expensive:

prefer graceful quality reduction over disabling core gameplay.

Example:

```text
dense forest
→ fewer distant tree instances
```

NOT:

```text
forest fails to load
```

---

# 119. STREAMING FALLBACKS

If generation falls behind:

prioritize:

```text
collision
terrain
major structures
```

before:

```text
grass
birds
cosmetic clutter
```

The game should degrade gracefully.

---

# 120. NETWORK FALLBACKS

Under latency:

prioritize authoritative consistency.

Cosmetic smoothness may degrade before world state correctness.

---

# 121. DEBUG BUILD

Maintain a development/debug build with:

```text
teleports
speed multipliers
planet selection
weather control
performance stats
network stats
persistence inspector
```

Do not remove these.

They are essential for Phase 2.

---

# 122. PLAYTEST BUILD

Create a cleaner build with debug features hidden/disabled by default.

Keep optional developer console accessible through explicit configuration if appropriate.

---

# 123. README

Update root `README.md`.

Include:

```text
What the project is
Current MVP capabilities
Prerequisites
How to build
How to run server
How to run client
How to run tests
Known limitations
Repository architecture
```

Keep it accurate.

---

# 124. SETUP DOCUMENT

Create:

```text
Docs/Setup/DevelopmentSetup.md
```

A new developer/agent should be able to bootstrap the project.

---

# 125. AGENT HANDOFF DOCUMENT

Create:

```text
Docs/AI/AgentHandoff.md
```

Summarize:

* architecture
* critical invariants
* dangerous areas
* test requirements
* current technical debt
* future phase priorities

This supports future parallel AI development.

---

# 126. CODE OWNERSHIP MAP

Document major subsystem ownership:

```text
UniverseCore
Galaxy
Systems
Planets
Terrain
Environment
Travel
Persistence
WorldState
Networking
UI
```

This is useful for future multi-agent work.

---

# 127. ARCHITECTURAL INVARIANTS

Explicitly document invariants that future agents MUST NOT break.

Examples:

```text
Unreal coordinates are not canonical universe coordinates.

Procedural base state is regenerated, not persisted.

Persistent storage contains deltas/history.

Clients do not authoritatively mutate shared world state.

Terrain/environment runtime detail is bounded by relevance.

Distant universe scale does not imply distant Actor count.

Different local origins must still represent same canonical location.
```

---

# 128. DEBT AUDIT

List intentional shortcuts from Sprints 001–007.

Classify:

```text
must fix before public alpha
should fix
can defer
```

Do not refactor all debt during Sprint 008.

Make it visible.

---

# 129. PHASE 2 BOUNDARY

Sprint 008 finishes the first MVP.

After completion, do NOT automatically continue adding features.

First evaluate:

```text
Is the universe fun to traverse?

Do planets feel interesting?

Is building/persistence satisfying?

Does multiplayer feel coherent?

Does the architecture actually scale computationally?

What did playtesters naturally want to do?
```

Use evidence to choose Phase 2.

---

# 130. POTENTIAL PHASE 2 TRACKS

Do not implement now.

Potential next systems include:

```text
Advanced Building
Resources / Mining
Crafting / Manufacturing
Ships
Combat
Procedural Fauna
Ecosystems
Settlements
NPC Populations
Civilizations
Industry
Economy
Territory
Organizations
Technology
Politics
War
Large-scale server partitioning
Mobile companion/client
```

Prioritize based on playtest evidence.

---

# 131. SPRINT 008 ACCEPTANCE CRITERIA

Sprint 008 is complete only if:

## Build

* [ ] packaged Windows client builds
* [ ] packaged dedicated server builds
* [ ] both run outside Unreal Editor
* [ ] reproducible build scripts exist
* [ ] project/build version exists

## Usability

* [ ] main menu exists
* [ ] server connect flow exists
* [ ] input mappings coherent
* [ ] navigation targeting understandable
* [ ] warp controls understandable
* [ ] ship entry/exit understandable
* [ ] build mode understandable
* [ ] ordinary Golden Path requires no console commands

## Stability

* [ ] no known Blocker Golden Path defects
* [ ] major crashes fixed
* [ ] async shutdown safe
* [ ] streaming queues bounded
* [ ] repeated travel does not obviously leak resources
* [ ] save state remains intact
* [ ] server restart remains intact

## Performance

* [ ] baseline profiling completed
* [ ] deep space measured
* [ ] orbit measured
* [ ] dense surface measured
* [ ] weather measured
* [ ] warp measured
* [ ] multiplayer measured
* [ ] memory measured
* [ ] server tick measured
* [ ] bandwidth measured
* [ ] performance document created

## Graphics

* [ ] space visually coherent
* [ ] planet visible convincingly from orbit
* [ ] atmosphere transition acceptable
* [ ] surface environment coherent
* [ ] alien planet demonstrates variation
* [ ] graphics presets exist
* [ ] graphics settings do not alter canonical world identity

## Persistence

* [ ] structure placement survives region unload
* [ ] survives client restart
* [ ] survives server restart
* [ ] procedural removal survives
* [ ] multiple system state remains isolated
* [ ] DB version compatibility works

## Multiplayer

* [ ] 2+ clients connect
* [ ] players meet on same planet
* [ ] canonical position survives different local origins
* [ ] shared structures work
* [ ] shared removals work
* [ ] players can occupy different systems
* [ ] reconnect works
* [ ] server restart works
* [ ] irrelevant replication significantly reduces

## Travel

* [ ] space flight works
* [ ] planet approach works
* [ ] landing works
* [ ] surface → space works
* [ ] interstellar warp works
* [ ] another system can be reached
* [ ] return trip works
* [ ] debug galaxy exit / second galaxy proof still works

## Testing

* [ ] Sprint 001–007 automated tests pass
* [ ] Golden Path single-player passes
* [ ] Golden Path multiplayer passes
* [ ] restart tests pass
* [ ] stress tests pass at acceptable level
* [ ] soak test performed and results documented
* [ ] packaged-build tests performed

## Documentation

* [ ] README updated
* [ ] MVP architecture documented
* [ ] performance baseline documented
* [ ] development setup documented
* [ ] Golden Paths documented
* [ ] known failure modes documented
* [ ] architectural invariants documented
* [ ] AI agent handoff documented
* [ ] technical debt documented

---

# 132. DEFINING MVP DEMONSTRATION

Before Sprint 008 may be declared complete, perform and record this demonstration using packaged builds.

```text
START DEDICATED SERVER

↓

PLAYER A LAUNCHES PACKAGED CLIENT

↓

connects

↓

spawns in orbit around procedural world

↓

flies away from starting planet

↓

selects another star

↓

accelerates

↓

ENTERS WARP

↓

crosses interstellar space

↓

arrives in completely different procedural system

↓

selects habitable planet

↓

planet grows from distant object to full world

↓

atmosphere becomes visible

↓

clouds / ocean / terrain become visible

↓

enters atmosphere

↓

descends

↓

lands in procedural biome

↓

exits spacecraft

↓

walks through grass / vegetation

↓

weather changes

↓

birds / wildlife visible

↓

places persistent structure

↓

PLAYER B CONNECTS

↓

B travels to same system

↓

B enters same atmosphere

↓

B lands near A

↓

A and B see each other

↓

B sees A's structure

↓

B removes procedural tree

↓

A sees tree disappear

↓

A enters ship

↓

takes off

↓

warps to another system

↓

B stays behind

↓

A returns

↓

lands in same region

↓

structure remains

↓

tree remains absent

↓

both disconnect

↓

DEDICATED SERVER STOPS

↓

DEDICATED SERVER RESTARTS

↓

both reconnect

↓

travel back to same location

↓

same procedural planet

same terrain

same biome

same persistent structure

same removed tree
```

If this works reliably from packaged builds:

# THE FIRST UNIVERSE MVP IS COMPLETE.

---

# 133. COMPLETION REPORT

At completion provide a comprehensive engineering report.

## MVP Status

State clearly what genuinely works.

## Golden Path

Report result of the complete packaged-build Golden Path.

## Architecture

Summarize:

* Universe coordinates
* Galaxy generation
* System generation
* Planet generation
* Terrain
* Environment
* Travel
* Persistence
* World State
* Networking

## Performance

Provide measured:

* CPU
* GPU
* frame times
* memory
* terrain generation
* environment generation
* warp cost
* server tick
* bandwidth
* persistence latency

Include hardware/build configuration.

Do not fabricate.

## Stability

Report:

* crashes found
* crashes fixed
* soak duration
* memory behavior
* streaming behavior

## Multiplayer

Report:

* player count tested
* latency tested
* bandwidth
* interest-management behavior
* reconnect/server-restart behavior

## Packaged Artifacts

Report exact produced client/server builds and how to run them.

## Known Limitations

Be explicit.

## Technical Debt

Prioritized.

## Phase 2 Recommendations

Do NOT simply recommend "add everything."

Use actual MVP evidence.

Rank next systems by:

```text
player value
architectural dependency
technical risk
development leverage
```

---

# FINAL PRINCIPLE

Sprint 008 does not exist to make the universe larger.

It already is enormous.

It does not exist to add more systems.

It exists to prove that the systems already built form one coherent game.

The finished MVP should communicate the core fantasy without explanation:

> I am standing on a living procedural planet inside a giant shared universe. I can get in my ship, leave this world, fly through space, warp to another star, land on another planet, build there, meet another real player, leave, and come back later — and the universe remembers what happened.

If that sentence is genuinely true in a packaged playable build, Sprint 008 is complete.

Begin Sprint 008.
