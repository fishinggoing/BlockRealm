#include "BlockRealmContentLibrary.h"
#include "Engine/Font.h"
#include "Engine/FontFace.h"
#include "Fonts/CompositeFont.h"
#include "Materials/MaterialParameterCollection.h"
bool UBlockRealmContentLibrary::ConfigureChineseFont(UFont* Font, UFontFace* Face)
{
    if (!Font || !Face) return false;
    Font->FontCacheType = EFontCacheType::Runtime;
    Font->LegacyFontSize = 32;
    Face->LoadingPolicy = EFontLoadingPolicy::Inline;
    FTypefaceEntry Entry(TEXT("Regular"));
    Entry.Font = FFontData(Face);
    FCompositeFont& Composite = Font->GetMutableInternalCompositeFont();
    Composite.DefaultTypeface.Fonts.Reset();
    Composite.DefaultTypeface.Fonts.Add(Entry);
#if WITH_EDITORONLY_DATA
    Composite.MakeDirty();
#endif
    Font->MarkPackageDirty();
    Face->MarkPackageDirty();
    return true;
}

bool UBlockRealmContentLibrary::ConfigureVoxelSkyCollection(UMaterialParameterCollection* Collection)
{
#if WITH_EDITOR
    if (!IsValid(Collection)) return false;

    const FName StrengthName(TEXT("SkyAmbientStrength"));
    const FName TintName(TEXT("SkyAmbientTint"));
    constexpr float DefaultStrength = 1000.0f;
    const FLinearColor DefaultTint(0.72f, 0.82f, 1.0f, 1.0f);
    const int32 StrengthIndex = Collection->ScalarParameters.IndexOfByPredicate(
        [StrengthName](const FCollectionScalarParameter& Parameter) { return Parameter.ParameterName == StrengthName; });
    const int32 TintIndex = Collection->VectorParameters.IndexOfByPredicate(
        [TintName](const FCollectionVectorParameter& Parameter) { return Parameter.ParameterName == TintName; });
    const bool bStrengthNeedsChange = StrengthIndex == INDEX_NONE
        || !Collection->ScalarParameters[StrengthIndex].Id.IsValid()
        || Collection->ScalarParameters[StrengthIndex].DefaultValue != DefaultStrength;
    const bool bTintNeedsChange = TintIndex == INDEX_NONE
        || !Collection->VectorParameters[TintIndex].Id.IsValid()
        || Collection->VectorParameters[TintIndex].DefaultValue != DefaultTint;
    if (!bStrengthNeedsChange && !bTintNeedsChange) return true;

    Collection->Modify();
    Collection->PreEditChange(nullptr);
    FCollectionScalarParameter& Strength = StrengthIndex == INDEX_NONE
        ? Collection->ScalarParameters.AddDefaulted_GetRef() : Collection->ScalarParameters[StrengthIndex];
    Strength.ParameterName = StrengthName;
    Strength.DefaultValue = DefaultStrength;
    if (!Strength.Id.IsValid()) Strength.Id = FGuid::NewGuid();

    FCollectionVectorParameter& Tint = TintIndex == INDEX_NONE
        ? Collection->VectorParameters.AddDefaulted_GetRef() : Collection->VectorParameters[TintIndex];
    Tint.ParameterName = TintName;
    Tint.DefaultValue = DefaultTint;
    if (!Tint.Id.IsValid()) Tint.Id = FGuid::NewGuid();
    // Preserve parameter IDs so rerunning the import keeps existing material
    // references valid. Native notifications rebuild the collection resource.
    Collection->PostEditChange();
    Collection->MarkPackageDirty();
    return Collection->GetParameterId(StrengthName).IsValid() && Collection->GetParameterId(TintName).IsValid();
#else
    return false;
#endif
}
