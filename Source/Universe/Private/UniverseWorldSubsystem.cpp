// Copyright Universe Project. All Rights Reserved.

#include "UniverseWorldSubsystem.h"
#include "UniverseAnchorComponent.h"
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

    TimeSinceNearestSearch += static_cast<double>(DeltaTime);
    if (TimeSinceNearestSearch >= NearestSearchIntervalSeconds)
    {
        TimeSinceNearestSearch = 0.0;
        UpdateNearestSystem();
    }
}
