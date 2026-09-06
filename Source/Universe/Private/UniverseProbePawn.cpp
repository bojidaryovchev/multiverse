// Copyright Universe Project. All Rights Reserved.

#include "UniverseProbePawn.h"
#include "UniverseAnchorComponent.h"
#include "UniverseWorldSubsystem.h"
#include "PlanetActor.h"
#include "PlanetCharacter.h"
#include "PlanetTrajectory.h"
#include "PlanetSurfaceQuery.h"
#include "UniverseGameMode.h"
#include "PlanetActor.h"
#include "PlanetTerrainComponent.h"
#include "UniverseScale.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Engine.h"
#include "UnrealClient.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/ConstructorHelpers.h"

DEFINE_LOG_CATEGORY_STATIC(LogUniverseProbe, Log, All);

namespace
{
    /** Speed of light, m/s. */
    constexpr double SpeedOfLightMs = 299792458.0;

    /**
     * Debug autopilot: holds full forward thrust with nobody at the controls.
     *
     * This is the Phase F traversal harness. Proving that the coordinate
     * architecture survives crossing millions of cells needs a long, repeatable
     * run, and a human holding W is neither. With this and a periodic state
     * log, the whole proof runs headless and its evidence lands in the log:
     *
     *   UnrealEditor.exe <project> -game -benchmark -benchmarkseconds=60
     *     -ExecCmds="universe.AutoPilot 1, universe.LogStateInterval 2"
     */
    static TAutoConsoleVariable<int32> CVarAutoPilot(
        TEXT("universe.AutoPilot"),
        0,
        TEXT("Debug: hold full forward thrust every frame (0 = off, 1 = on)."),
        ECVF_Cheat);

    static TAutoConsoleVariable<int32> CVarAutoPilotTier(
        TEXT("universe.AutoPilotTier"),
        8,
        TEXT("Thrust tier the autopilot forces (each tier is 10x acceleration)."),
        ECVF_Cheat);

    /**
     * Seconds to wait before taking a screenshot (0 = off), then self-disables.
     *
     * HighResShot fires the moment it is executed, which for a -ExecCmds run is
     * before any terrain has streamed in - so a capture of the planet always
     * came out empty. Waiting a few seconds first is the difference between a
     * screenshot that proves something and one that proves the streamer had not
     * started yet.
     */
    static TAutoConsoleVariable<float> CVarScreenshotAfterSeconds(
        TEXT("universe.ScreenshotAfterSeconds"),
        0.0f,
        TEXT("Take a screenshot this many seconds from now, once streaming has settled (0 = off)."),
        ECVF_Cheat);

    static TAutoConsoleVariable<float> CVarStateLogInterval(
        TEXT("universe.LogStateInterval"),
        0.0f,
        TEXT("Seconds between periodic probe-state log lines (0 = off)."),
        ECVF_Cheat);
}

/**
 * universe.WarpJump <lightyears>
 *
 * Drives the whole-cell jump path from the console, so the exact-at-any-
 * distance branch can be exercised without a key press - and therefore
 * scripted into a headless validation run.
 */
static void UniverseWarpJumpCommand(const TArray<FString>& Args, UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));
    if (Probe == nullptr)
    {
        UE_LOG(LogUniverseProbe, Warning, TEXT("universe.WarpJump: no probe pawn."));
        return;
    }

    // Default to the probe's configured jump distance when no argument is given.
    double LightYears = Probe->WarpJumpLightYears;
    if (Args.Num() > 0)
    {
        LightYears = FCString::Atod(*Args[0]);
    }

    Probe->WarpJump(LightYears);
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseWarpJumpCommand(
    TEXT("universe.WarpJump"),
    TEXT("Debug: jump the probe forward by N light years using whole-cell arithmetic. e.g. universe.WarpJump 250"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseWarpJumpCommand));

/**
 * universe.GotoAltitude <metres>
 *
 * Places the probe directly above the streaming planet at a given altitude,
 * looking down.
 *
 * Sprint 002 asks for a way to jump to useful debug altitudes so LOD and
 * streaming can be tested repeatedly. Flying down from orbit by hand each time
 * makes a five-second check into a two-minute one, and worse, makes it
 * impossible to compare two runs - a teleport lands on exactly the same
 * viewpoint every time, so patch counts and timings are directly comparable.
 */
static void UniverseGotoAltitudeCommand(const TArray<FString>& Args, UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));
    const AUniverseGameMode* GameMode = World->GetAuthGameMode<AUniverseGameMode>();
    if (Probe == nullptr || GameMode == nullptr)
    {
        UE_LOG(LogUniverseProbe, Warning, TEXT("universe.GotoAltitude: no probe or game mode."));
        return;
    }

    const APlanetActor* Planet = GameMode->GetPlanetActor();
    if (Planet == nullptr)
    {
        UE_LOG(LogUniverseProbe, Warning, TEXT("universe.GotoAltitude: no streaming planet in this world."));
        return;
    }

    double AltitudeMeters = 100000.0;
    if (Args.Num() > 0)
    {
        AltitudeMeters = FCString::Atod(*Args[0]);
    }

    const FPlanetSurfaceDescriptor& Surface = Planet->GetPlanetDescriptor();

    // Keep the probe over the same patch of ground when only the altitude
    // changes, so repeated jumps compare like with like. Only when the probe is
    // effectively at the planet centre is an arbitrary direction chosen.
    FVector3d Direction(0.0, 0.0, 1.0);
    {
        const FVector3d Local = Planet->UniverseToPlanetLocalMeters(Probe->GetUniversePosition());
        const double Length = Local.Size();
        if (Length > 1.0)
        {
            Direction = FVector3d(Local.X / Length, Local.Y / Length, Local.Z / Length);
        }
    }

    const double TargetRadius = Surface.RadiusMeters + AltitudeMeters;
    const FVector3d TargetLocalMeters(
        Direction.X * TargetRadius, Direction.Y * TargetRadius, Direction.Z * TargetRadius);

    const FVector3d OffsetCm(
        TargetLocalMeters.X * UniverseScale::CmPerMeter,
        TargetLocalMeters.Y * UniverseScale::CmPerMeter,
        TargetLocalMeters.Z * UniverseScale::CmPerMeter);

    const FUniversePosition Target = Surface.Position.OffsetByCm(OffsetCm);

    if (UUniverseAnchorComponent* Anchor = Probe->GetAnchor())
    {
        Anchor->SetUniversePosition(Target);
    }

    // Look straight down, and stop dead - arriving at 0.3c would immediately
    // undo the jump.
    Probe->FullStop();
    Probe->SetActorRotation(FVector(-Direction.X, -Direction.Y, -Direction.Z).Rotation());

    if (UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        Subsystem->SetRenderOrigin(Target);
    }

    UE_LOG(LogUniverseProbe, Log, TEXT("Moved to %.1f m altitude above %s"),
        AltitudeMeters, *Surface.ToDebugString());
}

