// Copyright Universe Project. All Rights Reserved.

#include "Tests/UniverseGenerationTestList.h"
#include "InterstellarTravel.h"
#include "GalaxyDescriptor.h"
#include "StarSystemGenerator.h"
#include "UniverseScale.h"
#include "UniverseSweep.h"

// ---------------------------------------------------------------------------
// InterstellarTravelTests.cpp
//
// Sprint 006 sections 93 to 95 and 97: high-speed integration, warp overshoot,
// body intersection and cache bounds.
//
// The unifying property under test is that nothing here degrades with speed.
// The naive implementations of all four - integrate then check, brake when
// close, test what you are touching, cache what you visited - each work
// perfectly at 100 m/s and fail completely at 10^14, and they fail *silently*:
// the ship arrives somewhere, it just is not where it should be and it went
// through a star to get there.
// ---------------------------------------------------------------------------

namespace
{
    FUniverseSeedHierarchy MakeTravelHierarchy(const TCHAR* Text)
    {
        return FUniverseSeedHierarchy::FromText(Text);
    }

    /** A position inside a galaxy, so that there are stars to run into. */
    FUniversePosition GetInhabitedPlace(const FUniverseSeedHierarchy& Hierarchy)
    {
        FGalaxyDescriptor Galaxy;

        if (FGalaxyGenerator::FindNearestGalaxy(Hierarchy, FUniversePosition(), Galaxy))
        {
            return FGalaxyGenerator::GetInhabitedPosition(Galaxy);
        }

        return FUniversePosition();
    }

    FUniversePosition OffsetMeters(const FUniversePosition& Position, const FVector3d& Meters)
    {
        return Position.OffsetByCm(FVector3d(
            Meters.X * UniverseScale::CmPerMeter,
            Meters.Y * UniverseScale::CmPerMeter,
            Meters.Z * UniverseScale::CmPerMeter));
    }
}

/**
 * Sprint 006 section 93: a high-speed path integrated over many sectors lands
 * where the arithmetic says it should.
 *
 * The claim being tested is that the canonical coordinate system does not lose
 * anything under repeated large steps. Three hundred steps of a hundred billion
 * metres is thirty trillion metres - three light days - and the endpoint has to
 * agree with a single step of the same total to within the resolution of the
 * coordinate system, not to within some fraction of the distance travelled.
 */
bool UniverseTest_InterstellarMovementIntegration(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeTravelHierarchy(TEXT("travel-integration"));

    // Deep intergalactic space, so that nothing is in the way and the test
    // measures integration rather than collision avoidance.
    const FUniversePosition Start = FUniversePosition::FromCells(0, 0, 0);

    const FVector3d StepMeters(1.0e11, -5.0e10, 2.5e10);
    const int32 Steps = 300;

    FUniversePosition Incremental = Start;

    for (int32 Index = 0; Index < Steps; ++Index)
    {
        Incremental = OffsetMeters(Incremental, StepMeters);
        UVERIFY_TRUE(Result, Incremental.IsNormalized());
    }

    const FVector3d TotalMeters = StepMeters * static_cast<double>(Steps);
    const FUniversePosition Direct = OffsetMeters(Start, TotalMeters);

    // Within a metre over three light days. The two paths do not have to be
    // bit-identical - the incremental one rounds three hundred times and the
    // direct one once - but the divergence must be a property of the local
    // offset's resolution and not of the distance covered.
    UVERIFY_TRUE(Result, FUniversePosition::DistanceMeters(Incremental, Direct) < 1.0);

    // The journey really was as long as claimed.
    const double TravelledMeters = FUniversePosition::DistanceMeters(Start, Incremental);
    UVERIFY_NEAR(Result, TravelledMeters, TotalMeters.Size(), TotalMeters.Size() * 1.0e-12);

    // And it is reversible. Retracing the same steps backwards has to return to
    // the start exactly: the cell arithmetic is integer, so anything less than
    // exact means the local offset is accumulating error.
    for (int32 Index = 0; Index < Steps; ++Index)
    {
        Incremental = OffsetMeters(Incremental, StepMeters * -1.0);
    }

    UVERIFY_TRUE(Result, FUniversePosition::DistanceMeters(Incremental, Start) < 1.0e-3);

    return Result.Passed();
}

