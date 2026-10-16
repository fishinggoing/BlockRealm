#include "BlockRealmHUD.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmCrafting.h"
#include "BlockRealmCrop.h"
#include "BlockRealmFurnace.h"
#include "BlockRealmGameMode.h"
#include "BlockRealmPlayerActions.h"
#include "BlockRealmRestPoint.h"
#include "BlockWorld.h"
#include "Camera/CameraComponent.h"
#include "CanvasItem.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"

namespace
{
    const FLinearColor Ivory(.97f, .99f, .96f, 1.f);
    const FLinearColor Muted(.78f, .85f, .81f, 1.f);
    const FLinearColor Mint(.62f, .86f, .68f, 1.f);
    const FLinearColor Amber(.97f, .72f, .39f, 1.f);
    const FLinearColor Ink(.018f, .035f, .039f, 1.f);
    constexpr float DesignWidth = 1920.f;
    constexpr float DesignHeight = 1080.f;
    constexpr float InventoryPanelX = 440.f;
    constexpr float InventoryPanelY = 90.f;
    constexpr float InventoryToolSize = 96.f;
    constexpr float InventoryToolGap = 8.f;

    FBox2D DesignInventoryButtonBounds(FName Name)
    {
        if (Name == TEXT("InventoryClose"))
            return FBox2D(FVector2D(InventoryPanelX + 856.f, InventoryPanelY + 22.f),
                FVector2D(InventoryPanelX + 1000.f, InventoryPanelY + 68.f));
        if (Name == TEXT("CraftOutput"))
            return FBox2D(FVector2D(InventoryPanelX + 292.f, InventoryPanelY + 134.f),
                FVector2D(InventoryPanelX + 388.f, InventoryPanelY + 230.f));
        if (Name == TEXT("InventoryBareHands"))
            return FBox2D(FVector2D(InventoryPanelX + 856.f, InventoryPanelY + 244.f),
                FVector2D(InventoryPanelX + 1000.f, InventoryPanelY + 290.f));
        if (Name == TEXT("InventoryFood"))
            return FBox2D(FVector2D(InventoryPanelX + 40.f + 6.f * 104.f, InventoryPanelY + 352.f),
                FVector2D(InventoryPanelX + 40.f + 6.f * 104.f + 96.f, InventoryPanelY + 448.f));
        if (Name == TEXT("InventoryPlanks"))
            return FBox2D(FVector2D(InventoryPanelX + 40.f + 7.f * 104.f, InventoryPanelY + 352.f),
                FVector2D(InventoryPanelX + 40.f + 7.f * 104.f + 96.f, InventoryPanelY + 448.f));
        const FString Id = Name.ToString();
        if (Id.StartsWith(TEXT("InventoryBlock_")))
        {
            const int32 Slot = FCString::Atoi(*Id.Mid(15));
            if (Slot >= 0 && Slot < 9)
            {
                const FVector2D Min(InventoryPanelX + 40.f + Slot * 104.f, InventoryPanelY + 692.f);
                return FBox2D(Min, Min + FVector2D(96.f));
            }
        }
        if (Id.StartsWith(TEXT("InventoryTool_")))
        {
            const int32 Slot = FCString::Atoi(*Id.Mid(14));
            if (Slot >= 0 && Slot < FBlockRealmSurvivalData::ToolSlotCount)
            {
                const FVector2D Min(InventoryPanelX + 40.f + Slot * (InventoryToolSize + InventoryToolGap),
                    InventoryPanelY + 352.f);
                return FBox2D(Min, Min + FVector2D(InventoryToolSize));
            }
        }
        if (Id.StartsWith(TEXT("CraftRecipe_")))
        {
            const int32 Index = FCString::Atoi(*Id.Mid(12));
            if (Index >= 0 && Index < 5)
            {
                const FVector2D Min(InventoryPanelX + 488.f + Index * 96.f, InventoryPanelY + 90.f);
                return FBox2D(Min, Min + FVector2D(88.f));
            }
        }
        return FBox2D(ForceInit);
    }

    FBox2D DesignButtonBounds(FName Name, bool bPaused)
    {
        if (!bPaused)
        {
            if (Name == TEXT("Start")) return FBox2D(FVector2D(112.f, 574.f), FVector2D(444.f, 640.f));
            if (Name == TEXT("Quit")) return FBox2D(FVector2D(112.f, 658.f), FVector2D(444.f, 714.f));
        }
        else
        {
            if (Name == TEXT("Resume")) return FBox2D(FVector2D(750.f, 436.f), FVector2D(1170.f, 498.f));
            if (Name == TEXT("Save")) return FBox2D(FVector2D(750.f, 514.f), FVector2D(1170.f, 576.f));
            if (Name == TEXT("Title")) return FBox2D(FVector2D(750.f, 592.f), FVector2D(1170.f, 654.f));
        }
        const FName GraphicsNames[] = {TEXT("TSR"), TEXT("Quality"), TEXT("Balanced"), TEXT("Performance")};
        for (int32 Index = 0; Index < 4; ++Index)
        {
            if (Name == GraphicsNames[Index])
            {
                const float X = (bPaused ? 728.f : 1280.f) + Index * (bPaused ? 116.f : 132.f);
                const float Y = bPaused ? 760.f : 879.f;
                return FBox2D(FVector2D(X, Y), FVector2D(X + (bPaused ? 108.f : 120.f), Y + 38.f));
            }
        }
        return FBox2D(ForceInit);
    }

    FString CountText(int32 Count)
    {
        return Count > 999 ? TEXT("999+") : FString::FromInt(Count);
    }

    FString GraphicsModeText(const FString& Status)
    {
        FString Mode = Status;
        FString Remainder;
        Status.Split(TEXT(" | "), &Mode, &Remainder);
        FString Name = Mode;
        Mode.Split(TEXT(" · "), &Name, &Remainder);
        Name.ReplaceInline(TEXT("Quality"), TEXT("质量"));
        Name.ReplaceInline(TEXT("Balanced"), TEXT("平衡"));
        Name.ReplaceInline(TEXT("Performance"), TEXT("性能"));
        Name.ReplaceInline(TEXT("DLSS-SR"), TEXT("DLSS 超分辨率"));
        return Name.TrimStartAndEnd();
    }

    FString GraphicsHint(const FString& Status)
    {
        if (Status.Contains(TEXT("驱动过旧"))) return TEXT("更新显卡驱动后，可启用 DLSS。");
        if (Status.Contains(TEXT("当前显卡不支持"))) return TEXT("这台设备使用 TSR 画面重建。");
        if (Status.Contains(TEXT("系统过旧"))) return TEXT("更新 Windows 后，可启用 DLSS。");
        if (Status.Contains(TEXT("图形捕获"))) return TEXT("关闭图形捕获工具后，重新启动游戏。");
        if (Status.Contains(TEXT("未生效")) || Status.Contains(TEXT("未提供有效")))
            return TEXT("当前档位暂不可用，已使用 TSR。");
        if (Status.Contains(TEXT("初始化")) || Status.Contains(TEXT("未编入")))
            return TEXT("DLSS 暂不可用，已使用 TSR。");
        return TEXT("质量更清晰，性能更流畅。");
    }

    FBox2D DesignToScreenBox(const FVector2D& DesignMin, const FVector2D& DesignMax,
        const FVector2D& ViewportSize)
    {
        if (ViewportSize.X <= 0.f || ViewportSize.Y <= 0.f)
        {
            return FBox2D(ForceInit);
        }
        const float Scale = FMath::Min(ViewportSize.X / DesignWidth, ViewportSize.Y / DesignHeight);
        const FVector2D Origin = (ViewportSize - FVector2D(DesignWidth, DesignHeight) * Scale) * .5f;
        return FBox2D(Origin + DesignMin * Scale, Origin + DesignMax * Scale);
    }

    template <typename T>
    auto ReadHealth(const T* Character, int) -> decltype(static_cast<float>(Character->GetHealth()))
    {
        return static_cast<float>(Character->GetHealth());
    }

    template <typename T>
    float ReadHealth(const T*, ...)
    {
        return 20.f;
    }

    template <typename T>
    auto ReadMaxHealth(const T* Character, int) -> decltype(static_cast<float>(Character->GetMaxHealth()))
    {
        return static_cast<float>(Character->GetMaxHealth());
    }

    template <typename T>
    float ReadMaxHealth(const T*, ...)
    {
        return 20.f;
    }

    template <typename T>
    auto ReadHunger(const T* Character, int) -> decltype(static_cast<float>(Character->GetHunger()))
    {
        return static_cast<float>(Character->GetHunger());
    }

    template <typename T>
    float ReadHunger(const T*, ...)
    {
        return 20.f;
    }

    template <typename T>
    auto ReadMaxHunger(const T* Character, int) -> decltype(static_cast<float>(Character->GetMaxHunger()))
    {
        return static_cast<float>(Character->GetMaxHunger());
    }

    template <typename T>
    float ReadMaxHunger(const T*, ...)
    {
        return 20.f;
    }

    inline FString ToHudString(const FString& Value)
    {
        return Value;
    }

    inline FString ToHudString(const FText& Value)
    {
        return Value.ToString();
    }

    inline FString ToHudString(const FName& Value)
    {
        return Value.ToString();
    }

    template <typename T>
    FString ToHudString(const T&)
    {
        return FString();
    }

    template <typename T>
    auto ReadSelectedToolName(const T* Character, int)
        -> decltype(Character->GetSelectedToolName(), FString())
    {
        return ToHudString(Character->GetSelectedToolName());
    }

    template <typename T>
    FString ReadSelectedToolName(const T*, ...)
    {
        return FString();
    }

    template <typename T>
    auto ReadSelectedToolDurability(const T* Character, int)
        -> decltype(static_cast<float>(Character->GetSelectedToolDurability()))
    {
        return static_cast<float>(Character->GetSelectedToolDurability());
    }

    template <typename T>
    float ReadSelectedToolDurability(const T*, ...)
    {
        return 0.f;
    }

    template <typename T>
    auto ReadSelectedToolMaxDurability(const T* Character, int)
        -> decltype(static_cast<float>(Character->GetSelectedToolMaxDurability()))
    {
        return static_cast<float>(Character->GetSelectedToolMaxDurability());
    }

    template <typename T>
    float ReadSelectedToolMaxDurability(const T*, ...)
    {
        return 0.f;
    }

    template <typename T>
    auto InvokeCraftRecipe(T* Character, int32 RecipeIndex, int)
        -> decltype(static_cast<bool>(Character->TryCraftRecipe(RecipeIndex)))
    {
        return static_cast<bool>(Character->TryCraftRecipe(RecipeIndex));
    }

    template <typename T>
    bool InvokeCraftRecipe(T*, int32, ...)
    {
        return false;
    }

    template <typename T>
    auto ReadInventoryOpen(const T* Character, int)
        -> decltype(static_cast<bool>(Character->IsInventoryOpen()))
    {
        return Character ? static_cast<bool>(Character->IsInventoryOpen()) : false;
    }

    template <typename T>
    bool ReadInventoryOpen(const T*, ...)
    {
        return false;
    }

    template <typename T>
    auto InvokeToggleInventory(T* Character, int)
        -> decltype(Character->ToggleInventory(), void())
    {
        Character->ToggleInventory();
    }

    template <typename T>
    void InvokeToggleInventory(T*, ...)
    {
    }

    EBlockType ItemToBlock(EBlockRealmItemType Item)
    {
        switch (Item)
        {
        case EBlockRealmItemType::Grass: return EBlockType::Grass;
        case EBlockRealmItemType::Dirt: return EBlockType::Dirt;
        case EBlockRealmItemType::Stone: return EBlockType::Stone;
        case EBlockRealmItemType::Sand: return EBlockType::Sand;
        case EBlockRealmItemType::Wood: return EBlockType::Wood;
        case EBlockRealmItemType::Leaves: return EBlockType::Leaves;
        case EBlockRealmItemType::Glass: return EBlockType::Glass;
        case EBlockRealmItemType::Glow: return EBlockType::Glow;
        default: return EBlockType::Air;
        }
    }

