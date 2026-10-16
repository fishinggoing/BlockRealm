#pragma once

#include "CoreMinimal.h"
#include "BlockTypes.h"

// Survival data intentionally lives outside the save schema.  The original
// world format only has nine block inventory slots; these transient values
// default safely when a legacy save is loaded.
enum class EBlockRealmToolType : uint8
{
    None = 0,
    WoodenPickaxe,
    StonePickaxe,
    Torch
};

struct FBlockRealmToolSlot
{
    EBlockRealmToolType Type = EBlockRealmToolType::None;
    int32 Durability = 0;
    int32 MaxDurability = 0;
    int32 Quantity = 0;

    bool IsUsable() const
    {
        return Type != EBlockRealmToolType::None && Quantity > 0
            && (Type == EBlockRealmToolType::Torch || Durability > 0);
    }
};

enum class EBlockRealmItemType : uint8
{
    None = 0,
    Planks,
    WoodenPickaxe,
    StonePickaxe,
    Torch,
    Grass,
    Dirt,
    Stone,
    Sand,
    Wood,
    Leaves,
    Glass,
    Glow
};

struct FBlockRealmItemStack
{
    EBlockRealmItemType Item = EBlockRealmItemType::None;
    int32 Count = 0;
};

struct FBlockRealmSurvivalRecipe
{
    FString Id;
    FString DisplayName;
    TArray<FBlockRealmItemStack> Ingredients;
    TArray<FBlockRealmItemStack> Results;
};

// World-free fall state. Heights use the same capsule-center reference for the
// whole fall, so capsule height cancels out. Resets never produce damage.
struct BLOCKREALM_API FBlockRealmFallTracker
{
    void Begin(double HeightCm);
    void Sample(double HeightCm);
    void Reset();
    bool IsTracking() const { return bTracking; }
    double GetFallDistance(double LandingHeightCm) const;
    float Land(double LandingHeightCm);

private:
    bool bTracking = false;
    double PeakHeightCm = 0.0;
};

class BLOCKREALM_API FBlockRealmSurvivalData
{
public:
    static constexpr int32 ToolSlotCount = 6;
    static constexpr int32 MaxToolStack = 64;
    static constexpr int32 WoodenPickaxeDurability = 59;
    static constexpr int32 StonePickaxeDurability = 131;

    static const TArray<FBlockRealmSurvivalRecipe>& GetCraftingRecipes();
    static FString GetToolDisplayName(EBlockRealmToolType Type);
    static FString GetItemDisplayName(EBlockRealmItemType Item);
    // The same permission is checked before holding-to-mine starts and again
    // immediately before committing a world edit. Successful mining returns
    // one intact block, including glass reclaimed with a stone pickaxe.
    static bool CanMineBlock(EBlockType Material, EBlockRealmToolType Tool,
        FString* OutReason = nullptr);
    static FString GetMiningRequirementText(EBlockType Material);
    static float GetMiningDuration(EBlockType Material, EBlockRealmToolType Tool);
    // One block is 100 cm. The first three blocks are safe, then each extra
    // block costs 10 of the character's 100 health (fractional blocks count).
    static float CalculateFallDamage(double FallDistanceCm);
    // Pure-data regression entry point: tool tiers, denial reasons, a craft
    // path from hand-collected wood, and fall threshold/reset/long-step cases.
    static bool RunRuleSelfTests(TArray<FString>& OutFailures);
    static int32 GetInventorySlot(EBlockRealmItemType Item);
    // Recover a torch stack without partially changing a full tool inventory.
    static bool TryAddTorches(int32 Count, TArray<FBlockRealmToolSlot>& Tools);

    // Atomic pure-data craft operation.  Character supplies its transient
    // nine-slot counts, tool slots and plank count; no world or UObject is
    // captured, so HUD/tests can use the same recipe rules.
    static bool TryCraft(int32 RecipeIndex, TArray<int32>& Inventory,
        TArray<FBlockRealmToolSlot>& Tools, int32& Planks, FString* OutReason = nullptr);

private:
    static int32 CountItem(EBlockRealmItemType Item, const TArray<int32>& Inventory,
        const TArray<FBlockRealmToolSlot>& Tools, int32 Planks);
    static bool ConsumeItem(EBlockRealmItemType Item, int32 Count, TArray<int32>& Inventory,
        TArray<FBlockRealmToolSlot>& Tools, int32& Planks);
    static bool AddItem(EBlockRealmItemType Item, int32 Count, TArray<int32>& Inventory,
        TArray<FBlockRealmToolSlot>& Tools, int32& Planks);
};
