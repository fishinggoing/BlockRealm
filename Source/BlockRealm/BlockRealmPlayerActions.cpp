#include "BlockRealmPlayerActions.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmGameMode.h"
#include "BlockRealmMob.h"
#include "BlockRealmSurvival.h"
#include "BlockWorld.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Controller.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    constexpr float AttackReachCm = 350.0f;
    constexpr float PickupReachCm = 125.0f;
    constexpr float PickupScanInterval = 0.15f;
    constexpr float HitFeedbackDuration = 0.80f;
    constexpr float FoodHungerRestore = 28.0f;
    constexpr float FoodHealthRestore = 4.0f;
    constexpr int32 LeavesPerFood = 4;
    constexpr double DrowningDamageInterval = 1.0;
    constexpr double DrowningDamagePerInterval = 2.0;

    const FName DropTag(TEXT("BlockRealmDrop"));
    const FName TorchDropTag(TEXT("BlockRealmTorchDrop"));
    const FName FoodDropTag(TEXT("BlockRealmFoodDrop"));

    float GetAttackDamage(EBlockRealmToolType Tool)
    {
        switch (Tool)
        {
        case EBlockRealmToolType::WoodenPickaxe: return 6.0f;
        case EBlockRealmToolType::StonePickaxe: return 9.0f;
        default: return 4.0f;
        }
    }
}

UBlockRealmPlayerActionsComponent::UBlockRealmPlayerActionsComponent()
{
    PrimaryComponentTick.bCanEverTick = true;
}

ABlockRealmCharacter* UBlockRealmPlayerActionsComponent::GetPlayer() const
{
    return Cast<ABlockRealmCharacter>(GetOwner());
}

bool UBlockRealmPlayerActionsComponent::CanAct(const ABlockRealmCharacter* Player) const
{
    return IsValid(Player) && Player->GetHealth() > 0.0f
        && !Player->IsInventoryOpen() && Player->CanUsePlayerActions();
}

void UBlockRealmPlayerActionsComponent::TickComponent(float DeltaTime, ELevelTick TickType,
    FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    const float Elapsed = FMath::IsFinite(DeltaTime) ? FMath::Max(DeltaTime, 0.0f) : 0.0f;
    AttackCooldownRemaining = FMath::Max(0.0f, AttackCooldownRemaining - Elapsed);
    HitFeedbackRemaining = FMath::Max(0.0f, HitFeedbackRemaining - Elapsed);
    if (HitFeedbackRemaining <= 0.0f)
    {
        HitFeedback.Reset();
    }
    PickupScanRemaining = FMath::Max(0.0f, PickupScanRemaining - Elapsed);

    ABlockRealmCharacter* Player = GetPlayer();
    TickBreathing(Elapsed, Player);
    if (!CanAct(Player) || PickupScanRemaining > 0.0f)
    {
        return;
    }
    PickupScanRemaining = PickupScanInterval;
    PickUpNearbyDrops(Player);
}