    int32 CountCraftingItem(const ABlockRealmCharacter* Player, EBlockRealmItemType Item)
    {
        if (!Player)
        {
            return 0;
        }
        if (Item == EBlockRealmItemType::Planks)
        {
            return Player->GetCraftedPlankCount();
        }
        if (Item == EBlockRealmItemType::WoodenPickaxe || Item == EBlockRealmItemType::StonePickaxe
            || Item == EBlockRealmItemType::Torch)
        {
            int32 Count = 0;
            for (int32 Slot = 0; Slot < Player->GetToolSlotCount(); ++Slot)
            {
                FBlockRealmToolSlot ToolSlot;
                if (!Player->GetToolSlot(Slot, ToolSlot))
                {
                    continue;
                }
                const EBlockRealmToolType Wanted = Item == EBlockRealmItemType::WoodenPickaxe
                    ? EBlockRealmToolType::WoodenPickaxe
                    : Item == EBlockRealmItemType::StonePickaxe ? EBlockRealmToolType::StonePickaxe
                    : EBlockRealmToolType::Torch;
                if (ToolSlot.Type == Wanted)
                {
                    Count += FMath::Max(0, ToolSlot.Quantity);
                }
            }
            return Count;
        }
        const int32 Slot = FBlockRealmSurvivalData::GetInventorySlot(Item);
        return Slot != INDEX_NONE ? Player->GetInventoryCount(Slot) : 0;
    }

    FString RecipeResultText(const FBlockRealmSurvivalRecipe& Recipe)
    {
        if (!Recipe.Results.IsValidIndex(0))
        {
            return TEXT("无产出");
        }
        const FBlockRealmItemStack& Result = Recipe.Results[0];
        return FString::Printf(TEXT("产出 %s ×%d"),
            *BlockRealmCrafting::GetCraftingItemDisplayName(Result.Item), Result.Count);
    }

    FString RecipeIngredientsText(const FBlockRealmSurvivalRecipe& Recipe)
    {
        FString Result;
        for (const FBlockRealmItemStack& Ingredient : Recipe.Ingredients)
        {
            if (Ingredient.Item == EBlockRealmItemType::None || Ingredient.Count <= 0)
            {
                continue;
            }
            if (!Result.IsEmpty())
            {
                Result += TEXT(" · ");
            }
            Result += FString::Printf(TEXT("%s×%d"),
                *BlockRealmCrafting::GetCraftingItemDisplayName(Ingredient.Item), Ingredient.Count);
        }
        return Result;
    }

    bool CanCraftRecipe(const ABlockRealmCharacter* Player,
        const FBlockRealmSurvivalRecipe& Recipe, FString& Missing)
    {
        Missing.Empty();
        if (!Player)
        {
            Missing = TEXT("需要玩家角色");
            return false;
        }
        for (const FBlockRealmItemStack& Ingredient : Recipe.Ingredients)
        {
            const int32 Have = CountCraftingItem(Player, Ingredient.Item);
            if (Have < Ingredient.Count)
            {
                if (!Missing.IsEmpty())
                {
                    Missing += TEXT("、");
                }
                Missing += FString::Printf(TEXT("%s %d/%d"),
                    *BlockRealmCrafting::GetCraftingItemDisplayName(Ingredient.Item), Have, Ingredient.Count);
            }
        }
        return Missing.IsEmpty();
    }
}

ABlockRealmHUD::ABlockRealmHUD()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    bShowHUD = true;
}

void ABlockRealmHUD::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    (void)DeltaSeconds;
    HandleInventoryInput();
}

FBox2D ABlockRealmHUD::GetMenuButtonBounds(FName ButtonName, const FVector2D& ViewportSize, bool bPaused)
{
    const FBox2D Bounds = DesignButtonBounds(ButtonName, bPaused);
    if (!Bounds.bIsValid || ViewportSize.X <= 0.f || ViewportSize.Y <= 0.f)
    {
        return FBox2D(ForceInit);
    }
    const float Scale = FMath::Min(ViewportSize.X / DesignWidth, ViewportSize.Y / DesignHeight);
    const FVector2D Origin = (ViewportSize - FVector2D(DesignWidth, DesignHeight) * Scale) * .5f;
    return FBox2D(Origin + Bounds.Min * Scale, Origin + Bounds.Max * Scale);
}

FBox2D ABlockRealmHUD::GetInventoryButtonBounds(FName ButtonName, const FVector2D& ViewportSize)
{
    const FBox2D Bounds = DesignInventoryButtonBounds(ButtonName);
    return Bounds.bIsValid ? DesignToScreenBox(Bounds.Min, Bounds.Max, ViewportSize) : FBox2D(ForceInit);
}

void ABlockRealmHUD::AddInventoryHitBox(FName Name)
{
    if (!Canvas) return;
    const FBox2D Bounds = GetInventoryButtonBounds(Name, FVector2D(Canvas->ClipX, Canvas->ClipY));
    if (Bounds.bIsValid) AddHitBox(Bounds.Min, Bounds.GetSize(), Name, true, 20);
}

void ABlockRealmHUD::Text(const FString& Value, float X, float Y, float Pixels, const FLinearColor& Color,
    bool bCentered, float MaxWidth)
{
    if (!Canvas || !ChineseFont || Value.IsEmpty()) return;
    const float Scale = Pixels * UiScale / FMath::Max(1.f, ChineseFont->GetMaxCharHeight());
    FString VisibleValue = Value;
    float Width = 0.f;
    float Height = 0.f;
    Canvas->StrLen(ChineseFont, VisibleValue, Width, Height);
    // A long hint must never silently become tiny. Multi-line information uses
    // WrappedText; compact labels keep their chosen size and end in an ellipsis.
    if (MaxWidth > 0.f && Width * Scale > MaxWidth * UiScale)
    {
        do
        {
            VisibleValue = VisibleValue.LeftChop(1);
            Canvas->StrLen(ChineseFont, VisibleValue + TEXT("…"), Width, Height);
        } while (!VisibleValue.IsEmpty() && Width * Scale > MaxWidth * UiScale);
        VisibleValue += TEXT("…");
    }
    FVector2D Position = UiOrigin + FVector2D(X, Y) * UiScale;
    if (bCentered) Position.X -= Width * Scale * .5f;
    FCanvasTextItem Item(Position, FText::FromString(VisibleValue), ChineseFont, Color);
    Item.Scale = FVector2D(Scale);
    Item.EnableShadow(FLinearColor(0.f, 0.f, 0.f, .9f * Color.A), FVector2D(UiScale, UiScale));
    Canvas->DrawItem(Item);
}

void ABlockRealmHUD::WrappedText(const FString& Value, float X, float Y, float Pixels,
    const FLinearColor& Color, float MaxWidth, int32 MaxLines, bool bCentered)
{
    if (!Canvas || !ChineseFont || Value.IsEmpty() || MaxLines <= 0) return;
    if (MaxLines == 1) { Text(Value, X, Y, Pixels, Color, bCentered, MaxWidth); return; }
    TArray<FString> Lines;
    FString Current;
    const float DesignScale = Pixels / FMath::Max(1.f, ChineseFont->GetMaxCharHeight());
    for (int32 Index = 0; Index < Value.Len(); ++Index)
    {
        const FString Character = Value.Mid(Index, 1);
        if (Character == TEXT("\r")) continue;
        float Width = 0.f;
        float Height = 0.f;
        Canvas->StrLen(ChineseFont, Current + Character, Width, Height);
        if (Character == TEXT("\n") || (!Current.IsEmpty() && Width * DesignScale > MaxWidth))
        {
            Lines.Add(Current.TrimEnd());
            Current.Empty();
            if (Lines.Num() >= MaxLines - 1)
            {
                Current = Value.Mid(Index).Replace(TEXT("\r"), TEXT("")).Replace(TEXT("\n"), TEXT(" "));
                break;
            }
            if (Character == TEXT("\n")) continue;
        }
        Current += Character;
    }
    if (!Current.IsEmpty()) Lines.Add(Current.TrimEnd());
    for (int32 Index = 0; Index < FMath::Min(Lines.Num(), MaxLines); ++Index)
        Text(Lines[Index], X, Y + Index * (Pixels + 7.f), Pixels, Color, bCentered, MaxWidth);
}

void ABlockRealmHUD::Fill(float X, float Y, float Width, float Height, const FLinearColor& Color)
{
    if (!Canvas || Width <= 0.f || Height <= 0.f)
    {
        return;
    }
    FCanvasTileItem Tile(UiOrigin + FVector2D(X, Y) * UiScale, FVector2D(Width, Height) * UiScale, Color);
    Tile.BlendMode = SE_BLEND_Translucent;
    Canvas->DrawItem(Tile);
}

void ABlockRealmHUD::Line(float X1, float Y1, float X2, float Y2, const FLinearColor& Color, float Thickness)
{
    FCanvasLineItem Item(UiOrigin + FVector2D(X1, Y1) * UiScale, UiOrigin + FVector2D(X2, Y2) * UiScale);
    Item.SetColor(Color);
    Item.LineThickness = Thickness * UiScale;
    Canvas->DrawItem(Item);
}

void ABlockRealmHUD::Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color)
{
    FCanvasUVTri Tri;
    Tri.V0_Pos = UiOrigin + A * UiScale;
    Tri.V1_Pos = UiOrigin + B * UiScale;
    Tri.V2_Pos = UiOrigin + C * UiScale;
    Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
    Tri.V0_Color = Tri.V1_Color = Tri.V2_Color = Color;
    Canvas->K2_DrawTriangle(nullptr, {Tri});
}

void ABlockRealmHUD::CubeIcon(float X, float Y, float Size, EBlockType Type, float Opacity)
{
    FLinearColor Base = BlockDisplayColor(Type);
    if (Type == EBlockType::Grass) Base = FLinearColor(.39f, .66f, .32f);
    if (Type == EBlockType::Wood) Base = FLinearColor(.63f, .40f, .23f);
    if (Type == EBlockType::Leaves) Base = FLinearColor(.27f, .53f, .32f);
    const auto Shade = [Base, Opacity](float Factor)
    {
        return FLinearColor(FMath::Min(1.f, Base.R * Factor), FMath::Min(1.f, Base.G * Factor),
            FMath::Min(1.f, Base.B * Factor), Opacity);
    };
    const FVector2D Top(X, Y - Size * .55f);
    const FVector2D Left(X - Size * .55f, Y - Size * .27f);
    const FVector2D Middle(X, Y + Size * .02f);
    const FVector2D Right(X + Size * .55f, Y - Size * .27f);
    const FVector2D BottomLeft(X - Size * .55f, Y + Size * .31f);
    const FVector2D Bottom(X, Y + Size * .60f);
    const FVector2D BottomRight(X + Size * .55f, Y + Size * .31f);
    Triangle(Top, Left, Middle, Shade(1.28f));
    Triangle(Top, Middle, Right, Shade(1.28f));
    const FLinearColor LeftColor = Type == EBlockType::Grass ? FLinearColor(.45f, .30f, .18f, Opacity) : Shade(.85f);
    const FLinearColor RightColor = Type == EBlockType::Grass ? FLinearColor(.34f, .22f, .13f, Opacity) : Shade(.62f);
    Triangle(Left, BottomLeft, Bottom, LeftColor);
    Triangle(Left, Bottom, Middle, LeftColor);
    Triangle(Middle, Bottom, BottomRight, RightColor);
    Triangle(Middle, BottomRight, Right, RightColor);
    Line(Top.X, Top.Y, Left.X, Left.Y, Ivory.CopyWithNewOpacity(.17f * Opacity), .8f);
    Line(Top.X, Top.Y, Right.X, Right.Y, Ivory.CopyWithNewOpacity(.17f * Opacity), .8f);
    if (Type == EBlockType::Wood)
    {
        Line(X - Size * .35f, Y - Size * .05f, X - Size * .35f, Y + Size * .33f,
            Ink.CopyWithNewOpacity(.3f * Opacity), 1.5f);
        Line(X - Size * .18f, Y + Size * .03f, X - Size * .18f, Y + Size * .41f,
            Ivory.CopyWithNewOpacity(.2f * Opacity), 1.f);
    }
    if (Type == EBlockType::Glass)
    {
        Line(X + Size * .15f, Y + Size * .02f, X + Size * .43f, Y - Size * .11f,
            Ivory.CopyWithNewOpacity(.8f * Opacity), 1.3f);
        Line(X + Size * .15f, Y + Size * .19f, X + Size * .43f, Y + Size * .06f,
            Ivory.CopyWithNewOpacity(.45f * Opacity), 1.f);
    }
}

