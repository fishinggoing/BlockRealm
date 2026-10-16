#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "BlockTypes.h"
#include "BlockRealmPlayerActions.generated.h"

class ABlockRealmCharacter;
class AActor;

/** First-person combat, food, breathing and resource drops owned by the player. */
UCLASS(ClassGroup = (BlockRealm), meta = (BlueprintSpawnableComponent))
class BLOCKREALM_API UBlockRealmPlayerActionsComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UBlockRealmPlayerActionsComponent();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType,
        FActorComponentTickFunction* ThisTickFunction) override;

    static constexpr int32 DefaultFoodCount = 6;
    static constexpr int32 MaxFoodCount = 1000000;
    static constexpr float AttackCooldownSeconds = 0.45f;
    static constexpr float MaxBreathSeconds = 15.0f;
    static constexpr float BreathRecoverySeconds = 3.0f;

    // Returns true only when a living mob actually takes damage. A permitted
    // swing always starts the cooldown, including a miss or a blocked trace.
    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Player Actions")
    bool TryAttack();

    // Restores hunger and health together. No food is spent when both are full.
    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Player Actions")
    bool TryEat();

    // Call exactly once after a block has successfully been removed and stored.
    // EBlockType is intentionally a plain C++ enum in this project.
    void OnBlockMined(EBlockType Type);

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Player Actions")
    int32 GetFoodCount() const { return FoodCount; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Player Actions")
    FString GetHitFeedback() const;

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Player Actions")
    float GetHitFeedbackAlpha() const;

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Player Actions")
    float GetAttackCooldownRemaining() const { return AttackCooldownRemaining; }

    // Remaining fraction: 1 immediately after a swing, 0 when ready.
    UFUNCTION(BlueprintPure, Category = "BlockRealm|Player Actions")
    float GetAttackCooldownPercent() const;

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Survival")
    float GetBreathSeconds() const { return BreathRemainingSeconds; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Survival")
    float GetBreathPercent() const { return BreathRemainingSeconds / MaxBreathSeconds; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Survival")
    bool IsHeadUnderwater() const { return bHeadUnderwater; }

    // Food is the only persistent field owned here; action/breathing timers,
    // immersion and partial leaf harvest reset on a new adventure or restore.
    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Player Actions")
    void ResetState();

    int32 CaptureFoodCount() const { return FoodCount; }

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Player Actions")
    void SetFoodCount(int32 Count);
    bool TryConsumeFood(int32 Count);
    bool TryAddFood(int32 Count);

private:
    ABlockRealmCharacter* GetPlayer() const;
    bool CanAct(const ABlockRealmCharacter* Player) const;
    void TickBreathing(float Elapsed, ABlockRealmCharacter* Player);
    void PickUpNearbyDrops(ABlockRealmCharacter* Player);
    static bool GetDropType(const AActor* Drop, EBlockType& OutType);

    UPROPERTY(Transient)
    int32 FoodCount = DefaultFoodCount;

    int32 MinedLeavesSinceFood = 0;
    float AttackCooldownRemaining = 0.0f;
    float HitFeedbackRemaining = 0.0f;
    float PickupScanRemaining = 0.0f;
    float BreathRemainingSeconds = MaxBreathSeconds;
    double DrowningAccumulator = 0.0;
    bool bHeadUnderwater = false;
    FString HitFeedback;
};
