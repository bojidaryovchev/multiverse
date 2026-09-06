// Copyright Universe Project. All Rights Reserved.

#include "PlanetGravity.h"

double FPlanetGravityField::GetMagnitudeMs2(double DistanceFromCentreMeters) const
{
    if (!IsValid())
    {
        return 0.0;
    }

    const double Distance = FMath::Max(DistanceFromCentreMeters, 0.0);

    if (Distance >= RadiusMeters)
    {
        // Outside: inverse square. Written as a squared ratio rather than
        // mu / r^2 so that r = R gives back exactly SurfaceGravityMs2 - the
        // two forms differ by rounding, and a discontinuity precisely at the
        // surface is the one place it would be noticed.
        const double Ratio = RadiusMeters / Distance;
        return SurfaceGravityMs2 * Ratio * Ratio;
    }

    // Inside: linear to zero at the centre. See the header.
    return SurfaceGravityMs2 * (Distance / RadiusMeters);
}

FVector3d FPlanetGravityField::GetAccelerationMs2(const FVector3d& PlanetLocalMeters) const
{
    const double DistanceSquared = PlanetLocalMeters.SizeSquared();

    if (DistanceSquared <= 0.0)
    {
        return FVector3d::ZeroVector;
    }

    const double Distance = FMath::Sqrt(DistanceSquared);
    const double Magnitude = GetMagnitudeMs2(Distance);

    // Toward the centre: -position / |position|, scaled.
    const double Scale = -Magnitude / Distance;

    return FVector3d(
        PlanetLocalMeters.X * Scale,
        PlanetLocalMeters.Y * Scale,
        PlanetLocalMeters.Z * Scale);
}

bool FPlanetGravityField::TryGetLocalUp(const FVector3d& PlanetLocalMeters, FVector3d& OutUp)
{
    const double DistanceSquared = PlanetLocalMeters.SizeSquared();

    if (DistanceSquared <= 0.0)
    {
        OutUp = FVector3d(0.0, 0.0, 1.0);
        return false;
    }

    const double InverseDistance = 1.0 / FMath::Sqrt(DistanceSquared);

    OutUp = FVector3d(
        PlanetLocalMeters.X * InverseDistance,
        PlanetLocalMeters.Y * InverseDistance,
        PlanetLocalMeters.Z * InverseDistance);

    return true;
}

FVector3d FPlanetGravityField::GetLocalUp(const FVector3d& PlanetLocalMeters)
{
    FVector3d Up;
    TryGetLocalUp(PlanetLocalMeters, Up);
    return Up;
}

double FPlanetGravityField::GetEscapeVelocityMs() const
{
    if (!IsValid())
    {
        return 0.0;
    }

    return FMath::Sqrt(2.0 * SurfaceGravityMs2 * RadiusMeters);
}

double FPlanetGravityField::GetCircularOrbitSpeedMs(double DistanceFromCentreMeters) const
{
    if (!IsValid() || DistanceFromCentreMeters < RadiusMeters)
    {
        return 0.0;
    }

    // v = sqrt(mu / r), with mu = g R^2.
    return FMath::Sqrt(GetGravitationalParameter() / DistanceFromCentreMeters);
}
