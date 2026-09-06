// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseCoordinates.h"
#include "UniverseSeed.h"
#include "UniverseSerialization.h"
#include "StarSystemDescriptor.h"

/**
 * PlanetSurface.h
 *
 * What a planet *is*, for terrain purposes: a seed, a radius, an elevation
 * range and a generation version.
 *
 * Sprint 001 already produces FPlanetDescriptor - mass, radius, orbit,
 * temperature, type - as part of star system generation. This is deliberately
 * not a second copy of that. FPlanetSurfaceDescriptor is *derived from* it and
 * adds only what terrain needs, so a planet's radius has exactly one source of
 * truth. If the two ever disagreed, the astronomy and the ground under the
 * player's feet would be describing different worlds.
 *
 * Like everything else in this module it is plain data: no UObject, no Actor,
 * safe to copy to a worker thread, and reconstructible from its address alone.
 */

/**
 * Terrain generation version.
 *
 * Bumping this changes the shape of every planet in the universe.
 *
 * That sounds dramatic and is exactly the point. Players will eventually build
 * structures anchored to particular ground; if the algorithm underneath them
 * changes silently, mountains move and buildings end up buried or floating.
 * Making the version an explicit, serialised part of a planet's identity means
 * that when the generator does change, saved worlds can be detected as having
 * been produced by an older one, rather than quietly regenerating wrong.
 *
 * No migration machinery exists yet, and none is needed yet. What is needed
 * now is that the version is not an invisible implementation detail later.
 */
namespace PlanetTerrainVersion
{
    inline constexpr uint32 Current = 1;
}

/**
 * Tunables that affect how terrain is *built*, never what it *is*.
 *
 * The split matters: changing anything here must not move a single vertex of
 * the planet. Patch resolution changes how finely the surface is sampled, not
 * where the surface lies, so a player on a low-detail machine stands on the
 * same mountain as everyone else.
 */
struct UNIVERSEPLANET_API FPlanetTerrainSettings
{
    /**
     * Vertices per patch edge. MUST be 2^p + 1.
     *
     * Not a style preference - a hard requirement from CubeSphere.h. Patch
     * vertex UVs are (X + i/(N-1)) / 2^L, which is dyadic only when N-1 is a
     * power of two, and only dyadic UVs make seam arithmetic exact. N = 50
     * would put a one-ULP crack along every patch border on the planet.
     *
     * 65 gives 64 quads per edge: 8,192 triangles and 4,225 vertices per patch.
     * That is a good ratio of draw-call overhead to triangle count for a
     * component-per-patch renderer - 33 makes patches too cheap to justify
     * their per-component cost, 129 makes each generation task chunky enough to
     * hurt streaming latency at 16x the sample count.
     */
    int32 PatchResolution = 65;

    /**
     * Octaves in each terrain layer.
     *
     * Fixed rather than varying with patch level, deliberately. Level-dependent
     * octave counts are the obvious optimisation and would let a low LOD skip
     * detail it cannot resolve, but they make a vertex's height depend on which
     * LOD asked for it - so the surface shifts slightly whenever a patch splits
     * or merges. A fixed count means a point on the planet has exactly one
     * height, and parent and child agree on it bit-for-bit. Correctness first;
     * the optimisation is recorded as future work.
     */
    int32 ContinentOctaves = 5;
    int32 MountainOctaves = 8;
    int32 DetailOctaves = 6;

    /** True if PatchResolution is a valid 2^p + 1 value of sane size. */
    bool IsValid() const
    {
        if (PatchResolution < 5 || PatchResolution > 513)
        {
            return false;
        }
        const int32 Quads = PatchResolution - 1;
        return (Quads & (Quads - 1)) == 0;
    }

    int32 GetVertexCount() const { return PatchResolution * PatchResolution; }
    int32 GetQuadsPerEdge() const { return PatchResolution - 1; }
};

/**
 * A planet, as terrain generation sees it.
 */
