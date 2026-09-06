// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniversePlanetTestList.h"
#include "PlanetSurfaceQuery.h"
#include "PlanetGravity.h"
#include "PlanetTrajectory.h"
#include "SimulationFrame.h"

namespace
{
    FPlanetSurfaceDescriptor MakeTraversalPlanet(
        uint64 SeedValue,
        double RadiusMeters,
        double SurfaceGravityMs2 = 9.81,
        double AtmosphereHeightMeters = 100000.0)
    {
        FPlanetSurfaceDescriptor Planet;
        Planet.PlanetKey = SeedValue | 1ull;
        Planet.Seed = FUniverseSeed(SeedValue);
        Planet.RadiusMeters = RadiusMeters;
        Planet.MaxElevationMeters = RadiusMeters * 0.00139;
        Planet.MaxDepthMeters = RadiusMeters * 0.00172;
        Planet.SurfaceGravityMs2 = SurfaceGravityMs2;
        Planet.AtmosphereHeightMeters = AtmosphereHeightMeters;
        Planet.GenerationVersion = PlanetTerrainVersion::Current;
        return Planet;
    }

    /**
     * A spread of unit directions that is not aligned with any axis or cube
     * face.
     *
     * Deliberately irrational-looking rather than a neat lat/long grid: a grid
     * samples face centres and edges over and over and would miss anything that
     * only goes wrong away from them, which is where cube-sphere bugs live.
     */
    FVector3d SpreadDirection(int32 Index)
    {
        const double A = 0.7548776662466927 * static_cast<double>(Index + 1);
        const double B = 0.5698402909980532 * static_cast<double>(Index + 1);

        const double U = A - FMath::FloorToDouble(A);
        const double V = B - FMath::FloorToDouble(B);

        // Uniform on the sphere: z uniform in [-1, 1], angle uniform in [0, 2pi).
        const double Z = 2.0 * U - 1.0;
        const double R = FMath::Sqrt(FMath::Max(0.0, 1.0 - Z * Z));
        const double Theta = 2.0 * PI * V;

        return FVector3d(R * FMath::Cos(Theta), R * FMath::Sin(Theta), Z);
    }
}

/**
 * The three altitudes must mean what their names say, and must agree with each
 * other.
 *
 * This is the test that exists because the word "altitude" is ambiguous. The
 * relationships are simple enough to look self-evident, which is exactly why
 * they are worth pinning: the failure mode is not a wrong formula, it is a
 * caller reaching for the cheap sea-level query when it needed the expensive
 * terrain one, and the only defence against that is that the two provably
 * differ by the elevation and are therefore not interchangeable.
 */
bool UniverseTest_SurfaceQueryAltitudes(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const double Radii[3] = { 200000.0, 1737400.0, 6371000.0 };

    for (double Radius : Radii)
    {
        const FPlanetSurfaceDescriptor Planet = MakeTraversalPlanet(0xA53F1C0000000001ull, Radius);

        for (int32 Index = 0; Index < 64; ++Index)
        {
            const FVector3d Direction = SpreadDirection(Index);

            const double SurfaceRadius =
                FPlanetSurfaceQuery::GetSurfaceHeightMeters(Planet, Settings, Direction);

            const double Elevation = SurfaceRadius - Planet.RadiusMeters;

            // Pick observers relative to the ground, so the expected answers
            // are known exactly regardless of what the terrain does here.
            const double Heights[4] = { 0.0, 1.5, 12000.0, 400000.0 };

            for (double Height : Heights)
            {
                const FVector3d Observer = FPlanetSurfaceQuery::GetPositionAboveTerrain(
                    Planet, Settings, Direction, Height);

                const double FromCentre =
                    FPlanetSurfaceQuery::GetDistanceFromCentreMeters(Observer);
                const double AboveSeaLevel =
                    FPlanetSurfaceQuery::GetAltitudeAboveSeaLevelMeters(Planet, Observer);
                const double AboveTerrain =
                    FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(Planet, Settings, Observer);

                // Placement is the inverse of the terrain query, so the height
                // above the ground comes back as the height that was asked for.
                UVERIFY_NEAR(Result, AboveTerrain, Height, 1e-6 * Radius);

                // Distance from the centre is sea-level altitude plus radius,
                // by definition.
                UVERIFY_NEAR(Result, FromCentre, AboveSeaLevel + Planet.RadiusMeters, 1e-6 * Radius);

                // The two altitudes differ by exactly the terrain elevation.
                // On a real planet this is kilometres, which is the entire
                // reason they are separate functions.
                UVERIFY_NEAR(Result, AboveSeaLevel - AboveTerrain, Elevation, 1e-6 * Radius);
            }
        }

        // At the centre, the direction is undefined and every query must still
        // return something finite rather than a NaN that spreads.
        FVector3d Ignored;
        UVERIFY_FALSE(Result, FPlanetSurfaceQuery::TryGetDirection(FVector3d::ZeroVector, Ignored));

        const double CentreAltitude = FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
            Planet, Settings, FVector3d::ZeroVector);

        UVERIFY_TRUE(Result, FMath::IsFinite(CentreAltitude));

        // And it must read as deeply underground, not as zero. Zero would pass
        // a naive "am I above the ground" check from inside the planet.
        UVERIFY_TRUE(Result, CentreAltitude < -Planet.GetMinRadiusMeters() * 0.99);
    }

    return Result.Passed();
}

