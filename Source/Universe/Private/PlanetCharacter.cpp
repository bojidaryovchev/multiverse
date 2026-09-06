// Copyright Universe Project. All Rights Reserved.

#include "PlanetCharacter.h"
#include "PlanetActor.h"
#include "UniverseAnchorComponent.h"
#include "UniverseWorldSubsystem.h"
#include "UniverseProbePawn.h"
#include "UniverseScale.h"
#include "PlanetTerrainComponent.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"

DEFINE_LOG_CATEGORY_STATIC(LogPlanetCharacter, Log, All);

namespace
{
    /** Unreal's default world gravity, cm/s^2. The CMC expresses its own as a
     *  scale of this, so a planetary magnitude has to be divided back out. */
    constexpr double UnrealDefaultGravityCmS2 = 980.0;
}

APlanetCharacter::APlanetCharacter()
{
    PrimaryActorTick.bCanEverTick = true;

    // Tick after physics so the anchor reads the position the movement
    // component actually ended the frame at. Reading it before would write a
    // universe position one frame stale back onto the actor, which over a long
    // walk is a slow, invisible drift.
    PrimaryActorTick.TickGroup = TG_PostPhysics;

    Anchor = CreateDefaultSubobject<UUniverseAnchorComponent>(TEXT("UniverseAnchor"));

    if (UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        Capsule->InitCapsuleSize(42.0f, 96.0f);
    }

    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->MaxWalkSpeed = WalkSpeedCmS;
        Movement->JumpZVelocity = 420.0f;
        Movement->AirControl = 0.35f;
        Movement->BrakingDecelerationWalking = 2000.0f;

        // The capsule follows gravity, not the controller. Orientation on a
        // sphere is decided by where you are standing, and letting the
        // controller rotation drive it would fight the per-frame gravity
        // update - two systems writing one transform, which resolves as
        // jitter.
        Movement->bOrientRotationToMovement = false;
        Movement->bUseControllerDesiredRotation = false;

        // Steep terrain is normal on a procedural planet, and a low limit
        // turns ordinary hillsides into invisible walls.
        Movement->SetWalkableFloorAngle(55.0f);
    }

    bUseControllerRotationYaw = false;
    bUseControllerRotationPitch = false;
    bUseControllerRotationRoll = false;

    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(GetCapsuleComponent());
    CameraBoom->TargetArmLength = 320.0f;
    CameraBoom->bUsePawnControlRotation = true;
    CameraBoom->bDoCollisionTest = true;

    Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
    Camera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    Camera->bUsePawnControlRotation = false;

    // A planet fills the sky from its surface and recedes to a dot from orbit,
    // so the depth range has to span both. Sprint 002 already set this on the
    // probe for the same reason.
    Camera->SetFieldOfView(90.0f);
}

void APlanetCharacter::BeginPlay()
{
    Super::BeginPlay();

    BuildInputAssets();
    RebuildLookBasis();
}

void APlanetCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);

    if (const APlayerController* Player = Cast<APlayerController>(NewController))
    {
        if (const ULocalPlayer* LocalPlayer = Player->GetLocalPlayer())
        {
            if (UEnhancedInputLocalPlayerSubsystem* InputSubsystem =
                    LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
            {
                BuildInputAssets();
                InputSubsystem->AddMappingContext(MappingContext, 0);
            }
        }
    }

    // Possession is what makes this the thing the universe follows. Without
    // it the render origin would keep chasing the ship the player just left,
    // and the character would be rebased away from under their own feet.
    if (UUniverseWorldSubsystem* Subsystem = GetUniverseSubsystem())
    {
        Subsystem->SetTrackedAnchor(Anchor);
    }

    RebuildLookBasis();
}

UUniverseWorldSubsystem* APlanetCharacter::GetUniverseSubsystem() const
{
    UWorld* World = GetWorld();
    return (World != nullptr) ? World->GetSubsystem<UUniverseWorldSubsystem>() : nullptr;
}

const FUniversePosition& APlanetCharacter::GetUniversePosition() const
{
    static const FUniversePosition Fallback;
    return (Anchor != nullptr) ? Anchor->GetUniversePosition() : Fallback;
}

