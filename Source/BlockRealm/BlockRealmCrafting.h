#pragma once

#include "CoreMinimal.h"
#include "BlockRealmSurvival.h"

// HUD-facing compatibility facade. Gameplay owns the recipe value type and
// mutation rules in BlockRealmSurvival; this module keeps the HUD dependency
// stable and leaves room for data assets later.
namespace BlockRealmCrafting
{
    BLOCKREALM_API const TArray<FBlockRealmSurvivalRecipe>& GetCraftingRecipes();
    BLOCKREALM_API FString GetCraftingItemDisplayName(EBlockRealmItemType Item);
}
