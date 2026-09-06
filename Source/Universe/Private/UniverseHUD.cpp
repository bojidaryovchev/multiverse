// Copyright Universe Project. All Rights Reserved.

#include "UniverseHUD.h"
#include "UniverseGameMode.h"
#include "UniverseProbePawn.h"
#include "PlanetCharacter.h"
#include "PlanetEnvironmentQuery.h"
#include "PlanetVegetationComponent.h"
#include "PlanetWildlifeComponent.h"
#include "UniverseAnchorComponent.h"
#include "AstronomicalBodyActor.h"
#include "PlanetActor.h"
#include "PlanetTerrainComponent.h"
#include "UniverseWorldSubsystem.h"

#include "StarSystemDescriptor.h"
#include "UniverseScale.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    /**
     * Developer diagnostics are on by default during the prototype and off
     * with a single console command. Registered as a CVar rather than a
     * compile-time flag so it can be toggled at runtime for captures.
     */
    static TAutoConsoleVariable<int32> CVarShowUniverseDebug(
        TEXT("universe.ShowDebug"),
        1,
        TEXT("Show the universe developer diagnostics overlay (0 = off, 1 = on). F1 toggles."),
        ECVF_Cheat);

    const FLinearColor ColourHeading(0.55f, 0.80f, 1.00f);
    const FLinearColor ColourLabel(0.65f, 0.65f, 0.68f);
    const FLinearColor ColourValue(1.00f, 1.00f, 1.00f);
    const FLinearColor ColourAccent(0.55f, 1.00f, 0.65f);
    const FLinearColor ColourWarn(1.00f, 0.75f, 0.35f);

    /** Speed of light, m/s. */
    constexpr double SpeedOfLightMs = 299792458.0;

    /**
     * Formats a distance using whichever astronomical unit keeps the number
     * readable. A single fixed unit is unusable across a range that spans
     * metres to light years.
     */
    FString FormatDistance(double Meters)
    {
        const double Absolute = FMath::Abs(Meters);
        if (Absolute >= UniverseScale::MetersPerLightYear * 0.01)
        {
            return FString::Printf(TEXT("%.6f ly"), Meters / UniverseScale::MetersPerLightYear);
        }
        if (Absolute >= UniverseScale::MetersPerAu * 0.001)
        {
            return FString::Printf(TEXT("%.6f AU"), Meters / UniverseScale::MetersPerAu);
        }
        if (Absolute >= 1000.0)
        {
            return FString::Printf(TEXT("%.3f km"), Meters / 1000.0);
        }
        return FString::Printf(TEXT("%.3f m"), Meters);
    }

    FString FormatSpeed(double MetersPerSecond)
    {
        const double InC = MetersPerSecond / SpeedOfLightMs;
        if (InC >= 0.001)
        {
            return FString::Printf(TEXT("%.6g m/s  (%.4g c)"), MetersPerSecond, InC);
        }
        if (MetersPerSecond >= 1000.0)
        {
            return FString::Printf(TEXT("%.3f km/s"), MetersPerSecond / 1000.0);
        }
        return FString::Printf(TEXT("%.3f m/s"), MetersPerSecond);
    }
}

AUniverseHUD::AUniverseHUD()
{
    PrimaryActorTick.bCanEverTick = false;
}

void AUniverseHUD::DrawHeading(const FString& Text, float& CursorY)
{
    if (Canvas == nullptr)
    {
        return;
    }
    CursorY += RowHeight * 0.5f;
    FCanvasTextItem Item(FVector2D(LabelColumnX, CursorY), FText::FromString(Text),
        GEngine->GetSmallFont(), ColourHeading);
    Item.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(Item);
    CursorY += RowHeight;
}

void AUniverseHUD::DrawRow(const FString& Label, const FString& Value, float& CursorY, const FLinearColor& Colour)
{
    if (Canvas == nullptr)
    {
        return;
    }

    FCanvasTextItem LabelItem(FVector2D(LabelColumnX + 8.0f, CursorY), FText::FromString(Label),
        GEngine->GetSmallFont(), ColourLabel);
    LabelItem.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(LabelItem);

    FCanvasTextItem ValueItem(FVector2D(ValueColumnX, CursorY), FText::FromString(Value),
        GEngine->GetSmallFont(), Colour);
    ValueItem.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(ValueItem);

    CursorY += RowHeight;
}

