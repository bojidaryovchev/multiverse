// Copyright Universe Project. All Rights Reserved.

#include "InterstellarJourneySubsystem.h"

#include "InterstellarTravel.h"
#include "StarSystemGenerator.h"
#include "StarSystemStreamingSubsystem.h"
#include "UniverseAnchorComponent.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"
#include "UniverseWorldSubsystem.h"

#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

DEFINE_LOG_CATEGORY_STATIC(LogInterstellarJourney, Log, All);

const TCHAR* LexToString(EInterstellarJourneyStage Stage)
{
    switch (Stage)
    {
    case EInterstellarJourneyStage::Idle:      return TEXT("Idle");
    case EInterstellarJourneyStage::Settling:  return TEXT("Settling");
    case EInterstellarJourneyStage::Departing: return TEXT("Departing");
    case EInterstellarJourneyStage::Cruising:  return TEXT("Cruising");
    case EInterstellarJourneyStage::Arriving:  return TEXT("Arriving");
    case EInterstellarJourneyStage::Returning: return TEXT("Returning");
    case EInterstellarJourneyStage::Verifying: return TEXT("Verifying");
    case EInterstellarJourneyStage::Complete:  return TEXT("Complete");
    case EInterstellarJourneyStage::Failed:    return TEXT("Failed");
    default:                                   return TEXT("?");
    }
}

TStatId UInterstellarJourneySubsystem::GetStatId() const
{
    RETURN_QUICK_DECLARE_CYCLE_STAT(UInterstellarJourneySubsystem, STATGROUP_Tickables);
}

bool UInterstellarJourneySubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
    return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

AUniverseProbePawn* UInterstellarJourneySubsystem::GetProbe() const
{
    return Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(GetWorld(), 0));
}

UStarSystemStreamingSubsystem* UInterstellarJourneySubsystem::GetStreamer() const
{
    UWorld* World = GetWorld();

    return (World != nullptr) ? World->GetSubsystem<UStarSystemStreamingSubsystem>() : nullptr;
}

void UInterstellarJourneySubsystem::BeginJourney(bool bReturnTrip)
{
    bWantReturnTrip = bReturnTrip;

    HomeId = FUniverseSystemId();
    DestinationId = FUniverseSystemId();
    OutboundSeconds = 0.0;
    ReturnSeconds = 0.0;
    PeakSpeedMs = 0.0;
    FailureReason.Reset();

    UE_LOG(LogInterstellarJourney, Log,
        TEXT("=== Interstellar journey: %s ==="),
        bReturnTrip ? TEXT("out and back") : TEXT("one way"));

    // A generous settling deadline. The streamer scans twice a second and the
    // home system has to activate and build terrain before there is anything
    // meaningful to leave.
    EnterStage(EInterstellarJourneyStage::Settling, 30.0);
}

void UInterstellarJourneySubsystem::AbortJourney(const FString& Reason)
{
    if (Stage == EInterstellarJourneyStage::Idle)
    {
        return;
    }

    if (AUniverseProbePawn* Probe = GetProbe())
    {
        Probe->SetAutoSteer(false);
        Probe->SetWarpEngaged(false);
        Probe->FullStop();
    }

    UE_LOG(LogInterstellarJourney, Warning, TEXT("Journey aborted: %s"), *Reason);

    Stage = EInterstellarJourneyStage::Idle;
}

void UInterstellarJourneySubsystem::EnterStage(
    EInterstellarJourneyStage NewStage, double DeadlineSeconds)
{
    Stage = NewStage;
    TimeInStageSeconds = 0.0;
    StageDeadlineSeconds = DeadlineSeconds;
    LastProgressLogSeconds = -1000.0;

    UE_LOG(LogInterstellarJourney, Log,
        TEXT("Stage: %s (deadline %.0f s)"), LexToString(NewStage), DeadlineSeconds);
}

void UInterstellarJourneySubsystem::Fail(const FString& Reason)
{
    FailureReason = Reason;

    UE_LOG(LogInterstellarJourney, Error, TEXT("FAILED: %s"), *Reason);

    if (AUniverseProbePawn* Probe = GetProbe())
    {
        Probe->SetAutoSteer(false);
        Probe->SetWarpEngaged(false);
    }

    Stage = EInterstellarJourneyStage::Failed;
    Report();
}

bool UInterstellarJourneySubsystem::StartLegTo(const FUniverseSystemId& Destination)
{
    AUniverseProbePawn* Probe = GetProbe();
    UStarSystemStreamingSubsystem* Streamer = GetStreamer();

    if (Probe == nullptr || Streamer == nullptr)
    {
        return false;
    }

    if (!Streamer->SetTargetSystem(Destination))
    {
        return false;
    }

    Probe->SetAutoBrake(true);
    Probe->SetAutoSteer(true);

    return Probe->SetWarpEngaged(true);
}