/**
 * universe.TerrainStress <cycles>
 *
 * Runs the Sprint 002 stress path: orbit, rapid descent, surface traverse,
 * cube-face crossing, rapid ascent, opposite side, repeat.
 *
 * Scripted rather than flown by hand, for the same reason GotoAltitude exists:
 * a hand-flown stress test cannot be repeated identically, so two runs cannot
 * be compared and a slow leak is invisible. This walks a fixed sequence of
 * viewpoints so patch counts, pool sizes and task queues can be read at the
 * same points on every pass, which is what makes "converges to a bounded
 * range" a checkable statement rather than an impression.
 *
 * Deliberately teleports rather than flying: the streamer's worst case is an
 * observer that arrives somewhere new instantly, with every in-flight
 * generation immediately stale. Flying there smoothly would be the easy case.
 */
static void UniverseTerrainStressCommand(const TArray<FString>& Args, UWorld* World)
{
    if (World == nullptr)
    {
        return;
    }

    AUniverseProbePawn* Probe = Cast<AUniverseProbePawn>(UGameplayStatics::GetPlayerPawn(World, 0));
    if (Probe == nullptr)
    {
        return;
    }

    int32 Cycles = 4;
    if (Args.Num() > 0)
    {
        Cycles = FMath::Clamp(FCString::Atoi(*Args[0]), 1, 64);
    }

    Probe->BeginTerrainStress(Cycles);
}

static FAutoConsoleCommandWithWorldAndArgs GUniverseTerrainStressCommand(
    TEXT("universe.TerrainStress"),
    TEXT("Debug: run N cycles of the orbit/descend/traverse/ascend stress path. e.g. universe.TerrainStress 4"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseTerrainStressCommand));

static FAutoConsoleCommandWithWorldAndArgs GUniverseGotoAltitudeCommand(
    TEXT("universe.GotoAltitude"),
    TEXT("Debug: place the probe at N metres altitude above the streaming planet, looking down. e.g. universe.GotoAltitude 1000"),
    FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&UniverseGotoAltitudeCommand));

AUniverseProbePawn::AUniverseProbePawn()
{
    PrimaryActorTick.bCanEverTick = true;
    // Integrate after input has been gathered for the frame.
    PrimaryActorTick.TickGroup = TG_PrePhysics;

    RootScene = CreateDefaultSubobject<USceneComponent>(TEXT("RootScene"));
    SetRootComponent(RootScene);

    HullMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HullMesh"));
    HullMesh->SetupAttachment(RootScene);
    HullMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HullMesh->SetCastShadow(false);

    // Engine primitive, so the project needs no committed binary content.
    static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeMesh(
        TEXT("/Engine/BasicShapes/Cone.Cone"));
    if (ConeMesh.Succeeded())
    {
        HullMesh->SetStaticMesh(ConeMesh.Object);
        // Point the cone along +X (the actor's forward axis) and make it small
        // enough not to fill the view.
        HullMesh->SetRelativeRotation(FRotator(-90.0, 0.0, 0.0));
        HullMesh->SetRelativeScale3D(FVector(0.3, 0.3, 0.6));
        HullMesh->SetRelativeLocation(FVector(120.0, 0.0, 0.0));
    }

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(RootScene);
    Camera->SetRelativeLocation(FVector(-400.0, 0.0, 120.0));
    // A very long far plane: even in scaled space, "distant" means distant.
    Camera->SetFieldOfView(90.0f);

    Anchor = CreateDefaultSubobject<UUniverseAnchorComponent>(TEXT("UniverseAnchor"));
    // The probe is something the player is inside: it lives at true 1:1 scale.
    Anchor->RenderSpace = EUniverseRenderSpace::Local;

    // Deliberately no AutoPossessPlayer: AUniverseGameMode names this class as
    // DefaultPawnClass and the standard spawn/possess path handles it. Setting
    // both would have the pawn try to claim player 0 a second time.

    // The probe steers by rotating the actor itself, so the controller's
    // rotation must not drive it - otherwise roll is impossible and pitch is
    // clamped, neither of which suits a spacecraft with no local "up".
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = false;
    bUseControllerRotationRoll = false;
}

FUniversePosition AUniverseProbePawn::GetUniversePosition() const
{
    return Anchor != nullptr ? Anchor->GetUniversePosition() : FUniversePosition();
}

double AUniverseProbePawn::GetSpeedInC() const
{
    return VelocityMetersPerSecond.Size() / SpeedOfLightMs;
}