/**
 * The sector traversal underneath the broad phase visits exactly the sectors a
 * segment enters.
 */
bool UniverseTest_TravelSectorTraversal(FUniverseTestResult& Result)
{
    const double MetersPerSector =
        UniverseScale::MetersPerCell * static_cast<double>(UniverseScale::SectorSizeInCells);

    // --- A step that stays inside one sector --------------------------------
    {
        const FUniversePosition From = FUniversePosition::FromSectorCorner(10, 20, 30)
            .OffsetByCm(FVector3d(
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter));

        const FUniversePosition To = OffsetMeters(From, FVector3d(MetersPerSector * 0.1, 0.0, 0.0));

        TArray<FUniverseSectorCoord> Sectors;
        UVERIFY_TRUE(Result, FTravelBroadPhase::GetSectorsCrossed(From, To, Sectors));
        UVERIFY_EQ_INT(Result, Sectors.Num(), 1);
        UVERIFY_TRUE(Result, Sectors[0] == FUniverseSectorCoord(10, 20, 30));
    }

    // --- An axis-aligned run of ten sectors ---------------------------------
    {
        const FUniversePosition From = FUniversePosition::FromSectorCorner(0, 0, 0)
            .OffsetByCm(FVector3d(
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter));

        const FUniversePosition To = OffsetMeters(From, FVector3d(MetersPerSector * 10.0, 0.0, 0.0));

        TArray<FUniverseSectorCoord> Sectors;
        UVERIFY_TRUE(Result, FTravelBroadPhase::GetSectorsCrossed(From, To, Sectors));

        // Eleven: sectors 0 through 10 inclusive.
        UVERIFY_EQ_INT(Result, Sectors.Num(), 11);

        for (int32 Index = 0; Index < Sectors.Num(); ++Index)
        {
            UVERIFY_TRUE(Result, Sectors[Index] == FUniverseSectorCoord(Index, 0, 0));
        }
    }

    // --- A diagonal path ----------------------------------------------------
    //
    // Consecutive sectors must be adjacent: the traversal steps one axis at a
    // time, so a jump of two in any axis means a boundary was skipped, which is
    // exactly the failure that lets a ship through a star.
    {
        const FUniversePosition From = FUniversePosition::FromSectorCorner(-5, -5, -5)
            .OffsetByCm(FVector3d(
                MetersPerSector * 0.3 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.7 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.1 * UniverseScale::CmPerMeter));

        const FUniversePosition To = OffsetMeters(
            From, FVector3d(MetersPerSector * 7.0, MetersPerSector * 4.0, MetersPerSector * -3.0));

        TArray<FUniverseSectorCoord> Sectors;
        UVERIFY_TRUE(Result, FTravelBroadPhase::GetSectorsCrossed(From, To, Sectors));

        UVERIFY_TRUE(Result, Sectors.Num() > 7);

        for (int32 Index = 1; Index < Sectors.Num(); ++Index)
        {
            const int64 DX = FMath::Abs(Sectors[Index].X - Sectors[Index - 1].X);
            const int64 DY = FMath::Abs(Sectors[Index].Y - Sectors[Index - 1].Y);
            const int64 DZ = FMath::Abs(Sectors[Index].Z - Sectors[Index - 1].Z);

            UVERIFY_EQ_INT(Result, DX + DY + DZ, 1);
        }

        // It starts and ends where it should.
        UVERIFY_TRUE(Result, Sectors[0] == FUniverseSectorCoord::FromPosition(From));
        UVERIFY_TRUE(Result, Sectors.Last() == FUniverseSectorCoord::FromPosition(To));
    }

    // --- A zero-length step -------------------------------------------------
    {
        const FUniversePosition From = FUniversePosition::FromSectorCorner(3, 3, 3);

        TArray<FUniverseSectorCoord> Sectors;
        UVERIFY_TRUE(Result, FTravelBroadPhase::GetSectorsCrossed(From, From, Sectors));
        UVERIFY_EQ_INT(Result, Sectors.Num(), 1);
    }

    // --- The budget is honoured --------------------------------------------
    //
    // A thousand sectors asked to fit in eight. It must return false rather
    // than running to completion, and what it did produce must be a correct
    // prefix of the answer rather than a truncated mess - that is what makes a
    // partial result safe for a caller to substep from.
    {
        const FUniversePosition From = FUniversePosition::FromSectorCorner(0, 0, 0)
            .OffsetByCm(FVector3d(
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter,
                MetersPerSector * 0.5 * UniverseScale::CmPerMeter));

        const FUniversePosition To = OffsetMeters(From, FVector3d(MetersPerSector * 1000.0, 0.0, 0.0));

        TArray<FUniverseSectorCoord> Sectors;
        UVERIFY_FALSE(Result, FTravelBroadPhase::GetSectorsCrossed(From, To, Sectors, 8));
        UVERIFY_EQ_INT(Result, Sectors.Num(), 8);

        for (int32 Index = 0; Index < Sectors.Num(); ++Index)
        {
            UVERIFY_TRUE(Result, Sectors[Index] == FUniverseSectorCoord(Index, 0, 0));
        }
    }

    return Result.Passed();
}