struct UNIVERSEPLANET_API FPlanetSurfaceDescriptor
{
    /**
     * Stable identity: the system's address, the orbit index, and a hash of
     * both. Address-derived exactly like FUniverseSystemId, for the same
     * reason - it has to survive restarts, machines and engine versions.
     */
    FUniverseSystemId SystemId;
    int32 OrbitIndex = 0;
    uint64 PlanetKey = 0;

    /** Terrain seed, descended from the body seed through the Surface domain. */
    FUniverseSeed Seed;

    /** Canonical position of the planet centre in universe coordinates. */
    FUniversePosition Position;

    /** Sea-level radius in metres. Terrain is displaced about this. */
    double RadiusMeters = 6371000.0;

    /**
     * Peak terrain elevation above the sea-level radius, in metres.
     *
     * Derived from the planet's own radius rather than fixed: Earth's highest
     * peak is about 0.14% of its radius, and relief scales with a body's size
     * and gravity. Using a constant would give a small moon Himalayas.
     */
    double MaxElevationMeters = 9000.0;

    /** Deepest basin below the sea-level radius, in metres (positive value). */
    double MaxDepthMeters = 11000.0;

    /**
     * Gravitational acceleration at the sea-level radius, in m/s^2.
     *
     * Carried through from FPlanetDescriptor rather than recomputed from mass
     * and radius here, for the same reason radius is: one source of truth. The
     * whole gravity field is derived from this and RadiusMeters - see
     * PlanetGravity.h - so a planet's astronomy and the weight of a player
     * standing on it can never disagree.
     */
    double SurfaceGravityMs2 = 9.81;

    /**
     * Height of the logical top of the atmosphere above the sea-level radius,
     * in metres. Zero means no atmosphere.
     *
     * "Logical" is the operative word. This is a simulation boundary - where
     * drag, heating and the transition from a space flight model to an
     * atmospheric one begin - and it is deliberately independent of whatever a
     * renderer eventually does with sky colour. A visual effect can be changed
     * or switched off without moving the altitude at which the simulation
     * behaves differently.
     */
    double AtmosphereHeightMeters = 0.0;

    /** The version that generated this planet. See PlanetTerrainVersion. */
    uint32 GenerationVersion = PlanetTerrainVersion::Current;

    bool IsValid() const
    {
        return PlanetKey != 0 && RadiusMeters > 0.0 && GenerationVersion != 0;
    }

    /** Largest radius any terrain vertex can reach - the bounding sphere. */
    double GetMaxRadiusMeters() const { return RadiusMeters + MaxElevationMeters; }

    /** Smallest radius any terrain vertex can reach. */
    double GetMinRadiusMeters() const { return RadiusMeters - MaxDepthMeters; }

    /** Total relief, peak to trough, in metres. */
    double GetElevationRangeMeters() const { return MaxElevationMeters + MaxDepthMeters; }

    bool HasAtmosphere() const { return AtmosphereHeightMeters > 0.0; }

    /** Distance from the planet centre to the top of the atmosphere, in metres. */
    double GetAtmosphereTopRadiusMeters() const { return RadiusMeters + AtmosphereHeightMeters; }

    /**
     * Builds the terrain view of a planet from Sprint 001's astronomical
     * descriptor. The single conversion point between the two layers.
     */
    static FPlanetSurfaceDescriptor FromGeneratedPlanet(
        const FStarSystemDescriptor& System,
        int32 InOrbitIndex);

    static constexpr int32 SerializedSizeBytes =
        FUniverseSystemId::SerializedSizeBytes + 4 + 8 + 8
        + FUniversePosition::SerializedSizeBytes + 8 + 8 + 8 + 8 + 8 + 4;

    void Serialize(FUniverseByteWriter& Writer) const;
    bool Deserialize(FUniverseByteReader& Reader);

    /** Stable hash of everything that affects terrain shape. */
    uint64 GetContentHash() const;

    FString ToDebugString() const;
};
