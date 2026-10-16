#include "BlockRealmSurvival.h"

namespace
{
    void AddStack(TArray<FBlockRealmItemStack>& Stacks, EBlockRealmItemType Item, int32 Count)
    {
        FBlockRealmItemStack Stack;
        Stack.Item = Item;
        Stack.Count = Count;
        Stacks.Add(Stack);
    }

    int32 ToolQuantity(const TArray<FBlockRealmToolSlot>& Tools, EBlockRealmToolType Type)
    {
        int32 Total = 0;
        for (const FBlockRealmToolSlot& Slot : Tools)
        {
            if (Slot.Type == Type)
            {
                Total = FMath::Clamp(Total + FMath::Max(0, Slot.Quantity), 0, INT32_MAX);
            }
        }
        return Total;
    }

}

void FBlockRealmFallTracker::Begin(double HeightCm)
{
    Reset();
    if (FMath::IsFinite(HeightCm))
    {
        bTracking = true;
        PeakHeightCm = HeightCm;
    }
}

void FBlockRealmFallTracker::Sample(double HeightCm)
{
    if (!FMath::IsFinite(HeightCm))
    {
        Reset();
        return;
    }
    if (bTracking) PeakHeightCm = FMath::Max(PeakHeightCm, HeightCm);
}

void FBlockRealmFallTracker::Reset()
{
    bTracking = false;
    PeakHeightCm = 0.0;
}

double FBlockRealmFallTracker::GetFallDistance(double LandingHeightCm) const
{
    return bTracking && FMath::IsFinite(LandingHeightCm)
        ? FMath::Max(0.0, PeakHeightCm - LandingHeightCm) : 0.0;
}

float FBlockRealmFallTracker::Land(double LandingHeightCm)
{
    const float Damage = FBlockRealmSurvivalData::CalculateFallDamage(GetFallDistance(LandingHeightCm));
    Reset();
    return Damage;
}

const TArray<FBlockRealmSurvivalRecipe>& FBlockRealmSurvivalData::GetCraftingRecipes()
{
    static const TArray<FBlockRealmSurvivalRecipe> Recipes = []()
    {
        TArray<FBlockRealmSurvivalRecipe> Out;

        FBlockRealmSurvivalRecipe Planks;
        Planks.Id = TEXT("wood_planks");
        Planks.DisplayName = TEXT("木板");
        AddStack(Planks.Ingredients, EBlockRealmItemType::Wood, 1);
        AddStack(Planks.Results, EBlockRealmItemType::Planks, 4);
        Out.Add(MoveTemp(Planks));

        FBlockRealmSurvivalRecipe WoodenPickaxe;
        WoodenPickaxe.Id = TEXT("wooden_pickaxe");
        WoodenPickaxe.DisplayName = TEXT("木镐");
        AddStack(WoodenPickaxe.Ingredients, EBlockRealmItemType::Planks, 3);
        AddStack(WoodenPickaxe.Ingredients, EBlockRealmItemType::Wood, 1);
        AddStack(WoodenPickaxe.Results, EBlockRealmItemType::WoodenPickaxe, 1);
        Out.Add(MoveTemp(WoodenPickaxe));

        FBlockRealmSurvivalRecipe StonePickaxe;
        StonePickaxe.Id = TEXT("stone_pickaxe");
        StonePickaxe.DisplayName = TEXT("石镐");
        AddStack(StonePickaxe.Ingredients, EBlockRealmItemType::Stone, 3);
        AddStack(StonePickaxe.Ingredients, EBlockRealmItemType::Wood, 1);
        AddStack(StonePickaxe.Results, EBlockRealmItemType::StonePickaxe, 1);
        Out.Add(MoveTemp(StonePickaxe));

        FBlockRealmSurvivalRecipe Torch;
        Torch.Id = TEXT("torch");
        Torch.DisplayName = TEXT("火把");
        AddStack(Torch.Ingredients, EBlockRealmItemType::Wood, 1);
        AddStack(Torch.Ingredients, EBlockRealmItemType::Glow, 1);
        AddStack(Torch.Results, EBlockRealmItemType::Torch, 4);
        Out.Add(MoveTemp(Torch));

        FBlockRealmSurvivalRecipe Glass;
        Glass.Id = TEXT("glass");
        Glass.DisplayName = TEXT("玻璃");
        AddStack(Glass.Ingredients, EBlockRealmItemType::Sand, 1);
        AddStack(Glass.Ingredients, EBlockRealmItemType::Glow, 1);
        AddStack(Glass.Results, EBlockRealmItemType::Glass, 1);
        Out.Add(MoveTemp(Glass));

        return Out;
    }();
    return Recipes;
}

