// Copyright Universe Project. All Rights Reserved.

#include "PlanetQuadtree.h"
#include "PlanetTerrain.h"

namespace
{
    /** Sort key giving a stable, address-derived ordering. */
    uint64 PatchSortKey(const FPlanetPatchId& Id)
    {
        return MakePlanetPatchKey(Id);
    }

    /** Binary search in a sorted key array. */
    bool ContainsKey(const TArray<uint64>& SortedKeys, uint64 Key)
    {
        int32 Low = 0;
        int32 High = SortedKeys.Num() - 1;
        while (Low <= High)
        {
            const int32 Mid = Low + (High - Low) / 2;
            const uint64 Value = SortedKeys[Mid];
            if (Value == Key)
            {
                return true;
            }
            if (Value < Key)
            {
                Low = Mid + 1;
            }
            else
            {
                High = Mid - 1;
            }
        }
        return false;
    }

    void SortKeys(TArray<uint64>& Keys)
    {
        for (int32 Index = 1; Index < Keys.Num(); ++Index)
        {
            const uint64 Current = Keys[Index];
            int32 Position = Index - 1;
            while (Position >= 0 && Keys[Position] > Current)
            {
                Keys[Position + 1] = Keys[Position];
                --Position;
            }
            Keys[Position + 1] = Current;
        }
    }

    /** Insertion sort by patch address. Selections are small and nearly sorted
     *  already, so this beats a general sort and keeps the shim dependency-free. */
    void SortByAddress(TArray<FPlanetSelectedPatch>& Patches)
    {
        for (int32 Index = 1; Index < Patches.Num(); ++Index)
        {
            const FPlanetSelectedPatch Current = Patches[Index];
            const uint64 CurrentKey = PatchSortKey(Current.PatchId);

            int32 Position = Index - 1;
            while (Position >= 0 && PatchSortKey(Patches[Position].PatchId) > CurrentKey)
            {
                Patches[Position + 1] = Patches[Position];
                --Position;
            }
            Patches[Position + 1] = Current;
        }
    }
}

double FPlanetQuadtree::EstimateGeometricErrorMeters(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FPlanetPatchId& PatchId)
{
    const double PatchSizeMeters = PatchId.GetApproximateSizeMeters(Planet.RadiusMeters);
    const double VertexSpacing = PatchSizeMeters / static_cast<double>(Settings.GetQuadsPerEdge());

    // Two sources of error.
    //
    // Curvature: even with perfectly flat terrain, a chord across one quad
    // departs from the sphere by roughly s^2 / (8R) - the sagitta. This is what
    // makes a planet look faceted from orbit and is the term that dominates at
    // low LOD.
    const double CurvatureError = (VertexSpacing * VertexSpacing) / (8.0 * Planet.RadiusMeters);

    // Relief: terrain can vary within a quad by up to roughly the relief slope
    // over that distance. Scaled by patch level so the estimate tightens as
    // patches shrink - relief is fractal, so the variation within a small patch
    // is much less than the planet's total range.
    const double TotalRelief = Planet.GetElevationRangeMeters();
    const double LevelScale = 1.0 / static_cast<double>(1u << FMath::Min<uint32>(PatchId.Level, 20u));
    const double ReliefError = TotalRelief * LevelScale * 0.5;

    // Deliberately additive rather than a maximum: over-estimating costs frame
    // time, under-estimating leaves faceting that no later pass can repair.
    return CurvatureError + ReliefError;
}

double FPlanetQuadtree::GetDistanceToPatchMeters(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetPatchId& PatchId,
    const FVector3d& ObserverPositionMeters)
{
    // Distance to the patch's surface centre, reduced by the patch's own
    // extent so that a large patch the observer is sitting inside reports a
    // small distance rather than the distance to its middle. Without that, a
    // low-level patch directly underfoot looks far away and never subdivides.
    const FVector3d Centre = PatchId.GetCentreDirection();
    const double SurfaceRadius = Planet.RadiusMeters;

    const FVector3d PatchCentre(
        Centre.X * SurfaceRadius,
        Centre.Y * SurfaceRadius,
        Centre.Z * SurfaceRadius);

    const FVector3d Delta(
        ObserverPositionMeters.X - PatchCentre.X,
        ObserverPositionMeters.Y - PatchCentre.Y,
        ObserverPositionMeters.Z - PatchCentre.Z);

    const double CentreDistance = Delta.Size();
    const double PatchRadius = 0.5 * PatchId.GetApproximateSizeMeters(Planet.RadiusMeters);

    // Never zero: a zero distance would give infinite screen error and split
    // forever.
    return FMath::Max(CentreDistance - PatchRadius, 1.0);
}