void ABlockRealmHUD::ToolIcon(float X, float Y, float Size, EBlockRealmToolType Type, float Opacity)
{
    const FLinearColor Wood(.65f, .39f, .19f, Opacity);
    const FLinearColor Edge = Ink.CopyWithNewOpacity(Opacity);
    if (Type == EBlockRealmToolType::Torch)
    {
        Line(X - Size * .1f, Y + Size * .43f, X + Size * .08f, Y - Size * .28f, Edge, Size * .19f);
        Line(X - Size * .1f, Y + Size * .43f, X + Size * .08f, Y - Size * .28f, Wood, Size * .12f);
        CubeIcon(X + Size * .08f, Y - Size * .28f, Size * .39f, EBlockType::Glow, Opacity);
    }
    else if (Type == EBlockRealmToolType::WoodenPickaxe || Type == EBlockRealmToolType::StonePickaxe)
    {
        Line(X - Size * .25f, Y + Size * .43f, X + Size * .16f, Y - Size * .27f, Edge, Size * .16f);
        Line(X - Size * .25f, Y + Size * .43f, X + Size * .16f, Y - Size * .27f, Wood, Size * .09f);
        const FLinearColor Head = Type == EBlockRealmToolType::StonePickaxe
            ? FLinearColor(.66f, .73f, .76f, Opacity) : FLinearColor(.73f, .47f, .23f, Opacity);
        Line(X - Size * .38f, Y - Size * .25f, X + Size * .4f, Y - Size * .12f, Edge, Size * .22f);
        Line(X - Size * .38f, Y - Size * .25f, X + Size * .4f, Y - Size * .12f, Head, Size * .14f);
        Line(X - Size * .38f, Y - Size * .25f, X - Size * .49f, Y - Size * .06f, Head, Size * .13f);
        Line(X + Size * .4f, Y - Size * .12f, X + Size * .48f, Y + Size * .07f, Head, Size * .13f);
    }
    else
    {
        const FLinearColor Skin(.73f, .49f, .31f, Opacity);
        Fill(X - Size * .24f, Y - Size * .16f, Size * .5f, Size * .48f, Skin);
        Fill(X - Size * .32f, Y - Size * .02f, Size * .16f, Size * .24f, Skin);
        Fill(X - Size * .2f, Y + Size * .29f, Size * .4f, Size * .15f,
            Mint.CopyWithNewOpacity(Opacity));
    }
}

void ABlockRealmHUD::Button(FName Id, const FString& Label, bool bPaused, bool bPrimary, bool bEnabled)
{
    const FBox2D Bounds = DesignButtonBounds(Id, bPaused);
    if (!Bounds.bIsValid)
    {
        return;
    }
    const FVector2D Size = Bounds.GetSize();
    const float X = Bounds.Min.X;
    const float Y = Bounds.Min.Y;
    const bool bHovered = bEnabled && HoveredButton == Id;
    const FLinearColor Surface = bPrimary && bEnabled
        ? (bHovered ? Ivory : Mint) : Ink.CopyWithNewOpacity(bHovered ? .95f : .72f);
    Fill(X, Y, Size.X, Size.Y, Surface);
    if (!bPrimary || !bEnabled)
    {
        Line(X, Y, X + Size.X, Y, Muted.CopyWithNewOpacity(bHovered ? .5f : .22f));
        Line(X, Y + Size.Y, X + Size.X, Y + Size.Y, Muted.CopyWithNewOpacity(bHovered ? .5f : .22f));
    }
    const FLinearColor Foreground = !bEnabled ? Muted : bPrimary ? Ink : bHovered ? Ivory : Muted;
    Text(Label, X + 24.f, Y + (Size.Y - 22.f) * .5f, 22.f, Foreground, false, Size.X - 70.f);
    if (bEnabled)
    {
        const float AX = X + Size.X - 29.f;
        const float AY = Y + Size.Y * .5f;
        Line(AX - 12.f, AY, AX + 2.f, AY, Foreground, 1.5f);
        Line(AX - 4.f, AY - 6.f, AX + 2.f, AY, Foreground, 1.5f);
        Line(AX - 4.f, AY + 6.f, AX + 2.f, AY, Foreground, 1.5f);
        const FBox2D Pixels = GetMenuButtonBounds(Id, FVector2D(Canvas->ClipX, Canvas->ClipY), bPaused);
        AddHitBox(Pixels.Min, Pixels.GetSize(), Id, true, 10);
    }
}

void ABlockRealmHUD::GraphicsButtons(ABlockRealmGameMode* Game, bool bPaused)
{
    const float Left = bPaused ? 728.f : 1280.f;
    const float LabelY = bPaused ? 717.f : 842.f;
    Text(TEXT("画面"), Left, LabelY, 15.f, Muted);
    const FString GraphicsStatus = Game->GetGraphicsStatus();
    Text(GraphicsModeText(GraphicsStatus), Left + 72.f, LabelY + 1.f, 14.f,
        Mint.CopyWithNewOpacity(.85f), false, bPaused ? 392.f : 444.f);
    const FName Names[] = {TEXT("TSR"), TEXT("Quality"), TEXT("Balanced"), TEXT("Performance")};
    const FString Labels[] = {TEXT("原生 TSR"), TEXT("DLSS 质量"), TEXT("DLSS 平衡"), TEXT("DLSS 性能")};
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FBox2D Bounds = DesignButtonBounds(Names[Index], bPaused);
        const FVector2D Size = Bounds.GetSize();
        const bool bSelected = Game->GetUpscaleMode() == Index;
        const bool bHovered = HoveredButton == Names[Index];
        Fill(Bounds.Min.X, Bounds.Min.Y, Size.X, Size.Y,
            bSelected ? Mint.CopyWithNewOpacity(.15f) : Ivory.CopyWithNewOpacity(bHovered ? .1f : .035f));
        if (bSelected)
        {
            Fill(Bounds.Min.X, Bounds.Max.Y - 2.f, Size.X, 2.f, Mint);
        }
        Text(Labels[Index], Bounds.GetCenter().X, Bounds.Min.Y + 11.f, 15.f,
            bSelected || bHovered ? Ivory : Muted, true, Size.X - 10.f);
        const FBox2D Pixels = GetMenuButtonBounds(Names[Index], FVector2D(Canvas->ClipX, Canvas->ClipY), bPaused);
        AddHitBox(Pixels.Min, Pixels.GetSize(), Names[Index], true, 10);
    }
    Text(GraphicsHint(GraphicsStatus), Left, bPaused ? 815.f : 934.f, 13.f, Muted, false, bPaused ? 464.f : 516.f);
}

void ABlockRealmHUD::DrawTitle(ABlockRealmGameMode* Game)
{
    Fill(-UiOrigin.X / UiScale, -UiOrigin.Y / UiScale, Canvas->ClipX / UiScale, Canvas->ClipY / UiScale,
        Ink.CopyWithNewOpacity(.18f));
    for (int32 Index = 0; Index < 32; ++Index)
    {
        const float T = static_cast<float>(Index) / 31.f;
        Fill(Index * 36.f, 0.f, 37.f, DesignHeight, Ink.CopyWithNewOpacity(.77f * FMath::Square(1.f - T)));
    }
    CubeIcon(132.f, 188.f, 32.f, EBlockType::Grass);
    Text(TEXT("自由建造  /  无限探索"), 166.f, 178.f, 16.f, Mint);
    Text(TEXT("方境"), 105.f, 250.f, 106.f, Ivory);
    Text(TEXT("B L O C K R E A L M"), 113.f, 382.f, 18.f, Muted);
    Line(112.f, 423.f, 173.f, 423.f, Mint.CopyWithNewOpacity(.75f), 2.f);
    Text(TEXT("把想象，筑成风景。"), 112.f, 458.f, 28.f, Ivory);
    Text(TEXT("山野、河流与光，从脚下这一块开始。"), 112.f, 504.f, 18.f, Muted);
    ABlockWorld* World = Game->GetWorldActor();
    const bool bReady = World && World->IsInitialTerrainReady();
    const bool bHasSave = World && IFileManager::Get().FileExists(*World->GetSavePath());
    Button(TEXT("Start"), !bReady ? TEXT("正在准备山野…") : bHasSave ? TEXT("继续旅程") : TEXT("开始旅程"), false, true, bReady);
    Button(TEXT("Quit"), TEXT("退出"), false);
    if (bHasSave)
    {
        Text(TEXT("已保存的山野，仍在等你。"), 113.f, 746.f, 15.f, Muted);
    }
    if (!Game->GetNotice().IsEmpty())
    {
        Text(Game->GetNotice(), 112.f, 801.f, 16.f, Amber, false, 580.f);
    }
    Fill(1256.f, 818.f, 552.f, 157.f, Ink.CopyWithNewOpacity(.75f));
    Line(1256.f, 818.f, 1808.f, 818.f, Ivory.CopyWithNewOpacity(.17f));
    GraphicsButtons(Game, false);
    Text(TEXT("单人生存建造"), 112.f, 989.f, 15.f, Ivory.CopyWithNewOpacity(.76f));
    Text(TEXT("采集 · 建造 · 探索"), 112.f, 1020.f, 13.f, Muted);
    Text(TEXT("MiSans 字体 · 小米科技"), 1554.f, 1019.f, 12.f, Muted.CopyWithNewOpacity(.7f), false, 254.f);
}

void ABlockRealmHUD::DrawPause(ABlockRealmGameMode* Game)
{
    Fill(-UiOrigin.X / UiScale, -UiOrigin.Y / UiScale, Canvas->ClipX / UiScale, Canvas->ClipY / UiScale,
        Ink.CopyWithNewOpacity(.65f));
    Fill(692.f, 273.f, 536.f, 604.f, Ink.CopyWithNewOpacity(.9f));
    Line(692.f, 273.f, 1228.f, 273.f, Mint.CopyWithNewOpacity(.55f), 2.f);
    Text(TEXT("稍作停留"), 960.f, 313.f, 43.f, Ivory, true);
    Text(TEXT("山野会等待。"), 960.f, 378.f, 18.f, Muted, true);
    Button(TEXT("Resume"), TEXT("继续探索"), true, true);
    Button(TEXT("Save"), TEXT("保存世界"), true);
    Button(TEXT("Title"), TEXT("返回标题"), true);
    Line(728.f, 690.f, 1192.f, 690.f, Ivory.CopyWithNewOpacity(.12f));
    GraphicsButtons(Game, true);
    Text(TEXT("Esc 继续探索"), 960.f, 899.f, 14.f, Muted, true);
    const ABlockRealmCharacter* Player = Game->GetPlayerCharacter();
    const FString Interaction = Player ? Player->GetInteractionNotice() : FString();
    const bool bGameNoticeVisible = FPlatformTime::Seconds() < GameNoticeUntil;
    // Menu actions update the game notice, while repeated F5 updates the character
    // notice even when its text is unchanged. A different menu result is newer.
    if (!Interaction.IsEmpty() && (!bGameNoticeVisible || Interaction == LastGameNotice))
    {
        DrawNotice(Interaction, 945.f);
    }
    else if (bGameNoticeVisible)
    {
        DrawNotice(LastGameNotice, 945.f);
    }
}

