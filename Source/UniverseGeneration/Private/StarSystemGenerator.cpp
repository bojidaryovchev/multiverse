// Copyright Universe Project. All Rights Reserved.

#include "StarSystemGenerator.h"
#include "UniverseRandom.h"
#include "UniverseScale.h"

namespace
{
    // -----------------------------------------------------------------------
    // Physical constants and reference values.
    // -----------------------------------------------------------------------

    constexpr double SolarMassKg        = 1.98847e30;
    constexpr double SolarRadiusMeters  = 6.957e8;
    constexpr double SolarTemperatureK  = 5772.0;
    constexpr double EarthRadiusMeters  = 6.371e6;
    constexpr double GravitationalConst = 6.67430e-11;
    constexpr double Pi                 = 3.14159265358979323846;
    constexpr double SecondsPerYear     = 3.15576e7;

    /**
     * Relative abundance of each spectral class, roughly following the initial
     * mass function for main-sequence stars in the solar neighbourhood. M
     * dwarfs dominate overwhelmingly and O stars are vanishingly rare; using a
     * flat distribution instead would fill the galaxy with blue giants and make
     * every system look the same kind of spectacular.
     */
    constexpr double StarClassWeights[static_cast<int32>(EStarClass::Count)] = {
        0.0000003,  // O
        0.0013,     // B
        0.0060,     // A
        0.0300,     // F
        0.0760,     // G
        0.1210,     // K
        0.7650,     // M
    };

    /** Mass range per class, in solar masses. */
    constexpr double StarClassMassRange[static_cast<int32>(EStarClass::Count)][2] = {
        { 16.0, 90.0 },   // O
        {  2.1, 16.0 },   // B
        {  1.4,  2.1 },   // A
        {  1.04, 1.4 },   // F
        {  0.80, 1.04 },  // G
        {  0.45, 0.80 },  // K
        {  0.08, 0.45 },  // M
    };

    /** Effective temperature range per class, in kelvin. */
    constexpr double StarClassTempRange[static_cast<int32>(EStarClass::Count)][2] = {
        { 30000.0, 50000.0 },  // O
        { 10000.0, 30000.0 },  // B
        {  7500.0, 10000.0 },  // A
        {  6000.0,  7500.0 },  // F
        {  5200.0,  6000.0 },  // G
        {  3700.0,  5200.0 },  // K
        {  2400.0,  3700.0 },  // M
    };

    /**
     * Approximate blackbody colour in linear sRGB, normalised so the brightest
     * channel is 1. A cheap piecewise fit rather than a full Planck integral:
     * this drives a placeholder light colour, and the visual difference between
     * this and a physically exact spectrum is not worth the cost here.
     */
    void TemperatureToLinearColor(double TemperatureK, double& OutR, double& OutG, double& OutB)
    {
        const double T = FMath::Clamp(TemperatureK, 1000.0, 40000.0) / 100.0;

        double R;
        double G;
        double B;

        if (T <= 66.0)
        {
            R = 1.0;
            G = FMath::Clamp((99.4708025861 * FMath::Loge(T) - 161.1195681661) / 255.0, 0.0, 1.0);
            B = (T <= 19.0)
                ? 0.0
                : FMath::Clamp((138.5177312231 * FMath::Loge(T - 10.0) - 305.0447927307) / 255.0, 0.0, 1.0);
        }
        else
        {
            R = FMath::Clamp((329.698727446 * FMath::Pow(T - 60.0, -0.1332047592)) / 255.0, 0.0, 1.0);
            G = FMath::Clamp((288.1221695283 * FMath::Pow(T - 60.0, -0.0755148492)) / 255.0, 0.0, 1.0);
            B = 1.0;
        }

        // Normalise so placeholder star lights have comparable intensity and
        // only their hue differs.
        const double MaxChannel = FMath::Max(R, FMath::Max(G, B));
        if (MaxChannel > 0.0)
        {
            R /= MaxChannel;
            G /= MaxChannel;
            B /= MaxChannel;
        }

        OutR = R;
        OutG = G;
        OutB = B;
    }

