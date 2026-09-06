// Copyright Universe Project. All Rights Reserved.

#include "PlanetTrajectory.h"
#include "PlanetSurfaceQuery.h"

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
    FPlanetSweepResult Result;

    const FVector3d Delta(
        EndMeters.X - StartMeters.X,
        EndMeters.Y - StartMeters.Y,
        EndMeters.Z - StartMeters.Z);

    const double RadiusSquared = SphereRadiusMeters * SphereRadiusMeters;

    // Quadratic coefficients for |Start + t*Delta|^2 = r^2, with the factors of
    // two folded into B so the discriminant is B^2 - A*C rather than
    // B^2 - 4AC. Same roots, two fewer operations, and one fewer place to put
    // a stray factor of two.
    const double A = Delta.SizeSquared();
    const double B = FVector3d::DotProduct(StartMeters, Delta);
    const double C = StartMeters.SizeSquared() - RadiusSquared;

    Result.bStartedInside = C <= 0.0;

    if (A <= 0.0)
    {
        // A degenerate segment: the craft did not move. It intersects the body
        // exactly when it is already inside it, and the closest approach is
        // simply where it is standing.
        Result.ClosestApproachMeters = StartMeters.Size();
        Result.ClosestApproachFraction = 0.0;

        if (Result.bStartedInside)
        {
            Result.bHit = true;
            Result.EntryFraction = 0.0;
            Result.ExitFraction = 0.0;
            Result.EntryPointMeters = StartMeters;
        }

        return Result;
    }

    // Closest approach to the centre, independent of whether there is a hit.
    // The unclamped minimum of |Start + t*Delta| is at t = -B/A; clamping it to
    // the segment is what makes this the closest approach of the *segment*
    // rather than of the infinite line.
    Result.ClosestApproachFraction = FMath::Clamp(-B / A, 0.0, 1.0);
    Result.ClosestApproachMeters =
        PointAt(StartMeters, Delta, Result.ClosestApproachFraction).Size();

    const double Discriminant = B * B - A * C;

    if (Discriminant < 0.0)
    {
        return Result;
    }

    const double RootDiscriminant = FMath::Sqrt(Discriminant);

    // The numerically stable pair. Computing both roots as
    // (-B +/- sqrt(disc)) / A subtracts two nearly equal large numbers for one
    // of them, and here "nearly equal" means agreeing to thirteen digits: a
    // craft 10^13 m away has B^2 near 10^26 and A*C near 10^13. The root that
    // survives that subtraction is the one where the signs agree, so it is
    // computed directly and the other is recovered from the fact that the
    // product of the roots is C/A. Both then carry full precision.
    const double Q = (B >= 0.0)
        ? -(B + RootDiscriminant)
        : -(B - RootDiscriminant);

    double T0 = Q / A;
    double T1 = (Q != 0.0) ? (C / Q) : T0;

    if (T0 > T1)
    {
        const double Swap = T0;
        T0 = T1;
        T1 = Swap;
    }

    // Both intersections behind the start, or both beyond the end: the
    // infinite line hits, the segment travelled this frame does not.
    if (T1 < 0.0 || T0 > 1.0)
    {
        return Result;
    }

    Result.bHit = true;
    Result.EntryFraction = FMath::Clamp(T0, 0.0, 1.0);
    Result.ExitFraction = FMath::Clamp(T1, 0.0, 1.0);
    Result.EntryPointMeters = PointAt(StartMeters, Delta, Result.EntryFraction);

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
