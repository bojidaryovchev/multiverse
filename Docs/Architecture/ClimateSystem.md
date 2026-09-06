# Climate System

Temperature and moisture, anywhere on a planet, from its physical properties.

Related: [PlanetEnvironment.md](PlanetEnvironment.md), [Biomes.md](Biomes.md).

---

## Latitude without a map

```
sin(latitude) = dot(surfaceDirection, rotationAxis)
```

From the rotation axis, **never** from cube-face coordinates. A cube-derived
latitude would put six seams and eight corners into the temperature field — in
exactly the places [ADR-003](../ADR/ADR-003-planet-topology-and-lod.md) worked
hardest to make invisible — and the result would be a planet whose climate
quietly revealed its topology.

Note that `sin(latitude)` is the useful quantity, not the angle. Every formula
here wants the sine or its square, both of which come straight out of the dot
product, so the arcsine is never taken. That is not a micro-optimisation: it
keeps the whole climate path free of transcendentals, and therefore bit-identical
across platforms, for the same reason ADR-003 gives for terrain.

---

## Temperature

```
T = mean
  + (delta/2) * (1 - 2 sin²(lat))     latitude
  - lapse * altitudeKm                elevation, above the ocean only
  + regional                          low-frequency variation, ±9 K
  → moderated toward the mean if submerged
```

The latitude term is `cos(2·lat)` written without trigonometry: warmest at the
equator, coldest at both poles, exactly at the planet mean at 45° — the same
double-humped shape real insolation follows.

**The lapse rate applies only above the ocean surface**, because there is no air
column below it to cool. Applying it to the sea floor would put permanent ice at
the bottom of every trench.

**Submerged points are pulled a third of the way toward the planet mean.** Water's
heat capacity flattens extremes — the sea is warmer than the land at the poles and
cooler at the equator — and this is a crude stand-in for that. What it does
visibly is stop oceans freezing solid wherever the land beside them is cold.

Regional variation is sampled in 3D on the direction itself, like terrain, so it
is continuous across every cube face and corner by construction. Without it the
planet is a set of neat latitude stripes.

---

## Moisture

Four terms, all approximations, each named:

| Term | What it is |
| --- | --- |
| planet bias | how wet the world is overall, from ocean coverage |
| regional pattern | large coherent wet and dry bands, from its own noise stream |
| ocean proximity | **approximated by altitude above the ocean** |
| temperature | warm air holds more water; cold poles are deserts |

The ocean-proximity term is the weak one and is documented as such in the code. A
true distance-to-coast needs a search over the surface, which a per-point field
evaluated on a worker thread cannot do. Altitude is the available proxy and it is
a reasonable one — low ground is usually near water — but **it calls a dry inland
basin below sea level humid**. That is a known error, and the fix is a coarse
precomputed distance field over the sphere.

The temperature term is not a quirk. Polar deserts are real, and Antarctica is
one.

An airless world has zero moisture everywhere. That is not a tuning choice: a
vacuum holds no water.

---

## What is deliberately not modelled

No atmospheric circulation, no ocean currents, no rain shadows, no seasons.

Those are simulations. This is a field evaluated in isolation at a point, which
is what lets it run on a worker thread, at any resolution, in any order, with no
state. Every approximation is named where it is made rather than presented as
physics.

`AxialTiltRadians` is stored but unused: seasons need an orbital phase, which
needs a clock tied to the orbit, which is not built. It is present because the
rotation axis it would modify is already present, and adding it later would mean
revisiting every latitude call site.

---

## Frequencies

| Layer | Frequency | Feature size |
| --- | --- | --- |
| Terrain continents | 1.1 | continental |
| Terrain detail | 220 | metres |
| Climate temperature | 0.85 | a third of a planet |
| Climate humidity | 0.55 | half a planet |

Climate sits *below* even the continental terrain layer, because a climate band
has to be larger than the continent it crosses. A moisture field that varied per
metre would put a rainforest inside a desert.

---

## Testing

`Universe.Planet.ClimateFields` asserts:

- the equator is warmer than the poles by the stated delta, and 45° is the mean
- altitude cools at exactly the lapse rate, and only above the ocean
- humidity stays in [0, 1] and is finite everywhere
- cold is drier than warm
- an airless world has exactly zero moisture

and the one that matters most — **cube-face continuity**. Sampling either side of
all twelve cube edges, at 34 points along each, on all six faces: the worst
temperature discontinuity must be under 0.5 K and the worst humidity
discontinuity under 0.02. The two sample points are about 30 m apart on a
6,371 km planet, so a real gradient is far below that and a seam would be far
above it.