    /**
     * Syllable tables for system and body names.
     *
     * Names are generated, not stored, for the same reason everything else is:
     * there will be far too many systems to hold a name list for, and the name
     * must be the same every time the player returns. Purely cosmetic, but it
     * makes navigation and bug reports enormously easier than raw coordinates.
     */
    const TCHAR* const NamePrefixes[] = {
        TEXT("Ael"), TEXT("Bar"), TEXT("Cor"), TEXT("Dra"), TEXT("Eri"), TEXT("Fen"),
        TEXT("Gal"), TEXT("Hyr"), TEXT("Ith"), TEXT("Jor"), TEXT("Kel"), TEXT("Lyr"),
        TEXT("Mor"), TEXT("Nyx"), TEXT("Oph"), TEXT("Pyr"), TEXT("Quo"), TEXT("Rhe"),
        TEXT("Sol"), TEXT("Tar"), TEXT("Ura"), TEXT("Vel"), TEXT("Wex"), TEXT("Xan"),
        TEXT("Yth"), TEXT("Zar"),
    };

    const TCHAR* const NameMiddles[] = {
        TEXT("a"), TEXT("e"), TEXT("i"), TEXT("o"), TEXT("u"),
        TEXT("an"), TEXT("en"), TEXT("in"), TEXT("or"), TEXT("ur"),
        TEXT("al"), TEXT("el"), TEXT("il"), TEXT("ol"), TEXT("ys"),
    };

    const TCHAR* const NameSuffixes[] = {
        TEXT("ra"), TEXT("nis"), TEXT("tor"), TEXT("vex"), TEXT("mir"), TEXT("kar"),
        TEXT("thys"), TEXT("dor"), TEXT("lux"), TEXT("pha"), TEXT("gon"), TEXT("ses"),
        TEXT("nar"), TEXT("tia"), TEXT("rix"), TEXT("dan"),
    };

    // The array bound deduces as size_t; taking the parameter as int32 would
    // make deduction fail rather than convert.
    template <typename T, size_t N>
    constexpr int32 ArrayCount(const T (&)[N]) { return static_cast<int32>(N); }

    /** Roman-numeral suffix for planets, as used for real exoplanet naming. */
    const TCHAR* const RomanNumerals[] = {
        TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IV"), TEXT("V"), TEXT("VI"),
        TEXT("VII"), TEXT("VIII"), TEXT("IX"), TEXT("X"), TEXT("XI"), TEXT("XII"),
        TEXT("XIII"), TEXT("XIV"), TEXT("XV"), TEXT("XVI"),
    };
}

int32 FStarSystemGenerator::GetSystemCountInSector(
    const FUniverseSeedHierarchy& Hierarchy,
    int64 SectorX, int64 SectorY, int64 SectorZ)
{
    const FUniverseSeed SectorSeed = Hierarchy.GetSectorSeed(SectorX, SectorY, SectorZ);

    // Drawn from a dedicated stream so that adding any other sector-level
    // property later cannot change how many stars a sector contains - which
    // would silently rearrange the entire galaxy.
    FUniverseRandom Random(SectorSeed.Stream(UniverseSeedDomain::StreamPrimary).Value);

    // Expectation 0.47 systems/sector, matching ~0.004 stars/ly^3 over a
    // 4.87 ly cube. Occasional pairs stand in for the wide binaries and
    // close neighbours that real stellar distributions contain.
    const double Weights[3] = { 0.60, 0.33, 0.07 };
    return Random.PickWeighted(Weights, 3);
}

FUniverseSystemId FStarSystemGenerator::MakeSystemId(int64 SectorX, int64 SectorY, int64 SectorZ, int32 IndexInSector)
{
    return FUniverseSystemId(SectorX, SectorY, SectorZ, IndexInSector);
}

