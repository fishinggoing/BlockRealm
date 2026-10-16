#include "BlockRealmCharacter.h"

#include "BlockRealmCrop.h"
#include "BlockRealmFurnace.h"
#include "BlockRealmGameMode.h"
#include "BlockRealmPlayerActions.h"
#include "BlockRealmRestPoint.h"
#include "BlockWorld.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    constexpr float ObjectReach = 600.f;
}

bool ABlockRealmCharacter::CanUseWorldObjectAction() const
{
    return CanUsePlayerActions() && BlockWorld && FirstPersonCamera && Controller
        && Health > 0.f && !bMineHeld && !bPlaceHeld && !bPendingPlacement;
}

void ABlockRealmCharacter::FarmPressed()
{
    if (!CanUseWorldObjectAction() || !PlayerActions) return;
    const FVector Origin = FirstPersonCamera->GetComponentLocation();
    const FVector Direction = Controller->GetControlRotation().Vector();
    if (ABlockRealmCrop* Crop = ABlockRealmCrop::FindAimedCrop(GetWorld(), BlockWorld, Origin, Direction, ObjectReach))
    {
        if (!Crop->IsMature())
        {
            SetInteractionNotice(FString::Printf(TEXT("作物正在生长：%.0f%%，成熟后按 V 收获。"),
                100.f * Crop->GetGrowthSeconds() / Crop->GetGrowthDurationSeconds()));
        }
        else SetInteractionNotice(Crop->TryHarvest(PlayerActions)
            ? TEXT("收获作物：食物 +3。") : TEXT("食物背包已满，作物保留在原处。"));
        return;
    }
    FIntVector Support;
    FIntVector Cell;
    if (!BlockWorld->TraceBlocks(Origin, Direction, ObjectReach, Support, Cell)
        || Cell != Support + FIntVector(0, 0, 1))
    {
        SetInteractionNotice(TEXT("瞄准草地或泥土的上表面，按 V 播种。"));
        return;
    }
    if (PlayerActions->GetFoodCount() < 1)
    {
        SetInteractionNotice(TEXT("播种需要 1 份食物，可以先采集树叶。"));
        return;
    }
    ABlockRealmCrop* Crop = ABlockRealmCrop::TryPlant(GetWorld(), BlockWorld, Support);
    if (!Crop)
    {
        SetInteractionNotice(TEXT("播种需要空位置和草地或泥土，不能与其他设施重叠。"));
        return;
    }
    if (!PlayerActions->TryConsumeFood(1))
    {
        Crop->Destroy();
        return;
    }
    SetInteractionNotice(TEXT("已播种 · 食物 -1，两分钟成熟后按 V 收获。"));
}

void ABlockRealmCharacter::FurnacePressed()
{
    if (!CanUseWorldObjectAction()) return;
    const FVector Origin = FirstPersonCamera->GetComponentLocation();
    const FVector Direction = Controller->GetControlRotation().Vector();
    if (ABlockRealmFurnace* Furnace = ABlockRealmFurnace::FindAimedFurnace(GetWorld(), BlockWorld, Origin, Direction, ObjectReach))
    {
        const int32 Glass = Furnace->GetOutputGlass();
        if (Glass > 0)
        {
            if (Inventory[6] > 1000000 - Glass)
            {
                SetInteractionNotice(TEXT("玻璃背包已满，成品保留在熔炉中。"));
                return;
            }
            if (Furnace->TakeGlass(Glass) == Glass)
            {
                Inventory[6] += Glass;
                SetInteractionNotice(FString::Printf(TEXT("领取玻璃 +%d。"), Glass));
            }
            return;
        }
        if (Inventory[3] < 1 || Inventory[4] < 1)
        {
            SetInteractionNotice(TEXT("每份烧制需要 1 沙块和 1 木材。"));
            return;
        }
        if (!Furnace->AddBatch(1, 1))
        {
            SetInteractionNotice(TEXT("熔炉已经装满或支撑无效。"));
            return;
        }
        --Inventory[3];
        --Inventory[4];
        SetInteractionNotice(TEXT("已投料 · 沙块 -1、木材 -1，8 秒烧成玻璃，再按 T 领取。"));
        return;
    }
    FIntVector Support;
    FIntVector Cell;
    if (!BlockWorld->TraceBlocks(Origin, Direction, ObjectReach, Support, Cell)
        || Cell != Support + FIntVector(0, 0, 1))
    {
        SetInteractionNotice(TEXT("瞄准地面的上表面，按 T 建造熔炉。"));
        return;
    }
    if (Inventory[2] < 8)
    {
        SetInteractionNotice(TEXT("建造熔炉需要 8 岩石。"));
        return;
    }
    if (DoesBlockIntersectPlayer(Cell) || !ABlockRealmFurnace::TryPlace(GetWorld(), BlockWorld, Cell))
    {
        SetInteractionNotice(TEXT("熔炉需要空位置和实体地面，不能与玩家或设施重叠。"));
        return;
    }
    Inventory[2] -= 8;
    SetInteractionNotice(TEXT("已建造熔炉 · 岩石 -8，瞄准炉子按 T 投料或领取。"));
}

void ABlockRealmCharacter::RestPressed()
{
    if (!CanUseWorldObjectAction()) return;
    const FVector Origin = FirstPersonCamera->GetComponentLocation();
    const FVector Direction = Controller->GetControlRotation().Vector();
    if (ABlockRealmRestPoint* Bed = ABlockRealmRestPoint::FindAimedRestPoint(GetWorld(), BlockWorld, Origin, Direction, ObjectReach))
    {
        if (ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this)))
        {
            Game->TryRestAt(Bed);
            SetInteractionNotice(Game->GetNotice());
        }
        return;
    }
    FIntVector Support;
    FIntVector Cell;
    if (!BlockWorld->TraceBlocks(Origin, Direction, ObjectReach, Support, Cell)
        || Cell != Support + FIntVector(0, 0, 1))
    {
        SetInteractionNotice(TEXT("瞄准地面按 B 建床，或瞄准床按 B 休息。"));
        return;
    }
    if (CraftedPlanks < 12)
    {
        SetInteractionNotice(TEXT("建床需要 12 木板，按 E 用木材制作木板。"));
        return;
    }
    const int32 Heading = (FMath::RoundToInt(FMath::UnwindDegrees(Controller->GetControlRotation().Yaw) / 90.f) % 4 + 4) % 4;
    const FIntVector Directions[] = {FIntVector(1, 0, 0), FIntVector(0, 1, 0), FIntVector(-1, 0, 0), FIntVector(0, -1, 0)};
    if (DoesBlockIntersectPlayer(Cell) || DoesBlockIntersectPlayer(Cell + Directions[Heading])
        || !ABlockRealmRestPoint::TryPlace(GetWorld(), BlockWorld, Cell, Heading))
    {
        SetInteractionNotice(TEXT("床需要连续两格空位置和实体地面，不能与玩家或设施重叠。"));
        return;
    }
    CraftedPlanks -= 12;
    SetInteractionNotice(TEXT("已建床 · 木板 -12，瞄准床按 B 设置重生点或度过夜晚。"));
}