double AUniverseProbePawn::GetSpeedMultiplier() const
{
    return FMath::Pow(10.0, static_cast<double>(SpeedTier));
}

FString AUniverseProbePawn::GetSpeedTierLabel() const
{
    // Named bands rather than a bare exponent: "interstellar" tells the reader
    // what the tier is for, which a raw 1e11 does not.
    if (SpeedTier <= 1)  { return TEXT("manoeuvring"); }
    if (SpeedTier <= 4)  { return TEXT("orbital"); }
    if (SpeedTier <= 7)  { return TEXT("interplanetary"); }
    if (SpeedTier <= 10) { return TEXT("interstellar"); }
    return TEXT("debug hyperspeed");
}

void AUniverseProbePawn::BuildInputAssets()
{
    if (MappingContext != nullptr)
    {
        return;
    }

    MappingContext = NewObject<UInputMappingContext>(this, TEXT("ProbeMappingContext"));

    auto MakeAxisAction = [this](const TCHAR* Name) -> UInputAction*
    {
        UInputAction* Action = NewObject<UInputAction>(this, Name);
        Action->ValueType = EInputActionValueType::Axis1D;
        return Action;
    };

    auto MakeBoolAction = [this](const TCHAR* Name) -> UInputAction*
    {
        UInputAction* Action = NewObject<UInputAction>(this, Name);
        Action->ValueType = EInputActionValueType::Boolean;
        return Action;
    };

    ActionThrust      = MakeAxisAction(TEXT("IA_Thrust"));
    ActionStrafe      = MakeAxisAction(TEXT("IA_Strafe"));
    ActionLift        = MakeAxisAction(TEXT("IA_Lift"));
    ActionPitch       = MakeAxisAction(TEXT("IA_Pitch"));
    ActionYaw         = MakeAxisAction(TEXT("IA_Yaw"));
    ActionRoll        = MakeAxisAction(TEXT("IA_Roll"));
    ActionBrake       = MakeBoolAction(TEXT("IA_Brake"));
    ActionSpeedUp     = MakeBoolAction(TEXT("IA_SpeedUp"));
    ActionSpeedDown   = MakeBoolAction(TEXT("IA_SpeedDown"));
    ActionWarpJump    = MakeBoolAction(TEXT("IA_WarpJump"));
    ActionToggleDebug = MakeBoolAction(TEXT("IA_ToggleDebug"));
    ActionExitShip    = MakeBoolAction(TEXT("IA_ExitShip"));

    // Negated bindings give the opposite direction of an axis without needing
    // a second action.
    auto MapNegated = [this](UInputAction* Action, const FKey& Key)
    {
        FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
        Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
    };

    MappingContext->MapKey(ActionThrust, EKeys::W);
    MapNegated(ActionThrust, EKeys::S);

    MappingContext->MapKey(ActionStrafe, EKeys::D);
    MapNegated(ActionStrafe, EKeys::A);

    MappingContext->MapKey(ActionLift, EKeys::E);
    MapNegated(ActionLift, EKeys::Q);

    MappingContext->MapKey(ActionRoll, EKeys::C);
    MapNegated(ActionRoll, EKeys::Z);

    MappingContext->MapKey(ActionYaw, EKeys::MouseX);
    MappingContext->MapKey(ActionPitch, EKeys::MouseY);

    MappingContext->MapKey(ActionBrake, EKeys::SpaceBar);
    MappingContext->MapKey(ActionSpeedUp, EKeys::MouseScrollUp);
    MappingContext->MapKey(ActionSpeedUp, EKeys::RightBracket);
    MappingContext->MapKey(ActionSpeedDown, EKeys::MouseScrollDown);
    MappingContext->MapKey(ActionSpeedDown, EKeys::LeftBracket);
    MappingContext->MapKey(ActionWarpJump, EKeys::G);
    MappingContext->MapKey(ActionToggleDebug, EKeys::F1);
    MappingContext->MapKey(ActionExitShip, EKeys::F);
}

void AUniverseProbePawn::BeginPlay()
{
    Super::BeginPlay();

    BuildInputAssets();

    UWorld* World = GetWorld();

    // Pull the start pose from the game mode rather than having it pushed.
    // Whether the default pawn exists before or after AGameModeBase::StartPlay
    // depends on the login path, but BuildTestSystem has always run by the time
    // any actor's BeginPlay fires - so pulling is ordering-independent.
    if (Anchor != nullptr && World != nullptr)
    {
        if (const AUniverseGameMode* GameMode = World->GetAuthGameMode<AUniverseGameMode>())
        {
            FUniversePosition StartPosition;
            FUniversePosition LookAt;
            if (GameMode->GetProbeStartPose(StartPosition, LookAt))
            {
                Anchor->SetUniversePosition(StartPosition);

                const FVector3d Facing = FUniversePosition::DirectionUnit(StartPosition, LookAt);
                if (!Facing.IsZero())
                {
                    SetActorRotation(FVector(Facing.X, Facing.Y, Facing.Z).Rotation());
                }
            }
        }
    }

    // Set the tracked anchor after positioning: SetTrackedAnchor re-centres the
    // render origin on it, so doing this first would centre on the origin and
    // leave every actor needlessly far out for a frame.
    if (UUniverseWorldSubsystem* Subsystem = World != nullptr ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr)
    {
        Subsystem->SetTrackedAnchor(Anchor);
    }

    if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
    {
        if (ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
                    LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                InputSubsystem->AddMappingContext(MappingContext, 0);
            }
        }

        // Mouse look without a visible cursor.
        PlayerController->SetShowMouseCursor(false);
        PlayerController->SetInputMode(FInputModeGameOnly());
    }
}

void AUniverseProbePawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    BuildInputAssets();

    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
    if (Input == nullptr)
    {
        UE_LOG(LogUniverseProbe, Error,
            TEXT("Expected an EnhancedInputComponent. Check that DefaultInput.ini selects the ")
            TEXT("Enhanced Input classes; the probe will be uncontrollable otherwise."));
        return;
    }

    Input->BindAction(ActionThrust,      ETriggerEvent::Triggered, this, &AUniverseProbePawn::OnThrust);
    Input->BindAction(ActionStrafe,      ETriggerEvent::Triggered, this, &AUniverseProbePawn::OnStrafe);
    Input->BindAction(ActionLift,        ETriggerEvent::Triggered, this, &AUniverseProbePawn::OnLift);
    Input->BindAction(ActionPitch,       ETriggerEvent::Triggered, this, &AUniverseProbePawn::OnPitch);
    Input->BindAction(ActionYaw,         ETriggerEvent::Triggered, this, &AUniverseProbePawn::OnYaw);
    Input->BindAction(ActionRoll,        ETriggerEvent::Triggered, this, &AUniverseProbePawn::OnRoll);
    Input->BindAction(ActionBrake,       ETriggerEvent::Triggered, this, &AUniverseProbePawn::OnBrake);
    Input->BindAction(ActionSpeedUp,     ETriggerEvent::Started,   this, &AUniverseProbePawn::OnSpeedUp);
    Input->BindAction(ActionSpeedDown,   ETriggerEvent::Started,   this, &AUniverseProbePawn::OnSpeedDown);
    Input->BindAction(ActionWarpJump,    ETriggerEvent::Started,   this, &AUniverseProbePawn::OnWarpJump);
    Input->BindAction(ActionToggleDebug, ETriggerEvent::Started,   this, &AUniverseProbePawn::OnToggleDebug);
    Input->BindAction(ActionExitShip,    ETriggerEvent::Started,   this, &AUniverseProbePawn::OnExitShip);
}

void AUniverseProbePawn::OnThrust(const FInputActionValue& Value) { ThrustInput = Value.Get<float>(); }
void AUniverseProbePawn::OnStrafe(const FInputActionValue& Value) { StrafeInput = Value.Get<float>(); }
void AUniverseProbePawn::OnLift(const FInputActionValue& Value)   { LiftInput   = Value.Get<float>(); }
void AUniverseProbePawn::OnPitch(const FInputActionValue& Value)  { PitchInput += Value.Get<float>(); }
void AUniverseProbePawn::OnYaw(const FInputActionValue& Value)    { YawInput   += Value.Get<float>(); }
void AUniverseProbePawn::OnRoll(const FInputActionValue& Value)   { RollInput   = Value.Get<float>(); }
void AUniverseProbePawn::OnBrake(const FInputActionValue& Value)  { bBraking    = Value.Get<bool>(); }

void AUniverseProbePawn::OnSpeedUp(const FInputActionValue& Value)
{
    SpeedTier = FMath::Min(SpeedTier + 1, MaxSpeedTier);
}

void AUniverseProbePawn::OnSpeedDown(const FInputActionValue& Value)
{
    SpeedTier = FMath::Max(SpeedTier - 1, 0);
}

void AUniverseProbePawn::OnWarpJump(const FInputActionValue& Value)
{
    WarpJump(WarpJumpLightYears);
}

void AUniverseProbePawn::OnToggleDebug(const FInputActionValue& Value)
{
    // Handled by the HUD, which reads the console variable. Kept here so the
    // key binding lives with the rest of the probe's controls.
    if (IConsoleVariable* Variable =
            IConsoleManager::Get().FindConsoleVariable(TEXT("universe.ShowDebug")))
    {
        Variable->Set(Variable->GetInt() != 0 ? 0 : 1);
    }
}

bool AUniverseProbePawn::WarpJump(double LightYears)
{
    if (Anchor == nullptr || LightYears == 0.0)
    {
        return false;
    }

    // Direction from the actor's current facing, converted to universe axes.
    // Universe axes and Unreal axes are the same basis here - the render
    // origin translates but never rotates, which is what keeps this a simple
    // vector copy rather than a frame transform.
    const FVector Forward = GetActorForwardVector();
    const FVector3d Direction = FVector3d(Forward.X, Forward.Y, Forward.Z).GetSafeNormal();
    if (Direction.IsZero())
    {
        return false;
    }

    const double DistanceCm = LightYears * UniverseScale::CmPerLightYear;
    const double CellsToMove = DistanceCm * UniverseScale::InvCellSizeCmD;

    // Whole cells only, so the jump is exact integer arithmetic. The sub-cell
    // remainder is discarded: it is at most one cell (~0.07 AU) out of a jump
    // measured in light years, and keeping it would reintroduce the
    // floating-point error the cell jump exists to avoid.
    const int64 DeltaX = static_cast<int64>(Direction.X * CellsToMove);
    const int64 DeltaY = static_cast<int64>(Direction.Y * CellsToMove);
    const int64 DeltaZ = static_cast<int64>(Direction.Z * CellsToMove);

    FUniversePosition Jumped;
    if (!Anchor->GetUniversePosition().TryOffsetByCells(DeltaX, DeltaY, DeltaZ, Jumped))
    {
        UE_LOG(LogUniverseProbe, Warning,
            TEXT("Warp jump refused: would overflow the cell index (edge of the universe)."));
        return false;
    }

    const double TravelledLy =
        FUniversePosition::DistanceLightYears(Anchor->GetUniversePosition(), Jumped);
    OdometerLightYears += TravelledLy;

    Anchor->SetUniversePosition(Jumped);

    // Rebase immediately so the frame after a jump is already well-conditioned
    // instead of running one frame at an extreme Unreal coordinate.
    if (UUniverseWorldSubsystem* Subsystem = GetWorld() ? GetWorld()->GetSubsystem<UUniverseWorldSubsystem>() : nullptr)
    {
        Subsystem->SetRenderOrigin(Jumped);
    }

    UE_LOG(LogUniverseProbe, Log, TEXT("Warp jump: %.4f ly to %s"),
        TravelledLy, *Jumped.ToCompactString());

    return true;
}

