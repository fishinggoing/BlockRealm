#include "BlockRealmGameMode.h"

#include "BlockRealmCharacter.h"
#include "BlockWorld.h"
#include "BlockChunk.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "UnrealClient.h"

// This is part of the existing isolated -BlockRealmSmoke run. Edits prepare a
// fixture; all actions under test use the real pawn, controller and collision.
bool ABlockRealmGameMode::TickInteractionSmoke()
{
    ABlockRealmCharacter* Player = GetPlayerCharacter();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0);
    if (!Player || !PC || !WorldActor) return true;
    UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
    const auto Check = [this](bool Pass, const TCHAR* Name)
    {
        (Pass ? SmokeChecks : SmokeFailures).Add(Name);
        UE_LOG(LogTemp, Display, TEXT("BR_CHECK %s %s"), Pass ? TEXT("PASS") : TEXT("FAIL"), Name);
    };
    const auto Input = [PC](const FKey& Key, bool Down)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, Down ? IE_Pressed : IE_Released, Down ? 1.f : 0.f));
    };
    const auto Key = [&Input](const FKey& Pressed) { Input(Pressed, true); Input(Pressed, false); };
    const auto OnPlatform = [this, Player](int32 X, int32 Y)
    {
        FWorldSaveState State = Player->CaptureSaveState();
        State.Position = ABlockWorld::BlockToWorld(SmokeFixtureRoot + FIntVector(X, Y, 0));
        State.Position.Z += 50.f + Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 2.f;
        State.Rotation = FRotator::ZeroRotator;
        Player->RestoreSaveState(State);
    };
    const auto Shot = [](const TCHAR* Name)
    {
        FString Dir;
        if (!FParse::Value(FCommandLine::Get(), TEXT("BRValidationDir="), Dir))
            Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Validation"));
        IFileManager::Get().MakeDirectory(*Dir, true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir, FString(Name) + TEXT(".png")), true, false);
    };
    switch (SmokeInteractionStage)
    {
    case 0:
        if (!WorldActor->IsInitialTerrainReady()) { SmokeTime = 1.f; return false; }
        StartAdventure();
        SmokeTime = .5f;
        break;
    case 1:
        if (!IsAdventureActive()) { SmokeTime = 1.f; return false; }
        Check(PC->CheatManager == nullptr, TEXT("Development play does not expose cheat flight commands"));
        SmokeFixtureRoot = ABlockWorld::WorldToBlock(WorldActor->GetSpawnLocation());
        SmokeFixtureRoot.Z = 52;
        for (int32 X = -4; X <= 4; ++X)
            for (int32 Y = -1; Y <= 1; ++Y)
                WorldActor->SetBlock(SmokeFixtureRoot + FIntVector(X, Y, 0), EBlockType::Stone);
        OnPlatform(0, 0);
        Player->SelectSlot(0);
        PC->SetControlRotation(FRotator(-89.f, 0.f, 0.f));
        Input(EKeys::LeftMouseButton, false);
        Input(EKeys::RightMouseButton, false);
        Input(EKeys::F, false);
        SmokeTime = .5f;
        break;
    case 2:
        Check(Movement->IsMovingOnGround() && !Player->IsFlying(), TEXT("Standing and normal jump start use gravity"));
        SmokePosition = SmokeFixtureRoot + FIntVector(0, 0, 1);
        SmokeInventoryCount = Player->GetInventoryCount(0);
        Input(EKeys::RightMouseButton, true);
        SmokeTime = .06f;
        break;
    case 3:
        Check(WorldActor->GetBlock(SmokePosition) == EBlockType::Air
            && Player->GetInventoryCount(0) == SmokeInventoryCount,
            TEXT("Placement overlapping the standing capsule is rejected without consuming inventory"));
        Input(EKeys::SpaceBar, true);
        SmokeTime = .48f;
        break;
    case 4:
        Input(EKeys::SpaceBar, false);
        Input(EKeys::RightMouseButton, false);
        Check(WorldActor->GetBlock(SmokePosition) == EBlockType::Grass,
            TEXT("Held right click places beneath a real jump when the capsule clears"));
        Check(Player->GetInventoryCount(0) == SmokeInventoryCount - 1,
            TEXT("Jump placement consumes exactly one block"));
        Check(!Player->IsFlying() && Movement->MovementMode != MOVE_Flying,
            TEXT("Space and underfoot building do not enable flight"));
        SmokeTime = .7f;
        break;
    case 5:
        Check(Movement->IsMovingOnGround()
            && Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
                >= (SmokePosition.Z + 1) * 100.f - 3.f,
            TEXT("The jump lands on the new block's immediate collision"));
        SmokeStartPosition = Player->GetActorLocation();
        Input(EKeys::F, true);
        SmokeTime = .08f;
        break;
    case 6:
        Check(Player->IsFlying(), TEXT("A deliberate F press enables flight"));
        Input(EKeys::F, true);
        SmokeTime = .15f;
        break;
    case 7:
        Check(Player->IsFlying(), TEXT("Repeated F while held does not toggle flight again"));
        Input(EKeys::SpaceBar, true);
        SmokeTime = .3f;
        break;
    case 8:
        Check(Player->GetActorLocation().Z > SmokeStartPosition.Z + 30.f,
            TEXT("Space moves upward during requested flight"));
        Input(EKeys::SpaceBar, false);
        Input(EKeys::F, false);
        Key(EKeys::F);
        SmokeTime = 1.f;
        break;
    case 9:
        Check(!Player->IsFlying() && Movement->IsMovingOnGround(),
            TEXT("A released second F press restores gravity and lands in walking mode"));
        Movement->SetMovementMode(MOVE_Flying);
        SmokeTime = .1f;
        break;
    case 10:
        Check(!Player->IsFlying() && Movement->MovementMode != MOVE_Flying,
            TEXT("An unexpected movement-mode change cannot latch unrequested flight"));
        OnPlatform(-3, -1);
        SmokeStartPosition = Player->GetActorLocation();
        Input(EKeys::W, true);
        SmokeTime = .95f;
        break;
    case 11:
        SmokeLandSpeed = Movement->Velocity.Size2D();
        Check(SmokeLandSpeed > 450.f && FVector::Dist2D(Player->GetActorLocation(), SmokeStartPosition) > 250.f,
            TEXT("Walking input reaches normal speed on the dry collision fixture"));
        Input(EKeys::W, false);
        OnPlatform(-3, 0);
        Player->SelectSlot(2);
        Check(Player->SelectToolSlot(0)
            && Player->GetSelectedToolType() == EBlockRealmToolType::WoodenPickaxe,
            TEXT("Stone mining equips the starter wooden pickaxe"));
        SmokeMiningBlock = SmokeFixtureRoot + FIntVector(1, 0, 2);
        WorldActor->SetBlock(SmokeMiningBlock, EBlockType::Stone);
        SmokeInventoryCount = Player->GetInventoryCount(2);
        SmokeTime = .3f;
        break;
    case 12:
        Player->RefreshTarget();
        Check(Player->HasTarget() && Player->GetTargetBlock() == SmokeMiningBlock,
            TEXT("Voxel targeting sees a newly edited block without a visual-mesh wait"));
        if (!Player->HasTarget() || Player->GetTargetBlock() != SmokeMiningBlock) return true;
        Check(!Player->MineTarget() && WorldActor->GetBlock(SmokeMiningBlock) == EBlockType::Stone,
            TEXT("The mining API cannot bypass a held timed action"));
        Input(EKeys::LeftMouseButton, true);
        SmokeTime = .2f;
        break;
    case 13:
        Check(Player->IsMining() && Player->GetMiningProgress() > 0.f && Player->GetMiningProgress() < 1.f
            && WorldActor->GetBlock(SmokeMiningBlock) == EBlockType::Stone,
            TEXT("Partial stone mining shows progress while retaining the block"));
        SmokeMiningDuration = Player->GetMiningDuration();
        Check(SmokeMiningDuration > .5f && FMath::IsNearlyEqual(SmokeMiningDuration,
            FBlockRealmSurvivalData::GetMiningDuration(EBlockType::Stone, EBlockRealmToolType::WoodenPickaxe), .001f),
            TEXT("Stone mining retains the meaningful duration required by the equipped wooden pickaxe"));
        Input(EKeys::LeftMouseButton, false);
        SmokeTime = .12f;
        break;
    case 14:
        Check(!Player->IsMining() && Player->GetMiningProgress() == 0.f
            && Player->GetInventoryCount(2) == SmokeInventoryCount,
            TEXT("Releasing mining cancels progress without changing inventory"));
        Input(EKeys::LeftMouseButton, true);
        SmokeTime = .2f;
        break;
    case 15:
        Key(EKeys::One);
        SmokeTime = .08f;
        break;
    case 16:
        Check(Player->GetMiningProgress() < .15f && WorldActor->GetBlock(SmokeMiningBlock) == EBlockType::Stone,
            TEXT("Changing the selected slot resets partial mining"));
        Input(EKeys::LeftMouseButton, false);
        Key(EKeys::Escape);
        SmokeTime = .15f;
        break;
    case 17:
        Check(bPauseOpen && !Player->IsMining(), TEXT("The pause menu clears held mining"));
        Key(EKeys::Escape);
        SmokeTime = .3f;
        break;
    case 18:
        Check(IsAdventureActive() && WorldActor->GetBlock(SmokeMiningBlock) == EBlockType::Stone,
            TEXT("Resume retains the unfinished block"));
        Player->SelectSlot(2);
        Check(Player->SelectToolSlot(0)
            && Player->GetSelectedToolType() == EBlockRealmToolType::WoodenPickaxe,
            TEXT("Resumed stone mining re-equips the wooden pickaxe"));
        Input(EKeys::LeftMouseButton, true);
        SmokeMiningStartTime = SmokeElapsed;
        SmokeTime = .2f;
        break;
    case 19:
        Check(Player->IsMining() && Player->GetMiningProgress() > 0.f,
            TEXT("Mining restarts with a fresh hold after resume"));
        Shot(TEXT("08_StoneMiningProgress"));
        SmokeTime = FMath::Max(.05f, Player->GetMiningDuration() * (1.f - Player->GetMiningProgress()) + .05f);
        break;
    case 20:
        if (WorldActor->GetBlock(SmokeMiningBlock) != EBlockType::Air && Player->IsMining()
            && SmokeElapsed - SmokeMiningStartTime < 4.f) { SmokeTime = .03f; return false; }
        Input(EKeys::LeftMouseButton, false);
        Check(WorldActor->GetBlock(SmokeMiningBlock) == EBlockType::Air
            && Player->GetInventoryCount(2) == SmokeInventoryCount + 1,
            TEXT("Completing timed stone mining removes one block and adds one inventory item"));
        {
            bool Found = false;
            for (int32 X = -80; X <= 80 && !Found; X += 2)
                for (int32 Y = -80; Y <= 80 && !Found; Y += 2)
                {
                    bool Clear = true;
                    for (int32 DX = 0; DX <= 11 && Clear; ++DX)
                        for (int32 DY = -1; DY <= 1 && Clear; ++DY)
                            Clear &= WorldActor->GetBlock(FIntVector(X + DX, Y + DY, FVoxelData::SeaLevel - 3)) == EBlockType::Water;
                    if (Clear) { SmokeWaterBlock = FIntVector(X, Y, FVoxelData::SeaLevel - 1); Found = true; }
                }
            Check(Found, TEXT("A natural water corridor is available for real swimming checks"));
            if (!Found) return true;
        }
        SmokeWaterPosition = ABlockWorld::BlockToWorld(SmokeWaterBlock);
        Player->SetActorLocation(SmokeWaterPosition, false, nullptr, ETeleportType::TeleportPhysics);
        Movement->StopMovementImmediately();
        PC->SetControlRotation(FRotator::ZeroRotator);
        WorldActor->RequestStreaming(SmokeWaterPosition);
        SmokeTime = 2.f;
        break;
    case 21:
        {
            TArray<AActor*> Chunks;
            UGameplayStatics::GetAllActorsOfClass(this, ABlockChunk::StaticClass(), Chunks);
            bool Ready = false;
            const FIntPoint Destination = FVoxelData::ChunkFromBlock(SmokeWaterBlock);
            for (AActor* Actor : Chunks)
            {
                const ABlockChunk* Chunk = Cast<ABlockChunk>(Actor);
                Ready |= IsValid(Chunk) && Chunk->GetChunkCoordinate() == Destination && Chunk->IsCollisionReady();
            }
            if (!Ready) { SmokeTime = 1.f; return false; }
        }
        Check(Player->IsInWater() && !Player->IsFlying() && Movement->MovementMode == MOVE_Custom,
            TEXT("Immersion uses a water movement mode without enabling flight"));
        Player->SetActorLocation(SmokeWaterPosition, false, nullptr, ETeleportType::TeleportPhysics);
        Movement->StopMovementImmediately();
        SmokeStartPosition = Player->GetActorLocation();
        Input(EKeys::W, true);
        SmokeTime = .95f;
        break;
    case 22:
        SmokeWaterSpeed = Movement->Velocity.Size2D();
        Check(SmokeWaterSpeed > 80.f && SmokeWaterSpeed < SmokeLandSpeed * .7f
            && FVector::Dist2D(Player->GetActorLocation(), SmokeStartPosition) > 80.f,
            TEXT("Real horizontal swimming is distinctly slower than walking"));
        Input(EKeys::W, false);
        SmokeStartPosition = Player->GetActorLocation();
        Input(EKeys::LeftControl, true);
        SmokeTime = .3f;
        break;
    case 23:
        Check(Player->GetActorLocation().Z < SmokeStartPosition.Z - 10.f && !Player->IsFlying(),
            TEXT("Ctrl descends in water without a flight state"));
        Input(EKeys::LeftControl, false);
        SmokeStartPosition = Player->GetActorLocation();
        Input(EKeys::SpaceBar, true);
        SmokeTime = .4f;
        break;
    case 24:
        Check(Player->GetActorLocation().Z > SmokeStartPosition.Z + 10.f && !Player->IsFlying(),
            TEXT("Space swims upward without enabling flight"));
        Input(EKeys::SpaceBar, false);
        SmokeStartPosition = Player->GetActorLocation();
        Shot(TEXT("09_WaterMovement"));
        Key(EKeys::Escape);
        SmokeTime = .15f;
        break;
    case 25:
        Check(bPauseOpen && FVector::Dist(Player->GetActorLocation(), SmokeStartPosition) < 5.f,
            TEXT("Pause freezes custom water movement"));
        Key(EKeys::Escape);
        SmokeTime = .3f;
        break;
    case 26:
    {
        Check(Player->IsInWater() && !Player->IsFlying() && Movement->MovementMode == MOVE_Custom,
            TEXT("Resume restores water movement without latching flight"));
        Player->SelectSlot(2);
        // Record the old underfoot pose before preparing a separate placement fixture.
        // The preceding real swim retains vertical momentum, so aiming straight
        // down can legitimately select a water cell occupied by the capsule.
        PC->SetControlRotation(FRotator(-89.f, 0.f, 0.f));
        Player->RefreshTarget();
        UE_LOG(LogTemp, Display, TEXT("BR_INTERACTION water_place_underfoot %s"),
            *Player->DescribePlacementState(Player->GetPlacementBlock()));

        // Keep the real underwater aim/click and water replacement, with a
        // support ahead inside the verified corridor and ample horizontal
        // capsule clearance. No movement timing or collision rule is bypassed.
        SmokePosition = SmokeWaterBlock + FIntVector(6, 0, -2);
        const FIntVector Support = SmokePosition - FIntVector(0, 0, 1);
        WorldActor->SetBlock(Support, EBlockType::Stone);
        const FVector CellCenter = ABlockWorld::BlockToWorld(SmokePosition);
        const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
        const FVector CapsuleCenter = Capsule->GetComponentLocation();
        const float RequiredHorizontalClearance = 50.f + Capsule->GetScaledCapsuleRadius() + 2.f;
        const bool bClearOfCapsule = FMath::Abs(CellCenter.X - CapsuleCenter.X) > RequiredHorizontalClearance
            || FMath::Abs(CellCenter.Y - CapsuleCenter.Y) > RequiredHorizontalClearance;
        Check(bClearOfCapsule, TEXT("The underwater placement fixture has horizontal capsule clearance"));
        if (!bClearOfCapsule) return true;
        const UCameraComponent* Camera = Player->FindComponentByClass<UCameraComponent>();
        if (!Camera) return true;
        const FVector AimPoint = ABlockWorld::BlockToWorld(Support) + FVector(0.f, 0.f, 49.f);
        PC->SetControlRotation((AimPoint - Camera->GetComponentLocation()).Rotation());
        Player->RefreshTarget();
        const bool bWaterTarget = Player->HasTarget() && Player->GetPlacementBlock() == SmokePosition
            && WorldActor->GetBlock(SmokePosition) == EBlockType::Water;
        UE_LOG(LogTemp, Display, TEXT("BR_INTERACTION water_place_before %s"),
            *Player->DescribePlacementState(SmokePosition));
        Check(bWaterTarget,
            TEXT("Underwater aiming selects the replaceable water cell above the floor"));
        if (!bWaterTarget) return true;
        SmokeInventoryCount = Player->GetInventoryCount(2);
        Key(EKeys::RightMouseButton);
        SmokeTime = .2f;
        break;
    }
    case 27:
        UE_LOG(LogTemp, Display, TEXT("BR_INTERACTION water_place_after expected_count=%d %s"),
            SmokeInventoryCount - 1, *Player->DescribePlacementState(SmokePosition));
        Check(WorldActor->GetBlock(SmokePosition) == EBlockType::Stone
            && Player->GetInventoryCount(2) == SmokeInventoryCount - 1,
            TEXT("A real placement replaces water and consumes exactly one solid block"));
        Key(EKeys::F);
        SmokeTime = .1f;
        break;
    case 28:
        Check(Player->IsFlying(), TEXT("Flight can be deliberately enabled from water"));
        Key(EKeys::F);
        SmokeTime = .2f;
        break;
    case 29:
        Check(!Player->IsFlying() && Player->IsInWater() && Movement->MovementMode == MOVE_Custom,
            TEXT("Disabling flight while immersed returns to water movement"));
        // The shore is a full block above the water surface. Only W and Space
        // may get the pawn onto it; no restore/teleport resets the exit state.
        for (int32 X = 4; X <= 11; ++X)
            for (int32 Y = -1; Y <= 1; ++Y)
                WorldActor->SetBlock(FIntVector(SmokeWaterBlock.X + X, SmokeWaterBlock.Y + Y,
                    FVoxelData::SeaLevel + 1), EBlockType::Sand);
        Player->SetActorLocation(SmokeWaterPosition, false, nullptr, ETeleportType::TeleportPhysics);
        Movement->StopMovementImmediately();
        PC->SetControlRotation(FRotator::ZeroRotator);
        Input(EKeys::W, true);
        Input(EKeys::SpaceBar, true);
        SmokeShoreStartTime = SmokeElapsed;
        SmokeTime = .05f;
        break;
    case 30:
        {
            const FIntVector Cell = ABlockWorld::WorldToBlock(Player->GetActorLocation());
            const bool Ashore = Movement->IsMovingOnGround() && Cell.X >= SmokeWaterBlock.X + 4
                && Cell.X <= SmokeWaterBlock.X + 11
                && Player->GetActorLocation().Z - Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()
                    >= (FVoxelData::SeaLevel + 2) * 100.f - 3.f;
            if (!Ashore && SmokeElapsed - SmokeShoreStartTime < 6.f) { SmokeTime = .05f; return false; }
            Input(EKeys::W, false);
            Input(EKeys::SpaceBar, false);
            Check(Ashore, TEXT("W and Space swim onto a one-block-high shore and land without flight"));
            Shot(TEXT("10_ShoreExit"));
        }
        Check(!Player->IsInWater() && !Player->IsFlying() && Movement->IsMovingOnGround()
            && FMath::IsNearlyEqual(Movement->MaxWalkSpeed, 520.f, 1.f),
            TEXT("Leaving water restores normal walking speed and gravity"));
        UE_LOG(LogTemp, Display, TEXT("BR_INTERACTION land_speed=%.1f water_speed=%.1f stone_seconds=%.2f"),
            SmokeLandSpeed, SmokeWaterSpeed, SmokeMiningDuration);
        return true;
    default:
        return true;
    }
    ++SmokeInteractionStage;
    return false;
}