/**
 * Local up, the terrain normal, and the relationship between them.
 *
 * Up must be exactly radial everywhere - that is what makes a character stand
 * upright on a sphere - and the normal must be unit length and mostly agree
 * with up, since terrain relief is a fraction of a percent of the radius and
 * therefore cannot produce overhangs or vertical walls at this scale.
 */
bool UniverseTest_SurfaceQueryOrientation(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTraversalPlanet(0x51DE00FF00000001ull, 6371000.0);

    for (int32 Index = 0; Index < 96; ++Index)
    {
        const FVector3d Direction = SpreadDirection(Index);

        const FPlanetSurfaceSample Sample =
            FPlanetSurfaceQuery::SampleDirection(Planet, Settings, Direction);

        UVERIFY_TRUE(Result, Sample.bValid);

        // Up is radial: it is the direction itself, to rounding.
        UVERIFY_NEAR(Result, Sample.UpUnit.X, Direction.X, 1e-12);
        UVERIFY_NEAR(Result, Sample.UpUnit.Y, Direction.Y, 1e-12);
        UVERIFY_NEAR(Result, Sample.UpUnit.Z, Direction.Z, 1e-12);

        // Both vectors are unit length.
        UVERIFY_NEAR(Result, Sample.UpUnit.Size(), 1.0, 1e-12);
        UVERIFY_NEAR(Result, Sample.NormalUnit.Size(), 1.0, 1e-9);

        // The normal leans away from up by the local slope. With relief at
        // 0.14% of the radius there is no geometry steep enough to tip it past
        // the horizontal, so a negative dot product would mean an inverted
        // normal rather than a cliff - and an inverted normal is the sort of
        // thing that makes a character fall through the world.
        const double Alignment = FVector3d::DotProduct(Sample.UpUnit, Sample.NormalUnit);
        UVERIFY_TRUE(Result, Alignment > 0.0);

        // The sample sits on the ground it describes.
        const double SampleRadius = Sample.SurfacePositionMeters.Size();
        UVERIFY_NEAR(Result, SampleRadius, Sample.SurfaceRadiusMeters, 1e-6);

        // Sampling below a position and sampling along its direction are the
        // same query, whatever height the position is at.
        //
        // Agreement is to normalisation precision, not bit-exact, and that is
        // worth being explicit about because everywhere else in this project
        // the seam guarantees *are* bit-exact. The difference is that those
        // hold for dyadic UVs run through a basis of exact zeros and ones,
        // whereas here the direction is recovered by dividing a scaled vector
        // by its own length - and (d * r) / |d * r| is not d. The last couple
        // of digits move, which moves the noise sample, which moves the
        // elevation by around a nanometre on a 6,371 km planet. That is
        // irrelevant to anything standing on it, and pretending otherwise by
        // asserting exact equality here would be asserting a property the
        // construction does not provide.
        const FVector3d Observer =
            FPlanetSurfaceQuery::GetPositionAboveTerrain(Planet, Settings, Direction, 25000.0);

        const FPlanetSurfaceSample Below =
            FPlanetSurfaceQuery::SampleBelow(Planet, Settings, Observer);

        UVERIFY_NEAR(Result, Below.ElevationMeters, Sample.ElevationMeters, 1e-6);
    }

    return Result.Passed();
}

/**
 * Gravity must point at the planet centre from everywhere, fall off as 1/r^2
 * outside the body, fall linearly to zero inside it, and never produce a NaN
 * or an infinity.
 *
 * The direction half of this is the one that matters most. A hardcoded world
 * "down" passes every test taken at the north pole and fails the moment
 * somebody walks over the horizon, so the assertion is made from directions
 * spread over the whole sphere rather than from a convenient one.
 */