FUniversePosition FStarSystemGenerator::GetSystemPosition(
    const FUniverseSeedHierarchy& Hierarchy,
    int64 SectorX, int64 SectorY, int64 SectorZ, int32 IndexInSector)
{
    const FUniverseSeed SectorSeed = Hierarchy.GetSectorSeed(SectorX, SectorY, SectorZ);
    const FUniverseSeed SystemSeed = FUniverseSeedHierarchy::GetSystemSeed(SectorSeed, IndexInSector);

    FUniverseRandom Random(SystemSeed.Stream(UniverseSeedDomain::StreamOrbital).Value);

    // Position is built as (integer cell offset within the sector) + (local
    // offset within that cell) rather than as a single scaled double. Picking
    // the cell as an integer keeps the placement exact at any distance from
    // the universe origin - a fractional-of-sector double would lose
    // resolution the further out the sector is, and stars would drift.
    const int32 MaxCellOffset = static_cast<int32>(UniverseScale::SectorSizeInCells - 1);
    const int64 CellOffsetX = Random.NextIntInclusive(0, MaxCellOffset);
    const int64 CellOffsetY = Random.NextIntInclusive(0, MaxCellOffset);
    const int64 CellOffsetZ = Random.NextIntInclusive(0, MaxCellOffset);

    const double LocalX = Random.NextUnit() * UniverseScale::CellSizeCmD;
    const double LocalY = Random.NextUnit() * UniverseScale::CellSizeCmD;
    const double LocalZ = Random.NextUnit() * UniverseScale::CellSizeCmD;

    return FUniversePosition(
        SectorX * UniverseScale::SectorSizeInCells + CellOffsetX,
        SectorY * UniverseScale::SectorSizeInCells + CellOffsetY,
        SectorZ * UniverseScale::SectorSizeInCells + CellOffsetZ,
        FVector3d(LocalX, LocalY, LocalZ));
}

FString FStarSystemGenerator::GenerateName(const FUniverseSeed& NameSeed)
{
    FUniverseRandom Random(NameSeed.Value);

    FString Name = FString(NamePrefixes[Random.NextIntInclusive(0, ArrayCount(NamePrefixes) - 1)]);
    Name += FString(NameMiddles[Random.NextIntInclusive(0, ArrayCount(NameMiddles) - 1)]);
    Name += FString(NameSuffixes[Random.NextIntInclusive(0, ArrayCount(NameSuffixes) - 1)]);

    // A numeric tail keeps names distinguishable across the very large number
    // of systems the syllable table alone would eventually collide on.
    const int32 Designation = Random.NextIntInclusive(100, 9999);
    Name += FString::Printf(TEXT("-%d"), Designation);

    return Name;
}

void FStarSystemGenerator::GenerateStar(const FUniverseSeed& SystemSeed, FStarDescriptor& OutStar)
{
    const FUniverseSeed StarSeed = FUniverseSeedHierarchy::GetBodySeed(SystemSeed, 0);
    OutStar.Seed = StarSeed;

    FUniverseRandom Random(StarSeed.Stream(UniverseSeedDomain::StreamPhysical).Value);

    const int32 ClassIndex = Random.PickWeighted(StarClassWeights, static_cast<int32>(EStarClass::Count));
    OutStar.Class = static_cast<EStarClass>(ClassIndex);

    OutStar.MassSolar = Random.NextRange(
        StarClassMassRange[ClassIndex][0], StarClassMassRange[ClassIndex][1]);

    OutStar.SurfaceTemperatureK = Random.NextRange(
        StarClassTempRange[ClassIndex][0], StarClassTempRange[ClassIndex][1]);

    // Main-sequence mass-luminosity and mass-radius relations. Both are
    // empirical power laws and only approximate, but they keep the derived
    // quantities (habitable zone, frost line, orbital periods) mutually
    // consistent, which matters more here than absolute accuracy.
    OutStar.LuminositySolar = FMath::Pow(OutStar.MassSolar, 3.5);
    OutStar.RadiusMeters = SolarRadiusMeters * FMath::Pow(OutStar.MassSolar, 0.8);

    TemperatureToLinearColor(OutStar.SurfaceTemperatureK, OutStar.ColorR, OutStar.ColorG, OutStar.ColorB);

    OutStar.Name = GenerateName(StarSeed.Stream(UniverseSeedDomain::StreamName));
}