void UInterstellarJourneySubsystem::Tick(float DeltaTime)
{
    Super::Tick(DeltaTime);

    if (Stage == EInterstellarJourneyStage::Idle
        || Stage == EInterstellarJourneyStage::Complete
        || Stage == EInterstellarJourneyStage::Failed)
    {
        return;
    }

    const double Dt = static_cast<double>(DeltaTime);
    TimeInStageSeconds += Dt;

    AUniverseProbePawn* Probe = GetProbe();
    UStarSystemStreamingSubsystem* Streamer = GetStreamer();
    const UUniverseWorldSubsystem* Universe =
        (GetWorld() != nullptr) ? GetWorld()->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;

    if (Probe == nullptr || Streamer == nullptr || Universe == nullptr)
    {
        Fail(TEXT("The probe or a subsystem went away mid-journey."));
        return;
    }

    PeakSpeedMs = FMath::Max(PeakSpeedMs, Probe->GetSpeedMetersPerSecond());

    if (StageDeadlineSeconds > 0.0 && TimeInStageSeconds > StageDeadlineSeconds)
    {
        Fail(FString::Printf(
            TEXT("Stage %s exceeded its %.0f s deadline. Speed %s, %s to target."),
            LexToString(Stage), StageDeadlineSeconds,
            *FInterstellarTravel::FormatSpeed(Probe->GetSpeedMetersPerSecond()),
            *FInterstellarTravel::FormatDistance(Probe->GetDistanceToTargetMeters())));
        return;
    }

    switch (Stage)
    {
    case EInterstellarJourneyStage::Settling:
    {
        FStarSystemDescriptor Active;

        if (!Streamer->GetActiveSystem(Active))
        {
            return;
        }

        HomeId = Active.Id;
        HomeName = Active.Name;
        HomeContentHash = Active.GetContentHash();
        HomePosition = Active.Position;

        HazardStopsAtStart = Probe->GetHazardStopCount();

        UE_LOG(LogInterstellarJourney, Log,
            TEXT("Home: %s  %s  content 0x%016llX"),
            *HomeName, *HomeId.ToDebugString(),
            static_cast<unsigned long long>(HomeContentHash));

        EnterStage(EInterstellarJourneyStage::Departing, 20.0);
        return;
    }

    case EInterstellarJourneyStage::Departing:
    {
        // The nearest system that is not home. Nearest rather than a fixed
        // choice so the run works under any seed, and not-home because
        // "travel to where you already are" proves nothing.
        const TArray<FStreamedSystem>& Systems = Streamer->GetTrackedSystems();

        const FStreamedSystem* Chosen = nullptr;

        for (const FStreamedSystem& Candidate : Systems)
        {
            if (!(Candidate.Id == HomeId) && Candidate.bHasDescriptor)
            {
                Chosen = &Candidate;
                break;
            }
        }

        if (Chosen == nullptr)
        {
            // Not a failure yet: the streamer generates descriptors as systems
            // come into visual range, so the first few frames legitimately have
            // none. The stage deadline is what turns this into a failure.
            return;
        }

        DestinationId = Chosen->Id;
        DestinationName = Chosen->Descriptor.Name;
        DestinationContentHashBefore = Chosen->Descriptor.GetContentHash();
        DestinationPosition = Chosen->Position;
        LegDistanceLightYears = Chosen->DistanceLightYears;

        UE_LOG(LogInterstellarJourney, Log,
            TEXT("Destination: %s  %.4f ly  content 0x%016llX"),
            *DestinationName, LegDistanceLightYears,
            static_cast<unsigned long long>(DestinationContentHashBefore));

        if (!StartLegTo(DestinationId))
        {
            Fail(TEXT("Could not engage warp for the outbound leg."));
            return;
        }

        OutboundSeconds = 0.0;

        // Deadline from the travel layer's own estimate, with a wide margin.
        // Taking the estimate rather than a constant is what makes this run
        // work at any speed profile, and it doubles as a check on the estimate:
        // one that is wrong by more than 4x fails the run.
        const double Estimate = FInterstellarTravel::EstimateTravelTimeSeconds(
            Probe->TravelProfile,
            LegDistanceLightYears * UniverseScale::MetersPerLightYear,
            EUniverseTravelMode::Warp);

        EnterStage(EInterstellarJourneyStage::Cruising, FMath::Max(Estimate * 4.0, 30.0));
        return;
    }

    case EInterstellarJourneyStage::Cruising:
    {
        OutboundSeconds += Dt;

        if (Probe->IsWarpEngaged())
        {
            LogProgress(*Probe, OutboundSeconds, TEXT("outbound"));
            return;
        }

        // Warp disengaged on its own, which the travel layer only does on
        // arrival.
        UE_LOG(LogInterstellarJourney, Log,
            TEXT("Outbound leg complete in %.1f s. Peak speed %s."),
            OutboundSeconds, *FInterstellarTravel::FormatSpeed(PeakSpeedMs));

        EnterStage(EInterstellarJourneyStage::Arriving, 30.0);
        return;
    }

    case EInterstellarJourneyStage::Arriving:
    {
        FStarSystemDescriptor Active;

        if (!Streamer->GetActiveSystem(Active))
        {
            // Still streaming in. The deadline covers the case where it never
            // does, which is the failure worth catching: arriving in black
            // space and waiting.
            return;
        }

        if (!(Active.Id == DestinationId))
        {
            Fail(FString::Printf(
                TEXT("Arrived at %s, expected %s."), *Active.Name, *DestinationName));
            return;
        }

        if (Active.GetContentHash() != DestinationContentHashBefore)
        {
            Fail(FString::Printf(
                TEXT("%s regenerated differently on arrival: 0x%016llX, was 0x%016llX."),
                *DestinationName,
                static_cast<unsigned long long>(Active.GetContentHash()),
                static_cast<unsigned long long>(DestinationContentHashBefore)));
            return;
        }

        const double DistanceMeters = FUniversePosition::DistanceMeters(
            Probe->GetUniversePosition(), DestinationPosition);

        UE_LOG(LogInterstellarJourney, Log,
            TEXT("Arrived at %s, %s from its star. Content hash matches the prediction "
                 "made before departure."),
            *DestinationName, *FInterstellarTravel::FormatDistance(DistanceMeters));

        if (Streamer->GetActivePlanetActor() == nullptr)
        {
            Fail(TEXT("Arrived, but no streaming planet was built for the destination."));
            return;
        }

        if (!bWantReturnTrip)
        {
            Stage = EInterstellarJourneyStage::Complete;
            Report();
            return;
        }

        if (!StartLegTo(HomeId))
        {
            Fail(TEXT("Could not engage warp for the return leg. Is home still tracked?"));
            return;
        }

        ReturnSeconds = 0.0;

        const double Estimate = FInterstellarTravel::EstimateTravelTimeSeconds(
            Probe->TravelProfile,
            LegDistanceLightYears * UniverseScale::MetersPerLightYear,
            EUniverseTravelMode::Warp);

        EnterStage(EInterstellarJourneyStage::Returning, FMath::Max(Estimate * 4.0, 30.0));
        return;
    }

    case EInterstellarJourneyStage::Returning:
    {
        ReturnSeconds += Dt;

        if (Probe->IsWarpEngaged())
        {
            LogProgress(*Probe, ReturnSeconds, TEXT("return"));
            return;
        }

        EnterStage(EInterstellarJourneyStage::Verifying, 30.0);
        return;
    }

    case EInterstellarJourneyStage::Verifying:
    {
        FStarSystemDescriptor Active;

        if (!Streamer->GetActiveSystem(Active))
        {
            return;
        }

        if (!(Active.Id == HomeId))
        {
            Fail(FString::Printf(
                TEXT("Returned to %s, expected %s."), *Active.Name, *HomeName));
            return;
        }

        // The assertion the whole run exists for. Every actor in this system
        // was destroyed when the player left and rebuilt on the way back; if
        // regeneration depended on anything but the address and the seed, the
        // hash would differ here and nowhere else.
        if (Active.GetContentHash() != HomeContentHash)
        {
            Fail(FString::Printf(
                TEXT("%s regenerated differently after the round trip: 0x%016llX, was 0x%016llX. ")
                TEXT("Procedural generation is order-dependent."),
                *HomeName,
                static_cast<unsigned long long>(Active.GetContentHash()),
                static_cast<unsigned long long>(HomeContentHash)));
            return;
        }

        Stage = EInterstellarJourneyStage::Complete;
        Report();
        return;
    }

    default:
        return;
    }
}

