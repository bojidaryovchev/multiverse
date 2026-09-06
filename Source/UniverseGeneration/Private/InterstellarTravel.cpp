// Copyright Universe Project. All Rights Reserved.

#include "InterstellarTravel.h"
#include "StarSystemGenerator.h"
#include "UniverseScale.h"
#include "UniverseSweep.h"

namespace
{
    /**
     * The largest distance a body can be from its star, in metres.
     *
     * Used to dilate the broad phase. Generous on purpose: it is a bound rather
     * than a measurement, and a bound that is too tight produces a ship flying
     * through a planet on the one frame the bound was wrong.
     */
    constexpr double SystemExtentMeters = 2.0e13;

    /** A universe position offset by a vector in metres. */
    FUniversePosition OffsetByMeters(const FUniversePosition& Position, const FVector3d& Meters)
    {
        return Position.OffsetByCm(FVector3d(
            Meters.X * UniverseScale::CmPerMeter,
            Meters.Y * UniverseScale::CmPerMeter,
            Meters.Z * UniverseScale::CmPerMeter));
    }

    /** From -> To in metres, in universe axes, correct at any separation. */
    FVector3d GetRelativeMeters(const FUniversePosition& From, const FUniversePosition& To)
    {
        // Via cell space, which keeps full relative precision no matter how far
        // apart the two are. Going through centimetres would fail beyond about
        // 0.01 light years, which is inside a single frame of warp.
        const FVector3d RelativeCells = FUniversePosition::GetRelativeCells(From, To);
        const double MetersPerCell = UniverseScale::MetersPerCell;

        return FVector3d(
            RelativeCells.X * MetersPerCell,
            RelativeCells.Y * MetersPerCell,
            RelativeCells.Z * MetersPerCell);
    }

    /** The standoff sphere for a body, in metres. */
    double GetStandoffMeters(const FTravelProfile& Profile, double BodyRadiusMeters)
    {
        return FMath::Max(
            BodyRadiusMeters * Profile.HazardStandoffRadii,
            Profile.MinimumHazardStandoffMeters);
    }
}

const TCHAR* LexToString(EUniverseTravelMode Mode)
{
    switch (Mode)
    {
    case EUniverseTravelMode::Surface:        return TEXT("Surface");
    case EUniverseTravelMode::LocalSpace:     return TEXT("LocalSpace");
    case EUniverseTravelMode::Interplanetary: return TEXT("Interplanetary");
    case EUniverseTravelMode::Interstellar:   return TEXT("Interstellar");
    case EUniverseTravelMode::Warp:           return TEXT("Warp");
    default:                                  return TEXT("Unknown");
    }
}

// ---------------------------------------------------------------------------
// FUniverseSectorCoord
// ---------------------------------------------------------------------------

FUniverseSectorCoord FUniverseSectorCoord::FromPosition(const FUniversePosition& Position)
{
    int64 X = 0;
    int64 Y = 0;
    int64 Z = 0;
    Position.GetSector(X, Y, Z);

    return FUniverseSectorCoord(X, Y, Z);
}

// ---------------------------------------------------------------------------
// FTravelProfile
// ---------------------------------------------------------------------------

bool FTravelProfile::IsValid() const
{
    // Monotonic speed limits. A profile where interplanetary is faster than
    // warp is not a balance choice, it is a typo, and it would make SelectMode
    // produce a slower ship the further out it went.
    if (!(LocalMaxSpeedMs > 0.0
        && InterplanetaryMaxSpeedMs > LocalMaxSpeedMs
        && InterstellarMaxSpeedMs > InterplanetaryMaxSpeedMs
        && WarpMaxSpeedMs > InterstellarMaxSpeedMs))
    {
        return false;
    }

    if (!(LocalAccelerationMs2 > 0.0
        && InterplanetaryAccelerationMs2 > 0.0
        && InterstellarAccelerationMs2 > 0.0
        && WarpAccelerationMs2 > 0.0
        && WarpDecelerationMs2 > 0.0))
    {
        return false;
    }

    if (!(SurfaceAltitudeMeters > 0.0
        && LocalSpaceRangeMeters > SurfaceAltitudeMeters
        && InterplanetaryRangeMeters > LocalSpaceRangeMeters))
    {
        return false;
    }

    return HazardStandoffRadii >= 1.0
        && MinimumHazardStandoffMeters > 0.0
        && BrakingMarginFraction >= 0.0;
}