bool FPlanetQuadtree::IsBeyondHorizon(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetPatchId& PatchId,
    const FVector3d& ObserverPositionMeters)
{
    const double ObserverRadius = ObserverPositionMeters.Size();

    // Inside the planet, or on the surface: nothing is over the horizon in any
    // useful sense, so cull nothing.
    const double SafeRadius = Planet.GetMinRadiusMeters();
    if (ObserverRadius <= SafeRadius)
    {
        return false;
    }

    // The horizon test, done conservatively.
    //
    // For an observer at radius r looking at a sphere of radius R, points are
    // visible when the angle between the observer direction and the point
    // direction is less than acos(R / r) - the half-angle of the visible cap -
    // plus the angle the terrain height adds.
    //
    // Terrain is accounted for by using the SMALLEST radius for the occluding
    // sphere and the LARGEST for the point being tested, which is the
    // pessimistic pairing: a mountain just over the geometric horizon stays
    // visible. Culling terrain that should be visible would be a hole in the
    // planet; keeping a few patches too many merely costs triangles.
    const double OccluderRadius = Planet.GetMinRadiusMeters();
    const double CosVisibleCap = FMath::Clamp(OccluderRadius / ObserverRadius, -1.0, 1.0);
    const double VisibleCapAngle = FMath::Acos(CosVisibleCap);

    // How much extra angle the tallest terrain buys beyond the geometric
    // horizon, seen from the surface.
    const double MaxRadius = Planet.GetMaxRadiusMeters();
    const double CosTerrainBonus = FMath::Clamp(OccluderRadius / MaxRadius, -1.0, 1.0);
    const double TerrainBonusAngle = FMath::Acos(CosTerrainBonus);

    const FVector3d ObserverDirection(
        ObserverPositionMeters.X / ObserverRadius,
        ObserverPositionMeters.Y / ObserverRadius,
        ObserverPositionMeters.Z / ObserverRadius);

    const FVector3d PatchDirection = PatchId.GetCentreDirection();

    const double CosAngle = FMath::Clamp(
        ObserverDirection.X * PatchDirection.X
        + ObserverDirection.Y * PatchDirection.Y
        + ObserverDirection.Z * PatchDirection.Z,
        -1.0, 1.0);
    const double Angle = FMath::Acos(CosAngle);

    // The patch's own angular radius, so a patch only partly over the horizon
    // is kept.
    const double PatchAngle = PatchId.GetAngularRadius();

    return Angle > (VisibleCapAngle + TerrainBonusAngle + PatchAngle);
}

void FPlanetQuadtree::SelectRecursive(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FPlanetLodContext& Context,
    const FPlanetPatchId& PatchId,
    double ProjectionFactor,
    const TArray<uint64>& PreviouslySplitSorted,
    TArray<FPlanetSelectedPatch>& OutPatches,
    TArray<uint64>& OutSplitSorted,
    FPlanetSelectionStats& OutStats)
{
    ++OutStats.NodesVisited;

    if (OutPatches.Num() >= Context.MaxSelectedPatches)
    {
        OutStats.bHitPatchLimit = true;
        return;
    }

    // Horizon culling, but never above MinLevel: culling a root patch would
    // remove a sixth of the planet, and the coarse levels are cheap enough that
    // keeping them is the safer trade.
    if (PatchId.Level > Context.MinLevel
        && IsBeyondHorizon(Planet, PatchId, Context.ObserverPositionMeters))
    {
        ++OutStats.HorizonCulled;
        return;
    }

    const double Distance = GetDistanceToPatchMeters(Planet, PatchId, Context.ObserverPositionMeters);
    const double GeometricError = EstimateGeometricErrorMeters(Planet, Settings, PatchId);
    const double ScreenError = GeometricError * ProjectionFactor / Distance;

    const bool bBelowMinLevel = PatchId.Level < Context.MinLevel;
    const bool bCanSplit =
        PatchId.CanSplit()
        && PatchId.Level < Context.MaxLevel
        && PatchId.Level < FPlanetPatchId::MaxLevel;

    // Hysteresis. A node that was split last time is held split until its error
    // falls to the lower merge threshold, so an observer hovering at the
    // boundary does not make it split and merge every frame - which would
    // rebuild meshes continuously and is the single most visible way a terrain
    // LOD system misbehaves.
    const bool bWasSplit = ContainsKey(PreviouslySplitSorted, MakePlanetPatchKey(PatchId));
    const double Threshold = bWasSplit
        ? Context.SplitPixelError * Context.MergeHysteresis
        : Context.SplitPixelError;

    const bool bWantsSplit = bBelowMinLevel || (ScreenError > Threshold);

    if (bCanSplit && bWantsSplit)
    {
        OutSplitSorted.Add(MakePlanetPatchKey(PatchId));

        for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
        {
            SelectRecursive(
                Planet, Settings, Context,
                PatchId.GetChild(static_cast<EPlanetPatchQuadrant>(Quadrant)),
                ProjectionFactor, PreviouslySplitSorted, OutPatches, OutSplitSorted, OutStats);
        }
        return;
    }

    FPlanetSelectedPatch Selected;
    Selected.PatchId = PatchId;
    Selected.DistanceMeters = Distance;
    Selected.ScreenErrorPixels = ScreenError;
    OutPatches.Add(Selected);

    OutStats.DeepestLevel = FMath::Max(OutStats.DeepestLevel, PatchId.Level);
}

