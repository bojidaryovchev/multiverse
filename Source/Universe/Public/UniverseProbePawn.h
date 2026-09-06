// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "UniverseCoordinates.h"
#include "InterstellarTravel.h"
#include "UniverseProbePawn.generated.h"

class UCameraComponent;
class UInputAction;
class UInputMappingContext;
class UStaticMeshComponent;
class UUniverseAnchorComponent;
class UUniverseWorldSubsystem;
class APlanetActor;
class APlanetCharacter;
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
 * anything in its path.
 *
 *
 * WHAT SPRINT 003 ADDED
 *
 * Three things, all conditional on the simulation frame:
 *
 * 1. **Gravity.** In the planetary frame the probe is pulled toward the planet
 *    centre by the same field a character on the ground feels. There is no
 *    separate flight-model gravity: one field, asked of one subsystem.
 *
 * 2. **Swept collision.** Before a movement step is committed it is tested
 *    analytically against the body. At the top thrust tier a frame is 3.3e10 m,
 *    so a position-based check sees empty space at both ends of a step that
 *    went straight through a planet. See PlanetTrajectory.h.
 *
 * 3. **Landing, and staying landed.** Once down, the probe holds its universe
 *    position rather than integrating a residual velocity into the ground, and
 *    it stays exactly where it was left when the player walks away from it.
 *
 * Chaos still owns nothing here. The probe is kinematic at every speed, which
 * is the only way one movement path can serve both a landing approach at 5 m/s
 * and a cruise at half the speed of light.
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
    virtual void PossessedBy(AController* NewController) override;

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
     * Sets velocity directly, in m/s. For scripted runs and debug commands.
     *
     * Clears the landed flag when given a non-zero velocity: a landed ship
     * holds its position and discards velocity, so setting one without lifting
     * off would silently do nothing.
     */
    void SetUniverseVelocity(const FVector3d& InVelocityMs);

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

    // --- Sprint 003: planetary flight, landing and crew ---------------------

    /** True when the probe is resting on a planet surface. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    bool IsLanded() const { return bLanded; }

    /** Height above the ground directly below, in metres. Zero in deep space. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    float GetAltitudeAboveTerrainMeters() const { return static_cast<float>(LastAltitudeAboveTerrainMeters); }

    /** Normalised atmospheric depth at the probe: 0 above the air, 1 at sea level. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    float GetAtmosphericDepthFraction() const { return static_cast<float>(LastAtmosphericDepth); }

    /** How many times a movement step has been clamped short of a body. */
    UFUNCTION(BlueprintPure, Category = "Universe|Probe")
    int32 GetCollisionClampCount() const { return CollisionClampCount; }

    /**
     * Puts the player on the ground beside the ship and possesses them.
     *
     * Only possible when the ship is landed: stepping out at altitude would
     * spawn a character in mid-air with no way back, which is a worse outcome
     * than refusing.
     */
    UFUNCTION(BlueprintCallable, Category = "Universe|Probe")
    bool TryExitToSurface();

    /**
     * Marks the ship as resting on the surface without it having flown there.
     *
     * For teleports. Placing a ship at the right height but leaving it flagged
     * as flying makes it start falling on the next tick, so the two have to be
     * set together.
     */
    void ForceLanded();

    /** Remembers the character that stepped out, so boarding can find it.
     *  Defined out of line: TWeakObjectPtr assignment needs a complete type. */
    void SetDisembarkedCharacter(APlanetCharacter* Character);
    APlanetCharacter* GetDisembarkedCharacter() const;

    /**
     * Vertical speed above which touching the ground is an impact rather than
     * a landing, in m/s.
     *
     * There is no damage model yet, so both outcomes currently stop the ship
     * on the surface; the distinction is logged, and exists so that when
     * damage arrives it has a threshold to key off rather than needing one
     * invented at that point.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double SafeLandingSpeedMs = 20.0;

    /** Height the hull rests at above the ground when landed, in metres. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double LandedClearanceMeters = 4.0;

    // --- Sprint 006: interstellar travel ------------------------------------
    //
    // One canonical position and one canonical velocity, unchanged. What Sprint
    // 006 adds is a second *controller* over that same state, not a second copy
    // of it: warp integrates through FInterstellarTravel, which sweeps the path
    // against stars and planets and brakes onto a target, while sublight flight
    // keeps the direct thrust model that Sprints 001 to 005 are built on.
    //
    // Two controllers over one state is fine and duplicated state is not, which
    // is why the split is here rather than in a separate pawn: whichever one
    // ran this frame, the answer to "where is the ship" comes from the same
    // anchor.

    /** Speed and acceleration limits per regime. Data, not code - see the type. */
    FTravelProfile TravelProfile;

    /** True while the warp drive is engaged. */
    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    bool IsWarpEngaged() const { return bWarpEngaged; }

    /**
     * Engages or disengages warp.
     *
     * Refuses to engage while landed or inside a planet's local space: warping
     * out of a gravity well from a standing start is not a manoeuvre, it is a
     * way to end up inside the ground on the far side. Returns what the state
     * actually is afterwards.
     */
    UFUNCTION(BlueprintCallable, Category = "Universe|Travel")
    bool SetWarpEngaged(bool bEngaged);

    /** The travel regime the ship is in, derived from where it is. */
    EUniverseTravelMode GetTravelMode() const { return CurrentTravelMode; }

    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    FString GetTravelModeName() const { return LexToString(CurrentTravelMode); }

    /** Whether the autopilot brakes onto the navigation target. */
    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    bool IsAutoBrakeEnabled() const { return bAutoBrakeToTarget; }

    UFUNCTION(BlueprintCallable, Category = "Universe|Travel")
    void SetAutoBrake(bool bEnabled) { bAutoBrakeToTarget = bEnabled; }

    /**
     * Whether the autopilot also steers toward the target.
     *
     * Separate from braking because they are separately useful: braking without
     * steering is "stop me before I hit it", which a player flying manually
     * wants; steering as well is the scripted-run mode the acceptance tests use.
     */
    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    bool IsAutoSteerEnabled() const { return bAutoSteerToTarget; }

    UFUNCTION(BlueprintCallable, Category = "Universe|Travel")
    void SetAutoSteer(bool bEnabled) { bAutoSteerToTarget = bEnabled; }

    /** True on the frames the autopilot is decelerating onto its target. */
    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    bool IsBraking() const { return bTravelBraking; }

    /** How many times a warp step has been stopped short of a body. */
    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    int32 GetHazardStopCount() const { return HazardStopCount; }

    /** The last thing a warp step was stopped by. */
    const FTravelHazard& GetLastHazard() const { return LastHazard; }

    /** Distance to the navigation target in metres, or a negative value. */
    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    double GetDistanceToTargetMeters() const { return DistanceToTargetMeters; }

    /** Estimated seconds to the navigation target, or a negative value. */
    UFUNCTION(BlueprintPure, Category = "Universe|Travel")
    double GetEstimatedArrivalSeconds() const { return EstimatedArrivalSeconds; }

    /**
     * Clearance kept above the terrain bounding sphere when a step is clamped,
     * in metres.
     *
     * Generous, because the clamp is against the *bounding* sphere, and being
     * stopped a kilometre early above a basin is far better than being stopped
     * a metre late inside a mountain.
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Universe|Probe")
    double CollisionStandoffMeters = 1000.0;


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

    // --- Sprint 003 state ---------------------------------------------------
    bool bLanded = false;
    double LastAltitudeAboveTerrainMeters = 0.0;
    double LastAtmosphericDepth = 0.0;
    int32 CollisionClampCount = 0;

    TWeakObjectPtr<APlanetCharacter> DisembarkedCharacter;

    /** Calibrates the camera to the dominant light source. */
    void UpdateCameraExposure();

    /** Applies gravity, drag, swept collision and landing. Returns the step. */
    FVector3d IntegratePlanetaryStep(double Dt);

    /**
     * Integrates one warp step through FInterstellarTravel.
     *
     * Writes the anchor position and the velocity directly rather than
     * returning a delta, because the travel layer may shorten the step for a
     * hazard or an arrival and the caller has no way to reconstruct that from a
     * displacement alone.
     */
    void IntegrateWarpStep(double Dt);

    /** Recomputes the derived travel mode and the target readouts. */
    void UpdateTravelReadouts();

    void OnToggleWarp(const FInputActionValue& Value);
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionToggleWarp;

    bool bWarpEngaged = false;
    bool bAutoBrakeToTarget = true;
    bool bAutoSteerToTarget = false;
    bool bTravelBraking = false;

    EUniverseTravelMode CurrentTravelMode = EUniverseTravelMode::LocalSpace;

    FTravelHazard LastHazard;
    int32 HazardStopCount = 0;

    double DistanceToTargetMeters = -1.0;
    double EstimatedArrivalSeconds = -1.0;

    void OnExitShip(const FInputActionValue& Value);
    UPROPERTY(Transient) TObjectPtr<UInputAction> ActionExitShip;

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
