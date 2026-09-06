// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "UniverseCoreMinimal.h"
#include "UniverseCoordinates.h"
#include "UniverseSeed.h"
#include "StarSystemDescriptor.h"

/**
 * InterstellarTravel.h
 *
 * Moving through the universe at speeds where a single frame is longer than a
 * solar system.
 *
 *
 * THE PROBLEM THIS SOLVES
 *
 * `NewPosition = OldPosition + Velocity * DeltaTime` is correct arithmetic and
 * a completely inadequate movement model here, for two separate reasons.
 *
 * The first is collision. At interstellar speed a frame covers more distance
 * than the diameter of a star, so a ship does not pass *through* a star so much
 * as skip over it: it is on one side at the start of the frame and clean past
 * at the end, never once occupying the same space. Any collision system that
 * looks at where things *are* reports nothing. The fix is to intersect the
 * swept segment analytically, which is what FTravelBroadPhase does.
 *
 * The second is arrival. A ship closing at 10^14 m/s that begins braking when
 * it is "close" has already passed its destination by several light hours. The
 * decision to decelerate has to be made from the braking distance implied by
 * the current speed, not from proximity - which is what
 * GetBrakingDistanceMeters and ShouldBeginBraking exist for.
 *
 *
 * ONE STATE, MANY REGIMES
 *
 * There is exactly one canonical ship position and one canonical velocity. The
 * travel *mode* changes what the controls do - how hard it accelerates, what it
 * is allowed to reach - and changes nothing about where the ship is. A second
 * position that applies "while warping" would be a second source of truth, and
 * the point where the two disagree is the point where a player watches their
 * ship jump.
 *
 * Mode is derived, not stored as authority: SelectMode is a pure function of
 * where the ship is relative to the nearest body. A ship near a planet is in a
 * local regime because it is near a planet, not because something set a flag
 * and forgot to clear it.
 *
 *
 * UNITS
 *
 * Position is canonical FUniversePosition. Velocity is metres per second in
 * universe axes - not centimetres, and not Unreal's render frame, both of which
 * would silently overflow single precision long before warp speed. Everything
 * here is double.
 *
 * Pure mathematics on plain data. Runs standalone and on worker threads.
 */

/**
 * The travel regimes.
 *
 * Ordered by scale, and that order is meaningful: SelectMode returns the
 * smallest regime that applies, and a caller may compare modes to ask "am I at
 * least at interstellar scale".
 */
enum class EUniverseTravelMode : uint8
{
    /** On or just above a planet's surface. Chaos owns movement here. */
    Surface = 0,

    /** Near a planet, in orbit or manoeuvring. Metres to planetary radii. */
    LocalSpace = 1,

    /** Between the bodies of one system. AU. */
    Interplanetary = 2,

    /** Between systems, under normal drive. Light hours to light days. */
    Interstellar = 3,

    /** Faster than light. Light years. */
    Warp = 4,
};

UNIVERSEGENERATION_API const TCHAR* LexToString(EUniverseTravelMode Mode);

/**
 * Speed and acceleration limits, per regime.
 *
 * Every number here is a gameplay decision rather than a physical one, which is
 * exactly why they live in one struct rather than scattered through the pawn as
 * literals. The sprint's requirement that future civilisation technology can
 * raise the maximum speed is met by this being data: nothing in the travel
 * mathematics knows what the numbers are.
 */
struct UNIVERSEGENERATION_API FTravelProfile
{
    // --- Speed limits, metres per second ------------------------------------

    /** Manoeuvring near a body. 10 km/s is low orbit. */
    double LocalMaxSpeedMs = 1.0e4;

    /** Crossing a system. 0.01 c crosses 1 AU in about eight minutes. */
    double InterplanetaryMaxSpeedMs = 3.0e6;

    /** Sublight cruise. 0.5 c, which reaches the nearest star in eight years. */
    double InterstellarMaxSpeedMs = 1.5e8;

    /**
     * Warp. 10^15 m/s is about 3.3 million c, which crosses four light years in
     * roughly forty seconds.
     *
     * That is absurd as physics and correct as game design: the alternative is
     * a loading screen, and the sprint is explicit that the player is to be
     * physically moving through the universe rather than selecting a
     * destination from a menu.
     */
    double WarpMaxSpeedMs = 1.0e15;