double FTravelProfile::GetMaxSpeedMs(EUniverseTravelMode Mode) const
{
    switch (Mode)
    {
    case EUniverseTravelMode::Surface:        return LocalMaxSpeedMs;
    case EUniverseTravelMode::LocalSpace:     return LocalMaxSpeedMs;
    case EUniverseTravelMode::Interplanetary: return InterplanetaryMaxSpeedMs;
    case EUniverseTravelMode::Interstellar:   return InterstellarMaxSpeedMs;
    case EUniverseTravelMode::Warp:           return WarpMaxSpeedMs;
    default:                                  return LocalMaxSpeedMs;
    }
}

double FTravelProfile::GetAccelerationMs2(EUniverseTravelMode Mode) const
{
    switch (Mode)
    {
    case EUniverseTravelMode::Surface:        return LocalAccelerationMs2;
    case EUniverseTravelMode::LocalSpace:     return LocalAccelerationMs2;
    case EUniverseTravelMode::Interplanetary: return InterplanetaryAccelerationMs2;
    case EUniverseTravelMode::Interstellar:   return InterstellarAccelerationMs2;
    case EUniverseTravelMode::Warp:           return WarpAccelerationMs2;
    default:                                  return LocalAccelerationMs2;
    }
}

double FTravelProfile::GetDecelerationMs2(EUniverseTravelMode Mode) const
{
    // Every regime but warp decelerates as hard as it accelerates - the drive
    // simply points the other way. Warp is the exception because it has to be:
    // see the profile's comment.
    return (Mode == EUniverseTravelMode::Warp)
        ? WarpDecelerationMs2
        : GetAccelerationMs2(Mode);
}

// ---------------------------------------------------------------------------
// FTravelBroadPhase
// ---------------------------------------------------------------------------