FString FBlockRealmSurvivalData::GetToolDisplayName(EBlockRealmToolType Type)
{
    switch (Type)
    {
    case EBlockRealmToolType::WoodenPickaxe: return TEXT("木镐");
    case EBlockRealmToolType::StonePickaxe: return TEXT("石镐");
    case EBlockRealmToolType::Torch: return TEXT("火把");
    default: return TEXT("徒手");
    }
}

FString FBlockRealmSurvivalData::GetItemDisplayName(EBlockRealmItemType Item)
{
    switch (Item)
    {
    case EBlockRealmItemType::Planks: return TEXT("木板");
    case EBlockRealmItemType::WoodenPickaxe: return TEXT("木镐");
    case EBlockRealmItemType::StonePickaxe: return TEXT("石镐");
    case EBlockRealmItemType::Torch: return TEXT("火把");
    case EBlockRealmItemType::Grass: return BlockDisplayName(EBlockType::Grass);
    case EBlockRealmItemType::Dirt: return BlockDisplayName(EBlockType::Dirt);
    case EBlockRealmItemType::Stone: return BlockDisplayName(EBlockType::Stone);
    case EBlockRealmItemType::Sand: return BlockDisplayName(EBlockType::Sand);
    case EBlockRealmItemType::Wood: return BlockDisplayName(EBlockType::Wood);
    case EBlockRealmItemType::Leaves: return BlockDisplayName(EBlockType::Leaves);
    case EBlockRealmItemType::Glass: return BlockDisplayName(EBlockType::Glass);
    case EBlockRealmItemType::Glow: return BlockDisplayName(EBlockType::Glow);
    default: return TEXT("未知物品");
    }
}

FString FBlockRealmSurvivalData::GetMiningRequirementText(EBlockType Material)
{
    switch (Material)
    {
    case EBlockType::Grass:
    case EBlockType::Dirt:
    case EBlockType::Sand:
    case EBlockType::Wood:
    case EBlockType::Leaves:
        return TEXT("可徒手采集");
    case EBlockType::Stone:
        return TEXT("需要木镐或石镐");
    case EBlockType::Glow:
        return TEXT("需要石镐");
    case EBlockType::Glass:
        return TEXT("需要石镐完整回收");
    case EBlockType::Water:
        return TEXT("水不能采集");
    default:
        return TEXT("没有可采集的方块");
    }
}