void UInterstellarJourneySubsystem::LogProgress(
    const AUniverseProbePawn& Probe, double ElapsedSeconds, const TCHAR* LegName)
{
    // A leg takes minutes, and a run that prints nothing for two of them is
    // indistinguishable from one that has silently stopped moving. Ten seconds
    // is often enough to see progress and rare enough not to bury the log.
    if (ElapsedSeconds - LastProgressLogSeconds < 10.0)
    {
        return;
    }

    LastProgressLogSeconds = ElapsedSeconds;

    UE_LOG(LogInterstellarJourney, Log,
        TEXT("  %s %5.0f s: %s to go, %s%s"),
        LegName, ElapsedSeconds,
        *FInterstellarTravel::FormatDistance(Probe.GetDistanceToTargetMeters()),
        *FInterstellarTravel::FormatSpeed(Probe.GetSpeedMetersPerSecond()),
        Probe.IsBraking() ? TEXT("  BRAKING") : TEXT(""));
}

void UInterstellarJourneySubsystem::Report() const
{
    const AUniverseProbePawn* Probe = GetProbe();
    const UStarSystemStreamingSubsystem* Streamer = GetStreamer();

    UE_LOG(LogInterstellarJourney, Log, TEXT("=== Interstellar journey report ==="));
    UE_LOG(LogInterstellarJourney, Log, TEXT("  Result        : %s"),
        (Stage == EInterstellarJourneyStage::Complete) ? TEXT("PASS") : TEXT("FAIL"));

    if (!FailureReason.IsEmpty())
    {
        UE_LOG(LogInterstellarJourney, Log, TEXT("  Reason        : %s"), *FailureReason);
    }

    UE_LOG(LogInterstellarJourney, Log, TEXT("  Home          : %s (0x%016llX)"),
        *HomeName, static_cast<unsigned long long>(HomeContentHash));
    UE_LOG(LogInterstellarJourney, Log, TEXT("  Destination   : %s (0x%016llX)"),
        *DestinationName, static_cast<unsigned long long>(DestinationContentHashBefore));
    UE_LOG(LogInterstellarJourney, Log, TEXT("  Leg distance  : %.4f ly"), LegDistanceLightYears);
    UE_LOG(LogInterstellarJourney, Log, TEXT("  Outbound      : %.1f s"), OutboundSeconds);

    if (bWantReturnTrip)
    {
        UE_LOG(LogInterstellarJourney, Log, TEXT("  Return        : %.1f s"), ReturnSeconds);
    }

    UE_LOG(LogInterstellarJourney, Log, TEXT("  Peak speed    : %s"),
        *FInterstellarTravel::FormatSpeed(PeakSpeedMs));

    if (Probe != nullptr)
    {
        UE_LOG(LogInterstellarJourney, Log, TEXT("  Odometer      : %.4f ly"),
            Probe->GetOdometerLightYears());
        UE_LOG(LogInterstellarJourney, Log, TEXT("  Hazard stops  : %d during the run"),
            Probe->GetHazardStopCount() - HazardStopsAtStart);
    }

    if (Streamer != nullptr)
    {
        int32 Detected = 0;
        int32 Visited = 0;
        Streamer->GetDiscoveryCounts(Detected, Visited);

        UE_LOG(LogInterstellarJourney, Log,
            TEXT("  Streaming     : %d tracked, %d generated, %d transitions"),
            Streamer->GetTrackedSystems().Num(),
            Streamer->GetGeneratedSystemCount(),
            Streamer->GetTransitionCount());

        UE_LOG(LogInterstellarJourney, Log,
            TEXT("  Discovery     : %d detected, %d visited"), Detected, Visited);
    }
}

// ---------------------------------------------------------------------------
// Console entry point
// ---------------------------------------------------------------------------

static void UniverseInterstellarJourneyCommand(const TArray<FString>& Args, UWorld* World)
{
    UInterstellarJourneySubsystem* Journey =
        (World != nullptr) ? World->GetSubsystem<UInterstellarJourneySubsystem>() : nullptr;

    if (Journey == nullptr)
    {
        UE_LOG(LogInterstellarJourney, Error, TEXT("No journey subsystem."));
        return;
    }

    if (Args.Num() > 0 && Args[0].Equals(TEXT("abort"), ESearchCase::IgnoreCase))
    {
        Journey->AbortJourney(TEXT("requested from the console"));
        return;
    }

    const bool bReturnTrip = (Args.Num() == 0)
        || !Args[0].Equals(TEXT("oneway"), ESearchCase::IgnoreCase);

    Journey->BeginJourney(bReturnTrip);
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseInterstellarJourneyCommand(
    TEXT("universe.InterstellarJourney"),
    TEXT("Runs the scripted interstellar acceptance journey and verifies it. ")
    TEXT("Arguments: oneway to skip the return leg, abort to stop."),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseInterstellarJourneyCommand));
