// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "StarSystemDescriptor.h"
#include "GalaxyDescriptor.h"
#include "UniverseGameMode.generated.h"

class AAstronomicalBodyActor;
class APlanetActor;

/**
 * AUniverseGameMode
 *
 * Builds the Sprint 001 test scene entirely from code.
 *
 * There is no committed level asset and no committed content of any kind. The
 * game mode seeds the universe, finds a real generated system, and spawns
 * placeholder actors for its star and planets, so a clean checkout builds and
 * runs with nothing but source. That keeps the repository free of binary assets
 * while the architecture is still moving, and it means the scene is always a
 * faithful view of what the generator actually produces rather than something
 * an artist froze into a map weeks ago.
 */
UCLASS()
class UNIVERSE_API AUniverseGameMode : public AGameModeBase
{
    GENERATED_BODY()

public:
    AUniverseGameMode();

    virtual void StartPlay() override;

    /**
     * Places a joining player.
     *
     * The game mode exists only on the server, so this is the only place that
     * knows where a new player belongs - and a client cannot work it out for
     * itself. A returning player is put back where they were; a new one gets
     * the start pose beside the home planet.
     */
    virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

    /** Remembers where a leaving player was, so they come back to it. */
    virtual void Logout(AController* Exiting) override;

    /** Seed phrase for this world. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Universe")
    FString UniverseSeedText = TEXT("sprint-001");

    /** How far to search for a system to spawn the player near, light years. */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Universe")
    double SystemSearchRadiusLightYears = 40.0;

    /**
     * How far to look for a system containing a habitable planet.
     *
     * Smaller than the fallback radius, because this search resolves an
     * environment for every plausible candidate - a few thousand terrain
     * evaluations each - and the point is to find *a* living world quickly,
     * not the best one in the galaxy.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Universe")
    double HabitableSearchRadiusLightYears = 60.0;

    /** Systems examined before giving up. Bounds the startup cost. */
    UPROPERTY(EditDefaultsOnly, Category = "Universe")
    int32 MaxHabitableSearchSystems = 48;

    /**
     * Where the probe starts, as a multiple of the outermost planet's own
     * radius. At 6 radii that planet fills a good part of the view while the
     * rest of the system stays visible as marked points.
     *
     * Expressed in planet radii rather than orbital radii on purpose: because
     * scaled space preserves angles, what determines whether a body reads as a
     * disc or a dot is the ratio of distance to radius, not the absolute
     * distance.
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Universe")
    double StartDistanceInPlanetRadii = 6.0;

    /**
     * The system the player is currently in.
     *
     * Since Sprint 006 this is a question for the streamer rather than a fact
     * the game mode owns: the player can leave, and the system they are in is
     * whichever one currently holds the Active state. The game mode's own
     * choice of *starting* system is HomeSystem below, and the two are the same
     * only until the player flies somewhere.
     */
    UFUNCTION(BlueprintPure, Category = "Universe")
    bool HasActiveSystem() const;

    bool GetActiveSystem(FStarSystemDescriptor& OutSystem) const;

    /** The system the world was started in. Fixed for the session. */
    const FStarSystemDescriptor& GetHomeSystem() const { return HomeSystem; }

    bool HasHomeSystem() const { return bHasHomeSystem; }

    /** The galaxy the world was started in. */
    const FGalaxyDescriptor& GetHomeGalaxy() const { return HomeGalaxy; }

    bool HasHomeGalaxy() const { return bHasHomeGalaxy; }

    /**
     * Every visible placeholder body across every streamed system, for the
     * HUD's markers.
     *
     * Gathered rather than stored: which bodies exist is the streamer's
     * business and changes as the player moves, and a cached list here would be
     * a second answer that goes stale the moment a system unloads.
     */
    void GetVisibleBodies(TArray<AAstronomicalBodyActor*>& OutBodies) const;

    /**
     * The streaming planet of the active system, if there is one.
     *
     * Forwards to the streamer. Kept on the game mode because half a dozen
     * callers ask this question and "which planet am I at" is a game-level
     * question rather than a streaming one - but the *lifetime* belongs to the
     * streamer, which is what changed in Sprint 006.
     */
    UFUNCTION(BlueprintPure, Category = "Universe")
    APlanetActor* GetPlanetActor() const;

    /**
     * Where the probe should start, and what it should be looking at.
     *
     * The probe reads this in its own BeginPlay rather than the game mode
     * pushing it after Super::StartPlay. Whether the default pawn exists before
     * or after StartPlay depends on the login path, so pulling is ordering-
     * independent in a way that pushing is not; BuildTestSystem has always run
     * by the time any actor's BeginPlay fires.
     *
     * Returns false if no system was found and there is nowhere meaningful to
     * put the probe.
     */
    bool GetProbeStartPose(FUniversePosition& OutPosition, FUniversePosition& OutLookAt) const;

private:
    /**
     * Finds a nearby system with a habitable planet, and which planet it is.
     *
     * Returns false if none is found, in which case the caller falls back to
     * the nearest system - the universe is not searched exhaustively, and a
     * region genuinely without life is a legitimate outcome rather than a bug.
     */
    bool FindHabitableSystem(
        const class UUniverseWorldSubsystem& Subsystem,
        const FUniversePosition& Centre,
        FStarSystemDescriptor& OutSystem,
        int32& OutPlanetIndex) const;

    void BuildTestSystem();

    /** Which orbit index the home system nominated as its streaming planet. */
    int32 StreamingPlanetOrbitIndex = -1;

    FStarSystemDescriptor HomeSystem;
    bool bHasHomeSystem = false;

    FGalaxyDescriptor HomeGalaxy;
    bool bHasHomeGalaxy = false;

    FUniversePosition ProbeStartPosition;
    FUniversePosition ProbeLookAtPosition;
};