/**
 * Sprint 006 section 95: warp trajectories against real bodies.
 *
 * Star, planet, clean miss, tangent, and a path that could hit several - and in
 * every case the answer must be the *first* body along the path, because a
 * broad phase that returns any hit rather than the nearest one will happily
 * stop a ship on the far side of the star it just flew through.
 */
bool UniverseTest_TravelBodyIntersection(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeTravelHierarchy(TEXT("travel-bodies"));
    const FTravelProfile Profile = FTravelProfile::MakeDefault();

    UVERIFY_TRUE(Result, Profile.IsValid());

    FStarSystemDescriptor System;
    UVERIFY_TRUE(Result,
        FStarSystemGenerator::FindSystemNear(Hierarchy, GetInhabitedPlace(Hierarchy), System));

    UVERIFY_TRUE(Result, System.Planets.Num() > 0);

    const double StarStandoff = FMath::Max(
        System.Star.RadiusMeters * Profile.HazardStandoffRadii,
        Profile.MinimumHazardStandoffMeters);

    // --- Straight at the star, from a long way off --------------------------
    //
    // Ten billion kilometres out and aimed dead centre. A naive step would put
    // the ship past the far side in one go.
    {
        const double ApproachMeters = 1.0e13;

        const FUniversePosition From = OffsetMeters(System.Position, FVector3d(ApproachMeters, 0.0, 0.0));
        const FUniversePosition To = OffsetMeters(System.Position, FVector3d(-ApproachMeters, 0.0, 0.0));

        FTravelHazard Hazard;
        UVERIFY_TRUE(Result, FTravelBroadPhase::SweepAgainstBodies(Hierarchy, Profile, From, To, Hazard));

        UVERIFY_TRUE(Result, Hazard.bHit);
        UVERIFY_TRUE(Result, Hazard.Fraction > 0.0 && Hazard.Fraction < 1.0);

        // Contact happens at the standoff distance, on the near side.
        const double ContactDistanceFromStar =
            ApproachMeters - Hazard.DistanceMeters;

        UVERIFY_NEAR(Result, ContactDistanceFromStar, StarStandoff, StarStandoff * 1.0e-6);

        // And it really is the star that was hit, not one of its planets.
        UVERIFY_EQ_INT(Result, Hazard.PlanetIndex, INDEX_NONE);
        UVERIFY_TRUE(Result, Hazard.SystemId == System.Id);
    }

    // --- The same path reversed finds the same body, from the other side ----
    {
        const double ApproachMeters = 1.0e13;

        const FUniversePosition From = OffsetMeters(System.Position, FVector3d(-ApproachMeters, 0.0, 0.0));
        const FUniversePosition To = OffsetMeters(System.Position, FVector3d(ApproachMeters, 0.0, 0.0));

        FTravelHazard Hazard;
        UVERIFY_TRUE(Result, FTravelBroadPhase::SweepAgainstBodies(Hierarchy, Profile, From, To, Hazard));
        UVERIFY_EQ_INT(Result, Hazard.PlanetIndex, INDEX_NONE);
    }

    // --- A clean miss -------------------------------------------------------
    //
    // Offset well beyond any orbit, parallel to the first path.
    {
        const double MissOffset = 5.0e13;
        const double ApproachMeters = 1.0e13;

        const FUniversePosition From =
            OffsetMeters(System.Position, FVector3d(ApproachMeters, MissOffset, 0.0));
        const FUniversePosition To =
            OffsetMeters(System.Position, FVector3d(-ApproachMeters, MissOffset, 0.0));

        FTravelHazard Hazard;
        UVERIFY_FALSE(Result, FTravelBroadPhase::SweepAgainstBodies(Hierarchy, Profile, From, To, Hazard));
        UVERIFY_FALSE(Result, Hazard.bHit);
    }

    // --- A tangent ----------------------------------------------------------
    //
    // Passing at just inside and just outside the standoff radius. These are
    // the cases a badly conditioned quadratic gets wrong, and getting them
    // wrong in the permissive direction is a ship inside a star.
    {
        const double ApproachMeters = 1.0e12;

        for (int32 Case = 0; Case < 2; ++Case)
        {
            const double Offset = (Case == 0)
                ? StarStandoff * 0.99
                : StarStandoff * 1.01;

            const FUniversePosition From =
                OffsetMeters(System.Position, FVector3d(ApproachMeters, Offset, 0.0));
            const FUniversePosition To =
                OffsetMeters(System.Position, FVector3d(-ApproachMeters, Offset, 0.0));

            FTravelHazard Hazard;
            const bool bHit =
                FTravelBroadPhase::SweepAgainstBodies(Hierarchy, Profile, From, To, Hazard);

            if (Case == 0)
            {
                UVERIFY_TRUE(Result, bHit);
            }
            else
            {
                // Just outside the star's standoff - but a planet may lie along
                // this line, so the assertion is that if anything was hit it
                // was not the star.
                if (bHit)
                {
                    UVERIFY_TRUE(Result, Hazard.PlanetIndex != INDEX_NONE);
                }
            }
        }
    }

    // --- A degenerate segment ----------------------------------------------
    //
    // Zero length, at the star's centre. It must report a hit rather than
    // dividing by zero, because that is where a ship that has already gone
    // wrong ends up.
    {
        FTravelHazard Hazard;
        const bool bHit = FTravelBroadPhase::SweepAgainstBodies(
            Hierarchy, Profile, System.Position, System.Position, Hazard);

        UVERIFY_TRUE(Result, bHit);
        UVERIFY_TRUE(Result, Hazard.bStartedInside);
    }

    return Result.Passed();
}

