#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "BlockTypes.h"
#include "BlockRealmSurvival.h"
#include "BlockRealmHUD.generated.h"

class ABlockRealmCharacter;
class ABlockRealmGameMode;
class UFont;

UCLASS()
class BLOCKREALM_API ABlockRealmHUD : public AHUD
{
    GENERATED_BODY()

public:
    ABlockRealmHUD();
    virtual void Tick(float DeltaSeconds) override;
    virtual void DrawHUD() override;
    virtual void NotifyHitBoxClick(FName BoxName) override;
    virtual void NotifyHitBoxBeginCursorOver(FName BoxName) override;
    virtual void NotifyHitBoxEndCursorOver(FName BoxName) override;

    // The drawing and hitboxes share this layout. Bounds are physical viewport pixels.
    // Names: Start, Quit, Resume, Save, Title, TSR, Quality, Balanced, Performance.
    static FBox2D GetMenuButtonBounds(FName ButtonName, const FVector2D& ViewportSize, bool bPaused);
    // InventoryBlock_0..8, InventoryTool_0..5, InventoryBareHands,
    // InventoryFood, InventoryPlanks, InventoryClose, CraftOutput and CraftRecipe_0..4.
    static FBox2D GetInventoryButtonBounds(FName ButtonName, const FVector2D& ViewportSize);

private:
    void Text(const FString& Value, float X, float Y, float Pixels, const FLinearColor& Color,
        bool bCentered = false, float MaxWidth = 0.f);
    void WrappedText(const FString& Value, float X, float Y, float Pixels, const FLinearColor& Color,
        float MaxWidth, int32 MaxLines = 2, bool bCentered = false);
    void Fill(float X, float Y, float Width, float Height, const FLinearColor& Color);
    void Line(float X1, float Y1, float X2, float Y2, const FLinearColor& Color, float Thickness = 1.f);
    void Triangle(const FVector2D& A, const FVector2D& B, const FVector2D& C, const FLinearColor& Color);
    void CubeIcon(float X, float Y, float Size, EBlockType Type, float Opacity = 1.f);
    void ToolIcon(float X, float Y, float Size, EBlockRealmToolType Type, float Opacity = 1.f);
    void Button(FName Id, const FString& Label, bool bPaused, bool bPrimary = false, bool bEnabled = true);
    void GraphicsButtons(ABlockRealmGameMode* Game, bool bPaused);
    void DrawTitle(ABlockRealmGameMode* Game);
    void DrawPause(ABlockRealmGameMode* Game);
    void DrawPlaying(ABlockRealmGameMode* Game, ABlockRealmCharacter* Player);
    void DrawMining(ABlockRealmCharacter* Player, float CenterX, float CenterY);
    void DrawHotbar(ABlockRealmCharacter* Player);
    void DrawSurvivalBars(ABlockRealmCharacter* Player);
    void DrawFoodStatus(ABlockRealmCharacter* Player);
    void DrawCompass(ABlockRealmCharacter* Player);
    void DrawCombatFeedback(ABlockRealmCharacter* Player, float CenterX, float CenterY);
    void DrawDamageFeedback(ABlockRealmCharacter* Player);
    void DrawToolDurability(ABlockRealmCharacter* Player);
    void DrawInventory(ABlockRealmCharacter* Player);
    void DrawRecessedSlot(float X, float Y, float Size, bool bHovered = false, bool bSelected = false);
    void DrawInventoryTooltip(ABlockRealmCharacter* Player, const FVector2D& MousePosition);
    void DrawInventorySlot(ABlockRealmCharacter* Player, int32 Slot, float X, float Y,
        float Size, bool bSelected = false);
    void DrawInventoryToolSlot(ABlockRealmCharacter* Player, int32 Slot);
    void AddInventoryHitBox(FName Name);
    void SetInventoryOpen(bool bOpen);
    void HandleInventoryInput();
    void SetInventoryNotice(const FString& Notice);
    void DrawNotice(const FString& Notice, float Y);
    void UpdateMetrics();

    UPROPERTY()
    TObjectPtr<UFont> ChineseFont;
    FName HoveredButton = NAME_None;
    float UiScale = 1.f;
    FVector2D UiOrigin = FVector2D::ZeroVector;
    double LastFrameSeconds = 0.0;
    double MetricsSeconds = 0.0;
    int32 MetricsFrames = 0;
    float FramesPerSecond = 0.f;
    float FrameMilliseconds = 0.f;
    bool bFontAttempted = false;
    bool bInventoryOpen = false;
    bool bEscapeKeyHeld = false;
    int32 SelectedRecipeIndex = 0;
    FName HoveredInventoryRecipe = NAME_None;
    FString InventoryNotice;
    double InventoryNoticeUntil = 0.0;
    FString LastGameNotice;
    double GameNoticeUntil = 0.0;
};
