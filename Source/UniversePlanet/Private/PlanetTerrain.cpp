// Copyright Universe Project. All Rights Reserved.

#include "PlanetTerrain.h"
#include "PlanetNoise.h"

namespace
{
    /**
     * Seed sub-streams, so each layer is independent.
     *
     * Frozen: changing one re-rolls that layer across every planet.
     */
    constexpr uint64 StreamContinents = 0x434F4E540000001ull;
    constexpr uint64 StreamMountains  = 0x4D4F554E54000001ull;
    constexpr uint64 StreamDetail     = 0x44455441494C0001ull;
    constexpr uint64 StreamWarp       = 0x5741525000000001ull;

    /**
     * Base frequencies, in cycles per radian of arc.
     *
     * Angular rather than absolute, so features scale with the planet: a body
     * twice the radius gets continents twice as wide, not twice as many. That
     * matches how real bodies look and keeps a small moon from being covered in
     * Earth-sized mountain ranges.
     */
    constexpr double ContinentFrequency = 1.35;
    constexpr double MountainFrequency = 5.0;
    constexpr double DetailFrequency = 22.0;
    constexpr double WarpFrequency = 2.2;

    /** How far domain warping displaces the mountain sample, in radians. */
    constexpr double WarpStrength = 0.35;

    /** Continentalness above this is land; below is ocean basin. */
    constexpr double SeaLevelThreshold = -0.05;

    /** Width of the coastal blend, in continentalness units. */
    constexpr double CoastBlend = 0.22;

    /** Smooth 0..1 ramp with zero derivative at both ends. */
    double SmoothStep(double Edge0, double Edge1, double Value)
    {
        if (Edge1 <= Edge0)
        {
            return (Value < Edge0) ? 0.0 : 1.0;
        }
        double T = (Value - Edge0) / (Edge1 - Edge0);
        T = FMath::Clamp(T, 0.0, 1.0);
        return T * T * (3.0 - 2.0 * T);
    }
}

FPlanetTerrain::FTerrainSample FPlanetTerrain::SampleDetailed(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction)
{
    FTerrainSample Sample;

    const uint64 Seed = Planet.Seed.Value;

    // --- Continentalness ---------------------------------------------------
    // Very low frequency: a handful of large masses over the whole sphere.
    Sample.Continentalness = PlanetNoise::FBM(
        Direction,
        UniverseHash::Hash(Seed, StreamContinents),
        Settings.ContinentOctaves,
        ContinentFrequency);

    // Land mask: 0 in deep ocean, 1 well inland, smoothly blended at coasts so
    // the shoreline is a gradient rather than a cliff.
    Sample.LandMask = SmoothStep(
        SeaLevelThreshold,
        SeaLevelThreshold + CoastBlend,
        Sample.Continentalness);

    // --- Mountains ---------------------------------------------------------
    // Domain warping first: offsetting the sample point by another noise field
    // bends the ridge lines into something that curves and branches like a real
    // range, instead of the visibly isotropic blobs raw ridged noise produces.
    const uint64 WarpSeed = UniverseHash::Hash(Seed, StreamWarp);
    const FVector3d WarpSample(
        Direction.X * WarpFrequency,
        Direction.Y * WarpFrequency,
        Direction.Z * WarpFrequency);

    const FVector3d Warped(
        Direction.X + PlanetNoise::Noise3D(WarpSample, UniverseHash::Hash(WarpSeed, 0)) * WarpStrength,
        Direction.Y + PlanetNoise::Noise3D(WarpSample, UniverseHash::Hash(WarpSeed, 1)) * WarpStrength,
        Direction.Z + PlanetNoise::Noise3D(WarpSample, UniverseHash::Hash(WarpSeed, 2)) * WarpStrength);

    const double RawMountains = PlanetNoise::RidgedFBM(
        Warped,
        UniverseHash::Hash(Seed, StreamMountains),
        Settings.MountainOctaves,
        MountainFrequency);

    // Ridged noise spends most of its range near the top; remapping to [0,1]
    // and masking by land keeps ranges on continents rather than rising out of
    // open ocean.
    Sample.Mountains = (RawMountains * 0.5 + 0.5) * Sample.LandMask;

    // --- Detail ------------------------------------------------------------
    Sample.Detail = PlanetNoise::FBM(
        Direction,
        UniverseHash::Hash(Seed, StreamDetail),
        Settings.DetailOctaves,
        DetailFrequency);

    // --- Composition -------------------------------------------------------
    // Worked in a normalised [-1, 1] space and scaled to metres at the very
    // end, so the elevation bounds hold by construction instead of by clamping
    // a value that had already gone out of range.
    //
    // Weights: continents dominate the broad shape, mountains add relief on
    // land, detail is a small perturbation everywhere.
    const double Macro = Sample.Continentalness * 0.55;
    const double Relief = Sample.Mountains * 0.85;
    const double Fine = Sample.Detail * 0.08;

    double Normalised = Macro + Relief + Fine;
    Normalised = FMath::Clamp(Normalised, -1.0, 1.0);
    Sample.NormalisedElevation = Normalised;

    // Asymmetric scaling: the range above sea level and the range below are
    // different, as they are on real bodies where ocean trenches are deeper
    // than mountains are tall.
    Sample.ElevationMeters = (Normalised >= 0.0)
        ? Normalised * Planet.MaxElevationMeters
        : Normalised * Planet.MaxDepthMeters;

    return Sample;
}