void FStarSystemGenerator::GeneratePlanets(
    const FUniverseSeed& SystemSeed,
    const FStarDescriptor& Star,
    TArray<FPlanetDescriptor>& OutPlanets)
{
    OutPlanets.Reset();

    FUniverseRandom CountRandom(SystemSeed.Stream(UniverseSeedDomain::StreamPrimary).Value);

    // Planet-count distribution: most systems have a handful, a few are empty,
    // a few are crowded.
    const double CountWeights[13] = {
        0.08, 0.10, 0.12, 0.14, 0.14, 0.12, 0.10, 0.07, 0.05, 0.03, 0.02, 0.02, 0.01
    };
    const int32 PlanetCount = CountRandom.PickWeighted(CountWeights, 13);
    if (PlanetCount <= 0)
    {
        return;
    }

    const double SqrtLuminosity = FMath::Sqrt(FMath::Max(Star.LuminositySolar, 1.0e-6));

    // The frost line: beyond it volatiles condense and giants can form. Scaled
    // by sqrt(L) so a dim M dwarf's frost line is close in and a bright F
    // star's is far out, which is what makes systems around different stars
    // actually differ in character rather than just in colour.
    const double FrostLineAu = 2.7 * SqrtLuminosity;

    // Innermost orbit, also luminosity-scaled: hot stars clear their inner
    // regions.
    FUniverseRandom OrbitRandom(SystemSeed.Stream(UniverseSeedDomain::StreamOrbital).Value);
    double OrbitAu = OrbitRandom.NextRange(0.05, 0.45) * FMath::Max(SqrtLuminosity, 0.3);

    OutPlanets.Reserve(PlanetCount);

    for (int32 Index = 0; Index < PlanetCount; ++Index)
    {
        const FUniverseSeed PlanetSeed = FUniverseSeedHierarchy::GetBodySeed(SystemSeed, Index + 1);

        FPlanetDescriptor Planet;
        Planet.Seed = PlanetSeed;
        Planet.OrbitIndex = Index;

        // Each planet's own properties come from its own seed, so inserting or
        // removing a planet from a system never perturbs the others.
        FUniverseRandom Physical(PlanetSeed.Stream(UniverseSeedDomain::StreamPhysical).Value);
        FUniverseRandom Orbital(PlanetSeed.Stream(UniverseSeedDomain::StreamOrbital).Value);

        Planet.OrbitRadiusMeters = OrbitAu * UniverseScale::MetersPerAu;

        // Equilibrium temperature for a body with Earth-like albedo:
        // T = 278.6 K * L^0.25 / sqrt(a_AU).
        Planet.EquilibriumTemperatureK =
            278.6 * FMath::Pow(FMath::Max(Star.LuminositySolar, 1.0e-6), 0.25)
            / FMath::Sqrt(FMath::Max(OrbitAu, 1.0e-4));

        // Type follows from where the planet formed relative to the frost line
        // and how hot it ended up - derived rather than rolled, so a system
        // reads as physically coherent.
        if (OrbitAu < FrostLineAu)
        {
            if (Planet.EquilibriumTemperatureK > 800.0)
            {
                Planet.Type = EPlanetType::Molten;
            }
            else if (Planet.EquilibriumTemperatureK > 400.0)
            {
                Planet.Type = Physical.NextBool(0.5) ? EPlanetType::Desert : EPlanetType::Rocky;
            }
            else if (Planet.EquilibriumTemperatureK > 240.0)
            {
                const double TypeWeights[3] = { 0.35, 0.35, 0.30 };
                const int32 Pick = Physical.PickWeighted(TypeWeights, 3);
                Planet.Type = (Pick == 0) ? EPlanetType::Terrestrial
                            : (Pick == 1) ? EPlanetType::Ocean
                                          : EPlanetType::Rocky;
            }
            else
            {
                Planet.Type = EPlanetType::Rocky;
            }
        }
        else
        {
            const double GiantWeights[3] = { 0.45, 0.25, 0.30 };
            const int32 Pick = Physical.PickWeighted(GiantWeights, 3);
            Planet.Type = (Pick == 0) ? EPlanetType::GasGiant
                        : (Pick == 1) ? EPlanetType::IceGiant
                                      : EPlanetType::Ice;
        }

        // Radius and bulk density by type. Densities are in kg/m^3 and are
        // representative of the real classes: rock ~5500, ice ~1600, gas ~1300.
        double RadiusEarth = 1.0;
        double DensityKgM3 = 5500.0;

        switch (Planet.Type)
        {
        case EPlanetType::Molten:
            RadiusEarth = Physical.NextRange(0.35, 1.3);
            DensityKgM3 = Physical.NextRange(5000.0, 6200.0);
            break;
        case EPlanetType::Rocky:
            RadiusEarth = Physical.NextRange(0.3, 1.6);
            DensityKgM3 = Physical.NextRange(4200.0, 5800.0);
            break;
        case EPlanetType::Desert:
            RadiusEarth = Physical.NextRange(0.5, 1.8);
            DensityKgM3 = Physical.NextRange(4000.0, 5600.0);
            break;
        case EPlanetType::Ocean:
            RadiusEarth = Physical.NextRange(0.8, 2.2);
            DensityKgM3 = Physical.NextRange(3200.0, 5000.0);
            break;
        case EPlanetType::Terrestrial:
            RadiusEarth = Physical.NextRange(0.7, 1.9);
            DensityKgM3 = Physical.NextRange(4500.0, 5800.0);
            break;
        case EPlanetType::Ice:
            RadiusEarth = Physical.NextRange(0.25, 1.4);
            DensityKgM3 = Physical.NextRange(1200.0, 2600.0);
            break;
        case EPlanetType::GasGiant:
            RadiusEarth = Physical.NextRange(7.0, 12.0);
            DensityKgM3 = Physical.NextRange(700.0, 1700.0);
            break;
        case EPlanetType::IceGiant:
            RadiusEarth = Physical.NextRange(3.2, 5.0);
            DensityKgM3 = Physical.NextRange(1200.0, 1900.0);
            break;
        default:
            break;
        }

        Planet.RadiusMeters = RadiusEarth * EarthRadiusMeters;

        // Mass from geometry and density, then gravity from mass and radius.
        // Deriving in this order keeps the three mutually consistent - a
        // player standing on the surface later must feel the gravity that the
        // planet's own numbers imply.
        const double Volume = (4.0 / 3.0) * Pi * Planet.RadiusMeters * Planet.RadiusMeters * Planet.RadiusMeters;
        Planet.MassKg = Volume * DensityKgM3;
        Planet.SurfaceGravityMs2 =
            GravitationalConst * Planet.MassKg / (Planet.RadiusMeters * Planet.RadiusMeters);

        // Kepler's third law about the star's mass:
        // P(years) = sqrt(a^3 / M).
        Planet.OrbitalPeriodSeconds =
            FMath::Sqrt((OrbitAu * OrbitAu * OrbitAu) / FMath::Max(Star.MassSolar, 1.0e-6)) * SecondsPerYear;

        Planet.OrbitPhaseRadians = Orbital.NextRange(0.0, 2.0 * Pi);

        // Small inclinations: planets form from a disc, so a system is close
        // to flat. A uniform sphere of directions would look like a swarm.
        Planet.OrbitInclinationRadians = Orbital.NextGaussian() * 0.03;

        Planet.AxialTiltRadians = Physical.NextRange(0.0, 0.6);

        // A negative period denotes retrograde rotation, as Venus has.
        const double RotationHours = Physical.NextRange(6.0, 120.0);
        Planet.RotationPeriodSeconds = RotationHours * 3600.0 * (Physical.NextBool(0.08) ? -1.0 : 1.0);

        // Whether a planet keeps an atmosphere depends on gravity holding it
        // and temperature driving it off. Giants always qualify.
        const bool bIsGiant = (Planet.Type == EPlanetType::GasGiant || Planet.Type == EPlanetType::IceGiant);
        Planet.bHasAtmosphere = bIsGiant
            || (Planet.SurfaceGravityMs2 > 3.0 && Planet.EquilibriumTemperatureK < 700.0);

        Planet.Name = FString::Printf(TEXT("%s"), RomanNumerals[
            FMath::Min(Index, ArrayCount(RomanNumerals) - 1)]);

        OutPlanets.Add(Planet);

        // Next orbit outward. The ratio is drawn per-gap rather than fixed so
        // spacing varies between systems, but stays above 1.4 to keep orbits
        // dynamically plausible instead of implausibly packed.
        OrbitAu *= OrbitRandom.NextRange(1.4, 2.3);
    }
}

