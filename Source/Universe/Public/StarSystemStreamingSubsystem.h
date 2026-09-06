// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UniverseCoordinates.h"
#include "StarSystemDescriptor.h"
#include "InterstellarTravel.h"
#include "StarSystemStreamingSubsystem.generated.h"

class AAstronomicalBodyActor;
class APlanetActor;
class UUniverseWorldSubsystem;

/**
 * StarSystemStreamingSubsystem.h
 *
 * Which of the universe's star systems currently have anything real behind
 * them.
 *
 *
 * THE SHAPE OF THE PROBLEM
 *
 * A galaxy has a hundred billion stars and the player is near one of them.
 * Every star that exists is reconstructible from its address at any moment, so
 * the question is never "does this system exist" - it always does - but "how
 * much of it is worth building right now".
 *
 * That makes this a *relevance* system rather than a loading system. There is
 * no point at which a system is created or destroyed; there is only a sliding
 * amount of representation, from an address that costs nothing through a point
 * of light to a fully simulated world with terrain under the player's feet.
 * Nothing is lost by dropping back down, because everything above the address
 * was derived from it.
 *
 *
 * THE STATES
 *
 *     Unknown         Not in range. No memory of it at all.
 *     DescriptorOnly  Address and position known. Costs a few hundred bytes.
 *     DistantVisual   A star, drawn as a point of light in scaled space.
 *     NearbyVisual    Star plus placeholder bodies for its planets.
 *     Prewarming      Building the real planet, before the player arrives.
 *     Active          Full simulation: terrain, environment, persistence.
 *     Unloading       Releasing the expensive parts.
 *
 * The transitions are driven by distance, with one exception: Prewarming can be
 * entered early on approach, because arriving in empty black space and waiting
 * for terrain to generate is the failure this whole layer exists to prevent.
 *
 *
 * WHAT IS *NOT* RELEASED
 *
 * Persistent deltas. A chopped tree and a placed structure live in the world
 * store, not in the actor, and dropping a system back to DescriptorOnly does
 * not touch them - that is precisely the point of the Sprint 005 architecture:
 * procedural base world plus sparse deltas. Discovery state is likewise
 * persistent, because "have I been here" is a fact about the player rather than
 * about what is currently loaded.
 *
 *
 * BUDGETS
 *
 * Exactly one system may be Active. That is not a performance guess - it is a
 * statement about what Active means: it owns the streaming planet, the
 * environment queries and the simulation frame, and two of those at once would
 * be two answers to "which way is down".
 */

/** How much of a system currently exists. */
UENUM(BlueprintType)
enum class ESystemStreamState : uint8
{
    Unknown         UMETA(DisplayName = "Unknown"),
    DescriptorOnly  UMETA(DisplayName = "Descriptor Only"),
    DistantVisual   UMETA(DisplayName = "Distant Visual"),
    NearbyVisual    UMETA(DisplayName = "Nearby Visual"),
    Prewarming      UMETA(DisplayName = "Prewarming"),
    Active          UMETA(DisplayName = "Active"),
    Unloading       UMETA(DisplayName = "Unloading"),
};

UNIVERSE_API const TCHAR* LexToString(ESystemStreamState State);

/**
 * What the player knows about a system.
 *
 * Deliberately three states and no progression system. It exists so that
 * navigation has something to list and so that persistence has something to
 * remember across sessions - not as the beginning of an exploration metagame,
 * which the sprint explicitly rules out.
 */
UENUM(BlueprintType)
enum class ESystemDiscoveryState : uint8
{
    Undiscovered UMETA(DisplayName = "Undiscovered"),
    Detected     UMETA(DisplayName = "Detected"),
    Visited      UMETA(DisplayName = "Visited"),
};

UNIVERSE_API const TCHAR* LexToString(ESystemDiscoveryState State);

/** One system's streaming record. */
USTRUCT()
struct FStreamedSystem
{
    GENERATED_BODY()

    FUniverseSystemId Id;

    FUniversePosition Position;

    /**
     * The generated system.
     *
     * Only filled once the system reaches DistantVisual: generating a star and
     * its planets for every address inside the scan radius would be thousands
     * of systems built to draw nothing.
     */
    FStarSystemDescriptor Descriptor;

    bool bHasDescriptor = false;

    ESystemStreamState State = ESystemStreamState::Unknown;

    ESystemDiscoveryState Discovery = ESystemDiscoveryState::Undiscovered;

    /** Distance from the viewpoint at the last refresh, light years. */
    double DistanceLightYears = 0.0;

    /** The star's placeholder, present from DistantVisual up. */
    UPROPERTY()
    TObjectPtr<AAstronomicalBodyActor> StarActor;

    /** Placeholders for the planets, present from NearbyVisual up. */
    UPROPERTY()
    TArray<TObjectPtr<AAstronomicalBodyActor>> BodyActors;

    /** The real streaming world, present only while Active. */
    UPROPERTY()
    TObjectPtr<APlanetActor> PlanetActor;

    /** Which orbit was promoted to a streaming world. */
    int32 StreamingPlanetOrbitIndex = INDEX_NONE;

    /** Seconds this record has been in its current state. Catches flapping. */
    double TimeInState = 0.0;
};