void ABlockRealmHUD::DrawHotbar(ABlockRealmCharacter* Player)
{
    if (!Player) return;
    constexpr float Left = 568.f;
    constexpr float Top = 946.f;
    Fill(Left - 8.f, Top - 8.f, 808.f, 112.f, FLinearColor(.12f, .12f, .12f, .82f));
    Text(Player->GetHeldItemName(), 960.f, 865.f, 24.f, Ivory, true, 520.f);
    for (int32 Slot = 0; Slot < 9; ++Slot)
    {
        const EBlockType Type = static_cast<EBlockType>(Slot + 1);
        const bool bSelected = Player->IsBlockEquipped() && Slot == Player->GetSelectedSlot();
        const float X = Left + Slot * 88.f;
        DrawRecessedSlot(X, Top, 80.f, false, bSelected);
        CubeIcon(X + 40.f, Top + 34.f, 38.f, Type, Type == EBlockType::Water ? .35f : 1.f);
        Text(FString::FromInt(Slot + 1), X + 7.f, Top + 5.f, 17.f, Ivory);
        Text(Type == EBlockType::Water ? TEXT("×") : CountText(Player->GetInventoryCount(Slot)),
            X + 48.f, Top + 54.f, 23.f, Ivory, true, 62.f);
    }
}

void ABlockRealmHUD::DrawSurvivalBars(ABlockRealmCharacter* Player)
{
    if (!Player) return;
    const float HealthMax = FMath::Max(1.f, ReadMaxHealth(Player, 0));
    const float HungerMax = FMath::Max(1.f, ReadMaxHunger(Player, 0));
    const float Health = FMath::Clamp(ReadHealth(Player, 0), 0.f, HealthMax);
    const float Hunger = FMath::Clamp(ReadHunger(Player, 0), 0.f, HungerMax);
    Text(FString::Printf(TEXT("生命 %.0f"), Health), 576.f, 886.f, 19.f, Ivory);
    Text(FString::Printf(TEXT("饥饿 %.0f"), Hunger), 1204.f, 886.f, 19.f, Ivory, false, 150.f);
    for (int32 Index = 0; Index < 10; ++Index)
    {
        const float X = 576.f + Index * 23.f;
        constexpr float Y = 914.f;
        const float HeartFill = FMath::Clamp(Health / HealthMax * 10.f - Index, 0.f, 1.f);
        const auto Heart = [this, X, Y](const FLinearColor& Color, float Fraction, float Inset)
        {
            const auto Pixel = [this, X, Y, Fraction, Inset, Color](float DX, float DY, float W, float H)
            {
                const float VisibleWidth = FMath::Min(W, FMath::Max(0.f, 20.f * Fraction - DX));
                Fill(X + DX + Inset, Y + DY + Inset, FMath::Max(0.f, VisibleWidth - Inset), FMath::Max(1.f, H - Inset), Color);
            };
            Pixel(2.f, 0.f, 6.f, 7.f); Pixel(12.f, 0.f, 6.f, 7.f);
            Pixel(0.f, 4.f, 20.f, 7.f); Pixel(3.f, 11.f, 14.f, 4.f);
            Pixel(6.f, 15.f, 8.f, 3.f); Pixel(9.f, 18.f, 2.f, 2.f);
        };
        Heart(FLinearColor(.04f, .02f, .02f), 1.f, 0.f);
        if (HeartFill > 0.f) Heart(FLinearColor(.96f, .13f, .16f), HeartFill, 1.f);
        const float FoodX = 1120.f + Index * 23.f;
        const float FoodFill = FMath::Clamp(Hunger / HungerMax * 10.f - Index, 0.f, 1.f);
        Line(FoodX + 3.f, Y + 18.f, FoodX + 12.f, Y + 9.f, FLinearColor(.05f, .035f, .02f), 8.f);
        Line(FoodX + 3.f, Y + 18.f, FoodX + 12.f, Y + 9.f, FoodFill > 0.f ? Ivory : FLinearColor(.16f, .16f, .16f), 4.f);
        Fill(FoodX + 7.f, Y, 14.f, 14.f, FLinearColor(.06f, .035f, .02f));
        if (FoodFill > 0.f)
        {
            Fill(FoodX + 9.f, Y + 2.f, 10.f * FoodFill, 10.f, FLinearColor(.76f, .35f, .10f));
            Fill(FoodX + 10.f, Y + 3.f, 4.f * FoodFill, 3.f, FLinearColor(.95f, .65f, .27f));
        }
    }
}

void ABlockRealmHUD::DrawFoodStatus(ABlockRealmCharacter* Player)
{
    const UBlockRealmPlayerActionsComponent* Actions = Player
        ? Player->FindComponentByClass<UBlockRealmPlayerActionsComponent>() : nullptr;
    if (!Actions || (!Actions->IsHeadUnderwater() && Actions->GetBreathPercent() >= 1.f)) return;
    const bool bLow = Actions->IsHeadUnderwater() && Actions->GetBreathPercent() <= .25f;
    const FLinearColor Color = bLow ? FLinearColor(1.f, .35f, .30f) : FLinearColor(.50f, .83f, 1.f);
    Text(FString::Printf(TEXT("呼吸 %.1f 秒"), Actions->GetBreathSeconds()), 960.f, 839.f, 21.f, Color, true, 260.f);
    for (int32 Index = 0; Index < 10; ++Index)
    {
        const float X = 1120.f + Index * 23.f;
        constexpr float Y = 846.f;
        const bool bFull = Actions->GetBreathPercent() * 10.f > Index;
        const FLinearColor Bubble = bFull ? Color : FLinearColor(.12f, .19f, .23f);
        Fill(X + 3.f, Y, 9.f, 3.f, Bubble);
        Fill(X, Y + 3.f, 3.f, 9.f, Bubble);
        Fill(X + 12.f, Y + 3.f, 3.f, 9.f, Bubble);
        Fill(X + 3.f, Y + 12.f, 9.f, 3.f, Bubble);
        if (bFull) Fill(X + 4.f, Y + 4.f, 4.f, 4.f, Ivory);
    }
    if (bLow)
    {
        Fill(730.f, 797.f, 460.f, 31.f, Ink.CopyWithNewOpacity(.9f));
        Text(Actions->GetBreathSeconds() <= 0.f ? TEXT("正在溺水，立即上浮！") : TEXT("呼吸不足，尽快露头！"),
            960.f, 803.f, 21.f, Color, true, 436.f);
    }
}

void ABlockRealmHUD::DrawCompass(ABlockRealmCharacter* Player)
{
    if (!Player) return;
    const float Yaw = static_cast<float>(Player->GetControlRotation().Yaw);
    if (!FMath::IsFinite(Yaw)) return;
    const float CardinalYaw[] = {-90.f, 0.f, 90.f, 180.f};
    const TCHAR* Letters[] = {TEXT("N"), TEXT("E"), TEXT("S"), TEXT("W")};
    const TCHAR* Names[] = {TEXT("北"), TEXT("东"), TEXT("南"), TEXT("西")};
    int32 Nearest = 0;
    float NearestAngle = 180.f;
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(CardinalYaw); ++Index)
    {
        const float Angle = FMath::Abs(FMath::FindDeltaAngleDegrees(Yaw, CardinalYaw[Index]));
        if (Angle < NearestAngle) { NearestAngle = Angle; Nearest = Index; }
    }
    constexpr float Left = 780.f;
    constexpr float Top = 32.f;
    Fill(Left, Top, 360.f, 90.f, Ink.CopyWithNewOpacity(.91f));
    Text(TEXT("方位"), Left + 18.f, Top + 12.f, 18.f, Muted);
    Text(FString::Printf(TEXT("%s · %s"), Letters[Nearest], Names[Nearest]), Left + 282.f, Top + 11.f, 21.f, Mint, true, 120.f);
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(CardinalYaw); ++Index)
    {
        const float X = Left + 46.f + Index * 89.f;
        Text(Letters[Index], X, Top + 48.f, 22.f, Index == Nearest ? Mint : Muted, true, 42.f);
        if (Index == Nearest) Fill(X - 14.f, Top + 79.f, 28.f, 3.f, Mint);
    }
}

void ABlockRealmHUD::DrawCombatFeedback(ABlockRealmCharacter* Player, float CenterX, float CenterY)
{
    const UBlockRealmPlayerActionsComponent* Actions = Player
        ? Player->FindComponentByClass<UBlockRealmPlayerActionsComponent>() : nullptr;
    if (!Actions)
    {
        return;
    }
    const FString HitNotice = Actions->GetHitFeedback();
    const float HitAlpha = FMath::Clamp(Actions->GetHitFeedbackAlpha(), 0.f, 1.f);
    if (!HitNotice.IsEmpty() && HitAlpha > 0.f)
    {
        const FLinearColor HitColor = HitNotice.Contains(TEXT("击败")) ? Amber : Mint;
        // Leave the reticle's inner ten pixels clear. The four diagonal arms
        // sit outside it and fade with the component's authoritative hit timer.
        for (int32 Pass = 0; Pass < 2; ++Pass)
        {
            const FLinearColor Color = Pass == 0
                ? Ink.CopyWithNewOpacity(.75f * HitAlpha) : HitColor.CopyWithNewOpacity(HitAlpha);
            const float Thickness = Pass == 0 ? 3.f : 1.7f;
            Line(CenterX - 26.f, CenterY - 26.f, CenterX - 18.f, CenterY - 18.f, Color, Thickness);
            Line(CenterX + 18.f, CenterY - 18.f, CenterX + 26.f, CenterY - 26.f, Color, Thickness);
            Line(CenterX - 26.f, CenterY + 26.f, CenterX - 18.f, CenterY + 18.f, Color, Thickness);
            Line(CenterX + 18.f, CenterY + 18.f, CenterX + 26.f, CenterY + 26.f, Color, Thickness);
        }
        Fill(CenterX - 210.f, CenterY - 82.f, 420.f, 44.f, Ink.CopyWithNewOpacity(.94f * HitAlpha));
        Text(HitNotice, CenterX, CenterY - 71.f, 21.f, HitColor.CopyWithNewOpacity(HitAlpha), true, 392.f);
    }
    const float Cooldown = FMath::Clamp(Actions->GetAttackCooldownPercent(), 0.f, 1.f);
    if (Cooldown > 0.f)
    {
        // Keep the cooldown below the target label and above contextual panels.
        constexpr float Width = 96.f;
        Fill(CenterX - Width * .5f, CenterY + 65.f, Width, 3.f,
            Ivory.CopyWithNewOpacity(.12f * Cooldown));
        Fill(CenterX - Width * .5f, CenterY + 65.f, Width * Cooldown, 3.f,
            Mint.CopyWithNewOpacity(.65f * Cooldown));
    }
}

void ABlockRealmHUD::DrawDamageFeedback(ABlockRealmCharacter* Player)
{
    if (!Player || !Canvas)
    {
        return;
    }
    const float Feedback = Player->GetDamageFeedbackAlpha();
    if (!FMath::IsFinite(Feedback) || Feedback <= 0.f)
    {
        return;
    }
    const float Alpha = FMath::Clamp(Feedback, 0.f, 1.f);
    const float Left = -UiOrigin.X / UiScale;
    const float Top = -UiOrigin.Y / UiScale;
    const float Width = Canvas->ClipX / UiScale;
    const float Height = Canvas->ClipY / UiScale;
    constexpr int32 Bands = 5;
    constexpr float BandWidth = 6.f;
    for (int32 Index = 0; Index < Bands; ++Index)
    {
        const float Inset = Index * BandWidth;
        const FLinearColor Color(.85f, .12f, .10f, Alpha * .18f * (1.f - static_cast<float>(Index) / Bands));
        Fill(Left + Inset, Top + Inset, Width - 2.f * Inset, BandWidth, Color);
        Fill(Left + Inset, Top + Height - Inset - BandWidth, Width - 2.f * Inset, BandWidth, Color);
        Fill(Left + Inset, Top + Inset + BandWidth, BandWidth, Height - 2.f * (Inset + BandWidth), Color);
        Fill(Left + Width - Inset - BandWidth, Top + Inset + BandWidth,
            BandWidth, Height - 2.f * (Inset + BandWidth), Color);
    }
}

