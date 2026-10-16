#include "BlockRealmGameMode.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmDayNight.h"
#include "BlockRealmHUD.h"
#include "BlockRealmMob.h"
#include "BlockRealmPlayerActions.h"
#include "BlockRealmSurvival.h"
#include "BlockRealmTorch.h"
#include "BlockWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/HUDHitBox.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "UnrealClient.h"
#include "VoxelData.h"

// Only the isolated -BlockRealmSmoke path owns this fixture. Player actions
// under test are controller input; public save restores prepare known values.
struct FBlockRealmExtendedSmokeState
{
    int32 Stage = 0;
    float StartedAt = 0.f;
    float WaitStartedAt = 0.f;
    float OriginalDayLength = 900.f;
    FIntVector Root = FIntVector::ZeroValue;
    FIntVector TorchCell = FIntVector::ZeroValue;
    FIntVector TorchSupport = FIntVector::ZeroValue;
    FIntVector LastTorchCell = FIntVector::ZeroValue;
    FIntVector LastTorchSupport = FIntVector::ZeroValue;
    FIntVector LowSurfaceRoot = FIntVector::ZeroValue;
    FVector PlayerPosition = FVector::ZeroVector;
    FVector MobSpawnPosition = FVector::ZeroVector;
    FVector FrozenMobPosition = FVector::ZeroVector;
    FVector DeathPosition = FVector::ZeroVector;
    TWeakObjectPtr<ABlockRealmMob> Mob;
    FWorldSaveState FrozenPlayer;
    FWorldSaveState ExpectedSave;
    float FrozenMobHealth = 0.f;
    float FrozenClock = 0.f;
    float ActiveClock = 0.f;
    float MobHealthBefore = 0.f;
    float HealthBefore = 0.f;
    float HungerBefore = 0.f;
    int32 DurabilityBefore = 0;
    int32 FoodBefore = 0;
    int32 StoneBefore = 0;
    int32 GlowBefore = 0;
    int32 WoodBefore = 0;
    int32 PlanksBefore = 0;
    int32 TorchQuantityBefore = 0;
    TArray<int32> BlocksBeforeLastTorch;
};

namespace
{
    constexpr float ExtendedSmokeBudgetSeconds = 40.f;

    bool SameTools(const FWorldSaveState& A, const FWorldSaveState& B)
    {
        if (A.ToolSlots.Num() != B.ToolSlots.Num()) return false;
        for (int32 Index = 0; Index < A.ToolSlots.Num(); ++Index)
        {
            const FBlockRealmSavedTool& Left = A.ToolSlots[Index];
            const FBlockRealmSavedTool& Right = B.ToolSlots[Index];
            if (Left.Type != Right.Type || Left.Durability != Right.Durability
                || Left.MaxDurability != Right.MaxDurability || Left.Quantity != Right.Quantity) return false;
        }
        return A.SelectedToolSlot == B.SelectedToolSlot;
    }

    bool SameSurvival(const FWorldSaveState& A, const FWorldSaveState& B, float HungerTolerance)
    {
        return A.bHasSurvivalState && B.bHasSurvivalState
            && FMath::IsNearlyEqual(A.Health, B.Health, .01f)
            && FMath::IsNearlyEqual(A.Hunger, B.Hunger, HungerTolerance)
            && A.FoodCount == B.FoodCount && A.CraftedPlanks == B.CraftedPlanks
            && A.SelectedSlot == B.SelectedSlot && A.Inventory == B.Inventory && SameTools(A, B);
    }

    bool SamePlacedTorches(const FWorldSaveState& A, const FWorldSaveState& B)
    {
        if (A.PlacedTorches.Num() != B.PlacedTorches.Num()
            || A.PlacedTorchSupports.Num() != A.PlacedTorches.Num()
            || B.PlacedTorchSupports.Num() != B.PlacedTorches.Num()) return false;
        for (int32 Index = 0; Index < B.PlacedTorches.Num(); ++Index)
        {
            const int32 OtherIndex = A.PlacedTorches.Find(B.PlacedTorches[Index]);
            if (OtherIndex == INDEX_NONE || A.PlacedTorchSupports[OtherIndex] != B.PlacedTorchSupports[Index]) return false;
        }
        return true;
    }