bool FBlockRealmSurvivalData::CanMineBlock(EBlockType Material, EBlockRealmToolType Tool,
    FString* OutReason)
{
    if (OutReason) OutReason->Empty();
    const auto Deny = [OutReason](const FString& Reason)
    {
        if (OutReason) *OutReason = Reason;
        return false;
    };
    if (Tool == EBlockRealmToolType::Torch)
        return Deny(TEXT("火把不能挖掘方块，请切换徒手或合适的镐。"));
    if (Tool != EBlockRealmToolType::None && Tool != EBlockRealmToolType::WoodenPickaxe
        && Tool != EBlockRealmToolType::StonePickaxe)
        return Deny(TEXT("当前物品不能挖掘方块。"));

    switch (Material)
    {
    case EBlockType::Grass:
    case EBlockType::Dirt:
    case EBlockType::Sand:
    case EBlockType::Wood:
    case EBlockType::Leaves:
        return true;
    case EBlockType::Stone:
        if (Tool == EBlockRealmToolType::WoodenPickaxe || Tool == EBlockRealmToolType::StonePickaxe)
            return true;
        return Deny(TEXT("岩石需要木镐或石镐，徒手无法开采。"));
    case EBlockType::Glow:
        if (Tool == EBlockRealmToolType::StonePickaxe) return true;
        return Deny(TEXT("辉光石需要石镐，先用木镐采集岩石并制作石镐。"));
    case EBlockType::Glass:
        if (Tool == EBlockRealmToolType::StonePickaxe) return true;
        return Deny(TEXT("玻璃需要石镐完整回收。"));
    default:
        return Deny(GetMiningRequirementText(Material) + TEXT("。"));
    }
}

float FBlockRealmSurvivalData::GetMiningDuration(EBlockType Material, EBlockRealmToolType Tool)
{
    if (!CanMineBlock(Material, Tool)) return 0.f;
    float BaseDuration = 0.f;
    switch (Material)
    {
    case EBlockType::Leaves: BaseDuration = .25f; break;
    case EBlockType::Sand: BaseDuration = .35f; break;
    case EBlockType::Grass: BaseDuration = .4f; break;
    case EBlockType::Dirt: BaseDuration = .45f; break;
    case EBlockType::Glass: BaseDuration = .5f; break;
    case EBlockType::Wood: BaseDuration = .75f; break;
    case EBlockType::Stone: BaseDuration = 1.1f; break;
    case EBlockType::Glow: BaseDuration = 1.3f; break;
    default: return 0.f;
    }

    switch (Tool)
    {
    case EBlockRealmToolType::WoodenPickaxe:
        if (Material == EBlockType::Stone || Material == EBlockType::Glow || Material == EBlockType::Glass)
            return BaseDuration * .52f;
        if (Material == EBlockType::Wood || Material == EBlockType::Leaves)
            return BaseDuration * .78f;
        return BaseDuration * .9f;
    case EBlockRealmToolType::StonePickaxe:
        if (Material == EBlockType::Stone || Material == EBlockType::Glow || Material == EBlockType::Glass)
            return BaseDuration * .30f;
        if (Material == EBlockType::Wood || Material == EBlockType::Leaves)
            return BaseDuration * .62f;
        return BaseDuration * .78f;
    case EBlockRealmToolType::Torch:
        // A torch is a utility item and cannot be used as a mining tool.
        return 0.f;
    default:
        return BaseDuration;
    }
}

float FBlockRealmSurvivalData::CalculateFallDamage(double FallDistanceCm)
{
    if (!FMath::IsFinite(FallDistanceCm) || FallDistanceCm <= 300.0) return 0.f;
    return static_cast<float>(FMath::Clamp((FallDistanceCm - 300.0) * .1, 0.0, 100.0));
}

int32 FBlockRealmSurvivalData::GetInventorySlot(EBlockRealmItemType Item)
{
    switch (Item)
    {
    case EBlockRealmItemType::Grass: return 0;
    case EBlockRealmItemType::Dirt: return 1;
    case EBlockRealmItemType::Stone: return 2;
    case EBlockRealmItemType::Sand: return 3;
    case EBlockRealmItemType::Wood: return 4;
    case EBlockRealmItemType::Leaves: return 5;
    case EBlockRealmItemType::Glass: return 6;
    // Water remains a generated/reserved slot in the legacy nine-slot format.
    case EBlockRealmItemType::Glow: return 8;
    default: return INDEX_NONE;
    }
}

