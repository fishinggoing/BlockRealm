#include "BlockRealmGameMode.h"
#include "BlockRealmCharacter.h"
#include "BlockRealmHUD.h"
#include "BlockRealmPlayerController.h"
#include "BlockWorld.h"
#include "BlockChunk.h"
#include "BlockRealmDayNight.h"
#include "BlockRealmMob.h"
#include "BlockRealmCreaturePopulation.h"
#include "BlockRealmTorch.h"
#include "BlockRealmCrop.h"
#include "BlockRealmRestPoint.h"
#include "BlockRealmFurnace.h"
#include "BlockRenderSettings.h"
#include "VoxelData.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "EngineUtils.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/ConfigCacheIni.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UnrealClient.h"
#include "Sound/SoundBase.h"
#include "SceneView.h"
#include "RHI.h"
#include "RenderUtils.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
    bool IsSaveableAirCell(const ABlockWorld* BlockWorld, const FIntVector& Cell)
    {
        return IsValid(BlockWorld) && FVoxelData::IsValidPosition(Cell) && Cell.Z > 0
            && BlockWorld->GetBlock(Cell) == EBlockType::Air;
    }

    bool IsSaveableReliableSupport(const ABlockWorld* BlockWorld, const FIntVector& Cell)
    {
        if (!IsValid(BlockWorld) || !FVoxelData::IsValidPosition(Cell)) return false;
        const EBlockType Type = BlockWorld->GetBlock(Cell);
        return FVoxelData::IsSolid(Type) && Type != EBlockType::Leaves && Type != EBlockType::Glass;
    }
}

ABlockRealmGameMode::ABlockRealmGameMode()
{
    PrimaryActorTick.bCanEverTick = true;
    DefaultPawnClass = ABlockRealmCharacter::StaticClass();
    HUDClass = ABlockRealmHUD::StaticClass();
    PlayerControllerClass = ABlockRealmPlayerController::StaticClass();
}

void ABlockRealmGameMode::BeginPlay()
{
    Super::BeginPlay();
    ConfigureLightingPostProcess();
    bSmoke = FParse::Param(FCommandLine::Get(), TEXT("BlockRealmSmoke"));
    FString RoofProbeMaterial;
    bLightingProbe = FParse::Value(FCommandLine::Get(), TEXT("BRRoofLighting="), RoofProbeMaterial)
        || FParse::Param(FCommandLine::Get(), TEXT("BRMouthLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRCaveLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRNightLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRDawnLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRDuskLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRSurfaceLighting"));
    if (bSmoke)
    {
        const bool ExposureProbe=FParse::Param(FCommandLine::Get(),TEXT("BRExposureProbe"));
        for (TActorIterator<APostProcessVolume> It(GetWorld());It;++It)
        {
            if (ExposureProbe) It->Settings.AutoExposureApplyPhysicalCameraExposure=false;
            UE_LOG(LogTemp,Display,TEXT("BR_RENDER Volume=%s unbound=%d Method=%d Physical=%d ISO=%.1f Shutter=%.1f FStop=%.2f Bias=%.2f"),
                *It->GetName(),It->bUnbound,int32(It->Settings.AutoExposureMethod),It->Settings.AutoExposureApplyPhysicalCameraExposure,
                It->Settings.CameraISO,It->Settings.CameraShutterSpeed,It->Settings.DepthOfFieldFstop,It->Settings.AutoExposureBias);
        }
        for (TActorIterator<ADirectionalLight> It(GetWorld());It;++It)
        {
            const UDirectionalLightComponent* Light=Cast<UDirectionalLightComponent>(It->GetLightComponent());
            UE_LOG(LogTemp,Display,TEXT("BR_RENDER Sun=%s Intensity=%.1f Rotation=%s Visible=%d"),*It->GetName(),
                Light?Light->Intensity:0.f,*It->GetActorRotation().ToString(),Light&&Light->IsVisible());
        }
    }
    WorldActor = GetWorld()->SpawnActor<ABlockWorld>();
    DayNightActor = ABlockRealmDayNight::Spawn(GetWorld());
    if (DayNightActor)
    {
        DayNightActor->SetWorld(WorldActor);
        // Start in the same calm morning window used by the authored map. The
        // actor then advances independently, so menus can remain responsive
        // while the world streams in.
        DayNightActor->SetTimeOfDay(0.32f);
        float RequestedDayLength = 0.f;
        if (FParse::Value(FCommandLine::Get(), TEXT("BRDayLength="), RequestedDayLength))
            DayNightActor->SetDayLengthSeconds(RequestedDayLength);
        if (FParse::Param(FCommandLine::Get(), TEXT("BRNight"))) DayNightActor->SetTimeOfDay(0.f);
    }
    if (ABlockRealmCharacter* Player = GetPlayerCharacter())
    {
        Player->SetWorld(WorldActor);
        const FVector Spawn = WorldActor->GetSpawnLocation();
        Player->SetActorLocation(Spawn + FVector(-1800.f,-1800.f,1500.f));
        Player->GetCharacterMovement()->SetMovementMode(MOVE_None);
        if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
            PC->SetControlRotation(FRotator(-30.f,45.f,0.f));
    }
    SetMenuInput(true);
    if (USoundBase* Ambient=LoadObject<USoundBase>(nullptr,TEXT("/Game/BlockRealm/Audio/AmbientValley.AmbientValley")))
        UGameplayStatics::SpawnSound2D(this,Ambient,.32f);
    FBlockRenderSettings::ApplyDisplayDefaults();
    SetUpscaleMode(0);
    Notice = TEXT("正在生成山谷…");
    SmokeTime = 7.f;
}

void ABlockRealmGameMode::ConfigureLightingPostProcess()
{
    UWorld* World = GetWorld();
    if (!World) return;

    // The authored volume already owns the stable exposure policy.  Give Lumen
    // a small, bounded skylight leak so the back side of voxel walls and tree
    // canopies retain shape while a solid roof still reads as a real shadow.
    // This is intentionally a post-process setting rather than a second global
    // light, so it does not add another shadow map or wash out the exterior.
    for (TActorIterator<APostProcessVolume> It(World); It; ++It)
    {
        APostProcessVolume* Volume = *It;
        if (!IsValid(Volume) || Volume->IsActorBeingDestroyed()) continue;
        if (!Volume->bUnbound && !Volume->ActorHasTag(TEXT("BR_StableExposure"))) continue;

        FPostProcessSettings& Settings = Volume->Settings;
        // Full hit lighting is available for a future validated driver profile.
        // The reference driver crashes creating its RTPSO shader collections;
        // the explicit compatibility profile keeps HWRT and local VSM lighting.
        Settings.bOverride_LumenRayLightingMode = true;
        const IConsoleVariable* HitLightingAllowed = IConsoleManager::Get().FindConsoleVariable(
            TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed"));
        Settings.LumenRayLightingMode = HitLightingAllowed && HitLightingAllowed->GetInt() != 0
            ? ELumenRayLightingModeOverride::HitLighting : ELumenRayLightingModeOverride::SurfaceCache;
        Settings.bOverride_LumenSkylightLeaking = true;
        Settings.LumenSkylightLeaking = 0.010f;
        Settings.bOverride_LumenSkylightLeakingTint = true;
        Settings.LumenSkylightLeakingTint = FLinearColor(0.72f, 0.82f, 1.0f);
        Settings.bOverride_LumenFullSkylightLeakingDistance = true;
        Settings.LumenFullSkylightLeakingDistance = 2400.0f;
        UE_LOG(LogTemp, Display, TEXT("BR_LIGHTING skylight leak volume=%s amount=%.3f distance=%.0f"),
            *Volume->GetName(), Settings.LumenSkylightLeaking,
            Settings.LumenFullSkylightLeakingDistance);
        return;
    }
    UE_LOG(LogTemp, Warning, TEXT("BR_LIGHTING could not find an unbound stable exposure volume"));
}

ABlockRealmCharacter* ABlockRealmGameMode::GetPlayerCharacter() const
{
    return Cast<ABlockRealmCharacter>(UGameplayStatics::GetPlayerPawn(this,0));
}

void ABlockRealmGameMode::SetMenuInput(bool bMenu)
{
    if (APlayerController* PC = UGameplayStatics::GetPlayerController(this,0))
    {
        PC->bShowMouseCursor = bMenu;
        PC->bEnableClickEvents = bMenu;
        PC->bEnableMouseOverEvents = bMenu;
        if (bMenu)
        {
            FInputModeGameAndUI Mode;
            Mode.SetHideCursorDuringCapture(false);
            Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
            PC->SetInputMode(Mode);
        }
        else PC->SetInputMode(FInputModeGameOnly());
        PC->ResetIgnoreMoveInput();
        PC->ResetIgnoreLookInput();
        PC->SetIgnoreMoveInput(bMenu);
        PC->SetIgnoreLookInput(bMenu);
    }
    if (ABlockRealmCharacter* Player = GetPlayerCharacter()) Player->CancelGameplayInput();
}

void ABlockRealmGameMode::StartAdventure()
{
    if (!WorldActor || !WorldActor->IsInitialTerrainReady())
    {
        Notice = TEXT("地形正在准备，请稍候…");
        return;
    }
    if (!bStarted && !bStartPrepared)
    {
        const bool ExistingSave=IFileManager::Get().FileExists(*WorldActor->GetSavePath());
        bRestoreOnStart = WorldActor->LoadWorld(PreparedState);
        if (ExistingSave && !bRestoreOnStart)
        {
            Notice=TEXT("存档无法读取，原文件已保留。");
            return;
        }
        bStartPrepared = true;
        if (!WorldActor->IsInitialTerrainReady())
        {
            bPendingStart = true;
            Notice = TEXT("正在恢复世界…");
            return;
        }
    }
    if (ABlockRealmCharacter* Player = GetPlayerCharacter())
    {
        if (!bStarted)
        {
            bHasRespawnPoint = false;
            RespawnPoint = FIntVector::ZeroValue;
            Player->ResetGameplayState();
            if (bRestoreOnStart) Player->RestoreSaveState(PreparedState);
            else
            {
                Player->SetActorLocation(WorldActor->GetSpawnLocation());
                if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
                    PC->SetControlRotation(FRotator(-8.f,35.f,0.f));
            }
            if (DayNightActor && bRestoreOnStart && PreparedState.bHasSurvivalState)
                DayNightActor->SetTimeOfDay(PreparedState.TimeOfDay);
            if (bRestoreOnStart)
            {
                const FIntVector Neighbors[] = {FIntVector(0, 0, -1), FIntVector(1, 0, 0),
                    FIntVector(-1, 0, 0), FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1)};
                for (int32 Index = 0; Index < PreparedState.PlacedTorches.Num(); ++Index)
                {
                    const FIntVector& Cell = PreparedState.PlacedTorches[Index];
                    if (PreparedState.PlacedTorchSupports.IsValidIndex(Index))
                    {
                        ABlockRealmTorch::TryPlace(GetWorld(), WorldActor, Cell, PreparedState.PlacedTorchSupports[Index]);
                        continue;
                    }
                    for (const FIntVector& Offset : Neighbors)
                    {
                        if (ABlockRealmTorch::TryPlace(GetWorld(), WorldActor, Cell, Cell + Offset)) break;
                    }
                }
                for (const FBlockRealmSavedRestPoint& SavedBed : PreparedState.RestPoints)
                {
                    ABlockRealmRestPoint* Bed = ABlockRealmRestPoint::TryPlace(
                        GetWorld(), WorldActor, SavedBed.Cell, SavedBed.Heading);
                    if (IsValid(Bed) && !Bed->IsActorBeingDestroyed() && PreparedState.bHasRespawnPoint
                        && SavedBed.Cell == PreparedState.RespawnPoint)
                    {
                        bHasRespawnPoint = true;
                        RespawnPoint = SavedBed.Cell;
                    }
                }
                for (const FBlockRealmSavedFurnace& SavedFurnace : PreparedState.Furnaces)
                {
                    ABlockRealmFurnace::TryPlace(GetWorld(), WorldActor, SavedFurnace.Cell,
                        SavedFurnace.InputSand, SavedFurnace.FuelWood, SavedFurnace.OutputGlass,
                        SavedFurnace.CookProgress);
                }
                for (const FBlockRealmSavedCrop& SavedCrop : PreparedState.Crops)
                {
                    if (!FVoxelData::IsValidPosition(SavedCrop.Cell) || SavedCrop.Cell.Z <= 0) continue;
                    ABlockRealmCrop::TryPlant(GetWorld(), WorldActor,
                        SavedCrop.Cell - FIntVector(0, 0, 1), SavedCrop.GrowthSeconds);
                }
            }
        }
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    }
    bStarted = true;
    bPendingStart = false;
    bTitleOpen = bPauseOpen = false;
    SetMenuInput(false);
    if (!bSmoke && !bLightingProbe && !IsValid(CreaturePopulation))
    {
        CreaturePopulation = GetWorld()->SpawnActor<ABlockRealmCreaturePopulation>();
        if (CreaturePopulation) CreaturePopulation->Initialize(WorldActor, GetPlayerCharacter(), DayNightActor);
    }
    Notice = TEXT("欢迎来到方境 · F 切换飞行，F5 保存");
}

void ABlockRealmGameMode::TogglePauseMenu()
{
    if (bTitleOpen) return;
    bPauseOpen = !bPauseOpen;
    SetMenuInput(bPauseOpen);
    if (ABlockRealmCharacter* Player = GetPlayerCharacter())
    {
        UCharacterMovementComponent* Movement = Player->GetCharacterMovement();
        Movement->StopMovementImmediately();
        if (bPauseOpen)
        {
            PausedMovementMode = static_cast<uint8>(Movement->MovementMode);
            PausedCustomMode = Movement->CustomMovementMode;
            Movement->SetMovementMode(MOVE_None);
        }
        else Movement->SetMovementMode(static_cast<EMovementMode>(PausedMovementMode),PausedCustomMode);
    }
}

bool ABlockRealmGameMode::SaveAdventure()
{
    if (!IsInGameThread()) return false;
    ABlockRealmCharacter* Player = GetPlayerCharacter();
    UWorld* World = GetWorld();
    if (!bStarted || !IsValid(Player) || !IsValid(World) || World->bIsTearingDown
        || !IsValid(WorldActor) || WorldActor->IsActorBeingDestroyed()
        || WorldActor->GetWorld() != World) return false;
    FWorldSaveState State = Player->CaptureSaveState();
    State.TimeOfDay = GetTimeOfDay();
    State.PlacedTorches.Reset();
    State.PlacedTorchSupports.Reset();
    State.RestPoints.Reset();
    State.Furnaces.Reset();
    State.Crops.Reset();
    TSet<FIntVector> OccupiedCells;

    // Recheck authoritative support/air now; a placement may still await its next cleanup tick.
    // Claim cells across every object kind so stale overlaps cannot invalidate the whole save.
    for (TActorIterator<ABlockRealmTorch> It(World); It; ++It)
    {
        const ABlockRealmTorch* Torch = *It;
        if (!IsValid(Torch) || Torch->IsActorBeingDestroyed() || Torch->GetBlockWorld() != WorldActor) continue;
        const FIntVector Cell = Torch->GetBlockPosition();
        const FIntVector Support = Torch->GetSupportBlock();
        if (!IsSaveableAirCell(WorldActor, Cell) || !FVoxelData::IsValidPosition(Support)
            || !FVoxelData::IsSolid(WorldActor->GetBlock(Support)) || OccupiedCells.Contains(Cell)) continue;
        const FIntVector Offset = Cell - Support;
        if (FMath::Abs(Offset.X) + FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z) != 1) continue;
        if (State.PlacedTorches.Num() >= FVoxelData::MaxSavedTorches) break;
        OccupiedCells.Add(Cell);
        State.PlacedTorches.Add(Cell);
        State.PlacedTorchSupports.Add(Support);
    }
    for (TActorIterator<ABlockRealmRestPoint> It(World); It; ++It)
    {
        const ABlockRealmRestPoint* Bed = *It;
        if (!IsValid(Bed) || Bed->IsActorBeingDestroyed() || Bed->GetBlockWorld() != WorldActor) continue;
        const FIntVector Cell = Bed->GetBlockPosition();
        const int32 Heading = Bed->GetHeading();
        if (!IsSaveableAirCell(WorldActor, Cell) || Heading < 0 || Heading > 3) continue;
        const FIntVector SecondCell = Bed->GetSecondBlockPosition();
        if (!IsSaveableAirCell(WorldActor, SecondCell)
            || !IsSaveableReliableSupport(WorldActor, Cell - FIntVector(0, 0, 1))
            || !IsSaveableReliableSupport(WorldActor, SecondCell - FIntVector(0, 0, 1))
            || OccupiedCells.Contains(Cell) || OccupiedCells.Contains(SecondCell)) continue;
        if (State.RestPoints.Num() >= FVoxelData::MaxSavedRestPoints) break;
        FBlockRealmSavedRestPoint SavedBed;
        SavedBed.Cell = Cell;
        SavedBed.Heading = Heading;
        OccupiedCells.Add(Cell);
        OccupiedCells.Add(SecondCell);
        State.RestPoints.Add(SavedBed);
    }
    for (TActorIterator<ABlockRealmFurnace> It(World); It; ++It)
    {
        const ABlockRealmFurnace* Furnace = *It;
        if (!IsValid(Furnace) || Furnace->IsActorBeingDestroyed() || Furnace->GetBlockWorld() != WorldActor) continue;
        const FIntVector Cell = Furnace->GetBlockPosition();
        if (!IsSaveableAirCell(WorldActor, Cell)
            || !IsSaveableReliableSupport(WorldActor, Cell - FIntVector(0, 0, 1))
            || OccupiedCells.Contains(Cell)) continue;
        FBlockRealmSavedFurnace SavedFurnace;
        SavedFurnace.Cell = Cell;
        SavedFurnace.InputSand = Furnace->GetInputSand();
        SavedFurnace.FuelWood = Furnace->GetFuelWood();
        SavedFurnace.OutputGlass = Furnace->GetOutputGlass();
        SavedFurnace.CookProgress = Furnace->GetCookProgress();
        const int32 Capacity = FVoxelData::MaxSavedFurnaceStack;
        if (SavedFurnace.InputSand < 0 || SavedFurnace.InputSand > Capacity
            || SavedFurnace.FuelWood < 0 || SavedFurnace.FuelWood > Capacity
            || SavedFurnace.OutputGlass < 0 || SavedFurnace.OutputGlass > Capacity
            || !FMath::IsFinite(SavedFurnace.CookProgress) || SavedFurnace.CookProgress < 0.f
            || SavedFurnace.CookProgress >= FVoxelData::FurnaceCookSeconds
            || (SavedFurnace.CookProgress > 0.f && (SavedFurnace.InputSand == 0
                || SavedFurnace.FuelWood == 0 || SavedFurnace.OutputGlass == Capacity))) continue;
        if (State.Furnaces.Num() >= FVoxelData::MaxSavedFurnaces) break;
        OccupiedCells.Add(Cell);
        State.Furnaces.Add(SavedFurnace);
    }
    for (TActorIterator<ABlockRealmCrop> It(World); It; ++It)
    {
        const ABlockRealmCrop* Crop = *It;
        if (!IsValid(Crop) || Crop->IsActorBeingDestroyed() || Crop->IsHarvested()
            || Crop->GetBlockWorld() != WorldActor) continue;
        const FIntVector Cell = Crop->GetBlockPosition();
        const float GrowthSeconds = Crop->GetGrowthSeconds();
        if (!IsSaveableAirCell(WorldActor, Cell) || Crop->GetSupportBlock() != Cell - FIntVector(0, 0, 1)
            || !FMath::IsFinite(GrowthSeconds) || GrowthSeconds < 0.f
            || GrowthSeconds > FVoxelData::MaxCropGrowthSeconds || OccupiedCells.Contains(Cell)) continue;
        const EBlockType Soil = WorldActor->GetBlock(Crop->GetSupportBlock());
        if (Soil != EBlockType::Grass && Soil != EBlockType::Dirt) continue;
        if (State.Crops.Num() >= FVoxelData::MaxSavedCrops) break;
        FBlockRealmSavedCrop SavedCrop;
        SavedCrop.Cell = Cell;
        SavedCrop.GrowthSeconds = GrowthSeconds;
        OccupiedCells.Add(Cell);
        State.Crops.Add(SavedCrop);
    }
    const bool bRuntimeHasRespawnPoint = bHasRespawnPoint && State.RestPoints.ContainsByPredicate(
        [this](const FBlockRealmSavedRestPoint& SavedBed) { return SavedBed.Cell == RespawnPoint; });
    State.bHasRespawnPoint = bRuntimeHasRespawnPoint;
    State.RespawnPoint = bRuntimeHasRespawnPoint ? RespawnPoint : FIntVector::ZeroValue;
    const bool bSaved = WorldActor->SaveWorld(State);
    // Keep the live anchor intact when a disk write fails.  A failed save must
    // not silently change the player's next death location.
    if (bSaved && !State.bHasRespawnPoint)
    {
        bHasRespawnPoint = false;
        RespawnPoint = FIntVector::ZeroValue;
    }
    Notice = bSaved ? TEXT("世界已保存") : TEXT("保存失败，当前进度已保留，请检查磁盘空间后重试");
    return bSaved;
}

void ABlockRealmGameMode::ReturnToTitle()
{
    if (!SaveAdventure()) return;
    if (IsValid(CreaturePopulation))
    {
        CreaturePopulation->ClearPopulation();
        CreaturePopulation->Destroy();
        CreaturePopulation = nullptr;
    }
    for (TObjectPtr<ABlockRealmMob>& Mob : ActiveMobs)
    {
        if (IsValid(Mob)) Mob->Destroy();
    }
    ActiveMobs.Empty();
    for (TActorIterator<AActor> It(GetWorld()); It; ++It)
    {
        if (!IsValid(*It) || It->IsActorBeingDestroyed()) continue;
        if (It->IsA<ABlockRealmMob>() || It->ActorHasTag(TEXT("BlockRealmMob"))
            || It->ActorHasTag(TEXT("BlockRealmDrop")) || It->ActorHasTag(TEXT("BlockRealmTorch"))
            || It->ActorHasTag(TEXT("BlockRealmTorchDrop")) || It->ActorHasTag(TEXT("BlockRealmCrop"))
            || It->ActorHasTag(TEXT("BlockRealmRestPoint")) || It->ActorHasTag(TEXT("BlockRealmFurnace"))
            || It->ActorHasTag(TEXT("BlockRealmFoodDrop"))) It->Destroy();
    }
    bHasRespawnPoint = false;
    RespawnPoint = FIntVector::ZeroValue;
    NightMobSpawnAccumulator = 0.0f;
    bTitleOpen = true;
    bPauseOpen = false;
    bStarted = false;
    bStartPrepared = bRestoreOnStart = bPendingStart = false;
    SetMenuInput(true);
    if (ABlockRealmCharacter* Player = GetPlayerCharacter())
    {
        Player->ResetGameplayState();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_None);
        Player->SetActorLocation(WorldActor->GetSpawnLocation()+FVector(-1800.f,-1800.f,1500.f));
        if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
            PC->SetControlRotation(FRotator(-30.f,45.f,0.f));
    }
}

void ABlockRealmGameMode::QuitAdventure()
{
    if (bStarted && !SaveAdventure()) return;
    UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this,0), EQuitPreference::Quit, false);
}

void ABlockRealmGameMode::SetUpscaleMode(int32 Mode)
{
    UpscaleMode = FMath::Clamp(Mode,0,3);
    if (UpscaleMode && !FBlockRenderSettings::IsDLSSSupported()) UpscaleMode = 0;
    FBlockRenderSettings::ApplyMode(UpscaleMode);
}

FString ABlockRealmGameMode::GetGraphicsStatus() const
{
    return FBlockRenderSettings::GetStatus(UpscaleMode);
}

void ABlockRealmGameMode::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bTitleOpen && Notice.Contains(TEXT("正在")) && WorldActor && WorldActor->IsInitialTerrainReady())
        Notice = TEXT("山谷已就绪 · 开始探索");
    if (bPendingStart && WorldActor && WorldActor->IsInitialTerrainReady()) StartAdventure();
    if (bLightingProbe) TickLightingProbe(DeltaSeconds);
    else
    {
        if (bSmoke) TickNightMobs(DeltaSeconds);
        if (bSmoke) TickSmoke(DeltaSeconds);
    }
}

bool ABlockRealmGameMode::IsNight() const
{
    return DayNightActor && DayNightActor->IsNight();
}

float ABlockRealmGameMode::GetTimeOfDay() const
{
    return DayNightActor ? DayNightActor->GetTimeOfDay() : 0.32f;
}

int32 ABlockRealmGameMode::GetActiveMobCount() const
{
    int32 Count = 0;
    for (TActorIterator<ABlockRealmMob> It(GetWorld()); It; ++It)
    {
        const ABlockRealmMob* Mob = *It;
        if (IsValid(Mob) && !Mob->IsActorBeingDestroyed() && !Mob->IsDead() && Mob->IsHostile()) ++Count;
    }
    return Count;
}

int32 ABlockRealmGameMode::GetActiveAnimalCount() const
{
    int32 Count = 0;
    for (TActorIterator<ABlockRealmMob> It(GetWorld()); It; ++It)
    {
        const ABlockRealmMob* Mob = *It;
        if (IsValid(Mob) && !Mob->IsActorBeingDestroyed() && !Mob->IsDead() && !Mob->IsHostile()) ++Count;
    }
    return Count;
}

bool ABlockRealmGameMode::TryRestAt(ABlockRealmRestPoint* RestPoint)
{
    if (!IsInGameThread()) return false;
    ABlockRealmCharacter* Player = GetPlayerCharacter();
    UWorld* World = GetWorld();
    if (!bStarted || !IsAdventureActive() || !IsValid(World) || World->bIsTearingDown
        || !IsValid(WorldActor) || WorldActor->IsActorBeingDestroyed() || WorldActor->GetWorld() != World
        || !IsValid(Player) || Player->IsActorBeingDestroyed() || Player->GetBlockWorld() != WorldActor
        || Player->GetHealth() <= 0.f || !Player->CanUsePlayerActions()) return false;

    auto ShowRestNotice = [this, Player](const TCHAR* Message)
    {
        Notice = Message;
        Player->NotifyPlayerAction(Notice);
    };
    if (!IsValid(RestPoint) || RestPoint->IsActorBeingDestroyed() || RestPoint->IsHidden()
        || RestPoint->GetWorld() != World || RestPoint->GetBlockWorld() != WorldActor)
    {
        ShowRestNotice(TEXT("这张床已失效，无法设置重生点。"));
        return false;
    }
    const FVector PlayerLocation = Player->GetActorLocation();
    const FVector BedLocation = RestPoint->GetActorLocation();
    const double DistanceSquared = FVector::DistSquared(PlayerLocation, BedLocation);
    if (PlayerLocation.ContainsNaN() || BedLocation.ContainsNaN() || !FMath::IsFinite(DistanceSquared)
        || DistanceSquared > FMath::Square(600.f))
    {
        ShowRestNotice(TEXT("离床太远，靠近后再休息。"));
        return false;
    }
    FVector SafeRespawn;
    if (!RestPoint->FindSafeRespawn(SafeRespawn))
    {
        ShowRestNotice(TEXT("床边没有安全站立空间，暂时无法设置重生点。"));
        return false;
    }
    // Query the world, including experimental creatures absent from ActiveMobs.
    // Both the player and the bed must have a quiet 8m area before sleeping or selecting a camp.
    for (TActorIterator<ABlockRealmMob> It(World); It; ++It)
    {
        const ABlockRealmMob* Mob = *It;
        if (!IsValid(Mob) || Mob->IsActorBeingDestroyed() || Mob->IsDead() || !Mob->IsHostile() || Mob->GetHealth() <= 0.f) continue;
        const FVector MobLocation = Mob->GetActorLocation();
        if (MobLocation.ContainsNaN()) continue;
        if (FVector::DistSquared(MobLocation, PlayerLocation) <= FMath::Square(800.f)
            || FVector::DistSquared(MobLocation, BedLocation) <= FMath::Square(800.f))
        {
            ShowRestNotice(TEXT("附近有敌对生物，暂时无法休息或设置重生点。"));
            return false;
        }
    }
    bHasRespawnPoint = true;
    RespawnPoint = RestPoint->GetBlockPosition();
    if (IsValid(DayNightActor) && !DayNightActor->IsActorBeingDestroyed() && DayNightActor->IsNight())
    {
        DayNightActor->SetTimeOfDay(.32f);
        Player->RestoreSurvivalHealth(6.f);
        NightMobSpawnAccumulator = 0.f;
        ShowRestNotice(TEXT("已睡至清晨，恢复少量生命；营地重生点已设置。"));
    }
    else
    {
        ShowRestNotice(TEXT("营地重生点已设置，白天无需睡眠。"));
    }
    return true;
}

FVector ABlockRealmGameMode::GetAdventureRespawnLocation() const
{
    if (!IsValid(WorldActor) || WorldActor->IsActorBeingDestroyed())
    {
        const ABlockRealmCharacter* Player = GetPlayerCharacter();
        return IsValid(Player) && !Player->GetActorLocation().ContainsNaN()
            ? Player->GetActorLocation() : FVector::ZeroVector;
    }
    UWorld* World = GetWorld();
    if (bHasRespawnPoint && IsValid(World) && !World->bIsTearingDown)
    {
        for (TActorIterator<ABlockRealmRestPoint> It(World); It; ++It)
        {
            const ABlockRealmRestPoint* Bed = *It;
            if (!IsValid(Bed) || Bed->IsActorBeingDestroyed() || Bed->GetBlockWorld() != WorldActor
                || Bed->GetBlockPosition() != RespawnPoint) continue;
            FVector SafeRespawn;
            if (Bed->FindSafeRespawn(SafeRespawn)) return SafeRespawn;
        }
    }
    return WorldActor->GetSpawnLocation();
}

void ABlockRealmGameMode::TickNightMobs(float DeltaSeconds)
{
    for (int32 Index = ActiveMobs.Num() - 1; Index >= 0; --Index)
    {
        if (!IsValid(ActiveMobs[Index]) || ActiveMobs[Index]->IsDead())
        {
            ActiveMobs.RemoveAtSwap(Index);
        }
    }
    const ABlockRealmCharacter* ActivePlayer = GetPlayerCharacter();
    if (!bStarted || !IsAdventureActive() || (ActivePlayer && ActivePlayer->IsInventoryOpen())) return;
    if (!DayNightActor || !DayNightActor->IsNight() || !WorldActor)
    {
        NightMobSpawnAccumulator = 0.0f;
        return;
    }

    NightMobSpawnAccumulator += FMath::Max(DeltaSeconds, 0.0f);
    // Keep the first night readable and bounded: one creature appears after a
    // short delay, then at most one more every 45 seconds, capped at three.
    const float SpawnInterval = GetActiveMobCount() == 0 ? 12.f : 45.f;
    if (NightMobSpawnAccumulator < SpawnInterval || GetActiveMobCount() >= 3)
    {
        return;
    }
    NightMobSpawnAccumulator = 0.0f;

    ABlockRealmCharacter* Player = GetPlayerCharacter();
    if (!Player || !GetWorld()) return;

    const FVector PlayerLocation = Player->GetActorLocation();
    const float Angle = static_cast<float>(ActiveMobs.Num()) * 2.15f
        + static_cast<float>(FPlatformTime::Seconds() * 0.23);
    const FVector Offset(FMath::Cos(Angle) * 900.0f, FMath::Sin(Angle) * 900.0f, 0.0f);
    const FVector TraceTop(PlayerLocation.X + Offset.X, PlayerLocation.Y + Offset.Y, 7000.0f);
    const FVector TraceBottom = TraceTop - FVector(0.0f, 0.0f, 9000.0f);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmNightMobSpawn), false, Player);
    for (const TObjectPtr<ABlockRealmMob>& Existing : ActiveMobs)
    {
        if (IsValid(Existing)) Query.AddIgnoredActor(Existing);
    }
    FHitResult GroundHit;
    if (!GetWorld()->LineTraceSingleByChannel(GroundHit, TraceTop, TraceBottom, ECC_Visibility, Query))
    {
        return;
    }
    if (!Cast<ABlockChunk>(GroundHit.GetActor()))
    {
        return;
    }

    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
    const FVector SpawnLocation = GroundHit.ImpactPoint + FVector(0.0f, 0.0f, 95.0f);
    for (TActorIterator<ABlockRealmTorch> It(GetWorld()); It; ++It)
        if (FVector::DistSquared(It->GetActorLocation(), SpawnLocation) < FMath::Square(600.f)) return;
    ABlockRealmMob* Mob = GetWorld()->SpawnActor<ABlockRealmMob>(ABlockRealmMob::StaticClass(),
        SpawnLocation, FRotator::ZeroRotator, Parameters);
    if (!Mob) return;
    Mob->SetWorld(WorldActor);
    Mob->SetTarget(Player);
    ActiveMobs.Add(Mob);
    Notice = TEXT("夜色降临：敌对生物已出现，保持距离或用工具反击。");
}