    // --- Accelerations, metres per second squared ---------------------------

    double LocalAccelerationMs2 = 500.0;
    double InterplanetaryAccelerationMs2 = 2.0e5;
    double InterstellarAccelerationMs2 = 1.0e7;

    /**
     * Warp acceleration and deceleration.
     *
     * Deceleration is deliberately the larger of the two. A ship that cannot
     * stop faster than it can start is a ship that overshoots every target it
     * ever aims at, and the resulting "fly back and try again" is not
     * interesting.
     */
    double WarpAccelerationMs2 = 5.0e13;
    double WarpDecelerationMs2 = 1.0e14;

    // --- Regime boundaries, metres ------------------------------------------

    /** Within this of a planet's surface, movement is Chaos's problem. */
    double SurfaceAltitudeMeters = 1.0e5;

    /** Within this of a body centre, the regime is LocalSpace. */
    double LocalSpaceRangeMeters = 1.0e9;

    /** Within this of a star, the regime is at most Interplanetary. */
    double InterplanetaryRangeMeters = 1.0e13;

    // --- Safety -------------------------------------------------------------

    /**
     * How many body radii of standoff the trajectory check enforces.
     *
     * Three rather than one: stopping exactly at a star's surface is stopping
     * inside its corona, and a margin costs nothing when the alternative is
     * being told about the collision after it has happened.
     */
    double HazardStandoffRadii = 3.0;

    /** Absolute floor on that standoff, for bodies small enough that radii are not enough. */
    double MinimumHazardStandoffMeters = 1.0e7;

    /**
     * Extra braking distance beyond the arithmetic minimum, as a fraction.
     *
     * The arithmetic minimum assumes deceleration begins on exactly the right
     * frame. It does not: the decision is made at frame boundaries, and at warp
     * one frame of delay is light minutes. A twenty percent margin absorbs that
     * without making arrival feel sluggish.
     */
    double BrakingMarginFraction = 0.2;

    bool IsValid() const;

    static FTravelProfile MakeDefault() { return FTravelProfile(); }

    double GetMaxSpeedMs(EUniverseTravelMode Mode) const;
    double GetAccelerationMs2(EUniverseTravelMode Mode) const;
    double GetDecelerationMs2(EUniverseTravelMode Mode) const;
};

/**
 * The one canonical movement state.
 *
 * Position is where the ship is, in the coordinate system everything else in
 * the engine agrees on. Velocity is how it is moving, in metres per second in
 * universe axes. Mode is a derived label carried along so that consumers - the
 * HUD, the streamer - do not each have to re-derive it.
 */
struct UNIVERSEGENERATION_API FTravelState
{
    FUniversePosition Position;

    FVector3d VelocityMs = FVector3d::ZeroVector;

    EUniverseTravelMode Mode = EUniverseTravelMode::LocalSpace;

    double GetSpeedMs() const { return VelocityMs.Size(); }

    /** Unit heading, or the zero vector when stationary. */
    FVector3d GetHeading() const { return VelocityMs.GetSafeNormal(); }
};

/** Where the ship is trying to get to. Optional: manual travel needs no target. */
struct UNIVERSEGENERATION_API FTravelTarget
{
    bool bValid = false;

    FUniversePosition Position;

    /** Identity of the target system, when the target is one. */
    FUniverseSystemId SystemId;

    /**
     * How close counts as arrived, in metres.
     *
     * A radius rather than a point because at these speeds a point is
     * unreachable: the smallest representable step at warp is larger than any
     * tolerance worth naming.
     */
    double ArrivalRadiusMeters = 1.0e11;

    FString Name;

    bool IsValid() const { return bValid; }
};

/** What a swept path ran into. */
struct UNIVERSEGENERATION_API FTravelHazard
{
    bool bHit = false;

    /** The system the body belongs to. */
    FUniverseSystemId SystemId;

    /** Index into that system's planets, or INDEX_NONE for the star itself. */
    int32 PlanetIndex = INDEX_NONE;

