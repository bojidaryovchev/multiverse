// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "UniverseCoordinates.h"
#include "PlanetCharacter.generated.h"

class UCameraComponent;
class USpringArmComponent;
class UInputAction;
class UInputMappingContext;
class UUniverseAnchorComponent;
class UUniverseWorldSubsystem;
class APlanetActor;
class AUniverseProbePawn;

/**
 * APlanetCharacter
 *
 * Someone standing on a sphere.
 *
 *
 * THE PROBLEM WITH WALKING ON A PLANET
 *
 * Every stock character controller assumes down is -Z and the ground is
 * roughly horizontal. On a sphere that assumption is not merely approximate,
 * it is wrong by up to 180 degrees: two players on opposite sides of a world
 * have exactly opposite up vectors and both are right. A character built on
 * world -Z walks fine near wherever the level designer put the origin and then
 * falls off the planet.
 *
 * UE 5.4 added arbitrary gravity direction to UCharacterMovementComponent -
 * SetGravityDirection, with the movement mode, floor sweeps, step-up and
 * jumping all expressed in a gravity-relative basis. This class uses that
 * rather than reimplementing character movement, which is a large and
 * thoroughly debugged piece of engine code that would be foolish to duplicate.
 * What is left for this class to do is comparatively small:
 *
 *   - tell the movement component which way gravity points, every frame,
 *     from the planet the simulation frame has selected;
 *   - scale its magnitude to the planet, so a moon feels like a moon;
 *   - keep the capsule upright in the local frame;
 *   - and build a camera rotation that does not degenerate as up rotates.
 *
 *
 * WHY THE CAMERA CANNOT USE A WORLD ROTATOR
 *
 * The usual AddControllerYawInput / AddControllerPitchInput pair accumulates a
 * world-space FRotator, which encodes orientation as yaw about world Z and
 * pitch about the resulting Y. That is a gimbal, and it locks when the thing
 * being described is pitched 90 degrees away from world Z. Walking a quarter of
 * the way around a planet does exactly that: the character's local up becomes
 * perpendicular to world Z, and the controls become undefined - the classic
 * symptom being a camera that spins wildly as the player crosses the equator of
 * whatever axis the rotator was built around.
 *
 * So look direction is not stored as a world rotator at all. It is stored as
 * two scalars - a yaw about the *local* up and a pitch away from the local
 * horizon - and the world rotation is rebuilt each frame from the current up
 * and a forward vector carried in the tangent plane. Pitch is clamped short of
 * vertical, which is where the only remaining degeneracy is, and yaw is
 * unbounded because rotation about up never degenerates. The result is a
 * rotation that is continuous everywhere on the sphere, including across cube
 * faces and over the poles of any axis anyone might name.
 */
