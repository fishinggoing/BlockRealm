#include "BlockRealmGameMode.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmCrop.h"
#include "BlockRealmDayNight.h"
#include "BlockRealmFurnace.h"
#include "BlockRealmHUD.h"
#include "BlockRealmMob.h"
#include "BlockRealmPlayerActions.h"
#include "BlockRealmRestPoint.h"
#include "BlockRealmSurvival.h"
#include "BlockRealmTorch.h"
#include "BlockWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
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

// Fixtures use public actor/save APIs; V, T, B, E, Esc, and menu clicks use
// the actual controller. No wall-clock simulation or direct age assignment.
struct FBlockRealmExpansionSmokeState
{
    int32 Stage = 0;
    float StartedAt = 0.f;
    float WaitStartedAt = 0.f;
    FIntVector Root = FIntVector::ZeroValue;
    FIntVector Soil = FIntVector::ZeroValue;
    FIntVector CropCell = FIntVector::ZeroValue;
    FIntVector FurnaceCell = FIntVector::ZeroValue;
    FIntVector BedCell = FIntVector::ZeroValue;
    FIntVector TorchCell = FIntVector::ZeroValue;
    FIntVector TorchWall = FIntVector::ZeroValue;
    FVector PlayerPosition = FVector::ZeroVector;
    FVector SafeRespawn = FVector::ZeroVector;
    TWeakObjectPtr<ABlockRealmCrop> Crop;
    TWeakObjectPtr<ABlockRealmFurnace> Furnace;
    TWeakObjectPtr<ABlockRealmRestPoint> Bed;
    TWeakObjectPtr<ABlockRealmMob> Threat;
    TWeakObjectPtr<AActor> TorchDrop;
    int32 FoodBefore = 0;
    int32 StoneBefore = 0;
    int32 SandBefore = 0;
    int32 WoodBefore = 0;
    int32 GlassBefore = 0;
    int32 PlanksBefore = 0;
    float HealthBefore = 0.f;
    float GrowthBefore = 0.f;
    float ProgressBefore = 0.f;
    float BreathBefore = 0.f;
    float BreathSampleAt = 0.f;
    FVector WetEyePosition = FVector::ZeroVector;
    FVector DryEyePosition = FVector::ZeroVector;
    bool bFurnaceBurningShotRequested = false;
    bool bRestPointShotRequested = false;
    bool bBreathShotRequested = false;
    TArray<int32> InventoryBefore;
    FWorldSaveState FrozenPlayer;
    FBlockRealmSavedFurnace FrozenFurnace;
    FWorldSaveState ExpectedSave;
    FWorldSaveState BreathRestore;
};

namespace BlockRealmExpansionSmoke
{
    constexpr float BudgetSeconds = 55.f;
    constexpr float ShortDurationSeconds = .4f;

    template <typename T>
    T* ActorAt(UWorld* World, const ABlockWorld* BlockWorld, FIntVector Cell)
    {
        for (TActorIterator<T> It(World); It; ++It)
        {
            if (IsValid(*It) && !It->IsActorBeingDestroyed()
                && It->GetBlockWorld() == BlockWorld && It->GetBlockPosition() == Cell) return *It;
        }
        return nullptr;
    }

    template <typename T>
    int32 ActorCount(UWorld* World, const ABlockWorld* BlockWorld)
    {
        int32 Count = 0;
        for (TActorIterator<T> It(World); It; ++It)
            if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetBlockWorld() == BlockWorld) ++Count;
        return Count;
    }

    TArray<AActor*> TorchDrops(UWorld* World)
    {
        TArray<AActor*> Drops;
        for (TActorIterator<AActor> It(World); It; ++It)
            if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->ActorHasTag(TEXT("BlockRealmTorchDrop")))
                Drops.Add(*It);
        return Drops;
    }

    TArray<FBlockRealmToolSlot> ReadTools(const ABlockRealmCharacter* Player)
    {
        TArray<FBlockRealmToolSlot> Tools;
        for (int32 Index = 0; Index < Player->GetToolSlotCount(); ++Index)
        {
            FBlockRealmToolSlot Slot;
            Player->GetToolSlot(Index, Slot);
            Tools.Add(Slot);
        }
        return Tools;
    }

    bool ToolsEqual(const TArray<FBlockRealmToolSlot>& A, const TArray<FBlockRealmToolSlot>& B)
    {
        if (A.Num() != B.Num()) return false;
        for (int32 Index = 0; Index < A.Num(); ++Index)
            if (A[Index].Type != B[Index].Type || A[Index].Quantity != B[Index].Quantity
                || A[Index].Durability != B[Index].Durability || A[Index].MaxDurability != B[Index].MaxDurability) return false;
        return true;
    }

    int32 TorchQuantity(const ABlockRealmCharacter* Player)
    {
        int32 Quantity = 0;
        for (const FBlockRealmToolSlot& Slot : ReadTools(Player))
            if (Slot.Type == EBlockRealmToolType::Torch) Quantity += Slot.Quantity;
        return Quantity;
    }

    FBlockRealmSavedFurnace ReadFurnace(const ABlockRealmFurnace* Furnace)
    {
        FBlockRealmSavedFurnace State;
        if (Furnace)
        {
            State.Cell = Furnace->GetCell();
            State.InputSand = Furnace->GetInputSand();
            State.FuelWood = Furnace->GetFuelWood();
            State.OutputGlass = Furnace->GetOutputGlass();
            State.CookProgress = Furnace->GetCookProgress();
        }
        return State;
    }

    bool FurnaceEqual(const FBlockRealmSavedFurnace& A, const FBlockRealmSavedFurnace& B, float AllowedAdvance)
    {
        if (A.Cell != B.Cell) return false;
        // The continued actor can resume real cooking before this harness tick.
        // Permit only the corresponding ingredients/output/progress transaction.
        const int32 Completed = A.OutputGlass - B.OutputGlass;
        if (Completed < 0 || Completed > 1 || A.InputSand != B.InputSand - Completed
            || A.FuelWood != B.FuelWood - Completed) return false;
        const float Advance = A.CookProgress - B.CookProgress
            + Completed * ABlockRealmFurnace::DefaultCookDurationSeconds;
        return Advance >= -.001f && Advance <= AllowedAdvance + .001f;
    }

    void CaptureActors(UWorld* World, const ABlockWorld* BlockWorld, FWorldSaveState& State)
    {
        State.Crops.Reset();
        State.RestPoints.Reset();
        State.Furnaces.Reset();
        for (TActorIterator<ABlockRealmCrop> It(World); It; ++It)
        {
            if (!IsValid(*It) || It->IsActorBeingDestroyed() || It->IsHarvested() || It->GetBlockWorld() != BlockWorld) continue;
            FBlockRealmSavedCrop Crop;
            Crop.Cell = It->GetBlockPosition();
            Crop.GrowthSeconds = It->GetGrowthSeconds();
            State.Crops.Add(Crop);
        }
        for (TActorIterator<ABlockRealmRestPoint> It(World); It; ++It)
        {
            if (!IsValid(*It) || It->IsActorBeingDestroyed() || It->GetBlockWorld() != BlockWorld) continue;
            FBlockRealmSavedRestPoint Bed;
            Bed.Cell = It->GetBlockPosition();
            Bed.Heading = It->GetHeading();
            State.RestPoints.Add(Bed);
        }
        for (TActorIterator<ABlockRealmFurnace> It(World); It; ++It)
        {
            if (IsValid(*It) && !It->IsActorBeingDestroyed() && It->GetBlockWorld() == BlockWorld)
                State.Furnaces.Add(ReadFurnace(*It));
        }
    }

    bool ExpansionEqual(const FWorldSaveState& Actual, const FWorldSaveState& Expected, float AllowedAdvance)
    {
        if (Actual.Crops.Num() != Expected.Crops.Num() || Actual.RestPoints.Num() != Expected.RestPoints.Num()
            || Actual.Furnaces.Num() != Expected.Furnaces.Num()
            || Actual.bHasRespawnPoint != Expected.bHasRespawnPoint
            || (Expected.bHasRespawnPoint && Actual.RespawnPoint != Expected.RespawnPoint)) return false;
        for (const FBlockRealmSavedCrop& Crop : Expected.Crops)
        {
            const FBlockRealmSavedCrop* Found = Actual.Crops.FindByPredicate(
                [&Crop](const FBlockRealmSavedCrop& Value) { return Value.Cell == Crop.Cell; });
            if (!Found || Found->GrowthSeconds < Crop.GrowthSeconds - .001f
                || Found->GrowthSeconds > Crop.GrowthSeconds + AllowedAdvance + .001f) return false;
        }
        for (const FBlockRealmSavedRestPoint& Bed : Expected.RestPoints)
        {
            const FBlockRealmSavedRestPoint* Found = Actual.RestPoints.FindByPredicate(
                [&Bed](const FBlockRealmSavedRestPoint& Value) { return Value.Cell == Bed.Cell; });
            if (!Found || Found->Heading != Bed.Heading) return false;
        }
        for (const FBlockRealmSavedFurnace& Furnace : Expected.Furnaces)
        {
            const FBlockRealmSavedFurnace* Found = Actual.Furnaces.FindByPredicate(
                [&Furnace](const FBlockRealmSavedFurnace& Value) { return Value.Cell == Furnace.Cell; });
            if (!Found || !FurnaceEqual(*Found, Furnace, AllowedAdvance)) return false;
        }
        return true;
    }
}