void APlanetCharacter::SetUniversePosition(const FUniversePosition& NewPosition)
{
    if (Anchor != nullptr)
    {
        Anchor->SetUniversePosition(NewPosition);
    }

    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        // A teleport is not a fall. Carrying the previous velocity through one
        // would mean arriving at a mountaintop still moving at whatever speed
        // the character was doing on the other side of the planet.
        Movement->StopMovementImmediately();
    }

    RebuildLookBasis();
}

APlanetActor* APlanetCharacter::GetFramePlanet() const
{
    const UUniverseWorldSubsystem* Subsystem = GetUniverseSubsystem();
    return (Subsystem != nullptr) ? Subsystem->GetFramePlanet() : nullptr;
}

void APlanetCharacter::PlaceOnSurface(APlanetActor* Planet, const FVector3d& PlanetLocalDirection)
{
    if (Planet == nullptr)
    {
        return;
    }

    // Half the capsule, plus a margin. Spawning with the feet exactly on the
    // ground leaves the capsule intersecting the collision mesh on its first
    // frame, and Chaos resolves an initial overlap by ejecting the body -
    // which on a planet means launching the character into the sky.
    double ClearanceMeters = 2.0;

    if (const UCapsuleComponent* Capsule = GetCapsuleComponent())
    {
        ClearanceMeters =
            Capsule->GetScaledCapsuleHalfHeight() * UniverseScale::MetersPerCm + 1.0;
    }

    SetUniversePosition(Planet->GetUniversePositionAboveTerrain(PlanetLocalDirection, ClearanceMeters));

    // Orientation has to be right on the first frame too. A character dropped
    // in with a world-Z-up capsule onto ground whose normal points sideways
    // spends the first moments toppling over, which reads as a bug even though
    // it corrects itself.
    UpdateGravityAndOrientation(0.0f);
}

float APlanetCharacter::GetAltitudeAboveTerrainMeters() const
{
    const APlanetActor* Planet = GetFramePlanet();

    if (Planet == nullptr)
    {
        return 0.0f;
    }

    const FPlanetSurfaceSample Sample = Planet->SampleSurfaceBelow(GetUniversePosition());
    const double Distance = Planet->GetDistanceFromCentreMeters(GetUniversePosition());

    return static_cast<float>(Distance - Sample.SurfaceRadiusMeters);
}

void APlanetCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);

    // The movement component is authoritative over the Unreal transform while
    // walking, so the universe position is read *back* from it rather than
    // written to it. This is the one place where the usual direction of
    // authority is reversed, and it is deliberate: reimplementing character
    // movement in universe coordinates to keep the arrow pointing the other
    // way would mean reimplementing floor sweeps, step-up, crouching and
    // deceleration. Instead Chaos owns the metre-scale motion, the anchor owns
    // the universe-scale position, and they are reconciled once per frame here,
    // after physics.
    if (Anchor != nullptr)
    {
        if (const UUniverseWorldSubsystem* Subsystem = GetUniverseSubsystem())
        {
            const FUniversePosition FromTransform =
                Subsystem->RenderLocationToUniverse(GetActorLocation());

            Anchor->SetUniversePosition(FromTransform);
        }
    }

    UpdateGravityAndOrientation(DeltaSeconds);
}

