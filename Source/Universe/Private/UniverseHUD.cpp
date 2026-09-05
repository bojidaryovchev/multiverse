// Copyright Universe Project. All Rights Reserved.

#include "UniverseHUD.h"
#include "UniverseGameMode.h"
#include "UniverseProbePawn.h"
#include "UniverseAnchorComponent.h"
#include "AstronomicalBodyActor.h"
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
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 12.0f, 12.0f, 560.0f, 430.0f);

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

    // --- Controls ----------------------------------------------------------
    DrawHeading(TEXT("CONTROLS"), CursorY);
    DrawRow(TEXT("Move / look"), TEXT("W S  strafe A D  lift Q E  roll Z C  mouse look"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Speed / brake"), TEXT("[ ]  or mouse wheel = thrust tier,  Space = brake"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Warp"), TEXT("G = jump forward in whole cells (exact at any distance)"),
        CursorY, ColourLabel);
}