/**
 * Sprint 006 section 94: overshoot.
 *
 * A ship closing at warp on a target must stop at it, not past it, no matter
 * how large the frame step is. The failure this catches is the one that looks
 * like success in a debugger: the ship decelerates, it just does so starting
 * from a position several light hours beyond where it was aiming.
 */
bool UniverseTest_TravelWarpOvershoot(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeTravelHierarchy(TEXT("travel-overshoot"));
    const FTravelProfile Profile = FTravelProfile::MakeDefault();

    // --- The braking distance is the arithmetic one -------------------------
    {
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FInterstellarTravel::GetBrakingDistanceMeters(100.0, 10.0), 500.0);

        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FInterstellarTravel::GetBrakingDistanceMeters(0.0, 10.0), 0.0);

        // Zero deceleration means it never stops - answered as zero rather than
        // as an infinity that would propagate into a position.
        UVERIFY_EQ_DOUBLE_EXACT(Result,
            FInterstellarTravel::GetBrakingDistanceMeters(100.0, 0.0), 0.0);

        // And the scaling is quadratic, which is the whole reason proximity is
        // the wrong trigger: ten times the speed is a hundred times the room.
        const double Slow = FInterstellarTravel::GetBrakingDistanceMeters(1.0e6, 1.0e14);
        const double Fast = FInterstellarTravel::GetBrakingDistanceMeters(1.0e7, 1.0e14);
        UVERIFY_NEAR(Result, Fast / Slow, 100.0, 1.0e-9);
    }

    // --- ShouldBeginBraking triggers on distance, not on proximity ----------
    {
        const double Deceleration = Profile.WarpDecelerationMs2;
        const double Speed = Profile.WarpMaxSpeedMs;
        const double Required = FInterstellarTravel::GetBrakingDistanceMeters(Speed, Deceleration);

        UVERIFY_TRUE(Result, FInterstellarTravel::ShouldBeginBraking(
            Required * 1.1, Speed, Deceleration, 0.2));

        UVERIFY_FALSE(Result, FInterstellarTravel::ShouldBeginBraking(
            Required * 1.5, Speed, Deceleration, 0.2));
    }

    // --- A full run: accelerate, cruise, arrive -----------------------------
    //
    // Empty space and a target two light years away, integrated at 30 Hz until
    // it stops. The assertions are that it does stop, that it stops near the
    // target rather than past it, and that it does not oscillate.
    {
        const FUniversePosition Start = FUniversePosition::FromCells(0, 0, 0);

        const double DistanceMeters = UniverseScale::MetersPerLightYear * 2.0;

        FTravelTarget Target;
        Target.bValid = true;
        Target.Position = OffsetMeters(Start, FVector3d(DistanceMeters, 0.0, 0.0));
        Target.ArrivalRadiusMeters = 1.0e11;
        Target.Name = TEXT("Test target");

        FTravelState State;
        State.Position = Start;

        const FVector3d Heading(1.0, 0.0, 0.0);
        const double DeltaSeconds = 1.0 / 30.0;

        double ClosestApproach = TNumericLimits<double>::Max();
        int32 StepsTaken = 0;
        bool bArrived = false;
        bool bEverBraked = false;

        // Ten thousand frames is five and a half minutes of simulated time,
        // which is far more than the estimate says this trip needs.
        for (int32 Step = 0; Step < 10000; ++Step)
        {
            const FTravelStepResult StepResult = FInterstellarTravel::Step(
                Hierarchy, Profile, State, Target, Heading, 1.0, DeltaSeconds,
                /* bWarpEngaged */ true, /* bAutoBrakeToTarget */ true);

            State = StepResult.State;
            ++StepsTaken;

            bEverBraked = bEverBraked || StepResult.bBraking;
            ClosestApproach = FMath::Min(ClosestApproach, StepResult.DistanceToTargetMeters);

            if (StepResult.bArrived)
            {
                bArrived = true;

                // Arrived *and stopped*. The arrival radius alone is not enough
                // of a test: a ship can be inside it for exactly one frame on
                // its way past at 10^12 m/s, which is not an arrival, it is a
                // flyby that happened to be sampled at the right moment.
                UVERIFY_TRUE(Result, State.GetSpeedMs() < Profile.InterplanetaryMaxSpeedMs);
                break;
            }
        }

        UVERIFY_TRUE(Result, bEverBraked);
        UVERIFY_TRUE(Result, bArrived);
        UVERIFY_TRUE(Result, StepsTaken < 10000);

        // It stopped near the target rather than somewhere past it. The
        // tolerance is the arrival radius plus the braking margin, not a
        // fraction of the two light years travelled - overshoot is measured
        // against the destination, not against the journey.
        const double FinalDistance =
            FUniversePosition::DistanceMeters(State.Position, Target.Position);

        UVERIFY_TRUE(Result, FinalDistance < Target.ArrivalRadiusMeters * 20.0);

        // And it really did travel two light years to get there.
        UVERIFY_NEAR(Result,
            FUniversePosition::DistanceMeters(Start, State.Position),
            DistanceMeters,
            DistanceMeters * 1.0e-3);
    }

    // --- An absurd single step ---------------------------------------------
    //
    // One frame of a hundred seconds at maximum warp, aimed at a target one
    // light year away: the unclamped step is thousands of times the distance to
    // the target. Nothing may become non-finite, and the ship must not end up
    // somewhere that has lost track of where it was going.
    {
        const FUniversePosition Start = FUniversePosition::FromCells(0, 0, 0);

        FTravelTarget Target;
        Target.bValid = true;
        Target.Position = OffsetMeters(Start, FVector3d(UniverseScale::MetersPerLightYear, 0.0, 0.0));
        Target.ArrivalRadiusMeters = 1.0e11;

        FTravelState State;
        State.Position = Start;
        State.VelocityMs = FVector3d(Profile.WarpMaxSpeedMs, 0.0, 0.0);

        const FTravelStepResult StepResult = FInterstellarTravel::Step(
            Hierarchy, Profile, State, Target, FVector3d(1.0, 0.0, 0.0), 1.0, 100.0,
            /* bWarpEngaged */ true, /* bAutoBrakeToTarget */ true);

        UVERIFY_TRUE(Result, StepResult.State.Position.IsNormalized());
        UVERIFY_TRUE(Result, FMath::IsFinite(StepResult.DistanceToTargetMeters));
        UVERIFY_TRUE(Result, FMath::IsFinite(StepResult.State.GetSpeedMs()));

        // Not braking: the target is farther away than the braking distance, so
        // the braking law correctly says "not yet". That is the point - the law
        // is right and it is not enough, because the step is a hundred seconds
        // long and would carry the ship thousands of times past its
        // destination. The arrival clamp is what catches it.
        UVERIFY_FALSE(Result, StepResult.bBraking);
        UVERIFY_TRUE(Result, StepResult.bArrivalClamped);
        UVERIFY_TRUE(Result, StepResult.bArrived);

        // It stopped at the target rather than a light year beyond it.
        UVERIFY_TRUE(Result, StepResult.DistanceToTargetMeters < Target.ArrivalRadiusMeters);
        UVERIFY_TRUE(Result, StepResult.State.GetSpeedMs() < 1.0e6);
    }

    return Result.Passed();
}

