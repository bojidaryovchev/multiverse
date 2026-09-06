// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UniverseCoordinates.h"
#include "UniverseProbePawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
class UUniverseAnchorComponent;
struct FInputActionValue;

/**
 * AUniverseProbePawn
 *
 * A free-flying six-degree-of-freedom probe: the Sprint 001 vehicle for
 * proving the coordinate architecture.
 *
 * The important structural point is where state lives. The probe's authoritative
 * position is an FUniversePosition on its anchor component, and its velocity is
 * a double-precision vector in metres per second. Neither is stored as an Unreal
 * transform. Each frame the probe integrates velocity into its universe
 * position, and the Unreal transform follows as a derived value.
 *
 * That ordering is what stops the classic failure: a pawn that accumulates
 * position in an FVector, drifts to enormous coordinates, and starts juddering.
 * Here the Unreal location is bounded by the rebase radius no matter how far
 * the probe travels, and travelling ten billion light years costs no precision
 * at all.
 *
 * Movement is kinematic rather than Chaos-driven, deliberately. At the speeds
 * this pawn reaches, discrete rigid-body integration would tunnel through
 * anything in its path; sweep/trajectory intersection replaces it when
 * collision arrives in a later sprint (see CLAUDE.md section 6).
 */
UCLASS()
class UNIVERSE_API AUniverseProbePawn : public APawn
{
    GENERATED_BODY()

public:
    AUniverseProbePawn();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;

    // --- State queries used by the debug HUD -------------------------------

    /** Canonical position. */
    FUniversePosition GetUniversePosition() const;

    /** Velocity in metres per second, in universe axes. */
    FVector3d GetUniverseVelocity() const { return VelocityMetersPerSecond; }

    /** Speed in metres per second. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    double GetSpeedMetersPerSecond() const { return VelocityMetersPerSecond.Size(); }

    /** Speed as a multiple of c, the useful unit once travel gets serious. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    double GetSpeedInC() const;

    /** Current debug speed tier (0 = realistic thrust). */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    int32 GetSpeedTier() const { return SpeedTier; }

    /** Thrust multiplier implied by the current tier. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    double GetSpeedMultiplier() const;

    /** Human-readable label for the current tier. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    FString GetSpeedTierLabel() const;

    UUniverseAnchorComponent* GetAnchor() const { return Anchor; }

    // --- Configuration -----------------------------------------------------

    /** Base linear acceleration at tier 0, m/s^2. Roughly 5g. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double BaseAccelerationMs2 = 50.0;

    /** Rotation rate, degrees per second at full input. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double RotationRateDegPerSec = 70.0;

    /** Mouse sensitivity, degrees per unit of mouse input. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double MouseSensitivity = 2.5;

    /** Fraction of velocity shed per second while braking. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double BrakeFractionPerSecond = 2.0;

    /** Highest selectable speed tier. Each tier is 10x the last. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    int32 MaxSpeedTier = 14;

    /**
     * Hard cap on speed, metres per second.
     *
     * Chosen so a single frame's displacement stays inside the range where
     * FUniversePosition::Normalize is provably exact (2^53 cm ~ 9e13 m). At
     * 1e12 m/s a 1/30 s frame moves 3.3e10 m, three orders of magnitude below
     * that limit. Travel faster than this must use WarpJumpLightYears, which
     * moves in whole cells and is exact at any distance.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double MaxSpeedMetersPerSecond = 1.0e12;

    /** Light years moved by a single warp jump. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double WarpJumpLightYears = 5.0;

    /**
     * Jumps forward by whole cells - exact at any distance because it is pure
     * integer arithmetic on cell indices, with no floating point involved.
     * This is the mechanism for travel too large for velocity integration.
     */
    UFUNCTION(BlueprintCallable, Category = "Universe|Probe")
    bool WarpJump(double LightYears);

    /** Zeroes velocity immediately. */
    UFUNCTION(BlueprintCallable, Category = "Universe|Probe")
    void FullStop() { VelocityMetersPerSecond = FVector3d::ZeroVector; }

    /**
     * Starts the scripted terrain stress path (Sprint 002 section 42).
     *
     * Teleports around a fixed sequence of viewpoints spanning orbit to
     * near-surface and crossing several cube faces, so streaming behaviour can
     * be measured at identical points on every pass.
     */
    void BeginTerrainStress(int32 Cycles);

    /** Total distance travelled this session, light years. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    double GetOdometerLightYears() const { return OdometerLightYears; }

protected:
    UPROPERTY(VisibleAnywhere, Category = "Universe|Probe")
    TObjectPtr<USceneComponent> RootScene;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Probe")
    TObjectPtr<UStaticMeshComponent> HullMesh;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Probe")
    TObjectPtr<UCameraComponent> Camera;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Probe")
    TObjectPtr<UUniverseAnchorComponent> Anchor;

private:
    // --- Input -------------------------------------------------------------

    /**
     * Input actions and the mapping context are constructed in C++ rather than
     * authored as assets.
     *
     * Sprint 001 has no committed .uasset content at all: everything is built
     * from code and engine primitives, so a clean checkout builds and runs
     * without any binary asset needing to exist. When the project grows real
     * content these move into proper Enhanced Input assets.
     */
    void BuildInputAssets();

    void OnThrust(const FInputActionValue& Value);
    void OnStrafe(const FInputActionValue& Value);
    void OnLift(const FInputActionValue& Value);
    void OnPitch(const FInputActionValue& Value);
    void OnYaw(const FInputActionValue& Value);
    void OnRoll(const FInputActionValue& Value);
    void OnBrake(const FInputActionValue& Value);
    void OnSpeedUp(const FInputActionValue& Value);
    void OnSpeedDown(const FInputActionValue& Value);
    void OnWarpJump(const FInputActionValue& Value);
    void OnToggleDebug(const FInputActionValue& Value);

    UPROPERTY(Transient) TObjectPtr<UInputMappingContext> MappingContext;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionThrust;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionStrafe;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionLift;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionPitch;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionYaw;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionRoll;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionBrake;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionSpeedUp;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionSpeedDown;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionWarpJump;
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionToggleDebug;

    // --- Runtime state -----------------------------------------------------

    /** Authoritative velocity, metres per second, universe axes. */
    FVector3d VelocityMetersPerSecond = FVector3d::ZeroVector;

    /** Per-frame accumulated input, consumed and cleared in Tick. */
    double ThrustInput = 0.0;
    double StrafeInput = 0.0;
    double LiftInput = 0.0;
    double PitchInput = 0.0;
    double YawInput = 0.0;
    double RollInput = 0.0;
    bool bBraking = false;

    int32 SpeedTier = 0;
    double OdometerLightYears = 0.0;

    /** Accumulator for the periodic debug state log. */
    double TimeSinceStateLog = 0.0;

    /** Accumulator for the delayed debug screenshot. */
    double TimeSinceScreenshotRequest = 0.0;

    /** Scripted stress path state. */
    void AdvanceTerrainStress();
    int32 StressCyclesRemaining = 0;
    int32 StressStepIndex = 0;
    double TimeSinceStressStep = 0.0;

    /**
     * Seconds spent at each stress viewpoint.
     *
     * Long enough for streaming to settle so the reported counts mean
     * something, short enough that a multi-cycle run finishes in a sensible
     * time.
     */
    static constexpr double StressStepSeconds = 2.5;
};