    /** Fraction along the swept segment where contact happens, in [0, 1]. */
    double Fraction = 1.0;

    /** Distance from the segment start to the contact point, in metres. */
    double DistanceMeters = 0.0;

    /** The body's centre. */
    FUniversePosition BodyPosition;

    /** The standoff sphere that was actually swept against, in metres. */
    double StandoffRadiusMeters = 0.0;

    /** Closest the segment came to that body's centre, in metres. */
    double ClosestApproachMeters = 0.0;

    /** True if the segment started already inside the standoff sphere. */
    bool bStartedInside = false;
};

/** The outcome of one movement step. */
struct UNIVERSEGENERATION_API FTravelStepResult
{
    FTravelState State;

    /** How far the ship actually moved, in metres. */
    double DistanceMovedMeters = 0.0;

    /** True if the step was shortened to avoid a body. */
    bool bClampedByHazard = false;

    /** What it was shortened for. Meaningful only when bClampedByHazard. */
    FTravelHazard Hazard;

    /** True if the ship is inside the target's arrival radius. */
    bool bArrived = false;

    /**
     * True if the step was shortened so as not to carry the ship past its
     * target.
     *
     * Distinct from bBraking, and needed as well as it. Braking is a
     * continuous law and it is correct in the limit; the limit is not where the
     * ship lives. A frame is a discrete decision, and on the last few frames of
     * an approach the ship is still doing 10^12 m/s inside an arrival radius of
     * 10^11 metres - it enters the radius correctly and leaves it again before
     * anything can act. Clamping the step to the point of closest approach
     * makes overshoot impossible at any frame rate, which no tuning of the
     * braking constants can.
     */
    bool bArrivalClamped = false;

    /** True if auto-braking took over the throttle this step. */
    bool bBraking = false;

    /** Braking distance implied by the speed at the start of the step. */
    double BrakingDistanceMeters = 0.0;

    /** Distance remaining to the target, or zero when there is no target. */
    double DistanceToTargetMeters = 0.0;
};

/**
 * A galactic sector address.
 *
 * Three int64s and nothing else. It exists so that "the sectors this path
 * crosses" can be returned as an array rather than three parallel ones, which
 * is the kind of thing that stays correct right up until somebody sorts one of
 * them.
 */
struct UNIVERSEGENERATION_API FUniverseSectorCoord
{
    int64 X = 0;
    int64 Y = 0;
    int64 Z = 0;

    FUniverseSectorCoord() = default;
    FUniverseSectorCoord(int64 InX, int64 InY, int64 InZ) : X(InX), Y(InY), Z(InZ) {}

    bool operator==(const FUniverseSectorCoord& Other) const
    {
        return X == Other.X && Y == Other.Y && Z == Other.Z;
    }

    bool operator!=(const FUniverseSectorCoord& Other) const { return !(*this == Other); }

    /** The sector containing a position. */
    static FUniverseSectorCoord FromPosition(const FUniversePosition& Position);
};

FORCEINLINE uint32 GetTypeHash(const FUniverseSectorCoord& Sector)
{
    const uint64 Hash = UniverseHash::Hash(
        UniverseHash::Hash(static_cast<uint64>(Sector.X), static_cast<uint64>(Sector.Y)),
        static_cast<uint64>(Sector.Z));

    return static_cast<uint32>(Hash ^ (Hash >> 32));
}

/**
 * Which sectors a path crosses, and what is in them.
 *
 * The sprint is explicit that this must not be a loop over every object in the
 * galaxy, and it is not: cost scales with the length of the path, because that
 * is what determines how many sectors it passes through. A frame-long step at
 * any speed below about five light years per frame touches a single-digit
 * number of sectors.
 */
class UNIVERSEGENERATION_API FTravelBroadPhase
{
public:
    /**
     * Every sector the segment passes through, in order.
     *
     * A 3D digital differential analyser over the sector grid - the same
     * algorithm a voxel raycast uses - so it visits exactly the sectors the
     * segment enters and no others. Bounded by MaxSectors, because a path long
     * enough to cross more than that is one the caller should be substepping.
     * Returns false when the bound was hit, with the sectors found so far.
     */
    static bool GetSectorsCrossed(
        const FUniversePosition& From,
        const FUniversePosition& To,
        TArray<FUniverseSectorCoord>& OutSectors,
        int32 MaxSectors = 512);