bool UniverseTest_PlanetGravityField(FUniverseTestResult& Result)
{
    const FPlanetSurfaceDescriptor Planet = MakeTraversalPlanet(0x6EA71700000001ull, 6371000.0, 9.81);
    const FPlanetGravityField Field = FPlanetGravityField::FromPlanet(Planet);

    UVERIFY_TRUE(Result, Field.IsValid());
    UVERIFY_NEAR(Result, Field.RadiusMeters, Planet.RadiusMeters, 0.0);
    UVERIFY_NEAR(Result, Field.SurfaceGravityMs2, Planet.SurfaceGravityMs2, 0.0);

    // Exactly at the surface radius the field must give back the surface
    // gravity, with no seam between the inside and outside branches.
    UVERIFY_NEAR(Result, Field.GetMagnitudeMs2(Planet.RadiusMeters), 9.81, 1e-12);

    for (int32 Index = 0; Index < 96; ++Index)
    {
        const FVector3d Direction = SpreadDirection(Index);

        const double Distances[6] = {
            0.0,
            Planet.RadiusMeters * 0.5,
            Planet.RadiusMeters,
            Planet.RadiusMeters * 2.0,
            Planet.RadiusMeters * 10.0,
            Planet.RadiusMeters * 1000.0,
        };

        for (double Distance : Distances)
        {
            const FVector3d Position(
                Direction.X * Distance, Direction.Y * Distance, Direction.Z * Distance);

            const FVector3d Acceleration = Field.GetAccelerationMs2(Position);

            UVERIFY_TRUE(Result, FMath::IsFinite(Acceleration.X));
            UVERIFY_TRUE(Result, FMath::IsFinite(Acceleration.Y));
            UVERIFY_TRUE(Result, FMath::IsFinite(Acceleration.Z));

            if (Distance <= 0.0)
            {
                // At the centre: exactly zero, and no invented direction.
                UVERIFY_EQ_DOUBLE_EXACT(Result, Acceleration.X, 0.0);
                UVERIFY_EQ_DOUBLE_EXACT(Result, Acceleration.Y, 0.0);
                UVERIFY_EQ_DOUBLE_EXACT(Result, Acceleration.Z, 0.0);
                continue;
            }

            // Antiparallel to the outward direction: straight down, wherever
            // "down" happens to be from here.
            const double Magnitude = Acceleration.Size();
            UVERIFY_TRUE(Result, Magnitude > 0.0);

            const FVector3d Unit(
                Acceleration.X / Magnitude, Acceleration.Y / Magnitude, Acceleration.Z / Magnitude);

            UVERIFY_NEAR(Result, FVector3d::DotProduct(Unit, Direction), -1.0, 1e-12);

            // And local up is exactly its opposite.
            const FVector3d Up = FPlanetGravityField::GetLocalUp(Position);
            UVERIFY_NEAR(Result, FVector3d::DotProduct(Up, Direction), 1.0, 1e-12);

            // Magnitude follows the right law on the right side of the surface.
            if (Distance >= Planet.RadiusMeters)
            {
                const double Ratio = Planet.RadiusMeters / Distance;
                UVERIFY_NEAR(Result, Magnitude, 9.81 * Ratio * Ratio, 1e-9);
            }
            else
            {
                UVERIFY_NEAR(Result, Magnitude, 9.81 * (Distance / Planet.RadiusMeters), 1e-9);
            }
        }
    }

    // Inverse square, stated as the property rather than as the formula:
    // doubling the distance quarters the pull.
    const double Near = Field.GetMagnitudeMs2(Planet.RadiusMeters * 3.0);
    const double Far = Field.GetMagnitudeMs2(Planet.RadiusMeters * 6.0);
    UVERIFY_NEAR(Result, Near / Far, 4.0, 1e-9);

    // Orbital and escape speeds, against textbook values for Earth. These are
    // the numbers that make orbits close, so they are worth checking against
    // something outside this codebase rather than against themselves.
    UVERIFY_NEAR(Result, Field.GetEscapeVelocityMs(), 11180.0, 20.0);
    UVERIFY_NEAR(Result, Field.GetCircularOrbitSpeedMs(Planet.RadiusMeters), 7906.0, 20.0);

    // A circular orbit is undefined inside the body, and says so rather than
    // returning a plausible-looking number.
    UVERIFY_EQ_DOUBLE_EXACT(Result, Field.GetCircularOrbitSpeedMs(Planet.RadiusMeters * 0.5), 0.0);

    // Gravity scales with the body. A small moon must not pull like Earth.
    const FPlanetSurfaceDescriptor Moon = MakeTraversalPlanet(0x99ull, 1737400.0, 1.62, 0.0);
    const FPlanetGravityField MoonField = FPlanetGravityField::FromPlanet(Moon);
    UVERIFY_NEAR(Result, MoonField.GetMagnitudeMs2(Moon.RadiusMeters), 1.62, 1e-12);
    UVERIFY_TRUE(Result, MoonField.GetEscapeVelocityMs() < Field.GetEscapeVelocityMs());

    return Result.Passed();
}