void UBlockRealmPlayerActionsComponent::TickBreathing(float Elapsed, ABlockRealmCharacter* Player)
{
    const ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    // Losing mouse focus still permits gravity and swimming. Only a real menu,
    // inventory or adventure boundary freezes this environmental survival clock.
    if (!IsValid(Player) || !FMath::IsFinite(Player->GetHealth()) || Player->GetHealth() <= 0.f
        || !Game || !Game->IsAdventureActive() || Player->IsInventoryOpen() || Elapsed <= 0.f)
    {
        return;
    }
    const ABlockWorld* BlockWorld = Player->GetBlockWorld();
    if (!IsValid(BlockWorld)) return;

    FVector EyeLocation;
    if (const UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>())
    {
        EyeLocation = Camera->GetComponentLocation();
    }
    else
    {
        FRotator EyeRotation;
        Player->GetActorEyesViewPoint(EyeLocation, EyeRotation);
    }
    if (EyeLocation.ContainsNaN()) return;
    bHeadUnderwater = BlockWorld->GetBlock(ABlockWorld::WorldToBlock(EyeLocation)) == EBlockType::Water;
    const double Seconds = static_cast<double>(Elapsed);
    if (!bHeadUnderwater)
    {
        DrowningAccumulator = 0.0;
        BreathRemainingSeconds = static_cast<float>(FMath::Min(static_cast<double>(MaxBreathSeconds),
            BreathRemainingSeconds + Seconds * MaxBreathSeconds / BreathRecoverySeconds));
        return;
    }

    const double BreathSpent = FMath::Min(static_cast<double>(BreathRemainingSeconds), Seconds);
    BreathRemainingSeconds = static_cast<float>(FMath::Max(0.0, BreathRemainingSeconds - BreathSpent));
    if (BreathRemainingSeconds > 0.f)
    {
        DrowningAccumulator = 0.0;
        return;
    }

    // Only time after the complete fifteen-second breath reserve is exhausted
    // counts toward the first damage pulse. Arithmetic catch-up handles a long
    // finite frame without an unbounded loop or repeated damage after respawn.
    const double DrowningSeconds = DrowningAccumulator + Seconds - BreathSpent;
    const double DamagePulses = FMath::FloorToDouble(DrowningSeconds / DrowningDamageInterval);
    DrowningAccumulator = FMath::Fmod(DrowningSeconds, DrowningDamageInterval);
    if (DamagePulses <= 0.0) return;
    const float Damage = static_cast<float>(FMath::Min(DamagePulses * DrowningDamagePerInterval,
        static_cast<double>(Player->GetHealth())));
    // Lethal damage synchronously calls ResetState. Do not rewrite a breath or
    // damage timer after this call, so no overdue pulse leaks into the new life.
    Player->ApplySurvivalDamage(Damage, TEXT("溺水"));
}

bool UBlockRealmPlayerActionsComponent::TryAttack()
{
    ABlockRealmCharacter* Player = GetPlayer();
    UWorld* World = GetWorld();
    if (!CanAct(Player) || !World || AttackCooldownRemaining > 0.0f)
    {
        return false;
    }

    AttackCooldownRemaining = AttackCooldownSeconds;
    HitFeedback.Reset();
    HitFeedbackRemaining = 0.0f;

    FVector ViewLocation;
    FRotator ViewRotation;
    if (const UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>())
    {
        ViewLocation = Camera->GetComponentLocation();
        ViewRotation = Player->GetController()
            ? Player->GetController()->GetControlRotation() : Camera->GetComponentRotation();
    }
    else
    {
        Player->GetActorEyesViewPoint(ViewLocation, ViewRotation);
    }
    const FVector Direction = ViewRotation.Vector().GetSafeNormal();
    if (ViewLocation.ContainsNaN() || Direction.ContainsNaN() || Direction.IsNearlyZero())
    {
        return false;
    }

    FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmPlayerAttack), false, Player);
    FHitResult Hit;
    if (!World->LineTraceSingleByChannel(Hit, ViewLocation,
        ViewLocation + Direction * AttackReachCm, ECC_Visibility, Query))
    {
        Player->NotifyPlayerAction(TEXT("攻击落空。"));
        return false;
    }
    ABlockRealmMob* Mob = Cast<ABlockRealmMob>(Hit.GetActor());
    if (!IsValid(Mob) || Mob->IsDead())
    {
        Player->NotifyPlayerAction(TEXT("没有击中怪物。"));
        return false;
    }

    // Visibility blocks ordinary occluders; this second trace also protects
    // against voxel walls whose rendered/cooked chunk collision is still pending.
    if (const ABlockWorld* BlockWorld = Player->GetBlockWorld())
    {
        FIntVector WallBlock;
        FIntVector WallAdjacent;
        if (BlockWorld->TraceBlocks(ViewLocation, Direction, Hit.Distance, WallBlock, WallAdjacent))
        {
            Player->NotifyPlayerAction(TEXT("墙体挡住了攻击。"));
            return false;
        }
    }

    const EBlockRealmToolType Tool = Player->GetSelectedToolType();
    const float HealthBefore = Mob->GetHealth();
    const float Applied = UGameplayStatics::ApplyDamage(Mob, GetAttackDamage(Tool),
        Player->GetController(), Player, UDamageType::StaticClass());
    const float DamageDealt = FMath::Min(FMath::Max(Applied, 0.0f), HealthBefore);
    if (DamageDealt <= 0.0f)
    {
        return false;
    }

    // The death delegate may destroy Mob synchronously. Do not dereference it
    // after ApplyDamage; the pre-hit health is enough for this HUD message.
    HitFeedback = FString::Printf(TEXT("%s · -%.0f 生命"),
        DamageDealt >= HealthBefore ? TEXT("击败怪物") : TEXT("命中怪物"), DamageDealt);
    HitFeedbackRemaining = HitFeedbackDuration;
    if (Tool == EBlockRealmToolType::WoodenPickaxe || Tool == EBlockRealmToolType::StonePickaxe)
    {
        Player->WearSelectedTool();
        if (Player->GetSelectedToolType() != Tool)
        {
            HitFeedback += FString::Printf(TEXT(" · %s已损坏"),
                *FBlockRealmSurvivalData::GetToolDisplayName(Tool));
        }
    }
    Player->NotifyPlayerAction(HitFeedback);
    return true;
}