void ABlockRealmGameMode::TickLightingProbe(float DeltaSeconds)
{
    LightingProbeElapsed += FMath::Clamp(DeltaSeconds, 0.0f, 0.25f);
    if (bLightingProbeShot)
    {
        if (LightingProbeElapsed >= 1.0f)
        {
            UKismetSystemLibrary::QuitGame(this, UGameplayStatics::GetPlayerController(this, 0),
                EQuitPreference::Quit, false);
        }
        return;
    }
    if (!WorldActor || !WorldActor->IsInitialTerrainReady()) return;
    if (!bStarted)
    {
        StartAdventure();
        return;
    }

    ABlockRealmCharacter* Player = GetPlayerCharacter();
    if (!Player) return;
    FString RoofMaterial;
    const bool bRoof = FParse::Value(FCommandLine::Get(), TEXT("BRRoofLighting="), RoofMaterial);
    const bool bMouth = FParse::Param(FCommandLine::Get(), TEXT("BRMouthLighting"));
    const bool bNight = FParse::Param(FCommandLine::Get(), TEXT("BRNightLighting"));
    const bool bDawn = FParse::Param(FCommandLine::Get(), TEXT("BRDawnLighting"));
    const bool bDusk = FParse::Param(FCommandLine::Get(), TEXT("BRDuskLighting"));
    const bool bSurface = bNight || bDawn || bDusk || FParse::Param(FCommandLine::Get(), TEXT("BRSurfaceLighting"));
    if (!bLightingProbeTeleported)
    {
        if (DayNightActor)
        {
            DayNightActor->SetAutoAdvance(false);
            DayNightActor->SetTimeOfDay(bDawn ? .25f : bDusk ? .75f : bNight ? 0.f : .32f);
        }
        bool bFound = false;
        // Locate a chamber with a solid floor and three consecutive air cells,
        // so the diagnostic exercises a walkable cave rather than a one-block
        // noise hole.  The scan is deterministic and runs only once.
        if (bRoof || bMouth)
        {
            // Construct comparison rooms only in a diagnostic save. A normal
            // world is never edited by a rendering probe.
            const FString SavePath = WorldActor->GetSavePath();
            if (!SavePath.Contains(TEXT("ValidationWorld")) && !SavePath.Contains(TEXT("LightingProbeWorld")))
            {
                LightingProbeError = TEXT("Lighting fixture refused a non-isolated save path");
            }
            EBlockType RoofType = EBlockType::Stone;
            if (bRoof)
            {
                if (RoofMaterial.Equals(TEXT("Wood"), ESearchCase::IgnoreCase)) RoofType = EBlockType::Wood;
                else if (RoofMaterial.Equals(TEXT("Stone"), ESearchCase::IgnoreCase)) RoofType = EBlockType::Stone;
                else if (RoofMaterial.Equals(TEXT("Glass"), ESearchCase::IgnoreCase)) RoofType = EBlockType::Glass;
                else if (RoofMaterial.Equals(TEXT("Leaves"), ESearchCase::IgnoreCase)) RoofType = EBlockType::Leaves;
                else LightingProbeError = TEXT("Unknown roof fixture material");
            }
            if (LightingProbeError.IsEmpty())
            {
                FIntVector Floor = ABlockWorld::WorldToBlock(WorldActor->GetSpawnLocation());
                Floor.Z = FMath::Clamp(Floor.Z + 6, 8, FVoxelData::WorldHeight - 10);
                auto Write = [this](const FIntVector& Cell, EBlockType Type)
                {
                    return WorldActor->GetBlock(Cell) == Type || WorldActor->SetBlock(Cell, Type);
                };
                bool bBuilt = true;
                for (int32 X = -7; X <= 7; ++X)
                {
                    for (int32 Y = -7; Y <= 7; ++Y)
                    {
                        for (int32 Z = 1; Z <= 7; ++Z)
                            bBuilt &= Write(Floor + FIntVector(X, Y, Z), EBlockType::Air);
                        bBuilt &= Write(Floor + FIntVector(X, Y, 0), EBlockType::Stone);
                        const bool bWall = bMouth ? (X >= -5 && X <= 0 && (FMath::Abs(Y) == 5 || X == -5))
                            : (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) == 5);
                        const bool bDoor = !bMouth && X == 5 && FMath::Abs(Y) <= 1;
                        if (bWall)
                            for (int32 Z = 1; Z <= 4; ++Z)
                                if (!bDoor || Z == 4) bBuilt &= Write(Floor + FIntVector(X, Y, Z), EBlockType::Stone);
                        if (FMath::Abs(Y) <= 5 && X >= -5 && X <= (bMouth ? 0 : 5))
                            bBuilt &= Write(Floor + FIntVector(X, Y, 5), RoofType);
                    }
                }
                LightingProbeCell = Floor + FIntVector(0, 0, 1);
                bLightingProbeFixtureReady = bBuilt;
                bFound = bBuilt;
                if (!bBuilt) LightingProbeError = TEXT("Could not build every isolated lighting fixture cell");
            }
        }
        else if (bSurface)
        {
            LightingProbeCell = ABlockWorld::WorldToBlock(WorldActor->GetSpawnLocation());
            bFound = true;
        }
        for (int32 Y = -128; Y <= 128 && !bFound && !bRoof && !bMouth; ++Y)
        {
            for (int32 X = -128; X <= 128 && !bFound; ++X)
            {
                for (int32 Z = FVoxelData::CaveMinZ + 1; Z <= 30 && !bFound; ++Z)
                {
                    const FIntVector Candidate(X, Y, Z);
                    if (!FVoxelData::IsCaveCell(Candidate, FVoxelData::DefaultSeed)
                        || !FVoxelData::IsSolid(WorldActor->GetBlock(Candidate - FIntVector(0, 0, 1))))
                    {
                        continue;
                    }
                    bool bClear = true;
                    for (int32 Head = 1; Head <= 2; ++Head)
                    {
                        if (WorldActor->GetBlock(Candidate + FIntVector(0, 0, Head)) != EBlockType::Air)
                        {
                            bClear = false;
                            break;
                        }
                    }
                    if (bClear)
                    {
                        LightingProbeCell = Candidate;
                        bFound = true;
                    }
                }
            }
        }
        if (!bFound)
        {
            UE_LOG(LogTemp, Error, TEXT("BR_LIGHTING probe setup failed: %s"),
                LightingProbeError.IsEmpty() ? TEXT("no walkable cave chamber") : *LightingProbeError);
            bLightingProbeShot = true;
            LightingProbeElapsed = 1.0f;
            return;
        }
        const FVector CaveLocation = bSurface ? WorldActor->GetSpawnLocation()
            : WorldActor->BlockToWorld(LightingProbeCell) + FVector(0.0f, 0.0f, 46.0f);
        Player->SetActorLocation(CaveLocation, false, nullptr, ETeleportType::TeleportPhysics);
        Player->GetCharacterMovement()->StopMovementImmediately();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        float Yaw = bMouth || bRoof ? 0.f : 35.f;
        float Pitch = -8.f;
        FParse::Value(FCommandLine::Get(), TEXT("BRLightingYaw="), Yaw);
        FParse::Value(FCommandLine::Get(), TEXT("BRLightingPitch="), Pitch);
        if (!FMath::IsFinite(Yaw)) Yaw = 35.f;
        if (!FMath::IsFinite(Pitch)) Pitch = -8.f;
        LightingProbeRotation = FRotator(FMath::Clamp(Pitch, -85.f, 85.f), Yaw, 0.0f).GetNormalized();
        if (APlayerController* PC = Cast<APlayerController>(Player->GetController()))
        {
            // Only unattended lighting probes own the camera. Ignore APIs stack,
            // so enter the input lock once, after StartAdventure enables play.
            PC->SetIgnoreLookInput(true);
            PC->SetIgnoreMoveInput(true);
            PC->RotationInput = FRotator::ZeroRotator;
            PC->SetControlRotation(LightingProbeRotation);
        }
        WorldActor->RequestStreaming(CaveLocation);
        bLightingProbeTeleported = true;
        LightingProbeElapsed = 0.0f;
        return;
    }

    if (APlayerController* ProbeController = Cast<APlayerController>(Player->GetController()))
    {
        ProbeController->RotationInput = FRotator::ZeroRotator;
        ProbeController->SetControlRotation(LightingProbeRotation);
    }
    WorldActor->RequestStreaming(Player->GetActorLocation());
    if (WorldActor->GetPendingChunkCount() > 0)
    {
        LightingProbeElapsed = 0.f;
        LightingProbeFrameMs.Reset();
        return;
    }
    if (LightingProbeElapsed >= 1.f && FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.f)
        LightingProbeFrameMs.Add(DeltaSeconds * 1000.f);
    if (LightingProbeElapsed < 4.0f) return;
    FString Directory;
    if (!FParse::Value(FCommandLine::Get(), TEXT("BRValidationDir="), Directory))
        Directory = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Validation"), TEXT("CaveLighting"));
    IFileManager::Get().MakeDirectory(*Directory, true);
    const FString Scene = bRoof ? TEXT("roof_") + RoofMaterial.ToLower() : bMouth ? TEXT("mouth")
        : bDawn ? TEXT("dawn") : bDusk ? TEXT("dusk") : bNight ? TEXT("night") : bSurface ? TEXT("surface") : TEXT("cave");
    const FString ScreenshotPath = FPaths::Combine(Directory, bRoof ? TEXT("RoofLighting.png") : bMouth ? TEXT("MouthLighting.png") : bSurface
        ? (bDawn ? TEXT("DawnLighting.png") : bDusk ? TEXT("DuskLighting.png") : bNight ? TEXT("NightLighting.png") : TEXT("SurfaceLighting.png")) : TEXT("UndergroundLighting.png"));
    FScreenshotRequest::RequestScreenshot(ScreenshotPath, true, false);
    UPointLightComponent* CaveLight = nullptr;
    UPointLightComponent* NightLight = nullptr;
    TArray<UPointLightComponent*> PlayerLights;
    Player->GetComponents(PlayerLights);
    for (UPointLightComponent* Light : PlayerLights)
    {
        if (Light && Light->GetFName() == FName(TEXT("CaveFillLight"))) CaveLight = Light;
        if (Light && Light->GetFName() == FName(TEXT("OutdoorNightFillLight"))) NightLight = Light;
    }
    auto CVarInt = [](const TCHAR* Name)
    {
        const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
        return Variable ? Variable->GetInt() : -1;
    };
    // Read the blended view, rather than assuming a project setting wins over
    // scalability, the camera, and every active post-process volume.
    int32 FinalRayLightingMode = -1;
    int32 FinalGIMethod = -1;
    bool bRuntimeRayTracingEnabled = false;
    bool bViewRayTracingAllowed = false;
    bool bGIShowFlagsEnabled = false;
    APlayerController* PC = Cast<APlayerController>(Player->GetController());
    if (PC && PC->GetLocalPlayer() && GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        FViewport* Viewport = GEngine->GameViewport->Viewport;
        FSceneViewFamilyContext Family(FSceneViewFamily::ConstructionValues(
            Viewport, GetWorld()->Scene, GEngine->GameViewport->EngineShowFlags).SetRealtimeUpdate(true));
        FVector ViewLocation;
        FRotator ViewRotation;
        if (FSceneView* View = PC->GetLocalPlayer()->CalcSceneView(&Family, ViewLocation, ViewRotation, Viewport))
        {
            FinalRayLightingMode = int32(View->FinalPostProcessSettings.LumenRayLightingMode);
            FinalGIMethod = int32(View->FinalPostProcessSettings.DynamicGlobalIlluminationMethod);
            bRuntimeRayTracingEnabled = IsRayTracingEnabled(Family.GetShaderPlatform());
            bViewRayTracingAllowed = View->IsRayTracingAllowedForView();
            bGIShowFlagsEnabled = Family.EngineShowFlags.GlobalIllumination
                && Family.EngineShowFlags.LumenGlobalIllumination
                && !Family.EngineShowFlags.PathTracing && !Family.EngineShowFlags.RayTracingDebug;
        }
    }
    const bool bHardwareLightingReady = GRHISupportsRayTracing && GRHISupportsRayTracingShaders
        && bRuntimeRayTracingEnabled && bViewRayTracingAllowed && bGIShowFlagsEnabled
        && GRHISupportsRayTracingDispatchIndirect && CVarInt(TEXT("r.RayTracing")) == 1
        && CVarInt(TEXT("r.Lumen.HardwareRayTracing")) == 1
        && CVarInt(TEXT("r.Lumen.DiffuseIndirect.Allow")) == 1
        && CVarInt(TEXT("r.RayTracing.Geometry.ProceduralMeshes")) == 1
        && CVarInt(TEXT("r.Lumen.ScreenProbeGather")) == 1
        && CVarInt(TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing")) == 1
        && FinalGIMethod == int32(EDynamicGlobalIlluminationMethod::Lumen);
    const bool bHitLightingReady = bHardwareLightingReady
        && CVarInt(TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed")) == 1
        && FinalRayLightingMode == int32(ELumenRayLightingModeOverride::HitLighting);
    int32 ConfiguredHitLightingAllowed = 1;
    GConfig->GetInt(TEXT("ConsoleVariables"), TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed"),
        ConfiguredHitLightingAllowed, GEngineIni);
    const bool bCompatibilityRequested = ConfiguredHitLightingAllowed == 0;
    const bool bCompatibilityReady = bCompatibilityRequested && bHardwareLightingReady
        && CVarInt(TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed")) == 0
        && FinalRayLightingMode == int32(ELumenRayLightingModeOverride::SurfaceCache);
    const bool bRenderPathReady = bCompatibilityRequested ? bCompatibilityReady : bHitLightingReady;
    const bool bFillDisableOverride = FParse::Param(FCommandLine::Get(), TEXT("BRDisablePlayerFill"));
    TArray<FString> Failures;
    auto Check = [&Failures](bool bPass, const TCHAR* Name)
    {
        if (!bPass) Failures.Add(Name);
    };
    const FRotator ActualProbeRotation = PC ? PC->GetControlRotation() : FRotator::ZeroRotator;
    const bool bProbeViewLocked = PC && PC->IsLookInputIgnored() && PC->IsMoveInputIgnored()
        && FMath::Abs(FMath::FindDeltaAngleDegrees(ActualProbeRotation.Yaw, LightingProbeRotation.Yaw)) <= .2f
        && FMath::Abs(FMath::FindDeltaAngleDegrees(ActualProbeRotation.Pitch, LightingProbeRotation.Pitch)) <= .2f;
    Check(bProbeViewLocked, TEXT("Diagnostic camera ignores physical input and retains the requested angles"));
    float SkyAmbientStrength = -1.f;
    FLinearColor SkyAmbientTint = FLinearColor::Black;
    bool bSkyAmbientReady = false;
    if (DayNightActor && DayNightActor->GetVoxelSkyCollection())
    {
        if (const UMaterialParameterCollectionInstance* Instance =
            GetWorld()->GetParameterCollectionInstance(DayNightActor->GetVoxelSkyCollection()))
        {
            bSkyAmbientReady = Instance->GetScalarParameterValue(TEXT("SkyAmbientStrength"), SkyAmbientStrength)
                && Instance->GetVectorParameterValue(TEXT("SkyAmbientTint"), SkyAmbientTint)
                && FMath::IsFinite(SkyAmbientStrength) && SkyAmbientStrength >= 0.f && SkyAmbientStrength <= 2000.f
                && FMath::IsFinite(SkyAmbientTint.R) && FMath::IsFinite(SkyAmbientTint.G)
                && FMath::IsFinite(SkyAmbientTint.B) && FMath::IsFinite(SkyAmbientTint.A);
        }
    }
    Check(bSkyAmbientReady, TEXT("Cooked voxel sky collection and per-world parameters are available"));
    const bool bPlayerLightComponentsRegistered = CaveLight && CaveLight->IsRegistered()
        && NightLight && NightLight->IsRegistered();
    Check(bPlayerLightComponentsRegistered, TEXT("Disabled player lighting components remain registered"));
    Check(bRenderPathReady, TEXT("Blended view and RHI satisfy the explicitly configured rendering profile"));
    Check(WorldActor->GetPendingChunkCount() == 0, TEXT("All requested chunks completed"));
    if (bRoof || bMouth) Check(bLightingProbeFixtureReady, TEXT("Every isolated fixture cell constructed"));
    if (bRoof)
    {
        const bool bOpaque = RoofMaterial.Equals(TEXT("Wood"), ESearchCase::IgnoreCase)
            || RoofMaterial.Equals(TEXT("Stone"), ESearchCase::IgnoreCase);
        Check(Player->IsLightingRoofOccluded() == bOpaque, TEXT("Opaque and transmitting roof classification"));
        Check(Player->IsLightingUnderCanopy() == RoofMaterial.Equals(TEXT("Leaves"), ESearchCase::IgnoreCase), TEXT("Leaves canopy classification"));
    }
    else if (bMouth)
    {
        Check(Player->IsLightingRoofOccluded() && Player->IsLightingCaveMouth()
            && !Player->IsLightingUnderCanopy()
            && FMath::IsNearlyEqual(Player->GetLightingEnclosureWeight(), .8f, .001f), TEXT("Open cave mouth has four of five roof columns"));
    }
    else if (bSurface)
    {
        Check(!Player->IsLightingRoofOccluded() && !Player->IsLightingCaveMouth()
            && FMath::IsNearlyZero(Player->GetCaveFillTargetIntensity()), TEXT("Open terrain keeps cave fill off"));
    }
    else
        Check(Player->IsLightingRoofOccluded() && Player->GetLightingEnclosureWeight() > 0.f, TEXT("Natural cave has authoritative roof coverage"));
    // Player-attached illumination is permanently disabled in every scene.
    // Dark caves without a placed torch or glowing mineral are expected.
    const bool bPlayerSourcesHidden = CaveLight && NightLight && !CaveLight->IsVisible() && !NightLight->IsVisible();
    Check(bPlayerSourcesHidden,
        TEXT("All scenes keep both player-attached light sources invisible"));
    const bool bPlayerIntensitiesZero = CaveLight && NightLight && FMath::IsNearlyZero(CaveLight->Intensity, .000001f)
        && FMath::IsNearlyZero(NightLight->Intensity, .000001f)
        && FMath::IsNearlyZero(Player->GetCaveFillIntensity(), .000001f)
        && FMath::IsNearlyZero(Player->GetOutdoorFillIntensity(), .000001f);
    Check(bPlayerIntensitiesZero,
        TEXT("All scenes keep both player light component and cached intensities at zero"));
    const bool bPlayerTargetsZero = FMath::IsNearlyZero(Player->GetCaveFillTargetIntensity(), .000001f)
        && FMath::IsNearlyZero(Player->GetOutdoorFillTargetIntensity(), .000001f);
    Check(bPlayerTargetsZero,
        TEXT("Cave, canopy, surface and night never request player-attached illumination"));
    int32 TorchLightCount = 0;
    int32 GlowLightCount = 0;
    bool bWorldLocalLightsValid = true;
    const auto CheckWorldLight = [&bWorldLocalLightsValid](UPointLightComponent* Light)
    {
        bWorldLocalLightsValid &= Light && Light->IsRegistered() && Light->IsVisible()
            && FMath::IsFinite(Light->Intensity) && Light->Intensity > 0.f
            && Light->CastShadows && FMath::IsFinite(Light->AttenuationRadius) && Light->AttenuationRadius > 0.f;
    };
    for (TActorIterator<ABlockRealmTorch> It(GetWorld()); It; ++It)
    {
        if (It->IsActorBeingDestroyed() || It->GetBlockWorld() != WorldActor) continue;
        TArray<UPointLightComponent*> Lights;
        It->GetComponents(Lights);
        Check(!Lights.IsEmpty(), TEXT("Every placed world torch retains its actual light component"));
        for (UPointLightComponent* Light : Lights) { ++TorchLightCount; CheckWorldLight(Light); }
    }
    for (TActorIterator<ABlockChunk> It(GetWorld()); It; ++It)
    {
        if (It->IsActorBeingDestroyed()) continue;
        TArray<UPointLightComponent*> Lights;
        It->GetComponents(Lights);
        for (UPointLightComponent* Light : Lights) { ++GlowLightCount; CheckWorldLight(Light); }
    }
    Check(bWorldLocalLightsValid, TEXT("Existing world torches and glowing minerals retain visible positive shadowed lights"));
    const bool bStateValid = Failures.IsEmpty();
    TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"), bStateValid);
    Result->SetStringField(TEXT("scene"), Scene);
    Result->SetStringField(TEXT("cell"), LightingProbeCell.ToString());
    Result->SetNumberField(TEXT("pending_chunks"), WorldActor->GetPendingChunkCount());
    Result->SetNumberField(TEXT("player_z"), Player->GetActorLocation().Z);
    Result->SetStringField(TEXT("validation_scope"), TEXT("render_configuration_and_fixture_state; inspect screenshots for visual occlusion; player-attached lights disabled; caves without world light sources may be dark"));
    Result->SetBoolField(TEXT("fixture_ready"), bLightingProbeFixtureReady);
    Result->SetStringField(TEXT("save_path"), WorldActor->GetSavePath());
    Result->SetNumberField(TEXT("view_yaw"), PC ? PC->GetControlRotation().Yaw : 0.f);
    Result->SetNumberField(TEXT("view_pitch"), PC ? PC->GetControlRotation().Pitch : 0.f);
    Result->SetBoolField(TEXT("probe_view_locked"), bProbeViewLocked);
    Result->SetBoolField(TEXT("roof_occluded"), Player->IsLightingRoofOccluded());
    Result->SetBoolField(TEXT("cave_mouth"), Player->IsLightingCaveMouth());
    Result->SetBoolField(TEXT("under_canopy"), Player->IsLightingUnderCanopy());
    Result->SetNumberField(TEXT("enclosure_weight"), Player->GetLightingEnclosureWeight());
    Result->SetBoolField(TEXT("cave_fill_visible"), CaveLight && CaveLight->IsVisible());
    Result->SetBoolField(TEXT("night_fill_visible"), NightLight && NightLight->IsVisible());
    Result->SetBoolField(TEXT("player_fill_disabled"), bPlayerLightComponentsRegistered
        && bPlayerSourcesHidden && bPlayerIntensitiesZero && bPlayerTargetsZero);
    Result->SetBoolField(TEXT("player_fill_disable_override"), bFillDisableOverride);
    Result->SetBoolField(TEXT("player_fill_components_registered"), bPlayerLightComponentsRegistered);
    Result->SetStringField(TEXT("player_fill_mode"), TEXT("permanently_disabled"));
    Result->SetStringField(TEXT("cave_darkness_policy"), TEXT("Dark caves without placed torches or glowing minerals are expected; the player emits no light."));
    Result->SetNumberField(TEXT("world_torch_light_count"), TorchLightCount);
    Result->SetNumberField(TEXT("world_glow_light_count"), GlowLightCount);
    Result->SetBoolField(TEXT("world_local_lights_valid"), bWorldLocalLightsValid);
    Result->SetNumberField(TEXT("cave_fill_intensity"), CaveLight ? CaveLight->Intensity : 0.f);
    Result->SetNumberField(TEXT("cave_fill_cached_intensity"), Player->GetCaveFillIntensity());
    Result->SetNumberField(TEXT("outdoor_fill_cached_intensity"), Player->GetOutdoorFillIntensity());
    Result->SetNumberField(TEXT("cave_fill_target"), Player->GetCaveFillTargetIntensity());
    Result->SetNumberField(TEXT("outdoor_fill_target"), Player->GetOutdoorFillTargetIntensity());
    Result->SetNumberField(TEXT("cave_fill_radius"), CaveLight ? CaveLight->AttenuationRadius : 0.f);
    Result->SetBoolField(TEXT("cave_fill_shadows"), CaveLight && CaveLight->CastShadows);
    Result->SetBoolField(TEXT("outdoor_fill_shadows"), NightLight && NightLight->CastShadows);
    Result->SetNumberField(TEXT("night_fill_intensity"), NightLight ? NightLight->Intensity : 0.f);
    Result->SetNumberField(TEXT("hit_lighting_allowed"), CVarInt(TEXT("r.Lumen.HardwareRayTracing.HitLighting.Allowed")));
    Result->SetNumberField(TEXT("hit_lighting_mode"), CVarInt(TEXT("r.Lumen.HardwareRayTracing.LightingMode")));
    Result->SetNumberField(TEXT("postprocess_lumen_ray_lighting_mode"), FinalRayLightingMode);
    Result->SetNumberField(TEXT("postprocess_gi_method"), FinalGIMethod);
    Result->SetBoolField(TEXT("raytracing_supported"), GRHISupportsRayTracing);
    Result->SetBoolField(TEXT("raygen_supported"), GRHISupportsRayTracingShaders);
    Result->SetBoolField(TEXT("raytracing_runtime_enabled"), bRuntimeRayTracingEnabled);
    Result->SetBoolField(TEXT("raytracing_view_allowed"), bViewRayTracingAllowed);
    Result->SetBoolField(TEXT("lumen_gi_show_flags_enabled"), bGIShowFlagsEnabled);
    Result->SetBoolField(TEXT("sky_ambient_collection_ready"), bSkyAmbientReady);
    Result->SetNumberField(TEXT("sky_ambient_strength"), SkyAmbientStrength);
    Result->SetStringField(TEXT("sky_ambient_tint"), SkyAmbientTint.ToString());
    Result->SetStringField(TEXT("sky_ambient_mode"), TEXT("voxel_column_occluded_surface_compensation"));
    Result->SetBoolField(TEXT("rt_indirect_dispatch_supported"), GRHISupportsRayTracingDispatchIndirect);
    Result->SetBoolField(TEXT("hit_lighting_requirements_met"), bHitLightingReady);
    Result->SetStringField(TEXT("render_path"), bCompatibilityRequested
        ? TEXT("hwrt_surface_cache_compatibility") : TEXT("hwrt_hit_lighting"));
    Result->SetBoolField(TEXT("render_path_requirements_met"), bRenderPathReady);
    Result->SetStringField(TEXT("lighting_compatibility_reason"), bCompatibilityRequested
        ? TEXT("Full Hit Lighting crashes during RTPSO creation on reference NVIDIA driver 566.24; world torches and glowing minerals retain shadowed local light, while player-attached lights remain disabled, without changing DLSS.") : TEXT(""));
    Result->SetNumberField(TEXT("procedural_mesh_raytracing"), CVarInt(TEXT("r.RayTracing.Geometry.ProceduralMeshes")));
    Result->SetNumberField(TEXT("lumen_diffuse_indirect_allowed"), CVarInt(TEXT("r.Lumen.DiffuseIndirect.Allow")));
    Result->SetNumberField(TEXT("lumen_screen_probe_gather"), CVarInt(TEXT("r.Lumen.ScreenProbeGather")));
    Result->SetNumberField(TEXT("lumen_screen_probe_hardware_raytracing"), CVarInt(TEXT("r.Lumen.ScreenProbeGather.HardwareRayTracing")));
    TArray<TSharedPtr<FJsonValue>> JsonFailures;
    for (const FString& Failure : Failures) JsonFailures.Add(MakeShared<FJsonValueString>(Failure));
    Result->SetArrayField(TEXT("failed"), JsonFailures);
    if (!LightingProbeFrameMs.IsEmpty())
    {
        LightingProbeFrameMs.Sort();
        double Total = 0.;
        for (float FrameMs : LightingProbeFrameMs) Total += FrameMs;
        const double AverageMs = Total / LightingProbeFrameMs.Num();
        const int32 P95 = FMath::Clamp(FMath::CeilToInt(LightingProbeFrameMs.Num() * .95f) - 1, 0, LightingProbeFrameMs.Num() - 1);
        Result->SetNumberField(TEXT("frame_samples"), LightingProbeFrameMs.Num());
        Result->SetNumberField(TEXT("average_frame_ms"), AverageMs);
        Result->SetNumberField(TEXT("average_fps"), 1000. / AverageMs);
        Result->SetNumberField(TEXT("p95_frame_ms"), LightingProbeFrameMs[P95]);
    }
    if (const IConsoleVariable* CoarsePages = IConsoleManager::Get().FindConsoleVariable(TEXT("r.Shadow.Virtual.NonNanite.IncludeInCoarsePages")))
        Result->SetNumberField(TEXT("non_nanite_coarse_pages"), CoarsePages->GetInt());
    Result->SetNumberField(TEXT("time_of_day"), GetTimeOfDay());
    Result->SetNumberField(TEXT("sun_lux"), DayNightActor && DayNightActor->SunLight ? DayNightActor->SunLight->Intensity : 0.f);
    Result->SetStringField(TEXT("graphics_status"), GetGraphicsStatus());
    FString Report;
    FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Report));
    FFileHelper::SaveStringToFile(Report, *FPaths::Combine(Directory, TEXT("runtime.json")));
    bLightingProbeShot = true;
    LightingProbeElapsed = 0.0f;
}

