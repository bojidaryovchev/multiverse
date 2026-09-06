// Copyright Universe Project. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UniverseCoordinates.h"
#include "RemotePlayerAvatar.generated.h"

class UStaticMeshComponent;
class UUniverseAnchorComponent;
class AUniversePlayerState;

/**
 * ARemotePlayerAvatar
 *
 * What another player looks like, on this client.
 *
 *
 * WHY THIS IS SPAWNED LOCALLY AND NOT REPLICATED
 *
 * The obvious arrangement is to replicate the other player's pawn. It does not
 * work here, and the reason is the same one that shapes everything else in this
 * project: no two clients share a render origin.
 *
 * Each client rebases Unreal's origin around its own viewpoint - a thousand
 * times during a single planetary descent. A replicated pawn transform is
 * expressed in *the server's* render space, and applying it on a client places
 * the other player wherever the two origins happen to have diverged, which
 * after a light year of travel is a meaningless number.
 *
 * So the canonical position replicates as data on AUniversePlayerState, and
 * each client spawns one of these and places it through its *own*
 * UUniverseAnchorComponent. Both clients then draw the other in the right
 * place, and neither has to know anything about the other's origin.
 *
 * This is what section 42 of the sprint - "different local origins must not
 * break shared positions" - actually requires. It is not a workaround; a
 * replicated transform is simply the wrong representation for a universe this
 * large.
 *
 *
 * INTERPOLATION
 *
 * Positions arrive ten times a second. Between them the avatar advances by the
 * replicated velocity, which is exact for a ship in free flight and near enough
 * for somebody walking. It is deliberately not smoothed toward the last
 * received position: at warp the extrapolation is light-seconds long and
 * smoothing would show the other player lagging behind where they are.
 */
UCLASS()
class UNIVERSE_API ARemotePlayerAvatar : public AActor
{
    GENERATED_BODY()

public:
    ARemotePlayerAvatar();

    virtual void Tick(float DeltaSeconds) override;

    /** Binds this avatar to a player state. */
    void SetPlayerState(AUniversePlayerState* InState);

    AUniversePlayerState* GetTrackedPlayerState() const { return TrackedState.Get(); }

    /** The persistent id of the player this represents. */
    const FString& GetTrackedPersistentId() const { return TrackedPersistentId; }

    UUniverseAnchorComponent* GetAnchor() const { return Anchor; }

protected:
    UPROPERTY(VisibleAnywhere, Category = "Universe|Net")
    TObjectPtr<USceneComponent> RootScene;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Net")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

    UPROPERTY(VisibleAnywhere, Category = "Universe|Net")
    TObjectPtr<UUniverseAnchorComponent> Anchor;

private:
    TWeakObjectPtr<AUniversePlayerState> TrackedState;

    FString TrackedPersistentId;

    /** The last position received, and when. Extrapolated from. */
    FUniversePosition LastReceivedPosition;
    FVector3d LastReceivedVelocityMs = FVector3d::ZeroVector;
    double LastReceivedAtSeconds = 0.0;
    bool bHaveReceived = false;
};