bool UBlockRealmPlayerActionsComponent::TryEat()
{
    ABlockRealmCharacter* Player = GetPlayer();
    if (!CanAct(Player))
    {
        return false;
    }
    if (Player->GetHunger() >= Player->GetMaxHunger()
        && Player->GetHealth() >= Player->GetMaxHealth())
    {
        Player->NotifyPlayerAction(TEXT("生命和饥饿已满，无需进食。"));
        return false;
    }
    if (FoodCount <= 0)
    {
        Player->NotifyPlayerAction(TEXT("没有食物，采集树叶可以补充。"));
        return false;
    }

    const float HungerBefore = Player->GetHunger();
    const float HealthBefore = Player->GetHealth();
    --FoodCount;
    Player->RestoreSurvivalHunger(FoodHungerRestore);
    Player->RestoreSurvivalHealth(FoodHealthRestore);
    Player->NotifyPlayerAction(FString::Printf(TEXT("已进食：+%.0f 饥饿，+%.0f 生命。"),
        FMath::Max(0.0f, Player->GetHunger() - HungerBefore),
        FMath::Max(0.0f, Player->GetHealth() - HealthBefore)));
    return true;
}

void UBlockRealmPlayerActionsComponent::OnBlockMined(EBlockType Type)
{
    ABlockRealmCharacter* Player = GetPlayer();
    if (Type != EBlockType::Leaves || !CanAct(Player))
    {
        return;
    }
    ++MinedLeavesSinceFood;
    if (MinedLeavesSinceFood < LeavesPerFood)
    {
        return;
    }
    MinedLeavesSinceFood = 0;
    if (FoodCount < MaxFoodCount)
    {
        ++FoodCount;
        Player->NotifyPlayerAction(TEXT("树叶中找到食物 +1。"));
    }
}

bool UBlockRealmPlayerActionsComponent::GetDropType(const AActor* Drop, EBlockType& OutType)
{
    if (!IsValid(Drop) || !Drop->ActorHasTag(DropTag))
    {
        return false;
    }
    // SpawnDrop currently labels these actors with Chinese BlockDisplayName.
    // Unknown tagged actors remain in the world rather than becoming air/items.
    for (const EBlockType Type : { EBlockType::Stone, EBlockType::Glow })
    {
        if (Drop->ActorHasTag(FName(*BlockDisplayName(Type))))
        {
            OutType = Type;
            return true;
        }
    }
    return false;
}