FUniversePosition FStarSystemGenerator::GetPlanetPosition(
    const FStarSystemDescriptor& System,
    const FPlanetDescriptor& Planet)
{
    const double OrbitCm = Planet.OrbitRadiusMeters * UniverseScale::CmPerMeter;

    const double CosPhase = FMath::Cos(Planet.OrbitPhaseRadians);
    const double SinPhase = FMath::Sin(Planet.OrbitPhaseRadians);
    const double CosIncl  = FMath::Cos(Planet.OrbitInclinationRadians);
    const double SinIncl  = FMath::Sin(Planet.OrbitInclinationRadians);

    // The offset is applied in canonical universe space, so a planet at 1 AU
    // really is 1.496e13 cm from its star. Only the rendering shrinks it.
    const FVector3d OrbitOffsetCm(
        OrbitCm * CosPhase * CosIncl,
        OrbitCm * SinPhase,
        OrbitCm * CosPhase * SinIncl);

    return System.Position.OffsetByCm(OrbitOffsetCm);
}

bool FStarSystemGenerator::GenerateSystem(
    const FUniverseSeedHierarchy& Hierarchy,
    int64 SectorX, int64 SectorY, int64 SectorZ, int32 IndexInSector,
    FStarSystemDescriptor& OutSystem)
{
    if (IndexInSector < 0)
    {
        return false;
    }

    const int32 Count = GetSystemCountInSector(Hierarchy, SectorX, SectorY, SectorZ);
    if (IndexInSector >= Count)
    {
        return false;
    }

    const FUniverseSeed SectorSeed = Hierarchy.GetSectorSeed(SectorX, SectorY, SectorZ);
    const FUniverseSeed SystemSeed = FUniverseSeedHierarchy::GetSystemSeed(SectorSeed, IndexInSector);

    OutSystem = FStarSystemDescriptor();
    OutSystem.Id = MakeSystemId(SectorX, SectorY, SectorZ, IndexInSector);
    OutSystem.Seed = SystemSeed;
    OutSystem.Position = GetSystemPosition(Hierarchy, SectorX, SectorY, SectorZ, IndexInSector);

    GenerateStar(SystemSeed, OutSystem.Star);
    GeneratePlanets(SystemSeed, OutSystem.Star, OutSystem.Planets);

    // The system takes its star's name; planets are that name plus a numeral.
    OutSystem.Name = OutSystem.Star.Name;
    for (FPlanetDescriptor& Planet : OutSystem.Planets)
    {
        Planet.Name = OutSystem.Name + FString(TEXT(" ")) + Planet.Name;
    }

    return true;
}