void ABlockRealmHUD::DrawToolDurability(ABlockRealmCharacter* Player)
{
    if (!Player) return;
    constexpr float Left = 1466.f;
    constexpr float Top = 208.f;
    constexpr float Width = 422.f;
    const float Maximum = FMath::Max(0.f, ReadSelectedToolMaxDurability(Player, 0));
    const float Durability = FMath::Clamp(ReadSelectedToolDurability(Player, 0), 0.f, Maximum);
    Fill(Left, Top, Width, 118.f, Ink.CopyWithNewOpacity(.91f));
    Text(TEXT("当前手持"), Left + 18.f, Top + 14.f, 18.f, Muted);
    Text(Player->GetHeldItemName(), Left + 132.f, Top + 12.f, 22.f, Ivory, false, Width - 150.f);
    if (Maximum > 0.f)
    {
        const float Ratio = Durability / Maximum;
        const FLinearColor Color = Ratio < .2f ? Amber : Mint;
        Text(FString::Printf(TEXT("耐久 %.0f / %.0f"), Durability, Maximum),
            Left + 18.f, Top + 51.f, 19.f, Color, false, Width - 36.f);
        Fill(Left + 18.f, Top + 88.f, Width - 36.f, 10.f, Ivory.CopyWithNewOpacity(.17f));
        Fill(Left + 18.f, Top + 88.f, (Width - 36.f) * Ratio, 10.f, Color);
    }
    else
    {
        FBlockRealmToolSlot Tool;
        const bool bHasTool = Player->GetToolSlot(Player->GetSelectedToolSlot(), Tool);
        const FString Detail = bHasTool && Tool.Type == EBlockRealmToolType::Torch
            ? FString::Printf(TEXT("剩余火把 ×%s"), *CountText(FMath::Max(0, Tool.Quantity)))
            : Player->IsBlockEquipped()
                ? FString::Printf(TEXT("库存 ×%s · 右键放置"), *CountText(Player->GetInventoryCount(Player->GetSelectedSlot())))
                : FString(TEXT("空手采集软质方块"));
        Text(Detail, Left + 18.f, Top + 51.f, 19.f, Muted, false, Width - 36.f);
        Text(TEXT("Q / R 选工具 · E 打开背包"), Left + 18.f, Top + 83.f, 18.f, Mint, false, Width - 36.f);
    }
}

void ABlockRealmHUD::DrawInventorySlot(ABlockRealmCharacter* Player, int32 Slot,
    float X, float Y, float Size, bool bSelected)
{
    if (!Player || Slot < 0 || Slot >= 9) return;
    const EBlockType Type = static_cast<EBlockType>(Slot + 1);
    const bool bWater = Type == EBlockType::Water;
    const FName Name(*FString::Printf(TEXT("InventoryBlock_%d"), Slot));
    DrawRecessedSlot(X, Y, Size, HoveredButton == Name, bSelected);
    CubeIcon(X + Size * .5f, Y + Size * .44f, Size * .49f, Type,
        bWater ? .4f : Player->GetInventoryCount(Slot) > 0 ? 1.f : .35f);
    Text(FString::FromInt(Slot + 1), X + 9.f, Y + 7.f, 18.f, FLinearColor(.12f, .12f, .12f));
    Text(bWater ? TEXT("×") : CountText(Player->GetInventoryCount(Slot)), X + Size - 33.f,
        Y + Size - 29.f, 24.f, Ivory, true, 60.f);
    if (!bWater) AddInventoryHitBox(Name);
}

void ABlockRealmHUD::DrawInventoryToolSlot(ABlockRealmCharacter* Player, int32 Slot)
{
    FBlockRealmToolSlot Tool;
    if (!Player || !Player->GetToolSlot(Slot, Tool)) return;
    const FName Name(*FString::Printf(TEXT("InventoryTool_%d"), Slot));
    const FBox2D Box = DesignInventoryButtonBounds(Name);
    const bool bUsable = Tool.IsUsable();
    const bool bSelected = bUsable && !Player->IsBlockEquipped() && Slot == Player->GetSelectedToolSlot();
    DrawRecessedSlot(Box.Min.X, Box.Min.Y, Box.GetSize().X, HoveredButton == Name, bSelected);
    if (!bUsable) return;
    ToolIcon(Box.GetCenter().X, Box.Min.Y + 41.f, 56.f, Tool.Type);
    if (Tool.Type == EBlockRealmToolType::Torch)
        Text(CountText(Tool.Quantity), Box.Max.X - 33.f, Box.Max.Y - 29.f, 24.f, Ivory, true, 60.f);
    else
    {
        const float Ratio = static_cast<float>(Tool.Durability) / FMath::Max(1, Tool.MaxDurability);
        Fill(Box.Min.X + 13.f, Box.Max.Y - 18.f, Box.GetSize().X - 26.f, 7.f, FLinearColor(.07f, .07f, .07f));
        Fill(Box.Min.X + 13.f, Box.Max.Y - 18.f, (Box.GetSize().X - 26.f) * Ratio, 5.f, Ratio < .2f ? Amber : Mint);
    }
    AddInventoryHitBox(Name);
}

void ABlockRealmHUD::DrawRecessedSlot(float X, float Y, float Size, bool bHovered, bool bSelected)
{
    Fill(X, Y, Size, Size, FLinearColor(.23f, .23f, .23f));
    Fill(X + 3.f, Y + 3.f, Size - 3.f, Size - 3.f, FLinearColor(.88f, .88f, .88f));
    Fill(X + 3.f, Y + 3.f, Size - 6.f, Size - 6.f, FLinearColor(bHovered ? .61f : .36f, bHovered ? .61f : .36f, bHovered ? .61f : .36f));
    if (bSelected)
    {
        Line(X - 2.f, Y - 2.f, X + Size + 2.f, Y - 2.f, Ivory, 3.f);
        Line(X - 2.f, Y + Size + 2.f, X + Size + 2.f, Y + Size + 2.f, Ivory, 3.f);
        Line(X - 2.f, Y - 2.f, X - 2.f, Y + Size + 2.f, Ivory, 3.f);
        Line(X + Size + 2.f, Y - 2.f, X + Size + 2.f, Y + Size + 2.f, Ivory, 3.f);
    }
}

void ABlockRealmHUD::DrawInventoryTooltip(ABlockRealmCharacter* Player, const FVector2D& MousePosition)
{
    if (!Player) return;
    const FName Hovered = !HoveredInventoryRecipe.IsNone() ? HoveredInventoryRecipe : HoveredButton;
    const FString Id = Hovered == TEXT("CraftOutput")
        ? FString::Printf(TEXT("CraftRecipe_%d"), SelectedRecipeIndex) : Hovered.ToString();
    FString Title;
    TArray<FString> Details;
    if (Id.StartsWith(TEXT("InventoryBlock_")))
    {
        const int32 Slot = FCString::Atoi(*Id.Mid(15));
        if (Slot < 0 || Slot >= 9) return;
        const EBlockType Type = static_cast<EBlockType>(Slot + 1);
        Title = BlockDisplayName(Type);
        Details.Add(FString::Printf(TEXT("数量：%d"), Player->GetInventoryCount(Slot)));
        Details.Add(FBlockRealmSurvivalData::GetMiningRequirementText(Type));
        Details.Add(Type == EBlockType::Water ? TEXT("不可携带，不可放置") : TEXT("点击装备 · 右键放置"));
    }
    else if (Id.StartsWith(TEXT("InventoryTool_")))
    {
        FBlockRealmToolSlot Tool;
        if (!Player->GetToolSlot(FCString::Atoi(*Id.Mid(14)), Tool) || !Tool.IsUsable()) return;
        Title = FBlockRealmSurvivalData::GetToolDisplayName(Tool.Type);
        Details.Add(FString::Printf(TEXT("数量：%d"), Tool.Quantity));
        if (Tool.Type != EBlockRealmToolType::Torch)
            Details.Add(FString::Printf(TEXT("耐久：%d / %d"), Tool.Durability, Tool.MaxDurability));
        Details.Add(Tool.Type == EBlockRealmToolType::StonePickaxe ? TEXT("可采集岩石、辉光石和玻璃")
            : Tool.Type == EBlockRealmToolType::WoodenPickaxe ? TEXT("可采集岩石") : TEXT("右键放置照明"));
        Details.Add(TEXT("点击装备"));
    }
    else if (Hovered == TEXT("InventoryFood"))
    {
        const UBlockRealmPlayerActionsComponent* Actions = Player->FindComponentByClass<UBlockRealmPlayerActionsComponent>();
        Title = TEXT("食物");
        Details.Add(FString::Printf(TEXT("数量：%d"), Actions ? Actions->GetFoodCount() : 0));
        Details.Add(TEXT("食用后 +28 饥饿 / +4 生命"));
        Details.Add(TEXT("采集 4 块树叶获得 1 份"));
        Details.Add(TEXT("点击食用 · 游戏中按 C"));
    }
    else if (Hovered == TEXT("InventoryPlanks"))
    {
        Title = TEXT("木板");
        Details.Add(FString::Printf(TEXT("数量：%d"), Player->GetCraftedPlankCount()));
        Details.Add(TEXT("合成材料 · 由木材制作"));
        Details.Add(TEXT("点击查看木镐配方"));
    }
    else if (Hovered == TEXT("InventoryBareHands"))
    {
        Title = TEXT("空手");
        Details.Add(TEXT("可采集泥土、木材等软质方块"));
        Details.Add(TEXT("点击收起当前物品"));
    }
    else if (Id.StartsWith(TEXT("CraftRecipe_")))
    {
        const int32 Index = FCString::Atoi(*Id.Mid(12));
        const TArray<FBlockRealmSurvivalRecipe>& Recipes = Player->GetCraftingRecipes();
        if (!Recipes.IsValidIndex(Index)) return;
        Title = Recipes[Index].DisplayName;
        Details.Add(RecipeResultText(Recipes[Index]));
        Details.Add(TEXT("材料：") + RecipeIngredientsText(Recipes[Index]));
        FString Missing;
        Details.Add(CanCraftRecipe(Player, Recipes[Index], Missing) ? TEXT("点击合成") : TEXT("缺少：") + Missing);
    }
    if (Title.IsEmpty()) return;
    constexpr float Width = 510.f;
    const float Height = 50.f + Details.Num() * 31.f;
    float X = MousePosition.X + 20.f;
    float Y = MousePosition.Y + 18.f;
    if (X + Width > DesignWidth - 16.f) X = MousePosition.X - Width - 20.f;
    if (Y + Height > DesignHeight - 16.f) Y = MousePosition.Y - Height - 18.f;
    X = FMath::Clamp(X, 16.f, DesignWidth - Width - 16.f);
    Y = FMath::Clamp(Y, 16.f, DesignHeight - Height - 16.f);
    Fill(X - 3.f, Y - 3.f, Width + 6.f, Height + 6.f, FLinearColor(.22f, .08f, .38f, .99f));
    Fill(X, Y, Width, Height, FLinearColor(.025f, .009f, .045f, .99f));
    Text(Title, X + 17.f, Y + 12.f, 26.f, Ivory, false, Width - 34.f);
    for (int32 Index = 0; Index < Details.Num(); ++Index)
        Text(Details[Index], X + 17.f, Y + 51.f + Index * 31.f, 22.f, Muted, false, Width - 34.f);
}

