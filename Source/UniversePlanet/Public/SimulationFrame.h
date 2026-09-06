// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "PlanetSurface.h"

/**
 * SimulationFrame.h
 *
 * Which body the simulation currently considers the player to be *at*.
 *
 *
 * THE PROBLEM THIS SOLVES
 *
 * Sprint 002 finished with two render spaces that did not meet. Astronomical
 * bodies are drawn in a uniformly scaled model of the system, at 10^-7, because
 * a solar system does not fit in float coordinates. Planet terrain is drawn at
 * true scale, 1:1, because a player has to be able to stand on it. Both were
 * correct in isolation and there was no defined moment at which one gave way to
 * the other, so anything that crossed between them - a light, a velocity, a
 * position - was silently reinterpreted at a factor of ten million. That
 * already produced one real bug, a star lighting terrain at 68 lux from orbit
 * and 950,000 lux from four kilometres up, and it would have produced more.
 *
 * The fix is not a smarter conversion. It is to make the frame an explicit,
 * named piece of state that everything can ask about, so that "which space is
 * this number in" has an answer rather than being inferred from context by each
 * caller separately.
 *
 *
 * THE FRAMES
 *
 *   Interstellar   No body dominates. Positions are universe coordinates,
 *                  bodies are drawn scaled, and there is no gravity, no ground
 *                  and no up. This is the default and covers the overwhelming
 *                  majority of the universe by volume.
 *
 *   Planetary      One planet dominates. Its terrain is drawn at true scale
 *                  around the observer, gravity points at its centre, "up" is
 *                  defined, and altitude means something.
 *
 * Two, not three. A separate "system" frame is tempting and was considered, but
 * it would be a frame with no distinct behaviour: near a star and far from any
 * planet, the simulation does exactly what it does between stars. A frame that
 * changes nothing is a name to keep in sync, not an abstraction.
 *
 *
 * WHY THE TRANSITION HAS HYSTERESIS
 *
 * A single threshold makes the frame a function of position, and position is
 * noisy: the boundary is a sphere, an orbiting craft skims along it, and each
 * frame it lands on a different side. Switching frames is not free - it rebases
 * the render origin, changes the gravity model, and starts and stops terrain
 * streaming - so a craft parked on the boundary would do all of that several
 * times a second, permanently.
 *
 * So there are two radii. The frame is entered at EnterRadius and only left at
 * ExitRadius, which is 50% further out. The state depends on the *history* of
 * the crossing rather than only on where the observer is now, which is the
 * whole point: inside the gap, both answers are stable, and which one applies
 * is decided by which way the observer was going when they crossed. The same
 * device is already used for LOD splits in FPlanetQuadtreeSelector, and for the
 * same reason.
 *
 * Pure mathematics on plain data. No UObject, no Actor, no World.
 */
enum class EUniverseFrameKind : uint8
{
    /** Deep space, or a system with no planet close enough to matter. */
    Interstellar = 0,

    /** Attached to a planet: gravity, ground, up and altitude all exist. */
    Planetary = 1,
};

UNIVERSEPLANET_API const TCHAR* LexToString(EUniverseFrameKind Kind);

/**
 * The two radii at which a planet claims and releases the observer, measured
 * from the planet centre in metres.
 */
struct UNIVERSEPLANET_API FPlanetFrameBounds
{
    double PlanetRadiusMeters = 0.0;
    double AtmosphereHeightMeters = 0.0;

    /** Distance from the centre at which the planetary frame is entered. */
    double EnterRadiusMeters = 0.0;

    /** Distance at which it is left. Always greater than EnterRadiusMeters. */
    double ExitRadiusMeters = 0.0;

    /**
     * How much further out the frame is released than it is claimed.
     *
     * 1.5 gives a band half a planet-influence-radius thick. At Earth scale
     * that is about 9,500 km, which a craft in low orbit at 7.8 km/s takes
     * twenty minutes to cross even flying straight through it. The band has to
     * be wide in *time*, not in metres, because what it is protecting against
     * is a rapid oscillation; a band a craft can cross in a frame protects
     * nothing.
     */
    static constexpr double ExitRadiusMultiplier = 1.5;