bool FStarSystemGenerator::GenerateSystem(
    const FUniverseSeedHierarchy& Hierarchy,
    const FUniverseSystemId& Id,
    FStarSystemDescriptor& OutSystem)
{
    return GenerateSystem(Hierarchy, Id.SectorX, Id.SectorY, Id.SectorZ, Id.IndexInSector, OutSystem);
}

void FStarSystemGenerator::FindSystemsWithin(
    const FUniverseSeedHierarchy& Hierarchy,
    const FUniversePosition& Centre,
    double RadiusLightYears,
    TArray<FStarSystemDescriptor>& OutSystems,
    int32 MaxResults)
{
    OutSystems.Reset();

    if (RadiusLightYears <= 0.0 || MaxResults <= 0)
    {
        return;
    }

    // How many sectors the search sphere spans. Rounded up, plus one, so a
    // sphere that only clips the corner of a sector still examines it.
    const int64 SectorRadius = static_cast<int64>(
        FMath::FloorToDouble(RadiusLightYears / UniverseScale::SectorSizeLightYears)) + 1;

    // Bound the work: this is a linear scan over a cube of sectors, so a
    // careless radius would be O(n^3). The cap converts a performance
    // catastrophe into a visibly truncated result.
    if (SectorRadius > 32)
    {
        return;
    }

    int64 CentreSectorX = 0;
    int64 CentreSectorY = 0;
    int64 CentreSectorZ = 0;
    Centre.GetSector(CentreSectorX, CentreSectorY, CentreSectorZ);

    // Iteration order is fixed by sector address, so the output array is a
    // deterministic function of the query - not of traversal order.
    for (int64 OffsetX = -SectorRadius; OffsetX <= SectorRadius; ++OffsetX)
    {
        for (int64 OffsetY = -SectorRadius; OffsetY <= SectorRadius; ++OffsetY)
        {
            for (int64 OffsetZ = -SectorRadius; OffsetZ <= SectorRadius; ++OffsetZ)
            {
                const int64 SectorX = CentreSectorX + OffsetX;
                const int64 SectorY = CentreSectorY + OffsetY;
                const int64 SectorZ = CentreSectorZ + OffsetZ;

                const int32 Count = GetSystemCountInSector(Hierarchy, SectorX, SectorY, SectorZ);
                for (int32 Index = 0; Index < Count; ++Index)
                {
                    // Test the position before generating the system: placement
                    // is a handful of random draws, whereas full generation
                    // builds a star and up to twelve planets.
                    const FUniversePosition Position =
                        GetSystemPosition(Hierarchy, SectorX, SectorY, SectorZ, Index);

                    if (FUniversePosition::DistanceLightYears(Centre, Position) > RadiusLightYears)
                    {
                        continue;
                    }

                    if (OutSystems.Num() >= MaxResults)
                    {
                        return;
                    }

                    FStarSystemDescriptor System;
                    if (GenerateSystem(Hierarchy, SectorX, SectorY, SectorZ, Index, System))
                    {
                        OutSystems.Add(System);
                    }
                }
            }
        }
    }
}

