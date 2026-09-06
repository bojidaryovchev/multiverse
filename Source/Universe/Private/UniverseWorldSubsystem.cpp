// Copyright Universe Project. All Rights Reserved.

#include "UniverseWorldSubsystem.h"
#include "UniverseAnchorComponent.h"
#include "PlanetActor.h"
#include "StarSystemGenerator.h"
#include "UniverseScale.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverse, Log, All);

void UUniverseWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // A world always has a valid universe, even before anything configures it.
    // FromText's empty-string path gives a known default rather than an
    // arbitrary one, so an unconfigured world is still reproducible.
    if (UniverseSeedText.IsEmpty())
    {
        UniverseSeedText = TEXT("sprint-001");
    }
    SeedHierarchy = FUniverseSeedHierarchy::FromText(*UniverseSeedText);

    UE_LOG(LogUniverse, Log,
        TEXT("Universe subsystem initialised. Seed \"%s\" -> 0x%016llX. Cell %.0f cm, sector %.4f ly."),
        *UniverseSeedText,
        static_cast<unsigned long long>(SeedHierarchy.GetUniverseSeedValue()),
        UniverseScale::CellSizeCmD,
        UniverseScale::SectorSizeLightYears);
}

void UUniverseWorldSubsystem::Deinitialize()
{
    Anchors.Reset();
    TrackedAnchor = nullptr;
    Super::Deinitialize();
}

bool UUniverseWorldSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    // Game and PIE only. An editor-preview world has no player to track and
    // rebasing there would fight the editor viewport.
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UUniverseWorldSubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UUniverseWorldSubsystem, STATGROUP_Tickables);
}

void UUniverseWorldSubsystem::SetUniverseSeedText(const FString& SeedText)
{
    UniverseSeedText = SeedText;
    SeedHierarchy = FUniverseSeedHierarchy::FromText(*UniverseSeedText);

    // The cached nearest system belongs to the old universe.
    bHasNearestSystem = false;
    NearestSystemDistanceLy = -1.0;

    UE_LOG(LogUniverse, Log, TEXT("Universe re-seeded: \"%s\" -> 0x%016llX"),
        *UniverseSeedText,
        static_cast<unsigned long long>(SeedHierarchy.GetUniverseSeedValue()));
}

FString UUniverseWorldSubsystem::GetUniverseSeedHex() const
{
    return FString::Printf(TEXT("0x%016llX"),
        static_cast<unsigned long long>(SeedHierarchy.GetUniverseSeedValue()));
}

void UUniverseWorldSubsystem::SetPresentation(const FUniversePresentationSettings& InSettings)
{
    Presentation = InSettings;
    ResyncAllAnchors();
}

bool UUniverseWorldSubsystem::TryGetRenderLocation(
    const FUniversePosition& Position,
    EUniverseRenderSpace Space,
    FVector& OutLocation) const
{
    FVector3d RelativeCm;
    if (!FUniversePosition::TryGetRelativeCm(RenderOrigin, Position, RelativeCm))
    {
        return false;
    }

    const double Scale = (Space == EUniverseRenderSpace::ScaledAstronomical)
        ? Presentation.AstronomicalScale
        : 1.0;

    const FVector3d Scaled = RelativeCm * Scale;

    // Reject anything that would leave Unreal's usable double-precision world
    // range. UE_LARGE_WORLD_MAX is about 8.8e12 cm; a transform beyond it is
    // not merely imprecise, it breaks culling and physics in ways that are
    // hard to attribute later.
    constexpr double MaxRenderCm = 8.0e12;
    if (FMath::Abs(Scaled.X) > MaxRenderCm
     || FMath::Abs(Scaled.Y) > MaxRenderCm
     || FMath::Abs(Scaled.Z) > MaxRenderCm)
    {
        return false;
    }

    OutLocation = FVector(Scaled.X, Scaled.Y, Scaled.Z);
    return true;
}

FUniversePosition UUniverseWorldSubsystem::RenderLocationToUniverse(const FVector& Location) const
{
    return RenderOrigin.OffsetByCm(FVector3d(Location.X, Location.Y, Location.Z));
}

void UUniverseWorldSubsystem::SetRenderOrigin(const FUniversePosition& NewOrigin)
{
    // Record the shift before changing the origin, so diagnostics can show
    // that the scene moved by exactly the amount the viewpoint had drifted.
    FVector3d ShiftCm = FVector3d::ZeroVector;
    FUniversePosition::TryGetRelativeCm(RenderOrigin, NewOrigin, ShiftCm);
    LastRebaseShift = FVector(ShiftCm.X, ShiftCm.Y, ShiftCm.Z);

    RenderOrigin = NewOrigin;
    ++RebaseCount;

    ResyncAllAnchors();
}

void UUniverseWorldSubsystem::RegisterAnchor(UUniverseAnchorComponent* Anchor)
{
    if (Anchor == nullptr)
    {
        return;
    }
    Anchors.AddUnique(Anchor);
}