UCLASS()
class UNIVERSE_API UStarSystemStreamingSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    // --- Configuration ------------------------------------------------------
    //
    // Distances in light years, largest first. Every threshold is a gameplay
    // decision and none of them is referenced from anywhere else, so tuning
    // happens here and nowhere else.

    /** Systems inside this are remembered as addresses. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    double ScanRadiusLightYears = 25.0;

    /** Inside this, a system is drawn as a star. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    double DistantVisualLightYears = 20.0;

    /** Inside this, its planets get placeholders too. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    double NearbyVisualLightYears = 0.05;

    /** Inside this, the system becomes fully active. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    double ActiveLightYears = 0.01;

    /**
     * Hysteresis on every threshold, as a fraction.
     *
     * A system leaves a state at 1.25x the distance it entered it. Without
     * this, a ship hovering on a boundary rebuilds and destroys a solar system
     * every frame - which is not a performance problem so much as a visible
     * one, since the planet under the player would blink.
     */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    double HysteresisFraction = 0.25;

    /** Seconds of time-to-arrival at which an approach starts prewarming. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    double PrewarmLeadSeconds = 8.0;

    /** How many systems may have visible star actors at once. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    int32 MaxVisualSystems = 96;

    /** How many may have planet placeholders at once. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    int32 MaxNearbySystems = 4;

    /** Seconds between rescans. The set changes on the scale of seconds. */
    UPROPERTY(EditAnywhere, Category = "Universe|Streaming")
    double RescanIntervalSeconds = 0.5;

    // --- Queries ------------------------------------------------------------

    /** The active system, if there is one. */
    bool GetActiveSystem(FStarSystemDescriptor& OutSystem) const;

    UFUNCTION(BlueprintPure, Category = "Universe|Streaming")
    bool HasActiveSystem() const { return ActiveSystemId.IsValid(); }

    /** The planet actor of the active system, or null. */
    UFUNCTION(BlueprintPure, Category = "Universe|Streaming")
    APlanetActor* GetActivePlanetActor() const;

    /** Every system currently tracked, nearest first. */
    const TArray<FStreamedSystem>& GetTrackedSystems() const { return Systems; }

    /** How many systems are in each state, for the debug HUD. */
    void GetStateCounts(TArray<int32>& OutCounts) const;

    /** The nearest tracked system, or false when there is none. */
    bool GetNearestSystem(FStreamedSystem& OutSystem) const;

    /** Looks up one system by identity. */
    const FStreamedSystem* FindSystem(const FUniverseSystemId& Id) const;

    /** Total systems generated this session. A cost measure, not a state. */
    UFUNCTION(BlueprintPure, Category = "Universe|Streaming")
    int32 GetGeneratedSystemCount() const { return GeneratedSystemCount; }

    /** How many state transitions have happened. Flapping shows up here. */
    UFUNCTION(BlueprintPure, Category = "Universe|Streaming")
    int32 GetTransitionCount() const { return TransitionCount; }

    // --- Navigation ---------------------------------------------------------

    /**
     * Sets the navigation target, which also tells the streamer what to
     * prewarm.
     *
     * Returns false for an unknown system rather than storing a target that
     * will never resolve.
     */
    bool SetTargetSystem(const FUniverseSystemId& Id);

    void ClearTarget();

    bool HasTarget() const { return TargetId.IsValid(); }

    const FUniverseSystemId& GetTargetId() const { return TargetId; }

    /** The travel target for the movement layer, valid only when a target is set. */
    FTravelTarget GetTravelTarget() const;

    /**
     * The nearest system in a direction, for "target what I am looking at".
     *
     * ConeCosine is the cosine of the half-angle: 0.98 is about eleven degrees.
     */
    bool FindSystemInDirection(
        const FVector3d& DirectionUnit,
        double ConeCosine,
        FUniverseSystemId& OutId) const;

    // --- Discovery ----------------------------------------------------------

    ESystemDiscoveryState GetDiscoveryState(const FUniverseSystemId& Id) const;

    /** How many systems have been detected, and how many visited. */
    void GetDiscoveryCounts(int32& OutDetected, int32& OutVisited) const;

    /** Broadcast when a system is first detected or first visited. */
    DECLARE_MULTICAST_DELEGATE_TwoParams(
        FOnSystemDiscovered, const FStarSystemDescriptor&, ESystemDiscoveryState);
    FOnSystemDiscovered OnSystemDiscovered;

private:
    /** Rebuilds the tracked set from the sectors around the viewpoint. */
    void Rescan(const FUniversePosition& Viewpoint);

    /** Chooses and applies the state each tracked system should be in. */
    void ApplyStates(double DeltaSeconds);

    /** Moves one record to a new state, building or releasing as required. */
    void TransitionTo(FStreamedSystem& System, ESystemStreamState NewState);

    void BuildStarActor(FStreamedSystem& System);
    void BuildBodyActors(FStreamedSystem& System);
    void BuildPlanetActor(FStreamedSystem& System);

    void ReleaseBodyActors(FStreamedSystem& System);
    void ReleasePlanetActor(FStreamedSystem& System);
    void ReleaseAll(FStreamedSystem& System);

    /** Ensures the descriptor is generated. Returns false if it cannot be. */
    bool EnsureDescriptor(FStreamedSystem& System);

    /** Which orbit of a system is worth promoting to a streaming world. */
    int32 ChooseStreamingPlanet(const FStarSystemDescriptor& System) const;

    void MarkDiscovered(FStreamedSystem& System, ESystemDiscoveryState NewState);

    /** Loads discovery state from the world store, once the world is open. */
    void LoadDiscoveryState();

    /** Records one system's discovery state in the world store. */
    void SaveDiscoveryState(const FStreamedSystem& System);

    UUniverseWorldSubsystem* GetUniverse() const;

    /** The viewpoint the streamer follows: the tracked anchor's position. */
    bool GetViewpoint(FUniversePosition& OutPosition, FVector3d& OutVelocityMs) const;

    UPROPERTY()
    TArray<FStreamedSystem> Systems;

    FUniverseSystemId ActiveSystemId;
    FUniverseSystemId TargetId;

    double TimeSinceRescan = 0.0;

    int32 GeneratedSystemCount = 0;
    int32 TransitionCount = 0;

    bool bDiscoveryLoaded = false;
};