UCLASS()
class UNIVERSE_API APlanetCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    APlanetCharacter();

    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual void PossessedBy(AController* NewController) override;

    UUniverseAnchorComponent* GetAnchor() const { return Anchor; }

    const FUniversePosition& GetUniversePosition() const;

    /** Places the character at a universe position and rebuilds its basis. */
    void SetUniversePosition(const FUniversePosition& NewPosition);

    /**
     * Places the character on the ground below a universe position.
     *
     * Used by ship exit and by the debug teleports. The height offset puts the
     * capsule centre above the ground rather than its feet inside it - a
     * character spawned exactly on the surface starts the frame intersecting
     * it, and Chaos resolves that by ejecting them, usually upward and fast.
     */
    void PlaceOnSurface(APlanetActor* Planet, const FVector3d& PlanetLocalDirection);

    /** The planet this character is standing on, or null in deep space. */
    APlanetActor* GetFramePlanet() const;

    /** Height above the ground directly below, in metres. */
    UFUNCTION(BlueprintPure, Category = "Universe|Character")
    float GetAltitudeAboveTerrainMeters() const;

    /** Local up, in render space. +Z when there is no planet. */
    FVector GetLocalUp() const { return CachedUp; }

    /** True while the character is held because the ground below has no
     *  cooked collision yet. Surfaced on the HUD so the state is visible
     *  rather than looking like a hang. */
    UFUNCTION(BlueprintPure, Category = "Universe|Character")
    bool IsWaitingForCollision() const { return bWaitingForCollision; }

    /** Gravity magnitude currently being applied, in m/s^2. */
    UFUNCTION(BlueprintPure, Category = "Universe|Character")
    float GetGravityMagnitudeMs2() const { return static_cast<float>(GravityMagnitudeMs2); }

    /** The ship to return to, remembered when the character left it.
     *  Defined out of line: TWeakObjectPtr assignment needs a complete type. */
    void SetShipToReenter(AUniverseProbePawn* Ship);
    AUniverseProbePawn* GetShipToReenter() const;

    /** Re-possesses the ship this character came from, if it is close enough. */
    UFUNCTION(BlueprintCallable, Category = "Universe|Character")
    bool TryEnterShip();

    /** How close the character must be to the ship to board it, in metres. */
    UPROPERTY(EditAnywhere, Category = "Universe|Character")
    float ShipBoardingRangeMeters = 40.0f;

    /** Walking speed on the flat, cm/s. */
    UPROPERTY(EditAnywhere, Category = "Universe|Character")
    float WalkSpeedCmS = 400.0f;

    /** Sprint multiplier applied while the run key is held. */
    UPROPERTY(EditAnywhere, Category = "Universe|Character")
    float SprintMultiplier = 3.0f;

    /** Degrees of yaw and pitch per unit of mouse input. */
    UPROPERTY(EditAnywhere, Category = "Universe|Character")
    float MouseSensitivity = 0.6f;

protected:
    virtual void BeginPlay() override;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Character")
    TObjectPtr<UUniverseAnchorComponent> Anchor;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Character")
    TObjectPtr<USpringArmComponent> CameraBoom;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Character")
    TObjectPtr<UCameraComponent> Camera;

private:
    void BuildInputAssets();
    void UpdateGravityAndOrientation(float DeltaSeconds);
    void RebuildLookBasis();

    void OnMoveForward(const struct FInputActionValue& Value);
    void OnMoveRight(const struct FInputActionValue& Value);
    void OnLookYaw(const struct FInputActionValue& Value);
    void OnLookPitch(const struct FInputActionValue& Value);
    void OnJumpPressed();
    void OnJumpReleased();
    void OnSprintPressed();
    void OnSprintReleased();
    void OnEnterShip();

    UUniverseWorldSubsystem* GetUniverseSubsystem() const;

    UPROPERTY() TObjectPtr<UInputMappingContext> MappingContext;
    UPROPERTY() TObjectPtr<UInputAction> ActionMoveForward;
    UPROPERTY() TObjectPtr<UInputAction> ActionMoveRight;
    UPROPERTY() TObjectPtr<UInputAction> ActionLookYaw;
    UPROPERTY() TObjectPtr<UInputAction> ActionLookPitch;
    UPROPERTY() TObjectPtr<UInputAction> ActionJump;
    UPROPERTY() TObjectPtr<UInputAction> ActionSprint;
    UPROPERTY() TObjectPtr<UInputAction> ActionEnterShip;

    TWeakObjectPtr<AUniverseProbePawn> ShipToReenter;

    /**
     * Look direction, held in the local frame rather than as a world rotator.
     *
     * LookForward is a unit vector in the tangent plane, carried from frame to
     * frame and reprojected as up changes. Storing a *vector* rather than a
     * yaw angle is what makes walking over the pole of any axis a non-event:
     * an angle has to be measured from some reference direction, and every
     * choice of reference degenerates somewhere on the sphere. A vector
     * carried forward has no reference to degenerate.
     */
    FVector LookForward = FVector::ForwardVector;

    /** Pitch away from the local horizon, in degrees, clamped short of 90. */
    double LookPitchDegrees = 0.0;

    FVector CachedUp = FVector::UpVector;
    double GravityMagnitudeMs2 = 0.0;
    bool bSprinting = false;

    /** True while gravity is withheld because the floor has no collision yet. */
    bool bWaitingForCollision = false;

    /** Pitch limit. Not 90: at exactly vertical the tangent basis is undefined. */
    static constexpr double MaxPitchDegrees = 88.0;
};