    /**
     * Influence radius as a multiple of the planet radius.
     *
     * Three radii, so the frame is entered well before the planet is close
     * enough for its terrain to need streaming or its gravity to be
     * appreciable - at 3R, gravity is already down to 11% of surface. Entering
     * early is nearly free and gives the streaming prewarm time to work;
     * entering late means arriving somewhere with no ground built yet.
     */
    static constexpr double InfluenceRadiiOfPlanet = 3.0;

    /**
     * Minimum influence radius as a multiple of the atmosphere height above
     * the surface, for bodies with a thick atmosphere relative to their size.
     */
    static constexpr double InfluenceAtmosphereMultiplier = 10.0;

    static FPlanetFrameBounds FromPlanet(const FPlanetSurfaceDescriptor& Planet);

    bool IsValid() const { return PlanetRadiusMeters > 0.0 && ExitRadiusMeters > EnterRadiusMeters; }
};

/**
 * One planet offered to the selector this tick, with the observer distance
 * already measured.
 *
 * The distance is passed in rather than computed here because the caller holds
 * the universe positions and this module deliberately does not know what a
 * universe position is.
 */
struct UNIVERSEPLANET_API FPlanetFrameCandidate
{
    uint64 PlanetKey = 0;
    double DistanceFromCentreMeters = 0.0;
    FPlanetFrameBounds Bounds;

    /**
     * Distance as a fraction of the enter radius: below 1 means close enough
     * to claim the observer, and lower means a stronger claim.
     *
     * A ratio rather than a raw distance, so that a nearby moon and a distant
     * gas giant are compared on how much each dominates the observer's
     * surroundings rather than on kilometres, which would always hand the
     * observer to whichever body happened to be physically nearer even when
     * they are standing on the other one.
     */
    double GetDominance() const;

    bool IsValid() const { return PlanetKey != 0 && Bounds.IsValid(); }
};

/** What frame the simulation is in, and because of which body. */
struct UNIVERSEPLANET_API FUniverseFrameState
{
    EUniverseFrameKind Kind = EUniverseFrameKind::Interstellar;

    /** The dominant planet, when Kind is Planetary. Zero otherwise. */
    uint64 PlanetKey = 0;

    bool IsPlanetary() const { return Kind == EUniverseFrameKind::Planetary; }

    bool operator==(const FUniverseFrameState& Other) const
    {
        return Kind == Other.Kind && PlanetKey == Other.PlanetKey;
    }

    bool operator!=(const FUniverseFrameState& Other) const { return !(*this == Other); }
};

/**
 * Picks the frame, and remembers the previous one so hysteresis can work.
 *
 * Stateful for exactly the reason FPlanetQuadtreeSelector is: a threshold with
 * memory is not a function of the present, so it cannot be a free function
 * however much tidier that would look.
 */
class UNIVERSEPLANET_API FSimulationFrameSelector
{
public:
    /**
     * How much more dominant a rival planet must be before the observer is
     * handed over to it without first leaving the current one.
     *
     * Only reachable where two bodies overlap - a moon inside its planet's
     * influence radius. Requiring twice the dominance means the boundary
     * between them is itself hysteretic, so a craft flying between a planet
     * and its moon changes frame once each way rather than repeatedly in the
     * overlap.
     */
    static constexpr double SwitchDominanceAdvantage = 0.5;

    /**
     * Offers the candidates for this tick. Returns true if the frame changed,
     * which is the signal for a caller to rebase, re-parent or restart
     * streaming.
     */
    bool Update(const FPlanetFrameCandidate* Candidates, int32 Count);

    /** Convenience for the common single-planet case. */
    bool Update(const FPlanetFrameCandidate& Candidate);

    /** Drops to Interstellar and forgets the history. */
    void Reset();

    const FUniverseFrameState& GetState() const { return State; }
    EUniverseFrameKind GetKind() const { return State.Kind; }
    uint64 GetPlanetKey() const { return State.PlanetKey; }

    /** Dominance of the attached planet at the last update, for the HUD. */
    double GetDominance() const { return Dominance; }

    /** How many times the frame has changed. Diagnostic; catches flapping. */
    int32 GetTransitionCount() const { return TransitionCount; }

private:
    FUniverseFrameState State;
    double Dominance = 0.0;
    int32 TransitionCount = 0;
};
