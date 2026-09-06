// Copyright Universe Project. All Rights Reserved.

#include "GalaxyDescriptor.h"
#include "UniverseHash.h"
#include "UniverseRandom.h"
#include "UniverseScale.h"

const TCHAR* LexToString(EGalaxyType Type)
{
    switch (Type)
    {
    case EGalaxyType::Spiral:     return TEXT("Spiral");
    case EGalaxyType::Elliptical: return TEXT("Elliptical");
    case EGalaxyType::Irregular:  return TEXT("Irregular");
    default:                      return TEXT("Unknown");
    }
}

// --- FGalaxyId ---------------------------------------------------------------

FGalaxyId::FGalaxyId(int64 InX, int64 InY, int64 InZ, int32 InIndex)
    : CellX(InX), CellY(InY), CellZ(InZ), Index(InIndex)
{
    // Hashed from the address alone, like FUniverseSystemId, so identity is a
    // pure function of *where* and never of when it was generated or in what
    // order. The low bit is forced so a valid galaxy never hashes to zero,
    // which is the sentinel for "no galaxy".
    Hash = UniverseHash::Hash(
        0x47414C4158590001ull,   // "GALAXY"
        UniverseHash::Hash(static_cast<uint64>(InX), static_cast<uint64>(InY)),
        UniverseHash::Hash(static_cast<uint64>(InZ), static_cast<uint64>(InIndex))) | 1ull;
}

FString FGalaxyId::ToDebugString() const
{
    return FString::Printf(
        TEXT("Galaxy [%lld, %lld, %lld] #%d (0x%016llX)"),
        static_cast<long long>(CellX), static_cast<long long>(CellY),
        static_cast<long long>(CellZ), Index,
        static_cast<unsigned long long>(Hash));
}

FVector3d FGalaxyDescriptor::GetDiskForward() const
{
    return FVector3d::CrossProduct(DiskNormal, DiskRight).GetSafeNormal();
}

FString FGalaxyDescriptor::ToDebugString() const
{
    return FString::Printf(
        TEXT("%s  %s  r=%.0f ly  disk +/-%.0f ly  bulge %.0f ly  %d arms  ~%.2g stars  gen v%u"),
        *Name, LexToString(Type), RadiusLightYears, DiskThicknessLightYears,
        BulgeRadiusLightYears, ArmCount, StarCountEstimate, GenerationVersion);
}

// --- Generation --------------------------------------------------------------

namespace
{
    /** Universe cells per light year. */
    double GetCellsPerLightYear()
    {
        return UniverseScale::CmPerLightYear / UniverseScale::CellSizeCmD;
    }

    /**
     * Deterministic name, for logs and navigation.
     *
     * Names never define identity - the id does - so this is free to change
     * without invalidating anything.
     */
    FString MakeGalaxyName(const FUniverseSeed& Seed)
    {
        static const TCHAR* Prefixes[] = {
            TEXT("Verinth"), TEXT("Calaxa"), TEXT("Morrigan"), TEXT("Ossuary"),
            TEXT("Pelagos"), TEXT("Thessaly"), TEXT("Kaldera"), TEXT("Ninveh"),
            TEXT("Astrapha"), TEXT("Lumeria"), TEXT("Sethari"), TEXT("Vantoros"),
        };

        FUniverseRandom Random(Seed.Stream(UniverseSeedDomain::StreamName).Value);

        const int32 Prefix = Random.NextIntInclusive(0, UE_ARRAY_COUNT(Prefixes) - 1);
        const int32 Number = Random.NextIntInclusive(100, 9999);

        return FString::Printf(TEXT("%s-%d"), Prefixes[Prefix], Number);
    }
}

double FGalaxyGenerator::GetCellSizeLightYears()
{
    const double CellsPerLightYear = GetCellsPerLightYear();

    return static_cast<double>(static_cast<int64>(1) << CellShiftInCells) / CellsPerLightYear;
}

void FGalaxyGenerator::GetCellCoordinates(
    const FUniversePosition& Position,
    int64& OutCellX, int64& OutCellY, int64& OutCellZ)
{
    // Arithmetic shift on the integer cell index, so a position never lands in
    // the wrong intergalactic cell because of a rounding error at a boundary -
    // the same reason the sector shift is a power of two.
    OutCellX = UniverseScale::FloorDivPow2(Position.CellX, CellShiftInCells);
    OutCellY = UniverseScale::FloorDivPow2(Position.CellY, CellShiftInCells);
    OutCellZ = UniverseScale::FloorDivPow2(Position.CellZ, CellShiftInCells);
}

