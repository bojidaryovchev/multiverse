// Copyright Universe Project. All Rights Reserved.

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

FSegmentSphereResult FUniverseSweep::SegmentSphere(
    const FVector3d& Start,
    const FVector3d& End,
    double SphereRadius)
{
    FSegmentSphereResult Result;

    const FVector3d Delta(
        End.X - Start.X,
        End.Y - Start.Y,
        End.Z - Start.Z);

    const double RadiusSquared = SphereRadius * SphereRadius;

    // Quadratic coefficients for |Start + t*Delta|^2 = r^2, with the factors of
    // two folded into B so the discriminant is B^2 - A*C rather than
    // B^2 - 4AC. Same roots, two fewer operations, and one fewer place to put a
    // stray factor of two.
    const double A = Delta.SizeSquared();
    const double B = FVector3d::DotProduct(Start, Delta);
    const double C = Start.SizeSquared() - RadiusSquared;

    Result.bStartedInside = C <= 0.0;

    if (A <= 0.0)
    {
        // A degenerate segment: the craft did not move. It intersects the body
        // exactly when it is already inside it, and the closest approach is
        // simply where it is standing.
        Result.ClosestApproach = Start.Size();
        Result.ClosestApproachFraction = 0.0;

        if (Result.bStartedInside)
        {
            Result.bHit = true;
            Result.EntryFraction = 0.0;
            Result.ExitFraction = 0.0;
            Result.EntryPoint = Start;
        }

        return Result;
    }

    // Closest approach to the centre, independent of whether there is a hit.
    // The unclamped minimum of |Start + t*Delta| is at t = -B/A; clamping it to
    // the segment is what makes this the closest approach of the *segment*
    // rather than of the infinite line.
    Result.ClosestApproachFraction = FMath::Clamp(-B / A, 0.0, 1.0);
    Result.ClosestApproach =
        PointAt(Start, Delta, Result.ClosestApproachFraction).Size();

    const double Discriminant = B * B - A * C;

    if (Discriminant < 0.0)
    {
        return Result;
    }

    const double RootDiscriminant = FMath::Sqrt(Discriminant);

    // The numerically stable pair. Computing both roots as
    // (-B +/- sqrt(disc)) / A subtracts two nearly equal large numbers for one
    // of them, and at these ranges "nearly equal" means agreeing to more digits
    // than a double has. The root that survives that subtraction is the one
    // where the signs agree, so it is computed directly and the other is
    // recovered from the fact that the product of the roots is C/A. Both then
    // carry full precision.
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

    // Both intersections behind the start, or both beyond the end: the infinite
    // line hits, the segment travelled this frame does not.
    if (T1 < 0.0 || T0 > 1.0)
    {
        return Result;
    }

    Result.bHit = true;
    Result.EntryFraction = FMath::Clamp(T0, 0.0, 1.0);
    Result.ExitFraction = FMath::Clamp(T1, 0.0, 1.0);
    Result.EntryPoint = PointAt(Start, Delta, Result.EntryFraction);

    return Result;
}

FSegmentSphereResult FUniverseSweep::SegmentSphereAt(
    const FVector3d& Start,
    const FVector3d& End,
    const FVector3d& Centre,
    double SphereRadius)
{
    FSegmentSphereResult Result = SegmentSphere(
        FVector3d(Start.X - Centre.X, Start.Y - Centre.Y, Start.Z - Centre.Z),
        FVector3d(End.X - Centre.X, End.Y - Centre.Y, End.Z - Centre.Z),
        SphereRadius);

    // The entry point is translated back into the caller's frame; everything
    // else is a scalar or a fraction and is frame-independent.
    Result.EntryPoint = FVector3d(
        Result.EntryPoint.X + Centre.X,
        Result.EntryPoint.Y + Centre.Y,
        Result.EntryPoint.Z + Centre.Z);

    return Result;
}
