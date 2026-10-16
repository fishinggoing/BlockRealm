#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BlockRealmContentLibrary.generated.h"
class UFont;
class UFontFace;
class UMaterialParameterCollection;
UCLASS()
class BLOCKREALM_API UBlockRealmContentLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="Content")
    static bool ConfigureChineseFont(UFont* Font, UFontFace* Face);

    /** Editor content-import helper. Cooked builds never create or modify collections. */
    UFUNCTION(BlueprintCallable, Category="Content")
    static bool ConfigureVoxelSkyCollection(UMaterialParameterCollection* Collection);
};
