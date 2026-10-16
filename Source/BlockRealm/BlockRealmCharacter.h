#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BlockTypes.h"
#include "BlockRealmSurvival.h"
#include "BlockRealmCharacter.generated.h"

class ABlockWorld;
class AController;
class UCameraComponent;
class UPointLightComponent;
class UProceduralMeshComponent;
class USceneComponent;
class UMaterialInterface;
class UBlockRealmPlayerActionsComponent;
class USoundBase;
struct FDamageEvent;

UCLASS()
class BLOCKREALM_API ABlockRealmCharacter : public ACharacter
{
    GENERATED_BODY()

public:
    ABlockRealmCharacter(const FObjectInitializer& ObjectInitializer);
    virtual void BeginPlay() override;
    virtual void PossessedBy(AController* NewController) override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) override;
    virtual float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;

    void SetWorld(ABlockWorld* InWorld);
    // AActor::GetWorld() must keep returning UWorld; use this name for the voxel world.
    ABlockWorld* GetBlockWorld() const { return BlockWorld; }
    bool MineTarget();
    bool PlaceTarget();
    void SelectSlot(int32 Slot);
    // Inventory clicks and keyboard/wheel selection share one authoritative path.
    bool SelectInventoryBlockSlot(int32 Slot);
    bool EquipBareHands();
    bool IsBlockEquipped() const;
    bool IsBareHandsEquipped() const { return bBareHandsEquipped; }
    FString GetHeldItemName() const;
    UProceduralMeshComponent* GetHeldItemMesh() const { return HeldItemMesh; }
    bool UseInventoryFood();
    int32 GetFoodCount() const;
    void RefreshTarget();
    bool HasTarget() const { return bHasTarget; }
    FIntVector GetTargetBlock() const { return TargetBlock; }
    FIntVector GetPlacementBlock() const { return PlacementBlock; }
    EBlockType GetTargetType() const;
    EBlockType GetSelectedType() const;
    int32 GetSelectedSlot() const { return SelectedSlot; }
    int32 GetInventoryCount(int32 Slot) const;
    int32 GetCraftedPlankCount() const { return CraftedPlanks; }

    // Optional survival fields preserve compatibility with nine-slot legacy saves.
    float GetHealth() const { return Health; }
    float GetMaxHealth() const { return MaxHealth; }
    float GetHunger() const { return Hunger; }
    float GetMaxHunger() const { return MaxHunger; }
    float GetHealthPercent() const { return MaxHealth > 0.f ? Health / MaxHealth : 0.f; }
    float GetHungerPercent() const { return MaxHunger > 0.f ? Hunger / MaxHunger : 0.f; }
    void ApplySurvivalDamage(float Amount, const FString& Reason = FString());
    void RestoreSurvivalHealth(float Amount);
    void RestoreSurvivalHunger(float Amount);
    bool CanUsePlayerActions() const;
    void NotifyPlayerAction(const FString& Message);
    bool AddBlockItem(EBlockType Type, int32 Count);
    bool AddTorchItem(int32 Count);
    void WearSelectedTool();
    float GetDamageFeedbackAlpha() const;

    int32 GetToolSlotCount() const { return ToolSlots.Num(); }
    int32 GetSelectedToolSlot() const { return SelectedToolSlot; }
    EBlockRealmToolType GetSelectedToolType() const;
    FString GetSelectedToolName() const;
    int32 GetSelectedToolDurability() const;
    int32 GetSelectedToolMaxDurability() const;
    bool GetToolSlot(int32 Slot, FBlockRealmToolSlot& OutSlot) const;
    bool SelectToolSlot(int32 Slot);
    const TArray<FBlockRealmSurvivalRecipe>& GetCraftingRecipes() const;
    bool TryCraftRecipe(int32 RecipeIndex);
    bool IsInventoryOpen() const { return bInventoryOpen; }
    void ToggleInventory();
    bool IsFlying() const;
    bool IsInWater() const { return bInWater; }
    bool IsMining() const { return bMiningActive; }
    float GetMiningProgress() const;
    float GetMiningDuration() const { return bMiningActive ? MiningDuration : 0.f; }
    FString GetInteractionNotice() const;
    // Read-only smoke diagnostics distinguish input gates from legitimate placement rejection.
    FString DescribePlacementState(const FIntVector& Position) const;

    // Cached authoritative roof samples; independent of camera look direction and mesh streaming.
    bool IsLightingRoofOccluded() const { return bLightingRoofOccluded; }
    bool IsLightingCaveMouth() const { return bLightingCaveMouth; }
    bool IsLightingUnderCanopy() const { return bLightingUnderCanopy; }
    float GetLightingEnclosureWeight() const { return LightingEnclosureWeight; }
    float GetCaveFillIntensity() const { return CaveFillIntensity; }
    float GetOutdoorFillIntensity() const { return OutdoorFillIntensity; }
    float GetCaveFillTargetIntensity() const { return CaveFillTargetIntensity; }
    float GetOutdoorFillTargetIntensity() const { return OutdoorFillTargetIntensity; }

    // Menu/focus transitions cancel held actions but preserve the explicit flight request.
    void CancelGameplayInput();
    // New adventures and save restores always begin with ordinary gravity.
    void ResetGameplayState();

    FWorldSaveState CaptureSaveState() const;
    void RestoreSaveState(const FWorldSaveState& State);

