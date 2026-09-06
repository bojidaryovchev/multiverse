// Copyright Universe Project. All Rights Reserved.

#include "UniverseHUD.h"
#include "UniverseGameMode.h"
#include "UniverseProbePawn.h"
#include "PlanetCharacter.h"
#include "PlanetEnvironmentQuery.h"
#include "PlanetVegetationComponent.h"
#include "PlanetWildlifeComponent.h"
#include "PlanetStructureComponent.h"
#include "WorldStateSubsystem.h"
#include "UniverseAnchorComponent.h"
#include "AstronomicalBodyActor.h"
#include "PlanetActor.h"
#include "PlanetTerrainComponent.h"
#include "UniverseWorldSubsystem.h"
#include "StarSystemStreamingSubsystem.h"
#include "UniverseGameState.h"
#include "UniverseNetSubsystem.h"
#include "UniversePlayerController.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "InterstellarTravel.h"
#include "GalaxyDescriptor.h"

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
        const double InC = MetersPerSecond / UniversePhysics::SpeedOfLightMs;
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

namespace
{
    /** A readable altitude, or an empty string when there is no ground below. */
    FString DescribeAltitude(const AUniverseProbePawn& Probe)
    {
        const double Meters = static_cast<double>(Probe.GetAltitudeAboveTerrainMeters());

        if (Meters <= 0.0)
        {
            return FString();
        }

        if (Meters >= 1000.0)
        {
            return FString::Printf(TEXT("%.1f km"), Meters / 1000.0);
        }

        return FString::Printf(TEXT("%.0f m"), Meters);
    }
}

FString AUniverseHUD::GetContextualPrompt() const
{
    const UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return FString();
    }

    // --- On foot -------------------------------------------------------------
    if (const APlanetCharacter* Character =
            Cast<APlanetCharacter>(UGameplayStatics::GetPlayerPawn(World, 0)))
    {
        if (Character->IsWaitingForCollision())
        {
            return TEXT("Waiting for the ground to finish building ...");
        }

        const AUniverseProbePawn* Ship = Character->GetShipToReenter();

        if (Ship != nullptr)
        {
            const double ToShip = FUniversePosition::DistanceMeters(
                Character->GetUniversePosition(), Ship->GetUniversePosition());

            if (ToShip <= static_cast<double>(Character->ShipBoardingRangeMeters))
            {
                return TEXT("[F] board the ship     [B] build     [X] clear    [WASD] walk");
            }
        }

        return TEXT("[B] build     [X] clear     [WASD] walk     the ship is where you left it");
    }

    // --- In the ship ---------------------------------------------------------
    const AUniverseProbePawn* Probe =
        Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));

    if (Probe == nullptr)
    {
        return FString();
    }

    if (Probe->IsLanded())
    {
        return TEXT("[F] step outside     [W] lift off");
    }

    if (Probe->IsWarpEngaged())
    {
        return TEXT("[J] drop out of warp     the drive stops itself on arrival");
    }

    const double Altitude = static_cast<double>(Probe->GetAltitudeAboveTerrainMeters());

    if (Altitude > 0.0 && Altitude < 200000.0)
    {
        return TEXT("[L] land     [Space] brake     [WASD] fly");
    }

    const UStarSystemStreamingSubsystem* Streamer =
        World->GetSubsystem<UStarSystemStreamingSubsystem>();

    if (Streamer != nullptr && Streamer->HasTarget())
    {
        return TEXT("[J] engage warp     [N] next target     [Space] brake");
    }

    return TEXT("[T] target the star ahead     [N] next target     [WASD] fly");
}

void AUniverseHUD::DrawPrompt(const FString& Text, const FLinearColor& Colour)
{
    if (Text.IsEmpty() || Canvas == nullptr || GEngine == nullptr)
    {
        return;
    }

    float Width = 0.0f;
    float Height = 0.0f;
    GetTextSize(Text, Width, Height, GEngine->GetMediumFont(), 1.0f);

    const float X = (Canvas->ClipX - Width) * 0.5f;
    const float Y = Canvas->ClipY - 96.0f;

    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.45f), X - 12.0f, Y - 6.0f, Width + 24.0f, Height + 12.0f);

    FCanvasTextItem Item(FVector2D(X, Y), FText::FromString(Text), GEngine->GetMediumFont(), Colour);
    Item.EnableShadow(FLinearColor::Black);
    Canvas->DrawItem(Item);
}