int32 FGalaxyGenerator::GetGalaxyCountInCell(
    const FUniverseSeedHierarchy& Hierarchy,
    int64 CellX, int64 CellY, int64 CellZ)
{
    // The intergalactic cell reuses the Sector domain tag at a different scale.
    //
    // A separate tag would be cleaner in principle; reusing it is safe because
    // the *coordinates* differ - an intergalactic cell address is a shifted
    // universe cell, not a sector address - and adding a tag now would change
    // every existing sector seed and regenerate the entire universe.
    const FUniverseSeed CellSeed = FUniverseSeed(UniverseHash::Hash(
        Hierarchy.GetUniverseSeed().Value,
        0x494E544552474C41ull,   // "INTERGLA"
        UniverseHash::Hash(
            UniverseHash::Hash(static_cast<uint64>(CellX), static_cast<uint64>(CellY)),
            static_cast<uint64>(CellZ))));

    FUniverseRandom Random(CellSeed.Stream(UniverseSeedDomain::StreamPrimary).Value);

    // Mostly empty. See the header for why this is denser than the real
    // universe and why that is a stated gameplay decision rather than a bug.
    const double Weights[3] = { 0.86, 0.13, 0.01 };

    return Random.PickWeighted(Weights, 3);
}

FGalaxyId FGalaxyGenerator::MakeGalaxyId(int64 CellX, int64 CellY, int64 CellZ, int32 Index)
{
    return FGalaxyId(CellX, CellY, CellZ, Index);
}

FUniversePosition FGalaxyGenerator::GetGalaxyPosition(
    const FUniverseSeedHierarchy& Hierarchy,
    int64 CellX, int64 CellY, int64 CellZ, int32 Index)
{
    const FGalaxyId Id = MakeGalaxyId(CellX, CellY, CellZ, Index);

    FUniverseRandom Random(UniverseHash::Hash(
        Hierarchy.GetUniverseSeed().Value, Id.Hash, 0x504F53u));

    // Somewhere inside the cell, in whole universe cells plus an offset. Built
    // from the cell's own base rather than from a float multiply, so a galaxy
    // near a cell boundary is placed exactly rather than approximately.
    const int64 CellSpan = static_cast<int64>(1) << CellShiftInCells;

    FUniversePosition Position;
    Position.CellX = (CellX << CellShiftInCells) + Random.NextIntInclusive(0, 1000000) * (CellSpan / 1000001);
    Position.CellY = (CellY << CellShiftInCells) + Random.NextIntInclusive(0, 1000000) * (CellSpan / 1000001);
    Position.CellZ = (CellZ << CellShiftInCells) + Random.NextIntInclusive(0, 1000000) * (CellSpan / 1000001);
    Position.Local = FVector3d::ZeroVector;

    Position.Normalize();

    return Position;
}

