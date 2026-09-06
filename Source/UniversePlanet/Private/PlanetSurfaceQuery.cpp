// Copyright Universe Project. All Rights Reserved.

#include "PlanetSurfaceQuery.h"
#include "PlanetGravity.h"

namespace
{
    /**
     * Converts a ground-measured sample spacing into the angular epsilon
     * FPlanetTerrain wants.
     *
     * An arc of length s on a sphere of radius R subtends s/R radians. Working
     * in metres at the call site and converting here is what keeps the same
     * number meaningful across planet sizes; the alternative is every caller
     * dividing by a radius, and one of them eventually forgetting.
     */
    double SpacingToAngularEpsilon(const FPlanetSurfaceDescriptor& Planet, double SpacingMeters)
    {
        const double Spacing = SpacingMeters > 0.0
            ? SpacingMeters
            : FPlanetSurfaceQuery::DefaultNormalSpacingMeters;

        if (Planet.RadiusMeters <= 0.0)
        {
            return 0.0;
        }

        return Spacing / Planet.RadiusMeters;
    }
}

bool FPlanetSurfaceQuery::TryGetDirection(const FVector3d& PlanetLocalMeters, FVector3d& OutDirection)
{
    return FPlanetGravityField::TryGetLocalUp(PlanetLocalMeters, OutDirection);
}

FVector3d FPlanetSurfaceQuery::GetLocalUp(const FVector3d& PlanetLocalMeters)
{
    return FPlanetGravityField::GetLocalUp(PlanetLocalMeters);
}

double FPlanetSurfaceQuery::GetDistanceFromCentreMeters(const FVector3d& PlanetLocalMeters)
{
    return PlanetLocalMeters.Size();
}

double FPlanetSurfaceQuery::GetAltitudeAboveSeaLevelMeters(
    const FPlanetSurfaceDescriptor& Planet,
    const FVector3d& PlanetLocalMeters)
{
    return GetDistanceFromCentreMeters(PlanetLocalMeters) - Planet.RadiusMeters;
}

double FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& PlanetLocalMeters)
{
    FVector3d Direction;

    if (!TryGetDirection(PlanetLocalMeters, Direction))
    {
        // At the centre. The whole planet is above you; the honest answer is
        // the negative of the smallest surface radius, not zero, and certainly
        // not something that would read as "safely above the ground".
        return -Planet.GetMinRadiusMeters();
    }

    const double Distance = GetDistanceFromCentreMeters(PlanetLocalMeters);
    const double SurfaceRadius = GetSurfaceHeightMeters(Planet, Settings, Direction);

    return Distance - SurfaceRadius;
}

double FPlanetSurfaceQuery::GetSurfaceHeightMeters(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction)
{
    return Planet.RadiusMeters + FPlanetTerrain::GetElevationMeters(Planet, Settings, Direction);
}

FVector3d FPlanetSurfaceQuery::GetSurfacePositionMeters(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction)
{
    return FPlanetTerrain::GetSurfacePositionMeters(Planet, Settings, Direction);
}

FVector3d FPlanetSurfaceQuery::GetSurfaceNormal(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction,
    double SampleSpacingMeters)
{
    return FPlanetTerrain::GetSurfaceNormal(
        Planet, Settings, Direction, SpacingToAngularEpsilon(Planet, SampleSpacingMeters));
}

FPlanetSurfaceSample FPlanetSurfaceQuery::SampleDirection(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction,
    double NormalSpacingMeters)
{
    FPlanetSurfaceSample Sample;

    FVector3d Unit;

    if (!TryGetDirection(Direction, Unit))
    {
        return Sample;
    }

    Sample.Direction = Unit;
    Sample.UpUnit = Unit;
    Sample.ElevationMeters = FPlanetTerrain::GetElevationMeters(Planet, Settings, Unit);
    Sample.SurfaceRadiusMeters = Planet.RadiusMeters + Sample.ElevationMeters;

    Sample.SurfacePositionMeters = FVector3d(
        Unit.X * Sample.SurfaceRadiusMeters,
        Unit.Y * Sample.SurfaceRadiusMeters,
        Unit.Z * Sample.SurfaceRadiusMeters);

    Sample.NormalUnit = FPlanetTerrain::GetSurfaceNormal(
        Planet, Settings, Unit, SpacingToAngularEpsilon(Planet, NormalSpacingMeters));

    Sample.bValid = true;

    return Sample;
}

FPlanetSurfaceSample FPlanetSurfaceQuery::SampleBelow(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& PlanetLocalMeters,
    double NormalSpacingMeters)
{
    // Projection to a direction is exactly what SampleDirection already does,
    // because the sample below a position and the sample along its direction
    // are the same point. Kept as a separate entry point purely so call sites
    // read as what they mean.
    return SampleDirection(Planet, Settings, PlanetLocalMeters, NormalSpacingMeters);
}

FVector3d FPlanetSurfaceQuery::GetPositionAboveTerrain(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction,
    double HeightAboveTerrainMeters)
{
    FVector3d Unit;

    if (!TryGetDirection(Direction, Unit))
    {
        return FVector3d::ZeroVector;
    }

    const double Radius =
        GetSurfaceHeightMeters(Planet, Settings, Unit) + HeightAboveTerrainMeters;

    return FVector3d(Unit.X * Radius, Unit.Y * Radius, Unit.Z * Radius);
}

double FPlanetSurfaceQuery::GetAtmosphericDepthFraction(
    const FPlanetSurfaceDescriptor& Planet,
    const FVector3d& PlanetLocalMeters)
{
    if (!Planet.HasAtmosphere())
    {
        return 0.0;
    }

    const double Altitude = GetAltitudeAboveSeaLevelMeters(Planet, PlanetLocalMeters);
    const double Fraction = 1.0 - (Altitude / Planet.AtmosphereHeightMeters);

    return FMath::Clamp(Fraction, 0.0, 1.0);
}

bool FPlanetSurfaceQuery::IsInsideAtmosphere(
    const FPlanetSurfaceDescriptor& Planet,
    const FVector3d& PlanetLocalMeters)
{
    if (!Planet.HasAtmosphere())
    {
        return false;
    }

    return GetDistanceFromCentreMeters(PlanetLocalMeters) <= Planet.GetAtmosphereTopRadiusMeters();
}