void AUniverseProbePawn::BeginTerrainStress(int32 Cycles)
{
    StressCyclesRemaining = Cycles;
    StressStepIndex = 0;
    TimeSinceStressStep = StressStepSeconds;  // start immediately

    UE_LOG(LogUniverseProbe, Log, TEXT("STRESS begin: %d cycles"), Cycles);
}

void AUniverseProbePawn::AdvanceTerrainStress()
{
    UWorld* World = GetWorld();
    if (World == nullptr || Anchor == nullptr)
    {
        StressCyclesRemaining = 0;
        return;
    }

    const AUniverseGameMode* GameMode = World->GetAuthGameMode<AUniverseGameMode>();
    const APlanetActor* Planet = (GameMode != nullptr) ? GameMode->GetPlanetActor() : nullptr;
    if (Planet == nullptr)
    {
        UE_LOG(LogUniverseProbe, Warning, TEXT("STRESS aborted: no streaming planet."));
        StressCyclesRemaining = 0;
        return;
    }

    const FPlanetSurfaceDescriptor& Surface = Planet->GetPlanetDescriptor();

    // The path. Altitudes span orbit down to near-surface and back, and the
    // directions deliberately land on different cube faces so face seams are
    // crossed rather than avoided.
    struct FStressStep
    {
        double AltitudeMeters;
        FVector3d Direction;
        const TCHAR* Label;
    };

    static const FStressStep Steps[] =
    {
        { 2000000.0, FVector3d( 0.0,  0.0,  1.0), TEXT("high orbit  (+Z face)") },
        {  200000.0, FVector3d( 0.0,  0.0,  1.0), TEXT("low orbit   (+Z face)") },
        {    5000.0, FVector3d( 0.0,  0.0,  1.0), TEXT("near surface(+Z face)") },
        {    5000.0, FVector3d( 0.7,  0.0,  0.7), TEXT("traverse    (+Z/+X seam)") },
        {    5000.0, FVector3d( 1.0,  0.0,  0.0), TEXT("traverse    (+X face)") },
        {    5000.0, FVector3d( 0.7,  0.7,  0.0), TEXT("traverse    (+X/+Y seam)") },
        {  200000.0, FVector3d( 0.0,  1.0,  0.0), TEXT("ascent      (+Y face)") },
        { 2000000.0, FVector3d( 0.0, -1.0,  0.0), TEXT("opposite    (-Y face)") },
        {    5000.0, FVector3d( 0.0,  0.0, -1.0), TEXT("far side    (-Z face)") },
        {  200000.0, FVector3d(-1.0,  0.0,  0.0), TEXT("ascent      (-X face)") },
    };

    constexpr int32 StepCount = static_cast<int32>(sizeof(Steps) / sizeof(Steps[0]));

    const FStressStep& Step = Steps[StressStepIndex % StepCount];

    const FVector3d Direction = Step.Direction.GetSafeNormal();
    const double TargetRadius = Surface.RadiusMeters + Step.AltitudeMeters;

    const FVector3d OffsetCm(
        Direction.X * TargetRadius * UniverseScale::CmPerMeter,
        Direction.Y * TargetRadius * UniverseScale::CmPerMeter,
        Direction.Z * TargetRadius * UniverseScale::CmPerMeter);

    const FUniversePosition Target = Surface.Position.OffsetByCm(OffsetCm);

    Anchor->SetUniversePosition(Target);
    FullStop();
    SetActorRotation(FVector(-Direction.X, -Direction.Y, -Direction.Z).Rotation());

    if (UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>())
    {
        Subsystem->SetRenderOrigin(Target);
    }

    // Report the terrain state reached at the previous viewpoint, which is what
    // shows whether resources are converging or creeping upward.
    if (const UPlanetTerrainComponent* Terrain = Planet->GetTerrainComponent())
    {
        const FPlanetTerrainStats& T = Terrain->GetStats();
        UE_LOG(LogUniverseProbe, Log,
            TEXT("STRESS cycle=%d step=%d %s | visible=%d pooled=%d gen=%d built=%d released=%d discarded=%d tris=%d"),
            StressCyclesRemaining, StressStepIndex % StepCount, Step.Label,
            T.VisiblePatches, T.PooledSlots, T.GeneratingPatches,
            T.TotalGenerated, T.TotalReleased, T.DiscardedResults, T.TriangleCount);
    }

    ++StressStepIndex;
    if ((StressStepIndex % StepCount) == 0)
    {
        --StressCyclesRemaining;
        if (StressCyclesRemaining <= 0)
        {
            UE_LOG(LogUniverseProbe, Log, TEXT("STRESS complete."));
        }
    }
}