bool FGalaxyGenerator::GenerateGalaxy(
    const FUniverseSeedHierarchy& Hierarchy,
    int64 CellX, int64 CellY, int64 CellZ, int32 Index,
    FGalaxyDescriptor& OutGalaxy)
{
    if (Index < 0 || Index >= GetGalaxyCountInCell(Hierarchy, CellX, CellY, CellZ))
    {
        return false;
    }

    OutGalaxy = FGalaxyDescriptor();
    OutGalaxy.Id = MakeGalaxyId(CellX, CellY, CellZ, Index);
    OutGalaxy.Seed = FUniverseSeed(UniverseHash::Hash(
        Hierarchy.GetUniverseSeed().Value, OutGalaxy.Id.Hash, 0x47454Eu));

    OutGalaxy.Position = GetGalaxyPosition(Hierarchy, CellX, CellY, CellZ, Index);

    FUniverseRandom Random(OutGalaxy.Seed.Stream(UniverseSeedDomain::StreamPhysical).Value);

    // Type. Real galaxy populations are roughly 60% spiral, 20% elliptical and
    // 20% irregular by number in the local volume; the exact split matters far
    // less than that all three occur.
    const double TypeWeights[3] = { 0.60, 0.20, 0.20 };
    OutGalaxy.Type = static_cast<EGalaxyType>(Random.PickWeighted(TypeWeights, 3));

    OutGalaxy.RadiusLightYears = Random.NextRange(15000.0, 90000.0);

    switch (OutGalaxy.Type)
    {
    case EGalaxyType::Spiral:
        // A disk is thin: the Milky Way is 100,000 ly across and about 2,000
        // thick, a ratio of fifty to one. Getting that ratio wrong is the
        // difference between a galaxy and a ball of stars.
        OutGalaxy.DiskThicknessLightYears = OutGalaxy.RadiusLightYears * Random.NextRange(0.015, 0.04);
        OutGalaxy.BulgeRadiusLightYears = OutGalaxy.RadiusLightYears * Random.NextRange(0.06, 0.16);
        OutGalaxy.ArmCount = Random.NextIntInclusive(2, 6);
        OutGalaxy.ArmWindingTightness = Random.NextRange(0.15, 0.45);
        OutGalaxy.ArmContrast = Random.NextRange(0.35, 0.75);
        break;

    case EGalaxyType::Elliptical:
        // Nearly spherical, no arms, smooth falloff.
        OutGalaxy.DiskThicknessLightYears = OutGalaxy.RadiusLightYears * Random.NextRange(0.5, 0.9);
        OutGalaxy.BulgeRadiusLightYears = OutGalaxy.RadiusLightYears * Random.NextRange(0.3, 0.6);
        OutGalaxy.ArmCount = 0;
        OutGalaxy.ArmContrast = 0.0;
        break;

    case EGalaxyType::Irregular:
    default:
        OutGalaxy.DiskThicknessLightYears = OutGalaxy.RadiusLightYears * Random.NextRange(0.15, 0.4);
        OutGalaxy.BulgeRadiusLightYears = OutGalaxy.RadiusLightYears * Random.NextRange(0.1, 0.3);
        OutGalaxy.ArmCount = 0;
        OutGalaxy.ArmContrast = 0.0;
        break;
    }

    // Orientation: a random unit normal, and a right vector made perpendicular
    // to it by Gram-Schmidt. Built rather than drawn independently, because two
    // independently drawn vectors are almost never orthogonal and a basis that
    // is not orthonormal skews every density sample subtly.
    {
        FVector3d Normal;

        for (int32 Attempt = 0; Attempt < 32; ++Attempt)
        {
            const FVector3d Candidate(
                Random.NextRange(-1.0, 1.0),
                Random.NextRange(-1.0, 1.0),
                Random.NextRange(-1.0, 1.0));

            const double LengthSquared = Candidate.SizeSquared();

            if (LengthSquared > 0.05 && LengthSquared <= 1.0)
            {
                Normal = Candidate / FMath::Sqrt(LengthSquared);
                break;
            }
        }

        if (Normal.IsZero())
        {
            Normal = FVector3d(0.0, 0.0, 1.0);
        }

        OutGalaxy.DiskNormal = Normal;

        // A reference axis that is definitely not parallel to the normal.
        const FVector3d Reference = (FMath::Abs(Normal.Z) < 0.9)
            ? FVector3d(0.0, 0.0, 1.0)
            : FVector3d(1.0, 0.0, 0.0);

        OutGalaxy.DiskRight = FVector3d::CrossProduct(Reference, Normal).GetSafeNormal();

        if (OutGalaxy.DiskRight.IsZero())
        {
            OutGalaxy.DiskRight = FVector3d(1.0, 0.0, 0.0);
        }
    }

    OutGalaxy.CoreDensity = Random.NextRange(0.7, 1.0);

    // Scaled from the Milky Way's hundred billion by volume, which is a rough
    // proxy at best and is used only for display.
    const double RadiusRatio = OutGalaxy.RadiusLightYears / 50000.0;
    OutGalaxy.StarCountEstimate = 1.0e11 * RadiusRatio * RadiusRatio;

    OutGalaxy.Name = MakeGalaxyName(OutGalaxy.Seed);
    OutGalaxy.GenerationVersion = GalaxyGeneratorVersion::Current;

    return true;
}

void FGalaxyGenerator::FindGalaxiesWithin(
    const FUniverseSeedHierarchy& Hierarchy,
    const FUniversePosition& Centre,
    double RadiusLightYears,
    TArray<FGalaxyDescriptor>& OutGalaxies,
    int32 MaxResults)
{
    OutGalaxies.Reset();

    if (RadiusLightYears <= 0.0 || MaxResults <= 0)
    {
        return;
    }

    int64 CentreX = 0;
    int64 CentreY = 0;
    int64 CentreZ = 0;
    GetCellCoordinates(Centre, CentreX, CentreY, CentreZ);

    const double CellSize = GetCellSizeLightYears();

    // Cells to search in each direction. Clamped so a wildly large radius does
    // not turn into a loop over billions of cells - a bounded scan that misses
    // distant galaxies is far better than one that never returns.
    const int32 Reach = FMath::Clamp(
        FMath::CeilToInt32(RadiusLightYears / CellSize), 0, 6);

    for (int64 Z = CentreZ - Reach; Z <= CentreZ + Reach; ++Z)
    {
        for (int64 Y = CentreY - Reach; Y <= CentreY + Reach; ++Y)
        {
            for (int64 X = CentreX - Reach; X <= CentreX + Reach; ++X)
            {
                const int32 Count = GetGalaxyCountInCell(Hierarchy, X, Y, Z);

                for (int32 Index = 0; Index < Count; ++Index)
                {
                    const FUniversePosition GalaxyPosition =
                        GetGalaxyPosition(Hierarchy, X, Y, Z, Index);

                    const double Distance =
                        FUniversePosition::DistanceLightYears(Centre, GalaxyPosition);

                    if (Distance > RadiusLightYears)
                    {
                        continue;
                    }

                    FGalaxyDescriptor Galaxy;

                    if (GenerateGalaxy(Hierarchy, X, Y, Z, Index, Galaxy))
                    {
                        OutGalaxies.Add(MoveTemp(Galaxy));

                        if (OutGalaxies.Num() >= MaxResults)
                        {
                            return;
                        }
                    }
                }
            }
        }
    }
}

bool FGalaxyGenerator::FindNearestGalaxy(
    const FUniverseSeedHierarchy& Hierarchy,
    const FUniversePosition& Centre,
    FGalaxyDescriptor& OutGalaxy,
    int32 MaxCellShells)
{
    int64 CentreX = 0;
    int64 CentreY = 0;
    int64 CentreZ = 0;
    GetCellCoordinates(Centre, CentreX, CentreY, CentreZ);

    double BestDistance = TNumericLimits<double>::Max();
    bool bFound = false;

    // Outward in shells, so the first shell containing anything is examined
    // fully before giving up - a nearer galaxy can sit in a later cell of the
    // same shell, so stopping at the first hit would return the wrong one.
    for (int32 Shell = 0; Shell <= FMath::Max(MaxCellShells, 0); ++Shell)
    {
        for (int64 Z = CentreZ - Shell; Z <= CentreZ + Shell; ++Z)
        {
            for (int64 Y = CentreY - Shell; Y <= CentreY + Shell; ++Y)
            {
                for (int64 X = CentreX - Shell; X <= CentreX + Shell; ++X)
                {
                    // Only the shell surface; the interior was covered already.
                    const bool bOnSurface =
                        FMath::Abs(X - CentreX) == Shell
                        || FMath::Abs(Y - CentreY) == Shell
                        || FMath::Abs(Z - CentreZ) == Shell;

                    if (Shell > 0 && !bOnSurface)
                    {
                        continue;
                    }

                    const int32 Count = GetGalaxyCountInCell(Hierarchy, X, Y, Z);

                    for (int32 Index = 0; Index < Count; ++Index)
                    {
                        const FUniversePosition GalaxyPosition =
                            GetGalaxyPosition(Hierarchy, X, Y, Z, Index);

                        const double Distance =
                            FUniversePosition::DistanceLightYears(Centre, GalaxyPosition);

                        if (Distance >= BestDistance)
                        {
                            continue;
                        }

                        FGalaxyDescriptor Candidate;

                        if (GenerateGalaxy(Hierarchy, X, Y, Z, Index, Candidate))
                        {
                            BestDistance = Distance;
                            OutGalaxy = MoveTemp(Candidate);
                            bFound = true;
                        }
                    }
                }
            }
        }

        if (bFound)
        {
            return true;
        }
    }

    return bFound;
}

FUniversePosition FGalaxyGenerator::GetInhabitedPosition(const FGalaxyDescriptor& Galaxy)
{
    if (!Galaxy.IsValid())
    {
        return FUniversePosition();
    }

    // Half way out along the disk, in the plane, on the galaxy's right axis.
    // Deterministic and typical: the core is atypically dense and the rim is
    // atypically empty, and a sample of either is a poor stand-in for a galaxy.
    const double Radius = Galaxy.RadiusLightYears * 0.5;

    return FromGalaxyLocal(Galaxy, FVector3d(
        Galaxy.DiskRight.X * Radius,
        Galaxy.DiskRight.Y * Radius,
        Galaxy.DiskRight.Z * Radius));
}

bool FGalaxyGenerator::FindGalaxyAt(
    const FUniverseSeedHierarchy& Hierarchy,
    const FUniversePosition& Position,
    FGalaxyDescriptor& OutGalaxy)
{
    int64 CentreX = 0;
    int64 CentreY = 0;
    int64 CentreZ = 0;
    GetCellCoordinates(Position, CentreX, CentreY, CentreZ);

    // One cell either way. A galaxy is at most 90,000 ly across and a cell is
    // 1.28 million, so a galaxy whose centre is two cells away cannot possibly
    // contain this point.
    double BestDensity = 0.0;
    bool bFound = false;

    for (int64 Z = CentreZ - 1; Z <= CentreZ + 1; ++Z)
    {
        for (int64 Y = CentreY - 1; Y <= CentreY + 1; ++Y)
        {
            for (int64 X = CentreX - 1; X <= CentreX + 1; ++X)
            {
                const int32 Count = GetGalaxyCountInCell(Hierarchy, X, Y, Z);

                for (int32 Index = 0; Index < Count; ++Index)
                {
                    FGalaxyDescriptor Candidate;

                    if (!GenerateGalaxy(Hierarchy, X, Y, Z, Index, Candidate))
                    {
                        continue;
                    }

                    const double Density = GetStellarDensity(Candidate, Position);

                    // The densest galaxy wins where two overlap, which is both
                    // the physically sensible answer and the one that makes the
                    // choice continuous as the player moves.
                    if (Density > BestDensity)
                    {
                        BestDensity = Density;
                        OutGalaxy = Candidate;
                        bFound = true;
                    }
                }
            }
        }
    }

    return bFound;
}

FGalaxyLocalPosition FGalaxyGenerator::ToGalaxyLocal(
    const FGalaxyDescriptor& Galaxy,
    const FUniversePosition& Position)
{
    FGalaxyLocalPosition Local;

    // Cell space, not centimetres.
    //
    // TryGetRelativeCm is exact only out to about 0.01 light years - the whole
    // reason FUniversePosition splits into an integer cell and a local offset -
    // so asking it for a vector across a galaxy always fails. Cell differences
    // are taken in double and keep full relative precision at any separation,
    // which is what a galaxy-scale offset actually needs.
    const FVector3d RelativeCells =
        FUniversePosition::GetRelativeCells(Galaxy.Position, Position);

    const double LightYearsPerCell = 1.0 / GetCellsPerLightYear();

    Local.OffsetLightYears = FVector3d(
        RelativeCells.X * LightYearsPerCell,
        RelativeCells.Y * LightYearsPerCell,
        RelativeCells.Z * LightYearsPerCell);

    Local.HeightLightYears = FVector3d::DotProduct(Local.OffsetLightYears, Galaxy.DiskNormal);

    const FVector3d InPlane(
        Local.OffsetLightYears.X - Galaxy.DiskNormal.X * Local.HeightLightYears,
        Local.OffsetLightYears.Y - Galaxy.DiskNormal.Y * Local.HeightLightYears,
        Local.OffsetLightYears.Z - Galaxy.DiskNormal.Z * Local.HeightLightYears);

    Local.RadiusLightYears = InPlane.Size();

    const FVector3d Forward = Galaxy.GetDiskForward();

    Local.AngleRadians = FMath::Atan2(
        FVector3d::DotProduct(InPlane, Forward),
        FVector3d::DotProduct(InPlane, Galaxy.DiskRight));

    return Local;
}

FUniversePosition FGalaxyGenerator::FromGalaxyLocal(
    const FGalaxyDescriptor& Galaxy,
    const FVector3d& OffsetLightYears)
{
    // Split into whole cells plus a remainder, rather than one enormous
    // OffsetByCm. A galactic offset is around 10^22 cm, where a double's ulp is
    // tens of kilometres; carrying the bulk of it as an integer cell count
    // keeps the remainder small enough to stay exact.
    const double CellsPerLightYear = GetCellsPerLightYear();

    const double CellsX = OffsetLightYears.X * CellsPerLightYear;
    const double CellsY = OffsetLightYears.Y * CellsPerLightYear;
    const double CellsZ = OffsetLightYears.Z * CellsPerLightYear;

    const int64 WholeX = static_cast<int64>(FMath::FloorToDouble(CellsX));
    const int64 WholeY = static_cast<int64>(FMath::FloorToDouble(CellsY));
    const int64 WholeZ = static_cast<int64>(FMath::FloorToDouble(CellsZ));

    FUniversePosition Result = Galaxy.Position;
    Result.CellX += WholeX;
    Result.CellY += WholeY;
    Result.CellZ += WholeZ;
    Result.Local += FVector3d(
        (CellsX - static_cast<double>(WholeX)) * UniverseScale::CellSizeCmD,
        (CellsY - static_cast<double>(WholeY)) * UniverseScale::CellSizeCmD,
        (CellsZ - static_cast<double>(WholeZ)) * UniverseScale::CellSizeCmD);

    Result.Normalize();

    return Result;
}

double FGalaxyGenerator::GetStellarDensityAt(
    const FGalaxyDescriptor& Galaxy,
    const FGalaxyLocalPosition& Local)
{
    if (!Galaxy.IsValid())
    {
        return 0.0;
    }

    const double Radius = Local.RadiusLightYears;
    const double Height = FMath::Abs(Local.HeightLightYears);

    // --- Hard edges ---------------------------------------------------------
    //
    // Zero outside the disk radius and outside its thickness, exactly. A galaxy
    // that faded asymptotically would put a thin scattering of stars across all
    // of intergalactic space, and "empty" would never actually be empty.
    if (Radius > Galaxy.RadiusLightYears)
    {
        return 0.0;
    }

    // The disk thins toward its edge, as real disks do - it is a flared
    // exponential, approximated here by a linear taper to a quarter thickness.
    const double RadiusFraction = Radius / Galaxy.RadiusLightYears;
    const double LocalThickness = Galaxy.DiskThicknessLightYears * (1.0 - 0.75 * RadiusFraction);

    if (Height > LocalThickness)
    {
        return 0.0;
    }

    // --- Radial profile -----------------------------------------------------
    //
    // A smooth falloff reaching zero at the rim, written as a polynomial rather
    // than an exponential. Real disks are exponential; a polynomial with the
    // same shape avoids a transcendental on a path evaluated once per sector,
    // and the difference is invisible against the noise the arms add.
    const double RadialTaper = (1.0 - RadiusFraction) * (1.0 - RadiusFraction);

    // --- Vertical profile ---------------------------------------------------
    const double HeightFraction = (LocalThickness > 0.0) ? (Height / LocalThickness) : 1.0;
    const double VerticalTaper = (1.0 - HeightFraction) * (1.0 - HeightFraction);

    // --- Bulge --------------------------------------------------------------
    //
    // A rational falloff that is 1 at the centre and drops smoothly. Rational
    // rather than exponential, again to stay off the transcendental path where
    // it costs nothing to do so.
    const double BulgeRatio = (Galaxy.BulgeRadiusLightYears > 0.0)
        ? (Local.GetDistanceLightYears() / Galaxy.BulgeRadiusLightYears)
        : 1000.0;

    const double Bulge = 1.0 / (1.0 + BulgeRatio * BulgeRatio);

    double Density = FMath::Max(RadialTaper * VerticalTaper, Bulge);

    // --- Spiral arms --------------------------------------------------------
    //
    // A logarithmic spiral: the arm's angle advances with the logarithm of the
    // radius, so arms wind more tightly toward the centre exactly as real ones
    // do. This is the one term that needs trigonometry - see the header note.
    if (Galaxy.ArmCount > 0 && Galaxy.ArmContrast > 0.0 && Radius > 1.0)
    {
        const double ArmPhase =
            Local.AngleRadians * Galaxy.ArmCount
            - FMath::Loge(Radius) / FMath::Max(Galaxy.ArmWindingTightness, 0.01);

        // Sin gives a smooth ridge; squaring makes the arms narrower than the
        // gaps, which is what a spiral galaxy actually looks like.
        const double Ridge = 0.5 + 0.5 * FMath::Sin(ArmPhase);
        const double Arms = Ridge * Ridge;

        const double Modulation = 1.0 - Galaxy.ArmContrast + Galaxy.ArmContrast * Arms;

        // The bulge is not modulated by arms - arms are a disk feature, and
        // applying them to the core would carve holes through the middle of
        // every galaxy.
        const double CoreWeight = Bulge;

        Density = Density * (Modulation * (1.0 - CoreWeight) + CoreWeight);
    }

    return FMath::Clamp(Density * Galaxy.CoreDensity, 0.0, 1.0);
}

double FGalaxyGenerator::GetStellarDensity(
    const FGalaxyDescriptor& Galaxy,
    const FUniversePosition& Position)
{
    return GetStellarDensityAt(Galaxy, ToGalaxyLocal(Galaxy, Position));
}
