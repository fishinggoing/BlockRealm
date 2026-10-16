#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockRealmTorch.generated.h"

class ABlockWorld;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UWorld;

/** A persistent placement is represented by Cell and Support, not a voxel edit. */
UCLASS()
class BLOCKREALM_API ABlockRealmTorch : public AActor
{
    GENERATED_BODY()

public:
    ABlockRealmTorch();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    static constexpr int32 MaxPlacedTorches = 512;

    /** Returns null on invalid occupancy/support, a duplicate cell, or the world limit. */
    static ABlockRealmTorch* TryPlace(UWorld* World, ABlockWorld* BlockWorld,
        FIntVector Cell, FIntVector Support);

    /** Tests the narrow rendered post/cap boxes, then rejects authoritative voxel occlusion. */
    static ABlockRealmTorch* FindAimedTorch(UWorld* World, ABlockWorld* BlockWorld,
        FVector Origin, FVector Direction, float MaxDistance);

    FIntVector GetBlockPosition() const { return BlockPosition; }
    FIntVector GetSupportBlock() const { return SupportBlock; }
    ABlockWorld* GetBlockWorld() const { return BlockWorld; }

private:
    bool IsAnchored() const;
    void SpawnRecoveryDrop();

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Torch")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Torch")
    TObjectPtr<UStaticMeshComponent> WoodPost;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Torch")
    TObjectPtr<UStaticMeshComponent> GlowCap;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Torch")
    TObjectPtr<UPointLightComponent> TorchLight;

    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;

    FIntVector BlockPosition = FIntVector::ZeroValue;
    FIntVector SupportBlock = FIntVector::ZeroValue;
    bool bPlacementInitialized = false;
    bool bRecoveryDropSpawned = false;
};