bool FStarSystemGenerator::FindNearestSystem(
    const FUniverseSeedHierarchy& Hierarchy,
    const FUniversePosition& Centre,
    double MaxRadiusLightYears,
    FStarSystemDescriptor& OutSystem)
{
    int64 CentreSectorX = 0;
    int64 CentreSectorY = 0;
    int64 CentreSectorZ = 0;
    Centre.GetSector(CentreSectorX, CentreSectorY, CentreSectorZ);

    const int64 MaxShell = FMath::Max<int64>(1, static_cast<int64>(
        FMath::FloorToDouble(MaxRadiusLightYears / UniverseScale::SectorSizeLightYears)) + 1);

    double BestDistance = MaxRadiusLightYears;
    bool bFound = false;
    FUniverseSystemId BestId;

    // Expanding shells rather than one big cube: most queries find a star
    // within a shell or two, and stopping early is the difference between
    // scanning 27 sectors and scanning tens of thousands.
    for (int64 Shell = 0; Shell <= MaxShell && Shell <= 32; ++Shell)
    {
        for (int64 OffsetX = -Shell; OffsetX <= Shell; ++OffsetX)
        {
            for (int64 OffsetY = -Shell; OffsetY <= Shell; ++OffsetY)
            {
                for (int64 OffsetZ = -Shell; OffsetZ <= Shell; ++OffsetZ)
                {
                    // Only the surface of the shell is new.
                    const int64 MaxOffset = FMath::Max(
                        FMath::Abs(OffsetX), FMath::Max(FMath::Abs(OffsetY), FMath::Abs(OffsetZ)));
                    if (MaxOffset != Shell)
                    {
                        continue;
                    }

                    const int64 SectorX = CentreSectorX + OffsetX;
                    const int64 SectorY = CentreSectorY + OffsetY;
                    const int64 SectorZ = CentreSectorZ + OffsetZ;

                    const int32 Count = GetSystemCountInSector(Hierarchy, SectorX, SectorY, SectorZ);
                    for (int32 Index = 0; Index < Count; ++Index)
                    {
                        const FUniversePosition Position =
                            GetSystemPosition(Hierarchy, SectorX, SectorY, SectorZ, Index);
                        const double Distance = FUniversePosition::DistanceLightYears(Centre, Position);

                        if (Distance < BestDistance)
                        {
                            BestDistance = Distance;
                            BestId = MakeSystemId(SectorX, SectorY, SectorZ, Index);
                            bFound = true;
                        }
                    }
                }
            }
        }

        // A hit inside the current shell cannot be beaten by anything two
        // shells further out, so stop once the best distance is safely inside.
        if (bFound && BestDistance < static_cast<double>(Shell) * UniverseScale::SectorSizeLightYears)
        {
            break;
        }
    }

    if (!bFound)
    {
        return false;
    }

    return GenerateSystem(Hierarchy, BestId, OutSystem);
}