    /**
     * The first body the segment would hit, with the profile's standoff applied.
     *
     * Candidate systems come from the sectors crossed, dilated by one sector in
     * each direction: a system's bodies orbit up to about 10^13 metres from
     * their star, which is small against a 4.87 light year sector but not zero,
     * so a star just outside a crossed sector can still put a planet inside it.
     *
     * Returns false when the path is clear.
     */
    static bool SweepAgainstBodies(
        const FUniverseSeedHierarchy& Hierarchy,
        const FTravelProfile& Profile,
        const FUniversePosition& From,
        const FUniversePosition& To,
        FTravelHazard& OutHazard,
        int32 MaxSectors = 512);

    /**
     * Distance to the nearest body centre, for regime selection.
     *
     * Returns false - and leaves the output alone - when there is nothing
     * within SearchRadiusMeters, which in interstellar space is the common
     * case and is not an error.
     */
    static bool FindNearestBodyDistance(
        const FUniverseSeedHierarchy& Hierarchy,
        const FUniversePosition& Position,
        double SearchRadiusMeters,
        double& OutDistanceMeters);
};

class UNIVERSEGENERATION_API FInterstellarTravel
{
public:
    /**
     * How far it takes to stop from a speed, at a deceleration.
     *
     * v^2 / 2a. The whole of the arrival problem is this one expression applied
     * before the fact rather than after it.
     */
    static double GetBrakingDistanceMeters(double SpeedMs, double DecelerationMs2);

    /**
     * Whether deceleration has to begin now to stop by the target.
     *
     * Takes the margin fraction rather than a distance because the amount of
     * slack needed scales with the braking distance, which spans ten orders of
     * magnitude between manoeuvring and warp.
     */
    static bool ShouldBeginBraking(
        double DistanceToTargetMeters,
        double SpeedMs,
        double DecelerationMs2,
        double MarginFraction);

    /**
     * The regime that applies at a distance from the nearest body.
     *
     * Pure and derived. Pass a negative distance to mean "nothing nearby".
     */
    static EUniverseTravelMode SelectMode(
        const FTravelProfile& Profile,
        double NearestBodyDistanceMeters,
        double NearestBodyRadiusMeters,
        bool bWarpEngaged);

    /**
     * Advances the state by one step.
     *
     * ThrustDirection is a unit vector in universe axes; Throttle is in
     * [-1, 1], where negative means retro-thrust along the heading. When
     * bAutoBrakeToTarget is set and a valid target is present, the throttle is
     * overridden the moment the braking distance says it must be.
     *
     * The step is swept against the astronomical broad phase and clamped short
     * of anything in the way. That clamp is the only thing standing between a
     * warp jump and flying through a star, so it is not optional and there is
     * no unswept variant of this function.
     *
     * With bAutoBrakeToTarget set, the step is additionally clamped so that it
     * cannot carry the ship past its target - see FTravelStepResult's
     * bArrivalClamped. That guarantee holds for any DeltaSeconds, including the
     * multi-second hitch that a level load produces, which is the case the
     * braking law alone cannot cover.
     */
    static FTravelStepResult Step(
        const FUniverseSeedHierarchy& Hierarchy,
        const FTravelProfile& Profile,
        const FTravelState& State,
        const FTravelTarget& Target,
        const FVector3d& ThrustDirection,
        double Throttle,
        double DeltaSeconds,
        bool bWarpEngaged,
        bool bAutoBrakeToTarget);

    /** Estimated time to reach a target, accounting for acceleration and braking. */
    static double EstimateTravelTimeSeconds(
        const FTravelProfile& Profile,
        double DistanceMeters,
        EUniverseTravelMode Mode);

    /** A distance rendered in whatever unit makes it readable. */
    static FString FormatDistance(double Meters);

    /** A speed rendered likewise, with a multiple of c once past about 0.01 c. */
    static FString FormatSpeed(double MetersPerSecond);
};