void ABlockRealmGameMode::TickSmoke(float DeltaSeconds)
{
    SmokeElapsed += DeltaSeconds;
    if (SmokeStage>=10 && SmokeStage<=12 && IsAdventureActive()) SmokeFrameMs.Add(DeltaSeconds*1000.f);
    SmokeTime -= DeltaSeconds;
    if (SmokeElapsed > 220.f)
    {
        SmokeFailures.Add(TEXT("Runtime validation timed out"));
        FinishSmoke();
        return;
    }
    if (SmokeTime > 0.f) return;
    ABlockRealmCharacter* Player = GetPlayerCharacter();
    APlayerController* PC = UGameplayStatics::GetPlayerController(this,0);
    if (!Player || !PC || !WorldActor) { SmokeTime=1.f; return; }
    auto Check = [this](bool Pass, const FString& Name)
    {
        (Pass ? SmokeChecks : SmokeFailures).Add(Name);
        UE_LOG(LogTemp,Display,TEXT("BR_CHECK %s %s"),Pass?TEXT("PASS"):TEXT("FAIL"),*Name);
    };
    auto Key = [PC](const FKey& Input)
    {
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Input,IE_Pressed,1.f));
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(Input,IE_Released,0.f));
    };
    auto Shot = [](const TCHAR* Name)
    {
        FString Dir;
        if (!FParse::Value(FCommandLine::Get(),TEXT("BRValidationDir="),Dir))
            Dir=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Validation"));
        IFileManager::Get().MakeDirectory(*Dir,true);
        FScreenshotRequest::RequestScreenshot(FPaths::Combine(Dir,FString(Name)+TEXT(".png")),true,false);
    };
    auto Click = [&](FName Id)
    {
        ABlockRealmHUD* HUD=Cast<ABlockRealmHUD>(PC->GetHUD());
        const FIntPoint Size=GEngine->GameViewport->Viewport->GetSizeXY();
        const FBox2D Bounds=ABlockRealmHUD::GetMenuButtonBounds(Id,FVector2D(Size.X,Size.Y),bPauseOpen);
        const FVector2D Center=Bounds.GetCenter();
        const FHUDHitBox* Hit=HUD?HUD->GetHitBoxAtCoordinates(Center,true):nullptr;
        Check(Hit && Hit->GetName()==Id,TEXT("HUD hitbox ")+Id.ToString());
        PC->SetMouseLocation(FMath::RoundToInt(Center.X),FMath::RoundToInt(Center.Y));
        Key(EKeys::LeftMouseButton);
    };
    switch(SmokeStage)
    {
    case 0:
        if (!WorldActor->IsInitialTerrainReady()) { SmokeTime=1.f; return; }
        if (!bSmokeSelfTestsDone)
        {
            Check(bTitleOpen && PC->bShowMouseCursor,TEXT("Title state and cursor"));
            {
                bool Daylight=false;
                for (TActorIterator<ADirectionalLight> It(GetWorld());It;++It)
                    Daylight |= It->GetActorRotation().Pitch<0.f;
                Check(Daylight,TEXT("Daylight sun rotation uses the correct axes"));
            }
            {
                TArray<FString> Failures;
                Check(WorldActor->RunSelfTests(Failures),TEXT("Voxel data and save self tests"));
                SmokeFailures.Append(Failures);
            }
            bSmokeSelfTestsDone=true;
            SmokeTime=1.f;
            return;
        }
        {
            const FString SavePath=WorldActor->GetSavePath();
            FString PriorSave;
            const bool HadSave=IFileManager::Get().FileExists(*SavePath);
            const bool CanRestore=!HadSave || FFileHelper::LoadFileToString(PriorSave,*SavePath);
            IFileManager::Get().MakeDirectory(*FPaths::GetPath(SavePath),true);
            if (CanRestore && SavePath.Contains(TEXT("ValidationWorld")) && FFileHelper::SaveStringToFile(TEXT("{}"),*SavePath))
            {
                StartAdventure();
                FString InvalidSave;
                FFileHelper::LoadFileToString(InvalidSave,*SavePath);
                Check(bTitleOpen && !bPendingStart && Notice==TEXT("存档无法读取，原文件已保留。") && InvalidSave==TEXT("{}"),TEXT("Invalid existing save stays at title without being overwritten"));
                const bool Restored=HadSave?FFileHelper::SaveStringToFile(PriorSave,*SavePath):IFileManager::Get().Delete(*SavePath);
                Check(Restored,TEXT("Invalid-save check restores the isolated prior save"));
                Notice=TEXT("山谷已就绪 · 开始探索");
            }
            else Check(false,TEXT("Invalid-save check can prepare its isolated file"));
        }
        Shot(TEXT("01_Title"));
        SmokeTime=2.f; break;
    case 1:
        if (!WorldActor->IsInitialTerrainReady()) { SmokeTime=1.f; return; }
        Click(TEXT("Start"));
        Check(IsAdventureActive()&&!PC->bShowMouseCursor,TEXT("Start mouse click enters game"));
        SmokeTime=3.f; break;
    case 2:
        Check(IsAdventureActive()&&!PC->bShowMouseCursor,TEXT("Start input processed"));
        Key(EKeys::F);
        {
            const FVector Location=Player->GetActorLocation();
            const FVector Top(Location.X,Location.Y,7000.f);
            FHitResult Surface;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmMiningView),false,Player);
            if (GetWorld()->LineTraceSingleByChannel(Surface,Top,Top-FVector(0.f,0.f,7200.f),ECC_Visibility,Query))
                Player->SetActorLocation(FVector(Location.X,Location.Y,Surface.ImpactPoint.Z+190.f));
        }
        PC->SetControlRotation(FRotator(-40.f,35.f,0.f));
        SmokeTime=2.f; break;
    case 3:
        if (bSmokeMiningInputSent)
        {
            Check(WorldActor->GetBlock(SmokePosition)==static_cast<EBlockType>(SmokeBlockType)
                && Player->IsMining() && Player->GetMiningProgress()>0.f && Player->GetMiningProgress()<1.f,
                TEXT("Mining input starts a timed hold before editing"));
            Shot(TEXT("07_MiningProgress"));
            SmokeTime=FMath::Max(.05f,Player->GetMiningDuration()*(1.f-Player->GetMiningProgress())+.04f);
            break;
        }
        Check(Player->IsFlying(),TEXT("Flight input"));
        Player->RefreshTarget();
        Check(Player->HasTarget(),TEXT("Raycast targets terrain"));
        if (!Player->HasTarget()) { FinishSmoke(); return; }
        SmokePosition=Player->GetTargetBlock();
        SmokeBlockType=static_cast<uint8>(WorldActor->GetBlock(SmokePosition));
        SmokeInventoryCount=Player->GetInventoryCount(int32(SmokeBlockType)-1);
        Player->SelectToolSlot(0);
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Pressed,1.f));
        bSmokeMiningInputSent=true;
        SmokeMiningStartTime=SmokeElapsed;
        SmokeTime=.1f; return;
    case 4:
        if (WorldActor->GetBlock(SmokePosition)!=EBlockType::Air && Player->IsMining()
            && SmokeElapsed-SmokeMiningStartTime<4.f) { SmokeTime=.03f; return; }
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::LeftMouseButton,IE_Released,0.f));
        Check(WorldActor->GetBlock(SmokePosition)==EBlockType::Air,TEXT("Mine input edits terrain"));
        Check(Player->GetInventoryCount(int32(SmokeBlockType)-1)==SmokeInventoryCount+1,TEXT("Mining adds inventory"));
        Shot(TEXT("02_Mining"));
        Player->SelectSlot(int32(SmokeBlockType)-1);
        PC->SetControlRotation(FRotator(-45.f,35.f,0.f));
        PC->UpdateCameraManager(0.f);
        Player->RefreshTarget();
        Check(Player->HasTarget(),TEXT("Target refreshed after collision rebuild"));
        if (!Player->HasTarget()) { FinishSmoke(); return; }
        SmokePosition=Player->GetPlacementBlock();
        {
            const bool Empty=WorldActor->GetBlock(SmokePosition)==EBlockType::Air;
            Check(Empty,TEXT("Placement targets an empty block"));
            if (!Empty) { FinishSmoke(); return; }
        }
        Key(EKeys::RightMouseButton);
        SmokeTime=3.f; break;
    case 5:
        {
            const bool Placed=WorldActor->GetBlock(SmokePosition)==static_cast<EBlockType>(SmokeBlockType);
            const bool Consumed=Player->GetInventoryCount(int32(SmokeBlockType)-1)==SmokeInventoryCount;
            Check(Placed,TEXT("Place input edits terrain"));
            Check(Consumed,TEXT("Placement consumes inventory"));
            if (!Placed || !Consumed) { FinishSmoke(); return; }
        }
        Shot(TEXT("03_Building"));
        Key(EKeys::F5);
        SmokeTime=1.f; break;
    case 6:
        Check(IFileManager::Get().FileExists(*WorldActor->GetSavePath()),TEXT("Save key writes isolated file"));
        Check(!GEngine->GameViewport->EngineShowFlags.ShaderComplexity,TEXT("Save shortcut preserves lit rendering"));
        {
            FWorldSaveState State;
            const bool Disturbed=WorldActor->SetBlock(SmokePosition,EBlockType::Air);
            Check(Disturbed,TEXT("Reload test changes the placed block first"));
            if (!Disturbed) { FinishSmoke(); return; }
            Check(WorldActor->LoadWorld(State),TEXT("World save reload"));
            Check(WorldActor->GetBlock(SmokePosition)==static_cast<EBlockType>(SmokeBlockType),TEXT("Reload restores edited blocks"));
            Check(State.Inventory.Num()==9 && State.SelectedSlot==Player->GetSelectedSlot(),TEXT("Reload preserves inventory and selection"));
        }
        SmokeStartPosition=Player->GetActorLocation();
        Key(EKeys::Escape);
        SmokeTime=2.f; break;
    case 7:
        if (!bSmokePauseShotDone)
        {
            Check(bPauseOpen&&PC->bShowMouseCursor,TEXT("Pause input and cursor"));
            Check(FVector::Dist(Player->GetActorLocation(),SmokeStartPosition)<5.f,TEXT("Pause freezes player position"));
            Shot(TEXT("04_Pause"));
            bSmokePauseShotDone=true;
            SmokeTime=1.f;
            return;
        }
        Click(TEXT("TSR"));
        SmokeTime=2.f; break;
    case 8:
        Check(GetUpscaleMode()==0,TEXT("TSR graphics button"));
        Check(IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt()==4,
            TEXT("TSR uses the TSR renderer"));
        Check(FMath::IsNearlyEqual(IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat(),100.f,.1f),
            TEXT("TSR renders at native percentage"));
        Click(TEXT("Quality"));
        SmokeTime=3.f; break;
    case 9:
        if (!bSmokeGraphicsShotDone)
        {
            const int32 Modes[] = {1,2,3,1};
            const TCHAR* Labels[] = {TEXT("Quality"),TEXT("Balanced"),TEXT("Performance"),TEXT("Restored Quality")};
            const bool Supported = FBlockRenderSettings::IsDLSSSupported();
            const float Percentage = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"))->GetFloat();
            const FString Label = Labels[SmokeGraphicsStep];
            Check(GetUpscaleMode()==(Supported?Modes[SmokeGraphicsStep]:0),Label+TEXT(" selection or supported fallback"));
            Check(IConsoleManager::Get().FindConsoleVariable(TEXT("r.AntiAliasingMethod"))->GetInt()==(Supported?2:4),
                Label+TEXT(" temporal renderer"));
            Check(Supported ? (Percentage>0.f && Percentage<100.f &&
                (SmokeGraphicsStep==3 ? Percentage>SmokePreviousPercentage : Percentage<SmokePreviousPercentage)) :
                FMath::IsNearlyEqual(Percentage,100.f,.1f), Label+TEXT(" rendering percentage"));
            SmokePreviousPercentage=Percentage;
            if (SmokeGraphicsStep<3)
            {
                const TCHAR* NextButtons[] = {TEXT("Balanced"),TEXT("Performance"),TEXT("Quality")};
                Click(NextButtons[SmokeGraphicsStep]);
                ++SmokeGraphicsStep;
                SmokeTime=3.f;
                return;
            }
            Shot(TEXT("05_Graphics"));
            bSmokeGraphicsShotDone=true;
            SmokeTime=1.f;
            return;
        }
        Click(TEXT("Resume"));
        SmokeTime=2.f; break;
    case 10:
        Check(IsAdventureActive(),TEXT("Resume mouse click"));
        Check(Player->IsFlying(),TEXT("Resume preserves flight mode"));
        SmokeStartPosition=Player->GetActorLocation();
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
        SmokeTime=1.5f; break;
    case 11:
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));
        Check(FVector::Dist(Player->GetActorLocation(),SmokeStartPosition)>100.f,TEXT("Forward movement input"));
        SmokeOldChunk=FVoxelData::ChunkFromBlock(ABlockWorld::WorldToBlock(Player->GetActorLocation()));
        SmokeStreamingPosition=Player->GetActorLocation()+FVector(-16000.f,0.f,1600.f);
        Player->SetActorLocation(SmokeStreamingPosition);
        PC->SetControlRotation(FRotator(-22.f,135.f,0.f));
        WorldActor->RequestStreaming(Player->GetActorLocation());
        SmokeTime=8.f; break;
    case 12:
        {
            const FIntPoint Destination=FVoxelData::ChunkFromBlock(ABlockWorld::WorldToBlock(SmokeStreamingPosition));
            TArray<AActor*> Chunks;
            UGameplayStatics::GetAllActorsOfClass(this,ABlockChunk::StaticClass(),Chunks);
            bool DestinationReady=false,OldChunkPresent=false;
            for (AActor* Actor:Chunks)
            {
                const ABlockChunk* Chunk=Cast<ABlockChunk>(Actor);
                if (!IsValid(Chunk)) continue;
                if (Chunk->GetChunkCoordinate()==Destination) DestinationReady=Chunk->IsCollisionReady();
                if (Chunk->GetChunkCoordinate()==SmokeOldChunk) OldChunkPresent=true;
            }
            if (!DestinationReady) { SmokeTime=1.f; return; }
            FHitResult GroundHit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmStreamingSmoke),false,Player);
            const FVector Top(SmokeStreamingPosition.X,SmokeStreamingPosition.Y,7000.f);
            const bool Hit=GetWorld()->LineTraceSingleByChannel(GroundHit,Top,Top-FVector(0.f,0.f,7200.f),ECC_Visibility,Query);
            const ABlockChunk* GroundChunk=Hit?Cast<ABlockChunk>(GroundHit.GetActor()):nullptr;
            if (!bSmokeExplorationPrepared)
            {
                if (GroundChunk)
                    Player->SetActorLocation(FVector(SmokeStreamingPosition.X,SmokeStreamingPosition.Y,GroundHit.ImpactPoint.Z+1000.f));
                Player->GetCharacterMovement()->StopMovementImmediately();
                PC->SetControlRotation(FRotator(-18.f,135.f,0.f));
                bSmokeExplorationPrepared=true;
                SmokeTime=1.f;
                return;
            }
            if (WorldActor->GetPendingChunkCount()>0) { SmokeTime=1.f; return; }
            Check(GroundChunk && GroundChunk->GetChunkCoordinate()==Destination,TEXT("Distant negative-coordinate terrain has collision"));
            Check(!OldChunkPresent,TEXT("Streaming unloads the old player chunk"));
            Check(WorldActor->GetLoadedChunkCount()<=169,TEXT("Streaming chunk retention stays bounded"));
        }
        Check(WorldActor->GetLoadedChunkCount()>0,TEXT("Chunks available after streaming move"));
        Check(WorldActor->GetVisibleFaceCount()>0,TEXT("Terrain mesh generated"));
        Shot(TEXT("06_Exploration"));
        Key(EKeys::Escape);
        SmokeTime=2.f; break;
    case 13:
        Check(bPauseOpen,TEXT("Pause after streaming"));
        Click(TEXT("Title"));
        SmokeTime=2.f; break;
    case 14:
        Check(bTitleOpen&&PC->bShowMouseCursor,TEXT("Return to title and save"));
        if (!WorldActor->IsInitialTerrainReady()) { SmokeTime=1.f; return; }
        Click(TEXT("Start"));
        SmokeTime=3.f; break;
    case 15:
        if (bPendingStart) { SmokeTime=1.f; return; }
        Check(IsAdventureActive(),TEXT("Continue saved world from title"));
        SmokeStartPosition=Player->GetActorLocation();
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Pressed,1.f));
        SmokeTime=1.f; break;
    case 16:
        PC->InputKey(FInputKeyEventArgs::CreateSimulated(EKeys::W,IE_Released,0.f));
        Check(FVector::Dist2D(Player->GetActorLocation(),SmokeStartPosition)>50.f,TEXT("Movement works after return and continue"));
        Key(EKeys::Escape);
        SmokeTime=2.f; break;
    case 17:
        Click(TEXT("Title"));
        SmokeTime=2.f; break;
    case 18:
        if (!bSmokeInteractionComplete)
        {
            if (!TickInteractionSmoke()) return;
            bSmokeInteractionComplete = true;
        }
        if (!bSmokeExtendedComplete)
        {
            if (!TickExtendedSmoke()) return;
            bSmokeExtendedComplete = true;
        }
        if (!bSmokeExpansionComplete)
        {
            if (!TickExpansionSmoke()) return;
            bSmokeExpansionComplete = true;
        }
        if (!TickV11Smoke()) return;
        FinishSmoke(); return;
    }
    ++SmokeStage;
}