double FPlanetTerrain::GetElevationMeters(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction)
{
    return SampleDetailed(Planet, Settings, Direction).ElevationMeters;
}

void FPlanetTerrain::GetTangentBasis(const FVector3d& Direction, FVector3d& OutTangentU, FVector3d& OutTangentV)
{
    // Choose the reference axis furthest from the direction. A fixed axis would
    // give a degenerate cross product at two points on every planet, and those
    // points would sit exactly at the poles of the chosen axis.
    const double AbsX = FMath::Abs(Direction.X);
    const double AbsY = FMath::Abs(Direction.Y);
    const double AbsZ = FMath::Abs(Direction.Z);

    // If the direction points mostly along Z, use X as the reference; anything
    // else is safely far from Z. Either way the reference is at least 45
    // degrees off the direction, so the cross product is well conditioned.
    (void)AbsX;
    (void)AbsY;
    const FVector3d Reference = (AbsZ >= AbsX && AbsZ >= AbsY)
        ? FVector3d(1.0, 0.0, 0.0)
        : FVector3d(0.0, 0.0, 1.0);

    // TangentU = normalize(cross(Reference, Direction))
    const FVector3d CrossU(
        Reference.Y * Direction.Z - Reference.Z * Direction.Y,
        Reference.Z * Direction.X - Reference.X * Direction.Z,
        Reference.X * Direction.Y - Reference.Y * Direction.X);

    const double LengthU = FMath::Sqrt(CrossU.X * CrossU.X + CrossU.Y * CrossU.Y + CrossU.Z * CrossU.Z);
    const double InvLengthU = (LengthU > 0.0) ? (1.0 / LengthU) : 0.0;
    OutTangentU = FVector3d(CrossU.X * InvLengthU, CrossU.Y * InvLengthU, CrossU.Z * InvLengthU);

    // TangentV = cross(Direction, TangentU), already unit length.
    OutTangentV = FVector3d(
        Direction.Y * OutTangentU.Z - Direction.Z * OutTangentU.Y,
        Direction.Z * OutTangentU.X - Direction.X * OutTangentU.Z,
        Direction.X * OutTangentU.Y - Direction.Y * OutTangentU.X);
}

FVector3d FPlanetTerrain::GetSurfaceNormal(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FVector3d& Direction,
    double AngularEpsilon)
{
    FVector3d TangentU;
    FVector3d TangentV;
    GetTangentBasis(Direction, TangentU, TangentV);

    // Central differences: sample the surface either side along each tangent
    // and take the cross product of the resulting spans. Central rather than
    // forward differences because a forward difference biases the normal by
    // half a step, which tilts lighting consistently in one direction.
    auto SurfaceAt = [&](const FVector3d& Offset) -> FVector3d
    {
        const FVector3d Moved(
            Direction.X + Offset.X,
            Direction.Y + Offset.Y,
            Direction.Z + Offset.Z);

        const double Length = FMath::Sqrt(Moved.X * Moved.X + Moved.Y * Moved.Y + Moved.Z * Moved.Z);
        const double InvLength = (Length > 0.0) ? (1.0 / Length) : 0.0;
        const FVector3d Unit(Moved.X * InvLength, Moved.Y * InvLength, Moved.Z * InvLength);

        return GetSurfacePositionMeters(Planet, Settings, Unit);
    };

    const FVector3d StepU(TangentU.X * AngularEpsilon, TangentU.Y * AngularEpsilon, TangentU.Z * AngularEpsilon);
    const FVector3d StepV(TangentV.X * AngularEpsilon, TangentV.Y * AngularEpsilon, TangentV.Z * AngularEpsilon);

    const FVector3d PlusU = SurfaceAt(StepU);
    const FVector3d MinusU = SurfaceAt(FVector3d(-StepU.X, -StepU.Y, -StepU.Z));
    const FVector3d PlusV = SurfaceAt(StepV);
    const FVector3d MinusV = SurfaceAt(FVector3d(-StepV.X, -StepV.Y, -StepV.Z));

    const FVector3d SpanU(PlusU.X - MinusU.X, PlusU.Y - MinusU.Y, PlusU.Z - MinusU.Z);
    const FVector3d SpanV(PlusV.X - MinusV.X, PlusV.Y - MinusV.Y, PlusV.Z - MinusV.Z);

    FVector3d Normal(
        SpanU.Y * SpanV.Z - SpanU.Z * SpanV.Y,
        SpanU.Z * SpanV.X - SpanU.X * SpanV.Z,
        SpanU.X * SpanV.Y - SpanU.Y * SpanV.X);

    const double Length = FMath::Sqrt(Normal.X * Normal.X + Normal.Y * Normal.Y + Normal.Z * Normal.Z);
    if (Length <= 0.0)
    {
        // Perfectly flat ground, or an epsilon so small the samples collapsed.
        // The radial direction is the correct answer in both cases.
        return Direction;
    }

    const double InvLength = 1.0 / Length;
    Normal = FVector3d(Normal.X * InvLength, Normal.Y * InvLength, Normal.Z * InvLength);

    // Orient outward. The cross product's sign depends on the tangent basis,
    // which flips between the reference-axis branches in GetTangentBasis, so
    // this cannot be assumed.
    const double Outward = Normal.X * Direction.X + Normal.Y * Direction.Y + Normal.Z * Direction.Z;
    if (Outward < 0.0)
    {
        Normal = FVector3d(-Normal.X, -Normal.Y, -Normal.Z);
    }

    return Normal;
}