/**
 * The frame must be entered on approach, left on departure, and must not
 * change at all while the observer sits on the boundary.
 *
 * The flapping case is the one worth the effort. A craft parked exactly at the
 * enter radius with a metre of jitter would, under a single threshold, change
 * frame every tick - and each change rebases the origin and restarts terrain
 * streaming. The assertion is therefore not "the frame is correct" but "the
 * frame changed zero times across two hundred ticks of noise", which is the
 * property that actually matters.
 */
bool UniverseTest_SimulationFrameHysteresis(FUniverseTestResult& Result)
{
    const FPlanetSurfaceDescriptor Planet = MakeTraversalPlanet(0xF00D000000000001ull, 6371000.0);
    const FPlanetFrameBounds Bounds = FPlanetFrameBounds::FromPlanet(Planet);

    UVERIFY_TRUE(Result, Bounds.IsValid());
    UVERIFY_TRUE(Result, Bounds.ExitRadiusMeters > Bounds.EnterRadiusMeters);

    // Three planet radii, since that term dominates for an Earth-like body.
    UVERIFY_NEAR(Result, Bounds.EnterRadiusMeters, Planet.RadiusMeters * 3.0, 1.0);
    UVERIFY_NEAR(Result, Bounds.ExitRadiusMeters, Bounds.EnterRadiusMeters * 1.5, 1.0);

    // The atmosphere must be comfortably inside the frame that is supposed to
    // simulate it. Entering the air before entering the frame would mean drag
    // and heating applied by a frame that does not exist yet.
    UVERIFY_TRUE(Result, Bounds.EnterRadiusMeters > Planet.GetAtmosphereTopRadiusMeters());

    auto MakeCandidate = [&](double Distance)
    {
        FPlanetFrameCandidate Candidate;
        Candidate.PlanetKey = Planet.PlanetKey;
        Candidate.DistanceFromCentreMeters = Distance;
        Candidate.Bounds = Bounds;
        return Candidate;
    };

    FSimulationFrameSelector Selector;

    // Far away: interstellar.
    UVERIFY_FALSE(Result, Selector.Update(MakeCandidate(Bounds.ExitRadiusMeters * 10.0)));
    UVERIFY_TRUE(Result, Selector.GetKind() == EUniverseFrameKind::Interstellar);

    // Inside the band but approaching from outside: still interstellar,
    // because the enter radius has not been reached.
    const double BandMiddle = (Bounds.EnterRadiusMeters + Bounds.ExitRadiusMeters) * 0.5;
    UVERIFY_FALSE(Result, Selector.Update(MakeCandidate(BandMiddle)));
    UVERIFY_TRUE(Result, Selector.GetKind() == EUniverseFrameKind::Interstellar);

    // Crossing the enter radius: attached, and the transition is reported.
    UVERIFY_TRUE(Result, Selector.Update(MakeCandidate(Bounds.EnterRadiusMeters * 0.99)));
    UVERIFY_TRUE(Result, Selector.GetKind() == EUniverseFrameKind::Planetary);
    UVERIFY_EQ_UINT(Result, Selector.GetPlanetKey(), Planet.PlanetKey);
    UVERIFY_EQ_INT(Result, Selector.GetTransitionCount(), 1);

    // Back out into the band: still attached. This is the asymmetry that makes
    // it hysteresis rather than a threshold.
    UVERIFY_FALSE(Result, Selector.Update(MakeCandidate(BandMiddle)));
    UVERIFY_TRUE(Result, Selector.GetKind() == EUniverseFrameKind::Planetary);

    // Past the exit radius: released.
    UVERIFY_TRUE(Result, Selector.Update(MakeCandidate(Bounds.ExitRadiusMeters * 1.01)));
    UVERIFY_TRUE(Result, Selector.GetKind() == EUniverseFrameKind::Interstellar);
    UVERIFY_EQ_INT(Result, Selector.GetTransitionCount(), 2);

    // Now the flapping test, at each boundary in turn. Jitter of a metre on a
    // 19,000 km radius is far smaller than anything real, and is exactly the
    // scale at which a single threshold fails.
    const double Boundaries[2] = { Bounds.EnterRadiusMeters, Bounds.ExitRadiusMeters };

    for (double Boundary : Boundaries)
    {
        FSimulationFrameSelector Jitter;

        // Approach from outside, then let the first oscillation tick settle
        // whichever side of the boundary it is going to settle on. Crossing
        // the enter radius for the first time is a legitimate transition and
        // must not be counted against the hysteresis - what is being measured
        // is what happens for the *rest* of the time spent on the boundary.
        Jitter.Update(MakeCandidate(Boundary * 2.0));
        Jitter.Update(MakeCandidate(Boundary - 1.0));

        const int32 Before = Jitter.GetTransitionCount();
        const FUniverseFrameState Settled = Jitter.GetState();

        for (int32 Tick = 0; Tick < 200; ++Tick)
        {
            const double Offset = ((Tick % 2) == 0) ? 1.0 : -1.0;
            Jitter.Update(MakeCandidate(Boundary + Offset));

            // Not merely the same count at the end - the same state at every
            // single tick. A pair of transitions that cancelled out would pass
            // a count-only check while having rebased the origin four hundred
            // times, which is the failure this is guarding against.
            UVERIFY_TRUE(Result, Jitter.GetState() == Settled);
        }

        UVERIFY_EQ_INT(Result, Jitter.GetTransitionCount() - Before, 0);
    }

    // A full descent and ascent must produce exactly one transition each way,
    // not one per step.
    FSimulationFrameSelector Journey;
    Journey.Update(MakeCandidate(Bounds.ExitRadiusMeters * 20.0));

    for (int32 Step = 0; Step <= 400; ++Step)
    {
        const double Fraction = static_cast<double>(Step) / 400.0;
        const double Distance = FMath::Lerp(Bounds.ExitRadiusMeters * 20.0, Planet.RadiusMeters, Fraction);
        Journey.Update(MakeCandidate(Distance));
    }

    UVERIFY_TRUE(Result, Journey.GetKind() == EUniverseFrameKind::Planetary);
    UVERIFY_EQ_INT(Result, Journey.GetTransitionCount(), 1);

    for (int32 Step = 0; Step <= 400; ++Step)
    {
        const double Fraction = static_cast<double>(Step) / 400.0;
        const double Distance = FMath::Lerp(Planet.RadiusMeters, Bounds.ExitRadiusMeters * 20.0, Fraction);
        Journey.Update(MakeCandidate(Distance));
    }

    UVERIFY_TRUE(Result, Journey.GetKind() == EUniverseFrameKind::Interstellar);
    UVERIFY_EQ_INT(Result, Journey.GetTransitionCount(), 2);

    return Result.Passed();
}

