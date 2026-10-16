#include "BlockRealmCrafting.h"

namespace BlockRealmCrafting
{
    const TArray<FBlockRealmSurvivalRecipe>& GetCraftingRecipes()
    {
        return FBlockRealmSurvivalData::GetCraftingRecipes();
    }

    FString GetCraftingItemDisplayName(EBlockRealmItemType Item)
    {
        return FBlockRealmSurvivalData::GetItemDisplayName(Item);
    }
}