void UUniverseWorldSubsystem::UnregisterAnchor(UUniverseAnchorComponent* Anchor)
{
    if (Anchor == nullptr)
    {
        return;
    }
    Anchors.RemoveAll([Anchor](const TWeakObjectPtr<UUniverseAnchorComponent>& Entry)
    {
        return !Entry.IsValid() || Entry.Get() == Anchor;
    });

    if (TrackedAnchor.Get() == Anchor)
    {
        TrackedAnchor = nullptr;
    }
}

void UUniverseWorldSubsystem::SetTrackedAnchor(UUniverseAnchorComponent* Anchor)
{
    TrackedAnchor = Anchor;

    // Start the session with the viewpoint exactly at the Unreal origin, so
    // the first frame is already in the best-precision configuration instead
    // of waiting for the first drift-triggered rebase.
    if (Anchor != nullptr)
    {
        SetRenderOrigin(Anchor->GetUniversePosition());
    }
}

void UUniverseWorldSubsystem::ResyncAllAnchors()
{
    // Compact away anchors whose actors have been destroyed while iterating.
    for (int32 Index = Anchors.Num() - 1; Index >= 0; --Index)
    {
        UUniverseAnchorComponent* Anchor = Anchors[Index].Get();
        if (Anchor == nullptr)
        {
            Anchors.RemoveAtSwap(Index);
            continue;
        }
        Anchor->SyncTransformToOrigin();
    }
}

void UUniverseWorldSubsystem::RebaseIfNeeded()
{
    UUniverseAnchorComponent* Anchor = TrackedAnchor.Get();
    if (Anchor == nullptr)
    {
        return;
    }

    const AActor* Owner = Anchor->GetOwner();
    if (Owner == nullptr)
    {
        return;
    }

    // Drift is measured in Local space, because that is the space whose float
    // precision the rebase exists to protect.
    const double DriftSquared = Owner->GetActorLocation().SizeSquared();
    const double Threshold = Presentation.RebaseRadiusCm;

    if (DriftSquared <= Threshold * Threshold)
    {
        return;
    }

    // Moving the origin to the viewpoint puts the viewpoint at (0,0,0) and
    // shifts every anchored actor by the same delta in the same frame. The
    // camera is on the tracked actor, so nothing changes relative to it and
    // the rebase is invisible - which is the property Phase F has to prove.
    SetRenderOrigin(Anchor->GetUniversePosition());

    UE_LOG(LogUniverse, Verbose,
        TEXT("Rebased render origin (#%d) by %.1f cm. New origin: %s"),
        RebaseCount, LastRebaseShift.Size(), *RenderOrigin.ToCompactString());
}

void UUniverseWorldSubsystem::UpdateNearestSystem()
{
    const UUniverseAnchorComponent* Anchor = TrackedAnchor.Get();
    if (Anchor == nullptr)
    {
        bHasNearestSystem = false;
        NearestSystemDistanceLy = -1.0;
        return;
    }

    const FUniversePosition& Centre = Anchor->GetUniversePosition();

    FStarSystemDescriptor Found;
    if (FStarSystemGenerator::FindNearestSystem(SeedHierarchy, Centre, NearestSearchRadiusLy, Found))
    {
        NearestSystem = Found;
        bHasNearestSystem = true;
        NearestSystemDistanceLy = FUniversePosition::DistanceLightYears(Centre, Found.Position);
    }
    else
    {
        bHasNearestSystem = false;
        NearestSystemDistanceLy = -1.0;
    }
}

bool UUniverseWorldSubsystem::GetNearestSystem(FStarSystemDescriptor& OutSystem) const
{
    if (!bHasNearestSystem)
    {
        return false;
    }
    OutSystem = NearestSystem;
    return true;
}

double UUniverseWorldSubsystem::GetNearestSystemDistanceLightYears() const
{
    return NearestSystemDistanceLy;
}

void UUniverseWorldSubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    RebaseIfNeeded();

    // Before the nearest-system search, because the frame decides which planet
    // gravity comes from and pawns read it later in the same frame.
    UpdateSimulationFrame();

    TimeSinceNearestSearch += static_cast<double>(DeltaTime);
    if (TimeSinceNearestSearch >= NearestSearchIntervalSeconds)
    {
        TimeSinceNearestSearch = 0.0;
        UpdateNearestSystem();
    }
}

// --- Planets and the simulation frame ---------------------------------------

void UUniverseWorldSubsystem::RegisterPlanet(APlanetActor* Planet)
{
    if (Planet == nullptr)
    {
        return;
    }

    Planets.AddUnique(Planet);
}

void UUniverseWorldSubsystem::UnregisterPlanet(APlanetActor* Planet)
{
    Planets.RemoveAll([Planet](const TWeakObjectPtr<APlanetActor>& Entry)
    {
        return !Entry.IsValid() || Entry.Get() == Planet;
    });

    if (FramePlanet.Get() == Planet)
    {
        // Dropping the planet the frame is attached to without telling the
        // selector would leave gravity pointing at a destroyed actor. Reset
        // instead, and let the next tick reattach if another body qualifies.
        FramePlanet.Reset();
        FrameSelector.Reset();
    }
}