int32 FBlockRealmSurvivalData::CountItem(EBlockRealmItemType Item, const TArray<int32>& Inventory,
    const TArray<FBlockRealmToolSlot>& Tools, int32 Planks)
{
    if (Item == EBlockRealmItemType::Planks)
    {
        return FMath::Max(0, Planks);
    }
    if (Item == EBlockRealmItemType::WoodenPickaxe || Item == EBlockRealmItemType::StonePickaxe)
    {
        return ToolQuantity(Tools, Item == EBlockRealmItemType::WoodenPickaxe
            ? EBlockRealmToolType::WoodenPickaxe : EBlockRealmToolType::StonePickaxe);
    }
    if (Item == EBlockRealmItemType::Torch)
    {
        return ToolQuantity(Tools, EBlockRealmToolType::Torch);
    }
    const int32 Slot = GetInventorySlot(Item);
    return Inventory.IsValidIndex(Slot) ? FMath::Max(0, Inventory[Slot]) : 0;
}

bool FBlockRealmSurvivalData::ConsumeItem(EBlockRealmItemType Item, int32 Count,
    TArray<int32>& Inventory, TArray<FBlockRealmToolSlot>& Tools, int32& Planks)
{
    if (Count <= 0) return true;
    if (Item == EBlockRealmItemType::Planks)
    {
        if (Planks < Count) return false;
        Planks -= Count;
        return true;
    }
    if (Item == EBlockRealmItemType::WoodenPickaxe || Item == EBlockRealmItemType::StonePickaxe
        || Item == EBlockRealmItemType::Torch)
    {
        const EBlockRealmToolType Tool = Item == EBlockRealmItemType::WoodenPickaxe
            ? EBlockRealmToolType::WoodenPickaxe
            : Item == EBlockRealmItemType::StonePickaxe ? EBlockRealmToolType::StonePickaxe
            : EBlockRealmToolType::Torch;
        int32 Remaining = Count;
        for (FBlockRealmToolSlot& Slot : Tools)
        {
            if (Slot.Type != Tool || Slot.Quantity <= 0) continue;
            const int32 Taken = FMath::Min(Remaining, Slot.Quantity);
            Slot.Quantity -= Taken;
            Remaining -= Taken;
            if (Slot.Quantity == 0)
            {
                Slot = FBlockRealmToolSlot();
            }
            if (Remaining == 0) return true;
        }
        return false;
    }
    const int32 Slot = GetInventorySlot(Item);
    if (!Inventory.IsValidIndex(Slot) || Inventory[Slot] < Count) return false;
    Inventory[Slot] -= Count;
    return true;
}

bool FBlockRealmSurvivalData::AddItem(EBlockRealmItemType Item, int32 Count,
    TArray<int32>& Inventory, TArray<FBlockRealmToolSlot>& Tools, int32& Planks)
{
    if (Count <= 0) return true;
    if (Item == EBlockRealmItemType::Planks)
    {
        if (Planks < 0 || Count > 1000000 - Planks) return false;
        Planks += Count;
        return true;
    }
    if (Item == EBlockRealmItemType::WoodenPickaxe || Item == EBlockRealmItemType::StonePickaxe)
    {
        const EBlockRealmToolType Tool = Item == EBlockRealmItemType::WoodenPickaxe
            ? EBlockRealmToolType::WoodenPickaxe : EBlockRealmToolType::StonePickaxe;
        const int32 MaxDurability = Item == EBlockRealmItemType::WoodenPickaxe
            ? WoodenPickaxeDurability : StonePickaxeDurability;
        for (FBlockRealmToolSlot& Slot : Tools)
        {
            if (Slot.Type == EBlockRealmToolType::None)
            {
                Slot.Type = Tool;
                Slot.Durability = MaxDurability;
                Slot.MaxDurability = MaxDurability;
                Slot.Quantity = 1;
                return true;
            }
        }
        return false;
    }
    if (Item == EBlockRealmItemType::Torch)
    {
        int32 Remaining = Count;
        for (FBlockRealmToolSlot& Slot : Tools)
        {
            if (Slot.Type != EBlockRealmToolType::Torch || Slot.Quantity >= MaxToolStack) continue;
            const int32 Added = FMath::Min(Remaining, MaxToolStack - Slot.Quantity);
            Slot.Quantity += Added;
            Remaining -= Added;
            if (Remaining == 0) return true;
        }
        for (FBlockRealmToolSlot& Slot : Tools)
        {
            if (Slot.Type == EBlockRealmToolType::None)
            {
                Slot.Type = EBlockRealmToolType::Torch;
                Slot.Durability = 0;
                Slot.MaxDurability = 0;
                Slot.Quantity = FMath::Min(Remaining, MaxToolStack);
                Remaining -= Slot.Quantity;
                if (Remaining == 0) return true;
            }
        }
        return Remaining == 0;
    }
    const int32 Slot = GetInventorySlot(Item);
    if (!Inventory.IsValidIndex(Slot)) return false;
    const int64 NewCount = static_cast<int64>(Inventory[Slot]) + static_cast<int64>(Count);
    if (NewCount < 0 || NewCount > 1000000) return false;
    Inventory[Slot] = static_cast<int32>(NewCount);
    return true;
}