void ABlockRealmGameMode::FinishSmoke()
{
    bSmoke=false;
    TSharedRef<FJsonObject> Result=MakeShared<FJsonObject>();
    Result->SetBoolField(TEXT("success"),SmokeFailures.IsEmpty());
    if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
    {
        const FIntPoint Size=GEngine->GameViewport->Viewport->GetSizeXY();
        Result->SetNumberField(TEXT("width"),Size.X);
        Result->SetNumberField(TEXT("height"),Size.Y);
    }
    Result->SetNumberField(TEXT("elapsed_seconds"),SmokeElapsed);
    Result->SetNumberField(TEXT("walking_speed_cm_s"),SmokeLandSpeed);
    Result->SetNumberField(TEXT("water_speed_cm_s"),SmokeWaterSpeed);
    Result->SetNumberField(TEXT("stone_mining_seconds"),SmokeMiningDuration);
    Result->SetBoolField(TEXT("dlss_supported"),FBlockRenderSettings::IsDLSSSupported());
    Result->SetStringField(TEXT("graphics_status"),GetGraphicsStatus());
    if (const IConsoleVariable* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("r.NGX.DLSS.Enable")))
        Result->SetBoolField(TEXT("dlss_cvar_enabled"),Enabled->GetInt()!=0);
    if (const IConsoleVariable* Percentage=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage")))
        Result->SetNumberField(TEXT("screen_percentage"),Percentage->GetFloat());
    if (!SmokeFrameMs.IsEmpty())
    {
        float Total=0.f;
        for (float Ms:SmokeFrameMs) Total+=Ms;
        SmokeFrameMs.Sort();
        Result->SetNumberField(TEXT("frame_samples"),SmokeFrameMs.Num());
        Result->SetNumberField(TEXT("sample_average_fps"),1000.f/(Total/SmokeFrameMs.Num()));
        Result->SetNumberField(TEXT("sample_p95_frame_ms"),SmokeFrameMs[FMath::Min(SmokeFrameMs.Num()-1,FMath::FloorToInt32(SmokeFrameMs.Num()*.95f))]);
    }
    Result->SetNumberField(TEXT("chunks"),WorldActor?WorldActor->GetLoadedChunkCount():0);
    Result->SetNumberField(TEXT("visible_faces"),WorldActor?WorldActor->GetVisibleFaceCount():0);
    TArray<TSharedPtr<FJsonValue>> Passed,Failed;
    for(const FString& Name:SmokeChecks) Passed.Add(MakeShared<FJsonValueString>(Name));
    for(const FString& Name:SmokeFailures) Failed.Add(MakeShared<FJsonValueString>(Name));
    Result->SetArrayField(TEXT("passed"),Passed);
    Result->SetArrayField(TEXT("failed"),Failed);
    FString Text;
    FJsonSerializer::Serialize(Result,TJsonWriterFactory<>::Create(&Text));
    FString Dir;
    if(!FParse::Value(FCommandLine::Get(),TEXT("BRValidationDir="),Dir))
        Dir=FPaths::Combine(FPaths::ProjectSavedDir(),TEXT("Validation"));
    IFileManager::Get().MakeDirectory(*Dir,true);
    FFileHelper::SaveStringToFile(Text,*FPaths::Combine(Dir,TEXT("runtime.json")));
    UE_LOG(LogTemp,Display,TEXT("BR_VALIDATION_COMPLETE checks=%d failed=%d dlss=%d"),SmokeChecks.Num(),SmokeFailures.Num(),FBlockRenderSettings::IsDLSSSupported());
    UKismetSystemLibrary::QuitGame(this,UGameplayStatics::GetPlayerController(this,0),EQuitPreference::Quit,false);
}
