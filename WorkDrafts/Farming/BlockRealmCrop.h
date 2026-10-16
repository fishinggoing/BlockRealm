#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockRealmCrop.generated.h"

class ABlockWorld;
class UBlockRealmPlayerActionsComponent;
class USceneComponent;
class UStaticMeshComponent;
class UWorld;

/** A four-stage plant rooted on a soil top face; it never edits voxel data. */
UCLASS()
class BLOCKREALM_API ABlockRealmCrop : public AActor
{
    GENERATED_BODY()

public:
    ABlockRealmCrop();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    static constexpr int32 MaxPlacedCrops = 256;
    static constexpr float DefaultGrowthDurationSeconds = 120.f;
    static constexpr int32 HarvestFoodCount = 3;

    /** Cell is always Support + (0,0,1). Restored age must be finite and in [0,120]. */
    static ABlockRealmCrop* TryPlant(UWorld* World, ABlockWorld* BlockWorld,
        FIntVector Support, float RestoredGrowthSeconds = 0.f);

    /** Hits the visible plant's narrow mesh boxes, rejecting authoritative voxel occlusion. */
    static ABlockRealmCrop* FindAimedCrop(UWorld* World, ABlockWorld* BlockWorld,
        FVector Origin, FVector Direction, float MaxDistance);

    FIntVector GetBlockPosition() const { return BlockPosition; }
    FIntVector GetSupportBlock() const { return SupportBlock; }
    ABlockWorld* GetBlockWorld() const { return BlockWorld; }
    float GetGrowthSeconds() const { return static_cast<float>(GrowthSeconds); }
    float GetGrowthDurationSeconds() const { return GrowthDurationSeconds; }
    bool IsMature() const { return !bHarvested && GrowthSeconds >= GrowthDurationSeconds; }
    bool IsHarvested() const { return bHarvested; }

    /** Finite 0 < Seconds <= 120. Preserve progress rather than instantly maturing a crop. */
    bool SetGrowthDurationSeconds(float Seconds);

    /** Complete +3 food first; only a successful addition claims and destroys the crop. */
    bool TryHarvest(UBlockRealmPlayerActionsComponent* Actions);

private:
    bool IsAnchored() const;
    bool CanGrow() const;
    void UpdateGrowthVisuals();
    void SpawnRecoveryDrop();

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Crop")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Crop")
    TObjectPtr<UStaticMeshComponent> Stem;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Crop")
    TArray<TObjectPtr<UStaticMeshComponent>> LeafMeshes;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Crop")
    TArray<TObjectPtr<UStaticMeshComponent>> GrainHeads;

    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;

    FIntVector BlockPosition = FIntVector::ZeroValue;
    FIntVector SupportBlock = FIntVector::ZeroValue;
    double GrowthSeconds = 0.0;
    float GrowthDurationSeconds = DefaultGrowthDurationSeconds;
    int32 VisualStage = INDEX_NONE;
    bool bPlacementInitialized = false;
    bool bHarvested = false;
    bool bRecoveryDropSpawned = false;
};