bool FTravelBroadPhase::GetSectorsCrossed(
    const FUniversePosition& From,
    const FUniversePosition& To,
    TArray<FUniverseSectorCoord>& OutSectors,
    int32 MaxSectors)
{
    OutSectors.Reset();

    if (MaxSectors <= 0)
    {
        return false;
    }

    const FUniverseSectorCoord StartSector = FUniverseSectorCoord::FromPosition(From);

    OutSectors.Add(StartSector);

    // --- Set up the traversal in sector space -------------------------------
    //
    // Everything below works in fractional sectors relative to the start
    // sector's corner, so the numbers are small and a double is exact enough
    // for a traversal that only ever needs to know which side of a boundary it
    // is on.
    const double CellsPerSector = static_cast<double>(UniverseScale::SectorSizeInCells);

    const FVector3d DeltaCells = FUniversePosition::GetRelativeCells(From, To);

    const FVector3d Delta(
        DeltaCells.X / CellsPerSector,
        DeltaCells.Y / CellsPerSector,
        DeltaCells.Z / CellsPerSector);

    if (Delta.IsNearlyZero())
    {
        return true;
    }

    // Where the start point sits inside its own sector, in [0, 1) per axis.
    const int64 StartCellX = StartSector.X * UniverseScale::SectorSizeInCells;
    const int64 StartCellY = StartSector.Y * UniverseScale::SectorSizeInCells;
    const int64 StartCellZ = StartSector.Z * UniverseScale::SectorSizeInCells;

    const FVector3d Origin(
        static_cast<double>(From.CellX - StartCellX) / CellsPerSector,
        static_cast<double>(From.CellY - StartCellY) / CellsPerSector,
        static_cast<double>(From.CellZ - StartCellZ) / CellsPerSector);

    // --- Amanatides-Woo -----------------------------------------------------
    //
    // Standard voxel traversal: for each axis, how far along the segment the
    // next boundary is (TMax) and how far apart consecutive boundaries are
    // (TDelta). Step whichever axis has the nearest boundary. Visits exactly
    // the sectors the segment enters, in order, and nothing else.
    int64 Current[3] = { StartSector.X, StartSector.Y, StartSector.Z };

    const double DeltaAxis[3] = { Delta.X, Delta.Y, Delta.Z };
    const double OriginAxis[3] = { Origin.X, Origin.Y, Origin.Z };

    int64 StepAxis[3] = { 0, 0, 0 };
    double TMax[3] = { 0.0, 0.0, 0.0 };
    double TDelta[3] = { 0.0, 0.0, 0.0 };

    const double Infinity = TNumericLimits<double>::Max();

    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        if (DeltaAxis[Axis] > 0.0)
        {
            StepAxis[Axis] = 1;
            TMax[Axis] = (1.0 - OriginAxis[Axis]) / DeltaAxis[Axis];
            TDelta[Axis] = 1.0 / DeltaAxis[Axis];
        }
        else if (DeltaAxis[Axis] < 0.0)
        {
            StepAxis[Axis] = -1;
            TMax[Axis] = OriginAxis[Axis] / -DeltaAxis[Axis];
            TDelta[Axis] = 1.0 / -DeltaAxis[Axis];
        }
        else
        {
            StepAxis[Axis] = 0;
            TMax[Axis] = Infinity;
            TDelta[Axis] = Infinity;
        }
    }

    while (OutSectors.Num() < MaxSectors)
    {
        // The axis whose boundary comes first.
        int32 Nearest = 0;

        if (TMax[1] < TMax[Nearest])
        {
            Nearest = 1;
        }

        if (TMax[2] < TMax[Nearest])
        {
            Nearest = 2;
        }

        if (TMax[Nearest] > 1.0)
        {
            // The next boundary is past the end of the segment.
            return true;
        }

        Current[Nearest] += StepAxis[Nearest];
        TMax[Nearest] += TDelta[Nearest];

        OutSectors.Add(FUniverseSectorCoord(Current[0], Current[1], Current[2]));
    }

    // Ran out of budget. The sectors found so far are correct and are a prefix
    // of the true answer, which is what makes a partial result usable rather
    // than merely wrong.
    return false;
}