void ABlockRealmHUD::DrawInventory(ABlockRealmCharacter* Player)
{
    if (!Player) return;
    constexpr float PanelX = InventoryPanelX;
    constexpr float PanelY = InventoryPanelY;
    constexpr float PanelW = 1040.f;
    constexpr float PanelH = 910.f;
    const FLinearColor Label(.13f, .13f, .13f);
    FVector2D MousePosition(-1000.f, -1000.f);
    if (APlayerController* PC = Cast<APlayerController>(PlayerOwner))
    {
        float MouseX = 0.f, MouseY = 0.f;
        if (PC->GetMousePosition(MouseX, MouseY)) MousePosition = (FVector2D(MouseX, MouseY) - UiOrigin) / UiScale;
    }
    // Read cursor geometry directly so hover information remains stable while
    // Character owns the inventory input mode and existing click actions.
    HoveredButton = NAME_None;
    HoveredInventoryRecipe = NAME_None;
    TArray<FName> Names = {TEXT("InventoryFood"), TEXT("InventoryPlanks"), TEXT("InventoryBareHands"), TEXT("InventoryClose"), TEXT("CraftOutput")};
    for (int32 Index = 0; Index < 9; ++Index) Names.Add(FName(*FString::Printf(TEXT("InventoryBlock_%d"), Index)));
    for (int32 Index = 0; Index < Player->GetToolSlotCount(); ++Index) Names.Add(FName(*FString::Printf(TEXT("InventoryTool_%d"), Index)));
    for (int32 Index = 0; Index < 5; ++Index) Names.Add(FName(*FString::Printf(TEXT("CraftRecipe_%d"), Index)));
    for (const FName Name : Names)
    {
        const FBox2D Box = DesignInventoryButtonBounds(Name);
        if (Box.bIsValid && Box.IsInside(MousePosition))
        {
            if (Name.ToString().StartsWith(TEXT("CraftRecipe_"))) HoveredInventoryRecipe = Name;
            else HoveredButton = Name;
            break;
        }
    }
    Fill(-UiOrigin.X / UiScale, -UiOrigin.Y / UiScale, Canvas->ClipX / UiScale, Canvas->ClipY / UiScale, Ink.CopyWithNewOpacity(.65f));
    Fill(PanelX - 5.f, PanelY - 5.f, PanelW + 10.f, PanelH + 10.f, FLinearColor(.10f, .10f, .10f));
    Fill(PanelX, PanelY, PanelW, PanelH, FLinearColor(.88f, .88f, .88f));
    Fill(PanelX + 5.f, PanelY + 5.f, PanelW - 5.f, PanelH - 5.f, FLinearColor(.25f, .25f, .25f));
    Fill(PanelX + 5.f, PanelY + 5.f, PanelW - 10.f, PanelH - 10.f, FLinearColor(.59f, .59f, .59f));
    Text(TEXT("合成"), PanelX + 40.f, PanelY + 31.f, 30.f, Label);
    const FBox2D CloseBox = DesignInventoryButtonBounds(TEXT("InventoryClose"));
    Fill(CloseBox.Min.X, CloseBox.Min.Y, CloseBox.GetSize().X, CloseBox.GetSize().Y, FLinearColor(.22f, .22f, .22f));
    Fill(CloseBox.Min.X + 3.f, CloseBox.Min.Y + 3.f, CloseBox.GetSize().X - 6.f, CloseBox.GetSize().Y - 6.f,
        HoveredButton == TEXT("InventoryClose") ? FLinearColor(.53f, .53f, .53f) : FLinearColor(.37f, .37f, .37f));
    Text(TEXT("关闭 E / Esc"), CloseBox.GetCenter().X, CloseBox.Min.Y + 12.f, 21.f, Ivory, true, 132.f);
    AddInventoryHitBox(TEXT("InventoryClose"));

    const TArray<FBlockRealmSurvivalRecipe>& Recipes = Player->GetCraftingRecipes();
    const int32 DisplayIndex = Recipes.IsValidIndex(SelectedRecipeIndex) ? SelectedRecipeIndex : 0;
    const FBlockRealmSurvivalRecipe* Recipe = Recipes.IsValidIndex(DisplayIndex) ? &Recipes[DisplayIndex] : nullptr;
    constexpr float CraftSize = 80.f;
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const float X = PanelX + 40.f + (Index % 2) * 88.f;
        const float Y = PanelY + 90.f + (Index / 2) * 88.f;
        DrawRecessedSlot(X, Y, CraftSize);
        if (!Recipe || !Recipe->Ingredients.IsValidIndex(Index)) continue;
        const FBlockRealmItemStack& Item = Recipe->Ingredients[Index];
        const EBlockType Block = ItemToBlock(Item.Item);
        if (Block != EBlockType::Air) CubeIcon(X + 40.f, Y + 33.f, 36.f, Block);
        else Text(BlockRealmCrafting::GetCraftingItemDisplayName(Item.Item), X + 40.f, Y + 24.f, 22.f, Ivory, true, 70.f);
        Text(FString::Printf(TEXT("%d"), Item.Count), X + 58.f, Y + 53.f, 23.f, Ivory, true, 36.f);
    }
    Text(TEXT("→"), PanelX + 242.f, PanelY + 146.f, 36.f, Label, true);
    DrawRecessedSlot(PanelX + 292.f, PanelY + 134.f, 96.f, HoveredButton == TEXT("CraftOutput"));
    AddInventoryHitBox(TEXT("CraftOutput"));
    if (Recipe && Recipe->Results.IsValidIndex(0))
    {
        const FBlockRealmItemStack& Result = Recipe->Results[0];
        const EBlockType Block = ItemToBlock(Result.Item);
        if (Block != EBlockType::Air) CubeIcon(PanelX + 340.f, PanelY + 173.f, 46.f, Block);
        else if (Result.Item == EBlockRealmItemType::Planks)
        {
            CubeIcon(PanelX + 340.f, PanelY + 173.f, 46.f, EBlockType::Wood);
            Line(PanelX + 316.f, PanelY + 176.f, PanelX + 364.f, PanelY + 185.f, Ivory, 3.f);
        }
        else ToolIcon(PanelX + 340.f, PanelY + 173.f, 53.f,
            Result.Item == EBlockRealmItemType::WoodenPickaxe ? EBlockRealmToolType::WoodenPickaxe
            : Result.Item == EBlockRealmItemType::StonePickaxe ? EBlockRealmToolType::StonePickaxe : EBlockRealmToolType::Torch);
        Text(FString::FromInt(Result.Count), PanelX + 363.f, PanelY + 201.f, 24.f, Ivory, true, 36.f);
    }
    Text(TEXT("配方"), PanelX + 488.f, PanelY + 39.f, 26.f, Label);
    for (int32 Index = 0; Index < Recipes.Num(); ++Index)
    {
        const FName Name(*FString::Printf(TEXT("CraftRecipe_%d"), Index));
        const FBox2D Box = DesignInventoryButtonBounds(Name);
        if (!Box.bIsValid) continue;
        DrawRecessedSlot(Box.Min.X, Box.Min.Y, Box.GetSize().X, HoveredInventoryRecipe == Name, DisplayIndex == Index);
        const EBlockRealmItemType Item = Recipes[Index].Results.IsValidIndex(0) ? Recipes[Index].Results[0].Item : EBlockRealmItemType::None;
        const EBlockType Block = ItemToBlock(Item);
        if (Block != EBlockType::Air) CubeIcon(Box.GetCenter().X, Box.Min.Y + 37.f, 40.f, Block);
        else if (Item == EBlockRealmItemType::Planks)
        {
            CubeIcon(Box.GetCenter().X, Box.Min.Y + 37.f, 40.f, EBlockType::Wood);
            Line(Box.Min.X + 25.f, Box.Min.Y + 37.f, Box.Max.X - 25.f, Box.Min.Y + 45.f, Ivory, 3.f);
        }
        else ToolIcon(Box.GetCenter().X, Box.Min.Y + 37.f, 45.f,
            Item == EBlockRealmItemType::WoodenPickaxe ? EBlockRealmToolType::WoodenPickaxe
            : Item == EBlockRealmItemType::StonePickaxe ? EBlockRealmToolType::StonePickaxe : EBlockRealmToolType::Torch);
        Text(Recipes[Index].DisplayName, Box.GetCenter().X, Box.Min.Y + 65.f, 19.f, Ivory, true, 78.f);
        AddInventoryHitBox(Name);
    }
    if (Recipe)
    {
        Text(Recipe->DisplayName, PanelX + 488.f, PanelY + 197.f, 25.f, Label, false, 500.f);
        Text(RecipeIngredientsText(*Recipe), PanelX + 488.f, PanelY + 232.f, 22.f, Label, false, 500.f);
        FString Missing;
        Text(CanCraftRecipe(Player, *Recipe, Missing) ? TEXT("点击配方合成") : TEXT("缺少：") + Missing,
            PanelX + 488.f, PanelY + 267.f, 22.f, Label, false, 340.f);
    }
    const FBox2D HandsBox = DesignInventoryButtonBounds(TEXT("InventoryBareHands"));
    Fill(HandsBox.Min.X, HandsBox.Min.Y, HandsBox.GetSize().X, HandsBox.GetSize().Y,
        FLinearColor(HoveredButton == TEXT("InventoryBareHands") ? .38f : .28f, .28f, .28f));
    Text(TEXT("空手"), HandsBox.GetCenter().X, HandsBox.Min.Y + 12.f, 22.f, Ivory, true);
    AddInventoryHitBox(TEXT("InventoryBareHands"));

    Text(TEXT("物品栏"), PanelX + 40.f, PanelY + 305.f, 30.f, Label);
    Text(TEXT("分类储物 · 点击取用"), PanelX + 682.f, PanelY + 313.f, 21.f, Label, false, 320.f);
    for (int32 Row = 0; Row < 3; ++Row)
        for (int32 Column = 0; Column < 9; ++Column)
            DrawRecessedSlot(PanelX + 40.f + Column * 104.f, PanelY + 352.f + Row * 104.f, 96.f);
    for (int32 Slot = 0; Slot < Player->GetToolSlotCount(); ++Slot) DrawInventoryToolSlot(Player, Slot);
    const FBox2D FoodBox = DesignInventoryButtonBounds(TEXT("InventoryFood"));
    DrawRecessedSlot(FoodBox.Min.X, FoodBox.Min.Y, 96.f, HoveredButton == TEXT("InventoryFood"));
    Fill(FoodBox.Min.X + 22.f, FoodBox.Min.Y + 30.f, 52.f, 31.f, FLinearColor(.70f, .37f, .10f));
    Fill(FoodBox.Min.X + 29.f, FoodBox.Min.Y + 23.f, 38.f, 11.f, FLinearColor(.91f, .65f, .28f));
    const UBlockRealmPlayerActionsComponent* Actions = Player->FindComponentByClass<UBlockRealmPlayerActionsComponent>();
    Text(CountText(Actions ? Actions->GetFoodCount() : 0), FoodBox.Max.X - 33.f, FoodBox.Max.Y - 29.f, 24.f, Ivory, true, 60.f);
    AddInventoryHitBox(TEXT("InventoryFood"));
    const FBox2D PlanksBox = DesignInventoryButtonBounds(TEXT("InventoryPlanks"));
    DrawRecessedSlot(PlanksBox.Min.X, PlanksBox.Min.Y, 96.f, HoveredButton == TEXT("InventoryPlanks"));
    CubeIcon(PlanksBox.GetCenter().X, PlanksBox.Min.Y + 41.f, 45.f, EBlockType::Wood);
    Line(PlanksBox.Min.X + 24.f, PlanksBox.Min.Y + 44.f, PlanksBox.Max.X - 24.f, PlanksBox.Min.Y + 53.f, Ivory, 3.f);
    Text(CountText(Player->GetCraftedPlankCount()), PlanksBox.Max.X - 33.f, PlanksBox.Max.Y - 29.f, 24.f, Ivory, true, 60.f);
    AddInventoryHitBox(TEXT("InventoryPlanks"));
    for (int32 Slot = 0; Slot < 9; ++Slot)
    {
        const FBox2D Box = DesignInventoryButtonBounds(FName(*FString::Printf(TEXT("InventoryBlock_%d"), Slot)));
        DrawInventorySlot(Player, Slot, Box.Min.X, Box.Min.Y, 96.f, Player->IsBlockEquipped() && Slot == Player->GetSelectedSlot());
    }
    const bool bNotice = FPlatformTime::Seconds() < InventoryNoticeUntil && !InventoryNotice.IsEmpty();
    if (bNotice)
        WrappedText(InventoryNotice, PanelX + 40.f, PanelY + 810.f, 23.f, Label, 960.f, 2);
    else
    {
        Text(TEXT("点击取用 · 悬停查看说明 · E / Esc 关闭"), PanelX + 40.f, PanelY + 810.f, 23.f, Label, false, 960.f);
        Text(TEXT("WASD 移动 · 左键采集/攻击 · 右键放置 · C 进食"), PanelX + 40.f, PanelY + 847.f, 21.f, Label, false, 960.f);
        Text(TEXT("Q / R 工具 · 0 空手 · V 作物 · T 熔炉 · B 床 · F5 保存"), PanelX + 40.f, PanelY + 878.f, 21.f, Label, false, 960.f);
    }
    DrawInventoryTooltip(Player, MousePosition);
}