void AUniverseHUD::DrawBodyMarkers()
{
    UWorld* World = GetWorld();
    if (World == nullptr || Canvas == nullptr || GEngine == nullptr)
    {
        return;
    }

    const AUniverseGameMode* GameMode = World->GetAuthGameMode<AUniverseGameMode>();
    const AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));
    if (GameMode == nullptr || Probe == nullptr)
    {
        return;
    }

    const FUniversePosition ProbePosition = Probe->GetUniversePosition();

    for (const TObjectPtr<AAstronomicalBodyActor>& Body : GameMode->GetSpawnedBodies())
    {
        if (Body == nullptr)
        {
            continue;
        }

        const UUniverseAnchorComponent* BodyAnchor = Body->GetAnchor();
        if (BodyAnchor == nullptr || BodyAnchor->IsOutOfRenderRange())
        {
            continue;
        }

        const FVector Screen = Project(Body->GetActorLocation());

        // Project returns a negative Z for anything behind the camera; drawing
        // those would put mirrored markers all over the screen.
        if (Screen.Z <= 0.0)
        {
            continue;
        }

        const float X = static_cast<float>(Screen.X);
        const float Y = static_cast<float>(Screen.Y);
        if (X < 0.0f || Y < 0.0f || X > Canvas->ClipX || Y > Canvas->ClipY)
        {
            continue;
        }

        // A small open reticle rather than a filled dot, so it does not hide
        // the body once the player is close enough to see it.
        const float Size = 7.0f;
        const FLinearColor MarkerColour(0.45f, 0.85f, 1.0f, 0.85f);
        DrawLine(X - Size, Y, X - Size * 0.4f, Y, MarkerColour, 1.0f);
        DrawLine(X + Size * 0.4f, Y, X + Size, Y, MarkerColour, 1.0f);
        DrawLine(X, Y - Size, X, Y - Size * 0.4f, MarkerColour, 1.0f);
        DrawLine(X, Y + Size * 0.4f, X, Y + Size, MarkerColour, 1.0f);

        // True logical distance, not the scaled render distance - the label has
        // to tell the truth about where the thing actually is.
        const double DistanceMeters = FUniversePosition::DistanceMeters(
            ProbePosition, BodyAnchor->GetUniversePosition());

        const FString Label = FString::Printf(TEXT("%s  %s"),
            *Body->GetBodyName(), *FormatDistance(DistanceMeters));

        FCanvasTextItem Item(FVector2D(X + Size + 4.0f, Y - 7.0f),
            FText::FromString(Label), GEngine->GetSmallFont(), MarkerColour);
        Item.EnableShadow(FLinearColor::Black);
        Canvas->DrawItem(Item);
    }
}