void APlanetCharacter::UpdateGravityAndOrientation(float DeltaSeconds)
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();

    if (Movement == nullptr)
    {
        return;
    }

    const UUniverseWorldSubsystem* Subsystem = GetUniverseSubsystem();
    const APlanetActor* Planet = GetFramePlanet();

    if (Subsystem == nullptr || Planet == nullptr)
    {
        // Interstellar frame: no planet, no gravity, no up. Floating is the
        // correct behaviour, and it is better than falling toward an arbitrary
        // axis, which is what a default -Z would produce here.
        GravityMagnitudeMs2 = 0.0;
        Movement->GravityScale = 0.0f;
        return;
    }

    const FUniversePosition Position = GetUniversePosition();

    const FVector3d GravityMs2 = Planet->GetGravityAccelerationMs2(Position);
    const FVector3d UpUniverse = Planet->GetLocalUp(Position);

    GravityMagnitudeMs2 = GravityMs2.Size();

    // Universe axes and render axes are parallel - rebasing is a translation,
    // never a rotation - so a direction needs no conversion between them. Only
    // positions do.
    CachedUp = FVector(UpUniverse.X, UpUniverse.Y, UpUniverse.Z).GetSafeNormal();

    if (CachedUp.IsNearlyZero())
    {
        CachedUp = FVector::UpVector;
    }

    Movement->SetGravityDirection(-CachedUp);

    // --- The collision safety zone ----------------------------------------
    //
    // Terrain that renders is not necessarily terrain you can stand on. A patch
    // is selected, generated, uploaded and only then cooked for collision, and
    // in the window between the last two the ground is visible and solid-looking
    // and completely intangible. A character walking onto such a patch does not
    // stop at its edge - they fall straight through the planet and keep going,
    // which at 9.8 m/s and with no floor below is unrecoverable.
    //
    // So gravity is withheld until the floor is real. Held in place is a
    // visibly odd state that lasts a fraction of a second and corrects itself;
    // falling through the world is not.
    const UPlanetTerrainComponent* Terrain = Planet->GetTerrainComponent();

    const bool bFloorReady =
        Terrain == nullptr
        || Terrain->HasCollisionAt(Planet->UniverseToPlanetLocalMeters(Position));

    if (!bFloorReady)
    {
        if (!bWaitingForCollision)
        {
            bWaitingForCollision = true;

            UE_LOG(LogPlanetCharacter, Verbose,
                TEXT("Holding: no cooked collision below the character yet."));
        }

        Movement->GravityScale = 0.0f;
        Movement->StopMovementImmediately();

        if (Movement->MovementMode != MOVE_Flying)
        {
            Movement->SetMovementMode(MOVE_Flying);
        }

        return;
    }

    if (bWaitingForCollision)
    {
        bWaitingForCollision = false;
        Movement->SetMovementMode(MOVE_Falling);
    }

    // The CMC expresses gravity as a scale of the world's, so the planetary
    // magnitude is divided back out into that unit. Going through GravityScale
    // rather than changing world gravity keeps this character's gravity local
    // to it, which is what allows two characters on opposite sides of a planet
    // - or on different planets - to both be right.
    Movement->GravityScale =
        static_cast<float>(GravityMagnitudeMs2 * UniverseScale::CmPerMeter / UnrealDefaultGravityCmS2);

    // Stand the capsule up in the local frame. The capsule's own yaw is
    // irrelevant - it is a capsule - so the rotation is built from up and
    // whatever forward the look basis is currently carrying, which keeps the
    // character facing where the camera looks.
    const FVector Forward = FVector::VectorPlaneProject(LookForward, CachedUp).GetSafeNormal();

    if (!Forward.IsNearlyZero())
    {
        SetActorRotation(FRotationMatrix::MakeFromZX(CachedUp, Forward).Rotator());
    }

    // And rebuild the controller rotation from the local basis. This is the
    // gimbal-free path described in the header: yaw is carried as a vector in
    // the tangent plane and pitch as a clamped scalar, so nothing here is ever
    // asked to express a rotation that a world rotator cannot represent.
    if (AController* OwningController = GetController())
    {
        const FVector Right = FVector::CrossProduct(CachedUp, Forward).GetSafeNormal();

        if (!Right.IsNearlyZero())
        {
            const FQuat Pitch(Right, FMath::DegreesToRadians(-LookPitchDegrees));
            const FVector LookDirection = Pitch.RotateVector(Forward);

            OwningController->SetControlRotation(
                FRotationMatrix::MakeFromXZ(LookDirection, CachedUp).Rotator());
        }
    }

    (void)DeltaSeconds;
}

void APlanetCharacter::RebuildLookBasis()
{
    // Re-derive a forward that lies in the tangent plane of wherever the
    // character now is. Called after any discontinuous move, because a forward
    // carried across a teleport can end up parallel to the new up, which has
    // no projection onto the tangent plane at all.
    const APlanetActor* Planet = GetFramePlanet();

    FVector Up = FVector::UpVector;

    if (Planet != nullptr)
    {
        const FVector3d UpUniverse = Planet->GetLocalUp(GetUniversePosition());
        Up = FVector(UpUniverse.X, UpUniverse.Y, UpUniverse.Z).GetSafeNormal();
    }

    if (Up.IsNearlyZero())
    {
        Up = FVector::UpVector;
    }

    CachedUp = Up;

    FVector Forward = FVector::VectorPlaneProject(LookForward, Up);

    if (Forward.IsNearlyZero())
    {
        // The old forward was parallel to the new up. Any tangent direction is
        // as good as any other, so take one from an axis that is definitely
        // not parallel - checking against the largest component of up, since
        // that is the axis it is most aligned with.
        const FVector Reference = (FMath::Abs(Up.Z) < 0.9)
            ? FVector::UpVector
            : FVector::ForwardVector;

        Forward = FVector::VectorPlaneProject(Reference, Up);
    }

    LookForward = Forward.GetSafeNormal();
    LookPitchDegrees = FMath::Clamp(LookPitchDegrees, -MaxPitchDegrees, MaxPitchDegrees);
}