FVector3d AUniverseProbePawn::IntegratePlanetaryStep(double Dt)
{
    const UWorld* World = GetWorld();
    const UUniverseWorldSubsystem* Subsystem =
        (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;

    APlanetActor* Planet = (Subsystem != nullptr) ? Subsystem->GetFramePlanet() : nullptr;

    if (Planet == nullptr)
    {
        // Interstellar frame. No gravity, no air, nothing to hit: the step is
        // simply velocity times time, exactly as it was in Sprint 001.
        bLanded = false;
        LastAltitudeAboveTerrainMeters = 0.0;
        LastAtmosphericDepth = 0.0;

        return VelocityMetersPerSecond * Dt;
    }

    const FUniversePosition Position = Anchor->GetUniversePosition();
    const FPlanetSurfaceDescriptor& Descriptor = Planet->GetPlanetDescriptor();

    const FVector3d LocalMeters = Planet->UniverseToPlanetLocalMeters(Position);

    LastAtmosphericDepth =
        FPlanetSurfaceQuery::GetAtmosphericDepthFraction(Descriptor, LocalMeters);

    const FPlanetSurfaceSample Surface = Planet->SampleSurfaceBelow(Position);
    LastAltitudeAboveTerrainMeters = LocalMeters.Size() - Surface.SurfaceRadiusMeters;

    // --- Landed ------------------------------------------------------------
    //
    // A landed ship holds its position exactly. It does not integrate a tiny
    // residual velocity, and it does not re-settle against the collision mesh
    // every frame - both of which produce a craft that slowly slides down a
    // hill over the several minutes a player might spend walking around it,
    // and comes back to find their ship somewhere else.
    if (bLanded)
    {
        const bool bWantsToLift =
            ThrustInput != 0.0 || StrafeInput != 0.0 || LiftInput != 0.0;

        if (!bWantsToLift)
        {
            VelocityMetersPerSecond = FVector3d::ZeroVector;
            return FVector3d::ZeroVector;
        }

        bLanded = false;

        UE_LOG(LogUniverseProbe, Log, TEXT("Lifting off from %s."), *Planet->GetName());
    }

    // --- Gravity -----------------------------------------------------------
    //
    // From the subsystem, so the probe and a character standing beside it are
    // pulled by the same field rather than by two implementations that agree
    // until one is changed.
    const FVector3d Gravity = Subsystem->GetGravityAccelerationMs2(Position);

    VelocityMetersPerSecond += Gravity * Dt;

    // --- Atmospheric drag --------------------------------------------------
    //
    // A single coefficient scaled by the normalised atmospheric depth, not a
    // physical drag model: there is no density, no cross-section and no
    // coefficient of drag to build one from yet. What it has to do is make
    // entering an atmosphere at interplanetary speed impossible, so that
    // reaching the ground means slowing down first, and it does that without
    // claiming to be aerodynamics.
    if (LastAtmosphericDepth > 0.0)
    {
        const double Speed = VelocityMetersPerSecond.Size();

        if (Speed > 0.0)
        {
            constexpr double DragPerSecondAtSeaLevel = 0.6;

            const double Retained = FMath::Max(
                0.0, 1.0 - DragPerSecondAtSeaLevel * LastAtmosphericDepth * Dt);

            VelocityMetersPerSecond *= Retained;
        }
    }

    if (VelocityMetersPerSecond.IsZero())
    {
        return FVector3d::ZeroVector;
    }

    // --- Swept collision ---------------------------------------------------
    FVector3d Step = VelocityMetersPerSecond * Dt;

    FVector3d TargetLocal(
        LocalMeters.X + Step.X, LocalMeters.Y + Step.Y, LocalMeters.Z + Step.Z);

    FPlanetSweepResult Sweep;

    if (FPlanetTrajectory::TryClampStepToBounds(
            Descriptor, LocalMeters, TargetLocal, CollisionStandoffMeters, Sweep))
    {
        // The step would have crossed the body. Stop at the standoff sphere
        // and shed the velocity that was heading into it, keeping whatever was
        // tangential - a craft clipping the edge of a planet should be
        // deflected along it, not brought to a dead halt.
        ++CollisionClampCount;

        Step = FVector3d(
            TargetLocal.X - LocalMeters.X,
            TargetLocal.Y - LocalMeters.Y,
            TargetLocal.Z - LocalMeters.Z);

        const FVector3d Up = FPlanetGravityField::GetLocalUp(TargetLocal);
        const double Inward = FVector3d::DotProduct(VelocityMetersPerSecond, Up);

        if (Inward < 0.0)
        {
            VelocityMetersPerSecond -= Up * Inward;
        }
    }

    // --- Touchdown ---------------------------------------------------------
    //
    // Checked against the real terrain rather than the bounding sphere, since
    // this is the point at which the answer has to be the actual ground.
    const FVector3d EndLocal(
        LocalMeters.X + Step.X, LocalMeters.Y + Step.Y, LocalMeters.Z + Step.Z);

    const FPlanetSweepResult TerrainSweep = FPlanetTrajectory::SweepAgainstTerrain(
        Descriptor, Planet->GetTerrainSettings(), LocalMeters, EndLocal, LandedClearanceMeters);

    if (TerrainSweep.bHit && !TerrainSweep.bStartedInside)
    {
        const FVector3d Up = FPlanetGravityField::GetLocalUp(TerrainSweep.EntryPointMeters);
        const double DescentSpeed = -FVector3d::DotProduct(VelocityMetersPerSecond, Up);

        UE_LOG(LogUniverseProbe, Log,
            TEXT("%s on %s at %.1f m/s descent (%.1f m/s total)."),
            (DescentSpeed <= SafeLandingSpeedMs) ? TEXT("Landed") : TEXT("Hard impact"),
            *Planet->GetName(),
            DescentSpeed,
            VelocityMetersPerSecond.Size());

        // Settle on the surface directly below the contact point, at the
        // resting clearance. Using the contact point's *direction* rather than
        // the contact point itself matters: the sweep stops the craft where it
        // first touched, which on a slope is partway up the hillside, and a
        // ship left there is intersecting the ground it landed on.
        const FVector3d Resting = FPlanetSurfaceQuery::GetPositionAboveTerrain(
            Descriptor, Planet->GetTerrainSettings(),
            TerrainSweep.EntryPointMeters, LandedClearanceMeters);

        bLanded = true;
        VelocityMetersPerSecond = FVector3d::ZeroVector;
        LastAltitudeAboveTerrainMeters = LandedClearanceMeters;

        return FVector3d(
            Resting.X - LocalMeters.X, Resting.Y - LocalMeters.Y, Resting.Z - LocalMeters.Z);
    }

    return Step;
}

void AUniverseProbePawn::SetDisembarkedCharacter(APlanetCharacter* Character)
{
    DisembarkedCharacter = Character;
}

APlanetCharacter* AUniverseProbePawn::GetDisembarkedCharacter() const
{
    return DisembarkedCharacter.Get();
}

bool AUniverseProbePawn::TryExitToSurface()
{
    UWorld* World = GetWorld();

    if (World == nullptr)
    {
        return false;
    }

    UUniverseWorldSubsystem* Subsystem = World->GetSubsystem<UUniverseWorldSubsystem>();
    APlanetActor* Planet = (Subsystem != nullptr) ? Subsystem->GetFramePlanet() : nullptr;

    if (Planet == nullptr)
    {
        UE_LOG(LogUniverseProbe, Log, TEXT("Cannot step out: not near a planet."));
        return false;
    }

    if (!bLanded)
    {
        // Refusing is better than obliging. A character spawned at altitude
        // would fall, and with the ship left flying there would be nothing to
        // fall back to.
        UE_LOG(LogUniverseProbe, Log,
            TEXT("Cannot step out: the ship is not landed (%.1f m above terrain)."),
            LastAltitudeAboveTerrainMeters);
        return false;
    }

    APlayerController* Player = Cast<APlayerController>(GetController());

    if (Player == nullptr)
    {
        return false;
    }

    const FUniversePosition ShipPosition = Anchor->GetUniversePosition();
    const FVector3d ShipLocal = Planet->UniverseToPlanetLocalMeters(ShipPosition);

    // Step out a few metres to one side, along the surface, so the character
    // does not appear inside the hull.
    FVector3d Up = FPlanetGravityField::GetLocalUp(ShipLocal);
    FVector3d TangentU;
    FVector3d TangentV;
    FPlanetTerrain::GetTangentBasis(Up, TangentU, TangentV);

    constexpr double DisembarkOffsetMeters = 6.0;

    const FVector3d ExitLocal(
        ShipLocal.X + TangentU.X * DisembarkOffsetMeters,
        ShipLocal.Y + TangentU.Y * DisembarkOffsetMeters,
        ShipLocal.Z + TangentU.Z * DisembarkOffsetMeters);

    APlanetCharacter* Character = DisembarkedCharacter.Get();

    if (Character == nullptr)
    {
        FActorSpawnParameters SpawnParams;
        SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

        Character = World->SpawnActor<APlanetCharacter>(
            APlanetCharacter::StaticClass(), FTransform::Identity, SpawnParams);

        if (Character == nullptr)
        {
            UE_LOG(LogUniverseProbe, Error, TEXT("Failed to spawn the character."));
            return false;
        }

        DisembarkedCharacter = Character;
    }
    else
    {
        // Returning from a previous excursion: the same character, put back.
        Character->SetActorHiddenInGame(false);
        Character->SetActorEnableCollision(true);
    }

    Character->SetShipToReenter(this);

    // Possess first, then place. Possession is what points the render origin
    // at the character, and placing before that would position them relative
    // to an origin that is about to move.
    Player->Possess(Character);
    Character->PlaceOnSurface(Planet, ExitLocal);

    UE_LOG(LogUniverseProbe, Log,
        TEXT("Stepped out onto %s. Press F beside the ship to board again."),
        *Planet->GetName());

    return true;
}

void AUniverseProbePawn::OnExitShip(const FInputActionValue& Value)
{
    (void)Value;
    TryExitToSurface();
}

void AUniverseProbePawn::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    if (Anchor == nullptr || DeltaSeconds <= 0.0f)
    {
        return;
    }

    const double Dt = static_cast<double>(DeltaSeconds);

    // --- Orientation -------------------------------------------------------
    // Local-space rotation, so the probe handles like a spacecraft with no
    // preferred "up" - there is no world gravity here to define one.
    const double PitchDelta = PitchInput * MouseSensitivity;
    const double YawDelta   = YawInput * MouseSensitivity;
    const double RollDelta  = RollInput * RotationRateDegPerSec * Dt;

    if (PitchDelta != 0.0 || YawDelta != 0.0 || RollDelta != 0.0)
    {
        AddActorLocalRotation(FRotator(-PitchDelta, YawDelta, RollDelta));
    }

    // Mouse deltas are consumed each frame; held keys are re-sent by the input
    // system, so only the accumulating axes are cleared here.
    PitchInput = 0.0;
    YawInput = 0.0;

    // --- Autopilot ---------------------------------------------------------
    // Applied here, after the input handlers have run for this frame, so it
    // reads exactly as if the key were held down.
    if (CVarAutoPilot.GetValueOnGameThread() != 0)
    {
        SpeedTier = FMath::Clamp(CVarAutoPilotTier.GetValueOnGameThread(), 0, MaxSpeedTier);
        ThrustInput = 1.0;
    }

    // --- Acceleration ------------------------------------------------------
    const double Acceleration = BaseAccelerationMs2 * GetSpeedMultiplier();

    const FVector Forward = GetActorForwardVector();
    const FVector Right   = GetActorRightVector();
    const FVector Up      = GetActorUpVector();

    FVector3d AccelVector = FVector3d::ZeroVector;
    AccelVector += FVector3d(Forward.X, Forward.Y, Forward.Z) * ThrustInput;
    AccelVector += FVector3d(Right.X, Right.Y, Right.Z) * StrafeInput;
    AccelVector += FVector3d(Up.X, Up.Y, Up.Z) * LiftInput;

    if (!AccelVector.IsZero())
    {
        VelocityMetersPerSecond += AccelVector.GetSafeNormal() * (Acceleration * Dt);
    }

    if (bBraking)
    {
        // Exponential decay rather than a linear subtraction: linear braking
        // overshoots through zero at low speed and jitters.
        const double Retained = FMath::Max(0.0, 1.0 - BrakeFractionPerSecond * Dt);
        VelocityMetersPerSecond *= Retained;
        if (VelocityMetersPerSecond.Size() < 0.01)
        {
            VelocityMetersPerSecond = FVector3d::ZeroVector;
        }
    }

    // Clamp so a frame's displacement stays inside the exact-normalisation
    // range. See MaxSpeedMetersPerSecond for the derivation.
    const double Speed = VelocityMetersPerSecond.Size();
    if (Speed > MaxSpeedMetersPerSecond)
    {
        VelocityMetersPerSecond = VelocityMetersPerSecond.GetSafeNormal() * MaxSpeedMetersPerSecond;
    }

    // --- Integrate into the canonical position -----------------------------
    //
    // Gravity, atmospheric drag, swept collision and landing all live in
    // IntegratePlanetaryStep, which does nothing at all in the interstellar
    // frame. Keeping them in one function rather than scattered through the
    // tick is what makes it possible to say where the probe's position can
    // change: here, and in the teleports.
    const FVector3d DeltaMeters = IntegratePlanetaryStep(Dt);

    if (!DeltaMeters.IsZero())
    {
        const FUniversePosition Previous = Anchor->GetUniversePosition();
        const FUniversePosition Next = Previous.OffsetByMeters(DeltaMeters);

        OdometerLightYears += FUniversePosition::DistanceLightYears(Previous, Next);

        Anchor->SetUniversePosition(Next);
    }

    // --- Terrain stress path -----------------------------------------------
    if (StressCyclesRemaining > 0)
    {
        TimeSinceStressStep += Dt;
        if (TimeSinceStressStep >= StressStepSeconds)
        {
            TimeSinceStressStep = 0.0;
            AdvanceTerrainStress();
        }
    }

    // --- Delayed screenshot ------------------------------------------------
    const float ScreenshotDelay = CVarScreenshotAfterSeconds.GetValueOnGameThread();
    if (ScreenshotDelay > 0.0f)
    {
        TimeSinceScreenshotRequest += Dt;
        if (TimeSinceScreenshotRequest >= static_cast<double>(ScreenshotDelay))
        {
            TimeSinceScreenshotRequest = 0.0;

            // Self-disable so a single request produces a single capture.
            CVarScreenshotAfterSeconds->Set(0.0f, ECVF_SetByConsole);

            // FScreenshotRequest rather than an Exec of HighResShot: the
            // console path depends on a viewport being wired up the way an
            // interactive session has it, and silently does nothing in a
            // -game -benchmark run, which is exactly the case this exists for.
            FScreenshotRequest::RequestScreenshot(TEXT("PlanetView"), false, false);
        }
    }

    // --- Periodic state log ------------------------------------------------
    // The evidence trail for a long traversal run: it shows the global cell
    // index climbing without bound while the Unreal local position stays
    // inside the rebase radius, which is the whole claim of the architecture.
    const double LogInterval = static_cast<double>(CVarStateLogInterval.GetValueOnGameThread());
    if (LogInterval > 0.0)
    {
        TimeSinceStateLog += Dt;
        if (TimeSinceStateLog >= LogInterval)
        {
            TimeSinceStateLog = 0.0;

            const FUniversePosition Position = Anchor->GetUniversePosition();
            const FVector LocalUnreal = GetActorLocation();
            const UUniverseWorldSubsystem* Subsystem =
                GetWorld() != nullptr ? GetWorld()->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;

            // The nearest system is included so a headless run can show that
            // proximity lookup keeps working as the probe travels, and that
            // leaving and returning finds the same system again.
            FString NearestText = TEXT("none");
            if (Subsystem != nullptr)
            {
                FStarSystemDescriptor Nearest;
                if (Subsystem->GetNearestSystem(Nearest))
                {
                    NearestText = FString::Printf(TEXT("%s@%.6fly"),
                        *Nearest.Name, Subsystem->GetNearestSystemDistanceLightYears());
                }
            }

            UE_LOG(LogUniverseProbe, Log,
                TEXT("STATE cell=[%lld,%lld,%lld] local=[%.1f,%.1f,%.1f]cm ")
                TEXT("unreal=|%.1f|cm rebases=%d speed=%.4gc odometer=%.6f ly origin_dist=%.6f ly nearest=%s"),
                static_cast<long long>(Position.CellX),
                static_cast<long long>(Position.CellY),
                static_cast<long long>(Position.CellZ),
                Position.Local.X, Position.Local.Y, Position.Local.Z,
                LocalUnreal.Size(),
                Subsystem != nullptr ? Subsystem->GetRebaseCount() : -1,
                GetSpeedInC(),
                OdometerLightYears,
                FUniversePosition::DistanceLightYears(FUniversePosition(), Position),
                *NearestText);
        }
    }

    // Held-axis inputs are re-delivered every frame while the key is down, so
    // clearing them here makes releasing a key stop the thrust.
    ThrustInput = 0.0;
    StrafeInput = 0.0;
    LiftInput = 0.0;
    RollInput = 0.0;
    bBraking = false;
}