void ABlockRealmHUD::SetInventoryNotice(const FString& Notice)
{
    InventoryNotice = Notice;
    InventoryNoticeUntil = FPlatformTime::Seconds() + 3.5;
}

void ABlockRealmHUD::SetInventoryOpen(bool bOpen)
{
    if (bInventoryOpen == bOpen)
    {
        return;
    }
    bInventoryOpen = bOpen;
    HoveredInventoryRecipe = NAME_None;
    if (APlayerController* PC = Cast<APlayerController>(PlayerOwner))
    {
        const ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
        const bool bMenuInput = bOpen || (Game && Game->IsMenuOpen());
        PC->ResetIgnoreMoveInput();
        PC->ResetIgnoreLookInput();
        PC->SetIgnoreMoveInput(bMenuInput);
        PC->SetIgnoreLookInput(bMenuInput);
        PC->bShowMouseCursor = bMenuInput;
        PC->bEnableClickEvents = bMenuInput;
        PC->bEnableMouseOverEvents = bMenuInput;
        if (bMenuInput)
        {
            FInputModeGameAndUI InputMode;
            InputMode.SetHideCursorDuringCapture(false);
            InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(InputMode);
            if (ABlockRealmCharacter* Player = PlayerOwner
                ? Cast<ABlockRealmCharacter>(PlayerOwner->GetPawn()) : nullptr)
            {
                Player->CancelGameplayInput();
            }
        }
        else
        {
            PC->SetInputMode(FInputModeGameOnly());
        }
    }
}

void ABlockRealmHUD::HandleInventoryInput()
{
    ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    APlayerController* PC = Cast<APlayerController>(PlayerOwner);
    if (!Game || !PC)
    {
        return;
    }
    ABlockRealmCharacter* Player = Game->GetPlayerCharacter();
    if (!Game->IsAdventureActive() && !Game->IsPausedMenu())
    {
        // Returning to the title resets the character overlay; also restore
        // the HUD input mode in case the transition happened while the panel
        // was open.
        if (bInventoryOpen)
        {
            SetInventoryOpen(false);
        }
        return;
    }
    // Character owns the E binding. Poll its public state so the HUD never
    // double-toggles the same key when both actors receive the input event.
    const bool bCharacterInventoryOpen = ReadInventoryOpen(Player, 0);
    if (bCharacterInventoryOpen != bInventoryOpen && Game->IsAdventureActive())
    {
        SetInventoryOpen(bCharacterInventoryOpen);
    }

    const bool bEscPressed = PC->IsInputKeyDown(EKeys::Escape);
    if (bEscPressed && !bEscapeKeyHeld && bInventoryOpen)
    {
        // Character::PausePressed owns Escape and closes its inventory state
        // before opening the pause menu. Only mirror that result here.
        SetInventoryOpen(false);
        if (Game->IsPausedMenu())
        {
            Game->TogglePauseMenu();
        }
    }
    bEscapeKeyHeld = bEscPressed;
    if (bInventoryOpen && Game->IsPausedMenu())
    {
        SetInventoryOpen(false);
    }
}

void ABlockRealmHUD::DrawNotice(const FString& Notice, float Y)
{
    if (Notice.IsEmpty()) return;
    Fill(460.f, Y - 10.f, 1000.f, 76.f, Ink.CopyWithNewOpacity(.95f));
    Fill(460.f, Y - 10.f, 4.f, 76.f, Mint);
    WrappedText(Notice, 960.f, Y + 4.f, 22.f, Ivory, 948.f, 2, true);
}

void ABlockRealmHUD::DrawMining(ABlockRealmCharacter* Player, float CenterX, float CenterY)
{
    if (!Player || !Player->IsMining()) return;
    const float Progress = FMath::Clamp(Player->GetMiningProgress(), 0.f, 1.f);
    const float Duration = Player->GetMiningDuration();
    constexpr float Width = 400.f;
    const float Left = CenterX - Width * .5f;
    const float Top = CenterY + 72.f;
    constexpr float BarWidth = Width - 40.f;
    Fill(Left, Top, Width, 104.f, Ink.CopyWithNewOpacity(.94f));
    Line(Left, Top, Left + Width, Top, Mint.CopyWithNewOpacity(.75f), 2.f);
    Text(TEXT("正在挖掘"), Left + 20.f, Top + 14.f, 22.f, Ivory, false, 270.f);
    Text(FString::Printf(TEXT("%d%%"), FMath::FloorToInt(Progress * 100.f)),
        Left + Width - 48.f, Top + 14.f, 22.f, Mint, true, 68.f);
    Fill(Left + 20.f, Top + 49.f, BarWidth, 10.f, Ivory.CopyWithNewOpacity(.17f));
    Fill(Left + 20.f, Top + 49.f, BarWidth * Progress, 10.f, Mint);
    const FString Hint = Duration > 0.f
        ? FString::Printf(TEXT("按住左键 · 还需约 %.1f 秒"), Duration * (1.f - Progress))
        : TEXT("保持按住左键");
    Text(Hint, CenterX, Top + 74.f, 18.f, Muted, true, BarWidth);
}

void ABlockRealmHUD::DrawPlaying(ABlockRealmGameMode* Game, ABlockRealmCharacter* Player)
{
    if (!Game->IsMenuOpen() && !bInventoryOpen)
    {
        const UBlockRealmPlayerActionsComponent* Actions = Player
            ? Player->FindComponentByClass<UBlockRealmPlayerActionsComponent>() : nullptr;
        if (Actions && Actions->IsHeadUnderwater())
        {
            const float Left = -UiOrigin.X / UiScale;
            const float Top = -UiOrigin.Y / UiScale;
            const float Width = Canvas->ClipX / UiScale;
            const float Height = Canvas->ClipY / UiScale;
            // A faint cool rim leaves the world centre clear; all HUD text is
            // painted afterwards, including the red low-breath warning.
            for (int32 Index = 0; Index < 4; ++Index)
            {
                const float Inset = Index * 10.f;
                const FLinearColor WaterColor(.10f, .40f, .64f, .07f - Index * .015f);
                Fill(Left + Inset, Top + Inset, Width - 2.f * Inset, 10.f, WaterColor);
                Fill(Left + Inset, Top + Height - Inset - 10.f, Width - 2.f * Inset, 10.f, WaterColor);
                Fill(Left + Inset, Top + Inset + 10.f, 10.f, Height - 2.f * (Inset + 10.f), WaterColor);
                Fill(Left + Width - Inset - 10.f, Top + Inset + 10.f,
                    10.f, Height - 2.f * (Inset + 10.f), WaterColor);
            }
        }
        // Draw the edge tint first so survival text and controls stay readable.
        DrawDamageFeedback(Player);
    }
    Fill(32.f, 32.f, 310.f, 110.f, Ink.CopyWithNewOpacity(.91f));
    CubeIcon(62.f, 66.f, 28.f, EBlockType::Grass);
    Text(TEXT("方境"), 96.f, 46.f, 28.f, Ivory);
    const bool bFlying = Player && Player->IsFlying();
    const bool bInWater = Player && Player->IsInWater();
    Text(bFlying ? TEXT("飞行") : bInWater ? TEXT("水中") : TEXT("步行"), 278.f, 51.f, 21.f,
        bFlying || bInWater ? Mint : Muted, true, 84.f);
    Text(bFlying ? TEXT("F 关闭飞行") : bInWater ? TEXT("Space 上浮 · Ctrl 下降") : TEXT("F 开启飞行"),
        50.f, 101.f, 19.f, bFlying ? Mint : Muted, false, 274.f);
    DrawSurvivalBars(Player);
    DrawFoodStatus(Player);
    DrawCompass(Player);

    Fill(1466.f, 32.f, 422.f, 160.f, Ink.CopyWithNewOpacity(.91f));
    Text(FramesPerSecond > 0.f ? FString::Printf(TEXT("%.0f FPS"), FramesPerSecond) : TEXT("— FPS"),
        1484.f, 46.f, 25.f, Ivory);
    Text(FrameMilliseconds > 0.f ? FString::Printf(TEXT("%.1f ms"), FrameMilliseconds) : TEXT("— ms"),
        1760.f, 50.f, 20.f, Muted, false, 110.f);
    ABlockWorld* World = Game->GetWorldActor();
    if (World)
    {
        const int32 Pending = World->GetPendingChunkCount();
        const FString Chunks = Pending > 0
            ? FString::Printf(TEXT("区块 %d · 加载中 %d"), World->GetLoadedChunkCount(), Pending)
            : FString::Printf(TEXT("区块 %d · 山野已就绪"), World->GetLoadedChunkCount());
        Text(Chunks, 1484.f, 85.f, 19.f, Muted, false, 386.f);
    }
    Text(GraphicsModeText(Game->GetGraphicsStatus()), 1484.f, 118.f, 18.f, Mint, false, 386.f);
    const float NormalizedTime = FMath::Clamp(Game->GetTimeOfDay(), 0.f, .9999f);
    const int32 GameHour = FMath::FloorToInt(NormalizedTime * 24.f) % 24;
    const FString WorldClock = FString::Printf(TEXT("%s %02d:00 · 动物 %d / 怪物 %d"),
        Game->IsNight() ? TEXT("夜晚") : TEXT("白昼"), GameHour, Game->GetActiveAnimalCount(), Game->GetActiveMobCount());
    Text(WorldClock, 1484.f, 153.f, 18.f, Game->IsNight() ? Amber : Muted, false, 386.f);
    DrawToolDurability(Player);

    FString TargetName = Player && Player->HasTarget()
        ? BlockDisplayName(Player->GetTargetType()) : FString();
    FString ObjectHint;
    if (Player && World && Player->GetBlockWorld() == World && Player->CanUsePlayerActions())
    {
        const UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>();
        const AController* Controller = Player->GetController();
        if (Camera && Controller)
        {
            // Match V/T/B's real camera, reach and visible-mesh/voxel-occlusion
            // queries. Reading a facility must never trigger its action.
            constexpr float ObjectReach = 600.f;
            const FVector Origin = Camera->GetComponentLocation();
            const FVector Direction = Controller->GetControlRotation().Vector();
            if (ABlockRealmCrop* Crop = ABlockRealmCrop::FindAimedCrop(GetWorld(), World, Origin, Direction, ObjectReach))
            {
                TargetName = Crop->IsMature() ? TEXT("成熟作物") : TEXT("作物");
                ObjectHint = Crop->IsMature() ? TEXT("V 收获 · 食物 +3")
                    : FString::Printf(TEXT("生长 %.0f%% · 成熟后按 V 收获"),
                        100.f * Crop->GetGrowthSeconds() / Crop->GetGrowthDurationSeconds());
            }
            else if (ABlockRealmFurnace* Furnace = ABlockRealmFurnace::FindAimedFurnace(GetWorld(), World, Origin, Direction, ObjectReach))
            {
                TargetName = Furnace->IsBurning()
                    ? FString::Printf(TEXT("熔炉 · 烧制 %.0f%%"),
                        100.f * Furnace->GetCookProgress() / Furnace->GetCookDurationSeconds())
                    : FString(TEXT("熔炉"));
                if (Furnace->GetOutputGlass() > 0)
                    ObjectHint = FString::Printf(TEXT("T 领取玻璃 ×%d"), Furnace->GetOutputGlass());
                else if (Player->GetInventoryCount(3) < 1 || Player->GetInventoryCount(4) < 1)
                    ObjectHint = TEXT("投料需要 1 沙块和 1 木材");
                else if (Furnace->CanAddBatch(1, 1))
                    ObjectHint = TEXT("T 投料 · 1 沙块 + 1 木材");
                else ObjectHint = TEXT("熔炉已装满 · 等待出炉");
            }
            else if (ABlockRealmRestPoint::FindAimedRestPoint(GetWorld(), World, Origin, Direction, ObjectReach))
            {
                TargetName = TEXT("床");
                ObjectHint = TEXT("B 休息 / 设置营地重生点");
            }
        }
    }
    if (ObjectHint.IsEmpty() && Player && Player->HasTarget() && !Player->IsMining()
        && !FBlockRealmSurvivalData::CanMineBlock(Player->GetTargetType(), Player->GetSelectedToolType()))
    {
        ObjectHint = FBlockRealmSurvivalData::GetMiningRequirementText(Player->GetTargetType());
    }
    const FLinearColor Reticle = !ObjectHint.IsEmpty() || (Player && Player->HasTarget())
        ? Mint : Ivory.CopyWithNewOpacity(.9f);
    const float CX = (Canvas->ClipX * .5f - UiOrigin.X) / UiScale;
    const float CY = (Canvas->ClipY * .5f - UiOrigin.Y) / UiScale;
    for (int32 Pass = 0; Pass < 2; ++Pass)
    {
        const FLinearColor Color = Pass == 0 ? Ink.CopyWithNewOpacity(.75f) : Reticle;
        const float Thickness = Pass == 0 ? 3.f : 1.3f;
        Line(CX - 10.f, CY, CX - 4.f, CY, Color, Thickness);
        Line(CX + 4.f, CY, CX + 10.f, CY, Color, Thickness);
        Line(CX, CY - 10.f, CX, CY - 4.f, Color, Thickness);
        Line(CX, CY + 4.f, CX, CY + 10.f, Color, Thickness);
    }
    if (!TargetName.IsEmpty())
    {
        Fill(CX - 220.f, CY + 26.f, 440.f, 36.f, Ink.CopyWithNewOpacity(.93f));
        Text(TargetName, CX, CY + 32.f, 22.f, Ivory, true, 416.f);
    }
    if (!ObjectHint.IsEmpty() && Player && !Player->IsMining())
    {
        // The cooldown is at Y+65; contextual panels start below it.
        Fill(CX - 270.f, CY + 72.f, 540.f, 42.f, Ink.CopyWithNewOpacity(.94f));
        Text(ObjectHint, CX, CY + 83.f, 20.f, Mint, true, 512.f);
    }
    if (!Game->IsMenuOpen() && !bInventoryOpen)
    {
        DrawMining(Player, CX, CY);
        DrawCombatFeedback(Player, CX, CY);
    }
    DrawHotbar(Player);
    if (!Game->IsMenuOpen())
    {
        const FString Interaction = Player ? Player->GetInteractionNotice() : FString();
        if (!Interaction.IsEmpty())
        {
            DrawNotice(Interaction, 142.f);
        }
        else if (FPlatformTime::Seconds() < GameNoticeUntil)
        {
            DrawNotice(LastGameNotice, 142.f);
        }
    }
    if (bInventoryOpen || (Player && Player->IsInventoryOpen()))
    {
        DrawInventory(Player);
    }
}