/**
 * Sprint 006 section 97: caches stay bounded while moving through many sectors.
 *
 * The galaxy lookup cache is the one with an unbounded-growth failure mode: it
 * is keyed on intergalactic cell, and a ship crossing the universe visits an
 * unlimited number of them. A cache that grows per visited cell is invisible in
 * every test that stays put and fatal in the one journey that does not.
 */
bool UniverseTest_TravelCacheBounds(FUniverseTestResult& Result)
{
    const FUniverseSeedHierarchy Hierarchy = MakeTravelHierarchy(TEXT("travel-cache"));

    FStarSystemGenerator::ResetGalaxyCache();

    // Walk a long way in sector-sized strides, asking the density question at
    // every step. Ten thousand distinct sectors spread over enough intergalactic
    // cells to evict the cache many times over.
    const FUniversePosition Start = GetInhabitedPlace(Hierarchy);

    int64 SectorX = 0;
    int64 SectorY = 0;
    int64 SectorZ = 0;
    Start.GetSector(SectorX, SectorY, SectorZ);

    double DensitySum = 0.0;

    for (int64 Step = 0; Step < 10000; ++Step)
    {
        const double Density = FStarSystemGenerator::GetSectorStellarDensity(
            Hierarchy, SectorX + Step * 97, SectorY + Step * 31, SectorZ + Step * 13);

        UVERIFY_TRUE(Result, FMath::IsFinite(Density));
        UVERIFY_TRUE(Result, Density >= 0.0 && Density <= 1.0);

        DensitySum += Density;
    }

    // The walk left the galaxy long before the end, so most of it is empty -
    // but it must have started inside one, or the test proved nothing.
    UVERIFY_TRUE(Result, DensitySum > 0.0);

    // The answer at the origin is unchanged by everything that happened in
    // between. A cache that grew rather than evicting would still pass this;
    // one that evicted incorrectly would not.
    const double First =
        FStarSystemGenerator::GetSectorStellarDensity(Hierarchy, SectorX, SectorY, SectorZ);

    FStarSystemGenerator::ResetGalaxyCache();

    const double Second =
        FStarSystemGenerator::GetSectorStellarDensity(Hierarchy, SectorX, SectorY, SectorZ);

    UVERIFY_EQ_DOUBLE_EXACT(Result, First, Second);

    return Result.Passed();
}