private:
    bool CanPlay() const;
    bool HasGameplayFocus() const;
    bool CanUseGameplayInput() const;
    bool DoesBlockIntersectPlayer(const FIntVector& Position) const;
    void UpdateWaterState();
    void UpdateCaveFillLight();
    void UpdateMovementState(bool bRunning);
    void TickSurvival(float DeltaSeconds, bool bRunning);
    void ResetSurvivalState();
    void CycleTool(int32 Direction);
    void ToggleInventoryPressed();
    void PreviousToolPressed();
    void NextToolPressed();
    void BareHandsPressed();
    void AttackPressed();
    void EatPressed();
    bool CanUseWorldObjectAction() const;
    void FarmPressed();
    void FurnacePressed();
    void RestPressed();
    float GetCurrentMiningDuration(EBlockType Type) const;
    void ConsumeToolDurability();
    void TickMining(float DeltaSeconds);
    void CancelMining();
    void TickPlacement(float DeltaSeconds);
    void CancelPlacement();
    bool TryPlaceBlock(const FIntVector& Position, int32 Slot);
    void Turn(float Value);
    void LookUp(float Value);
    void BeginJump();
    void EndJump();
    void ToggleFlight();
    void FlightReleased();
    void MinePressed();
    void MineReleased();
    void PlacePressed();
    void PlaceReleased();
    void NextSlot();
    void PreviousSlot();
    void CycleSlot(int32 Direction);
    void PausePressed();
    void SavePressed();
    void SetInteractionNotice(const FString& Notice);
    void RefreshHeldItemVisual();
    void UpdateHeldItemVisual(float DeltaSeconds);

    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<UCameraComponent> FirstPersonCamera;
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<USceneComponent> HeldItemRoot;
    UPROPERTY(VisibleAnywhere, Category = "Camera")
    TObjectPtr<UProceduralMeshComponent> HeldItemMesh;
    UPROPERTY()
    TObjectPtr<UMaterialInterface> HeldVertexMaterial;
    UPROPERTY()
    TArray<TObjectPtr<UMaterialInterface>> HeldBlockMaterials;
    // Disabled legacy components retained for regression lookup. Roof/canopy
    // sampling continues independently; character-relative illumination is zero.
    UPROPERTY(VisibleAnywhere, Category = "Lighting")
    TObjectPtr<UPointLightComponent> CaveFillLight;
    UPROPERTY(VisibleAnywhere, Category = "Lighting")
    TObjectPtr<UPointLightComponent> OutdoorNightFillLight;
    UPROPERTY(VisibleAnywhere, Category = "Gameplay")
    TObjectPtr<UBlockRealmPlayerActionsComponent> PlayerActions;
    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;
    UPROPERTY()
    TObjectPtr<USoundBase> MineSound;
    UPROPERTY()
    TObjectPtr<USoundBase> MineStoneSound;
    UPROPERTY()
    TObjectPtr<USoundBase> MineWoodSound;
    UPROPERTY()
    TObjectPtr<USoundBase> PlaceSound;
    UPROPERTY()
    TObjectPtr<USoundBase> SlotSound;
    TArray<int32> Inventory;
    TArray<FBlockRealmToolSlot> ToolSlots;
    int32 SelectedToolSlot = INDEX_NONE;
    // Kept outside the legacy nine block slots; no inventory is moved to equip.
    bool bBareHandsEquipped = false;
    bool bPendingInventoryFood = false;
    bool bHeldVisualInitialized = false;
    float HeldMotionTime = 0.f;
    int32 CraftedPlanks = 0;
    float Health = 100.f;
    float MaxHealth = 100.f;
    float Hunger = 100.f;
    float MaxHunger = 100.f;
    float HungerDrainAccumulator = 0.f;
    float StarvationAccumulator = 0.f;
    float RegenerationAccumulator = 0.f;
    bool bInventoryOpen = false;
    uint8 InventoryMovementMode = 1;
    uint8 InventoryCustomMovementMode = 0;
    int32 SelectedSlot = 0;
    bool bHasTarget = false;
    FIntVector TargetBlock = FIntVector::ZeroValue;
    FIntVector PlacementBlock = FIntVector::ZeroValue;
    FString InteractionNotice;
    double InteractionNoticeUntil = 0.0;
    double DamageFeedbackUntil = 0.0;
    FIntVector LightingSampleCell = FIntVector(MIN_int32, MIN_int32, MIN_int32);
    FVector LastLightingPosition = FVector::ZeroVector;
    double NextLightingSampleTime = 0.0;
    double LastLightingUpdateTime = -1.0;
    bool bHasLightingSample = false;
    bool bLightingRoofOccluded = false;
    bool bLightingCaveMouth = false;
    bool bLightingUnderCanopy = false;
    float LightingEnclosureWeight = 0.f;
    float CaveFillIntensity = 0.f;
    float OutdoorFillIntensity = 0.f;
    float CaveFillTargetIntensity = 0.f;
    float OutdoorFillTargetIntensity = 0.f;
    bool bGameplayInputReady = false;
    bool bHadGameplayFocus = false;
    bool bFlightRequested = false;
    bool bFlightKeyHeld = false;
    bool bInWater = false;
    bool bWaterMovementActive = false;
    float WaterImmersion = 0.f;
    bool bMineHeld = false;
    bool bMiningActive = false;
    FIntVector MiningBlock = FIntVector::ZeroValue;
    EBlockType MiningType = EBlockType::Air;
    float MiningElapsed = 0.f;
    float MiningDuration = 0.f;
    bool bPlaceHeld = false;
    bool bPendingPlacement = false;
    bool bPlacementCollisionBlocked = false;
    FIntVector PendingPlacementBlock = FIntVector::ZeroValue;
    int32 PendingPlacementSlot = 0;
    float PlacementBufferRemaining = 0.f;
    float PlacementCooldown = 0.f;
    bool bHasPendingRestoreView = false;
    FRotator PendingRestoreView = FRotator::ZeroRotator;
};