bool ABlockRealmGameMode::TickExpansionSmoke()
{
    using namespace BlockRealmExpansionSmoke;
    // GameMode samples in PrePhysics, before the Actions component's
    // DuringPhysics tick. Compare the last completed Actions boundary at both
    // ends, so a long submersion frame does not become a false timing failure.
    const float CompletedActionsTime = SmokeElapsed - GetWorld()->GetDeltaSeconds();
    const auto Check = [this](bool Pass, const TCHAR* Name)
    {
        (Pass ? SmokeChecks : SmokeFailures).Add(Name);
        UE_LOG(LogTemp, Display, TEXT("BR_CHECK %s %s"), Pass ? TEXT("PASS") : TEXT("FAIL"), Name);
    };
    ABlockRealmCharacter* Player = GetPlayerCharacter();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    UBlockRealmPlayerActionsComponent* Actions = Player ? Player->FindComponentByClass<UBlockRealmPlayerActionsComponent>() : nullptr;
    UCameraComponent* Camera = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!Player || !PC || !Actions || !Camera || !WorldActor || !DayNightActor)
    {
        Check(false, TEXT("Expansion smoke has the real player, actions, camera, controller, world and clock"));
        return true;
    }
    UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
    if (!ExpansionSmokeState.IsValid())
    {
        ExpansionSmokeState = MakeShared<FBlockRealmExpansionSmokeState>();
        ExpansionSmokeState->StartedAt = SmokeElapsed;
    }
    FBlockRealmExpansionSmokeState& State = *ExpansionSmokeState;
    const auto Input = [PC](const FKey& Key, bool Down)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,
            Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    };
    const auto Key = [&Input](const FKey& Pressed) { Input(Pressed, true); Input(Pressed, false); };
    const auto Release = [&Input]()
    {
        for (const FKey& Pressed : { EKeys::LeftMouseButton, EKeys::RightMouseButton,
            EKeys::MiddleMouseButton, EKeys::W, EKeys::SpaceBar, EKeys::V, EKeys::T, EKeys::B }) Input(Pressed, false);
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
    const auto Click = [PC, &Check, &Key](FName Name)
    {
        int32 Width = 0, Height = 0;
        PC->GetViewportSize(Width, Height);
        ABlockRealmHUD* HUD = Cast<ABlockRealmHUD>(PC->GetHUD());
        const FVector2D Center = ABlockRealmHUD::GetMenuButtonBounds(Name, FVector2D(Width, Height),
            Name == TEXT("Title")).GetCenter();
        const FHUDHitBox* Hit = HUD ? HUD->GetHitBoxAtCoordinates(Center, true) : nullptr;
        const bool bFound = Width > 0 && Height > 0 && Hit && Hit->GetName() == Name;
        Check(bFound, *FString::Printf(TEXT("Expansion HUD hitbox %s"), *Name.ToString()));
        if (!bFound) return;
        PC->SetMouseLocation(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
        Key(EKeys::LeftMouseButton);
    };
    const auto Aim = [PC, Camera, Player](FVector Target)
    {
        PC->SetControlRotation((Target - Camera->GetComponentLocation()).Rotation());
        Player->RefreshTarget();
    };
    const auto AimTop = [&Aim](FIntVector Support, FVector Offset = FVector::ZeroVector)
    {
        Aim(ABlockWorld::BlockToWorld(Support) + FVector(0.f, 0.f, 49.f) + Offset);
    };
    const auto MoveTo = [this, Player, Movement, &State](int32 X, int32 Y)
    {
        FVector Position = ABlockWorld::BlockToWorld(State.Root + FIntVector(X, Y, 0));
        Position.Z += 50.f + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
        Player->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        Movement->StopMovementImmediately();
        WorldActor->RequestStreaming(Position);
    };
    const auto MoveEyesTo = [this, Player, Camera, Movement](const FVector& EyePosition)
    {
        const FVector EyeOffset = Camera->GetComponentLocation() - Player->GetActorLocation();
        const FVector Position = EyePosition - EyeOffset;
        Player->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        Movement->StopMovementImmediately();
        WorldActor->RequestStreaming(Position);
    };
    const auto AimCrop = [&Aim, &State, Camera, this](float Height)
    {
        if (ABlockRealmCrop* Crop = State.Crop.Get())
        {
            const FVector Target = Crop->GetActorLocation() + FVector(0.f, 0.f, Height);
            Aim(Target);
            return ABlockRealmCrop::FindAimedCrop(GetWorld(), WorldActor, Camera->GetComponentLocation(),
                (Target - Camera->GetComponentLocation()).GetSafeNormal(), 700.f) == Crop;
        }
        return false;
    };
    const auto AimFurnace = [&Aim, &State, Camera, this]()
    {
        if (ABlockRealmFurnace* Furnace = State.Furnace.Get())
        {
            const FVector Target = Furnace->GetActorLocation() + FVector(-37.f, 0.f, -6.f);
            Aim(Target);
            return ABlockRealmFurnace::FindAimedFurnace(GetWorld(), WorldActor, Camera->GetComponentLocation(),
                (Target - Camera->GetComponentLocation()).GetSafeNormal(), 700.f) == Furnace;
        }
        return false;
    };
    const auto AimBed = [&Aim, &State, Camera, this]()
    {
        if (ABlockRealmRestPoint* Bed = State.Bed.Get())
        {
            const FVector Target = Bed->GetActorLocation() + FVector(0.f, 0.f, -14.f);
            Aim(Target);
            return ABlockRealmRestPoint::FindAimedRestPoint(GetWorld(), WorldActor, Camera->GetComponentLocation(),
                (Target - Camera->GetComponentLocation()).GetSafeNormal(), 700.f) == Bed;
        }
        return false;
    };
    const auto SnapshotFrozen = [&State, Player]()
    {
        State.FrozenPlayer = Player->CaptureSaveState();
        State.GrowthBefore = State.Crop.IsValid() ? State.Crop->GetGrowthSeconds() : -1.f;
        State.FrozenFurnace = ReadFurnace(State.Furnace.Get());
        State.ProgressBefore = State.FrozenFurnace.CookProgress;
    };
    const auto CheckFrozen = [&State, Player, &Check](bool bInventory)
    {
        const FWorldSaveState Current = Player->CaptureSaveState();
        Check(State.Crop.IsValid() && State.Furnace.IsValid()
            && FMath::IsNearlyEqual(State.Crop->GetGrowthSeconds(), State.GrowthBefore, .0001f)
            && FurnaceEqual(ReadFurnace(State.Furnace.Get()), State.FrozenFurnace, .0001f)
            && !State.Furnace->IsBurning(),
            bInventory ? TEXT("E freezes real crop age and furnace inputs, outputs, progress and fire")
                : TEXT("Esc freezes real crop age and furnace inputs, outputs, progress and fire"));
        Check(Current.FoodCount == State.FrozenPlayer.FoodCount
            && Current.CraftedPlanks == State.FrozenPlayer.CraftedPlanks
            && Current.Inventory == State.FrozenPlayer.Inventory,
            bInventory ? TEXT("V, T and B during E consume no food, materials or planks")
                : TEXT("V, T and B during Esc consume no food, materials or planks"));
    };

    if (SmokeElapsed - State.StartedAt >= BudgetSeconds)
    {
        Check(false, TEXT("Expansion smoke completes within its 55-second added budget"));
        Release();
        Movement->SetComponentTickEnabled(true);
        DayNightActor->SetAutoAdvance(false);
        if (ABlockRealmMob* Mob = State.Threat.Get()) Mob->Destroy();
        return true;
    }

    switch (State.Stage)
    {
    case 0:
        Check(IsAdventureActive() && !Player->IsInventoryOpen()
            && WorldActor->GetSavePath().Contains(TEXT("ValidationWorld")),
            TEXT("Expansion tests start in the isolated active smoke world"));
        if (!IsAdventureActive() || Player->IsInventoryOpen()
            || !WorldActor->GetSavePath().Contains(TEXT("ValidationWorld"))) return true;
        Release();
        DayNightActor->SetAutoAdvance(false);
        DayNightActor->SetTimeOfDay(.32f);
        NightMobSpawnAccumulator = 0.f;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
        {
            if (Cast<ABlockRealmMob>(*It) || It->ActorHasTag(TEXT("BlockRealmDrop"))
                || It->ActorHasTag(TEXT("BlockRealmTorch")) || It->ActorHasTag(TEXT("BlockRealmCrop"))
                || It->ActorHasTag(TEXT("BlockRealmRestPoint")) || It->ActorHasTag(TEXT("BlockRealmFurnace"))) It->Destroy();
        }
        ActiveMobs.Empty();
        State.Root = ABlockWorld::WorldToBlock(WorldActor->GetSpawnLocation()) + FIntVector(0, 12, 0);
        State.Root.Z = 48;
        State.Soil = State.Root + FIntVector(3, 0, 0);
        State.CropCell = State.Soil + FIntVector(0, 0, 1);
        State.FurnaceCell = State.Root + FIntVector(3, 3, 1);
        State.BedCell = State.Root + FIntVector(8, 0, 1);
        for (int32 X = -3; X <= 14; ++X)
            for (int32 Y = -3; Y <= 5; ++Y)
            {
                WorldActor->SetBlock(State.Root + FIntVector(X, Y, 0), EBlockType::Stone);
                for (int32 Z = 1; Z <= 5; ++Z)
                    WorldActor->SetBlock(State.Root + FIntVector(X, Y, Z), EBlockType::Air);
            }
        MoveTo(0, 0);
        State.PlayerPosition = Player->GetActorLocation();
        {
            FWorldSaveState Fixture = Player->CaptureSaveState();
            Fixture.Position = State.PlayerPosition;
            Fixture.Rotation = FRotator::ZeroRotator;
            Fixture.Health = Fixture.Hunger = 100.f;
            Fixture.FoodCount = 16;
            Fixture.CraftedPlanks = 32;
            Fixture.SelectedToolSlot = INDEX_NONE;
            Fixture.SelectedSlot = 0;
            Fixture.Inventory.Init(64, 9);
            Fixture.Inventory[7] = 0;
            Fixture.bHasRespawnPoint = false;
            Fixture.RespawnPoint = FIntVector::ZeroValue;
            Player->RestoreSaveState(Fixture);
        }
        Movement->SetComponentTickEnabled(false);
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .4f;
        break;
    case 1:
        {
            FHitResult Floor;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmExpansionFloor), false, Player);
            const bool bFloor = GetWorld()->LineTraceSingleByChannel(Floor, State.PlayerPosition,
                State.PlayerPosition - FVector(0.f, 0.f, 250.f), ECC_Visibility, Query)
                && FMath::IsNearlyEqual(Floor.ImpactPoint.Z, (State.Root.Z + 1) * 100.f, 2.f);
            if (!bFloor && SmokeElapsed - State.WaitStartedAt < 5.f) { SmokeTime = .1f; return false; }
            Check(bFloor, TEXT("Expansion fixture has actual stone-floor collision"));
            Movement->SetComponentTickEnabled(true);
            if (!bFloor) return true;
        }
        Movement->SetMovementMode(MOVE_Walking);
        SmokeTime = .15f;
        break;
    case 2:
        State.FoodBefore = Actions->GetFoodCount();
        AimTop(State.Soil);
        Check(Player->HasTarget() && Player->GetTargetBlock() == State.Soil,
            TEXT("The farming camera aims at the fixed top-face fixture"));
        Key(EKeys::V);
        SmokeTime = .1f;
        break;
    case 3:
        Check(Actions->GetFoodCount() == State.FoodBefore && ActorCount<ABlockRealmCrop>(GetWorld(), WorldActor) == 0,
            TEXT("Actual V on stone rejects planting without consuming food"));
        WorldActor->SetBlock(State.Soil, EBlockType::Grass);
        AimTop(State.Soil);
        Key(EKeys::V);
        SmokeTime = .1f;
        break;
    case 4:
        State.Crop = ActorAt<ABlockRealmCrop>(GetWorld(), WorldActor, State.CropCell);
        Check(State.Crop.IsValid() && !State.Crop->IsMature() && State.Crop->GetGrowthSeconds() < 1.f
            && Actions->GetFoodCount() == State.FoodBefore - 1,
            TEXT("Actual V plants a fresh zero-age crop and consumes exactly one food"));
        if (!State.Crop.IsValid()) return true;
        State.FoodBefore = Actions->GetFoodCount();
        Check(AimCrop(7.f), TEXT("The real camera can aim at the visible young crop stem"));
        Key(EKeys::V);
        SmokeTime = .1f;
        break;
    case 5:
        Check(State.Crop.IsValid() && !State.Crop->IsMature()
            && Actions->GetFoodCount() == State.FoodBefore && ActorCount<ABlockRealmCrop>(GetWorld(), WorldActor) == 1,
            TEXT("Actual V on an immature crop neither harvests nor consumes another seed"));
        AimTop(State.Soil, FVector(32.f, 32.f, 0.f));
        Check(ABlockRealmCrop::FindAimedCrop(GetWorld(), WorldActor, Camera->GetComponentLocation(),
            PC->GetControlRotation().Vector(), 700.f) == nullptr && Player->GetTargetBlock() == State.Soil,
            TEXT("The duplicate-seed test aims past the narrow plant at the same soil cell"));
        Key(EKeys::V);
        SmokeTime = .1f;
        break;
    case 6:
        Check(ActorCount<ABlockRealmCrop>(GetWorld(), WorldActor) == 1 && Actions->GetFoodCount() == State.FoodBefore,
            TEXT("Actual V cannot plant a duplicate crop or consume food in an occupied crop cell"));
        MoveTo(0, 3);
        AimTop(State.FurnaceCell - FIntVector(0, 0, 1));
        State.StoneBefore = Player->GetInventoryCount(2);
        Key(EKeys::T);
        SmokeTime = .1f;
        break;
    case 7:
        State.Furnace = ActorAt<ABlockRealmFurnace>(GetWorld(), WorldActor, State.FurnaceCell);
        Check(State.Furnace.IsValid() && Player->GetInventoryCount(2) == State.StoneBefore - 8
            && ActorCount<ABlockRealmFurnace>(GetWorld(), WorldActor) == 1,
            TEXT("Actual T places one furnace and consumes exactly eight Stone"));
        if (!State.Furnace.IsValid()) return true;
        Check(AimFurnace(), TEXT("The actual camera targets a rendered furnace back panel"));
        State.SandBefore = Player->GetInventoryCount(3);
        State.WoodBefore = Player->GetInventoryCount(4);
        Key(EKeys::T);
        SmokeTime = .08f;
        break;
    case 8:
        if (!State.Crop.IsValid() || !State.Furnace.IsValid())
        {
            Check(false, TEXT("Crop and furnace survive until the growth/cook freeze test"));
            return true;
        }
        Check(Player->GetInventoryCount(3) == State.SandBefore - 1 && Player->GetInventoryCount(4) == State.WoodBefore - 1
            && State.Furnace->GetInputSand() == 1 && State.Furnace->GetFuelWood() == 1
            && State.Furnace->GetOutputGlass() == 0 && State.Furnace->GetCookProgress() > 0.f,
            TEXT("Actual T feeds one Sand and one Wood atomically and real furnace cooking begins"));
        Check(State.Crop->SetGrowthDurationSeconds(ShortDurationSeconds)
            && State.Furnace->SetCookDurationSeconds(ShortDurationSeconds)
            && !State.Crop->IsMature() && State.Furnace->GetOutputGlass() == 0,
            TEXT("Short test durations preserve existing fractions rather than instantly grow or cook"));
        Key(EKeys::E);
        SmokeTime = .1f;
        break;
    case 9:
        Check(Player->IsInventoryOpen(), TEXT("Actual E opens the expansion inventory freeze test"));
        SnapshotFrozen();
        Key(EKeys::V); Key(EKeys::T); Key(EKeys::B);
        SmokeTime = .5f;
        break;
    case 10:
        CheckFrozen(true);
        Key(EKeys::E);
        SmokeTime = .08f;
        break;
    case 11:
        Check(!Player->IsInventoryOpen() && State.Crop.IsValid() && State.Furnace.IsValid()
            && State.Crop->GetGrowthSeconds() > State.GrowthBefore
            && State.Furnace->GetCookProgress() > State.ProgressBefore,
            TEXT("Crop and furnace positive controls resume actual Delta progress after closing E"));
        Key(EKeys::Escape);
        SmokeTime = .08f;
        break;
    case 12:
        Check(IsPausedMenu(), TEXT("Actual Esc opens the expansion pause freeze test"));
        SnapshotFrozen();
        Key(EKeys::V); Key(EKeys::T); Key(EKeys::B);
        SmokeTime = .5f;
        break;
    case 13:
        CheckFrozen(false);
        Key(EKeys::Escape);
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .08f;
        break;
    case 14:
        if (State.Crop.IsValid() && State.Furnace.IsValid()
            && (!State.Crop->IsMature() || State.Furnace->GetOutputGlass() == 0)
            && SmokeElapsed - State.WaitStartedAt < 2.f) { SmokeTime = .05f; return false; }
        Check(State.Crop.IsValid() && State.Crop->IsMature()
            && FMath::IsNearlyEqual(State.Crop->GetGrowthSeconds(), ShortDurationSeconds, .001f),
            TEXT("A zero-age planted crop matures only after real short-duration Tick growth"));
        Check(State.Furnace.IsValid() && State.Furnace->GetInputSand() == 0 && State.Furnace->GetFuelWood() == 0
            && State.Furnace->GetOutputGlass() == 1 && FMath::IsNearlyZero(State.Furnace->GetCookProgress(), .001f),
            TEXT("Actual short cooking consumes both ingredients once and produces exactly one Glass"));
        if (!State.Furnace.IsValid() || !State.Crop.IsValid()) return true;
        Check(AimFurnace(), TEXT("The actual camera still aims at the completed furnace"));
        Shot(TEXT("16_Furnace"));
        State.GlassBefore = Player->GetInventoryCount(6);
        State.SandBefore = Player->GetInventoryCount(3);
        State.WoodBefore = Player->GetInventoryCount(4);
        Key(EKeys::T);
        SmokeTime = .1f;
        break;
    case 15:
        Check(State.Furnace.IsValid() && State.Furnace->GetOutputGlass() == 0
            && Player->GetInventoryCount(6) == State.GlassBefore + 1
            && Player->GetInventoryCount(3) == State.SandBefore && Player->GetInventoryCount(4) == State.WoodBefore,
            TEXT("Actual T claims all cooked Glass once without also consuming another recipe"));
        if (!State.Furnace.IsValid()) return true;
        Check(State.Furnace->SetCookDurationSeconds(ABlockRealmFurnace::DefaultCookDurationSeconds),
            TEXT("Persistence uses the furnace's ordinary eight-second cook duration"));
        Key(EKeys::T);
        SmokeTime = .1f;
        break;
    case 16:
        if (!State.bFurnaceBurningShotRequested)
        {
            Check(Player->GetInventoryCount(6) == State.GlassBefore + 1 && State.Furnace.IsValid()
                && State.Furnace->GetOutputGlass() == 0 && State.Furnace->GetInputSand() == 1
                && State.Furnace->GetFuelWood() == 1
                && Player->GetInventoryCount(3) == State.SandBefore - 1 && Player->GetInventoryCount(4) == State.WoodBefore - 1,
                TEXT("A second actual T gives no duplicate Glass and starts the next batch only when output is empty"));
            if (!State.Furnace.IsValid()) return true;
            // Keep the back-panel interaction test above; show the real second
            // batch through the +X opening from a separate three-quarter view.
            MoveTo(6, 1);
            Aim(State.Furnace->GetActorLocation() + FVector(30.f, 0.f, -10.f));
            Check(State.Furnace->IsBurning() && State.Furnace->GetCookProgress() > 0.f
                && State.Furnace->GetCookProgress() < State.Furnace->GetCookDurationSeconds()
                && ABlockRealmFurnace::FindAimedFurnace(GetWorld(), WorldActor,
                    Camera->GetComponentLocation(), PC->GetControlRotation().Vector(), 600.f) == State.Furnace.Get(),
                TEXT("The additional furnace screenshot aims through the visible +X opening during real second-batch cooking"));
            Shot(TEXT("20_FurnaceBurning"));
            State.bFurnaceBurningShotRequested = true;
            // Screenshot requests are fulfilled at frame end. Do not move the
            // camera or request the crop screenshot before that frame renders.
            SmokeTime = .15f;
            return false;
        }
        MoveTo(0, 0);
        Check(AimCrop(35.f), TEXT("The real camera can aim at the mature crop stem"));
        Shot(TEXT("15_Farming"));
        State.FoodBefore = Actions->GetFoodCount();
        Key(EKeys::V);
        SmokeTime = .08f;
        break;
    case 17:
        Check(Actions->GetFoodCount() == State.FoodBefore + ABlockRealmCrop::HarvestFoodCount
            && ActorCount<ABlockRealmCrop>(GetWorld(), WorldActor) == 0,
            TEXT("Actual V harvests the mature crop for exactly three food and removes it"));
        Key(EKeys::V);
        SmokeTime = .08f;
        break;
    case 18:
        Check(Actions->GetFoodCount() == State.FoodBefore + ABlockRealmCrop::HarvestFoodCount
            && ActorCount<ABlockRealmCrop>(GetWorld(), WorldActor) == 0,
            TEXT("Repeated actual V at the harvested crop view grants no second harvest"));
        MoveTo(8, -3);
        AimTop(State.BedCell - FIntVector(0, 0, 1));
        State.PlanksBefore = Player->GetCraftedPlankCount();
        Key(EKeys::B);
        SmokeTime = .1f;
        break;
    case 19:
        State.Bed = ActorAt<ABlockRealmRestPoint>(GetWorld(), WorldActor, State.BedCell);
        Check(State.Bed.IsValid() && State.Bed->GetHeading() == 1
            && State.Bed->GetSecondBlockPosition() == State.BedCell + FIntVector(0, 1, 0)
            && Player->GetCraftedPlankCount() == State.PlanksBefore - 12,
            TEXT("Actual B places a two-cell +Y bed and consumes exactly twelve crafted planks"));
        if (!State.Bed.IsValid()) return true;
        {
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ABlockRealmMob* Mob = GetWorld()->SpawnActor<ABlockRealmMob>(ABlockRealmMob::StaticClass(),
                Player->GetActorLocation() + FVector(300.f, 0.f, 0.f), FRotator::ZeroRotator, Parameters);
            Check(IsValid(Mob), TEXT("Rest threat fixture creates an actual living mob within eight meters"));
            if (Mob)
            {
                Mob->SetWorld(WorldActor);
                Mob->SetTarget(Player);
                Mob->SetActorTickEnabled(false);
                State.Threat = Mob;
                ActiveMobs.Add(Mob);
            }
        }
        Player->ApplySurvivalDamage(12.f, TEXT("Expansion rest fixture"));
        State.HealthBefore = Player->GetHealth();
        DayNightActor->SetTimeOfDay(.02f);
        NightMobSpawnAccumulator = 0.f;
        Check(AimBed(), TEXT("The real camera targets the placed bed mattress"));
        Key(EKeys::B);
        SmokeTime = .1f;
        break;
    case 20:
        Check(State.Threat.IsValid() && !HasRespawnPoint()
            && FMath::IsNearlyEqual(GetTimeOfDay(), .02f, .000001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .01f),
            TEXT("Actual B with a nearby living mob neither selects a respawn point nor sleeps or heals"));
        if (ABlockRealmMob* Mob = State.Threat.Get()) Mob->Destroy();
        SmokeTime = .1f;
        break;
    case 21:
        State.HealthBefore = Player->GetHealth();
        Check(AimBed(), TEXT("The actual bed remains aimable after the threat is removed"));
        Key(EKeys::B);
        SmokeTime = .1f;
        break;
    case 22:
        if (!State.bRestPointShotRequested)
        {
            Check(HasRespawnPoint() && GetRespawnPoint() == State.BedCell
                && FMath::IsNearlyEqual(GetTimeOfDay(), .32f, .000001f) && Player->GetHealth() > State.HealthBefore
                && Player->GetCraftedPlankCount() == State.PlanksBefore - 12,
                TEXT("Actual B selects the bed, skips night to morning and heals without charging planks again"));
            const bool bSafe = State.Bed.IsValid() && State.Bed->FindSafeRespawn(State.SafeRespawn);
            const FIntVector Feet = ABlockWorld::WorldToBlock(State.SafeRespawn);
            Check(bSafe && !State.Bed->OccupiesCell(Feet) && WorldActor->GetBlock(Feet) == EBlockType::Air
                && WorldActor->GetBlock(Feet + FIntVector(0, 0, 1)) == EBlockType::Air
                && FVoxelData::IsSolid(WorldActor->GetBlock(Feet - FIntVector(0, 0, 1))),
                TEXT("The selected two-cell bed offers a legal supported adjacent respawn column"));
            MoveTo(10, -2);
            if (State.Bed.IsValid())
                Aim(State.Bed->GetActorLocation() + FVector(0.f, 50.f, -10.f));
            Check(State.Bed.IsValid()
                && ABlockRealmRestPoint::FindAimedRestPoint(GetWorld(), WorldActor,
                    Camera->GetComponentLocation(), PC->GetControlRotation().Vector(), 600.f) == State.Bed.Get()
                && GetNotice() == TEXT("已睡至清晨，恢复少量生命；营地重生点已设置。"),
                TEXT("The rest screenshot targets the selected visible bed while the real morning rest notice remains active"));
            Shot(TEXT("17_RestPoint"));
            State.bRestPointShotRequested = true;
            // Preserve the complete bed view and successful rest notice until
            // the screenshot has rendered, then test lethal respawn next tick.
            SmokeTime = .15f;
            return false;
        }
        UGameplayStatics::ApplyDamage(Player, 1000.f, PC, this, UDamageType::StaticClass());
        SmokeTime = .15f;
        break;
    case 23:
        Check(FVector::Dist(Player->GetActorLocation(), State.SafeRespawn) < 15.f
            && FVector::Dist(Player->GetActorLocation(), WorldActor->GetSpawnLocation()) > 500.f
            && FMath::IsNearlyEqual(Player->GetHealth(), Player->GetMaxHealth(), .01f)
            && HasRespawnPoint() && GetRespawnPoint() == State.BedCell,
            TEXT("Real lethal damage respawns beside the saved bed safely and preserves the selected respawn anchor"));
        State.TorchCell = State.Root + FIntVector(10, -2, 1);
        State.TorchWall = State.Root + FIntVector(11, -2, 1);
        MoveTo(10, -2);
        {
            FWorldSaveState FullTools = Player->CaptureSaveState();
            FullTools.ToolSlots.SetNum(FBlockRealmSurvivalData::ToolSlotCount);
            for (FBlockRealmSavedTool& Slot : FullTools.ToolSlots)
            {
                Slot.Type = static_cast<int32>(EBlockRealmToolType::Torch);
                Slot.Durability = Slot.MaxDurability = 0;
                Slot.Quantity = FBlockRealmSurvivalData::MaxToolStack;
            }
            FullTools.SelectedToolSlot = INDEX_NONE;
            Player->RestoreSaveState(FullTools);
            TArray<FBlockRealmToolSlot> Atomic = ReadTools(Player);
            Atomic.Last().Quantity = FBlockRealmSurvivalData::MaxToolStack - 1;
            const TArray<FBlockRealmToolSlot> Before = Atomic;
            Check(!FBlockRealmSurvivalData::TryAddTorches(64, Atomic) && ToolsEqual(Atomic, Before),
                TEXT("A large torch batch with only one free capacity fails without a partial inventory change"));
        }
        WorldActor->SetBlock(State.TorchWall, EBlockType::Stone);
        Check(ABlockRealmTorch::TryPlace(GetWorld(), WorldActor, State.TorchCell, State.TorchWall) != nullptr,
            TEXT("Torch recovery fixture creates a wall torch above a retained floor"));
        WorldActor->SetBlock(State.TorchWall, EBlockType::Air);
        SmokeTime = .7f;
        break;
    case 24:
        {
            const TArray<AActor*> Drops = TorchDrops(GetWorld());
            State.TorchDrop = Drops.Num() == 1 ? Drops[0] : nullptr;
            Check(Drops.Num() == 1 && ActorCount<ABlockRealmTorch>(GetWorld(), WorldActor) == 0
                && TorchQuantity(Player) == FBlockRealmSurvivalData::ToolSlotCount * FBlockRealmSurvivalData::MaxToolStack,
                TEXT("A real support-loss TorchDrop remains tagged and available when every tool slot is full"));
            FWorldSaveState Space = Player->CaptureSaveState();
            Space.ToolSlots.Last().Quantity = FBlockRealmSurvivalData::MaxToolStack - 2;
            Player->RestoreSaveState(Space);
        }
        SmokeTime = .3f;
        break;
    case 25:
        Check(TorchDrops(GetWorld()).IsEmpty()
            && (!State.TorchDrop.IsValid() || !State.TorchDrop->ActorHasTag(TEXT("BlockRealmTorchDrop")))
            && TorchQuantity(Player) == FBlockRealmSurvivalData::ToolSlotCount * FBlockRealmSurvivalData::MaxToolStack - 1,
            TEXT("Freeing tool space lets the real pickup scanner add exactly one torch and remove its drop marker"));
        SmokeTime = .3f;
        break;
    case 26:
        Check(TorchQuantity(Player) == FBlockRealmSurvivalData::ToolSlotCount * FBlockRealmSurvivalData::MaxToolStack - 1
            && TorchDrops(GetWorld()).IsEmpty(),
            TEXT("Later pickup scans cannot credit the recovered TorchDrop a second time despite spare capacity"));
        MoveTo(0, 0);
        State.FoodBefore = Actions->GetFoodCount();
        AimTop(State.Soil);
        Key(EKeys::V);
        SmokeTime = .1f;
        break;
    case 27:
        State.Crop = ActorAt<ABlockRealmCrop>(GetWorld(), WorldActor, State.CropCell);
        Check(State.Crop.IsValid() && !State.Crop->IsMature() && State.Crop->GetGrowthSeconds() > 0.f
            && FMath::IsNearlyEqual(State.Crop->GetGrowthDurationSeconds(), ABlockRealmCrop::DefaultGrowthDurationSeconds, .001f)
            && Actions->GetFoodCount() == State.FoodBefore - 1,
            TEXT("Persistence fixture uses a second actually planted crop with real age and ordinary growth duration"));
        Key(EKeys::Escape);
        SmokeTime = .12f;
        break;
    case 28:
        Check(IsPausedMenu(), TEXT("Expansion save snapshots crop and cooking age under the actual pause overlay"));
        State.ExpectedSave = Player->CaptureSaveState();
        State.ExpectedSave.bHasRespawnPoint = HasRespawnPoint();
        State.ExpectedSave.RespawnPoint = HasRespawnPoint() ? GetRespawnPoint() : FIntVector::ZeroValue;
        CaptureActors(GetWorld(), WorldActor, State.ExpectedSave);
        Check(State.ExpectedSave.Crops.Num() == 1 && State.ExpectedSave.RestPoints.Num() == 1
            && State.ExpectedSave.Furnaces.Num() == 1 && State.ExpectedSave.bHasRespawnPoint
            && State.ExpectedSave.RespawnPoint == State.BedCell,
            TEXT("The expansion snapshot contains a live crop, furnace, oriented bed and selected respawn anchor"));
        Key(EKeys::F5);
        SmokeTime = .12f;
        break;
    case 29:
        {
            FString Json;
            FVoxelSaveRecord Disk;
            const FString SavePath = WorldActor->GetSavePath();
            const bool bParsed = FFileHelper::LoadFileToString(Json, *SavePath) && FVoxelData::ParseRecord(Json, Disk);
            Check(GetNotice() == TEXT("世界已保存") && bParsed && ExpansionEqual(Disk.Player, State.ExpectedSave, .0001f),
                TEXT("Actual F5 persists crop age, furnace inventory/progress, bed heading and respawn selection in world JSON"));
            TArray<FString> Files;
            IFileManager::Get().FindFiles(Files, *FPaths::Combine(FPaths::GetPath(SavePath), TEXT("*")), true, false);
            Check(Files.Num() == 1 && Files.Contains(FPaths::GetCleanFilename(SavePath)),
                TEXT("Expansion progress still uses one world file with no crop, furnace or respawn sidecar"));
        }
        Click(TEXT("Title"));
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .15f;
        break;
    case 30:
        Check(bTitleOpen && ActorCount<ABlockRealmCrop>(GetWorld(), WorldActor) == 0
            && ActorCount<ABlockRealmFurnace>(GetWorld(), WorldActor) == 0
            && ActorCount<ABlockRealmRestPoint>(GetWorld(), WorldActor) == 0,
            TEXT("Actual title-button input clears all three expansion actor types"));
        if (!WorldActor->IsInitialTerrainReady() && SmokeElapsed - State.WaitStartedAt < 5.f)
        {
            SmokeTime = .1f;
            return false;
        }
        Click(TEXT("Start"));
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .2f;
        break;
    case 31:
        if ((bPendingStart || !IsAdventureActive()) && SmokeElapsed - State.WaitStartedAt < 5.f)
        {
            SmokeTime = .1f;
            return false;
        }
        {
            FWorldSaveState Restored = Player->CaptureSaveState();
            Restored.bHasRespawnPoint = HasRespawnPoint();
            Restored.RespawnPoint = HasRespawnPoint() ? GetRespawnPoint() : FIntVector::ZeroValue;
            CaptureActors(GetWorld(), WorldActor, Restored);
            Check(IsAdventureActive() && ExpansionEqual(Restored, State.ExpectedSave, .35f),
                TEXT("Actual title to continue restores crop age, furnace transaction state, bed heading and selected respawn"));
            State.Crop = ActorAt<ABlockRealmCrop>(GetWorld(), WorldActor, State.CropCell);
            State.Furnace = ActorAt<ABlockRealmFurnace>(GetWorld(), WorldActor, State.FurnaceCell);
            State.Bed = ActorAt<ABlockRealmRestPoint>(GetWorld(), WorldActor, State.BedCell);
            FVector RestoredSafe;
            Check(State.Bed.IsValid() && State.Bed->GetHeading() == 1
                && State.Bed->FindSafeRespawn(RestoredSafe)
                && Restored.bHasRespawnPoint && Restored.RespawnPoint == State.BedCell,
                TEXT("The restored +Y bed preserves its two-cell direction and still provides the selected safe respawn"));
        }
        AimCrop(7.f);
        Shot(TEXT("18_ExpansionContinue"));
        SmokeTime = .25f;
        break;
    case 32:
        {
            bool bFoundWater = false;
            for (int32 X = -80; X <= 80 && !bFoundWater; X += 2)
                for (int32 Y = -80; Y <= 80 && !bFoundWater; Y += 2)
                {
                    bool bClear = true;
                    for (int32 DX = -1; DX <= 1 && bClear; ++DX)
                        for (int32 DY = -1; DY <= 1 && bClear; ++DY)
                        {
                            for (int32 Z = FVoxelData::SeaLevel - 3; Z <= FVoxelData::SeaLevel && bClear; ++Z)
                                bClear = WorldActor->GetBlock(FIntVector(X + DX, Y + DY, Z)) == EBlockType::Water;
                            bClear &= WorldActor->GetBlock(FIntVector(X + DX, Y + DY, FVoxelData::SeaLevel + 1)) == EBlockType::Air;
                        }
                    if (bClear)
                    {
                        State.WetEyePosition = ABlockWorld::BlockToWorld(FIntVector(X, Y, FVoxelData::SeaLevel - 1));
                        State.DryEyePosition = State.WetEyePosition;
                        State.DryEyePosition.Z = (FVoxelData::SeaLevel + 1) * FVoxelData::BlockSize + 35.f;
                        bFoundWater = true;
                    }
                }
            Check(bFoundWater, TEXT("Breathing fixture finds actual deep voxel Water with an open Air surface"));
            if (!bFoundWater) return true;
        }
        Release();
        State.BreathRestore = Player->CaptureSaveState();
        PC->SetControlRotation(FRotator::ZeroRotator);
        // Hold the camera height against buoyancy, while the real character,
        // Actions component and HUD continue to tick throughout these waits.
        Movement->SetComponentTickEnabled(false);
        MoveEyesTo(State.DryEyePosition);
        Actions->ResetState();
        Player->RestoreSurvivalHealth(Player->GetMaxHealth());
        State.HealthBefore = Player->GetHealth();
        SmokeTime = .5f;
        break;
    case 33:
        {
            const FVector Feet = Player->GetActorLocation()
                - FVector(0.f, 0.f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 8.f);
            Check(WorldActor->GetBlock(ABlockWorld::WorldToBlock(Feet)) == EBlockType::Water
                && WorldActor->GetBlock(ABlockWorld::WorldToBlock(Camera->GetComponentLocation())) == EBlockType::Air
                && Player->IsInWater() && !Actions->IsHeadUnderwater()
                && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), 15.f, .001f)
                && FMath::IsNearlyEqual(Actions->GetBreathPercent(), 1.f, .001f)
                && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .001f),
                TEXT("Actual ticks keep the full fifteen-second breath reserve when only feet are in Water"));
        }
        MoveEyesTo(State.WetEyePosition);
        State.BreathSampleAt = CompletedActionsTime;
        SmokeTime = .8f;
        break;
    case 34:
        Check(WorldActor->GetBlock(ABlockWorld::WorldToBlock(Camera->GetComponentLocation())) == EBlockType::Water
            && Actions->IsHeadUnderwater()
            && FMath::IsNearlyEqual(15.f - Actions->GetBreathSeconds(), CompletedActionsTime - State.BreathSampleAt, .12f)
            && Actions->GetBreathSeconds() < 14.5f
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .001f),
            TEXT("A submerged real camera consumes one breath second per active Tick second before any damage"));
        Key(EKeys::E);
        SmokeTime = .08f;
        break;
    case 35:
        Check(Player->IsInventoryOpen(), TEXT("Actual E opens the underwater breath freeze test"));
        State.BreathBefore = Actions->GetBreathSeconds();
        State.HealthBefore = Player->GetHealth();
        SmokeTime = .5f;
        break;
    case 36:
        Check(Player->IsInventoryOpen() && Actions->IsHeadUnderwater()
            && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), State.BreathBefore, .0001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .0001f),
            TEXT("E inventory freezes the real underwater breath reserve and health"));
        Key(EKeys::E);
        SmokeTime = .1f;
        break;
    case 37:
        Check(!Player->IsInventoryOpen() && Actions->GetBreathSeconds() < State.BreathBefore - .02f,
            TEXT("Closing E resumes real underwater breath consumption"));
        Key(EKeys::Escape);
        SmokeTime = .08f;
        break;
    case 38:
        Check(IsPausedMenu(), TEXT("Actual Esc opens the underwater breath freeze test"));
        State.BreathBefore = Actions->GetBreathSeconds();
        State.HealthBefore = Player->GetHealth();
        SmokeTime = .5f;
        break;
    case 39:
        Check(IsPausedMenu() && Actions->IsHeadUnderwater()
            && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), State.BreathBefore, .0001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .0001f),
            TEXT("Esc pause freezes the real underwater breath reserve and health"));
        // Hold the old menu click gate shut deliberately: environmental survival
        // must continue even while player action readiness remains false.
        Input(EKeys::LeftMouseButton, true);
        Key(EKeys::Escape);
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .05f;
        break;
    case 40:
        if (!State.bBreathShotRequested && Actions->GetBreathSeconds() <= 3.5f)
        {
            Shot(TEXT("19_UnderwaterBreath"));
            State.bBreathShotRequested = true;
            SmokeTime = .08f;
            return false;
        }
        if (Actions->GetBreathSeconds() > .0001f && SmokeElapsed - State.WaitStartedAt < 16.f)
        {
            SmokeTime = .05f;
            return false;
        }
        Check(IsAdventureActive() && !Player->CanUsePlayerActions() && Actions->IsHeadUnderwater()
            && FMath::IsNearlyZero(Actions->GetBreathSeconds(), .0001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .001f),
            TEXT("Real ticks exhaust all fifteen breath seconds independently of the mouse action gate, without early drowning damage"));
        Input(EKeys::LeftMouseButton, false);
        Key(EKeys::E);
        SmokeTime = .08f;
        break;
    case 41:
        Check(Player->IsInventoryOpen(), TEXT("Actual E opens with an exhausted underwater breath reserve"));
        State.HealthBefore = Player->GetHealth();
        SmokeTime = 1.1f;
        break;
    case 42:
        Check(Player->IsInventoryOpen() && FMath::IsNearlyZero(Actions->GetBreathSeconds(), .0001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .0001f),
            TEXT("More than one real second inside E advances neither depleted breath nor the drowning damage timer"));
        Key(EKeys::E);
        SmokeTime = .08f;
        break;
    case 43:
        Check(!Player->IsInventoryOpen() && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .001f),
            TEXT("Closing E does not replay the frozen drowning interval"));
        Key(EKeys::Escape);
        SmokeTime = .08f;
        break;
    case 44:
        Check(IsPausedMenu(), TEXT("Actual Esc opens with an exhausted underwater breath reserve"));
        State.HealthBefore = Player->GetHealth();
        SmokeTime = 1.1f;
        break;
    case 45:
        Check(IsPausedMenu() && FMath::IsNearlyZero(Actions->GetBreathSeconds(), .0001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .0001f),
            TEXT("More than one real second inside Esc advances neither depleted breath nor the drowning damage timer"));
        Key(EKeys::Escape);
        SmokeTime = 1.1f;
        break;
    case 46:
        Check(IsAdventureActive() && Actions->IsHeadUnderwater()
            && FMath::IsNearlyZero(Actions->GetBreathSeconds(), .0001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore - 2.f, .01f),
            TEXT("After resuming, one real depleted underwater second deals exactly two health damage"));
        State.HealthBefore = Player->GetHealth();
        MoveEyesTo(State.DryEyePosition);
        State.BreathSampleAt = CompletedActionsTime;
        SmokeTime = .8f;
        break;
    case 47:
        Check(!Actions->IsHeadUnderwater()
            && WorldActor->GetBlock(ABlockWorld::WorldToBlock(Camera->GetComponentLocation())) == EBlockType::Air
            && Actions->GetBreathSeconds() > 3.f && Actions->GetBreathSeconds() < 6.f
            && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), (CompletedActionsTime - State.BreathSampleAt) * 5.f, .6f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .001f),
            TEXT("Exposing the actual camera to Air restores breath over time and immediately stops drowning damage"));
        SmokeTime = 2.3f;
        break;
    case 48:
        Check(!Actions->IsHeadUnderwater() && Player->IsInWater()
            && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), 15.f, .001f)
            && FMath::IsNearlyEqual(Actions->GetBreathPercent(), 1.f, .001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), State.HealthBefore, .001f),
            TEXT("Roughly three actual Air seconds refill breath completely while feet remain in Water"));
        MoveEyesTo(State.WetEyePosition);
        SmokeTime = .6f;
        break;
    case 49:
        Check(Actions->IsHeadUnderwater() && Actions->GetBreathSeconds() < 14.6f,
            TEXT("Submerging again resumes the real breath clock after surface recovery"));
        Actions->ResetState();
        Check(!Actions->IsHeadUnderwater() && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), 15.f, .001f),
            TEXT("ResetState immediately clears immersion and restores the entire transient breath reserve"));
        SmokeTime = .4f;
        break;
    case 50:
        Check(Actions->IsHeadUnderwater() && Actions->GetBreathSeconds() < 14.8f,
            TEXT("Real ticks sample the submerged camera again after ResetState"));
        Player->RestoreSaveState(State.BreathRestore);
        Check(!Actions->IsHeadUnderwater() && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), 15.f, .001f),
            TEXT("Restoring a real player save resets breath instead of carrying the prior dive into the restored adventure"));
        MoveEyesTo(State.WetEyePosition);
        SmokeTime = .4f;
        break;
    case 51:
        Check(Actions->IsHeadUnderwater() && Actions->GetBreathSeconds() < 14.8f,
            TEXT("The lethal damage fixture starts with a genuinely submerged camera and spent breath"));
        UGameplayStatics::ApplyDamage(Player, 1000.f, PC, this, UDamageType::StaticClass());
        Check(!Actions->IsHeadUnderwater() && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), 15.f, .001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), Player->GetMaxHealth(), .001f)
            && FVector::Dist(Player->GetActorLocation(), GetAdventureRespawnLocation()) < 15.f,
            TEXT("Actual lethal damage respawns safely and resets transient breath and immersion through ResetState"));
        SmokeTime = 1.2f;
        break;
    case 52:
        Check(!Actions->IsHeadUnderwater() && FMath::IsNearlyEqual(Actions->GetBreathSeconds(), 15.f, .001f)
            && FMath::IsNearlyEqual(Player->GetHealth(), Player->GetMaxHealth(), .001f),
            TEXT("Later real ticks cause no residual drowning damage after respawn"));
        Player->RestoreSaveState(State.BreathRestore);
        Movement->SetComponentTickEnabled(true);
        Release();
        DayNightActor->SetAutoAdvance(false);
        Check(SmokeElapsed - State.StartedAt < BudgetSeconds,
            TEXT("Expansion smoke completes within its 55-second added budget"));
        UE_LOG(LogTemp, Display, TEXT("BR_EXPANSION elapsed=%.2f seconds stages=%d"),
            SmokeElapsed - State.StartedAt, State.Stage + 1);
        return true;
    default:
        return true;
    }
    ++State.Stage;
    return false;
}