    TArray<FIntVector> TorchPositions(UWorld* World, const ABlockWorld* BlockWorld)
    {
        TArray<FIntVector> Positions;
        for (TActorIterator<ABlockRealmTorch> It(World); It; ++It)
        {
            if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetBlockWorld() == BlockWorld)
                Positions.Add(It->GetBlockPosition());
        }
        Positions.Sort([](const FIntVector& A, const FIntVector& B)
        {
            if (A.X != B.X) return A.X < B.X;
            if (A.Y != B.Y) return A.Y < B.Y;
            return A.Z < B.Z;
        });
        return Positions;
    }

    TArray<FIntVector> TorchSupports(UWorld* World, const ABlockWorld* BlockWorld)
    {
        TMap<FIntVector, FIntVector> SupportsByCell;
        for (TActorIterator<ABlockRealmTorch> It(World); It; ++It)
        {
            if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetBlockWorld() == BlockWorld)
                SupportsByCell.Add(It->GetBlockPosition(), It->GetSupportBlock());
        }
        TArray<FIntVector> Supports;
        for (const FIntVector& Cell : TorchPositions(World, BlockWorld)) Supports.Add(SupportsByCell.FindChecked(Cell));
        return Supports;
    }

    int32 DropCount(UWorld* World, EBlockType Type)
    {
        int32 Count = 0;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            if (IsValid(*It) && !It->IsActorBeingDestroyed()
                && It->ActorHasTag(TEXT("BlockRealmDrop"))
                && It->ActorHasTag(FName(*BlockDisplayName(Type)))) ++Count;
        }
        return Count;
    }
}