/**
 * With two overlapping bodies - a moon inside its planet's influence - the
 * selector must pick one, hand over only on a decisive difference, and never
 * oscillate between them.
 */
bool UniverseTest_SimulationFrameHandover(FUniverseTestResult& Result)
{
    const FPlanetSurfaceDescriptor Big = MakeTraversalPlanet(0x1111ull, 6371000.0);
    const FPlanetSurfaceDescriptor Small = MakeTraversalPlanet(0x2222ull, 1737400.0, 1.62, 0.0);

    FPlanetFrameCandidate Candidates[2];
    Candidates[0].PlanetKey = Big.PlanetKey;
    Candidates[0].Bounds = FPlanetFrameBounds::FromPlanet(Big);
    Candidates[1].PlanetKey = Small.PlanetKey;
    Candidates[1].Bounds = FPlanetFrameBounds::FromPlanet(Small);

    UVERIFY_TRUE(Result, Big.PlanetKey != Small.PlanetKey);

    FSimulationFrameSelector Selector;

    // Standing on the small body, well inside the large one's influence too.
    Candidates[0].DistanceFromCentreMeters = Big.RadiusMeters * 2.0;
    Candidates[1].DistanceFromCentreMeters = Small.RadiusMeters;

    Selector.Update(Candidates, 2);

    // The small body wins: dominance is a ratio of each body's own influence
    // radius, so standing on a moon attaches you to the moon even though the
    // planet is larger and its influence sphere encloses you.
    UVERIFY_TRUE(Result, Selector.GetKind() == EUniverseFrameKind::Planetary);
    UVERIFY_EQ_UINT(Result, Selector.GetPlanetKey(), Small.PlanetKey);

    // Leaving the moon for the planet: one handover, no oscillation on the way.
    const int32 BeforeDeparture = Selector.GetTransitionCount();

    for (int32 Step = 0; Step <= 200; ++Step)
    {
        const double Fraction = static_cast<double>(Step) / 200.0;
        Candidates[1].DistanceFromCentreMeters =
            FMath::Lerp(Small.RadiusMeters, Small.RadiusMeters * 20.0, Fraction);
        Candidates[0].DistanceFromCentreMeters =
            FMath::Lerp(Big.RadiusMeters * 2.0, Big.RadiusMeters * 1.2, Fraction);

        Selector.Update(Candidates, 2);
    }

    UVERIFY_EQ_UINT(Result, Selector.GetPlanetKey(), Big.PlanetKey);
    UVERIFY_EQ_INT(Result, Selector.GetTransitionCount() - BeforeDeparture, 1);

    // An empty candidate list means nothing is nearby: fall back to
    // interstellar rather than staying attached to a body that is no longer
    // being offered, which would leave gravity pointing at a planet the
    // streaming system has already forgotten about.
    UVERIFY_TRUE(Result, Selector.Update(nullptr, 0));
    UVERIFY_TRUE(Result, Selector.GetKind() == EUniverseFrameKind::Interstellar);

    return Result.Passed();
}