bool FTravelBroadPhase::SweepAgainstBodies(
    const FUniverseSeedHierarchy& Hierarchy,
    const FTravelProfile& Profile,
    const FUniversePosition& From,
    const FUniversePosition& To,
    FTravelHazard& OutHazard,
    int32 MaxSectors)
{
    OutHazard = FTravelHazard();

    TArray<FUniverseSectorCoord> Crossed;
    GetSectorsCrossed(From, To, Crossed, MaxSectors);

    if (Crossed.Num() == 0)
    {
        return false;
    }

    // --- Dilate by one sector ----------------------------------------------
    //
    // A system's bodies orbit up to about 2 x 10^13 metres from their star,
    // which is 0.04% of a sector - small, but not zero. A star just outside a
    // crossed sector can still put a planet inside it, so the candidate set is
    // the crossed sectors plus their neighbours. Deduplicated, because
    // consecutive crossed sectors share most of their neighbours and the naive
    // version would generate the same system twenty-seven times.
    TSet<FUniverseSectorCoord> Candidates;
    Candidates.Reserve(Crossed.Num() * 8);

    for (const FUniverseSectorCoord& Sector : Crossed)
    {
        for (int64 DZ = -1; DZ <= 1; ++DZ)
        {
            for (int64 DY = -1; DY <= 1; ++DY)
            {
                for (int64 DX = -1; DX <= 1; ++DX)
                {
                    Candidates.Add(FUniverseSectorCoord(Sector.X + DX, Sector.Y + DY, Sector.Z + DZ));
                }
            }
        }
    }

    const FVector3d SegmentStart = FVector3d::ZeroVector;
    const FVector3d SegmentEnd = GetRelativeMeters(From, To);

    double BestFraction = TNumericLimits<double>::Max();
    bool bFound = false;

    for (const FUniverseSectorCoord& Sector : Candidates)
    {
        const int32 Count =
            FStarSystemGenerator::GetSystemCountInSector(Hierarchy, Sector.X, Sector.Y, Sector.Z);

        for (int32 Index = 0; Index < Count; ++Index)
        {
            // --- Cheap rejection first --------------------------------------
            //
            // The system's position without generating the system. Full
            // generation builds a star and every planet, and the overwhelming
            // majority of candidates are nowhere near the path.
            const FUniversePosition SystemPosition =
                FStarSystemGenerator::GetSystemPosition(Hierarchy, Sector.X, Sector.Y, Sector.Z, Index);

            const FVector3d SystemCentre = GetRelativeMeters(From, SystemPosition);

            const FSegmentSphereResult Bounds = FUniverseSweep::SegmentSphereAt(
                SegmentStart, SegmentEnd, SystemCentre, SystemExtentMeters);

            if (!Bounds.bHit && Bounds.ClosestApproach > SystemExtentMeters)
            {
                continue;
            }

            FStarSystemDescriptor System;

            if (!FStarSystemGenerator::GenerateSystem(
                    Hierarchy, Sector.X, Sector.Y, Sector.Z, Index, System))
            {
                continue;
            }

            // --- The star ---------------------------------------------------
            {
                const double Standoff = GetStandoffMeters(Profile, System.Star.RadiusMeters);

                const FSegmentSphereResult Sweep = FUniverseSweep::SegmentSphereAt(
                    SegmentStart, SegmentEnd, SystemCentre, Standoff);

                if (Sweep.bHit && Sweep.EntryFraction < BestFraction)
                {
                    BestFraction = Sweep.EntryFraction;
                    bFound = true;

                    OutHazard.bHit = true;
                    OutHazard.SystemId = System.Id;
                    OutHazard.PlanetIndex = INDEX_NONE;
                    OutHazard.Fraction = Sweep.EntryFraction;
                    OutHazard.BodyPosition = System.Position;
                    OutHazard.StandoffRadiusMeters = Standoff;
                    OutHazard.ClosestApproachMeters = Sweep.ClosestApproach;
                    OutHazard.bStartedInside = Sweep.bStartedInside;
                }
            }

            // --- The planets ------------------------------------------------
            for (int32 PlanetIndex = 0; PlanetIndex < System.Planets.Num(); ++PlanetIndex)
            {
                const FPlanetDescriptor& Planet = System.Planets[PlanetIndex];

                const FUniversePosition PlanetPosition =
                    FStarSystemGenerator::GetPlanetPosition(System, Planet);

                const FVector3d PlanetCentre = GetRelativeMeters(From, PlanetPosition);

                const double Standoff = GetStandoffMeters(Profile, Planet.RadiusMeters);

                const FSegmentSphereResult Sweep = FUniverseSweep::SegmentSphereAt(
                    SegmentStart, SegmentEnd, PlanetCentre, Standoff);

                if (Sweep.bHit && Sweep.EntryFraction < BestFraction)
                {
                    BestFraction = Sweep.EntryFraction;
                    bFound = true;

                    OutHazard.bHit = true;
                    OutHazard.SystemId = System.Id;
                    OutHazard.PlanetIndex = PlanetIndex;
                    OutHazard.Fraction = Sweep.EntryFraction;
                    OutHazard.BodyPosition = PlanetPosition;
                    OutHazard.StandoffRadiusMeters = Standoff;
                    OutHazard.ClosestApproachMeters = Sweep.ClosestApproach;
                    OutHazard.bStartedInside = Sweep.bStartedInside;
                }
            }
        }
    }

    if (bFound)
    {
        OutHazard.DistanceMeters = SegmentEnd.Size() * OutHazard.Fraction;
    }

    return bFound;
}