void FPlanetQuadtree::EnforceNeighbourBalance(
    TArray<FPlanetSelectedPatch>& Patches,
    const FPlanetLodContext& Context,
    FPlanetSelectionStats& OutStats)
{
    // Adjacent patches must differ by at most one level, so the skirt depth
    // needed to hide a T-junction stays proportional to patch size. A five-level
    // difference would need a skirt deeper than the patch is wide.
    //
    // Two things make this loop behave:
    //
    // 1. Each pass collects every violation and applies them together, rather
    //    than splitting one patch and starting over. Splitting one per pass
    //    bounds the total work at the pass count, which silently leaves most
    //    violations in place - the selection then looks balanced only where the
    //    algorithm happened to reach.
    //
    // 2. Lookup is indexed, not linear. A neighbour address may be covered by
    //    that patch or any ancestor, so the query walks up at most MaxLevel
    //    ancestors and binary-searches a sorted key array. Scanning the whole
    //    selection for every neighbour would be quadratic - roughly 16 million
    //    comparisons per pass at the patch cap, every frame.
    //
    // Passes are bounded because a hang is worse than a missed split; splitting
    // a patch raises it one level, so the worst case is the initial level
    // spread, and MaxLevel passes is comfortably sufficient.
    const int32 MaxPasses = static_cast<int32>(FPlanetPatchId::MaxLevel) + 2;

    struct FKeyEntry
    {
        uint64 Key;
        int32 Index;
    };

    TArray<FKeyEntry> Entries;
    TArray<int32> ToSplit;

    for (int32 Pass = 0; Pass < MaxPasses; ++Pass)
    {
        // --- Index the current selection ---------------------------------
        Entries.Reset();
        Entries.Reserve(Patches.Num());
        for (int32 Index = 0; Index < Patches.Num(); ++Index)
        {
            Entries.Add({ MakePlanetPatchKey(Patches[Index].PatchId), Index });
        }

        for (int32 Index = 1; Index < Entries.Num(); ++Index)
        {
            const FKeyEntry Current = Entries[Index];
            int32 Position = Index - 1;
            while (Position >= 0 && Entries[Position].Key > Current.Key)
            {
                Entries[Position + 1] = Entries[Position];
                --Position;
            }
            Entries[Position + 1] = Current;
        }

        auto FindCoveringIndex = [&Entries](const FPlanetPatchId& Address) -> int32
        {
            FPlanetPatchId Walk = Address;
            for (int32 Step = 0; Step <= FPlanetPatchId::MaxLevel; ++Step)
            {
                const uint64 Key = MakePlanetPatchKey(Walk);

                int32 Low = 0;
                int32 High = Entries.Num() - 1;
                while (Low <= High)
                {
                    const int32 Mid = Low + (High - Low) / 2;
                    if (Entries[Mid].Key == Key)
                    {
                        return Entries[Mid].Index;
                    }
                    if (Entries[Mid].Key < Key)
                    {
                        Low = Mid + 1;
                    }
                    else
                    {
                        High = Mid - 1;
                    }
                }

                if (Walk.IsRoot())
                {
                    break;
                }
                Walk = Walk.GetParent();
            }
            return -1;
        };

        // --- Collect every violation in this pass -------------------------
        ToSplit.Reset();

        const int32 CountThisPass = Patches.Num();
        for (int32 Index = 0; Index < CountThisPass; ++Index)
        {
            const FPlanetPatchId& Current = Patches[Index].PatchId;

            for (int32 Direction = 0; Direction < 4; ++Direction)
            {
                FPlanetPatchId NeighbourAddress;
                if (!Current.TryGetNeighbour(static_cast<EPlanetPatchNeighbour>(Direction), NeighbourAddress))
                {
                    continue;
                }

                const int32 NeighbourIndex = FindCoveringIndex(NeighbourAddress);
                if (NeighbourIndex < 0)
                {
                    // No covering patch means that side is finer than this one.
                    // Those finer patches will find this patch as their own
                    // coarse neighbour, so there is nothing to do from here.
                    continue;
                }

                const FPlanetPatchId& Neighbour = Patches[NeighbourIndex].PatchId;

                // Split the coarser of the pair.
                if (static_cast<int32>(Neighbour.Level) + 1 >= static_cast<int32>(Current.Level))
                {
                    continue;
                }

                if (!Neighbour.CanSplit() || Neighbour.Level >= Context.MaxLevel)
                {
                    continue;
                }

                // Deduplicate: one patch can violate against several neighbours
                // in the same pass, and splitting it twice would corrupt the
                // selection.
                bool bAlreadyQueued = false;
                for (int32 Queued : ToSplit)
                {
                    if (Queued == NeighbourIndex)
                    {
                        bAlreadyQueued = true;
                        break;
                    }
                }
                if (!bAlreadyQueued)
                {
                    ToSplit.Add(NeighbourIndex);
                }
            }
        }

        if (ToSplit.Num() == 0)
        {
            return;
        }

        // --- Apply ---------------------------------------------------------
        for (int32 SplitIndex : ToSplit)
        {
            if (Patches.Num() + 3 > Context.MaxSelectedPatches)
            {
                OutStats.bHitPatchLimit = true;
                return;
            }

            const FPlanetSelectedPatch Parent = Patches[SplitIndex];

            for (int32 Quadrant = 0; Quadrant < 4; ++Quadrant)
            {
                FPlanetSelectedPatch Child;
                Child.PatchId = Parent.PatchId.GetChild(static_cast<EPlanetPatchQuadrant>(Quadrant));
                Child.DistanceMeters = Parent.DistanceMeters;
                Child.ScreenErrorPixels = Parent.ScreenErrorPixels;
                Child.bForcedByBalancing = true;

                if (Quadrant == 0)
                {
                    Patches[SplitIndex] = Child;
                }
                else
                {
                    Patches.Add(Child);
                }
            }

            ++OutStats.BalancingSplits;
        }
    }
}

