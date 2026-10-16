#pragma once

#include "CoreMinimal.h"

enum class EBlockType : uint8
{
    Air = 0, Grass, Dirt, Stone, Sand, Wood, Leaves, Glass, Water, Glow
};

// Save values stay independent of the gameplay tool enum to avoid a header cycle.
// Type: 0=None, 1=WoodenPickaxe, 2=StonePickaxe, 3=Torch.
struct FBlockRealmSavedTool
{
    int32 Type = 0;
    int32 Durability = 0;
    int32 MaxDurability = 0;
    int32 Quantity = 0;
};

// Expansion objects remain plain values so the save layer never depends on actors.
struct FBlockRealmSavedCrop
{
    FIntVector Cell = FIntVector::ZeroValue;
    float GrowthSeconds = 0.f;
};

struct FBlockRealmSavedRestPoint
{
    FIntVector Cell = FIntVector::ZeroValue;
    // The second bed cell is +X, +Y, -X or -Y for headings 0, 1, 2 or 3.
    int32 Heading = 0;
};

struct FBlockRealmSavedFurnace
{
    FIntVector Cell = FIntVector::ZeroValue;
    int32 InputSand = 0;
    int32 FuelWood = 0;
    int32 OutputGlass = 0;
    float CookProgress = 0.f;
};

struct FWorldSaveState
{
    FVector Position = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    int32 SelectedSlot = 0;
    TArray<int32> Inventory;

    // Missing optional data in a version-1 save leaves these defaults intact.
    bool bHasSurvivalState = false;
    float Health = 100.f;
    float Hunger = 100.f;
    int32 CraftedPlanks = 0;
    int32 FoodCount = 6;
    int32 SelectedToolSlot = -1;
    // Optional in version 1: -1 alone still means the selected block for old saves.
    bool bBareHandsEquipped = false;
    TArray<FBlockRealmSavedTool> ToolSlots;
    float TimeOfDay = .32f;
    TArray<FIntVector> PlacedTorches;
    // Empty for old saves; otherwise one original support per placed torch.
    TArray<FIntVector> PlacedTorchSupports;
    // Independently optional root fields, including when survival is absent.
    TArray<FBlockRealmSavedCrop> Crops;
    TArray<FBlockRealmSavedRestPoint> RestPoints;
    TArray<FBlockRealmSavedFurnace> Furnaces;
    bool bHasRespawnPoint = false;
    // A saved bed anchor cell, never the second cell or a world-space position.
    FIntVector RespawnPoint = FIntVector::ZeroValue;
};

inline FString BlockDisplayName(EBlockType Type)
{
    switch (Type)
    {
    case EBlockType::Grass: return TEXT("草方块");
    case EBlockType::Dirt: return TEXT("泥土");
    case EBlockType::Stone: return TEXT("石头");
    case EBlockType::Sand: return TEXT("沙子");
    case EBlockType::Wood: return TEXT("原木");
    case EBlockType::Leaves: return TEXT("树叶");
    case EBlockType::Glass: return TEXT("玻璃");
    case EBlockType::Water: return TEXT("水");
    case EBlockType::Glow: return TEXT("荧石");
    default: return TEXT("空气");
    }
}

inline FLinearColor BlockDisplayColor(EBlockType Type)
{
    switch (Type)
    {
    case EBlockType::Grass: return FLinearColor(.22f,.52f,.16f);
    case EBlockType::Dirt: return FLinearColor(.36f,.22f,.12f);
    case EBlockType::Stone: return FLinearColor(.42f,.46f,.50f);
    case EBlockType::Sand: return FLinearColor(.82f,.69f,.40f);
    case EBlockType::Wood: return FLinearColor(.40f,.23f,.10f);
    case EBlockType::Leaves: return FLinearColor(.13f,.42f,.20f);
    case EBlockType::Glass: return FLinearColor(.42f,.79f,.86f);
    case EBlockType::Water: return FLinearColor(.08f,.39f,.60f);
    case EBlockType::Glow: return FLinearColor(1.f,.66f,.17f);
    default: return FLinearColor::Transparent;
    }
}