bool FTravelBroadPhase::FindNearestBodyDistance(
    const FUniverseSeedHierarchy& Hierarchy,
    const FUniversePosition& Position,
    double SearchRadiusMeters,
    double& OutDistanceMeters)
{
    if (SearchRadiusMeters <= 0.0)
    {
        return false;
    }

    const double SearchRadiusLy = SearchRadiusMeters / UniverseScale::MetersPerLightYear;

    TArray<FStarSystemDescriptor> Systems;
    FStarSystemGenerator::FindSystemsWithin(Hierarchy, Position, SearchRadiusLy, Systems, 16);

    double Best = TNumericLimits<double>::Max();
    bool bFound = false;

    for (const FStarSystemDescriptor& System : Systems)
    {
        const double ToStar = FUniversePosition::DistanceMeters(Position, System.Position);

        if (ToStar < Best)
        {
            Best = ToStar;
            bFound = true;
        }

        for (const FPlanetDescriptor& Planet : System.Planets)
        {
            const double ToPlanet = FUniversePosition::DistanceMeters(
                Position, FStarSystemGenerator::GetPlanetPosition(System, Planet));

            if (ToPlanet < Best)
            {
                Best = ToPlanet;
                bFound = true;
            }
        }
    }

    if (bFound)
    {
        OutDistanceMeters = Best;
    }

    return bFound;
}

// ---------------------------------------------------------------------------
// FInterstellarTravel
// ---------------------------------------------------------------------------

double FInterstellarTravel::GetBrakingDistanceMeters(double SpeedMs, double DecelerationMs2)
{
    if (DecelerationMs2 <= 0.0 || SpeedMs <= 0.0)
    {
        return 0.0;
    }

    return (SpeedMs * SpeedMs) / (2.0 * DecelerationMs2);
}

bool FInterstellarTravel::ShouldBeginBraking(
    double DistanceToTargetMeters,
    double SpeedMs,
    double DecelerationMs2,
    double MarginFraction)
{
    const double Required =
        GetBrakingDistanceMeters(SpeedMs, DecelerationMs2)
        * (1.0 + FMath::Max(MarginFraction, 0.0));

    return DistanceToTargetMeters <= Required;
}

EUniverseTravelMode FInterstellarTravel::SelectMode(
    const FTravelProfile& Profile,
    double NearestBodyDistanceMeters,
    double NearestBodyRadiusMeters,
    bool bWarpEngaged)
{
    // Warp is the one mode that is asserted rather than derived - it is a thing
    // the player switches on. Everything else follows from where the ship is.
    if (bWarpEngaged)
    {
        return EUniverseTravelMode::Warp;
    }

    if (NearestBodyDistanceMeters < 0.0)
    {
        // Nothing nearby at all: interstellar space.
        return EUniverseTravelMode::Interstellar;
    }

    const double Altitude = NearestBodyDistanceMeters - FMath::Max(NearestBodyRadiusMeters, 0.0);

    if (Altitude <= Profile.SurfaceAltitudeMeters)
    {
        return EUniverseTravelMode::Surface;
    }

    if (NearestBodyDistanceMeters <= Profile.LocalSpaceRangeMeters)
    {
        return EUniverseTravelMode::LocalSpace;
    }

    if (NearestBodyDistanceMeters <= Profile.InterplanetaryRangeMeters)
    {
        return EUniverseTravelMode::Interplanetary;
    }

    return EUniverseTravelMode::Interstellar;
}