// --- Input ------------------------------------------------------------------

void APlanetCharacter::BuildInputAssets()
{
    if (MappingContext != nullptr)
    {
        return;
    }

    MappingContext = NewObject<UInputMappingContext>(this, TEXT("CharacterMappingContext"));

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

    ActionMoveForward = MakeAxisAction(TEXT("IA_WalkForward"));
    ActionMoveRight   = MakeAxisAction(TEXT("IA_WalkRight"));
    ActionLookYaw     = MakeAxisAction(TEXT("IA_WalkLookYaw"));
    ActionLookPitch   = MakeAxisAction(TEXT("IA_WalkLookPitch"));
    ActionJump        = MakeBoolAction(TEXT("IA_WalkJump"));
    ActionSprint      = MakeBoolAction(TEXT("IA_WalkSprint"));
    ActionEnterShip   = MakeBoolAction(TEXT("IA_EnterShip"));

    auto MapNegated = [this](UInputAction* Action, const FKey& Key)
    {
        FEnhancedActionKeyMapping& Mapping = MappingContext->MapKey(Action, Key);
        Mapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
    };

    MappingContext->MapKey(ActionMoveForward, EKeys::W);
    MapNegated(ActionMoveForward, EKeys::S);

    MappingContext->MapKey(ActionMoveRight, EKeys::D);
    MapNegated(ActionMoveRight, EKeys::A);

    MappingContext->MapKey(ActionLookYaw, EKeys::MouseX);
    MappingContext->MapKey(ActionLookPitch, EKeys::MouseY);

    MappingContext->MapKey(ActionJump, EKeys::SpaceBar);
    MappingContext->MapKey(ActionSprint, EKeys::LeftShift);
    MappingContext->MapKey(ActionEnterShip, EKeys::F);
}

void APlanetCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    BuildInputAssets();

    UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);

    if (Input == nullptr)
    {
        UE_LOG(LogPlanetCharacter, Error,
            TEXT("Expected an EnhancedInputComponent. Check DefaultInput.ini."));
        return;
    }

    Input->BindAction(ActionMoveForward, ETriggerEvent::Triggered, this, &APlanetCharacter::OnMoveForward);
    Input->BindAction(ActionMoveRight,   ETriggerEvent::Triggered, this, &APlanetCharacter::OnMoveRight);
    Input->BindAction(ActionLookYaw,     ETriggerEvent::Triggered, this, &APlanetCharacter::OnLookYaw);
    Input->BindAction(ActionLookPitch,   ETriggerEvent::Triggered, this, &APlanetCharacter::OnLookPitch);
    Input->BindAction(ActionJump,        ETriggerEvent::Started,   this, &APlanetCharacter::OnJumpPressed);
    Input->BindAction(ActionJump,        ETriggerEvent::Completed, this, &APlanetCharacter::OnJumpReleased);
    Input->BindAction(ActionSprint,      ETriggerEvent::Started,   this, &APlanetCharacter::OnSprintPressed);
    Input->BindAction(ActionSprint,      ETriggerEvent::Completed, this, &APlanetCharacter::OnSprintReleased);
    Input->BindAction(ActionEnterShip,   ETriggerEvent::Started,   this, &APlanetCharacter::OnEnterShip);
}

void APlanetCharacter::OnMoveForward(const FInputActionValue& Value)
{
    const float Scale = Value.Get<float>();

    if (FMath::IsNearlyZero(Scale))
    {
        return;
    }

    // Move along the tangent plane, not along the camera's world forward. On a
    // sphere the two differ by the pitch of the camera, and using the camera
    // vector directly would make a character walk slower whenever they looked
    // at their feet.
    const FVector Forward = FVector::VectorPlaneProject(LookForward, CachedUp).GetSafeNormal();

    if (!Forward.IsNearlyZero())
    {
        AddMovementInput(Forward, Scale);
    }
}

