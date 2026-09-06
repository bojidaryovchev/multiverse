// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UniverseNetTypes.h"
#include "UniverseNetSubsystem.generated.h"

class ARemotePlayerAvatar;
class AUniversePlayerState;

/**
 * UUniverseNetSubsystem
 *
 * Who else is here, how relevant they are, and what that costs.
 *
 *
 * INTEREST MANAGEMENT AT UNIVERSE SCALE
 *
 * Unreal's default relevancy is a distance check in world space, which is
 * useless here for two reasons: world space is a different origin on every
 * client, and the distances involved span twenty orders of magnitude. "Within
 * 15,000 units" is not a meaningful question to ask about two players who are
 * four light years apart.
 *
 * The relevant question is *structural* rather than metric: are these two
 * players in the same star system at all? A player in another system is not
 * far away in the sense that a distance number captures - they are somewhere
 * the other one cannot see, cannot reach quickly, and cannot interact with.
 * That gives the hierarchy:
 *
 *     Irrelevant   different system, or no system     - nothing sent
 *     SameSystem   same system, far apart             - position, slowly
 *     SameRegion   same planet, or nearby in space    - position and heading
 *     Visible      close enough to see                - everything, full rate
 *
 * The classes are computed on both ends from replicated canonical positions,
 * so a client can display "3 players in this system, 1 visible" without the
 * server sending it anything extra.
 *
 *
 * WHY THE AVATARS ARE LOCAL
 *
 * See ARemotePlayerAvatar. In short: no two clients share a render origin, so a
 * replicated transform means nothing, and the canonical position does.
 */
UCLASS()
class UNIVERSE_API UUniverseNetSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()

public:
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

    // --- Configuration ------------------------------------------------------

    /** Inside this, in metres, a player in the same system is SameRegion. */
    UPROPERTY(EditAnywhere, Category = "Universe|Net")
    double SameRegionRangeMeters = 1.0e9;

    /** Inside this, in metres, a player is Visible and gets an avatar. */
    UPROPERTY(EditAnywhere, Category = "Universe|Net")
    double VisibleRangeMeters = 5.0e4;

    /** Seconds between relevance re-evaluations. */
    UPROPERTY(EditAnywhere, Category = "Universe|Net")
    double EvaluationIntervalSeconds = 0.25;

    // --- Queries ------------------------------------------------------------

    /** Every remote player this client knows about, with its relevance. */
    const TArray<FRemotePlayerSnapshot>& GetRemotePlayers() const { return RemotePlayers; }

    /** How many remote players are at least this relevant. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 CountAtLeast(ENetRelevanceClass Minimum) const;

    /** How many avatars are currently spawned. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 GetAvatarCount() const { return Avatars.Num(); }

    /** The local player's persistent id, or empty before it arrives. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    FString GetLocalPersistentId() const;

    /** Relevance of one player to the local one. */
    ENetRelevanceClass ClassifyRelevance(const AUniversePlayerState& Other) const;

    // --- Diagnostics --------------------------------------------------------

    /** How many avatars have been spawned and destroyed this session. */
    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 GetAvatarSpawnCount() const { return AvatarSpawnCount; }

    UFUNCTION(BlueprintPure, Category = "Universe|Net")
    int32 GetAvatarDespawnCount() const { return AvatarDespawnCount; }

private:
    void EvaluateRelevance();

    /** Creates or destroys avatars so that exactly the Visible set has one. */
    void ReconcileAvatars();

    AUniversePlayerState* GetLocalPlayerState() const;

    UPROPERTY()
    TArray<FRemotePlayerSnapshot> RemotePlayers;

    /** Avatars, by the persistent id of the player they represent. */
    UPROPERTY()
    TMap<FString, TObjectPtr<ARemotePlayerAvatar>> Avatars;

    double TimeSinceEvaluation = 0.0;

    int32 AvatarSpawnCount = 0;
    int32 AvatarDespawnCount = 0;
};