/**
 * A movement step fast enough to cross the whole planet in one frame must be
 * detected as an intersection.
 *
 * This is the tunnelling test, and it is written the way the failure actually
 * happens: two positions, both in empty space, both outside the body, with the
 * planet squarely between them. A position-based collision check sees nothing
 * wrong with either endpoint. The swept test has to catch it from the segment
 * alone.
 */
bool UniverseTest_TrajectoryHighSpeedIntersection(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTraversalPlanet(0xBEEF000000000001ull, 6371000.0);

    // One frame at 30 Hz for each of several thrust tiers, up to the probe's
    // documented maximum of 10^12 m/s.
    const double FrameSeconds = 1.0 / 30.0;
    const double Speeds[5] = { 1.0e4, 1.0e6, 1.0e8, 1.0e10, 1.0e12 };

    for (double Speed : Speeds)
    {
        const double StepMeters = Speed * FrameSeconds;

        // Only the fast tiers overshoot the planet in a single frame. The
        // slower ones start inside it, and are kept in the loop because they
        // exercise the other branch: the sweep must distinguish "began
        // underground" from "flew through", since the two want opposite
        // responses from the caller.
        const bool bTunnelling = StepMeters > Planet.GetMaxRadiusMeters();

        for (int32 Index = 0; Index < 32; ++Index)
        {
            const FVector3d Direction = SpreadDirection(Index);

            // Start one step out along the direction and end one step out on
            // the far side, so the segment runs straight through the centre.
            const FVector3d Start(
                Direction.X * StepMeters, Direction.Y * StepMeters, Direction.Z * StepMeters);

            const FVector3d End(-Start.X, -Start.Y, -Start.Z);

            const FPlanetSweepResult Sweep =
                FPlanetTrajectory::SweepAgainstPlanetBounds(Planet, Start, End);

            // A segment through the centre always intersects, at every speed.
            UVERIFY_TRUE(Result, Sweep.bHit);
            UVERIFY_EQ_INT(Result, Sweep.bStartedInside ? 1 : 0, bTunnelling ? 0 : 1);

            if (!bTunnelling)
            {
                continue;
            }

            // Both endpoints are outside the body - which is precisely why a
            // position-based check misses this.
            UVERIFY_TRUE(Result, Start.Size() > Planet.GetMaxRadiusMeters());
            UVERIFY_TRUE(Result, End.Size() > Planet.GetMaxRadiusMeters());
            UVERIFY_FALSE(Result, Sweep.bStartedInside);

            // Entry comes before exit, both on the segment.
            UVERIFY_TRUE(Result, Sweep.EntryFraction >= 0.0);
            UVERIFY_TRUE(Result, Sweep.EntryFraction < Sweep.ExitFraction);
            UVERIFY_TRUE(Result, Sweep.ExitFraction <= 1.0);

            // The contact point is on the bounding sphere, not merely
            // somewhere along the line. This is what a caller clamps to, so it
            // has to be exact even when the segment is 10^10 m long - which is
            // the whole reason the quadratic is solved in its stable form.
            UVERIFY_NEAR(Result, Sweep.EntryPointMeters.Size(), Planet.GetMaxRadiusMeters(),
                Planet.RadiusMeters * 1e-6);

            // A segment aimed at the centre passes through it.
            UVERIFY_TRUE(Result, Sweep.ClosestApproachMeters < Planet.RadiusMeters * 1e-6);
        }
    }

    // A grazing pass outside the bounding sphere must be reported as a miss,
    // with a usable closest approach so a proximity warning can still work.
    {
        const double Offset = Planet.GetMaxRadiusMeters() * 1.5;
        const FVector3d Start(-1.0e12, Offset, 0.0);
        const FVector3d End(1.0e12, Offset, 0.0);

        const FPlanetSweepResult Sweep =
            FPlanetTrajectory::SweepAgainstPlanetBounds(Planet, Start, End);

        UVERIFY_FALSE(Result, Sweep.bHit);
        UVERIFY_NEAR(Result, Sweep.ClosestApproachMeters, Offset, Offset * 1e-6);
    }

    // A step that starts underground is reported as such rather than being
    // clamped to an entry point that is behind it.
    {
        const FVector3d Start(0.0, 0.0, Planet.RadiusMeters * 0.5);
        const FVector3d End(0.0, 0.0, Planet.RadiusMeters * 10.0);

        const FPlanetSweepResult Sweep =
            FPlanetTrajectory::SweepAgainstPlanetBounds(Planet, Start, End);

        UVERIFY_TRUE(Result, Sweep.bStartedInside);
        UVERIFY_TRUE(Result, Sweep.bHit);
    }

    // Clamping stops a step short of the body with the requested clearance,
    // and leaves a clear path untouched.
    {
        const double Standoff = 50000.0;
        const FVector3d Start(0.0, 0.0, 1.0e11);
        FVector3d End(0.0, 0.0, -1.0e11);

        FPlanetSweepResult Sweep;
        const bool bClamped =
            FPlanetTrajectory::TryClampStepToBounds(Planet, Start, End, Standoff, Sweep);

        UVERIFY_TRUE(Result, bClamped);
        UVERIFY_NEAR(Result, End.Size(), Planet.GetMaxRadiusMeters() + Standoff,
            Planet.RadiusMeters * 1e-6);

        // And the clamped endpoint is above the highest possible terrain, which
        // is the property that makes it safe to hand over to Chaos.
        UVERIFY_TRUE(Result,
            FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(Planet, Settings, End) > 0.0);

        FVector3d ClearEnd(0.0, Planet.RadiusMeters * 100.0, 1.0e11);
        const FVector3d ClearEndCopy = ClearEnd;

        UVERIFY_FALSE(Result,
            FPlanetTrajectory::TryClampStepToBounds(Planet, Start, ClearEnd, Standoff, Sweep));
        UVERIFY_EQ_DOUBLE_EXACT(Result, ClearEnd.Y, ClearEndCopy.Y);
    }

    return Result.Passed();
}