bool FBlockRealmSurvivalData::TryAddTorches(int32 Count, TArray<FBlockRealmToolSlot>& Tools)
{
    if (Count <= 0 || Count > ToolSlotCount * MaxToolStack || Tools.Num() != ToolSlotCount) return false;
    TArray<FBlockRealmToolSlot> UpdatedTools = Tools;
    TArray<int32> UnusedInventory;
    int32 UnusedPlanks = 0;
    if (!AddItem(EBlockRealmItemType::Torch, Count, UnusedInventory, UpdatedTools, UnusedPlanks)) return false;
    Tools = MoveTemp(UpdatedTools);
    return true;
}

bool FBlockRealmSurvivalData::TryCraft(int32 RecipeIndex, TArray<int32>& Inventory,
    TArray<FBlockRealmToolSlot>& Tools, int32& Planks, FString* OutReason)
{
    const TArray<FBlockRealmSurvivalRecipe>& Recipes = GetCraftingRecipes();
    auto Fail = [OutReason](const FString& Reason)
    {
        if (OutReason) *OutReason = Reason;
        return false;
    };
    if (!Recipes.IsValidIndex(RecipeIndex)) return Fail(TEXT("配方不存在。"));
    const FBlockRealmSurvivalRecipe& Recipe = Recipes[RecipeIndex];
    if (Inventory.Num() < 9) Inventory.SetNumZeroed(9);
    if (Tools.Num() < ToolSlotCount) Tools.SetNum(ToolSlotCount);
    for (const FBlockRealmItemStack& Ingredient : Recipe.Ingredients)
    {
        if (Ingredient.Count <= 0) continue;
        if (CountItem(Ingredient.Item, Inventory, Tools, Planks) < Ingredient.Count)
        {
            return Fail(FString::Printf(TEXT("材料不足：%s。"), *GetItemDisplayName(Ingredient.Item)));
        }
    }
    // Validate outputs before consuming anything, keeping failed crafts atomic.
    TArray<int32> TestInventory = Inventory;
    TArray<FBlockRealmToolSlot> TestTools = Tools;
    int32 TestPlanks = Planks;
    for (const FBlockRealmItemStack& Ingredient : Recipe.Ingredients)
    {
        if (!ConsumeItem(Ingredient.Item, Ingredient.Count, TestInventory, TestTools, TestPlanks))
            return Fail(TEXT("材料状态发生变化，请重试。"));
    }
    for (const FBlockRealmItemStack& Result : Recipe.Results)
    {
        if (!AddItem(Result.Item, Result.Count, TestInventory, TestTools, TestPlanks))
            return Fail(TEXT("背包或工具栏没有空间。"));
    }
    Inventory = MoveTemp(TestInventory);
    Tools = MoveTemp(TestTools);
    Planks = TestPlanks;
    if (OutReason) *OutReason = FString::Printf(TEXT("已制作：%s。"), *Recipe.DisplayName);
    return true;
}