void FPlanetQuadtree::SelectPatchesWithHistory(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FPlanetLodContext& Context,
    const TArray<uint64>& PreviouslySplitSorted,
    TArray<FPlanetSelectedPatch>& OutPatches,
    TArray<uint64>& OutSplitSorted,
    FPlanetSelectionStats& OutStats)
{
    OutPatches.Reset();
    OutSplitSorted.Reset();
    OutStats = FPlanetSelectionStats();

    if (!Planet.IsValid() || !Settings.IsValid())
    {
        return;
    }

    const double ProjectionFactor = Context.GetProjectionFactor();

    for (int32 FaceIndex = 0; FaceIndex < CubeSphere::FaceCount; ++FaceIndex)
    {
        SelectRecursive(
            Planet, Settings, Context,
            FPlanetPatchId::Root(static_cast<CubeSphere::EFace>(FaceIndex)),
            ProjectionFactor, PreviouslySplitSorted, OutPatches, OutSplitSorted, OutStats);
    }

    EnforceNeighbourBalance(OutPatches, Context, OutStats);

    // Address order, never distance order, so a caller diffing successive
    // frames sees a stable sequence rather than a set that reshuffles whenever
    // the observer moves.
    SortByAddress(OutPatches);
    SortKeys(OutSplitSorted);

    OutStats.SelectedPatches = OutPatches.Num();
    for (const FPlanetSelectedPatch& Patch : OutPatches)
    {
        OutStats.DeepestLevel = FMath::Max(OutStats.DeepestLevel, Patch.PatchId.Level);
    }
}

void FPlanetQuadtree::SelectPatches(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FPlanetLodContext& Context,
    TArray<FPlanetSelectedPatch>& OutPatches,
    FPlanetSelectionStats& OutStats)
{
    // No history: the stateless form has no hysteresis, which is the honest
    // behaviour for a function with no memory.
    const TArray<uint64> NoHistory;
    TArray<uint64> IgnoredSplits;
    SelectPatchesWithHistory(
        Planet, Settings, Context, NoHistory, OutPatches, IgnoredSplits, OutStats);
}

void FPlanetQuadtreeSelector::Select(
    const FPlanetSurfaceDescriptor& Planet,
    const FPlanetTerrainSettings& Settings,
    const FPlanetLodContext& Context,
    TArray<FPlanetSelectedPatch>& OutPatches,
    FPlanetSelectionStats& OutStats)
{
    FPlanetQuadtree::SelectPatchesWithHistory(
        Planet, Settings, Context, SplitNodes, OutPatches, NextSplitNodes, OutStats);

    // Swap rather than copy: this runs every frame and the arrays are the only
    // per-frame allocation the selector would otherwise make.
    SplitNodes = NextSplitNodes;
}