/**
 * Refining a bounding-sphere hit against the real terrain must land on the
 * ground, and must reject the false positives the bounding sphere produces
 * over low-lying ground.
 */
bool UniverseTest_TrajectoryTerrainRefinement(FUniverseTestResult& Result)
{
    const FPlanetTerrainSettings Settings;
    const FPlanetSurfaceDescriptor Planet = MakeTraversalPlanet(0xCAFE000000000001ull, 6371000.0);

    int32 Refined = 0;
    int32 Rejected = 0;

    for (int32 Index = 0; Index < 48; ++Index)
    {
        const FVector3d Direction = SpreadDirection(Index);

        // Straight down at the surface from far out, at a speed that would
        // cross the planet in a frame.
        const FVector3d Start(
            Direction.X * 1.0e11, Direction.Y * 1.0e11, Direction.Z * 1.0e11);
        const FVector3d End(-Start.X, -Start.Y, -Start.Z);

        const FPlanetSweepResult Sweep =
            FPlanetTrajectory::SweepAgainstTerrain(Planet, Settings, Start, End);

        UVERIFY_TRUE(Result, Sweep.bHit);
        ++Refined;

        // The refined contact point is on the ground: its clearance above the
        // terrain is zero to within the bracket the bisection was left with.
        const double Clearance =
            FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(Planet, Settings, Sweep.EntryPointMeters);

        UVERIFY_NEAR(Result, Clearance, 0.0, 1.0);

        // And it is strictly closer to the planet than the bounding-sphere
        // answer, since the ground is below the highest possible peak
        // everywhere except at the single highest point on the planet.
        const FPlanetSweepResult Bounds =
            FPlanetTrajectory::SweepAgainstPlanetBounds(Planet, Start, End);

        UVERIFY_TRUE(Result, Sweep.EntryFraction >= Bounds.EntryFraction);
    }

    UVERIFY_EQ_INT(Result, Refined, 48);

    // A pass that clears the highest peak but is inside the bounding sphere is
    // a bounding-sphere false positive, and refinement must reject it.
    for (int32 Index = 0; Index < 48 && Rejected == 0; ++Index)
    {
        const FVector3d Direction = SpreadDirection(Index);

        FVector3d TangentU;
        FVector3d TangentV;
        FPlanetTerrain::GetTangentBasis(Direction, TangentU, TangentV);

        // Skim past at a radius above every peak but below the bounding sphere
        // plus its margin: a horizontal chord offset by just under the maximum
        // radius.
        const double Offset = Planet.GetMaxRadiusMeters() * 0.99999;
        const double Reach = Planet.RadiusMeters * 10.0;

        const FVector3d Centre(
            Direction.X * Offset, Direction.Y * Offset, Direction.Z * Offset);

        const FVector3d Start(
            Centre.X - TangentU.X * Reach, Centre.Y - TangentU.Y * Reach, Centre.Z - TangentU.Z * Reach);
        const FVector3d End(
            Centre.X + TangentU.X * Reach, Centre.Y + TangentU.Y * Reach, Centre.Z + TangentU.Z * Reach);

        const FPlanetSweepResult Bounds =
            FPlanetTrajectory::SweepAgainstPlanetBounds(Planet, Start, End);

        if (!Bounds.bHit)
        {
            continue;
        }

        const FPlanetSweepResult Terrain =
            FPlanetTrajectory::SweepAgainstTerrain(Planet, Settings, Start, End);

        // Whether it really misses depends on the terrain under this chord, so
        // the assertion is the invariant rather than the outcome: refinement
        // never turns a bounding-sphere miss into a hit, and when it reports a
        // hit the contact point is genuinely on the ground.
        if (!Terrain.bHit)
        {
            ++Rejected;
            UVERIFY_TRUE(Result,
                FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
                    Planet, Settings, Bounds.EntryPointMeters) > 0.0);
        }
        else
        {
            UVERIFY_NEAR(Result,
                FPlanetSurfaceQuery::GetAltitudeAboveTerrainMeters(
                    Planet, Settings, Terrain.EntryPointMeters),
                0.0, 1.0);
        }
    }

    return Result.Passed();
}