void APlanetCharacter::OnMoveRight(const FInputActionValue& Value)
{
    const float Scale = Value.Get<float>();

    if (FMath::IsNearlyZero(Scale))
    {
        return;
    }

    const FVector Forward = FVector::VectorPlaneProject(LookForward, CachedUp).GetSafeNormal();
    const FVector Right = FVector::CrossProduct(CachedUp, Forward).GetSafeNormal();

    if (!Right.IsNearlyZero())
    {
        AddMovementInput(Right, Scale);
    }
}

void APlanetCharacter::OnLookYaw(const FInputActionValue& Value)
{
    const double Degrees = Value.Get<float>() * MouseSensitivity;

    if (FMath::IsNearlyZero(Degrees))
    {
        return;
    }

    // Yaw rotates the carried forward vector about the current up. Because it
    // is applied to a vector rather than accumulated into an angle, there is
    // no reference direction to become degenerate, and yaw is therefore
    // unbounded and continuous everywhere on the planet.
    const FQuat Rotation(CachedUp, FMath::DegreesToRadians(Degrees));

    LookForward = Rotation.RotateVector(LookForward).GetSafeNormal();

    if (LookForward.IsNearlyZero())
    {
        RebuildLookBasis();
    }
}

void APlanetCharacter::OnLookPitch(const FInputActionValue& Value)
{
    // Clamped short of vertical. Straight up is the single orientation where
    // the tangent basis is undefined, and clamping is preferable to special
    // casing it: the alternative is a camera that can be pointed at a
    // direction from which "forward" cannot be recovered.
    LookPitchDegrees = FMath::Clamp(
        LookPitchDegrees - Value.Get<float>() * MouseSensitivity,
        -MaxPitchDegrees, MaxPitchDegrees);
}

void APlanetCharacter::OnJumpPressed()
{
    Jump();
}

void APlanetCharacter::OnJumpReleased()
{
    StopJumping();
}

void APlanetCharacter::OnSprintPressed()
{
    bSprinting = true;

    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->MaxWalkSpeed = WalkSpeedCmS * SprintMultiplier;
    }
}

void APlanetCharacter::OnSprintReleased()
{
    bSprinting = false;

    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->MaxWalkSpeed = WalkSpeedCmS;
    }
}

void APlanetCharacter::OnEnterShip()
{
    TryEnterShip();
}

void APlanetCharacter::SetShipToReenter(AUniverseProbePawn* Ship)
{
    ShipToReenter = Ship;
}

AUniverseProbePawn* APlanetCharacter::GetShipToReenter() const
{
    return ShipToReenter.Get();
}

bool APlanetCharacter::TryEnterShip()
{
    AUniverseProbePawn* Ship = ShipToReenter.Get();

    if (Ship == nullptr)
    {
        UE_LOG(LogPlanetCharacter, Log, TEXT("No ship to enter."));
        return false;
    }

    // Range is measured in universe coordinates, not from the Unreal
    // transforms. The two agree here, but only because both actors happen to
    // be near the render origin; the universe positions are correct
    // unconditionally, and this is exactly the kind of check that would
    // silently start passing at the wrong distance if it were written against
    // render space and the player then walked far enough for a rebase.
    const double DistanceMeters = FUniversePosition::DistanceMeters(
        GetUniversePosition(), Ship->GetUniversePosition());

    if (DistanceMeters > ShipBoardingRangeMeters)
    {
        UE_LOG(LogPlanetCharacter, Log,
            TEXT("Ship is %.1f m away; boarding range is %.1f m."),
            DistanceMeters, ShipBoardingRangeMeters);
        return false;
    }

    APlayerController* Player = Cast<APlayerController>(GetController());

    if (Player == nullptr)
    {
        return false;
    }

    UE_LOG(LogPlanetCharacter, Log, TEXT("Boarding ship at %.1f m."), DistanceMeters);

    Ship->SetDisembarkedCharacter(this);
    Player->Possess(Ship);

    // The character stays in the world where it was. Destroying it would make
    // leaving the ship again a fresh spawn rather than a return, and would
    // discard whatever state a character eventually carries.
    SetActorHiddenInGame(true);
    SetActorEnableCollision(false);

    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->StopMovementImmediately();
        Movement->SetMovementMode(MOVE_None);
    }

    return true;
}