bool FBlockRealmSurvivalData::RunRuleSelfTests(TArray<FString>& OutFailures)
{
    OutFailures.Reset();
    const auto Check = [&OutFailures](bool bCondition, const TCHAR* Description)
    {
        if (!bCondition) OutFailures.Add(Description);
    };
    const EBlockRealmToolType ToolsToTest[] = {EBlockRealmToolType::None,
        EBlockRealmToolType::WoodenPickaxe, EBlockRealmToolType::StonePickaxe,
        EBlockRealmToolType::Torch};
    for (int32 MaterialIndex = 0; MaterialIndex <= static_cast<int32>(EBlockType::Glow); ++MaterialIndex)
    {
        const EBlockType Material = static_cast<EBlockType>(MaterialIndex);
        for (const EBlockRealmToolType Tool : ToolsToTest)
        {
            const bool bSoft = Material == EBlockType::Grass || Material == EBlockType::Dirt
                || Material == EBlockType::Sand || Material == EBlockType::Wood
                || Material == EBlockType::Leaves;
            const bool bExpected = Tool != EBlockRealmToolType::Torch && (bSoft
                || (Material == EBlockType::Stone && Tool != EBlockRealmToolType::None)
                || ((Material == EBlockType::Glow || Material == EBlockType::Glass)
                    && Tool == EBlockRealmToolType::StonePickaxe));
            FString Reason = TEXT("stale reason");
            const bool bAllowed = CanMineBlock(Material, Tool, &Reason);
            if (bAllowed != bExpected || (GetMiningDuration(Material, Tool) > 0.f) != bExpected
                || (bAllowed ? !Reason.IsEmpty() : Reason.IsEmpty()))
            {
                OutFailures.Add(FString::Printf(TEXT("Mining matrix material=%d tool=%d allowed=%d expected=%d reason=%s"),
                    MaterialIndex, static_cast<int32>(Tool), bAllowed, bExpected, *Reason));
            }
        }
    }
    Check(!CanMineBlock(static_cast<EBlockType>(255), EBlockRealmToolType::StonePickaxe),
        TEXT("Unknown block types cannot be mined"));
    Check(!CanMineBlock(EBlockType::Wood, static_cast<EBlockRealmToolType>(255)),
        TEXT("Unknown tools cannot bypass mining permissions"));
    Check(GetMiningDuration(EBlockType::Stone, EBlockRealmToolType::StonePickaxe)
        < GetMiningDuration(EBlockType::Stone, EBlockRealmToolType::WoodenPickaxe),
        TEXT("Stone pickaxe is faster than wooden pickaxe on stone"));

    // Prove that permissions do not create a crafting dead end, even when the
    // test starts with empty block/tool inventories instead of the starter kit.
    TArray<int32> Inventory;
    Inventory.Init(0, 9);
    TArray<FBlockRealmToolSlot> Tools;
    Tools.Init(FBlockRealmToolSlot(), ToolSlotCount);
    int32 Planks = 0;
    const auto Collect = [&Inventory](EBlockType Material, EBlockRealmToolType Tool, int32 Count)
    {
        if (!CanMineBlock(Material, Tool) || Count <= 0) return false;
        const int32 Slot = static_cast<int32>(Material) - 1;
        if (!Inventory.IsValidIndex(Slot)) return false;
        Inventory[Slot] += Count;
        return true;
    };
    Check(Collect(EBlockType::Wood, EBlockRealmToolType::None, 4), TEXT("Hand collection starts the wood loop"));
    Check(TryCraft(0, Inventory, Tools, Planks), TEXT("Wood crafts into planks"));
    Check(TryCraft(1, Inventory, Tools, Planks), TEXT("Hand-collected wood crafts a wooden pickaxe"));
    Check(Tools[0].Type == EBlockRealmToolType::WoodenPickaxe && Tools[0].IsUsable(),
        TEXT("Wooden pickaxe craft supplies a usable tool"));
    const int32 StoneBeforeDeniedCollection = Inventory[2];
    Check(!Collect(EBlockType::Stone, EBlockRealmToolType::None, 3)
        && Inventory[2] == StoneBeforeDeniedCollection, TEXT("Hand-mined stone supplies no crafting material"));
    Check(Collect(EBlockType::Stone, EBlockRealmToolType::WoodenPickaxe, 3),
        TEXT("Wooden pickaxe unlocks stone"));
    Check(TryCraft(2, Inventory, Tools, Planks), TEXT("Stone unlocks a stone pickaxe"));
    Check(Tools[1].Type == EBlockRealmToolType::StonePickaxe && Tools[1].IsUsable(),
        TEXT("Stone pickaxe craft supplies a usable tool"));
    Check(!Collect(EBlockType::Glow, EBlockRealmToolType::WoodenPickaxe, 1),
        TEXT("Wooden pickaxe cannot skip the stone tier"));
    Check(Collect(EBlockType::Glow, EBlockRealmToolType::StonePickaxe, 2)
        && Collect(EBlockType::Sand, EBlockRealmToolType::None, 1),
        TEXT("Stone tier unlocks glow while sand remains hand collectible"));
    Check(TryCraft(3, Inventory, Tools, Planks), TEXT("Progression produces torches"));
    Check(TryCraft(4, Inventory, Tools, Planks) && Inventory[6] == 1,
        TEXT("Progression produces glass"));
    Check(!Collect(EBlockType::Glass, EBlockRealmToolType::WoodenPickaxe, 1)
        && Collect(EBlockType::Glass, EBlockRealmToolType::StonePickaxe, 1)
        && Inventory[6] == 2, TEXT("Only stone pickaxes recover intact glass"));

    Check(CalculateFallDamage(-100.0) == 0.f && CalculateFallDamage(300.0) == 0.f,
        TEXT("The first three blocks are safe"));
    Check(FMath::IsNearlyEqual(CalculateFallDamage(350.0), 5.f)
        && FMath::IsNearlyEqual(CalculateFallDamage(400.0), 10.f),
        TEXT("Fall damage scales by ten health per excess block"));
    Check(CalculateFallDamage(2000.0) == 100.f, TEXT("Fall damage is capped at full health"));
    FBlockRealmFallTracker Fall;
    Fall.Begin(1000.0);
    Check(Fall.Land(700.0) == 0.f, TEXT("Exactly three blocks do not hurt"));
    Check(Fall.Land(0.0) == 0.f, TEXT("A completed fall cannot deal damage twice"));
    Fall.Begin(1000.0);
    Fall.Sample(1127.0);
    Check(Fall.Land(1000.0) == 0.f, TEXT("Normal jump height is safe"));
    Fall.Begin(1000.0);
    Check(Fall.Land(0.0) == 70.f, TEXT("A long frame crossing the entire fall still deals damage"));
    Fall.Begin(1000.0);
    Fall.Sample(1500.0);
    Fall.Sample(1250.0);
    Check(Fall.Land(1000.0) == 20.f, TEXT("A fall is measured from its actual peak"));
    Fall.Begin(1000.0);
    Fall.Sample(900.0);
    // No samples are added while physics is paused. Elapsed wall time is never
    // an input, so resuming at the same location cannot invent fall distance.
    Check(Fall.Land(600.0) == 10.f, TEXT("A pause gap does not change fall distance"));
    Fall.Begin(5000.0);
    Fall.Reset();
    Fall.Sample(500.0);
    Check(Fall.Land(0.0) == 0.f, TEXT("Teleport, load, respawn, water and flight resets discard old peaks"));
    Fall.Begin(400.0);
    Check(Fall.Land(0.0) == 10.f, TEXT("A new fall works after a reset"));
    return OutFailures.IsEmpty();
}