void UUniverseWorldSubsystem::UpdateSimulationFrame()
{
    const UUniverseAnchorComponent* Tracked = TrackedAnchor.Get();

    const FUniversePosition Observer =
        (Tracked != nullptr) ? Tracked->GetUniversePosition() : RenderOrigin;

    TArray<FPlanetFrameCandidate, TInlineAllocator<8>> Candidates;
    TArray<APlanetActor*, TInlineAllocator<8>> Bodies;

    for (int32 Index = Planets.Num() - 1; Index >= 0; --Index)
    {
        APlanetActor* Planet = Planets[Index].Get();

        if (Planet == nullptr)
        {
            Planets.RemoveAtSwap(Index);
            continue;
        }

        const FPlanetFrameCandidate Candidate = Planet->MakeFrameCandidate(Observer);

        if (Candidate.IsValid())
        {
            Candidates.Add(Candidate);
            Bodies.Add(Planet);
        }
    }

    const bool bChanged = FrameSelector.Update(Candidates.GetData(), Candidates.Num());

    // Resolve the key back to an actor every tick rather than only on a change:
    // the actor can be destroyed while the key stays selected, and a stale
    // pointer here is a null dereference in whatever asks for gravity next.
    APlanetActor* Resolved = nullptr;

    if (FrameSelector.GetState().IsPlanetary())
    {
        const uint64 Key = FrameSelector.GetPlanetKey();

        for (int32 Index = 0; Index < Candidates.Num(); ++Index)
        {
            if (Candidates[Index].PlanetKey == Key)
            {
                Resolved = Bodies[Index];
                break;
            }
        }
    }

    FramePlanet = Resolved;

    // Scaled-space bodies are hidden while attached to a planet.
    //
    // This is the render-space collision Sprint 002 left open, seen from the
    // other side. Scaled space is a 1e-7 model of the system built around the
    // render origin - and once the player is standing on a planet, the render
    // origin is where they are standing. The entire solar system is therefore
    // drawn a few metres in front of their face, as small black discs that
    // occlude the actual sky. It is not a scaling error; it is what a 1e-7
    // model *means* when the viewer is inside it.
    //
    // Hiding them is the honest reconciliation available now. The alternative
    // that would actually be right - rendering distant bodies through a
    // separate far-field pass with its own depth range, so a planet appears in
    // the sky at its true angular size - is a rendering feature rather than a
    // coordinate one, and is recorded as future work rather than approximated
    // here. The cost is stated plainly: from a planet surface, other bodies in
    // the system are not visible.
    if (bChanged)
    {
        ApplyScaledSpaceVisibility(!FrameSelector.GetState().IsPlanetary());
    }

    if (bChanged)
    {
        const FUniverseFrameState& State = FrameSelector.GetState();

        UE_LOG(LogUniverse, Log,
            TEXT("Simulation frame -> %s%s (dominance %.3f, transition %d)"),
            LexToString(State.Kind),
            (Resolved != nullptr) ? *FString::Printf(TEXT(" [%s]"), *Resolved->GetName()) : TEXT(""),
            FrameSelector.GetDominance(),
            FrameSelector.GetTransitionCount());

        OnSimulationFrameChanged.Broadcast(State);
    }
}

FVector3d UUniverseWorldSubsystem::GetGravityAccelerationMs2(const FUniversePosition& Position) const
{
    const APlanetActor* Planet = FramePlanet.Get();

    if (Planet == nullptr)
    {
        return FVector3d::ZeroVector;
    }

    return Planet->GetGravityAccelerationMs2(Position);
}

FVector3d UUniverseWorldSubsystem::GetLocalUp(const FUniversePosition& Position) const
{
    const APlanetActor* Planet = FramePlanet.Get();

    if (Planet == nullptr)
    {
        return FVector3d(0.0, 0.0, 1.0);
    }

    return Planet->GetLocalUp(Position);
}

void UUniverseWorldSubsystem::ApplyScaledSpaceVisibility(bool bVisible)
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    // Found by iterating anchors rather than by keeping a registry of body
    // actors: the rule is about a *render space*, not about a class, so it
    // should apply to anything drawn in scaled space - including whatever gets
    // drawn there in a later sprint that does not exist yet.
    for (const TWeakObjectPtr<UUniverseAnchorComponent>& Entry : Anchors)
    {
        const UUniverseAnchorComponent* Anchor = Entry.Get();

        if (Anchor == nullptr || Anchor->RenderSpace != EUniverseRenderSpace::ScaledAstronomical)
        {
            continue;
        }

        if (AActor* Owner = Anchor->GetOwner())
        {
            Owner->SetActorHiddenInGame(!bVisible);
        }
    }
}