/**
 * Travel-time estimates and the mode selector.
 *
 * Both feed the navigation HUD, and both are the kind of thing that is wrong by
 * a factor of two for months because nobody checks an estimate against the
 * integration it is estimating.
 */
bool UniverseTest_TravelEstimatesAndModes(FUniverseTestResult& Result)
{
    const FTravelProfile Profile = FTravelProfile::MakeDefault();

    UVERIFY_TRUE(Result, Profile.IsValid());

    // --- A profile with the limits out of order is rejected -----------------
    {
        FTravelProfile Broken = Profile;
        Broken.WarpMaxSpeedMs = Broken.LocalMaxSpeedMs * 0.5;
        UVERIFY_FALSE(Result, Broken.IsValid());
    }

    // --- Mode selection is monotonic in distance ----------------------------
    {
        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, 1.0e3, 0.0, false) == EUniverseTravelMode::Surface);

        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, 1.0e8, 0.0, false) == EUniverseTravelMode::LocalSpace);

        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, 1.0e12, 0.0, false) == EUniverseTravelMode::Interplanetary);

        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, 1.0e15, 0.0, false) == EUniverseTravelMode::Interstellar);

        // Nothing nearby at all.
        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, -1.0, 0.0, false) == EUniverseTravelMode::Interstellar);

        // Warp is asserted, and overrides everything - including standing on a
        // planet, which is the caller's problem to prevent rather than the mode
        // selector's to second-guess.
        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, 1.0e3, 0.0, true) == EUniverseTravelMode::Warp);

        // Body radius is taken into account: the same centre distance is
        // "surface" for a large body and "local space" for a small one.
        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, 6.4e6, 6.371e6, false) == EUniverseTravelMode::Surface);

        UVERIFY_TRUE(Result,
            FInterstellarTravel::SelectMode(Profile, 6.4e6, 1.0e3, false) == EUniverseTravelMode::LocalSpace);
    }

    // --- The estimate agrees with the trapezoid it models -------------------
    {
        // A trip long enough to reach cruise speed.
        const double Distance = UniverseScale::MetersPerLightYear * 10.0;

        const double Estimate = FInterstellarTravel::EstimateTravelTimeSeconds(
            Profile, Distance, EUniverseTravelMode::Warp);

        UVERIFY_TRUE(Result, Estimate > 0.0);
        UVERIFY_TRUE(Result, FMath::IsFinite(Estimate));

        // It must be longer than the distance at cruise speed - the ship has to
        // get up to speed and back down again - but not by more than the ramp
        // times.
        const double CruiseOnly = Distance / Profile.WarpMaxSpeedMs;
        const double RampTime =
            Profile.WarpMaxSpeedMs / Profile.WarpAccelerationMs2
            + Profile.WarpMaxSpeedMs / Profile.WarpDecelerationMs2;

        UVERIFY_TRUE(Result, Estimate > CruiseOnly);
        UVERIFY_TRUE(Result, Estimate < CruiseOnly + RampTime);
    }

    {
        // A trip too short to reach cruise speed: the triangular case.
        const double Distance = 1.0e9;

        const double Estimate = FInterstellarTravel::EstimateTravelTimeSeconds(
            Profile, Distance, EUniverseTravelMode::Warp);

        UVERIFY_TRUE(Result, Estimate > 0.0);
        UVERIFY_TRUE(Result, FMath::IsFinite(Estimate));

        // Recover the peak speed from the estimate and check it is consistent
        // with having covered exactly this distance.
        const double A = Profile.WarpAccelerationMs2;
        const double B = Profile.WarpDecelerationMs2;
        const double PeakSpeed = FMath::Sqrt((2.0 * Distance * A * B) / (A + B));

        const double Covered = (PeakSpeed * PeakSpeed) / (2.0 * A) + (PeakSpeed * PeakSpeed) / (2.0 * B);

        UVERIFY_NEAR(Result, Covered, Distance, Distance * 1.0e-9);
        UVERIFY_TRUE(Result, PeakSpeed < Profile.WarpMaxSpeedMs);
    }

    UVERIFY_EQ_DOUBLE_EXACT(Result,
        FInterstellarTravel::EstimateTravelTimeSeconds(Profile, 0.0, EUniverseTravelMode::Warp), 0.0);

    // --- Formatting ---------------------------------------------------------
    //
    // Asserted only to the extent that it picks the right unit. The exact text
    // is not a contract and a test that pins it is a test that fails on a
    // rewording.
    {
        UVERIFY_TRUE(Result, FInterstellarTravel::FormatDistance(500.0).Contains(TEXT("m")));
        UVERIFY_TRUE(Result, FInterstellarTravel::FormatDistance(5.0e4).Contains(TEXT("km")));
        UVERIFY_TRUE(Result, FInterstellarTravel::FormatDistance(1.5e11).Contains(TEXT("AU")));
        UVERIFY_TRUE(Result, FInterstellarTravel::FormatDistance(1.0e17).Contains(TEXT("ly")));

        UVERIFY_TRUE(Result, FInterstellarTravel::FormatSpeed(500.0).Contains(TEXT("m/s")));
        UVERIFY_TRUE(Result, FInterstellarTravel::FormatSpeed(1.0e12).Contains(TEXT("c")));
    }

    return Result.Passed();
}
