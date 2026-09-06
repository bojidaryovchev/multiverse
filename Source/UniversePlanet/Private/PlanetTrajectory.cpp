// Copyright Universe Project. All Rights Reserved.

#include "PlanetTrajectory.h"
#include "PlanetSurfaceQuery.h"
#include "UniverseSweep.h"

namespace
{
    FVector3d PointAt(const FVector3d& Start, const FVector3d& Delta, double Fraction)
    {
        return FVector3d(
            Start.X + Delta.X * Fraction,
            Start.Y + Delta.Y * Fraction,
            Start.Z + Delta.Z * Fraction);
    }
}

FPlanetSweepResult FPlanetTrajectory::SweepSegmentAgainstSphere(
    const FVector3d& StartMeters,
    const FVector3d& EndMeters,
    double SphereRadiusMeters)
{
    // Delegated to UniverseCore. Sprint 006 needs the identical segment-sphere
    // question for stars and star systems, from a module that sits below
    // UniversePlanet and cannot call into it - and a second copy of a quadratic
    // this subtle is a copy that eventually drifts, in the direction of
    // silently letting a ship pass through a star. See UniverseSweep.h.
    const FSegmentSphereResult Sweep =
        FUniverseSweep::SegmentSphere(StartMeters, EndMeters, SphereRadiusMeters);

    FPlanetSweepResult Result;
    Result.bHit = Sweep.bHit;
    Result.bStartedInside = Sweep.bStartedInside;
    Result.EntryFraction = Sweep.EntryFraction;
    Result.ExitFraction = Sweep.ExitFraction;
    Result.EntryPointMeters = Sweep.EntryPoint;
    Result.ClosestApproachMeters = Sweep.ClosestApproach;
    Result.ClosestApproachFraction = Sweep.ClosestApproachFraction;

    return Result;
}

FPlanetSweepResult FPlanetTrajectory::SweepAgainstPlanetBounds(
    const FPlanetSurfaceDescriptor& Planet,
    const FVector3d& StartMeters,
    const FVector3d& EndMeters,
    double MarginMeters)
{
    const double Radius = FMath::Max(Planet.GetMaxRadiusMeters() + MarginMeters, 0.0);

    return SweepSegmentAgainstSphere(StartMeters, EndMeters, Radius);
}

FPlanetSweepResult FPlanetTrajectory::SweepAgainstTerrain(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& StartMeters,
    const FVector3d& EndMeters,
    double MarginMeters,
    int32 RefinementSteps)
{
    FPlanetSweepResult Result =
        SweepAgainstPlanetBounds(Planet, StartMeters, EndMeters, MarginMeters);

    if (!Result.bHit || Result.bStartedInside)
    {
        return Result;
    }

    const FVector3d Delta(
        EndMeters.X - StartMeters.X,
        EndMeters.Y - StartMeters.Y,
        EndMeters.Z - StartMeters.Z);

    // Signed clearance above the terrain, positive outside. The bracket runs
    // from the bounding-sphere entry - guaranteed outside, since the bounding
    // sphere encloses every possible peak - to the point of closest approach,
    // which is the deepest the segment ever gets. If the clearance is still
    // positive there, the segment passed over a basin and missed the ground
    // entirely: a bounding-sphere false positive, which is exactly what this
    // refinement exists to reject.
    const double Margin = FMath::Max(MarginMeters, 0.0);

    auto ClearanceAt = [&](double Fraction) -> double
    {
        const FVector3d Point = PointAt(StartMeters, Delta, Fraction);
        return FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(Planet, Settings, Point) - Margin;
    };

    const double DeepestFraction = FMath::Max(Result.ClosestApproachFraction, Result.EntryFraction);

    if (ClearanceAt(DeepestFraction) > 0.0)
    {
        FPlanetSweepResult Miss;
        Miss.bStartedInside = false;
        Miss.ClosestApproachMeters = Result.ClosestApproachMeters;
        Miss.ClosestApproachFraction = Result.ClosestApproachFraction;
        return Miss;
    }

    double Outside = Result.EntryFraction;
    double Inside = DeepestFraction;

    const int32 Steps = FMath::Clamp(RefinementSteps, 1, 64);

    for (int32 Step = 0; Step < Steps; ++Step)
    {
        const double Middle = (Outside + Inside) * 0.5;

        if (ClearanceAt(Middle) > 0.0)
        {
            Outside = Middle;
        }
        else
        {
            Inside = Middle;
        }
    }

    // Report the first point known to be *at or below* the surface. Returning
    // the outside end of the bracket instead would leave the contact point
    // fractionally above the ground, which reads as a hit that never touches
    // anything.
    Result.EntryFraction = Inside;
    Result.EntryPointMeters = PointAt(StartMeters, Delta, Inside);

    return Result;
}

bool FPlanetTrajectory::TryClampStepToBounds(
    const FPlanetSurfaceDescriptor& Planet,
    const FVector3d& StartMeters,
    FVector3d& InOutEndMeters,
    double StopDistanceMeters,
    FPlanetSweepResult& OutSweep)
{
    const double Stop = FMath::Max(StopDistanceMeters, 0.0);

    OutSweep = SweepAgainstPlanetBounds(Planet, StartMeters, InOutEndMeters, Stop);

    if (!OutSweep.bHit || OutSweep.bStartedInside)
    {
        return false;
    }

    // The entry point already sits StopDistanceMeters clear of the highest
    // possible terrain, because the margin was applied to the sphere rather
    // than subtracted from the result afterwards. Stopping there is therefore
    // safe by construction, with no second geometric test to get wrong.
    InOutEndMeters = OutSweep.EntryPointMeters;

    return true;
}