void ABlockRealmHUD::UpdateMetrics()
{
    const double Now = FPlatformTime::Seconds();
    if (LastFrameSeconds > 0.0)
    {
        MetricsSeconds += Now - LastFrameSeconds;
        ++MetricsFrames;
        if (MetricsSeconds >= .5 && MetricsFrames > 0)
        {
            FramesPerSecond = static_cast<float>(MetricsFrames / MetricsSeconds);
            FrameMilliseconds = static_cast<float>(MetricsSeconds * 1000.0 / MetricsFrames);
            MetricsSeconds = 0.0;
            MetricsFrames = 0;
        }
    }
    LastFrameSeconds = Now;
}

void ABlockRealmHUD::DrawHUD()
{
    Super::DrawHUD();
    if (!Canvas || Canvas->ClipX <= 0.f || Canvas->ClipY <= 0.f)
    {
        return;
    }
    if (!bFontAttempted)
    {
        bFontAttempted = true;
        ChineseFont = LoadObject<UFont>(nullptr, TEXT("/Game/BlockRealm/UI/F_MiSans.F_MiSans"));
        if (!ChineseFont && GEngine)
        {
            ChineseFont = GEngine->GetMediumFont();
        }
    }
    ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Game)
    {
        return;
    }
    UiScale = FMath::Min(Canvas->ClipX / DesignWidth, Canvas->ClipY / DesignHeight);
    UiOrigin = (FVector2D(Canvas->ClipX, Canvas->ClipY) - FVector2D(DesignWidth, DesignHeight) * UiScale) * .5f;
    UpdateMetrics();
    const FString GameNotice = Game->GetNotice();
    if (LastGameNotice != GameNotice)
    {
        LastGameNotice = GameNotice;
        GameNoticeUntil = FPlatformTime::Seconds() + 5.0;
    }
    if (!Game->IsMenuOpen())
    {
        HoveredButton = NAME_None;
        DrawPlaying(Game, Game->GetPlayerCharacter());
    }
    else if (Game->IsPausedMenu())
    {
        DrawPlaying(Game, Game->GetPlayerCharacter());
        DrawPause(Game);
    }
    else
    {
        DrawTitle(Game);
    }
}

void ABlockRealmHUD::NotifyHitBoxClick(FName BoxName)
{
    Super::NotifyHitBoxClick(BoxName);
    ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Game)
    {
        return;
    }
    ABlockRealmCharacter* InventoryPlayer = Game->GetPlayerCharacter();
    if (bInventoryOpen || (InventoryPlayer && InventoryPlayer->IsInventoryOpen()))
    {
        if (BoxName == TEXT("CraftOutput"))
            BoxName = FName(*FString::Printf(TEXT("CraftRecipe_%d"), SelectedRecipeIndex));
        if (BoxName == TEXT("InventoryClose"))
        {
            if (ABlockRealmCharacter* Player = Game->GetPlayerCharacter())
            {
                if (ReadInventoryOpen(Player, 0))
                {
                    InvokeToggleInventory(Player, 0);
                }
            }
            SetInventoryOpen(false);
            return;
        }
        const FString Name = BoxName.ToString();
        if (Name.StartsWith(TEXT("InventoryBlock_")) || Name.StartsWith(TEXT("InventoryTool_"))
            || BoxName == TEXT("InventoryBareHands"))
        {
            bool bEquipped = false;
            if (InventoryPlayer)
            {
                if (Name.StartsWith(TEXT("InventoryBlock_")))
                {
                    const int32 Slot = FCString::Atoi(*Name.Mid(15));
                    if (InventoryPlayer->GetInventoryCount(Slot) > 0)
                        bEquipped = InventoryPlayer->SelectInventoryBlockSlot(Slot);
                    else
                        InventoryPlayer->NotifyPlayerAction(TEXT("这种方块已经用完，先采集一些再装备。"));
                }
                else if (Name.StartsWith(TEXT("InventoryTool_")))
                {
                    bEquipped = InventoryPlayer->SelectToolSlot(FCString::Atoi(*Name.Mid(14)));
                }
                else
                {
                    bEquipped = InventoryPlayer->EquipBareHands();
                }
            }
            if (bEquipped)
            {
                if (InventoryPlayer->IsInventoryOpen()) InventoryPlayer->ToggleInventory();
                SetInventoryOpen(false);
                InventoryPlayer->NotifyPlayerAction(FString::Printf(TEXT("已装备 %s · Q / R 选工具，数字键选方块"),
                    *InventoryPlayer->GetHeldItemName()));
            }
            else
            {
                SetInventoryNotice(InventoryPlayer ? InventoryPlayer->GetInteractionNotice() : TEXT("暂时无法装备。"));
            }
            return;
        }
        if (BoxName == TEXT("InventoryFood"))
        {
            if (InventoryPlayer && InventoryPlayer->UseInventoryFood()) SetInventoryOpen(false);
            else SetInventoryNotice(InventoryPlayer ? InventoryPlayer->GetInteractionNotice() : TEXT("暂时无法进食。"));
            return;
        }
        if (BoxName == TEXT("InventoryPlanks"))
        {
            if (InventoryPlayer)
            {
                const TArray<FBlockRealmSurvivalRecipe>& Recipes = InventoryPlayer->GetCraftingRecipes();
                for (int32 Index = 0; Index < Recipes.Num(); ++Index)
                {
                    if (Recipes[Index].Id == TEXT("wooden_pickaxe"))
                    {
                        SelectedRecipeIndex = Index;
                        break;
                    }
                }
            }
            SetInventoryNotice(TEXT("已显示木镐配方：3 木板 + 1 木材。点击右侧木镐配方制作，再点击装备栏拿取。"));
            return;
        }
        if (Name.StartsWith(TEXT("CraftRecipe_")))
        {
            const int32 RecipeIndex = FCString::Atoi(*Name.Mid(12));
            ABlockRealmCharacter* Player = Game->GetPlayerCharacter();
            const TArray<FBlockRealmSurvivalRecipe>* RecipesPtr = Player
                ? &Player->GetCraftingRecipes() : &BlockRealmCrafting::GetCraftingRecipes();
            const TArray<FBlockRealmSurvivalRecipe>& Recipes = *RecipesPtr;
            if (Recipes.IsValidIndex(RecipeIndex))
            {
                SelectedRecipeIndex = RecipeIndex;
                FString Missing;
                if (!CanCraftRecipe(Player, Recipes[RecipeIndex], Missing))
                {
                    SetInventoryNotice(FString::Printf(TEXT("材料不足：%s"), *Missing));
                }
                else if (InvokeCraftRecipe(Player, RecipeIndex, 0))
                {
                    const int32 ResultCount = Recipes[RecipeIndex].Results.IsValidIndex(0)
                        ? Recipes[RecipeIndex].Results[0].Count : 1;
                    SetInventoryNotice(FString::Printf(TEXT("已合成 %s ×%d"),
                        *Recipes[RecipeIndex].DisplayName, ResultCount));
                }
                else
                {
                    SetInventoryNotice(Player ? Player->GetInteractionNotice() : TEXT("暂时无法合成，请重试。"));
                }
            }
        }
        return;
    }
    if (!Game->IsMenuOpen())
    {
        return;
    }
    HoveredButton = NAME_None;
    if (BoxName == TEXT("Start") && !Game->IsPausedMenu())
    {
        Game->StartAdventure();
    }
    else if (BoxName == TEXT("Quit") && !Game->IsPausedMenu())
    {
        Game->QuitAdventure();
        GameNoticeUntil = FPlatformTime::Seconds() + 5.0;
    }
    else if (BoxName == TEXT("Resume") && Game->IsPausedMenu())
    {
        Game->TogglePauseMenu();
    }
    else if (BoxName == TEXT("Save") && Game->IsPausedMenu())
    {
        Game->SaveAdventure();
        GameNoticeUntil = FPlatformTime::Seconds() + 5.0;
    }
    else if (BoxName == TEXT("Title") && Game->IsPausedMenu())
    {
        Game->ReturnToTitle();
        GameNoticeUntil = FPlatformTime::Seconds() + 5.0;
    }
    else if (BoxName == TEXT("TSR"))
    {
        Game->SetUpscaleMode(0);
    }
    else if (BoxName == TEXT("Quality"))
    {
        Game->SetUpscaleMode(1);
    }
    else if (BoxName == TEXT("Balanced"))
    {
        Game->SetUpscaleMode(2);
    }
    else if (BoxName == TEXT("Performance"))
    {
        Game->SetUpscaleMode(3);
    }
}

void ABlockRealmHUD::NotifyHitBoxBeginCursorOver(FName BoxName)
{
    Super::NotifyHitBoxBeginCursorOver(BoxName);
    if (bInventoryOpen && BoxName.ToString().StartsWith(TEXT("CraftRecipe_")))
    {
        HoveredInventoryRecipe = BoxName;
    }
    else
    {
        HoveredButton = BoxName;
    }
}

void ABlockRealmHUD::NotifyHitBoxEndCursorOver(FName BoxName)
{
    Super::NotifyHitBoxEndCursorOver(BoxName);
    if (HoveredInventoryRecipe == BoxName)
    {
        HoveredInventoryRecipe = NAME_None;
    }
    if (HoveredButton == BoxName)
    {
        HoveredButton = NAME_None;
    }
}