void UBlockRealmPlayerActionsComponent::PickUpNearbyDrops(ABlockRealmCharacter* Player)
{
    UWorld* World = GetWorld();
    if (!World || !CanAct(Player))
    {
        return;
    }
    const FVector PlayerLocation = Player->GetActorLocation();
    int32 PickedStone = 0;
    int32 PickedGlow = 0;
    int32 PickedTorches = 0;
    int32 PickedFood = 0;
    for (TActorIterator<AActor> It(World); It; ++It)
    {
        AActor* Drop = *It;
        EBlockType Type = EBlockType::Air;
        const bool bTorchDrop = IsValid(Drop) && Drop->ActorHasTag(TorchDropTag);
        const bool bFoodDrop = IsValid(Drop) && Drop->ActorHasTag(FoodDropTag);
        if (!bTorchDrop && !bFoodDrop && !GetDropType(Drop, Type))
        {
            continue;
        }
        const FVector DropLocation = Drop->GetActorLocation();
        if (DropLocation.ContainsNaN()
            || FVector::DistSquared(PlayerLocation, DropLocation) > FMath::Square(PickupReachCm))
        {
            continue;
        }
        // The owner must report a complete, successful addition. Full/invalid
        // inventories leave the drop available; existing slot counts stay intact.
        if (!(bFoodDrop ? TryAddFood(1) : bTorchDrop ? Player->AddTorchItem(1) : Player->AddBlockItem(Type, 1)))
        {
            continue;
        }
        // Remove the pickup marker first to prevent a duplicate credit even if
        // Destroy is deferred or an unusual actor refuses destruction.
        Drop->Tags.Remove(DropTag);
        Drop->Tags.Remove(TorchDropTag);
        Drop->Tags.Remove(FoodDropTag);
        Drop->Destroy();
        if (bFoodDrop)
        {
            ++PickedFood;
        }
        else if (bTorchDrop)
        {
            ++PickedTorches;
        }
        else if (Type == EBlockType::Stone)
        {
            ++PickedStone;
        }
        else
        {
            ++PickedGlow;
        }
    }
    if (PickedStone > 0 || PickedGlow > 0 || PickedTorches > 0 || PickedFood > 0)
    {
        FString Notice(TEXT("拾取："));
        if (PickedStone > 0)
        {
            Notice += FString::Printf(TEXT("岩石 +%d"), PickedStone);
        }
        if (PickedGlow > 0)
        {
            if (PickedStone > 0) Notice += TEXT("，");
            Notice += FString::Printf(TEXT("辉光石 +%d"), PickedGlow);
        }
        if (PickedTorches > 0)
        {
            if (PickedStone > 0 || PickedGlow > 0) Notice += TEXT("，");
            Notice += FString::Printf(TEXT("火把 +%d"), PickedTorches);
        }
        if (PickedFood > 0)
        {
            if (PickedStone > 0 || PickedGlow > 0 || PickedTorches > 0) Notice += TEXT("，");
            Notice += FString::Printf(TEXT("食物 +%d"), PickedFood);
        }
        Player->NotifyPlayerAction(Notice + TEXT("。"));
    }
}

FString UBlockRealmPlayerActionsComponent::GetHitFeedback() const
{
    return HitFeedbackRemaining > 0.0f ? HitFeedback : FString();
}

float UBlockRealmPlayerActionsComponent::GetHitFeedbackAlpha() const
{
    return FMath::Clamp(HitFeedbackRemaining / HitFeedbackDuration, 0.0f, 1.0f);
}

float UBlockRealmPlayerActionsComponent::GetAttackCooldownPercent() const
{
    return FMath::Clamp(AttackCooldownRemaining / AttackCooldownSeconds, 0.0f, 1.0f);
}

void UBlockRealmPlayerActionsComponent::ResetState()
{
    FoodCount = DefaultFoodCount;
    MinedLeavesSinceFood = 0;
    AttackCooldownRemaining = 0.0f;
    HitFeedbackRemaining = 0.0f;
    PickupScanRemaining = 0.0f;
    BreathRemainingSeconds = MaxBreathSeconds;
    DrowningAccumulator = 0.0;
    bHeadUnderwater = false;
    HitFeedback.Reset();
}

void UBlockRealmPlayerActionsComponent::SetFoodCount(int32 Count)
{
    FoodCount = FMath::Clamp(Count, 0, MaxFoodCount);
}

bool UBlockRealmPlayerActionsComponent::TryConsumeFood(int32 Count)
{
    if (Count <= 0 || Count > FoodCount) return false;
    FoodCount -= Count;
    return true;
}

bool UBlockRealmPlayerActionsComponent::TryAddFood(int32 Count)
{
    if (Count <= 0 || Count > MaxFoodCount - FoodCount) return false;
    FoodCount += Count;
    return true;
}
