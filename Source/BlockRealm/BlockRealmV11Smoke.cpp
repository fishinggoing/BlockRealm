#include "BlockRealmGameMode.h"

#include "BlockRealmAnimal.h"
#include "BlockRealmCharacter.h"
#include "BlockRealmCreaturePopulation.h"
#include "BlockRealmHUD.h"
#include "BlockRealmMovementComponent.h"
#include "BlockRealmSurvival.h"
#include "BlockWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUDHitBox.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "InputCoreTypes.h"
#include "InputKeyEventArgs.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"

struct FBlockRealmV11SmokeState
{
    int32 Stage = 0;
    bool bDone = false;
    bool bInventoryShot = false;
    bool bInventoryHoverReady = false;
    bool bInventoryTooltipShot = false;
    bool bToolShot = false;
    bool bPopulationShot = false;
    bool bFallShot = false;
    float StartedAt = 0.f;
    float WaitStartedAt = 0.f;
    FIntVector Root = FIntVector::ZeroValue;
    FIntVector MiningCell = FIntVector::ZeroValue;
    FVector FloorPosition = FVector::ZeroVector;
    FVector LedgePosition = FVector::ZeroVector;
    FVector AnimalPosition = FVector::ZeroVector;
    FVector DeathPosition = FVector::ZeroVector;
    FWorldSaveState Restore;
    FWorldSaveState FrozenPlayer;
    TWeakObjectPtr<ABlockRealmCreaturePopulation> Population;
    TWeakObjectPtr<ABlockRealmAnimal> Animal;
    TArray<TWeakObjectPtr<ABlockRealmMob>> FrozenCreatures;
    TArray<FVector> FrozenPositions;
    TArray<float> FrozenHealth;
    int32 FrozenAnimals = 0;
    int32 FrozenHostiles = 0;
    int32 StoneBefore = 0;
    int32 FoodBefore = 0;
    int32 DropsBefore = 0;
    int32 ExpectedFood = 0;
    float HealthBefore = 0.f;
    float AnimalHealthBefore = 0.f;
};