bool ABlockRealmGameMode::TickExtendedSmoke()
{
    const auto Check = [this](bool Pass, const TCHAR* Name)
    {
        (Pass ? SmokeChecks : SmokeFailures).Add(Name);
        UE_LOG(LogTemp, Display, TEXT("BR_CHECK %s %s"), Pass ? TEXT("PASS") : TEXT("FAIL"), Name);
    };
    ABlockRealmCharacter* Player = GetPlayerCharacter();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!Player || !PC || !WorldActor || !DayNightActor)
    {
        Check(false, TEXT("Extended smoke has a player, controller, voxel world and clock"));
        return true;
    }
    UBlockRealmPlayerActionsComponent* Actions = Player->FindComponentByClass<UBlockRealmPlayerActionsComponent>();
    UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>();
    UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
    if (!Actions || !Camera || !Movement)
    {
        Check(false, TEXT("Extended smoke uses the real player actions, camera and movement components"));
        return true;
    }
    if (!ExtendedSmokeState.IsValid())
    {
        ExtendedSmokeState = MakeShared<FBlockRealmExtendedSmokeState>();
        ExtendedSmokeState->StartedAt = SmokeElapsed;
        ExtendedSmokeState->OriginalDayLength = DayNightActor->GetDayLengthSeconds();
    }
    FBlockRealmExtendedSmokeState& State = *ExtendedSmokeState;
    const auto Input = [PC](const FKey& Key, bool Down)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,
            Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    };
    const auto Key = [&Input](const FKey& Pressed) { Input(Pressed, true); Input(Pressed, false); };
    const auto Release = [&Input]()
    {
        for (const FKey& Pressed : { EKeys::W, EKeys::SpaceBar, EKeys::LeftControl, EKeys::F,
            EKeys::LeftMouseButton, EKeys::RightMouseButton, EKeys::MiddleMouseButton, EKeys::C })
            Input(Pressed, false);
    };
    const auto Shot = [](const TCHAR* Name)
    {
        FString Directory;
        if (!FParse::Value(FCommandLine::Get(), TEXT("BRValidationDir="), Directory))
            Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Validation"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,
            FString(Name) + TEXT(".png")), true, false);
    };
    const auto Click = [PC, &Key, &Check](FName Name)
    {
        ABlockRealmHUD* HUD = Cast<ABlockRealmHUD>(PC->GetHUD());
        int32 Width = 0;
        int32 Height = 0;
        PC->GetViewportSize(Width, Height);
        FVector2D Center;
        if (Name == TEXT("CraftRecipe_0"))
        {
            // Follow the shared inventory layout after window/DPI or UI changes.
            Center = ABlockRealmHUD::GetInventoryButtonBounds(Name, FVector2D(Width, Height)).GetCenter();
        }
        else
        {
            Center = ABlockRealmHUD::GetMenuButtonBounds(Name, FVector2D(Width, Height),
                Name == TEXT("Title")).GetCenter();
        }
        const FHUDHitBox* Hit = HUD ? HUD->GetHitBoxAtCoordinates(Center, true) : nullptr;
        const bool bFound = Width > 0 && Height > 0 && Hit && Hit->GetName() == Name;
        Check(bFound, *FString::Printf(TEXT("Extended HUD hitbox %s"), *Name.ToString()));
        if (!bFound) return false;
        PC->SetMouseLocation(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
        Key(EKeys::LeftMouseButton);
        return true;
    };
    const auto Aim = [PC, Camera, Player](const FVector& Target)
    {
        PC->SetControlRotation((Target - Camera->GetComponentLocation()).Rotation());
        Player->RefreshTarget();
    };
    const auto ResetMobPosition = [&State, &Aim]()
    {
        if (ABlockRealmMob* Mob = State.Mob.Get())
        {
            Mob->SetActorLocation(State.MobSpawnPosition, false, nullptr, ETeleportType::TeleportPhysics);
            Mob->GetCharacterMovement()->StopMovementImmediately();
            Aim(Mob->GetActorLocation());
        }
    };
    const auto SnapshotFrozen = [&State, Player, this]()
    {
        State.FrozenPlayer = Player->CaptureSaveState();
        State.FrozenClock = GetTimeOfDay();
        if (const ABlockRealmMob* Mob = State.Mob.Get())
        {
            State.FrozenMobPosition = Mob->GetActorLocation();
            State.FrozenMobHealth = Mob->GetHealth();
        }
    };
    const auto TryBlockedInput = [PC, &Input, &Key]()
    {
        PC->SetMouseLocation(10, 10);
        Input(EKeys::W, true);
        Input(EKeys::LeftMouseButton, true);
        Input(EKeys::RightMouseButton, true);
        Key(EKeys::MiddleMouseButton);
        Key(EKeys::C);
    };
    const auto CheckFrozen = [&State, Player, this, &Check](bool bInventory)
    {
        const ABlockRealmMob* Mob = State.Mob.Get();
        Check(Mob && FVector::Dist(Mob->GetActorLocation(), State.FrozenMobPosition) < .5f
            && FMath::IsNearlyEqual(Mob->GetHealth(), State.FrozenMobHealth, .01f),
            bInventory ? TEXT("E inventory freezes live mob position and health")
                : TEXT("Esc pause freezes live mob position and health"));
        Check(SameSurvival(Player->CaptureSaveState(), State.FrozenPlayer, .01f)
            && FVector::Dist(Player->GetActorLocation(), State.FrozenPlayer.Position) < 2.f,
            bInventory ? TEXT("Inventory blocks movement, mining, placement, attacks and food use")
                : TEXT("Pause blocks movement, mining, placement, attacks and food use"));
        Check(FMath::IsNearlyEqual(GetTimeOfDay(), State.FrozenClock, .000001f),
            bInventory ? TEXT("E inventory freezes an enabled day-night clock")
                : TEXT("Esc pause freezes an enabled day-night clock"));
    };

    if (SmokeElapsed - State.StartedAt >= ExtendedSmokeBudgetSeconds)
    {
        Check(false, TEXT("Extended smoke completes within its 40-second added budget"));
        Release();
        DayNightActor->SetAutoAdvance(false);
        DayNightActor->SetDayLengthSeconds(State.OriginalDayLength);
        Movement->SetComponentTickEnabled(true);
        if (ABlockRealmMob* Mob = State.Mob.Get()) Mob->Destroy();
        return true;
    }

    switch (State.Stage)
    {
    case 0:
        Check(IsAdventureActive() && !Player->IsInventoryOpen()
            && WorldActor->GetSavePath().Contains(TEXT("ValidationWorld")),
            TEXT("Extended fixture starts in the isolated active smoke adventure"));
        if (!IsAdventureActive() || Player->IsInventoryOpen()
            || !WorldActor->GetSavePath().Contains(TEXT("ValidationWorld"))) return true;
        Release();
        DayNightActor->SetAutoAdvance(false);
        DayNightActor->SetDayLengthSeconds(90.f);
        DayNightActor->SetTimeOfDay(.32f);
        NightMobSpawnAccumulator = 0.f;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            if (Cast<ABlockRealmMob>(*It) || It->ActorHasTag(TEXT("BlockRealmDrop"))
                || It->ActorHasTag(TEXT("BlockRealmTorch"))) It->Destroy();
        }
        ActiveMobs.Empty();
        State.Root = ABlockWorld::WorldToBlock(WorldActor->GetSpawnLocation());
        State.Root.Z = 52;
        for (int32 X = -3; X <= 6; ++X)
            for (int32 Y = -3; Y <= 3; ++Y)
            {
                WorldActor->SetBlock(State.Root + FIntVector(X, Y, 0), EBlockType::Stone);
                for (int32 Z = 1; Z <= 5; ++Z)
                    WorldActor->SetBlock(State.Root + FIntVector(X, Y, Z), EBlockType::Air);
            }
        State.PlayerPosition = ABlockWorld::BlockToWorld(State.Root);
        State.PlayerPosition.Z += 50.f + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
        {
            FWorldSaveState Fixture = Player->CaptureSaveState();
            Fixture.Position = State.PlayerPosition;
            Fixture.Rotation = FRotator::ZeroRotator;
            Fixture.Health = 100.f;
            Fixture.Hunger = 100.f;
            Fixture.FoodCount = 6;
            Fixture.CraftedPlanks = 0;
            Fixture.SelectedSlot = 0;
            Fixture.SelectedToolSlot = 1;
            Fixture.Inventory.Init(64, 9);
            Fixture.Inventory[7] = 0;
            Fixture.ToolSlots.SetNum(FBlockRealmSurvivalData::ToolSlotCount);
            Fixture.ToolSlots[1].Type = static_cast<int32>(EBlockRealmToolType::StonePickaxe);
            Fixture.ToolSlots[1].Durability = FBlockRealmSurvivalData::StonePickaxeDurability;
            Fixture.ToolSlots[1].MaxDurability = FBlockRealmSurvivalData::StonePickaxeDurability;
            Fixture.ToolSlots[1].Quantity = 1;
            Fixture.ToolSlots[2].Type = static_cast<int32>(EBlockRealmToolType::Torch);
            Fixture.ToolSlots[2].Durability = Fixture.ToolSlots[2].MaxDurability = 0;
            Fixture.ToolSlots[2].Quantity = 8;
            Player->RestoreSaveState(Fixture);
        }
        Movement->SetMovementMode(MOVE_None);
        Movement->SetComponentTickEnabled(false);
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .5f;
        break;
    case 1:
        {
            FHitResult Floor;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmExtendedFloor), false, Player);
            const bool bFloorReady = GetWorld()->LineTraceSingleByChannel(Floor, State.PlayerPosition,
                State.PlayerPosition - FVector(0.f, 0.f, 250.f), ECC_Visibility, Query)
                && FMath::IsNearlyEqual(Floor.ImpactPoint.Z, (State.Root.Z + 1) * 100.f, 2.f);
            if (!bFloorReady && SmokeElapsed - State.WaitStartedAt < 5.f)
            {
                Movement->SetMovementMode(MOVE_None);
                Player->SetActorLocation(State.PlayerPosition, false, nullptr, ETeleportType::TeleportPhysics);
                SmokeTime = .1f;
                return false;
            }
            Check(bFloorReady, TEXT("Extended stone platform has real collision"));
            if (!bFloorReady)
            {
                Movement->SetComponentTickEnabled(true);
                DayNightActor->SetDayLengthSeconds(State.OriginalDayLength);
                return true;
            }
        }
        Player->SetActorLocation(State.PlayerPosition, false, nullptr, ETeleportType::TeleportPhysics);
        Movement->SetComponentTickEnabled(true);
        Movement->SetMovementMode(MOVE_Walking);
        State.MobSpawnPosition = State.PlayerPosition + FVector(200.f, 0.f, 0.f);
        {
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ABlockRealmMob* Mob = GetWorld()->SpawnActor<ABlockRealmMob>(ABlockRealmMob::StaticClass(),
                State.MobSpawnPosition, FRotator::ZeroRotator, Parameters);
            Check(IsValid(Mob), TEXT("A fixed live mob spawns 200 cm ahead without a night spawn roll"));
            if (!Mob) return true;
            State.MobSpawnPosition.Z = (State.Root.Z + 1) * 100.f
                + Mob->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
            Mob->SetActorLocation(State.MobSpawnPosition, false, nullptr, ETeleportType::TeleportPhysics);
            Mob->SetWorld(WorldActor);
            Mob->SetTarget(Player);
            State.Mob = Mob;
            ActiveMobs.Add(Mob);
            Aim(Mob->GetActorLocation());
        }
        SmokeTime = .2f;
        break;
    case 2:
        Check(State.Mob.IsValid()
            && FVector::Dist2D(State.Mob->GetActorLocation(), State.MobSpawnPosition) > 10.f,
            TEXT("The fixture mob actively chases before overlay freeze checks"));
        Check(Player->CanUsePlayerActions(), TEXT("Released mouse input re-arms real player actions"));
        Key(EKeys::E);
        SmokeTime = .15f;
        break;
    case 3:
        {
            ABlockRealmHUD* HUD = Cast<ABlockRealmHUD>(PC->GetHUD());
            Check(Player->IsInventoryOpen() && PC->bShowMouseCursor && HUD
                && HUD->GetHitBoxWithName(TEXT("InventoryClose"))
                && HUD->GetHitBoxWithName(TEXT("CraftRecipe_0")),
                TEXT("Real E input opens the inventory overlay and recipe UI"));
        }
        SnapshotFrozen();
        DayNightActor->SetAutoAdvance(true);
        TryBlockedInput();
        SmokeTime = .55f;
        break;
    case 4:
        CheckFrozen(true);
        Release();
        State.WoodBefore = Player->GetInventoryCount(4);
        State.PlanksBefore = Player->GetCraftedPlankCount();
        Click(TEXT("CraftRecipe_0"));
        SmokeTime = .12f;
        break;
    case 5:
        Check(Player->GetInventoryCount(4) == State.WoodBefore - 1
            && Player->GetCraftedPlankCount() == State.PlanksBefore + 4,
            TEXT("Inventory recipe click consumes one wood and produces four planks"));
        Shot(TEXT("11_Inventory"));
        DayNightActor->SetAutoAdvance(false);
        Key(EKeys::E);
        SmokeTime = .15f;
        break;
    case 6:
        Check(!Player->IsInventoryOpen() && Player->CanUsePlayerActions(),
            TEXT("Closing E inventory restores the released-input action gate"));
        ResetMobPosition();
        State.ActiveClock = GetTimeOfDay();
        DayNightActor->SetAutoAdvance(true);
        SmokeTime = .18f;
        break;
    case 7:
        Check(GetTimeOfDay() > State.ActiveClock + .0001f,
            TEXT("Day-night positive control advances during active play"));
        DayNightActor->SetAutoAdvance(false);
        Key(EKeys::Escape);
        SmokeTime = .15f;
        break;
    case 8:
        Check(IsPausedMenu() && PC->bShowMouseCursor, TEXT("Real Esc input opens the pause overlay"));
        SnapshotFrozen();
        DayNightActor->SetAutoAdvance(true);
        TryBlockedInput();
        SmokeTime = .55f;
        break;
    case 9:
        CheckFrozen(false);
        Release();
        DayNightActor->SetAutoAdvance(false);
        Key(EKeys::Escape);
        SmokeTime = .15f;
        break;
    case 10:
        Check(IsAdventureActive() && Player->SelectToolSlot(1)
            && Player->GetSelectedToolType() == EBlockRealmToolType::StonePickaxe,
            TEXT("Combat resumes with the actual stone pickaxe selected"));
        if (!State.Mob.IsValid())
        {
            Check(false, TEXT("Combat fixture mob survives the inventory and pause checks"));
            return true;
        }
        ResetMobPosition();
        State.MobHealthBefore = State.Mob->GetHealth();
        State.DurabilityBefore = Player->GetSelectedToolDurability();
        Key(EKeys::MiddleMouseButton);
        SmokeTime = .06f;
        break;
    case 11:
        Check(State.Mob.IsValid()
            && FMath::IsNearlyEqual(State.Mob->GetHealth(), State.MobHealthBefore - 9.f, .01f)
            && Player->GetSelectedToolDurability() == State.DurabilityBefore - 1,
            TEXT("Real middle mouse hits for nine stone-pickaxe damage and wears one durability"));
        Check(Actions->GetAttackCooldownRemaining() > 0.f && Actions->GetHitFeedbackAlpha() > 0.f,
            TEXT("A real hit starts the attack cooldown and HUD hit feedback"));
        Key(EKeys::MiddleMouseButton);
        SmokeTime = .06f;
        break;
    case 12:
        Check(State.Mob.IsValid()
            && FMath::IsNearlyEqual(State.Mob->GetHealth(), State.MobHealthBefore - 9.f, .01f)
            && Player->GetSelectedToolDurability() == State.DurabilityBefore - 1,
            TEXT("Middle mouse repeated during cooldown deals no damage and spends no durability"));
        Shot(TEXT("12_Combat"));
        SmokeTime = .5f;
        break;
    case 13:
        ResetMobPosition();
        Key(EKeys::MiddleMouseButton);
        SmokeTime = .06f;
        break;
    case 14:
        Check(State.Mob.IsValid()
            && FMath::IsNearlyEqual(State.Mob->GetHealth(), State.MobHealthBefore - 18.f, .01f)
            && Player->GetSelectedToolDurability() == State.DurabilityBefore - 2,
            TEXT("A second attack after cooldown deals nine more damage and wears the tool once"));
        SmokeTime = .5f;
        break;
    case 15:
        ResetMobPosition();
        State.DeathPosition = State.Mob.IsValid() ? State.Mob->GetActorLocation() : State.MobSpawnPosition;
        State.StoneBefore = Player->GetInventoryCount(2);
        State.GlowBefore = Player->GetInventoryCount(8);
        Key(EKeys::MiddleMouseButton);
        SmokeTime = .05f;
        break;
    case 16:
        Check((!State.Mob.IsValid() || State.Mob->IsDead())
            && Player->GetSelectedToolDurability() == State.DurabilityBefore - 3,
            TEXT("The third real stone-pickaxe hit kills the mob and wears the tool once"));
        Check(DropCount(GetWorld(), EBlockType::Stone) == 1 && DropCount(GetWorld(), EBlockType::Glow) == 1,
            TEXT("Mob death creates one real Stone drop and one real Glow drop"));
        Player->SetActorLocation(FVector(State.DeathPosition.X, State.DeathPosition.Y,
            State.PlayerPosition.Z), false, nullptr, ETeleportType::TeleportPhysics);
        Movement->StopMovementImmediately();
        SmokeTime = .3f;
        break;
    case 17:
        Check(Player->GetInventoryCount(2) == State.StoneBefore + 1
            && Player->GetInventoryCount(8) == State.GlowBefore + 1
            && DropCount(GetWorld(), EBlockType::Stone) == 0 && DropCount(GetWorld(), EBlockType::Glow) == 0,
            TEXT("The real proximity pickup component adds Stone and Glow exactly once and removes their drops"));
        {
            FWorldSaveState FoodFixture = Player->CaptureSaveState();
            FoodFixture.Health = 60.f;
            FoodFixture.Hunger = 35.f;
            Player->RestoreSaveState(FoodFixture);
        }
        Release();
        SmokeTime = .15f;
        break;
    case 18:
        State.FoodBefore = Actions->GetFoodCount();
        State.HealthBefore = Player->GetHealth();
        State.HungerBefore = Player->GetHunger();
        Key(EKeys::C);
        SmokeTime = .06f;
        break;
    case 19:
        Check(Actions->GetFoodCount() == State.FoodBefore - 1
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore + 4.f, .01f)
            && FMath::IsNearlyEqual(Player->GetHunger(), State.HungerBefore + 28.f, .15f),
            TEXT("Real C input consumes one food and restores four health plus twenty-eight hunger"));
        Player->RestoreSurvivalHealth(Player->GetMaxHealth());
        Player->RestoreSurvivalHunger(Player->GetMaxHunger());
        State.FoodBefore = Actions->GetFoodCount();
        Key(EKeys::C);
        SmokeTime = .06f;
        break;
    case 20:
        Check(Actions->GetFoodCount() == State.FoodBefore
            && FMath::IsNearlyEqual(Player->GetHealth(), Player->GetMaxHealth(), .01f)
            && FMath::IsNearlyEqual(Player->GetHunger(), Player->GetMaxHunger(), .01f),
            TEXT("Real C input at full health and hunger spends no food"));
        State.TorchSupport = State.Root + FIntVector(3, 0, 1);
        State.TorchCell = State.Root + FIntVector(2, 0, 1);
        WorldActor->SetBlock(State.TorchSupport, EBlockType::Stone);
        {
            FWorldSaveState SaveFixture = Player->CaptureSaveState();
            SaveFixture.Position = State.PlayerPosition;
            SaveFixture.Health = 73.f;
            SaveFixture.Hunger = 54.f;
            Player->RestoreSaveState(SaveFixture);
        }
        DayNightActor->SetTimeOfDay(.02f);
        DayNightActor->SetAutoAdvance(false);
        NightMobSpawnAccumulator = 0.f;
        Aim(ABlockWorld::BlockToWorld(State.TorchSupport));
        SmokeTime = .8f;
        break;
    case 21:
        Check(IsNight() && GetActiveMobCount() == 0,
            TEXT("Night screenshot uses a fixed clock without natural mob spawning"));
        Shot(TEXT("13_Night"));
        Check(Player->SelectToolSlot(2) && Player->GetSelectedToolType() == EBlockRealmToolType::Torch,
            TEXT("Torch placement selects real tool slot two"));
        Aim(ABlockWorld::BlockToWorld(State.TorchSupport));
        Check(Player->HasTarget() && Player->GetTargetBlock() == State.TorchSupport
            && Player->GetPlacementBlock() == State.TorchCell,
            TEXT("The real camera trace selects the fixed torch support and air cell"));
        {
            FBlockRealmToolSlot TorchSlot;
            Player->GetToolSlot(2, TorchSlot);
            State.TorchQuantityBefore = TorchSlot.Quantity;
        }
        Input(EKeys::RightMouseButton, true);
        SmokeTime = .1f;
        break;
    case 22:
        {
            FBlockRealmToolSlot TorchSlot;
            Player->GetToolSlot(2, TorchSlot);
            const TArray<FIntVector> Torches = TorchPositions(GetWorld(), WorldActor);
            Check(Torches.Num() == 1 && Torches.Contains(State.TorchCell)
                && TorchSlot.Quantity == State.TorchQuantityBefore - 1,
                TEXT("Real right mouse places one torch and consumes exactly one torch item"));
        }
        SmokeTime = .6f;
        break;
    case 23:
        {
            FBlockRealmToolSlot TorchSlot;
            Player->GetToolSlot(2, TorchSlot);
            Check(TorchPositions(GetWorld(), WorldActor).Num() == 1
                && TorchSlot.Quantity == State.TorchQuantityBefore - 1
                && WorldActor->GetBlock(State.TorchCell) == EBlockType::Air,
                TEXT("Held right mouse on the same torch cell creates no duplicate and spends no extra item"));
        }
        Input(EKeys::RightMouseButton, false);
        State.LastTorchSupport = State.Root + FIntVector(3, 2, 1);
        State.LastTorchCell = State.Root + FIntVector(2, 2, 1);
        WorldActor->SetBlock(State.LastTorchSupport, EBlockType::Stone);
        {
            FWorldSaveState LastTorchFixture = Player->CaptureSaveState();
            LastTorchFixture.ToolSlots[2].Quantity = 1;
            LastTorchFixture.Position = State.PlayerPosition + FVector(0.f, 200.f, 0.f);
            Player->RestoreSaveState(LastTorchFixture);
            State.BlocksBeforeLastTorch = LastTorchFixture.Inventory;
        }
        Aim(ABlockWorld::BlockToWorld(State.LastTorchSupport));
        SmokeTime = .15f;
        break;
    case 24:
        Check(Player->SelectToolSlot(2), TEXT("The final one-item torch stack is selectable"));
        Aim(ABlockWorld::BlockToWorld(State.LastTorchSupport));
        Check(Player->HasTarget() && Player->GetTargetBlock() == State.LastTorchSupport
            && Player->GetPlacementBlock() == State.LastTorchCell,
            TEXT("The final-torch hold test aims at its own support and empty cell"));
        Input(EKeys::RightMouseButton, true);
        SmokeTime = .1f;
        break;
    case 25:
        {
            FBlockRealmToolSlot TorchSlot;
            Player->GetToolSlot(2, TorchSlot);
            Check(TorchPositions(GetWorld(), WorldActor).Num() == 2
                && TorchSlot.Quantity == 0 && Player->GetSelectedToolSlot() == INDEX_NONE,
                TEXT("Placing the last torch empties its slot and selects no tool"));
        }
        SmokeTime = .65f;
        break;
    case 26:
        Check(TorchPositions(GetWorld(), WorldActor).Contains(State.LastTorchCell)
            && TorchPositions(GetWorld(), WorldActor).Num() == 2
            && WorldActor->GetBlock(State.LastTorchCell) == EBlockType::Air
            && Player->CaptureSaveState().Inventory == State.BlocksBeforeLastTorch,
            TEXT("Holding right mouse after the last torch does not place an ordinary block or destroy the torch"));
        Input(EKeys::RightMouseButton, false);
        Shot(TEXT("14_Torch"));
        Key(EKeys::Escape);
        SmokeTime = .15f;
        break;
    case 27:
        Check(IsPausedMenu(), TEXT("The survival save snapshot is stable in the real pause menu"));
        State.ExpectedSave = Player->CaptureSaveState();
        State.ExpectedSave.TimeOfDay = GetTimeOfDay();
        State.ExpectedSave.PlacedTorches = TorchPositions(GetWorld(), WorldActor);
        State.ExpectedSave.PlacedTorchSupports = TorchSupports(GetWorld(), WorldActor);
        Key(EKeys::F5);
        SmokeTime = .12f;
        break;
    case 28:
        {
            FString Json;
            FVoxelSaveRecord Disk;
            const FString SavePath = WorldActor->GetSavePath();
            const bool bParsed = FFileHelper::LoadFileToString(Json, *SavePath) && FVoxelData::ParseRecord(Json, Disk);
            Check(GetNotice() == TEXT("世界已保存") && bParsed
                && SameSurvival(Disk.Player, State.ExpectedSave, .01f)
                && FMath::IsNearlyEqual(Disk.Player.TimeOfDay, State.ExpectedSave.TimeOfDay, .000001f)
                && SamePlacedTorches(Disk.Player, State.ExpectedSave),
                TEXT("Real F5 writes health, hunger, worn tools, food, planks, time and torch cells/supports into the world JSON"));
            TArray<FString> Files;
            IFileManager::Get().FindFiles(Files, *FPaths::Combine(FPaths::GetPath(SavePath), TEXT("*")), true, false);
            Check(Files.Num() == 1 && Files.Contains(FPaths::GetCleanFilename(SavePath)),
                TEXT("Adventure persistence uses exactly one world file with no survival sidecar or leftover temporary file"));
            Check(SaveAdventure(), TEXT("Public SaveAdventure successfully saves the same complete survival world"));
        }
        Click(TEXT("Title"));
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .15f;
        break;
    case 29:
        Check(bTitleOpen && PC->bShowMouseCursor && TorchPositions(GetWorld(), WorldActor).IsEmpty()
            && Player->GetCraftedPlankCount() == 0 && Actions->GetFoodCount() == 6,
            TEXT("Actual title-button input removes world torches and resets transient survival state"));
        DayNightActor->SetTimeOfDay(.42f);
        if (!WorldActor->IsInitialTerrainReady() && SmokeElapsed - State.WaitStartedAt < 5.f)
        {
            SmokeTime = .1f;
            return false;
        }
        Click(TEXT("Start"));
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .2f;
        break;
    case 30:
        if ((bPendingStart || !IsAdventureActive()) && SmokeElapsed - State.WaitStartedAt < 5.f)
        {
            SmokeTime = .1f;
            return false;
        }
        {
            const FWorldSaveState Restored = Player->CaptureSaveState();
            Check(IsAdventureActive() && FMath::IsNearlyEqual(Restored.Health, State.ExpectedSave.Health, .01f)
                && FMath::IsNearlyEqual(Restored.Hunger, State.ExpectedSave.Hunger, .15f),
                TEXT("Title to continue restores saved health and hunger"));
            Check(Restored.FoodCount == State.ExpectedSave.FoodCount
                && Restored.CraftedPlanks == State.ExpectedSave.CraftedPlanks
                && Restored.Inventory == State.ExpectedSave.Inventory && Restored.SelectedSlot == State.ExpectedSave.SelectedSlot,
                TEXT("Title to continue restores food, crafted planks and all block inventory slots"));
            Check(SameTools(Restored, State.ExpectedSave),
                TEXT("Title to continue restores every tool type, durability, quantity and selected tool slot"));
            Check(FMath::IsNearlyEqual(GetTimeOfDay(), State.ExpectedSave.TimeOfDay, .000001f)
                && TorchPositions(GetWorld(), WorldActor) == State.ExpectedSave.PlacedTorches
                && TorchSupports(GetWorld(), WorldActor) == State.ExpectedSave.PlacedTorchSupports,
                TEXT("Title to continue restores the saved night clock and every torch cell with its original wall support"));
            Check(FVector::Dist(Restored.Position, State.ExpectedSave.Position) < 5.f
                && Restored.Rotation.Equals(State.ExpectedSave.Rotation, .1f)
                && !Player->IsInventoryOpen() && !Player->IsFlying(),
                TEXT("Continue restores player position and view with normal movement and closed inventory"));
        }
        Aim(ABlockWorld::BlockToWorld(State.TorchCell));
        Input(EKeys::LeftMouseButton, true);
        SmokeTime = .65f;
        break;
    case 31:
        {
            FBlockRealmToolSlot RecoveredTorch;
            Player->GetToolSlot(2, RecoveredTorch);
            const TArray<FIntVector> Remaining = TorchPositions(GetWorld(), WorldActor);
            Check(Remaining.Num() == 1 && !Remaining.Contains(State.TorchCell)
                && Remaining.Contains(State.LastTorchCell)
                && RecoveredTorch.Type == EBlockRealmToolType::Torch && RecoveredTorch.Quantity == 1,
                TEXT("Actual left mouse on a restored torch returns one torch item and removes only the aimed torch"));
            Check(!Player->IsMining() && WorldActor->GetBlock(State.TorchSupport) == EBlockType::Stone
                && Player->CaptureSaveState().Inventory == State.ExpectedSave.Inventory,
                TEXT("Holding the torch-recovery click does not start mining its support or change block inventory"));
        }
        Input(EKeys::LeftMouseButton, false);
        // Prove that altitude alone cannot turn on the cave fill light. This
        // separate low surface has a clear sky column through WorldHeight.
        State.LowSurfaceRoot = State.Root + FIntVector(0, 5, 0);
        State.LowSurfaceRoot.Z = FVoxelData::SeaLevel + 2;
        for (int32 X = -1; X <= 1; ++X)
            for (int32 Y = -1; Y <= 1; ++Y)
            {
                WorldActor->SetBlock(State.LowSurfaceRoot + FIntVector(X, Y, 0), EBlockType::Stone);
                for (int32 Z = State.LowSurfaceRoot.Z + 1; Z < FVoxelData::WorldHeight; ++Z)
                    WorldActor->SetBlock(FIntVector(State.LowSurfaceRoot.X + X, State.LowSurfaceRoot.Y + Y, Z), EBlockType::Air);
            }
        {
            FWorldSaveState LowSurface = Player->CaptureSaveState();
            LowSurface.Position = ABlockWorld::BlockToWorld(State.LowSurfaceRoot);
            LowSurface.Position.Z += 50.f + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
            LowSurface.Rotation = FRotator::ZeroRotator;
            Player->RestoreSaveState(LowSurface);
        }
        DayNightActor->SetTimeOfDay(.32f);
        DayNightActor->SetAutoAdvance(false);
        NightMobSpawnAccumulator = 0.f;
        SmokeTime = .25f;
        break;
    case 32:
        {
            const FIntVector PlayerCell = ABlockWorld::WorldToBlock(Player->GetActorLocation());
            UPointLightComponent* CaveLight = Player->FindComponentByClass<UPointLightComponent>();
            bool bOpenSky = true;
            for (int32 Z = PlayerCell.Z + 1; Z < FVoxelData::WorldHeight; ++Z)
                bOpenSky &= WorldActor->GetBlock(FIntVector(PlayerCell.X, PlayerCell.Y, Z)) == EBlockType::Air;
            Check(PlayerCell.Z <= 20 && bOpenSky && CaveLight && !CaveLight->IsVisible(),
                TEXT("An open surface below block height twenty keeps CaveFillLight disabled"));
        }
        Release();
        DayNightActor->SetDayLengthSeconds(State.OriginalDayLength);
        Check(SmokeElapsed - State.StartedAt < ExtendedSmokeBudgetSeconds,
            TEXT("Extended smoke completes within its 40-second added budget"));
        UE_LOG(LogTemp, Display, TEXT("BR_EXTENDED elapsed=%.2f seconds stages=%d"),
            SmokeElapsed - State.StartedAt, State.Stage + 1);
        return true;
    default:
        return true;
    }
    ++State.Stage;
    return false;
}
