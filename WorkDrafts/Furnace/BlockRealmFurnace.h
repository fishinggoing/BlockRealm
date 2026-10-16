#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockRealmFurnace.generated.h"

class ABlockWorld;
class UPointLightComponent;
class USceneComponent;
class UStaticMeshComponent;
class UWorld;

/** One air-cell furnace; placement and stored items never edit authoritative voxels. */
UCLASS()
class BLOCKREALM_API ABlockRealmFurnace : public AActor
{
    GENERATED_BODY()

public:
    ABlockRealmFurnace();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    static constexpr int32 MaxPlacedFurnaces = 64;
    static constexpr int32 ItemCapacity = 64;
    static constexpr int32 MaxCookCompletionsPerTick = 8;
    static constexpr float DefaultCookDurationSeconds = 8.f;
    static constexpr float MinCookDurationSeconds = .01f;

    /**
     * Requires air, solid support directly below (excluding leaves/glass), and no placement
     * actor in this cell. Each item count is 0..64. Progress is finite 0..8 seconds;
     * an exact 8 is clamped just below completion. Nonzero progress requires sand, wood,
     * and output space. Failed placement changes neither terrain nor the caller's inventory.
     */
    static ABlockRealmFurnace* TryPlace(UWorld* World, ABlockWorld* BlockWorld,
        FIntVector Cell, int32 InputSand = 0, int32 FuelWood = 0,
        int32 OutputGlass = 0, float CookProgress = 0.f);

    /** Tests individual rendered cube bounds, then rejects authoritative voxel occlusion. */
    static ABlockRealmFurnace* FindAimedFurnace(UWorld* World, ABlockWorld* BlockWorld,
        FVector Origin, FVector Direction, float MaxDistance);

    FIntVector GetCell() const { return BlockPosition; }
    FIntVector GetBlockPosition() const { return BlockPosition; }
    ABlockWorld* GetBlockWorld() const { return BlockWorld; }
    int32 GetInputSand() const { return InputSand; }
    int32 GetFuelWood() const { return FuelWood; }
    int32 GetOutputGlass() const { return OutputGlass; }
    float GetCookProgress() const { return CookProgress; }
    float GetCookDurationSeconds() const { return CookDurationSeconds; }
    bool IsBurning() const { return bBurning; }

    /** Nonnegative counts, at least one nonzero; the whole batch fits or nothing is added. */
    bool CanAddBatch(int32 Sand, int32 Wood) const;
    bool AddBatch(int32 Sand, int32 Wood);

    /** Returns Count only if the whole positive request is available; failure returns 0. */
    int32 TakeGlass(int32 Count);

    /** Finite .01..8 seconds for isolated short tests; preserves the current cooked fraction. */
    bool SetCookDurationSeconds(float Seconds);

private:
    bool IsAnchored() const;
    bool IsStateValid() const;
    bool HasRecipeReady() const;
    bool IsCookingAllowed() const;
    void SetBurningVisuals(bool bNewBurning);

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Furnace")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Furnace")
    TArray<TObjectPtr<UStaticMeshComponent>> StoneParts;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Furnace")
    TObjectPtr<UStaticMeshComponent> FuelStack;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Furnace")
    TObjectPtr<UStaticMeshComponent> FireFace;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Furnace")
    TObjectPtr<UPointLightComponent> FurnaceLight;

    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;

    FIntVector BlockPosition = FIntVector::ZeroValue;
    int32 InputSand = 0;
    int32 FuelWood = 0;
    int32 OutputGlass = 0;
    float CookProgress = 0.f;
    float CookDurationSeconds = DefaultCookDurationSeconds;
    bool bPlacementInitialized = false;
    bool bBurning = false;
};