// Every state advances on real GameMode ticks. Teleports only prepare fixtures;
// the measured fall starts after a real grounded frame and uses walking input.
bool ABlockRealmGameMode::TickV11Smoke()
{
    const auto Check = [this](bool bPass, const TCHAR* Name)
    {
        (bPass ? SmokeChecks : SmokeFailures).Add(Name);
        UE_LOG(LogTemp, Display, TEXT("BR_CHECK %s %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), Name);
    };
    ABlockRealmCharacter* Player = GetPlayerCharacter();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    UBlockRealmMovementComponent* Movement = Player
        ? Cast<UBlockRealmMovementComponent>(Player->GetCharacterMovement()) : nullptr;
    UCameraComponent* Camera = Player ? Player->FindComponentByClass<UCameraComponent>() : nullptr;
    if (!Player || !PC || !Movement || !Camera || !WorldActor)
    {
        Check(false, TEXT("V11 regression has the actual player, controller, camera, movement and world"));
        return true;
    }
    if (!V11SmokeState.IsValid())
    {
        V11SmokeState = MakeShared<FBlockRealmV11SmokeState>();
        V11SmokeState->StartedAt = SmokeElapsed;
    }
    FBlockRealmV11SmokeState& State = *V11SmokeState;
    if (State.bDone) return true;
    const auto Input = [PC](const FKey& Key, bool bDown)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key,
            bDown ? IE_Pressed : IE_Released, bDown ? 1.f : 0.f));
    };
    const auto Key = [&Input](const FKey& Pressed) { Input(Pressed, true); Input(Pressed, false); };
    const auto Shot = [](const TCHAR* Name)
    {
        FString Directory;
        if (!FParse::Value(FCommandLine::Get(), TEXT("BRValidationDir="), Directory))
            Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Validation"));
        IFileManager::Get().MakeDirectory(*Directory, true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Directory,
            FString(Name) + TEXT(".png")), true, false);
    };
    const auto CheckNativeResolution = [this, &Check]()
    {
        const IConsoleVariable* Primary = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
        const IConsoleVariable* Secondary = IConsoleManager::Get().FindConsoleVariable(TEXT("r.SecondaryScreenPercentage.GameViewport"));
        Check(GetUpscaleMode() == 0 && Primary && Secondary
            && FMath::IsNearlyEqual(Primary->GetFloat(), 100.f, .001f)
            && FMath::IsNearlyEqual(Secondary->GetFloat(), 100.f, .001f),
            TEXT("V11 TSR runs at native 100 percent primary and secondary viewport resolution"));
    };
    const auto Click = [PC, &Key, &Check](FName Name, bool bExpectedHit = true)
    {
        int32 Width = 0, Height = 0;
        PC->GetViewportSize(Width, Height);
        const FBox2D Bounds = ABlockRealmHUD::GetInventoryButtonBounds(Name, FVector2D(Width, Height));
        const FVector2D Center = Bounds.GetCenter();
        ABlockRealmHUD* HUD = Cast<ABlockRealmHUD>(PC->GetHUD());
        const FHUDHitBox* Hit = HUD && Bounds.bIsValid ? HUD->GetHitBoxAtCoordinates(Center, true) : nullptr;
        const bool bFound = Width > 0 && Height > 0 && Bounds.bIsValid && Hit && Hit->GetName() == Name;
        Check(bExpectedHit ? bFound : (Width > 0 && Height > 0 && Bounds.bIsValid && !Hit),
            *FString::Printf(TEXT("V11 actual inventory hitbox %s %s"), *Name.ToString(),
                bExpectedHit ? TEXT("is clickable") : TEXT("is disabled while empty")));
        if (!Bounds.bIsValid || Width <= 0 || Height <= 0 || (bExpectedHit && !bFound)) return;
        PC->SetMouseLocation(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
        Key(EKeys::LeftMouseButton);
    };
    const auto SetPosition = [this, Player, Movement](const FVector& Position)
    {
        Player->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
        Movement->StopMovementImmediately();
        Movement->SetMovementMode(MOVE_Walking);
        WorldActor->RequestStreaming(Position);
    };
    const auto Aim = [PC, Camera, Player](const FVector& Position)
    {
        PC->SetControlRotation((Position - Camera->GetComponentLocation()).Rotation());
        Player->RefreshTarget();
    };
    const auto FoodDrops = [this]()
    {
        int32 Count = 0;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
            if (!It->IsActorBeingDestroyed() && It->ActorHasTag(TEXT("BlockRealmFoodDrop"))) ++Count;
        return Count;
    };
    const auto SnapshotFrozen = [Player, &State]()
    {
        State.FrozenPlayer = Player->CaptureSaveState();
        State.FrozenCreatures.Empty();
        State.FrozenPositions.Empty();
        State.FrozenHealth.Empty();
        if (ABlockRealmCreaturePopulation* Population = State.Population.Get())
        {
            State.FrozenAnimals = Population->GetAnimalCount();
            State.FrozenHostiles = Population->GetHostileCount();
            for (ABlockRealmMob* Creature : Population->GetCreatures())
            {
                if (!IsValid(Creature) || Creature->IsDead()) continue;
                State.FrozenCreatures.Add(Creature);
                State.FrozenPositions.Add(Creature->GetActorLocation());
                State.FrozenHealth.Add(Creature->GetHealth());
            }
        }
    };
    const auto CheckFrozen = [this, Player, &State, &Check](bool bInventory)
    {
        bool bCreaturesFrozen = !State.FrozenCreatures.IsEmpty();
        for (int32 Index = 0; Index < State.FrozenCreatures.Num(); ++Index)
        {
            const ABlockRealmMob* Creature = State.FrozenCreatures[Index].Get();
            bCreaturesFrozen &= Creature && !Creature->IsDead()
                && FVector::Dist(Creature->GetActorLocation(), State.FrozenPositions[Index]) < .1f
                && FMath::IsNearlyEqual(Creature->GetHealth(), State.FrozenHealth[Index], .0001f);
        }
        const ABlockRealmCreaturePopulation* Population = State.Population.Get();
        bCreaturesFrozen &= Population && Population->GetAnimalCount() == State.FrozenAnimals
            && Population->GetHostileCount() == State.FrozenHostiles;
        Check((bInventory ? Player->IsInventoryOpen() : IsPausedMenu()) && bCreaturesFrozen,
            bInventory ? TEXT("V11 E inventory freezes every populated creature, damage and population counts")
                : TEXT("V11 Esc pause freezes every populated creature, damage and population counts"));
        const FWorldSaveState Current = Player->CaptureSaveState();
        Check(FVector::Dist(Current.Position, State.FrozenPlayer.Position) < .1f
            && FMath::IsNearlyEqual(Current.Health, State.FrozenPlayer.Health, .0001f)
            && Current.Inventory == State.FrozenPlayer.Inventory && Current.FoodCount == State.FrozenPlayer.FoodCount,
            bInventory ? TEXT("V11 inventory freezes player position, health, blocks and food")
                : TEXT("V11 pause freezes player position, health, blocks and food"));
    };
    const auto Cleanup = [this, Player, Movement, &State, &Input]()
    {
        Input(EKeys::W, false);
        Input(EKeys::LeftMouseButton, false);
        Input(EKeys::RightMouseButton, false);
        Input(EKeys::MiddleMouseButton, false);
        if (ABlockRealmCreaturePopulation* Population = State.Population.Get())
        {
            Population->ClearPopulation();
            Population->Destroy();
        }
        Player->RestoreSaveState(State.Restore);
        Movement->SetComponentTickEnabled(true);
        SetUpscaleMode(0);
        State.bDone = true;
    };

    switch (State.Stage)
    {
    case 0:
    {
        SetUpscaleMode(0);
        TArray<FString> Failures;
        Check(FBlockRealmSurvivalData::RunRuleSelfTests(Failures),
            TEXT("V11 mining tiers, denials, craft progression and fall rule self-tests"));
        for (const FString& Failure : Failures) Check(false, *Failure);
        if (IsPausedMenu()) TogglePauseMenu();
        if (Player->IsInventoryOpen()) Player->ToggleInventory();
        State.Restore = Player->CaptureSaveState();
        State.Root = ABlockWorld::WorldToBlock(WorldActor->GetSpawnLocation());
        State.Root.Z = 44;
        for (TActorIterator<AActor> It(GetWorld()); It; ++It)
            if (Cast<ABlockRealmMob>(*It) || It->ActorHasTag(TEXT("BlockRealmFoodDrop"))) It->Destroy();
        ActiveMobs.Empty();
        // A broad dry grass fixture supports natural-soil population placement
        // and leaves room for genuine flee movement in every direction.
        for (int32 X = -16; X <= 16; ++X)
            for (int32 Y = -16; Y <= 16; ++Y)
            {
                WorldActor->SetBlock(State.Root + FIntVector(X, Y, 0), EBlockType::Grass);
                for (int32 Z = 1; Z <= 8; ++Z)
                    WorldActor->SetBlock(State.Root + FIntVector(X, Y, Z), EBlockType::Air);
            }
        for (int32 X = -14; X <= -12; ++X)
            for (int32 Y = -14; Y <= -12; ++Y)
                WorldActor->SetBlock(State.Root + FIntVector(X, Y, 5), EBlockType::Stone);
        const float StandOffset = 50.f + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
        State.FloorPosition = ABlockWorld::BlockToWorld(State.Root) + FVector(0.f, 0.f, StandOffset);
        State.LedgePosition = ABlockWorld::BlockToWorld(State.Root + FIntVector(-13, -13, 5))
            + FVector(0.f, 0.f, StandOffset);
        State.MiningCell = State.Root + FIntVector(3, 0, 2);
        WorldActor->SetBlock(State.MiningCell, EBlockType::Stone);
        FWorldSaveState Fixture = State.Restore;
        Fixture.Position = State.FloorPosition;
        Fixture.Rotation = FRotator::ZeroRotator;
        Fixture.Health = 100.f;
        Fixture.Hunger = 50.f; // Avoid regeneration masking an exact fall-damage check.
        Fixture.Inventory.Init(64, 9);
        Fixture.Inventory[6] = Fixture.Inventory[7] = 0;
        Fixture.SelectedSlot = 0;
        Fixture.SelectedToolSlot = INDEX_NONE;
        Fixture.bHasSurvivalState = true;
        Fixture.ToolSlots.SetNum(FBlockRealmSurvivalData::ToolSlotCount);
        for (FBlockRealmSavedTool& Tool : Fixture.ToolSlots) Tool = FBlockRealmSavedTool();
        Fixture.ToolSlots[0].Type = static_cast<uint8>(EBlockRealmToolType::WoodenPickaxe);
        Fixture.ToolSlots[0].Durability = Fixture.ToolSlots[0].MaxDurability = FBlockRealmSurvivalData::WoodenPickaxeDurability;
        Fixture.ToolSlots[0].Quantity = 1;
        Fixture.ToolSlots[2].Type = static_cast<uint8>(EBlockRealmToolType::Torch);
        Fixture.ToolSlots[2].Quantity = 4;
        Player->RestoreSaveState(Fixture);
        Movement->SetComponentTickEnabled(false);
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .2f;
        break;
    }
    case 1:
    {
        FHitResult Floor;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmV11Floor), false, Player);
        const bool bFloor = GetWorld()->LineTraceSingleByChannel(Floor, State.FloorPosition,
            State.FloorPosition - FVector(0.f, 0.f, 250.f), ECC_Visibility, Query)
            && FMath::IsNearlyEqual(Floor.ImpactPoint.Z, (State.Root.Z + 1) * 100.f, 2.f);
        if (!bFloor && SmokeElapsed - State.WaitStartedAt < 8.f) { SmokeTime = .1f; return false; }
        Check(bFloor, TEXT("V11 fixture waits for actual dry ground collision"));
        if (!bFloor) { Cleanup(); return true; }
        Movement->SetComponentTickEnabled(true);
        SmokeTime = .3f;
        break;
    }
    case 2:
        CheckNativeResolution();
        Check(Movement->IsMovingOnGround() && !Player->IsFlying(), TEXT("V11 player stands on the real collision fixture"));
        Key(EKeys::E);
        SmokeTime = .12f;
        break;
    case 3:
        if (!State.bInventoryShot)
        {
            Shot(TEXT("21_V11Inventory"));
            State.bInventoryShot = true;
            SmokeTime = .08f;
            return false;
        }
        if (!State.bInventoryHoverReady)
        {
            int32 Width = 0, Height = 0;
            PC->GetViewportSize(Width, Height);
            const FBox2D Bounds = ABlockRealmHUD::GetInventoryButtonBounds(
                TEXT("InventoryBlock_8"), FVector2D(Width, Height));
            const FVector2D Center = Bounds.GetCenter();
            PC->SetMouseLocation(FMath::RoundToInt(Center.X), FMath::RoundToInt(Center.Y));
            State.bInventoryHoverReady = true;
            SmokeTime = .15f;
            return false;
        }
        if (!State.bInventoryTooltipShot)
        {
            Shot(TEXT("26_V11InventoryTooltip"));
            State.bInventoryTooltipShot = true;
            SmokeTime = .08f;
            return false;
        }
        Click(TEXT("InventoryBlock_0"));
        SmokeTime = .12f;
        break;
    case 4:
        Check(!Player->IsInventoryOpen() && Player->IsBlockEquipped() && Player->GetSelectedSlot() == 0
            && Player->GetInventoryCount(0) == 64 && Player->GetHeldItemMesh()
            && Player->GetHeldItemMesh()->IsVisible() && Player->GetHeldItemMesh()->GetProcMeshSection(2),
            TEXT("V11 real block hitbox equips a visible held block and closes E without consuming inventory"));
        Key(EKeys::E);
        SmokeTime = .12f;
        break;
    case 5:
        Click(TEXT("InventoryTool_0"));
        SmokeTime = .12f;
        break;
    case 6:
        if (!State.bToolShot)
        {
            Shot(TEXT("22_V11WoodenPickaxe"));
            State.bToolShot = true;
            SmokeTime = .08f;
            return false;
        }
        Check(!Player->IsInventoryOpen() && Player->GetSelectedToolSlot() == 0
            && Player->GetSelectedToolType() == EBlockRealmToolType::WoodenPickaxe
            && Player->GetHeldItemMesh() && Player->GetHeldItemMesh()->IsVisible()
            && Player->GetHeldItemMesh()->GetProcMeshSection(2) && Player->GetHeldItemMesh()->GetProcMeshSection(3),
            TEXT("V11 real wooden-pickaxe hitbox equips both handle and head mesh sections and closes E"));
        Key(EKeys::E);
        SmokeTime = .12f;
        break;
    case 7:
        Click(TEXT("InventoryBareHands"));
        SmokeTime = .12f;
        break;
    case 8:
        Check(!Player->IsInventoryOpen() && Player->IsBareHandsEquipped() && !Player->IsBlockEquipped()
            && Player->GetSelectedToolType() == EBlockRealmToolType::None
            && Player->GetHeldItemMesh() && Player->GetHeldItemMesh()->IsVisible()
            && Player->GetHeldItemMesh()->GetProcMeshSection(0)
            && !Player->GetHeldItemMesh()->GetProcMeshSection(2) && !Player->GetHeldItemMesh()->GetProcMeshSection(3),
            TEXT("V11 real bare-hands hitbox closes E and replaces the held item with the hand mesh"));
        Key(EKeys::E);
        SmokeTime = .12f;
        break;
    case 9:
        Click(TEXT("InventoryTool_5"), false);
        SmokeTime = .12f;
        break;
    case 10:
        Check(Player->IsInventoryOpen() && Player->IsBareHandsEquipped(),
            TEXT("V11 clicking an empty tool position keeps E open and preserves bare hands"));
        Click(TEXT("InventoryBlock_6"));
        SmokeTime = .12f;
        break;
    case 11:
        Check(Player->IsInventoryOpen() && Player->IsBareHandsEquipped() && Player->GetInventoryCount(6) == 0,
            TEXT("V11 empty block hitbox rejects equip without closing E or creating a phantom item"));
        Click(TEXT("InventoryClose"));
        SmokeTime = .12f;
        break;
    case 12:
        Check(!Player->IsInventoryOpen(), TEXT("V11 actual inventory close hitbox returns to play"));
        Aim(ABlockWorld::BlockToWorld(State.MiningCell));
        Check(Player->HasTarget() && Player->GetTargetBlock() == State.MiningCell,
            TEXT("V11 bare-hands mining camera targets the real stone fixture"));
        State.StoneBefore = Player->GetInventoryCount(2);
        Input(EKeys::LeftMouseButton, true);
        SmokeTime = 1.f;
        break;
    case 13:
        Check(Player->IsBareHandsEquipped() && !Player->IsMining() && Player->GetMiningProgress() == 0.f
            && WorldActor->GetBlock(State.MiningCell) == EBlockType::Stone
            && Player->GetInventoryCount(2) == State.StoneBefore && !Player->GetInteractionNotice().IsEmpty(),
            TEXT("V11 a real held bare-hand click denies stone mining with a reason and no progress, edit or reward"));
        Input(EKeys::LeftMouseButton, false);
        WorldActor->SetBlock(State.MiningCell, EBlockType::Air);
        SetPosition(State.LedgePosition);
        Movement->SetComponentTickEnabled(false);
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .1f;
        break;
    case 14:
    {
        FHitResult Floor;
        FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmV11Ledge), false, Player);
        const bool bLedge = GetWorld()->LineTraceSingleByChannel(Floor, State.LedgePosition,
            State.LedgePosition - FVector(0.f, 0.f, 250.f), ECC_Visibility, Query)
            && FMath::IsNearlyEqual(Floor.ImpactPoint.Z, (State.Root.Z + 6) * 100.f, 2.f);
        if (!bLedge && SmokeElapsed - State.WaitStartedAt < 5.f) { SmokeTime = .1f; return false; }
        Check(bLedge, TEXT("V11 five-block fall begins above an actual ledge collider"));
        Movement->SetComponentTickEnabled(true);
        SmokeTime = .4f;
        break;
    }
    case 15:
        Check(Movement->IsMovingOnGround() && Movement->CurrentFloor.IsWalkableFloor()
            && FVector::Dist(Player->GetActorLocation(), State.LedgePosition) < 5.f,
            TEXT("V11 fall protection is rearmed by real grounded frames after the fixture teleport"));
        State.HealthBefore = Player->GetHealth();
        PC->SetControlRotation(FRotator::ZeroRotator);
        Input(EKeys::W, true);
        State.WaitStartedAt = SmokeElapsed;
        SmokeTime = .03f;
        break;
    case 16:
        if (!Movement->IsFalling() && SmokeElapsed - State.WaitStartedAt < 3.f) { SmokeTime = .03f; return false; }
        Check(Movement->IsFalling() && Player->GetActorLocation().X > State.LedgePosition.X + 120.f,
            TEXT("V11 actual W walks off the ledge and enters gravity-driven falling"));
        Input(EKeys::W, false);
        SmokeTime = .08f;
        break;
    case 17:
        if (!Movement->IsMovingOnGround() && SmokeElapsed - State.WaitStartedAt < 5.f) { SmokeTime = .03f; return false; }
        if (!State.bFallShot)
        {
            Check(Movement->IsMovingOnGround() && Movement->GetLastFallDistanceCm() > 470.0
                && Movement->GetLastFallDistanceCm() < 530.0 && Movement->GetLastFallDamage() > 17.f
                && Movement->GetLastFallDamage() < 23.f
                && FMath::IsNearlyEqual(State.HealthBefore - Player->GetHealth(), Movement->GetLastFallDamage(), .05f),
                TEXT("V11 a real five-block walking fall lands and removes the measured twenty health"));
            UE_LOG(LogTemp, Display, TEXT("BR_V11_FALL distance_cm=%.2f damage=%.2f health_before=%.2f health_after=%.2f"),
                Movement->GetLastFallDistanceCm(), Movement->GetLastFallDamage(), State.HealthBefore, Player->GetHealth());
            Shot(TEXT("23_V11FallDamage"));
            State.bFallShot = true;
            SmokeTime = .08f;
            return false;
        }
        SetPosition(State.FloorPosition);
        SmokeTime = .2f;
        break;
    case 18:
    {
        ABlockRealmCreaturePopulation* Population = GetWorld()->SpawnActor<ABlockRealmCreaturePopulation>();
        State.Population = Population;
        Check(Population != nullptr, TEXT("V11 explicitly creates the real bounded creature population"));
        if (!Population) { Cleanup(); return true; }
        PC->SetControlRotation(FRotator::ZeroRotator);
        Population->Initialize(WorldActor, Player, DayNightActor);
        Population->PopulateNow();
        SmokeTime = .3f;
        break;
    }
    case 19:
    {
        if (!State.bPopulationShot)
        {
            Shot(TEXT("24_V11Population"));
            State.bPopulationShot = true;
            SmokeTime = .08f;
            return false;
        }
        ABlockRealmCreaturePopulation* Population = State.Population.Get();
        TArray<FString> Failures;
        Check(Population && Population->GetSpeciesCount(EBlockRealmAnimalSpecies::Pig) > 0
            && Population->GetSpeciesCount(EBlockRealmAnimalSpecies::Sheep) > 0
            && Population->GetAnimalCount() <= ABlockRealmCreaturePopulation::MaximumAnimals
            && Population->GetHostileCount() <= ABlockRealmCreaturePopulation::MaximumHostiles,
            TEXT("V11 PopulateNow creates living pigs and sheep within the hard population caps"));
        Check(Population && Population->ValidatePopulation(Failures),
            TEXT("V11 every populated creature has clear dry authoritative support and the correct world"));
        for (const FString& Failure : Failures) Check(false, *Failure);
        ABlockRealmAnimal* Animal = Population ? Cast<ABlockRealmAnimal>(Population->GetNearestCreature(false)) : nullptr;
        State.Animal = Animal;
        Check(Animal && !Animal->IsHostile() && Animal->IsGrounded(), TEXT("V11 nearest passive creature is a grounded population animal"));
        if (!Animal) { Cleanup(); return true; }
        State.AnimalHealthBefore = Animal->GetHealth();
        State.ExpectedFood = Animal->GetSpecies() == EBlockRealmAnimalSpecies::Pig ? 2 : 1;
        SetPosition(Animal->GetActorLocation() - FVector(260.f, 0.f, 0.f)
            + FVector(0.f, 0.f, Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 64.f));
        Player->EquipBareHands();
        Aim(Animal->GetActorLocation());
        SmokeTime = .12f;
        break;
    }
    case 20:
        if (ABlockRealmAnimal* Animal = State.Animal.Get())
        {
            Aim(Animal->GetActorLocation());
            State.AnimalPosition = Animal->GetActorLocation();
        }
        Shot(TEXT("25_V11BareHandsAnimal"));
        Key(EKeys::LeftMouseButton);
        SmokeTime = .08f;
        break;
    case 21:
        Check(State.Animal.IsValid() && State.Animal->IsFleeing()
            && FMath::IsNearlyEqual(State.AnimalHealthBefore - State.Animal->GetHealth(), 4.f, .001f)
            && !Player->IsMining(),
            TEXT("V11 actual left-click melee damages the animal and starts fleeing without mining terrain"));
        SmokeTime = .35f;
        break;
    case 22:
        Check(State.Animal.IsValid() && State.Animal->IsFleeing()
            && FVector::Dist2D(State.Animal->GetActorLocation(), State.AnimalPosition) > 30.f,
            TEXT("V11 a hit animal physically flees across dry voxel support on real ticks"));
        Key(EKeys::E);
        SmokeTime = .12f;
        break;
    case 23:
        SnapshotFrozen();
        if (State.Animal.IsValid()) State.Animal->ApplyDamage(100.f);
        Input(EKeys::W, true);
        Key(EKeys::MiddleMouseButton);
        SmokeTime = .5f;
        break;
    case 24:
        CheckFrozen(true);
        Input(EKeys::W, false);
        Key(EKeys::E);
        SmokeTime = .12f;
        break;
    case 25:
        Check(State.Animal.IsValid() && State.Animal->IsFleeing(), TEXT("V11 closing E retains the interrupted flee state"));
        Key(EKeys::Escape);
        SmokeTime = .12f;
        break;
    case 26:
        SnapshotFrozen();
        if (State.Animal.IsValid()) State.Animal->ApplyDamage(100.f);
        Input(EKeys::W, true);
        Key(EKeys::MiddleMouseButton);
        SmokeTime = .5f;
        break;
    case 27:
        CheckFrozen(false);
        Input(EKeys::W, false);
        Key(EKeys::Escape);
        SmokeTime = .12f;
        break;
    case 28:
        if (ABlockRealmAnimal* Animal = State.Animal.Get())
        {
            State.DeathPosition = Animal->GetActorLocation();
            State.FoodBefore = Player->GetFoodCount();
            State.DropsBefore = FoodDrops();
            // The initial hit above exercises controller aiming and melee.
            // Lethal damage now tests the same actual actor death/drop path.
            Animal->ApplyDamage(100.f);
        }
        SmokeTime = .05f;
        break;
    case 29:
        Check((!State.Animal.IsValid() || State.Animal->IsDead())
            && FoodDrops() == State.DropsBefore + State.ExpectedFood,
            TEXT("V11 lethal animal damage produces the species-specific real food pickup actors"));
        SetPosition(State.DeathPosition + FVector(0.f, 0.f,
            Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() - 64.f));
        SmokeTime = .35f;
        break;
    case 30:
        Check(Player->GetFoodCount() == State.FoodBefore + State.ExpectedFood && FoodDrops() == State.DropsBefore,
            TEXT("V11 approaching the real animal drops collects exactly their food count and removes the pickup actors"));
        Cleanup();
        CheckNativeResolution();
        UE_LOG(LogTemp, Display, TEXT("BR_V11 elapsed=%.2f seconds stages=%d"), SmokeElapsed - State.StartedAt, State.Stage + 1);
        return true;
    default:
        Check(false, TEXT("V11 smoke reached an unexpected stage"));
        Cleanup();
        return true;
    }
    ++State.Stage;
    return false;
}