FTravelStepResult FInterstellarTravel::Step(
    const FUniverseSeedHierarchy& Hierarchy,
    const FTravelProfile& Profile,
    const FTravelState& State,
    const FTravelTarget& Target,
    const FVector3d& ThrustDirection,
    double Throttle,
    double DeltaSeconds,
    bool bWarpEngaged,
    bool bAutoBrakeToTarget)
{
    FTravelStepResult Result;
    Result.State = State;

    if (!(DeltaSeconds > 0.0) || !Profile.IsValid())
    {
        return Result;
    }

    // --- Regime -------------------------------------------------------------
    //
    // Derived from where the ship is now, before anything moves. The search
    // radius is the interplanetary boundary: past that, nothing is close enough
    // to change the answer.
    double NearestBodyMeters = -1.0;
    (void)FTravelBroadPhase::FindNearestBodyDistance(
        Hierarchy, State.Position, Profile.InterplanetaryRangeMeters, NearestBodyMeters);

    const EUniverseTravelMode Mode = SelectMode(Profile, NearestBodyMeters, 0.0, bWarpEngaged);

    Result.State.Mode = Mode;

    const double MaxSpeed = Profile.GetMaxSpeedMs(Mode);
    const double Acceleration = Profile.GetAccelerationMs2(Mode);
    const double Deceleration = Profile.GetDecelerationMs2(Mode);

    // --- Target and braking -------------------------------------------------
    double DistanceToTarget = 0.0;
    FVector3d ToTarget = FVector3d::ZeroVector;

    if (Target.IsValid())
    {
        ToTarget = GetRelativeMeters(State.Position, Target.Position);
        DistanceToTarget = ToTarget.Size();

        Result.DistanceToTargetMeters = DistanceToTarget;
        Result.bArrived = DistanceToTarget <= Target.ArrivalRadiusMeters;
    }

    const double Speed = State.GetSpeedMs();

    Result.BrakingDistanceMeters = GetBrakingDistanceMeters(Speed, Deceleration);

    FVector3d AppliedDirection = ThrustDirection.GetSafeNormal();
    double AppliedThrottle = FMath::Clamp(Throttle, -1.0, 1.0);

    if (bAutoBrakeToTarget
        && Target.IsValid()
        && Speed > 0.0
        && ShouldBeginBraking(DistanceToTarget, Speed, Deceleration, Profile.BrakingMarginFraction))
    {
        // Full retro-thrust along the heading. Not along the vector to the
        // target: the ship may be closing on it sideways, and thrusting at the
        // target would then curve the path rather than slow it down.
        Result.bBraking = true;
        AppliedDirection = State.VelocityMs.GetSafeNormal() * -1.0;
        AppliedThrottle = 1.0;
    }

    // --- Integrate velocity -------------------------------------------------
    FVector3d Velocity = State.VelocityMs;

    if (!AppliedDirection.IsNearlyZero() && AppliedThrottle != 0.0)
    {
        const double Rate = Result.bBraking ? Deceleration : Acceleration;
        const double DeltaV = Rate * AppliedThrottle * DeltaSeconds;

        Velocity = Velocity + AppliedDirection * DeltaV;
    }

    // Braking overshoot: a step large enough to reverse the ship is a step that
    // should have stopped it. Without this the ship oscillates around its
    // target forever, accelerating backwards as hard as it just decelerated.
    if (Result.bBraking && FVector3d::DotProduct(Velocity, State.VelocityMs) < 0.0)
    {
        Velocity = FVector3d::ZeroVector;
    }

    // Clamp to the regime's limit.
    {
        const double NewSpeed = Velocity.Size();

        if (NewSpeed > MaxSpeed && NewSpeed > 0.0)
        {
            Velocity = Velocity * (MaxSpeed / NewSpeed);
        }
    }

    Result.State.VelocityMs = Velocity;

    // --- Integrate position, swept ------------------------------------------
    FVector3d StepMeters = Velocity * DeltaSeconds;

    if (StepMeters.IsNearlyZero())
    {
        return Result;
    }

    // --- Arrival clamp ------------------------------------------------------
    //
    // An auto-braked step may not carry the ship past its target, however long
    // the step is. The braking law above is correct in the limit and the limit
    // is not where the ship lives: on the last frames of a warp approach it is
    // still doing 10^12 m/s with an arrival radius of 10^11 metres, so it
    // enters the radius and leaves it again inside one frame. Clamping to the
    // point of closest approach removes overshoot as a possibility rather than
    // tuning it down, and it costs one dot product.
    if (bAutoBrakeToTarget && Target.IsValid())
    {
        const FVector3d ToTargetNow = GetRelativeMeters(State.Position, Target.Position);
        const double StepLengthSquared = StepMeters.SizeSquared();

        if (StepLengthSquared > 0.0)
        {
            const double ClosestFraction =
                FVector3d::DotProduct(ToTargetNow, StepMeters) / StepLengthSquared;

            if (ClosestFraction >= 0.0 && ClosestFraction < 1.0)
            {
                StepMeters = StepMeters * ClosestFraction;
                Result.bArrivalClamped = true;
            }
        }
    }

    const FUniversePosition Intended = OffsetByMeters(State.Position, StepMeters);

    FTravelHazard Hazard;

    if (FTravelBroadPhase::SweepAgainstBodies(
            Hierarchy, Profile, State.Position, Intended, Hazard)
        && !Hazard.bStartedInside)
    {
        // Stop short. The entry fraction is where the standoff sphere is first
        // touched, so stopping just inside it is stopping at the standoff
        // distance - which is already several body radii clear.
        Result.bClampedByHazard = true;
        Result.Hazard = Hazard;

        const double SafeFraction = FMath::Clamp(Hazard.Fraction, 0.0, 1.0);

        Result.State.Position = OffsetByMeters(State.Position, StepMeters * SafeFraction);
        Result.DistanceMovedMeters = StepMeters.Size() * SafeFraction;

        // Kill the velocity component heading into the body, and keep the rest.
        // Zeroing the whole velocity would bring a ship that merely grazed a
        // system to a dead stop, which is a worse experience than the collision
        // it is avoiding.
        const FVector3d ToBody =
            GetRelativeMeters(Result.State.Position, Hazard.BodyPosition).GetSafeNormal();

        if (!ToBody.IsNearlyZero())
        {
            const double Closing = FVector3d::DotProduct(Result.State.VelocityMs, ToBody);

            if (Closing > 0.0)
            {
                Result.State.VelocityMs = Result.State.VelocityMs - ToBody * Closing;
            }
        }
    }
    else
    {
        Result.State.Position = Intended;
        Result.DistanceMovedMeters = StepMeters.Size();

        if (Hazard.bStartedInside)
        {
            // Already inside a standoff sphere. Reported, not clamped: there is
            // nothing useful to clamp to, and clamping would trap the ship
            // where it is. Getting out is the caller's problem.
            Result.Hazard = Hazard;
        }
    }

    // A clamped step has closed everything it can this frame. Keeping the
    // closing velocity would send the ship straight back out on the next one,
    // which is the oscillation this clamp exists to prevent; lateral velocity
    // is left alone, because a ship passing a target sideways is manoeuvring
    // rather than arriving.
    if (Result.bArrivalClamped)
    {
        const FVector3d ToTargetAfter =
            GetRelativeMeters(Result.State.Position, Target.Position).GetSafeNormal();

        if (!ToTargetAfter.IsNearlyZero())
        {
            const double Closing = FVector3d::DotProduct(Result.State.VelocityMs, ToTargetAfter);

            if (Closing > 0.0)
            {
                Result.State.VelocityMs = Result.State.VelocityMs - ToTargetAfter * Closing;
            }
        }
        else
        {
            Result.State.VelocityMs = FVector3d::ZeroVector;
        }
    }

    // Re-evaluate arrival at the new position, so a step that arrives says so
    // on the step that arrives rather than the one after it.
    if (Target.IsValid())
    {
        Result.DistanceToTargetMeters =
            FUniversePosition::DistanceMeters(Result.State.Position, Target.Position);

        Result.bArrived = Result.DistanceToTargetMeters <= Target.ArrivalRadiusMeters;
    }

    return Result;
}