void AUniverseHUD::DrawPlayerHud()
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return;
    }

    const AUniverseProbePawn* Probe =
        Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));

    const APlanetCharacter* Character =
        Cast<APlanetCharacter>(UGameplayStatics::GetPlayerPawn(World, 0));

    // --- Bottom-left status ---------------------------------------------------
    //
    // Seven lines at most, on purpose. The diagnostics panel exists for
    // everything else, and a player who wants a patch count can press F1.
    TArray<TPair<FString, FString>> Rows;

    if (Probe != nullptr)
    {
        Rows.Emplace(TEXT("Speed"),
            FInterstellarTravel::FormatSpeed(Probe->GetSpeedMetersPerSecond()));

        Rows.Emplace(TEXT("Mode"),
            Probe->IsWarpEngaged()
                ? FString::Printf(TEXT("%s   WARP"), *Probe->GetTravelModeName())
                : Probe->GetTravelModeName());

        const FString Altitude = DescribeAltitude(*Probe);

        if (!Altitude.IsEmpty())
        {
            Rows.Emplace(TEXT("Altitude"), Altitude);
        }

        if (const UStarSystemStreamingSubsystem* Streamer =
                World->GetSubsystem<UStarSystemStreamingSubsystem>())
        {
            if (Streamer->HasTarget())
            {
                const FTravelTarget Target = Streamer->GetTravelTarget();

                Rows.Emplace(TEXT("Target"), Target.Name);
                Rows.Emplace(TEXT("Distance"),
                    FInterstellarTravel::FormatDistance(Probe->GetDistanceToTargetMeters()));

                const double Eta = Probe->GetEstimatedArrivalSeconds();

                if (Eta >= 0.0)
                {
                    Rows.Emplace(TEXT("Arrival"),
                        Eta < 90.0
                            ? FString::Printf(TEXT("%.0f s"), Eta)
                            : FString::Printf(TEXT("%.1f min"), Eta / 60.0));
                }
            }
        }
    }
    else if (Character != nullptr)
    {
        Rows.Emplace(TEXT("On foot"),
            FString::Printf(TEXT("%.1f m above the ground"),
                Character->GetAltitudeAboveTerrainMeters()));
    }

    // Connection state, but only when there is one to have.
    if (World->GetNetMode() != NM_Standalone)
    {
        if (const AUniverseGameState* NetState = World->GetGameState<AUniverseGameState>())
        {
            Rows.Emplace(TEXT("Connection"),
                NetState->IsWorldIdentityVerified()
                    ? FString(TEXT("connected"))
                    : FString(TEXT("INCOMPATIBLE UNIVERSE")));
        }

        if (const UUniverseNetSubsystem* Net = World->GetSubsystem<UUniverseNetSubsystem>())
        {
            const int32 Nearby = Net->CountAtLeast(ENetRelevanceClass::SameSystem);

            if (Nearby > 0)
            {
                Rows.Emplace(TEXT("Players here"), FString::Printf(TEXT("%d"), Nearby));
            }
        }
    }

    if (Rows.Num() > 0 && Canvas != nullptr && GEngine != nullptr)
    {
        const float LineHeight = 18.0f;
        const float PanelHeight = Rows.Num() * LineHeight + 16.0f;
        const float Top = Canvas->ClipY - PanelHeight - 140.0f;

        DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.40f), 20.0f, Top - 8.0f, 330.0f, PanelHeight);

        float Y = Top;

        for (const TPair<FString, FString>& Row : Rows)
        {
            FCanvasTextItem Label(FVector2D(32.0f, Y),
                FText::FromString(Row.Key), GEngine->GetSmallFont(),
                FLinearColor(0.65f, 0.72f, 0.80f));
            Label.EnableShadow(FLinearColor::Black);
            Canvas->DrawItem(Label);

            FCanvasTextItem Value(FVector2D(140.0f, Y),
                FText::FromString(Row.Value), GEngine->GetSmallFont(),
                FLinearColor(0.92f, 0.95f, 1.0f));
            Value.EnableShadow(FLinearColor::Black);
            Canvas->DrawItem(Value);

            Y += LineHeight;
        }
    }

    DrawPrompt(GetContextualPrompt(), FLinearColor(0.85f, 0.90f, 1.0f));
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

    // Gathered per frame rather than cached: which bodies exist is the
    // streamer's business and changes as the player moves.
    TArray<AAstronomicalBodyActor*> VisibleBodies;
    GameMode->GetVisibleBodies(VisibleBodies);

    for (AAstronomicalBodyActor* Body : VisibleBodies)
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

    if (Canvas == nullptr || GEngine == nullptr)
    {
        return;
    }

    // The player's HUD is always on; the diagnostics panel is what F1 toggles.
    // That is the opposite of how it was through Sprint 007, and it is the
    // right way round for a build somebody else runs.
    DrawPlayerHud();

    if (CVarShowUniverseDebug.GetValueOnGameThread() == 0)
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
    DrawRect(FLinearColor(0.0f, 0.0f, 0.0f, 0.55f), 12.0f, 12.0f, 780.0f, 880.0f);

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

        // --- Travel (Sprint 006 sections 44 and 17) -------------------------
        //
        // Speed and position are already above; what this adds is the *regime*
        // and the destination, which are the two things a player crossing
        // interstellar space cannot work out from a number in metres per
        // second.
        DrawHeading(TEXT("TRAVEL"), CursorY);

        DrawRow(TEXT("Mode"),
            FString::Printf(TEXT("%s%s"),
                *Probe->GetTravelModeName(),
                Probe->IsWarpEngaged() ? TEXT("   WARP ENGAGED") : TEXT("")),
            CursorY, Probe->IsWarpEngaged() ? ColourAccent : ColourValue);

        DrawRow(TEXT("Speed"),
            FInterstellarTravel::FormatSpeed(Probe->GetSpeedMetersPerSecond()),
            CursorY, ColourAccent);

        const UStarSystemStreamingSubsystem* TravelStreamer =
            World->GetSubsystem<UStarSystemStreamingSubsystem>();

        if (TravelStreamer != nullptr && TravelStreamer->HasTarget())
        {
            const FTravelTarget Target = TravelStreamer->GetTravelTarget();

            DrawRow(TEXT("Target"), Target.Name, CursorY, ColourAccent);

            DrawRow(TEXT("Distance"),
                FInterstellarTravel::FormatDistance(Probe->GetDistanceToTargetMeters()),
                CursorY, ColourValue);

            const double Eta = Probe->GetEstimatedArrivalSeconds();

            DrawRow(TEXT("ETA"),
                (Eta >= 0.0)
                    ? FString::Printf(TEXT("%.0f s%s"), Eta,
                        Probe->IsBraking() ? TEXT("   BRAKING") : TEXT(""))
                    : FString(TEXT("--")),
                CursorY, Probe->IsBraking() ? ColourWarn : ColourValue);
        }
        else
        {
            DrawRow(TEXT("Target"), TEXT("none   (universe.Target ahead)"), CursorY, ColourLabel);
        }

        DrawRow(TEXT("Autopilot"),
            FString::Printf(TEXT("steer %s   brake %s"),
                Probe->IsAutoSteerEnabled() ? TEXT("on") : TEXT("off"),
                Probe->IsAutoBrakeEnabled() ? TEXT("on") : TEXT("off")),
            CursorY, ColourValue);

        // Hazard stops are the evidence that the swept trajectory check is
        // alive. A number that never moves across a session spent flying
        // through crowded space is a check that has quietly stopped running.
        DrawRow(TEXT("Hazard stops"),
            FString::Printf(TEXT("%d"), Probe->GetHazardStopCount()),
            CursorY, (Probe->GetHazardStopCount() > 0) ? ColourWarn : ColourValue);
    }

    // --- Network (Sprint 007 sections 54 and 56) ---------------------------
    //
    // Shown only when there is a network. In single player every line would say
    // "standalone" and "none", which is noise on a panel that is meant to be
    // read at a glance.
    if (World->GetNetMode() != NM_Standalone)
    {
        DrawHeading(TEXT("NETWORK"), CursorY);

        const AUniverseGameState* NetState = World->GetGameState<AUniverseGameState>();

        DrawRow(TEXT("Role"),
            (World->GetNetMode() == NM_Client) ? TEXT("client")
                : (World->GetNetMode() == NM_DedicatedServer) ? TEXT("dedicated server")
                : TEXT("listen server"),
            CursorY, ColourValue);

        if (NetState != nullptr)
        {
            DrawRow(TEXT("Handshake"),
                NetState->IsWorldIdentityVerified()
                    ? FString(TEXT("verified"))
                    : FString::Printf(TEXT("NOT VERIFIED - %s"),
                        *NetState->GetIdentityMismatchReason()),
                CursorY,
                NetState->IsWorldIdentityVerified() ? ColourValue : ColourWarn);

            DrawRow(TEXT("Universe"),
                FString::Printf(TEXT("\"%s\" (0x%016llX)"),
                    *NetState->GetWorldIdentity().SeedText,
                    static_cast<unsigned long long>(NetState->GetWorldIdentity().SeedValue)),
                CursorY, ColourValue);

            DrawRow(TEXT("Shared clock"),
                FString::Printf(TEXT("%.1f s   drift %+.3f s"),
                    NetState->GetUniverseTimeSeconds(), NetState->GetClockDriftSeconds()),
                CursorY, ColourValue);
        }

        // Bandwidth and ping, from whichever connection this machine has.
        if (UNetDriver* Driver = World->GetNetDriver())
        {
            if (UNetConnection* ToServer = Driver->ServerConnection)
            {
                DrawRow(TEXT("Connection"),
                    FString::Printf(TEXT("ping %.0f ms   in %.1f kB/s   out %.1f kB/s"),
                        ToServer->AvgLag * 1000.0f,
                        ToServer->InBytesPerSecond / 1024.0f,
                        ToServer->OutBytesPerSecond / 1024.0f),
                    CursorY, ColourValue);
            }
            else
            {
                DrawRow(TEXT("Clients"),
                    FString::Printf(TEXT("%d connected"), Driver->ClientConnections.Num()),
                    CursorY, ColourValue);
            }
        }

        if (const UUniverseNetSubsystem* Net = World->GetSubsystem<UUniverseNetSubsystem>())
        {
            DrawRow(TEXT("Other players"),
                FString::Printf(TEXT("%d in system, %d in region, %d visible"),
                    Net->CountAtLeast(ENetRelevanceClass::SameSystem),
                    Net->CountAtLeast(ENetRelevanceClass::SameRegion),
                    Net->CountAtLeast(ENetRelevanceClass::Visible)),
                CursorY, ColourValue);

            for (const FRemotePlayerSnapshot& Snapshot : Net->GetRemotePlayers())
            {
                DrawRow(FString::Printf(TEXT("  %s"), *Snapshot.PlayerId),
                    FString::Printf(TEXT("%-12s %s"),
                        LexToString(Snapshot.Relevance),
                        Snapshot.bOnFoot ? TEXT("on foot") : TEXT("in a ship")),
                    CursorY, ColourValue);
            }
        }

        if (const AUniversePlayerController* NetController =
                Cast<AUniversePlayerController>(World->GetFirstPlayerController()))
        {
            // Corrections are the number worth watching. A client that is being
            // corrected is a client whose simulation has diverged from what the
            // server will accept, and a count that climbs steadily means the
            // validation is too tight rather than that the player is cheating.
            DrawRow(TEXT("Corrections"),
                FString::Printf(TEXT("%d of %d moves%s"),
                    NetController->GetCorrectionCount(),
                    NetController->GetSentMoveCount(),
                    NetController->GetLastCorrectionReason().IsEmpty()
                        ? TEXT("")
                        : *FString::Printf(TEXT("   last: %s"),
                            *NetController->GetLastCorrectionReason())),
                CursorY,
                (NetController->GetCorrectionCount() > 0) ? ColourWarn : ColourValue);
        }
    }

    // --- Galaxy (Sprint 006 section 42) ------------------------------------
    if (Subsystem != nullptr && Probe != nullptr)
    {
        DrawHeading(TEXT("GALAXY"), CursorY);

        const FUniversePosition Here = Probe->GetUniversePosition();

        FGalaxyDescriptor Galaxy;

        if (FGalaxyGenerator::FindGalaxyAt(Subsystem->GetSeedHierarchy(), Here, Galaxy))
        {
            const FGalaxyLocalPosition Local = FGalaxyGenerator::ToGalaxyLocal(Galaxy, Here);

            DrawRow(TEXT("Galaxy"),
                FString::Printf(TEXT("%s   %s   r=%.0f ly"),
                    *Galaxy.Name, LexToString(Galaxy.Type), Galaxy.RadiusLightYears),
                CursorY, ColourValue);

            DrawRow(TEXT("Galactic radius"),
                FString::Printf(TEXT("%.0f ly   (%.0f%% out)"),
                    Local.RadiusLightYears,
                    100.0 * Local.RadiusLightYears / FMath::Max(Galaxy.RadiusLightYears, 1.0)),
                CursorY, ColourValue);

            DrawRow(TEXT("Above disk"),
                FString::Printf(TEXT("%+.0f ly   (half-thickness %.0f)"),
                    Local.HeightLightYears, Galaxy.DiskThicknessLightYears),
                CursorY, ColourValue);

            DrawRow(TEXT("Stellar density"),
                FString::Printf(TEXT("%.4f of core"),
                    FGalaxyGenerator::GetStellarDensityAt(Galaxy, Local)),
                CursorY, ColourValue);
        }
        else
        {
            DrawRow(TEXT("Galaxy"), TEXT("intergalactic space - no stars here at all"),
                CursorY, ColourWarn);
        }
    }

    // --- Streaming (Sprint 006 sections 41 and 46) -------------------------
    if (const UStarSystemStreamingSubsystem* Streamer =
            World->GetSubsystem<UStarSystemStreamingSubsystem>())
    {
        DrawHeading(TEXT("SYSTEM STREAMING"), CursorY);

        TArray<int32> Counts;
        Streamer->GetStateCounts(Counts);

        DrawRow(TEXT("Tracked"),
            FString::Printf(TEXT("%d systems   %d generated   %d transitions"),
                Streamer->GetTrackedSystems().Num(),
                Streamer->GetGeneratedSystemCount(),
                Streamer->GetTransitionCount()),
            CursorY, ColourValue);

        DrawRow(TEXT("States"),
            FString::Printf(TEXT("descriptor %d   distant %d   nearby %d   prewarm %d   active %d"),
                Counts.IsValidIndex(1) ? Counts[1] : 0,
                Counts.IsValidIndex(2) ? Counts[2] : 0,
                Counts.IsValidIndex(3) ? Counts[3] : 0,
                Counts.IsValidIndex(4) ? Counts[4] : 0,
                Counts.IsValidIndex(5) ? Counts[5] : 0),
            CursorY, ColourValue);

        int32 Detected = 0;
        int32 Visited = 0;
        Streamer->GetDiscoveryCounts(Detected, Visited);

        DrawRow(TEXT("Discovery"),
            FString::Printf(TEXT("%d detected, %d visited"), Detected, Visited),
            CursorY, ColourValue);

        // The nearest few, which is the whole starmap this sprint needs.
        const TArray<FStreamedSystem>& Nearby = Streamer->GetTrackedSystems();

        for (int32 Index = 0; Index < Nearby.Num() && Index < 5; ++Index)
        {
            const FStreamedSystem& System = Nearby[Index];

            DrawRow(FString::Printf(TEXT("  [%d]"), Index),
                FString::Printf(TEXT("%-20s %8.4f ly   %s%s"),
                    System.bHasDescriptor ? *System.Descriptor.Name : TEXT("(ungenerated)"),
                    System.DistanceLightYears,
                    LexToString(System.State),
                    (System.Id == Streamer->GetTargetId()) ? TEXT("   <- TARGET") : TEXT("")),
                CursorY,
                (System.Id == Streamer->GetTargetId()) ? ColourAccent : ColourValue);
        }
    }

    // --- Astronomy ---------------------------------------------------------
    DrawHeading(TEXT("ASTRONOMY"), CursorY);

    const AUniverseGameMode* GameMode = World->GetAuthGameMode<AUniverseGameMode>();
    FStarSystemDescriptor System;

    if (GameMode != nullptr && GameMode->GetActiveSystem(System))
    {
        DrawRow(TEXT("Current system"),
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
        DrawRow(TEXT("Current system"), TEXT("interstellar space"), CursorY, ColourWarn);
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

    // --- Persistence --------------------------------------------------------
    //
    // Last, because it is the only section that describes what the player has
    // *done* rather than what the universe is.
    if (UWorldStateSubsystem* WorldState = World->GetSubsystem<UWorldStateSubsystem>())
    {
        DrawHeading(TEXT("WORLD STATE"), CursorY);

        const FWorldSaveMetadata& Metadata = WorldState->GetMetadata();

        DrawRow(TEXT("Save"),
            FString::Printf(TEXT("%s   \"%s\"   schema v%d   terrain v%u   env v%u"),
                WorldState->IsOpen() ? TEXT("open") : TEXT("CLOSED"),
                *Metadata.UniverseSeedText, Metadata.SchemaVersion,
                Metadata.TerrainVersion, Metadata.EnvironmentVersion),
            CursorY,
            WorldState->IsOpen() ? ColourValue : ColourWarn);

        DrawRow(TEXT("Storage"),
            FString::Printf(TEXT("%lld records   %.1f KB   read %.2f ms   write %.2f ms"),
                static_cast<long long>(WorldState->GetStorageRecordCount()),
                WorldState->GetStorageSizeBytes() / 1024.0,
                WorldState->GetLastReadMilliseconds(),
                WorldState->GetLastWriteMilliseconds()),
            CursorY, ColourValue);

        DrawRow(TEXT("Regions"),
            FString::Printf(TEXT("%d cached   %d loading   %d created   %d removed"),
                WorldState->GetLoadedRegionCount(),
                WorldState->GetPendingLoadCount(),
                WorldState->GetCreatedEntityCount(),
                WorldState->GetRemovedEntityCount()),
            CursorY, ColourValue);

        // The region the player is standing in - the one a build or a chop
        // would write to.
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

                const FPersistenceRegionId Region = FPersistenceRegionId::FromPlanetLocal(
                    Planet->GetPlanetDescriptor(),
                    Planet->UniverseToPlanetLocalMeters(Observer));

                const FWorldRegionDelta* Delta = WorldState->FindLoadedRegion(Region);

                DrawRow(TEXT("Here"),
                    FString::Printf(TEXT("%s   %.0f m across   %s"),
                        *Region.ToString(),
                        Region.GetSizeMeters(Planet->GetPlanetDescriptor().RadiusMeters),
                        Delta != nullptr
                            ? *FString::Printf(TEXT("%d created, %d removed"),
                                Delta->Created.Num(), Delta->Removed.Num())
                            : TEXT("(not loaded)")),
                    CursorY, ColourAccent);

                if (const UPlanetStructureComponent* Structures = Planet->GetStructureComponent())
                {
                    DrawRow(TEXT("Structures"),
                        FString::Printf(TEXT("%d with a runtime representation nearby"),
                            Structures->GetLiveCount()),
                        CursorY, ColourValue);
                }
            }
        }

        if (WorldState->HasGenerationVersionMismatch())
        {
            DrawRow(TEXT("WARNING"),
                TEXT("Generation version mismatch - structures may not sit on their ground"),
                CursorY, ColourWarn);
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
    DrawRow(TEXT("Warp jump"), TEXT("G = jump forward in whole cells (exact at any distance)"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Warp drive"),
        TEXT("J = engage / disengage,  universe.Warp on|off"), CursorY, ColourLabel);
    DrawRow(TEXT("Navigation"),
        TEXT("universe.Systems,  universe.Target <n|name|ahead>,  universe.FlyTo <target>"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Interstellar"),
        TEXT("universe.GalaxyInfo,  universe.TravelInfo,  universe.InterstellarJourney"),
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
    DrawRow(TEXT("Build / chop"),
        TEXT("universe.Build beacon,  universe.Demolish,  universe.ChopTree"),
        CursorY, ColourLabel);
    DrawRow(TEXT("Persistence"),
        TEXT("universe.PersistenceInfo,  universe.PersistenceReset planet|all"),
        CursorY, ColourLabel);
}
