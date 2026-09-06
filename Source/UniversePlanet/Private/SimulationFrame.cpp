// Copyright Universe Project. All Rights Reserved.

#include "SimulationFrame.h"

const TCHAR* LexToString(EUniverseFrameKind Kind)
{
    switch (Kind)
    {
    case EUniverseFrameKind::Planetary:    return TEXT("Planetary");
    case EUniverseFrameKind::Interstellar: return TEXT("Interstellar");
    default:                               return TEXT("Unknown");
    }
}

FPlanetFrameBounds FPlanetFrameBounds::FromPlanet(const FPlanetSurfaceDescriptor& Planet)
{
    FPlanetFrameBounds Bounds;

    if (Planet.RadiusMeters <= 0.0)
    {
        return Bounds;
    }

    Bounds.PlanetRadiusMeters = Planet.RadiusMeters;
    Bounds.AtmosphereHeightMeters = Planet.AtmosphereHeightMeters;

    // Whichever is larger: a fixed number of planet radii, or enough clearance
    // above the atmosphere that the frame is always entered before the air is.
    // For every body generated so far the first term wins by a wide margin -
    // three radii against roughly 1.16 - but the second is what keeps the rule
    // honest if a body with a genuinely deep atmosphere is ever generated,
    // rather than leaving a planet whose air starts outside its own frame.
    const double FromRadius = Planet.RadiusMeters * InfluenceRadiiOfPlanet;
    const double FromAtmosphere =
        Planet.RadiusMeters + Planet.AtmosphereHeightMeters * InfluenceAtmosphereMultiplier;

    Bounds.EnterRadiusMeters = FMath::Max(FromRadius, FromAtmosphere);
    Bounds.ExitRadiusMeters = Bounds.EnterRadiusMeters * ExitRadiusMultiplier;

    return Bounds;
}

double FPlanetFrameCandidate::GetDominance() const
{
    if (Bounds.EnterRadiusMeters <= 0.0)
    {
        return TNumericLimits<double>::Max();
    }

    return DistanceFromCentreMeters / Bounds.EnterRadiusMeters;
}

bool FSimulationFrameSelector::Update(const FPlanetFrameCandidate& Candidate)
{
    return Update(&Candidate, 1);
}

bool FSimulationFrameSelector::Update(const FPlanetFrameCandidate* Candidates, int32 Count)
{
    const FUniverseFrameState Previous = State;

    // The strongest claim on offer, and separately the entry for the planet we
    // are currently attached to. Both are needed: the current planet decides
    // whether we stay, the best candidate decides where we go if we do not.
    const FPlanetFrameCandidate* Best = nullptr;
    const FPlanetFrameCandidate* Current = nullptr;

    for (int32 Index = 0; Index < Count; ++Index)
    {
        const FPlanetFrameCandidate& Candidate = Candidates[Index];

        if (!Candidate.IsValid())
        {
            continue;
        }

        if (State.IsPlanetary() && Candidate.PlanetKey == State.PlanetKey)
        {
            Current = &Candidate;
        }

        if (Best == nullptr || Candidate.GetDominance() < Best->GetDominance())
        {
            Best = &Candidate;
        }
    }

    if (Current != nullptr)
    {
        const double CurrentDominance = Current->GetDominance();

        // Still attached: the observer has not reached the exit radius. Note
        // this is the *exit* radius, not the enter radius - being between the
        // two is the whole purpose of the band, and testing against the enter
        // radius here would collapse the hysteresis to a single threshold.
        if (Current->DistanceFromCentreMeters <= Current->Bounds.ExitRadiusMeters)
        {
            // Unless another body has a decisively stronger claim, which only
            // happens where two influence spheres overlap.
            const bool bHandOver =
                Best != nullptr
                && Best->PlanetKey != Current->PlanetKey
                && Best->GetDominance() <= 1.0
                && Best->GetDominance() < CurrentDominance * SwitchDominanceAdvantage;

            if (!bHandOver)
            {
                Dominance = CurrentDominance;
                return false;
            }
        }
    }

    // Either unattached, or past the exit radius, or handing over: whoever has
    // the strongest claim takes the observer, if anyone can claim them at all.
    if (Best != nullptr && Best->GetDominance() <= 1.0)
    {
        State.Kind = EUniverseFrameKind::Planetary;
        State.PlanetKey = Best->PlanetKey;
        Dominance = Best->GetDominance();
    }
    else
    {
        State.Kind = EUniverseFrameKind::Interstellar;
        State.PlanetKey = 0;
        Dominance = (Best != nullptr) ? Best->GetDominance() : 0.0;
    }

    if (State != Previous)
    {
        ++TransitionCount;
        return true;
    }

    return false;
}

void FSimulationFrameSelector::Reset()
{
    State = FUniverseFrameState();
    Dominance = 0.0;
    TransitionCount = 0;
}