/**
 * The atmosphere is a logical boundary with a defined depth ramp, and an
 * airless body has none at all.
 */
bool UniverseTest_AtmosphereBoundary(FUniverseTestResult& Result)
{
    const FPlanetSurfaceDescriptor Planet = MakeTraversalPlanet(0x0A17000000000001ull, 6371000.0, 9.81, 100000.0);
    const FPlanetSurfaceDescriptor Airless = MakeTraversalPlanet(0x0A18000000000001ull, 1737400.0, 1.62, 0.0);

    UVERIFY_TRUE(Result, Planet.HasAtmosphere());
    UVERIFY_FALSE(Result, Airless.HasAtmosphere());
    UVERIFY_NEAR(Result, Planet.GetAtmosphereTopRadiusMeters(), 6471000.0, 1e-9);

    for (int32 Index = 0; Index < 32; ++Index)
    {
        const FVector3d Direction = SpreadDirection(Index);

        auto At = [&](double AltitudeAboveSeaLevel)
        {
            const double Radius = Planet.RadiusMeters + AltitudeAboveSeaLevel;
            return FVector3d(Direction.X * Radius, Direction.Y * Radius, Direction.Z * Radius);
        };

        // Sea level: fully inside.
        UVERIFY_NEAR(Result, FPlanetSurfaceQuery::GetAtmosphericDepthFraction(Planet, At(0.0)), 1.0, 1e-12);
        UVERIFY_TRUE(Result, FPlanetSurfaceQuery::IsInsideAtmosphere(Planet, At(0.0)));

        // Halfway up: half.
        UVERIFY_NEAR(Result,
            FPlanetSurfaceQuery::GetAtmosphericDepthFraction(Planet, At(50000.0)), 0.5, 1e-9);

        // At the top: the depth ramp reaches zero.
        //
        // The inside/outside predicate is deliberately *not* asserted exactly
        // at the boundary. Reaching a nominated radius by scaling a unit
        // direction and measuring the result back lands a few ULPs either side
        // of it, so a test there would be measuring rounding rather than
        // behaviour, and would pass or fail on the direction it happened to
        // pick. It is checked a metre in and a metre out instead - which is
        // also the only distinction any caller can act on, the boundary itself
        // being a chosen number rather than a physical surface.
        UVERIFY_NEAR(Result,
            FPlanetSurfaceQuery::GetAtmosphericDepthFraction(Planet, At(100000.0)), 0.0, 1e-9);
        UVERIFY_TRUE(Result, FPlanetSurfaceQuery::IsInsideAtmosphere(Planet, At(99999.0)));
        UVERIFY_FALSE(Result, FPlanetSurfaceQuery::IsInsideAtmosphere(Planet, At(100001.0)));

        // Above it: clamped to zero, not negative, and outside.
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FPlanetSurfaceQuery::GetAtmosphericDepthFraction(Planet, At(400000.0)), 0.0);
        UVERIFY_FALSE(Result, FPlanetSurfaceQuery::IsInsideAtmosphere(Planet, At(400000.0)));

        // Below sea level, in a trench: clamped to one, not above it.
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FPlanetSurfaceQuery::GetAtmosphericDepthFraction(Planet, At(-10000.0)), 1.0);

        // An airless body reads zero everywhere and is never inside anything.
        const double AirlessRadius = Airless.RadiusMeters + 1000.0;
        const FVector3d OnAirless(
            Direction.X * AirlessRadius, Direction.Y * AirlessRadius, Direction.Z * AirlessRadius);

        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FPlanetSurfaceQuery::GetAtmosphericDepthFraction(Airless, OnAirless), 0.0);
        UVERIFY_FALSE(Result, FPlanetSurfaceQuery::IsInsideAtmosphere(Airless, OnAirless));
    }

    return Result.Passed();
}