double FInterstellarTravel::EstimateTravelTimeSeconds(
    const FTravelProfile& Profile,
    double DistanceMeters,
    EUniverseTravelMode Mode)
{
    if (DistanceMeters <= 0.0)
    {
        return 0.0;
    }

    const double MaxSpeed = Profile.GetMaxSpeedMs(Mode);
    const double Acceleration = Profile.GetAccelerationMs2(Mode);
    const double Deceleration = Profile.GetDecelerationMs2(Mode);

    if (MaxSpeed <= 0.0 || Acceleration <= 0.0 || Deceleration <= 0.0)
    {
        return 0.0;
    }

    // Accelerate to cruise, coast, decelerate to rest. The interesting case is
    // the short trip that never reaches cruise speed at all, which at warp is
    // most of them - the acceleration distance to 10^15 m/s is over a light
    // year.
    const double AccelerateDistance = (MaxSpeed * MaxSpeed) / (2.0 * Acceleration);
    const double DecelerateDistance = (MaxSpeed * MaxSpeed) / (2.0 * Deceleration);

    if (AccelerateDistance + DecelerateDistance >= DistanceMeters)
    {
        // Triangular profile: the peak speed reached is set by the distance.
        // Solving d = v^2/2a + v^2/2b for v gives the expression below.
        const double PeakSpeed = FMath::Sqrt(
            (2.0 * DistanceMeters * Acceleration * Deceleration) / (Acceleration + Deceleration));

        return (PeakSpeed / Acceleration) + (PeakSpeed / Deceleration);
    }

    const double CruiseDistance = DistanceMeters - AccelerateDistance - DecelerateDistance;

    return (MaxSpeed / Acceleration) + (CruiseDistance / MaxSpeed) + (MaxSpeed / Deceleration);
}