void AUniverseHUD::DrawHUD()
{
    Super::DrawHUD();

    if (CVarShowUniverseDebug.GetValueOnGameThread() == 0 || Canvas == nullptr || GEngine == nullptr)
    {
        return;
    }

    DrawBodyMarkers();

    UWorld* World = GetWorld();
    if (World == nullptr)
    {
        return;
    }

    UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>();
    AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));

    float CursorY = 24.0f;

    // Translucent backing so white text stays legible against a star field.
    // AHUD::DrawRect fills; Canvas->K2_DrawBox would only outline.
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 12.0f, 12.0f, 760.0f, 820.0f);

    DrawHeading(TEXT("UNIVERSE DIAGNOSTICS   [F1 to toggle]"), CursorY);

    // --- Universe identity -------------------------------------------------
    if (Subsystem != nullptr)
    {
        DrawRow(TEXT("Universe seed"),
            FString::Printf(TEXT("\"%s\"  ->  %s"),
                *Subsystem->GetUniverseSeedText(), *Subsystem->GetUniverseSeedHex()),
            CursorY, ColourAccent);
    }

    DrawRow(TEXT("Cell size"),
        FString::Printf(TEXT("2^%d cm  =  %.4f AU"),
            UniverseScale::CellShift, UniverseScale::AuPerCell), CursorY, ColourValue);

    DrawRow(TEXT("Sector size"),
        FString::Printf(TEXT("%.4f ly"), UniverseScale::SectorSizeLightYears), CursorY, ColourValue);

    if (Subsystem != nullptr)
    {
        // Stated explicitly so nobody mistakes the rendered scene for 1:1.
        // Astronomical bodies are drawn as a uniform scale model, which is
        // geometrically exact - angular sizes are the real ones.
        const FUniversePresentationSettings& Settings = Subsystem->GetPresentation();
        const bool bInflated = Settings.DebugBodyRadiusInflation != 1.0;
        DrawRow(TEXT("Scaled-space factor"),
            bInflated
                ? FString::Printf(TEXT("%.3g   RADII INFLATED x%.4g (not to scale)"),
                    Settings.AstronomicalScale, Settings.DebugBodyRadiusInflation)
                : FString::Printf(TEXT("%.3g   uniform, angular sizes exact"),
                    Settings.AstronomicalScale),
            CursorY, bInflated ? ColourWarn : ColourValue);
    }

    // --- Position ----------------------------------------------------------
    if (Probe != nullptr)
    {
        const FUniversePosition Position = Probe->GetUniversePosition();

        int64 SectorX = 0;
        int64 SectorY = 0;
        int64 SectorZ = 0;
        Position.GetSector(SectorX, SectorY, SectorZ);

        DrawHeading(TEXT("POSITION"), CursorY);

        DrawRow(TEXT("Global cell"),
            FString::Printf(TEXT("[%lld, %lld, %lld]"),
                static_cast<long long>(Position.CellX),
                static_cast<long long>(Position.CellY),
                static_cast<long long>(Position.CellZ)),
            CursorY, ColourAccent);

        DrawRow(TEXT("Cell-local offset"),
            FString::Printf(TEXT("[%.1f, %.1f, %.1f] cm"),
                Position.Local.X, Position.Local.Y, Position.Local.Z),
            CursorY, ColourValue);

        DrawRow(TEXT("Sector"),
            FString::Printf(TEXT("[%lld, %lld, %lld]"),
                static_cast<long long>(SectorX),
                static_cast<long long>(SectorY),
                static_cast<long long>(SectorZ)),
            CursorY, ColourValue);

        DrawRow(TEXT("From universe origin"),
            FormatDistance(FUniversePosition::DistanceMeters(FUniversePosition(), Position)),
            CursorY, ColourValue);

        // The headline evidence that the architecture works: the Unreal
        // transform stays small no matter how large the global position gets.
        const FVector LocalUnreal = Probe->GetActorLocation();
        const bool bLocalSafe = LocalUnreal.Size() <= (Subsystem != nullptr
            ? Subsystem->GetPresentation().RebaseRadiusCm * 1.05
            : UniverseScale::DefaultRebaseRadiusCm * 1.05);

        DrawRow(TEXT("Unreal local position"),
            FString::Printf(TEXT("[%.1f, %.1f, %.1f] cm   |%.1f| cm"),
                LocalUnreal.X, LocalUnreal.Y, LocalUnreal.Z, LocalUnreal.Size()),
            CursorY, bLocalSafe ? ColourAccent : ColourWarn);

        if (Subsystem != nullptr)
        {
            DrawRow(TEXT("Render origin"),
                Subsystem->GetRenderOrigin().ToCompactString(), CursorY, ColourValue);

            DrawRow(TEXT("Rebases / last shift"),
                FString::Printf(TEXT("%d   /   %.1f cm"),
                    Subsystem->GetRebaseCount(), Subsystem->GetLastRebaseShift().Size()),
                CursorY, ColourValue);

            DrawRow(TEXT("Anchored actors"),
                FString::Printf(TEXT("%d"), Subsystem->GetRegisteredAnchorCount()),
                CursorY, ColourValue);
        }

        // --- Motion --------------------------------------------------------
        DrawHeading(TEXT("MOTION"), CursorY);

        DrawRow(TEXT("Logical speed"), FormatSpeed(Probe->GetSpeedMetersPerSecond()),
            CursorY, ColourAccent);

        const FVector3d Velocity = Probe->GetUniverseVelocity();
        DrawRow(TEXT("Logical velocity"),
            FString::Printf(TEXT("[%.4g, %.4g, %.4g] m/s"), Velocity.X, Velocity.Y, Velocity.Z),
            CursorY, ColourValue);

        DrawRow(TEXT("Thrust tier"),
            FString::Printf(TEXT("%d  (x%.0g)  %s"),
                Probe->GetSpeedTier(), Probe->GetSpeedMultiplier(), *Probe->GetSpeedTierLabel()),
            CursorY, ColourValue);

        DrawRow(TEXT("Odometer"),
            FString::Printf(TEXT("%.6f ly"), Probe->GetOdometerLightYears()), CursorY, ColourValue);
    }

    // --- Astronomy ---------------------------------------------------------
    DrawHeading(TEXT("ASTRONOMY"), CursorY);

    const AUniverseGameMode* GameMode = World->GetAuthGameMode<AUniverseGameMode>();
    if (GameMode != nullptr && GameMode->HasActiveSystem())
    {
        const FStarSystemDescriptor& System = GameMode->GetActiveSystem();
        DrawRow(TEXT("Spawned system"),
            FString::Printf(TEXT("%s   class %s   %d planets"),
                *System.Name, ToString(System.Star.Class), System.Planets.Num()),
            CursorY, ColourValue);

        if (Probe != nullptr)
        {
            DrawRow(TEXT("Distance to its star"),
                FormatDistance(FUniversePosition::DistanceMeters(
                    Probe->GetUniversePosition(), System.Position)),
                CursorY, ColourValue);
        }
    }
    else
    {
        DrawRow(TEXT("Spawned system"), TEXT("none"), CursorY, ColourWarn);
    }

    if (Subsystem != nullptr)
    {
        FStarSystemDescriptor Nearest;
        if (Subsystem->GetNearestSystem(Nearest))
        {
            DrawRow(TEXT("Nearest system"),
                FString::Printf(TEXT("%s   %s   %.4f ly"),
                    *Nearest.Name, *Nearest.Id.ToDebugString(),
                    Subsystem->GetNearestSystemDistanceLightYears()),
                CursorY, ColourAccent);
        }
        else
        {
            DrawRow(TEXT("Nearest system"),
                TEXT("none within the search radius"), CursorY, ColourWarn);
        }
    }

    // --- Simulation frame ---------------------------------------------------
    //
    // First of the gameplay sections, deliberately. Every number below it -
    // altitude, gravity, which way is up - is meaningless in the interstellar
    // frame, so the frame is what tells you how to read the rest.
    if (Subsystem != nullptr)
    {
        DrawHeading(TEXT("SIMULATION FRAME"), CursorY);

        const FUniverseFrameState& Frame = Subsystem->GetFrameState();
        const APlanetActor* FramePlanet = Subsystem->GetFramePlanet();

        DrawRow(TEXT("Frame"),
            FString::Printf(TEXT("%s   dominance %.3f   transitions %d"),
                LexToString(Frame.Kind),
                Subsystem->GetFrameDominance(),
                Subsystem->GetFrameTransitionCount()),
            CursorY,
            Frame.IsPlanetary() ? ColourAccent : ColourValue);

        if (FramePlanet != nullptr)
        {
            const FPlanetFrameBounds& Bounds = FramePlanet->GetFrameBounds();

            DrawRow(TEXT("Attached to"),
                FString::Printf(TEXT("%s   enter %s   leave %s"),
                    *FramePlanet->GetName(),
                    *FormatDistance(Bounds.EnterRadiusMeters),
                    *FormatDistance(Bounds.ExitRadiusMeters)),
                CursorY, ColourValue);

            // The three altitudes, side by side and separately labelled.
            // Showing them together is the point: on this planet they differ
            // by kilometres, and seeing that on screen is what stops anyone
            // reaching for whichever one is nearest to hand.
            const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

            FUniversePosition Observer = Subsystem->GetRenderOrigin();

            if (const AUniverseProbePawn* AsProbe = Cast<AUniverseProbePawn>(Pawn))
            {
                Observer = AsProbe->GetUniversePosition();
            }
            else if (const APlanetCharacter* AsCharacter = Cast<APlanetCharacter>(Pawn))
            {
                Observer = AsCharacter->GetUniversePosition();
            }

            const FPlanetSurfaceSample Ground = FramePlanet->SampleSurfaceBelow(Observer);
            const double FromCentre = FramePlanet->GetDistanceFromCentreMeters(Observer);

            DrawRow(TEXT("From centre"), FormatDistance(FromCentre), CursorY, ColourValue);

            DrawRow(TEXT("Above sea level"),
                FormatDistance(FramePlanet->GetAltitudeAboveSeaLevelMeters(Observer)),
                CursorY, ColourValue);

            DrawRow(TEXT("Above terrain"),
                FString::Printf(TEXT("%s   (ground elevation %+.0f m)"),
                    *FormatDistance(FromCentre - Ground.SurfaceRadiusMeters),
                    Ground.ElevationMeters),
                CursorY, ColourAccent);

            const FVector3d Gravity = FramePlanet->GetGravityAccelerationMs2(Observer);

            DrawRow(TEXT("Gravity"),
                FString::Printf(TEXT("%.3f m/s2   atmosphere %.0f%%   escape %.0f m/s"),
                    Gravity.Size(),
                    FramePlanet->GetAtmosphericDepthFraction(Observer) * 100.0,
                    FramePlanet->GetGravityField().GetEscapeVelocityMs()),
                CursorY, ColourValue);
        }

        // Whichever pawn is being flown or walked, and what state it is in.
        if (const AUniverseProbePawn* AsProbe =
                Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0)))
        {
            DrawRow(TEXT("Ship"),
                FString::Printf(TEXT("%s   step clamps %d   [F to step out when landed]"),
                    AsProbe->IsLanded() ? TEXT("LANDED") : TEXT("flying"),
                    AsProbe->GetCollisionClampCount()),
                CursorY, AsProbe->IsLanded() ? ColourAccent : ColourValue);
        }
        else if (const APlanetCharacter* AsCharacter =
                     Cast<APlanetCharacter>(UGameplayStatics::GetPlayerPawn(World, 0)))
        {
            const bool bHolding = AsCharacter->IsWaitingForCollision();

            DrawRow(TEXT("On foot"),
                FString::Printf(TEXT("%s   gravity %.2f m/s2   [F beside the ship to board]"),
                    bHolding ? TEXT("HOLDING - waiting for collision") : TEXT("walking"),
                    AsCharacter->GetGravityMagnitudeMs2()),
                CursorY, bHolding ? ColourWarn : ColourAccent);
        }
    }

    // --- Environment --------------------------------------------------------
    //
    // Placed between the frame and the terrain because it answers the question
    // those two raise: the frame says which planet, the terrain says what shape
    // the ground is, and this says what the ground *is*.
    if (Subsystem != nullptr)
    {
        if (const APlanetActor* Planet = Subsystem->GetFramePlanet())
        {
            const APawn* Pawn = UGameplayStatics::GetPlayerPawn(World, 0);

            FUniversePosition Observer = Subsystem->GetRenderOrigin();

            if (const AUniverseProbePawn* AsProbe = Cast<AUniverseProbePawn>(Pawn))
            {
                Observer = AsProbe->GetUniversePosition();
            }
            else if (const APlanetCharacter* AsCharacter = Cast<APlanetCharacter>(Pawn))
            {
                Observer = AsCharacter->GetUniversePosition();
            }

            const FPlanetEnvironmentDescriptor& Environment = Planet->GetEnvironment();

            const FEnvironmentSample Sample = FPlanetEnvironmentQuery::Sample(
                Planet->GetPlanetDescriptor(), Environment, Planet->GetTerrainSettings(),
                Planet->UniverseToPlanetLocalMeters(Observer),
                Planet->GetSimulationTimeSeconds());

            DrawHeading(TEXT("ENVIRONMENT"), CursorY);

            DrawRow(TEXT("World"),
                FString::Printf(TEXT("%s   %.1f C mean   atm %.2f   ocean %.0f%%   veg %.2f   env v%u"),
                    LexToString(Environment.Biosphere),
                    Environment.MeanSurfaceTemperatureK - 273.15,
                    Environment.AtmosphereDensity,
                    Environment.OceanCoverage * 100.0,
                    Environment.VegetationPotential,
                    Environment.GenerationVersion),
                CursorY, ColourValue);

            if (Sample.bValid)
            {
                // The blend, not just the winner. A point that is 40% forest
                // and 35% grassland is a transition, and showing only "forest"
                // there makes the classifier look wrong when it is working.
                FString BiomeText;

                for (int32 Index = 0; Index < Sample.Biome.Num; ++Index)
                {
                    BiomeText += FString::Printf(TEXT("%s%s %.0f%%"),
                        (Index > 0) ? TEXT("  +  ") : TEXT(""),
                        LexToString(Sample.Biome.Biomes[Index]),
                        Sample.Biome.Weights[Index] * 100.0);
                }

                DrawRow(TEXT("Biome"), BiomeText, CursorY, ColourAccent);

                DrawRow(TEXT("Climate"),
                    FString::Printf(TEXT("%.1f C   humidity %.2f   slope %.2f   %s"),
                        Sample.GetTemperatureCelsius(),
                        Sample.GetHumidity(),
                        Sample.GetSlopeCosine(),
                        Sample.IsOcean()
                            ? *FString::Printf(TEXT("OCEAN, %.0f m deep"), Sample.GetWaterDepthMeters())
                            : TEXT("land")),
                    CursorY, ColourValue);

                DrawRow(TEXT("Weather"),
                    FString::Printf(TEXT("%s   cloud %.0f%%   precip %.0f%%%s   fog %.0f%%   wet %.0f%%"),
                        LexToString(Sample.GetWeatherState()),
                        Sample.Weather.Cloudiness * 100.0,
                        Sample.Weather.Precipitation * 100.0,
                        Sample.Weather.bFrozen ? TEXT(" (frozen)") : TEXT(""),
                        Sample.Weather.Fog * 100.0,
                        Sample.GetSurfaceWetness() * 100.0),
                    CursorY,
                    Sample.Weather.Precipitation > 0.05f ? ColourAccent : ColourValue);

                DrawRow(TEXT("Wind / sun"),
                    FString::Printf(TEXT("%.1f m/s   sun %.1f deg   time of day %.2f   day %.1f h"),
                        Sample.GetWindSpeedMs(),
                        Planet->GetSolarElevationDegrees(Observer),
                        Planet->GetTimeOfDayFraction(Observer),
                        Environment.GetDayLengthSeconds() / 3600.0),
                    CursorY, ColourValue);
            }

            if (const UPlanetVegetationComponent* Vegetation = Planet->GetVegetationComponent())
            {
                DrawRow(TEXT("Vegetation"),
                    FString::Printf(TEXT("%d instances in %d patches   canopy %d   under %d   ground %d   rock %d"),
                        Vegetation->GetInstanceCount(),
                        Vegetation->GetActivePatchCount(),
                        Vegetation->GetLayerInstanceCount(EVegetationLayer::Canopy),
                        Vegetation->GetLayerInstanceCount(EVegetationLayer::Understory),
                        Vegetation->GetLayerInstanceCount(EVegetationLayer::Ground),
                        Vegetation->GetLayerInstanceCount(EVegetationLayer::Scatter)),
                    CursorY, ColourValue);

                DrawRow(TEXT("Env streaming"),
                    FString::Printf(TEXT("%d jobs in flight   %d placed   %d released   %d wildlife"),
                        Vegetation->GetPendingJobCount(),
                        Vegetation->GetTotalGenerated(),
                        Vegetation->GetTotalReleased(),
                        Planet->GetWildlifeComponent() != nullptr
                            ? Planet->GetWildlifeComponent()->GetActiveCount()
                            : 0),
                    CursorY, ColourValue);
            }
        }
    }

    // --- Terrain -----------------------------------------------------------
    if (GameMode != nullptr)
    {
        if (const APlanetActor* PlanetActor = GameMode->GetPlanetActor())
        {
            if (const UPlanetTerrainComponent* Terrain = PlanetActor->GetTerrainComponent())
            {
                const FPlanetTerrainStats& T = Terrain->GetStats();
                const FPlanetSurfaceDescriptor& Surface = PlanetActor->GetPlanetDescriptor();

                DrawHeading(TEXT("PLANET TERRAIN"), CursorY);

                DrawRow(TEXT("Planet"),
                    FString::Printf(TEXT("r=%.1f km   relief +%.0f / -%.0f m   gen v%u"),
                        Surface.RadiusMeters / 1000.0,
                        Surface.MaxElevationMeters, Surface.MaxDepthMeters,
                        Surface.GenerationVersion),
                    CursorY, ColourValue);

                DrawRow(TEXT("Observer altitude"),
                    FormatDistance(static_cast<double>(T.ObserverAltitudeMeters)), CursorY, ColourAccent);

                DrawRow(TEXT("Patches"),
                    FString::Printf(TEXT("selected %d   visible %d   generating %d   pooled %d"),
                        T.SelectedPatches, T.VisiblePatches, T.GeneratingPatches, T.PooledSlots),
                    CursorY, ColourAccent);

                DrawRow(TEXT("Geometry"),
                    FString::Printf(TEXT("%d triangles   %d vertices   deepest LOD %d"),
                        T.TriangleCount, T.VertexCount, T.DeepestLevel),
                    CursorY, ColourValue);

                DrawRow(TEXT("Culling / balancing"),
                    FString::Printf(TEXT("horizon-culled %d   balance splits %d   collision %d"),
                        T.HorizonCulled, T.BalancingSplits, T.CollisionPatches),
                    CursorY, ColourValue);

                DrawRow(TEXT("Streaming"),
                    FString::Printf(TEXT("built %d   released %d   discarded %d   prewarm %d"),
                        T.TotalGenerated, T.TotalReleased, T.DiscardedResults, T.PrewarmSelected),
                    CursorY, ColourValue);

                DrawRow(TEXT("Timing"),
                    FString::Printf(TEXT("select %.2f ms   upload %.2f ms   avg gen %.2f ms"),
                        T.LastSelectionMs, T.LastUploadMs, T.AverageGenerationMs),
                    CursorY, ColourValue);
            }
        }
    }

    // --- Controls ----------------------------------------------------------
    DrawHeading(TEXT("CONTROLS"), CursorY);
    DrawRow(TEXT("Move / look"), TEXT("W S  strafe A D  lift Q E  roll Z C  mouse look"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Speed / brake"), TEXT("[ ]  or mouse wheel = thrust tier,  Space = brake"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Warp"), TEXT("G = jump forward in whole cells (exact at any distance)"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Terrain debug"),
        TEXT("universe.TerrainDebugMode 0 elev / 1 LOD / 2 face / 3 patches"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Jump to altitude"),
        TEXT("universe.GotoAltitude <metres>"), CursorY, ColourLabel);
    DrawRow(TEXT("Land / walk"),
        TEXT("universe.Land   then F to step out,  WASD + Space to walk and jump"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Journey test"),
        TEXT("universe.Journey 1 = scripted orbit -> descent -> landing -> walk"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Environment"),
        TEXT("universe.EnvInfo <n> survey,  universe.GotoBiome <name>,  universe.ForceWeather <state>"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Time"),
        TEXT("universe.TimeScale <n> accelerates day/night and weather"),
        CursorY, ColourLabel);
}