FString FInterstellarTravel::FormatDistance(double Meters)
{
    const double Absolute = FMath::Abs(Meters);

    if (Absolute >= UniverseScale::MetersPerLightYear * 0.01)
    {
        return FString::Printf(TEXT("%.3f ly"), Meters / UniverseScale::MetersPerLightYear);
    }

    // AU next, and all the way up to the light-year threshold. An earlier draft
    // put light minutes in between, which reads well and is wrong: a light
    // minute is 0.12 AU, so the branch swallowed the whole of the system scale
    // and reported the Earth's orbit as "8.32 light minutes". True, and not
    // what anybody navigating a solar system wants to read.
    if (Absolute >= UniverseScale::MetersPerAu * 0.001)
    {
        return FString::Printf(TEXT("%.4f AU"), Meters / UniverseScale::MetersPerAu);
    }

    if (Absolute >= 1000.0)
    {
        return FString::Printf(TEXT("%.1f km"), Meters / 1000.0);
    }

    return FString::Printf(TEXT("%.1f m"), Meters);
}

FString FInterstellarTravel::FormatSpeed(double MetersPerSecond)
{
    const double Absolute = FMath::Abs(MetersPerSecond);
    const double C = UniversePhysics::SpeedOfLightMs;

    if (Absolute >= C * 0.01)
    {
        return FString::Printf(TEXT("%.4g c"), MetersPerSecond / C);
    }

    if (Absolute >= 1000.0)
    {
        return FString::Printf(TEXT("%.1f km/s"), MetersPerSecond / 1000.0);
    }

    return FString::Printf(TEXT("%.1f m/s"), MetersPerSecond);
}
