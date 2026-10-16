#include "BlockRealmCharacter.h"

#include "BlockRealmGameMode.h"
#include "BlockRealmMovementComponent.h"
#include "BlockRealmMob.h"
#include "BlockRealmSurvival.h"
#include "BlockRealmPlayerActions.h"
#include "BlockRealmTorch.h"
#include "BlockWorld.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "InputCoreTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Sound/SoundBase.h"
#include "ProceduralMeshComponent.h"
#include "UnrealClient.h"
#include "VoxelData.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
    constexpr int32 SlotCount = 9;
    constexpr int32 WaterSlot = 7;
    constexpr float BlockSize = 100.f;
    constexpr float InteractionReach = 700.f;
    constexpr float WalkSpeed = 520.f;
    constexpr float RunSpeed = 850.f;
    constexpr float FlySpeed = 780.f;
    constexpr float FastFlySpeed = 1450.f;
    constexpr float WaterSpeed = 220.f;
    constexpr float FastWaterSpeed = 300.f;
    constexpr float LandGravityScale = 1.55f;
    constexpr float PlacementRepeatInterval = .18f;
    constexpr float PlacementCollisionBuffer = .4f;
    constexpr float LightingSampleInterval = .2f;
    constexpr float DefaultCaveFillIntensity = 0.f;
    constexpr float DefaultCaveFillRadius = 2000.f;
    constexpr float DefaultCaveMouthFillIntensity = 0.f;

    TAutoConsoleVariable<float> CVarCaveFillIntensity(
        TEXT("r.BlockRealm.CaveFillIntensity"), DefaultCaveFillIntensity,
        TEXT("Legacy compatibility setting. Automatic player fill is permanently disabled."), ECVF_Default);
    TAutoConsoleVariable<float> CVarCaveFillRadius(
        TEXT("r.BlockRealm.CaveFillRadius"), DefaultCaveFillRadius,
        TEXT("Legacy compatibility setting. The disabled player light does not illuminate the world."), ECVF_Default);
    TAutoConsoleVariable<float> CVarCaveMouthFillIntensity(
        TEXT("r.BlockRealm.CaveMouthFillIntensity"), DefaultCaveMouthFillIntensity,
        TEXT("Legacy compatibility setting. Automatic player fill is permanently disabled."), ECVF_Default);

    ABlockRealmGameMode* GetRealmMode(const UObject* Context)
    {
        return Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(Context));
    }

    // Original cuboid geometry: a small sleeve, hand and the equipped item.
    // No collision or world actor is created for these camera-only visuals.
    struct FHeldMeshSection
    {
        TArray<FVector> Vertices;
        TArray<int32> Triangles;
        TArray<FVector> Normals;
        TArray<FVector2D> UVs;
        TArray<FLinearColor> Colors;
        TArray<FProcMeshTangent> Tangents;

        void Box(FVector Center, FVector HalfSize, FLinearColor Color,
            FRotator Rotation = FRotator::ZeroRotator)
        {
            const FVector FaceNormals[] = { FVector(1,0,0), FVector(-1,0,0),
                FVector(0,1,0), FVector(0,-1,0), FVector(0,0,1), FVector(0,0,-1) };
            const FVector FaceU[] = { FVector(0,1,0), FVector(0,-1,0),
                FVector(-1,0,0), FVector(1,0,0), FVector(1,0,0), FVector(-1,0,0) };
            const FVector FaceV[] = { FVector(0,0,1), FVector(0,0,1),
                FVector(0,0,1), FVector(0,0,1), FVector(0,1,0), FVector(0,1,0) };
            const FVector2D TexCoords[] = { FVector2D(0,1), FVector2D(1,1),
                FVector2D(1,0), FVector2D(0,0) };
            for (int32 Face = 0; Face < 6; ++Face)
            {
                const FVector N = FaceNormals[Face];
                const FVector U = FaceU[Face] * HalfSize;
                const FVector V = FaceV[Face] * HalfSize;
                const FVector C = N * HalfSize;
                const FVector Corners[] = { C-U-V, C+U-V, C+U+V, C-U+V };
                const int32 First = Vertices.Num();
                const float Shade = Face == 4 ? 1.f : Face == 5 ? .62f : Face % 2 == 0 ? .88f : .74f;
                for (int32 Corner = 0; Corner < 4; ++Corner)
                {
                    Vertices.Add(Center + Rotation.RotateVector(Corners[Corner]));
                    Normals.Add(Rotation.RotateVector(N));
                    UVs.Add(TexCoords[Corner]);
                    Colors.Add(FLinearColor(Color.R * Shade, Color.G * Shade, Color.B * Shade, 1.f));
                    Tangents.Add(FProcMeshTangent(Rotation.RotateVector(FaceU[Face]), false));
                }
                const int32 Indices[] = { 0,2,1,0,3,2 };
                for (int32 Index : Indices) Triangles.Add(First + Index);
            }
        }
    };
}

ABlockRealmCharacter::ABlockRealmCharacter(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer.SetDefaultSubobjectClass<UBlockRealmMovementComponent>(ACharacter::CharacterMovementComponentName))
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.bTickEvenWhenPaused = true;
    GetCapsuleComponent()->InitCapsuleSize(34.f, 90.f);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
    bUseControllerRotationPitch = false;
    bUseControllerRotationYaw = true;
    bUseControllerRotationRoll = false;

    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->bOrientRotationToMovement = false;
    Movement->MaxWalkSpeed = WalkSpeed;
    Movement->MaxFlySpeed = FlySpeed;
    Movement->JumpZVelocity = 620.f;
    Movement->GravityScale = LandGravityScale;
    Movement->AirControl = .45f;
    Movement->MaxAcceleration = 2700.f;
    Movement->BrakingDecelerationWalking = 2600.f;
    Movement->BrakingDecelerationFlying = 2500.f;
    Movement->GroundFriction = 8.f;
    Movement->MaxStepHeight = 42.f;
    Movement->NavAgentProps.bCanFly = true;

    FirstPersonCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FirstPersonCamera"));
    FirstPersonCamera->SetupAttachment(GetCapsuleComponent());
    FirstPersonCamera->SetRelativeLocation(FVector(0.f, 0.f, 68.f));
    FirstPersonCamera->bUsePawnControlRotation = true;
    FirstPersonCamera->FieldOfView = 88.f;
    FirstPersonCamera->SetEnableFirstPersonFieldOfView(true);
    FirstPersonCamera->SetFirstPersonFieldOfView(80.f);
    FirstPersonCamera->SetEnableFirstPersonScale(true);
    FirstPersonCamera->SetFirstPersonScale(.35f);

    HeldItemRoot = CreateDefaultSubobject<USceneComponent>(TEXT("HeldItemRoot"));
    HeldItemRoot->SetupAttachment(FirstPersonCamera);
    HeldItemRoot->SetRelativeLocation(FVector(65.f, 30.f, -21.f));
    HeldItemMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("HeldItemMesh"));
    HeldItemMesh->SetupAttachment(HeldItemRoot);
    HeldItemMesh->SetRelativeScale3D(FVector(.85f));
    HeldItemMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    HeldItemMesh->SetGenerateOverlapEvents(false);
    HeldItemMesh->SetCanEverAffectNavigation(false);
    HeldItemMesh->SetCastShadow(false);
    HeldItemMesh->SetOnlyOwnerSee(true);
    HeldItemMesh->SetVisibleInRayTracing(false);
    HeldItemMesh->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
    HeldItemMesh->bUseAsyncCooking = false;
    HeldItemMesh->SetVisibility(false);

    // Constructor references keep the runtime materials in a cooked build.
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> VertexMaterial(
        TEXT("/Engine/EngineDebugMaterials/VertexColorMaterial.VertexColorMaterial"));
    HeldVertexMaterial = VertexMaterial.Object;
    HeldBlockMaterials.SetNum(10);
    const TCHAR* HeldMaterialNames[] = { TEXT("Air"), TEXT("Grass"), TEXT("Dirt"), TEXT("Stone"),
        TEXT("Sand"), TEXT("Wood"), TEXT("Leaves"), TEXT("Glass"), TEXT("Water"), TEXT("Glow") };
    for (int32 Type = 1; Type < HeldBlockMaterials.Num(); ++Type)
    {
        const FString Path = FString::Printf(TEXT("/Game/BlockRealm/Materials/M_%s.M_%s"),
            HeldMaterialNames[Type], HeldMaterialNames[Type]);
        ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(*Path);
        HeldBlockMaterials[Type] = Material.Object;
    }

    CaveFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("CaveFillLight"));
    CaveFillLight->SetupAttachment(GetCapsuleComponent());
    CaveFillLight->SetRelativeLocation(FVector(0.f, 0.f, 58.f));
    CaveFillLight->SetMobility(EComponentMobility::Movable);
    CaveFillLight->bAffectsWorld = false;
    CaveFillLight->SetCastShadows(true);
    CaveFillLight->SetAffectTranslucentLighting(false);
    // Retain the named component for old diagnostics. It never lights the world;
    // underground visibility depends on the world's sky and placed light sources.
    CaveFillLight->SetUseInverseSquaredFalloff(false);
    CaveFillLight->SetIntensityUnits(ELightUnits::Unitless);
    CaveFillLight->SetLightFalloffExponent(2.f);
    CaveFillLight->SetInverseExposureBlend(1.f);
    CaveFillLight->SetIntensity(0.f);
    CaveFillLight->SetAttenuationRadius(DefaultCaveFillRadius);
    CaveFillLight->SetSourceRadius(12.f);
    CaveFillLight->SetSoftSourceRadius(20.f);
    // A camera-relative helper must not pump the persistent Lumen bounce field.
    CaveFillLight->SetIndirectLightingIntensity(0.f);
    CaveFillLight->SetVolumetricScatteringIntensity(0.f);
    CaveFillLight->SetLightColor(FLinearColor(1.f, .88f, .72f));
    CaveFillLight->SetVisibility(false);
    CaveFillLight->SetHiddenInGame(true);

    OutdoorNightFillLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("OutdoorNightFillLight"));
    OutdoorNightFillLight->SetupAttachment(GetCapsuleComponent());
    OutdoorNightFillLight->SetRelativeLocation(FVector(0.f, 0.f, 58.f));
    OutdoorNightFillLight->SetMobility(EComponentMobility::Movable);
    OutdoorNightFillLight->bAffectsWorld = false;
    OutdoorNightFillLight->SetUseInverseSquaredFalloff(false);
    OutdoorNightFillLight->SetIntensityUnits(ELightUnits::Unitless);
    // No character-relative night lamp. World lights remain authoritative.
    OutdoorNightFillLight->SetLightFalloffExponent(2.f);
    OutdoorNightFillLight->SetInverseExposureBlend(1.f);
    OutdoorNightFillLight->SetIntensity(0.f);
    OutdoorNightFillLight->SetAttenuationRadius(1800.f);
    OutdoorNightFillLight->SetSourceRadius(12.f);
    OutdoorNightFillLight->SetSoftSourceRadius(20.f);
    OutdoorNightFillLight->SetLightColor(FLinearColor(.58f, .69f, 1.f));
    OutdoorNightFillLight->SetCastShadows(true);
    OutdoorNightFillLight->SetIndirectLightingIntensity(0.f);
    OutdoorNightFillLight->SetAffectTranslucentLighting(false);
    OutdoorNightFillLight->SetVolumetricScatteringIntensity(0.f);
    OutdoorNightFillLight->SetVisibility(false);
    OutdoorNightFillLight->SetHiddenInGame(true);

    PlayerActions = CreateDefaultSubobject<UBlockRealmPlayerActionsComponent>(TEXT("PlayerActions"));

    Inventory.Init(64, SlotCount);
    Inventory[WaterSlot] = 0;
    ToolSlots.Init(FBlockRealmToolSlot(), FBlockRealmSurvivalData::ToolSlotCount);
    ResetSurvivalState();
}

void ABlockRealmCharacter::BeginPlay()
{
    Super::BeginPlay();
    UE_LOG(LogTemp, Display, TEXT("BR_PLAYER_LIGHT player_fill_disabled=1 cave=0 outdoor=0 source=world_lights_only"));
    MineSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/BlockRealm/Audio/MineSoil.MineSoil"));
    MineStoneSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/BlockRealm/Audio/MineStone.MineStone"));
    MineWoodSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/BlockRealm/Audio/MineWood.MineWood"));
    PlaceSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/BlockRealm/Audio/PlaceBlock.PlaceBlock"));
    SlotSound = LoadObject<USoundBase>(nullptr, TEXT("/Game/BlockRealm/Audio/SelectSlot.SelectSlot"));
    bHeldVisualInitialized = true;
    RefreshHeldItemVisual();
    if (!BlockWorld)
    {
        if (ABlockRealmGameMode* Game = GetRealmMode(this))
        {
            SetWorld(Game->GetWorldActor());
        }
    }
}

void ABlockRealmCharacter::PossessedBy(AController* NewController)
{
    Super::PossessedBy(NewController);
    if (NewController && bHasPendingRestoreView)
    {
        NewController->SetControlRotation(PendingRestoreView);
        bHasPendingRestoreView = false;
    }
}

void ABlockRealmCharacter::RefreshHeldItemVisual()
{
    if (!bHeldVisualInitialized || !HeldItemMesh) return;
    HeldItemMesh->ClearAllMeshSections();
    FHeldMeshSection Sections[4];
    const FLinearColor Skin(.64f, .39f, .22f, 1.f);
    const FLinearColor Sleeve(.16f, .31f, .27f, 1.f);
    Sections[0].Box(FVector(0.f, 0.f, 0.f), FVector(7.f, 4.8f, 4.8f), Skin, FRotator(0.f, 0.f, -12.f));
    Sections[0].Box(FVector(5.f, -3.8f, 2.8f), FVector(3.f, 2.f, 2.3f), Skin * .92f);
    Sections[1].Box(FVector(-12.f, 4.f, -7.f), FVector(10.f, 5.2f, 6.f), Sleeve,
        FRotator(-22.f, -12.f, -12.f));

    UMaterialInterface* ItemMaterial = HeldVertexMaterial;
    UMaterialInterface* HeadMaterial = HeldVertexMaterial;
    const auto BlockMaterial = [this](EBlockType Type) -> UMaterialInterface*
    {
        const int32 Index = static_cast<int32>(Type);
        return HeldBlockMaterials.IsValidIndex(Index) && HeldBlockMaterials[Index]
            ? HeldBlockMaterials[Index].Get() : HeldVertexMaterial.Get();
    };
    const EBlockRealmToolType Tool = GetSelectedToolType();
    if (IsBlockEquipped() && GetInventoryCount(SelectedSlot) > 0)
    {
        Sections[2].Box(FVector(4.f, -2.f, 9.f), FVector(10.f), FLinearColor::White,
            FRotator(8.f, 18.f, -10.f));
        ItemMaterial = BlockMaterial(GetSelectedType());
    }
    else if (Tool == EBlockRealmToolType::WoodenPickaxe || Tool == EBlockRealmToolType::StonePickaxe)
    {
        Sections[2].Box(FVector(3.f, -1.f, 10.f), FVector(1.8f, 1.8f, 16.f), FLinearColor::White,
            FRotator(0.f, 0.f, -14.f));
        Sections[3].Box(FVector(3.f, 4.f, 25.f), FVector(3.2f, 12.f, 2.8f), FLinearColor::White);
        Sections[3].Box(FVector(3.f, -8.f, 22.5f), FVector(3.f, 4.f, 2.5f), FLinearColor::White,
            FRotator(0.f, 0.f, -25.f));
        Sections[3].Box(FVector(3.f, 16.f, 22.5f), FVector(3.f, 4.f, 2.5f), FLinearColor::White,
            FRotator(0.f, 0.f, 25.f));
        ItemMaterial = BlockMaterial(EBlockType::Wood);
        HeadMaterial = BlockMaterial(Tool == EBlockRealmToolType::StonePickaxe ? EBlockType::Stone : EBlockType::Wood);
    }
    else if (Tool == EBlockRealmToolType::Torch)
    {
        Sections[2].Box(FVector(3.f, -1.f, 10.f), FVector(2.f, 2.f, 13.f), FLinearColor::White,
            FRotator(0.f, 0.f, -10.f));
        Sections[3].Box(FVector(3.f, 1.5f, 24.f), FVector(3.2f, 3.2f, 4.5f), FLinearColor::White);
        ItemMaterial = BlockMaterial(EBlockType::Wood);
        HeadMaterial = BlockMaterial(EBlockType::Glow);
    }
    else
    {
        // Empty selected stacks show the actual hand instead of a phantom block.
        Sections[0].Box(FVector(5.5f, -1.f, 1.5f), FVector(3.f, 4.8f, 4.5f), Skin);
    }
    UMaterialInterface* Materials[] = { HeldVertexMaterial.Get(), HeldVertexMaterial.Get(), ItemMaterial, HeadMaterial };
    for (int32 Index = 0; Index < 4; ++Index)
    {
        const FHeldMeshSection& Section = Sections[Index];
        if (Section.Vertices.IsEmpty()) continue;
        HeldItemMesh->CreateMeshSection_LinearColor(Index, Section.Vertices, Section.Triangles,
            Section.Normals, Section.UVs, Section.Colors, Section.Tangents, false);
        HeldItemMesh->SetMaterial(Index, Materials[Index]);
    }
}

void ABlockRealmCharacter::UpdateHeldItemVisual(float DeltaSeconds)
{
    if (!HeldItemRoot || !HeldItemMesh) return;
    const bool bVisible = CanPlay() && !bInventoryOpen && IsLocallyControlled();
    HeldItemMesh->SetVisibility(bVisible);
    if (!bVisible) return;
    const float Speed = GetVelocity().Size2D();
    const bool bWalking = !IsFlying() && !bInWater && GetCharacterMovement()->IsMovingOnGround();
    const float BobStrength = bWalking ? FMath::Clamp(Speed / RunSpeed, 0.f, 1.f) : 0.f;
    HeldMotionTime = FMath::Fmod(HeldMotionTime + FMath::Max(0.f, DeltaSeconds) * 9.f, 2.f * PI);
    const float Swing = bMiningActive ? FMath::Sin(GetMiningProgress() * 4.f * PI) : 0.f;
    const float Place = bPlaceHeld ? FMath::Max(0.f, PlacementCooldown / PlacementRepeatInterval) : 0.f;
    HeldItemRoot->SetRelativeLocation(FVector(65.f - Swing * 2.f - Place * 3.f,
        30.f + FMath::Sin(HeldMotionTime) * .55f * BobStrength,
        -21.f + FMath::Cos(HeldMotionTime * 2.f) * .4f * BobStrength - Swing));
    HeldItemRoot->SetRelativeRotation(FRotator(-Swing * 9.f + Place * 5.f, -Swing * 4.f, Swing * 5.f));
}

void ABlockRealmCharacter::SetWorld(ABlockWorld* InWorld)
{
    ResetGameplayState();
    BlockWorld = InWorld;
    bHasTarget = false;
}

bool ABlockRealmCharacter::CanPlay() const
{
    const ABlockRealmGameMode* Game = GetRealmMode(this);
    return Game && Game->IsAdventureActive();
}

bool ABlockRealmCharacter::HasGameplayFocus() const
{
    // The existing isolated smoke harness injects engine input into a hidden window.
    // Ordinary play always uses the real viewport's focus, including release recovery.
    static const bool bSmokeInput = FParse::Param(FCommandLine::Get(), TEXT("BlockRealmSmoke"));
    if (bSmokeInput)
    {
        return true;
    }
    const UGameViewportClient* ViewportClient = GetWorld() ? GetWorld()->GetGameViewport() : nullptr;
    return !ViewportClient || !ViewportClient->Viewport
        || (ViewportClient->Viewport->HasFocus() && ViewportClient->Viewport->IsForegroundWindow());
}

bool ABlockRealmCharacter::CanUseGameplayInput() const
{
    return CanPlay() && HasGameplayFocus() && !bInventoryOpen;
}

bool ABlockRealmCharacter::CanUsePlayerActions() const
{
    return CanUseGameplayInput() && bGameplayInputReady;
}

void ABlockRealmCharacter::NotifyPlayerAction(const FString& Message)
{
    SetInteractionNotice(Message);
}

bool ABlockRealmCharacter::AddBlockItem(EBlockType Type, int32 Count)
{
    const int32 Slot = static_cast<int32>(Type) - 1;
    if (Type == EBlockType::Air || Type == EBlockType::Water || Count <= 0
        || !Inventory.IsValidIndex(Slot) || Inventory[Slot] > 1000000 - Count) return false;
    Inventory[Slot] += Count;
    RefreshHeldItemVisual();
    return true;
}

bool ABlockRealmCharacter::AddTorchItem(int32 Count)
{
    const bool bAdded = FBlockRealmSurvivalData::TryAddTorches(Count, ToolSlots);
    if (bAdded) RefreshHeldItemVisual();
    return bAdded;
}

void ABlockRealmCharacter::WearSelectedTool()
{
    ConsumeToolDurability();
}

float ABlockRealmCharacter::GetDamageFeedbackAlpha() const
{
    return FMath::Clamp(static_cast<float>((DamageFeedbackUntil - FPlatformTime::Seconds()) / .35), 0.f, 1.f);
}

void ABlockRealmCharacter::AttackPressed()
{
    if (PlayerActions) PlayerActions->TryAttack();
}

void ABlockRealmCharacter::EatPressed()
{
    if (PlayerActions) PlayerActions->TryEat();
}

void ABlockRealmCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);
    if (!PlayerInputComponent)
    {
        return;
    }
    PlayerInputComponent->BindAxisKey(EKeys::MouseX, this, &ABlockRealmCharacter::Turn);
    PlayerInputComponent->BindAxisKey(EKeys::MouseY, this, &ABlockRealmCharacter::LookUp);

    // Chords without modifier requirements also work while running or flying down.
    const FKey SlotKeys[] = {EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five,
        EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine};
    const auto Bind = [this, PlayerInputComponent](const FKey& Key,
        EInputEvent Event, void (ABlockRealmCharacter::*Handler)(), bool bPaused = false)
    {
        FInputKeyBinding& Binding = PlayerInputComponent->BindKey(Key, Event, this, Handler);
        Binding.bExecuteWhenPaused = bPaused;
    };
    Bind(EKeys::SpaceBar, IE_Pressed, &ABlockRealmCharacter::BeginJump);
    Bind(EKeys::SpaceBar, IE_Released, &ABlockRealmCharacter::EndJump, true);
    Bind(EKeys::F, IE_Pressed, &ABlockRealmCharacter::ToggleFlight);
    Bind(EKeys::F, IE_Released, &ABlockRealmCharacter::FlightReleased, true);
    Bind(EKeys::LeftMouseButton, IE_Pressed, &ABlockRealmCharacter::MinePressed);
    Bind(EKeys::LeftMouseButton, IE_Released, &ABlockRealmCharacter::MineReleased, true);
    Bind(EKeys::RightMouseButton, IE_Pressed, &ABlockRealmCharacter::PlacePressed);
    Bind(EKeys::RightMouseButton, IE_Released, &ABlockRealmCharacter::PlaceReleased, true);
    Bind(EKeys::MouseScrollUp, IE_Pressed, &ABlockRealmCharacter::PreviousSlot);
    Bind(EKeys::MouseScrollDown, IE_Pressed, &ABlockRealmCharacter::NextSlot);
    Bind(EKeys::Q, IE_Pressed, &ABlockRealmCharacter::PreviousToolPressed);
    Bind(EKeys::R, IE_Pressed, &ABlockRealmCharacter::NextToolPressed);
    Bind(EKeys::Zero, IE_Pressed, &ABlockRealmCharacter::BareHandsPressed);
    Bind(EKeys::E, IE_Pressed, &ABlockRealmCharacter::ToggleInventoryPressed);
    Bind(EKeys::MiddleMouseButton, IE_Pressed, &ABlockRealmCharacter::AttackPressed);
    Bind(EKeys::C, IE_Pressed, &ABlockRealmCharacter::EatPressed);
    Bind(EKeys::V, IE_Pressed, &ABlockRealmCharacter::FarmPressed);
    Bind(EKeys::T, IE_Pressed, &ABlockRealmCharacter::FurnacePressed);
    Bind(EKeys::B, IE_Pressed, &ABlockRealmCharacter::RestPressed);
    Bind(EKeys::Escape, IE_Pressed, &ABlockRealmCharacter::PausePressed, true);
    Bind(EKeys::F5, IE_Pressed, &ABlockRealmCharacter::SavePressed, true);
    for (int32 Slot = 0; Slot < SlotCount; ++Slot)
    {
        FInputKeyBinding Binding(FInputChord(SlotKeys[Slot], false, false, false, false), IE_Pressed);
        Binding.KeyDelegate.GetDelegateForManualSet().BindLambda([this, Slot]()
        {
            if (CanUseGameplayInput())
            {
                SelectSlot(Slot);
            }
        });
        PlayerInputComponent->KeyBindings.Add(MoveTemp(Binding));
    }
}

void ABlockRealmCharacter::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    UpdateHeldItemVisual(DeltaSeconds);
    APlayerController* PlayerController = Cast<APlayerController>(Controller);
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (!CanPlay())
    {
        CancelGameplayInput();
        bHadGameplayFocus = false;
        const ABlockRealmGameMode* Game = GetRealmMode(this);
        if (Game && !Game->IsPausedMenu())
        {
            // Returning to the title is a new adventure boundary, not a pause.
            ResetGameplayState();
        }
        return;
    }
    if (BlockWorld)
    {
        BlockWorld->RequestStreaming(GetActorLocation());
    }
    if (!HasGameplayFocus())
    {
        if (bHadGameplayFocus)
        {
            CancelGameplayInput();
        }
        bHadGameplayFocus = false;
        ConsumeMovementInputVector();
        UpdateWaterState();
        if (bInventoryOpen) Movement->SetMovementMode(MOVE_None);
        else UpdateMovementState(false);
        return;
    }
    bHadGameplayFocus = true;
    UpdateWaterState();
    if (bInventoryOpen)
    {
        // The E-toggle is a lightweight local inventory overlay.  It stops
        // movement/edit input while keeping the character and world intact;
        // closing it waits for mouse release through the normal input gate.
        CancelGameplayInput();
        Movement->SetMovementMode(MOVE_None);
        return;
    }
    const bool bRunning = PlayerController && (PlayerController->IsInputKeyDown(EKeys::LeftShift)
        || PlayerController->IsInputKeyDown(EKeys::RightShift));
    UpdateMovementState(bRunning);
    if (PlayerController)
    {
        // Wait for the menu click to be released before accepting world edits.
        bGameplayInputReady |= !PlayerController->IsInputKeyDown(EKeys::LeftMouseButton)
            && !PlayerController->IsInputKeyDown(EKeys::RightMouseButton);
        if (bPendingInventoryFood && bGameplayInputReady)
        {
            bPendingInventoryFood = false;
            if (PlayerActions) PlayerActions->TryEat();
        }
        if (!PlayerController->IsInputKeyDown(EKeys::F))
        {
            bFlightKeyHeld = false;
        }
        const float Forward = (PlayerController->IsInputKeyDown(EKeys::W) ? 1.f : 0.f)
            - (PlayerController->IsInputKeyDown(EKeys::S) ? 1.f : 0.f);
        const float Right = (PlayerController->IsInputKeyDown(EKeys::D) ? 1.f : 0.f)
            - (PlayerController->IsInputKeyDown(EKeys::A) ? 1.f : 0.f);
        const FRotator Heading(0.f, PlayerController->GetControlRotation().Yaw, 0.f);
        AddMovementInput(Heading.Vector(), Forward);
        AddMovementInput(FRotationMatrix(Heading).GetUnitAxis(EAxis::Y), Right);
        if (IsFlying())
        {
            const float Vertical = (PlayerController->IsInputKeyDown(EKeys::SpaceBar) ? 1.f : 0.f)
                - ((PlayerController->IsInputKeyDown(EKeys::LeftControl)
                    || PlayerController->IsInputKeyDown(EKeys::RightControl)) ? 1.f : 0.f);
            AddMovementInput(FVector::UpVector, Vertical);
        }
        else if (!PlayerController->IsInputKeyDown(EKeys::SpaceBar))
        {
            StopJumping();
        }
        FirstPersonCamera->FieldOfView = FMath::FInterpTo(FirstPersonCamera->FieldOfView,
            bRunning && !Movement->Velocity.IsNearlyZero(30.f) ? 92.f : 88.f, DeltaSeconds, 6.f);
    }
    RefreshTarget();
    TickMining(DeltaSeconds);
    TickPlacement(DeltaSeconds);
    TickSurvival(DeltaSeconds, bRunning);
    if (bHasTarget)
    {
        DrawDebugBox(GetWorld(), ABlockWorld::BlockToWorld(TargetBlock), FVector(BlockSize * .505f),
            FColor(219, 237, 204), false, 0.f, 0, 1.3f);
    }
    // The finite-height initial world has no kill plane; recover an accidental fall.
    if (BlockWorld && GetActorLocation().Z < -1800.f)
    {
        ResetGameplayState();
        Movement->StopMovementImmediately();
        ABlockRealmGameMode* Game = GetRealmMode(this);
        const FVector Respawn = Game ? Game->GetAdventureRespawnLocation() : BlockWorld->GetSpawnLocation();
        BlockWorld->RequestStreaming(Respawn);
        SetActorLocation(Respawn, false, nullptr, ETeleportType::TeleportPhysics);
        SetInteractionNotice(TEXT("已回到安全的山野。"));
    }
}

void ABlockRealmCharacter::CancelGameplayInput()
{
    bHasTarget = false;
    bPendingInventoryFood = false;
    bGameplayInputReady = false;
    bMineHeld = false;
    bPlaceHeld = false;
    CancelMining();
    CancelPlacement();
    StopJumping();
    ConsumeMovementInputVector();
    GetCharacterMovement()->StopMovementImmediately();
    const APlayerController* PC = Cast<APlayerController>(Controller);
    bFlightKeyHeld = PC && PC->IsInputKeyDown(EKeys::F);
    if (UBlockRealmMovementComponent* Movement = Cast<UBlockRealmMovementComponent>(GetCharacterMovement()))
    {
        Movement->SetWaterInput(0.f, WaterSpeed);
    }
}

void ABlockRealmCharacter::ResetGameplayState()
{
    CancelGameplayInput();
    if (UBlockRealmMovementComponent* Movement = Cast<UBlockRealmMovementComponent>(GetCharacterMovement()))
    {
        Movement->ResetFallDamageTracking();
    }
    ResetSurvivalState();
    bInventoryOpen = false;
    bHasLightingSample = false;
    LightingSampleCell = FIntVector(MIN_int32, MIN_int32, MIN_int32);
    LastLightingPosition = FVector::ZeroVector;
    NextLightingSampleTime = 0.0;
    LastLightingUpdateTime = -1.0;
    bLightingRoofOccluded = false;
    bLightingCaveMouth = false;
    bLightingUnderCanopy = false;
    LightingEnclosureWeight = 0.f;
    CaveFillIntensity = OutdoorFillIntensity = 0.f;
    CaveFillTargetIntensity = OutdoorFillTargetIntensity = 0.f;
    if (CaveFillLight)
    {
        CaveFillLight->SetIntensity(0.f);
        CaveFillLight->SetVisibility(false, true);
    }
    if (OutdoorNightFillLight)
    {
        OutdoorNightFillLight->SetIntensity(0.f);
        OutdoorNightFillLight->SetVisibility(false, true);
    }
    bFlightRequested = false;
    bInWater = false;
    bWaterMovementActive = false;
    WaterImmersion = 0.f;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    Movement->bCheatFlying = false;
    Movement->GravityScale = LandGravityScale;
    Movement->MaxWalkSpeed = WalkSpeed;
    Movement->MaxAcceleration = 2700.f;
    SetActorEnableCollision(true);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    if (Movement->MovementMode != MOVE_None)
    {
        Movement->SetMovementMode(MOVE_Falling);
    }
}

void ABlockRealmCharacter::UpdateWaterState()
{
    WaterImmersion = 0.f;
    if (BlockWorld)
    {
        const FVector Center = GetCapsuleComponent()->GetComponentLocation();
        const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
        const float SampleRadius = GetCapsuleComponent()->GetScaledCapsuleRadius() * .6f;
        const FVector Offsets[] = {FVector::ZeroVector, FVector(SampleRadius, 0.f, 0.f),
            FVector(-SampleRadius, 0.f, 0.f), FVector(0.f, SampleRadius, 0.f), FVector(0.f, -SampleRadius, 0.f)};
        const float Heights[] = {-HalfHeight + 8.f, -HalfHeight * .22f, HalfHeight * .45f};
        const float Weights[] = {.3f, .4f, .3f};
        for (int32 Level = 0; Level < UE_ARRAY_COUNT(Heights); ++Level)
        {
            for (const FVector& Offset : Offsets)
            {
                if (BlockWorld->GetBlock(ABlockWorld::WorldToBlock(Center + Offset
                    + FVector(0.f, 0.f, Heights[Level]))) == EBlockType::Water)
                {
                    WaterImmersion += Weights[Level] / UE_ARRAY_COUNT(Offsets);
                }
            }
        }
    }
    bInWater = WaterImmersion > .06f;
    // Wading slows land movement. Deeper entry switches to buoyancy; once swimming,
    // keep it until the feet clear the surface so Space can lift the capsule ashore.
    bWaterMovementActive = !bFlightRequested && (bWaterMovementActive
        ? WaterImmersion > .06f : WaterImmersion >= .42f);
    UpdateCaveFillLight();
}

void ABlockRealmCharacter::UpdateCaveFillLight()
{
    // This function still samples authoritative roof/canopy data, but the
    // retained player light components cannot provide any automatic lighting.
    CaveFillTargetIntensity = OutdoorFillTargetIntensity = 0.f;
    CaveFillIntensity = OutdoorFillIntensity = 0.f;
    if (CaveFillLight)
    {
        CaveFillLight->SetIntensity(0.f);
        CaveFillLight->SetVisibility(false, true);
        CaveFillLight->SetHiddenInGame(true, true);
    }
    if (OutdoorNightFillLight)
    {
        OutdoorNightFillLight->SetIntensity(0.f);
        OutdoorNightFillLight->SetVisibility(false, true);
        OutdoorNightFillLight->SetHiddenInGame(true, true);
    }
    const UWorld* World = GetWorld();
    const FVector PlayerLocation = GetActorLocation();
    const bool bValidWorld = IsValid(BlockWorld) && World && !PlayerLocation.ContainsNaN();
    const double Now = World ? World->GetTimeSeconds() : 0.0;
    const FIntVector PreviousSampleCell = LightingSampleCell;
    const bool bPreviousRoof = bLightingRoofOccluded;
    const bool bPreviousMouth = bLightingCaveMouth;
    const bool bPreviousCanopy = bLightingUnderCanopy;
    const FVector EyeLocation = FirstPersonCamera ? FirstPersonCamera->GetComponentLocation() : PlayerLocation;
    const FIntVector PlayerBlock = bValidWorld ? ABlockWorld::WorldToBlock(EyeLocation) : FIntVector::ZeroValue;
    // Moving to another cell updates immediately. The bounded resample interval
    // catches roof edits while stationary without scanning the world each frame.
    if (bValidWorld && (!bHasLightingSample || PlayerBlock != LightingSampleCell || Now >= NextLightingSampleTime))
    {
        const int32 StartZ = FMath::Clamp(PlayerBlock.Z + 1, 0, FVoxelData::WorldHeight);
        const FIntPoint Offsets[] = {FIntPoint::ZeroValue, FIntPoint(1, 0), FIntPoint(-1, 0),
            FIntPoint(0, 1), FIntPoint(0, -1)};
        int32 RoofColumns = 0;
        bool bCenterRoof = false;
        bool bCenterCanopy = false;
        for (int32 Index = 0; Index < UE_ARRAY_COUNT(Offsets); ++Index)
        {
            bool bOpaqueRoof = false;
            bool bCanopy = false;
            for (int32 Z = StartZ; Z < FVoxelData::WorldHeight; ++Z)
            {
                const EBlockType Type = BlockWorld->GetBlock(FIntVector(
                    PlayerBlock.X + Offsets[Index].X, PlayerBlock.Y + Offsets[Index].Y, Z));
                if (Type == EBlockType::Leaves)
                {
                    bCanopy = true;
                    continue;
                }
                // Wood blocks sunlight just like stone, at any elevation. Glass
                // transmits it; foliage is a canopy rather than an enclosed cave.
                if (FVoxelData::IsSolid(Type) && Type != EBlockType::Glass)
                {
                    bOpaqueRoof = true;
                    break;
                }
            }
            RoofColumns += bOpaqueRoof ? 1 : 0;
            if (Index == 0)
            {
                bCenterRoof = bOpaqueRoof;
                bCenterCanopy = bCanopy;
            }
        }
        bLightingRoofOccluded = bCenterRoof;
        bLightingCaveMouth = bCenterRoof && RoofColumns < UE_ARRAY_COUNT(Offsets);
        bLightingUnderCanopy = !bCenterRoof && bCenterCanopy;
        LightingEnclosureWeight = bCenterRoof ? static_cast<float>(RoofColumns) / UE_ARRAY_COUNT(Offsets) : 0.f;
        LightingSampleCell = PlayerBlock;
        NextLightingSampleTime = Now + LightingSampleInterval;
        bHasLightingSample = true;
    }
    else if (!bValidWorld)
    {
        bHasLightingSample = false;
        bLightingRoofOccluded = bLightingCaveMouth = bLightingUnderCanopy = false;
        LightingEnclosureWeight = 0.f;
    }

    LastLightingUpdateTime = Now;
    LastLightingPosition = PlayerLocation;
    static const bool bLightingDiagnostic = FParse::Param(FCommandLine::Get(), TEXT("BRCaveLighting"));
    if (bLightingDiagnostic && CaveFillLight && (PlayerBlock != PreviousSampleCell || bLightingRoofOccluded != bPreviousRoof
        || bLightingCaveMouth != bPreviousMouth || bLightingUnderCanopy != bPreviousCanopy))
    {
        UE_LOG(LogTemp, Display, TEXT("BR_CAVE_LIGHT block=%s roof=%d mouth=%d canopy=%d enclosure=%.2f visible=%d registered=%d hidden=%d intensity=%.2f brightness=%.2f exposureBlend=%.2f radius=%.1f target=%.2f outdoor=%.2f outdoorTarget=%.2f disabled=%d player_fill_disabled=1"),
            *PlayerBlock.ToString(), bLightingRoofOccluded ? 1 : 0, bLightingCaveMouth ? 1 : 0,
            bLightingUnderCanopy ? 1 : 0, LightingEnclosureWeight,
            CaveFillLight->IsVisible() ? 1 : 0, CaveFillLight->IsRegistered() ? 1 : 0,
            CaveFillLight->bHiddenInGame ? 1 : 0, CaveFillLight->Intensity,
            CaveFillLight->ComputeLightBrightness(), CaveFillLight->InverseExposureBlend,
            CaveFillLight->AttenuationRadius, CaveFillTargetIntensity, OutdoorFillIntensity,
            OutdoorFillTargetIntensity, 1);
    }
}

void ABlockRealmCharacter::UpdateMovementState(bool bRunning)
{
    UBlockRealmMovementComponent* Movement = CastChecked<UBlockRealmMovementComponent>(GetCharacterMovement());
    const APlayerController* PC = Cast<APlayerController>(Controller);
    const bool bAcceptInput = CanUseGameplayInput();
    const bool bUp = bAcceptInput && PC && PC->IsInputKeyDown(EKeys::SpaceBar);
    const bool bDown = bAcceptInput && PC && (PC->IsInputKeyDown(EKeys::LeftControl) || PC->IsInputKeyDown(EKeys::RightControl));
    const bool bWasWaterMovement = Movement->IsInVoxelWaterMode();
    const bool bWaterSlowsMovement = bInWater && !bFlightRequested;
    const float SurvivalSpeedScale = Hunger <= 20.f ? .65f : 1.f;
    Movement->GravityScale = LandGravityScale;
    Movement->bCheatFlying = false;
    Movement->MaxWalkSpeed = (bWaterSlowsMovement ? (bRunning ? FastWaterSpeed : WaterSpeed)
        : (bRunning ? RunSpeed : WalkSpeed)) * SurvivalSpeedScale;
    Movement->MaxFlySpeed = (bRunning ? FastFlySpeed : FlySpeed) * SurvivalSpeedScale;
    Movement->MaxAcceleration = (bWaterSlowsMovement ? 1050.f : 2700.f) * SurvivalSpeedScale;
    Movement->SetWaterInput((bUp ? 1.f : 0.f) - (bDown ? 1.f : 0.f),
        (bRunning ? FastWaterSpeed : WaterSpeed) * SurvivalSpeedScale);
    if (bWaterSlowsMovement)
    {
        const FVector Horizontal = FVector(Movement->Velocity.X, Movement->Velocity.Y, 0.f)
            .GetClampedToMaxSize(Movement->MaxWalkSpeed);
        Movement->Velocity.X = Horizontal.X;
        Movement->Velocity.Y = Horizontal.Y;
    }
    if (bFlightRequested)
    {
        if (Movement->MovementMode != MOVE_Flying)
        {
            Movement->SetMovementMode(MOVE_Flying);
        }
    }
    else if (bWaterMovementActive)
    {
        StopJumping();
        if (!bWasWaterMovement)
        {
            Movement->SetMovementMode(MOVE_Custom, UBlockRealmMovementComponent::VoxelWaterMode);
            Movement->Velocity.Z = FMath::Clamp(Movement->Velocity.Z, -240.f, 250.f);
        }
    }
    else if (Movement->MovementMode == MOVE_Flying || Movement->MovementMode == MOVE_Swimming
        || Movement->MovementMode == MOVE_None || bWasWaterMovement)
    {
        // No physics volume or other caller can silently turn ordinary play into flight.
        SetActorEnableCollision(true);
        GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Movement->SetMovementMode(MOVE_Falling);
        if (bWasWaterMovement && bUp)
        {
            Movement->Velocity.Z = FMath::Max(Movement->Velocity.Z, Movement->JumpZVelocity);
        }
    }
}

void ABlockRealmCharacter::Turn(float Value)
{
    if (CanUseGameplayInput())
    {
        AddControllerYawInput(Value * .85f);
    }
}

void ABlockRealmCharacter::LookUp(float Value)
{
    if (CanUseGameplayInput())
    {
        AddControllerPitchInput(-Value * .85f);
    }
}

void ABlockRealmCharacter::BeginJump()
{
    if (CanUseGameplayInput() && !bFlightRequested && !bWaterMovementActive)
    {
        Jump();
    }
}

void ABlockRealmCharacter::EndJump()
{
    StopJumping();
}

bool ABlockRealmCharacter::IsFlying() const
{
    return bFlightRequested && GetCharacterMovement()->MovementMode == MOVE_Flying;
}

void ABlockRealmCharacter::ToggleFlight()
{
    if (!CanUseGameplayInput() || bFlightKeyHeld)
    {
        return;
    }
    bFlightKeyHeld = true;
    bFlightRequested = !bFlightRequested;
    StopJumping();
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->bCheatFlying = false;
    SetActorEnableCollision(true);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    UpdateWaterState();
    UpdateMovementState(false);
    SetInteractionNotice(bFlightRequested ? TEXT("飞行已开启 · 空格上升，Ctrl 下降")
        : bInWater ? TEXT("飞行已关闭 · 水中空格上浮，Ctrl 下潜") : TEXT("飞行已关闭 · 已恢复重力"));
}

void ABlockRealmCharacter::FlightReleased()
{
    bFlightKeyHeld = false;
}

void ABlockRealmCharacter::RefreshTarget()
{
    bHasTarget = false;
    if (!BlockWorld || !CanUseGameplayInput())
    {
        return;
    }
    const FVector EyeLocation = FirstPersonCamera->GetComponentLocation();
    const FRotator EyeRotation = Controller ? Controller->GetControlRotation() : FirstPersonCamera->GetComponentRotation();
    bHasTarget = BlockWorld->TraceBlocks(EyeLocation, EyeRotation.Vector(), InteractionReach, TargetBlock, PlacementBlock);
}

EBlockType ABlockRealmCharacter::GetTargetType() const
{
    return bHasTarget && BlockWorld ? BlockWorld->GetBlock(TargetBlock) : EBlockType::Air;
}

EBlockType ABlockRealmCharacter::GetSelectedType() const
{
    return static_cast<EBlockType>(SelectedSlot + 1);
}

bool ABlockRealmCharacter::IsBlockEquipped() const
{
    return !bBareHandsEquipped && SelectedToolSlot == INDEX_NONE;
}

FString ABlockRealmCharacter::GetHeldItemName() const
{
    if (IsBlockEquipped()) return BlockDisplayName(GetSelectedType());
    if (GetSelectedToolType() == EBlockRealmToolType::None) return TEXT("空手");
    return FBlockRealmSurvivalData::GetToolDisplayName(GetSelectedToolType());
}

int32 ABlockRealmCharacter::GetFoodCount() const
{
    return PlayerActions ? PlayerActions->GetFoodCount() : 0;
}

bool ABlockRealmCharacter::UseInventoryFood()
{
    if (!CanPlay() || !HasGameplayFocus() || !bInventoryOpen || !PlayerActions) return false;
    if (Health >= MaxHealth && Hunger >= MaxHunger)
    {
        SetInteractionNotice(TEXT("生命和饥饿已满，无需进食。"));
        return false;
    }
    if (PlayerActions->GetFoodCount() <= 0)
    {
        SetInteractionNotice(TEXT("没有食物，采集树叶可以补充。"));
        return false;
    }
    // Let the ordinary food action run after the click is released. This uses
    // exactly the same consumption and restoration rules as the C key.
    ToggleInventory();
    bPendingInventoryFood = true;
    if (bGameplayInputReady)
    {
        bPendingInventoryFood = false;
        return PlayerActions->TryEat();
    }
    return true;
}

int32 ABlockRealmCharacter::GetInventoryCount(int32 Slot) const
{
    return Inventory.IsValidIndex(Slot) && Slot != WaterSlot ? Inventory[Slot] : 0;
}

EBlockRealmToolType ABlockRealmCharacter::GetSelectedToolType() const
{
    return ToolSlots.IsValidIndex(SelectedToolSlot) && ToolSlots[SelectedToolSlot].IsUsable()
        ? ToolSlots[SelectedToolSlot].Type
        : EBlockRealmToolType::None;
}

FString ABlockRealmCharacter::GetSelectedToolName() const
{
    return FBlockRealmSurvivalData::GetToolDisplayName(GetSelectedToolType());
}

int32 ABlockRealmCharacter::GetSelectedToolDurability() const
{
    return ToolSlots.IsValidIndex(SelectedToolSlot) ? FMath::Max(0, ToolSlots[SelectedToolSlot].Durability) : 0;
}

int32 ABlockRealmCharacter::GetSelectedToolMaxDurability() const
{
    return ToolSlots.IsValidIndex(SelectedToolSlot) ? FMath::Max(0, ToolSlots[SelectedToolSlot].MaxDurability) : 0;
}

bool ABlockRealmCharacter::GetToolSlot(int32 Slot, FBlockRealmToolSlot& OutSlot) const
{
    if (!ToolSlots.IsValidIndex(Slot))
    {
        OutSlot = FBlockRealmToolSlot();
        return false;
    }
    OutSlot = ToolSlots[Slot];
    return true;
}

bool ABlockRealmCharacter::SelectToolSlot(int32 Slot)
{
    if (!CanPlay() || !HasGameplayFocus() || !ToolSlots.IsValidIndex(Slot))
    {
        return false;
    }
    if (!ToolSlots[Slot].IsUsable())
    {
        SetInteractionNotice(TEXT("这个工具槽为空或已经损坏。"));
        return false;
    }
    CancelMining();
    CancelPlacement();
    SelectedToolSlot = Slot;
    bBareHandsEquipped = false;
    RefreshHeldItemVisual();
    SetInteractionNotice(FString::Printf(TEXT("已装备 %s · 0 空手，数字键切换方块"), *GetHeldItemName()));
    if (SlotSound) UGameplayStatics::PlaySound2D(this, SlotSound, .4f);
    return true;
}

bool ABlockRealmCharacter::EquipBareHands()
{
    if (!CanPlay() || !HasGameplayFocus()) return false;
    CancelMining();
    CancelPlacement();
    SelectedToolSlot = INDEX_NONE;
    bBareHandsEquipped = true;
    RefreshHeldItemVisual();
    SetInteractionNotice(TEXT("已切换空手 · 数字键选择方块，Q / R 切换工具"));
    if (SlotSound) UGameplayStatics::PlaySound2D(this, SlotSound, .4f);
    return true;
}

void ABlockRealmCharacter::CycleTool(int32 Direction)
{
    if (!CanUseGameplayInput() || ToolSlots.Num() == 0) return;
    const int32 CycleCount = ToolSlots.Num() + 1;
    int32 Cursor = SelectedToolSlot == INDEX_NONE ? 0 : SelectedToolSlot + 1;
    for (int32 Step = 0; Step < CycleCount; ++Step)
    {
        Cursor = (Cursor + Direction + CycleCount) % CycleCount;
        if (Cursor == 0)
        {
            EquipBareHands();
            return;
        }
        const int32 Slot = Cursor - 1;
        if (ToolSlots[Slot].IsUsable())
        {
            SelectToolSlot(Slot);
            return;
        }
    }
    EquipBareHands();
}

void ABlockRealmCharacter::BareHandsPressed()
{
    if (CanUseGameplayInput()) EquipBareHands();
}

void ABlockRealmCharacter::PreviousToolPressed()
{
    CycleTool(-1);
}

void ABlockRealmCharacter::NextToolPressed()
{
    CycleTool(1);
}

const TArray<FBlockRealmSurvivalRecipe>& ABlockRealmCharacter::GetCraftingRecipes() const
{
    return FBlockRealmSurvivalData::GetCraftingRecipes();
}

bool ABlockRealmCharacter::TryCraftRecipe(int32 RecipeIndex)
{
    // Crafting is an inventory action, so it remains available while the E
    // overlay is open; ordinary world input stays blocked by CanUseGameplayInput.
    if (!CanPlay() || !HasGameplayFocus()) return false;
    FString Result;
    const bool bCrafted = FBlockRealmSurvivalData::TryCraft(RecipeIndex, Inventory, ToolSlots,
        CraftedPlanks, &Result);
    if (!Result.IsEmpty()) SetInteractionNotice(Result);
    if (bCrafted)
    {
        // A newly crafted tool is selectable immediately by the HUD without
        // changing the legacy block selection used by placement.
        CancelMining();
        CancelPlacement();
        RefreshHeldItemVisual();
    }
    return bCrafted;
}

void ABlockRealmCharacter::ToggleInventory()
{
    if (!CanPlay() || !HasGameplayFocus()) return;
    const bool bWasOpen = bInventoryOpen;
    bInventoryOpen = !bInventoryOpen;
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (bInventoryOpen)
    {
        bPendingInventoryFood = false;
        InventoryMovementMode = static_cast<uint8>(Movement->MovementMode);
        InventoryCustomMovementMode = Movement->CustomMovementMode;
    }
    CancelGameplayInput();
    if (bInventoryOpen)
    {
        Movement->SetMovementMode(MOVE_None);
    }
    else
    {
        Movement->SetMovementMode(static_cast<EMovementMode>(InventoryMovementMode), InventoryCustomMovementMode);
        // Closing the overlay is a gameplay boundary. Re-arm immediately when
        // the menu click has already been released; otherwise the normal Tick
        // gate waits for release and world edits remain protected.
        const APlayerController* PC = Cast<APlayerController>(Controller);
        const bool bMouseReleased = !PC
            || (!PC->IsInputKeyDown(EKeys::LeftMouseButton)
                && !PC->IsInputKeyDown(EKeys::RightMouseButton));
        bGameplayInputReady = bWasOpen && bMouseReleased;
    }
    SetInteractionNotice(bInventoryOpen ? TEXT("背包已打开 · E 关闭") : TEXT("背包已关闭"));
}

void ABlockRealmCharacter::ToggleInventoryPressed()
{
    ToggleInventory();
}

void ABlockRealmCharacter::SelectSlot(int32 Slot)
{
    if (CanUseGameplayInput()) SelectInventoryBlockSlot(Slot);
}

bool ABlockRealmCharacter::SelectInventoryBlockSlot(int32 Slot)
{
    if (!CanPlay() || !HasGameplayFocus() || Slot < 0 || Slot >= SlotCount)
    {
        return false;
    }
    if (Slot == WaterSlot)
    {
        SetInteractionNotice(TEXT("水面随地形生成，不能放置。"));
        return false;
    }
    if (SelectedSlot != Slot || !IsBlockEquipped())
    {
        CancelMining();
        CancelPlacement();
        if (SlotSound)
        {
            UGameplayStatics::PlaySound2D(this, SlotSound, .4f);
        }
    }
    SelectedSlot = Slot;
    SelectedToolSlot = INDEX_NONE;
    bBareHandsEquipped = false;
    RefreshHeldItemVisual();
    SetInteractionNotice(FString::Printf(TEXT("手持 %s ×%d · 右键放置"),
        *BlockDisplayName(GetSelectedType()), GetInventoryCount(SelectedSlot)));
    return true;
}

void ABlockRealmCharacter::CycleSlot(int32 Direction)
{
    if (!CanUseGameplayInput())
    {
        return;
    }
    int32 Slot = SelectedSlot;
    do
    {
        Slot = (Slot + Direction + SlotCount) % SlotCount;
    }
    while (Slot == WaterSlot);
    SelectSlot(Slot);
}

void ABlockRealmCharacter::NextSlot()
{
    CycleSlot(1);
}

void ABlockRealmCharacter::PreviousSlot()
{
    CycleSlot(-1);
}

void ABlockRealmCharacter::MinePressed()
{
    if (bGameplayInputReady && CanUseGameplayInput() && !bMineHeld)
    {
        if (FirstPersonCamera && Controller)
        {
            // Match the existing combat action's 350 cm visibility trace.
            // A voxel check also excludes walls with collision still streaming.
            const FVector Origin = FirstPersonCamera->GetComponentLocation();
            const FVector Direction = Controller->GetControlRotation().Vector();
            FHitResult CreatureHit;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmLeftClickCreature), false, this);
            if (PlayerActions && GetWorld()->LineTraceSingleByChannel(CreatureHit, Origin,
                Origin + Direction * 350.f, ECC_Visibility, Query))
            {
                ABlockRealmMob* Creature = Cast<ABlockRealmMob>(CreatureHit.GetActor());
                FIntVector WallBlock, WallAdjacent;
                if (IsValid(Creature) && !Creature->IsDead() && (!BlockWorld
                    || !BlockWorld->TraceBlocks(Origin, Direction, CreatureHit.Distance, WallBlock, WallAdjacent)))
                {
                    CancelMining();
                    PlayerActions->TryAttack();
                    return;
                }
            }
            if (ABlockRealmTorch* Torch = ABlockRealmTorch::FindAimedTorch(GetWorld(), BlockWorld,
                FirstPersonCamera->GetComponentLocation(), Controller->GetControlRotation().Vector(), InteractionReach))
            {
                CancelMining();
                if (AddTorchItem(1))
                {
                    Torch->Destroy();
                    SetInteractionNotice(TEXT("已回收火把 +1。"));
                }
                else SetInteractionNotice(TEXT("工具栏空间不足，先腾出火把位置。"));
                return;
            }
        }
        bMineHeld = true;
        CancelMining();
    }
}

void ABlockRealmCharacter::MineReleased()
{
    bMineHeld = false;
    CancelMining();
}

void ABlockRealmCharacter::PlacePressed()
{
    static const bool bSmokeInput = FParse::Param(FCommandLine::Get(), TEXT("BlockRealmSmoke"));
    if (bGameplayInputReady && CanUseGameplayInput() && !bPlaceHeld)
    {
        bPlaceHeld = true;
        // Retain the requested cell: success refreshes the target to the new block.
        const FIntVector RequestedPosition = PlacementBlock;
        const bool bPlaced = PlaceTarget();
        if (bSmokeInput)
        {
            UE_LOG(LogTemp, Display, TEXT("BR_PLACEMENT input=accepted immediate=%d %s"),
                bPlaced, *DescribePlacementState(RequestedPosition));
        }
    }
    else if (bSmokeInput)
    {
        UE_LOG(LogTemp, Display, TEXT("BR_PLACEMENT input=rejected %s"),
            *DescribePlacementState(PlacementBlock));
    }
}

void ABlockRealmCharacter::PlaceReleased()
{
    bPlaceHeld = false;
    // Preserve only the short collision retry for a click just before jumping.
}

void ABlockRealmCharacter::CancelMining()
{
    bMiningActive = false;
    MiningElapsed = 0.f;
    MiningDuration = 0.f;
    MiningType = EBlockType::Air;
}

float ABlockRealmCharacter::GetMiningProgress() const
{
    return bMiningActive && MiningDuration > 0.f ? FMath::Clamp(MiningElapsed / MiningDuration, 0.f, 1.f) : 0.f;
}

float ABlockRealmCharacter::GetCurrentMiningDuration(EBlockType Type) const
{
    return FBlockRealmSurvivalData::GetMiningDuration(Type, GetSelectedToolType());
}

void ABlockRealmCharacter::TickMining(float DeltaSeconds)
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    if (!bMineHeld || !bGameplayInputReady || !PC || !PC->IsInputKeyDown(EKeys::LeftMouseButton))
    {
        MineReleased();
        return;
    }
    const EBlockType Type = GetTargetType();
    FString MiningReason;
    if (bHasTarget && !FBlockRealmSurvivalData::CanMineBlock(Type, GetSelectedToolType(), &MiningReason))
    {
        SetInteractionNotice(MiningReason);
        CancelMining();
        return;
    }
    const float Duration = GetCurrentMiningDuration(Type);
    if (!bHasTarget || Duration <= 0.f)
    {
        if (bHasTarget && GetSelectedToolType() == EBlockRealmToolType::Torch)
        {
            SetInteractionNotice(TEXT("火把不能挖掘方块。"));
        }
        CancelMining();
        return;
    }
    if (!bMiningActive || MiningBlock != TargetBlock || MiningType != Type)
    {
        CancelMining();
        MiningBlock = TargetBlock;
        MiningType = Type;
        MiningDuration = Duration;
        bMiningActive = true;
        return;
    }
    MiningElapsed += FMath::Max(0.f, DeltaSeconds);
    if (MiningElapsed >= MiningDuration)
    {
        MineTarget();
    }
}

bool ABlockRealmCharacter::MineTarget()
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    if (!CanUseGameplayInput() || !BlockWorld || !bGameplayInputReady || !bMineHeld
        || !PC || !PC->IsInputKeyDown(EKeys::LeftMouseButton) || !bMiningActive
        || MiningDuration <= 0.f || MiningElapsed < MiningDuration)
    {
        return false;
    }
    RefreshTarget();
    if (!bHasTarget || TargetBlock != MiningBlock || GetTargetType() != MiningType)
    {
        CancelMining();
        return false;
    }
    const EBlockType Type = BlockWorld->GetBlock(TargetBlock);
    FString MiningReason;
    if (!FBlockRealmSurvivalData::CanMineBlock(Type, GetSelectedToolType(), &MiningReason))
    {
        SetInteractionNotice(MiningReason);
        CancelMining();
        return false;
    }
    const int32 Slot = static_cast<int32>(Type) - 1;
    if (!Inventory.IsValidIndex(Slot) || Type == EBlockType::Water)
    {
        CancelMining();
        return false;
    }
    if (Inventory[Slot] >= 1000000)
    {
        SetInteractionNotice(TEXT("这类方块的背包空间已满。"));
        CancelMining();
        return false;
    }
    const FVector Location = ABlockWorld::BlockToWorld(TargetBlock);
    if (!BlockWorld->SetBlock(TargetBlock, EBlockType::Air))
    {
        SetInteractionNotice(TargetBlock.Z == 0 ? TEXT("地基岩石不能挖掘。") : TEXT("这块方块暂时不能采集。"));
        CancelMining();
        return false;
    }
    ++Inventory[Slot];
    RefreshHeldItemVisual();
    if (PlayerActions) PlayerActions->OnBlockMined(Type);
    ConsumeToolDurability();
    Hunger = FMath::Clamp(Hunger - .45f, 0.f, MaxHunger);
    USoundBase* Sound = MineSound;
    if (Type == EBlockType::Stone || Type == EBlockType::Glass || Type == EBlockType::Glow)
    {
        Sound = MineStoneSound ? MineStoneSound.Get() : MineSound.Get();
    }
    else if (Type == EBlockType::Wood || Type == EBlockType::Leaves)
    {
        Sound = MineWoodSound ? MineWoodSound.Get() : MineSound.Get();
    }
    if (Sound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, Sound, Location, .65f, FMath::FRandRange(.94f, 1.06f));
    }
    CancelMining();
    RefreshTarget();
    return true;
}

void ABlockRealmCharacter::ConsumeToolDurability()
{
    if (!ToolSlots.IsValidIndex(SelectedToolSlot)) return;
    FBlockRealmToolSlot& Tool = ToolSlots[SelectedToolSlot];
    if (Tool.Type == EBlockRealmToolType::Torch || Tool.Durability <= 0) return;
    --Tool.Durability;
    if (Tool.Durability <= 0)
    {
        const FString Name = FBlockRealmSurvivalData::GetToolDisplayName(Tool.Type);
        Tool = FBlockRealmToolSlot();
        SelectedToolSlot = INDEX_NONE;
        bBareHandsEquipped = true;
        RefreshHeldItemVisual();
        CancelMining();
        SetInteractionNotice(FString::Printf(TEXT("%s 已损坏，已切回徒手。"), *Name));
    }
}

bool ABlockRealmCharacter::DoesBlockIntersectPlayer(const FIntVector& Position) const
{
    const FVector BlockCenter = ABlockWorld::BlockToWorld(Position);
    const FVector Center = GetCapsuleComponent()->GetComponentLocation();
    const double Radius = GetCapsuleComponent()->GetScaledCapsuleRadius() + 2.0;
    const double SegmentHalf = FMath::Max(0.0,
        static_cast<double>(GetCapsuleComponent()->GetScaledCapsuleHalfHeight()) - Radius + 2.0);
    const double HalfBlock = BlockSize * .5;
    const double DX = FMath::Max(0.0, FMath::Abs(Center.X - BlockCenter.X) - HalfBlock);
    const double DY = FMath::Max(0.0, FMath::Abs(Center.Y - BlockCenter.Y) - HalfBlock);
    const double DZ = FMath::Max(0.0, FMath::Abs(Center.Z - BlockCenter.Z) - SegmentHalf - HalfBlock);
    return DX * DX + DY * DY + DZ * DZ <= Radius * Radius;
}

FString ABlockRealmCharacter::DescribePlacementState(const FIntVector& Position) const
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    const UCapsuleComponent* Capsule = GetCapsuleComponent();
    const FVector Center = Capsule->GetComponentLocation();
    const FVector BlockCenter = ABlockWorld::BlockToWorld(Position);
    const UCharacterMovementComponent* Movement = GetCharacterMovement();
    return FString::Printf(TEXT("cell=%s occupant=%d slot=%d count=%d tool=%d target=%s adjacent=%s has_target=%d"
        " play=%d focus=%d ready=%d inventory_open=%d right_down=%d held=%d pending=%d collision_blocked=%d"
        " buffer=%.3f cooldown=%.3f intersects=%d feet_gap=%.2f capsule=%s velocity=%s water=%d mode=%d notice=\"%s\""),
        *Position.ToString(), BlockWorld ? static_cast<int32>(BlockWorld->GetBlock(Position)) : -1,
        SelectedSlot, GetInventoryCount(SelectedSlot), static_cast<int32>(GetSelectedToolType()),
        *TargetBlock.ToString(), *PlacementBlock.ToString(), bHasTarget, CanPlay(), HasGameplayFocus(),
        bGameplayInputReady, bInventoryOpen, PC && PC->IsInputKeyDown(EKeys::RightMouseButton),
        bPlaceHeld, bPendingPlacement, bPlacementCollisionBlocked, PlacementBufferRemaining, PlacementCooldown,
        DoesBlockIntersectPlayer(Position), Center.Z - Capsule->GetScaledCapsuleHalfHeight() - BlockCenter.Z - BlockSize * .5,
        *Center.ToCompactString(), *Movement->Velocity.ToCompactString(), bInWater,
        static_cast<int32>(Movement->MovementMode), *GetInteractionNotice());
}

bool ABlockRealmCharacter::PlaceTarget()
{
    CancelPlacement();
    if (!CanUseGameplayInput() || !BlockWorld)
    {
        return false;
    }
    RefreshTarget();
    if (!bHasTarget)
    {
        SetInteractionNotice(TEXT("准星需要指向 7 米内的实体方块。"));
        PlacementCooldown = PlacementRepeatInterval;
        return false;
    }
    if (GetSelectedToolType() == EBlockRealmToolType::Torch)
    {
        if (!CanUsePlayerActions() || !ToolSlots.IsValidIndex(SelectedToolSlot)
            || ToolSlots[SelectedToolSlot].Quantity <= 0) return false;
        ABlockRealmTorch* Torch = ABlockRealmTorch::TryPlace(GetWorld(), BlockWorld, PlacementBlock, TargetBlock);
        PlacementCooldown = PlacementRepeatInterval;
        if (!Torch)
        {
            SetInteractionNotice(TEXT("火把需要空位置和相邻的实体支撑。"));
            return false;
        }
        FBlockRealmToolSlot& Slot = ToolSlots[SelectedToolSlot];
        if (--Slot.Quantity <= 0)
        {
            Slot = FBlockRealmToolSlot();
            SelectedToolSlot = INDEX_NONE;
            bBareHandsEquipped = true;
            bPlaceHeld = false;
        }
        RefreshHeldItemVisual();
        SetInteractionNotice(TEXT("已放置火把。"));
        return true;
    }
    const FIntVector Position = PlacementBlock;
    const bool bPlaced = TryPlaceBlock(Position, SelectedSlot);
    if (bPlaced)
    {
        PlacementCooldown = PlacementRepeatInterval;
        RefreshTarget();
    }
    else if (bPlacementCollisionBlocked)
    {
        bPendingPlacement = true;
        PendingPlacementBlock = Position;
        PendingPlacementSlot = SelectedSlot;
        PlacementBufferRemaining = PlacementCollisionBuffer;
    }
    else
    {
        PlacementCooldown = PlacementRepeatInterval;
    }
    return bPlaced;
}

void ABlockRealmCharacter::CancelPlacement()
{
    bPendingPlacement = false;
    bPlacementCollisionBlocked = false;
    PlacementBufferRemaining = 0.f;
    PlacementCooldown = 0.f;
}

void ABlockRealmCharacter::TickPlacement(float DeltaSeconds)
{
    const APlayerController* PC = Cast<APlayerController>(Controller);
    if (!PC || !PC->IsInputKeyDown(EKeys::RightMouseButton))
    {
        bPlaceHeld = false;
    }
    PlacementCooldown = FMath::Max(0.f, PlacementCooldown - DeltaSeconds);
    if (!bGameplayInputReady)
    {
        CancelPlacement();
        return;
    }
    if (bPendingPlacement)
    {
        PlacementBufferRemaining -= DeltaSeconds;
        if (PlacementBufferRemaining <= 0.f || !bHasTarget || PlacementBlock != PendingPlacementBlock
            || SelectedSlot != PendingPlacementSlot)
        {
            CancelPlacement();
        }
        else
        {
            if (TryPlaceBlock(PendingPlacementBlock, PendingPlacementSlot))
            {
                CancelPlacement();
                PlacementCooldown = PlacementRepeatInterval;
                RefreshTarget();
            }
            else if (!bPlacementCollisionBlocked)
            {
                CancelPlacement();
                PlacementCooldown = PlacementRepeatInterval;
            }
            return;
        }
    }
    if (bPlaceHeld && PlacementCooldown <= 0.f)
    {
        PlaceTarget();
    }
}

bool ABlockRealmCharacter::TryPlaceBlock(const FIntVector& Position, int32 Slot)
{
    bPlacementCollisionBlocked = false;
    if (!IsBlockEquipped())
    {
        SetInteractionNotice(bBareHandsEquipped ? TEXT("空手没有可放置的物品，按数字键选择方块。")
            : TEXT("先切回方块槽位再放置。"));
        return false;
    }
    if (!CanUseGameplayInput() || !BlockWorld || !Inventory.IsValidIndex(Slot) || Slot == WaterSlot)
    {
        return false;
    }
    if (GetInventoryCount(Slot) <= 0)
    {
        SetInteractionNotice(TEXT("这类方块用完了，先去采集一些。"));
        return false;
    }
    const EBlockType Occupant = BlockWorld->GetBlock(Position);
    if (Occupant != EBlockType::Air && Occupant != EBlockType::Water)
    {
        SetInteractionNotice(TEXT("这里已经有方块。"));
        return false;
    }
    if (DoesBlockIntersectPlayer(Position))
    {
        bPlacementCollisionBlocked = true;
        SetInteractionNotice(TEXT("跳起或离开这块空间后再放置。"));
        return false;
    }
    const FVector Location = ABlockWorld::BlockToWorld(Position);
    if (!BlockWorld->SetBlock(Position, static_cast<EBlockType>(Slot + 1)))
    {
        SetInteractionNotice(TEXT("这块空间暂时不能建造。"));
        return false;
    }
    --Inventory[Slot];
    if (Inventory[Slot] == 0) RefreshHeldItemVisual();
    if (PlaceSound)
    {
        UGameplayStatics::PlaySoundAtLocation(this, PlaceSound, Location, .6f, FMath::FRandRange(.96f, 1.04f));
    }
    if (InteractionNotice == TEXT("跳起或离开这块空间后再放置。"))
    {
        InteractionNotice.Empty();
        InteractionNoticeUntil = 0.0;
    }
    return true;
}

FWorldSaveState ABlockRealmCharacter::CaptureSaveState() const
{
    FWorldSaveState State;
    State.Position = GetActorLocation();
    State.Rotation = Controller ? Controller->GetControlRotation()
        : bHasPendingRestoreView ? PendingRestoreView : GetActorRotation();
    State.SelectedSlot = SelectedSlot;
    State.Inventory = Inventory;
    State.bHasSurvivalState = true;
    State.Health = Health;
    State.Hunger = Hunger;
    State.CraftedPlanks = CraftedPlanks;
    State.FoodCount = PlayerActions ? PlayerActions->CaptureFoodCount() : 6;
    State.SelectedToolSlot = SelectedToolSlot;
    State.bBareHandsEquipped = bBareHandsEquipped;
    for (const FBlockRealmToolSlot& Slot : ToolSlots)
    {
        FBlockRealmSavedTool SavedTool;
        SavedTool.Type = static_cast<int32>(Slot.Type);
        SavedTool.Durability = Slot.Durability;
        SavedTool.MaxDurability = Slot.MaxDurability;
        SavedTool.Quantity = Slot.Quantity;
        State.ToolSlots.Add(SavedTool);
    }
    // The respawn anchor is owned by GameMode because it points at a world
    // object.  Mirroring it into this aggregate keeps smoke checks and legacy
    // callers consistent without moving ownership into the player actor.
    if (const ABlockRealmGameMode* Game = GetRealmMode(this))
    {
        State.bHasRespawnPoint = Game->HasRespawnPoint();
        State.RespawnPoint = State.bHasRespawnPoint
            ? Game->GetRespawnPoint() : FIntVector::ZeroValue;
    }
    return State;
}

void ABlockRealmCharacter::RestoreSaveState(const FWorldSaveState& State)
{
    ResetGameplayState();
    Inventory.Init(64, SlotCount);
    for (int32 Slot = 0; Slot < FMath::Min(SlotCount, State.Inventory.Num()); ++Slot)
    {
        Inventory[Slot] = FMath::Max(0, State.Inventory[Slot]);
    }
    Inventory[WaterSlot] = 0;
    if (State.bHasSurvivalState && State.ToolSlots.Num() == FBlockRealmSurvivalData::ToolSlotCount)
    {
        Health = FMath::Clamp(State.Health, 1.f, MaxHealth);
        Hunger = FMath::Clamp(State.Hunger, 0.f, MaxHunger);
        CraftedPlanks = State.CraftedPlanks;
        SelectedToolSlot = State.SelectedToolSlot;
        bBareHandsEquipped = State.bBareHandsEquipped;
        for (int32 Index = 0; Index < ToolSlots.Num(); ++Index)
        {
            const FBlockRealmSavedTool& SavedTool = State.ToolSlots[Index];
            FBlockRealmToolSlot& Slot = ToolSlots[Index];
            Slot.Type = static_cast<EBlockRealmToolType>(SavedTool.Type);
            Slot.Durability = SavedTool.Durability;
            Slot.MaxDurability = SavedTool.MaxDurability;
            Slot.Quantity = SavedTool.Quantity;
        }
        if (PlayerActions) PlayerActions->SetFoodCount(State.FoodCount);
    }
    SelectedSlot = State.SelectedSlot >= 0 && State.SelectedSlot < SlotCount
        && State.SelectedSlot != WaterSlot ? State.SelectedSlot : 0;
    if (bBareHandsEquipped || !ToolSlots.IsValidIndex(SelectedToolSlot) || !ToolSlots[SelectedToolSlot].IsUsable())
    {
        SelectedToolSlot = INDEX_NONE;
    }
    RefreshHeldItemVisual();
    StopJumping();
    ConsumeMovementInputVector();
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    const FVector Position = State.Position.ContainsNaN()
        ? (BlockWorld ? BlockWorld->GetSpawnLocation() : GetActorLocation()) : State.Position;
    const FRotator SavedRotation = State.Rotation.ContainsNaN() ? FRotator::ZeroRotator : State.Rotation;
    PendingRestoreView = FRotator(FMath::Clamp(FMath::UnwindDegrees(SavedRotation.Pitch), -89.f, 89.f),
        FMath::UnwindDegrees(SavedRotation.Yaw), 0.f);
    SetActorLocationAndRotation(Position, FRotator(0.f, PendingRestoreView.Yaw, 0.f),
        false, nullptr, ETeleportType::TeleportPhysics);
    bHasPendingRestoreView = !Controller;
    if (Controller)
    {
        Controller->SetControlRotation(PendingRestoreView);
    }
    if (BlockWorld)
    {
        BlockWorld->RequestStreaming(Position);
    }
    // Restore can be invoked while movement ticking is paused (title/fixture
    // setup). Refresh the authoritative water and local fill-light state now
    // so the first frame after a teleport does not retain the old location's
    // lighting or immersion flags. GameMode remains the owner of respawn beds.
    UpdateWaterState();
    bHasTarget = false;
    bGameplayInputReady = false;
    InteractionNotice.Empty();
    InteractionNoticeUntil = 0.0;
}

void ABlockRealmCharacter::ResetSurvivalState()
{
    if (PlayerActions) PlayerActions->ResetState();
    DamageFeedbackUntil = 0.0;
    MaxHealth = 100.f;
    MaxHunger = 100.f;
    Health = MaxHealth;
    Hunger = MaxHunger;
    HungerDrainAccumulator = 0.f;
    StarvationAccumulator = 0.f;
    RegenerationAccumulator = 0.f;
    CraftedPlanks = 0;
    SelectedToolSlot = INDEX_NONE;
    bBareHandsEquipped = false;
    bPendingInventoryFood = false;
    if (ToolSlots.Num() != FBlockRealmSurvivalData::ToolSlotCount)
    {
        ToolSlots.Init(FBlockRealmToolSlot(), FBlockRealmSurvivalData::ToolSlotCount);
    }
    else
    {
        for (FBlockRealmToolSlot& Slot : ToolSlots) Slot = FBlockRealmToolSlot();
    }
    // A fresh character has a modest starter kit so the survival loop is
    // playable immediately.  Additional tools are craftable into empty slots.
    ToolSlots[0].Type = EBlockRealmToolType::WoodenPickaxe;
    ToolSlots[0].Durability = FBlockRealmSurvivalData::WoodenPickaxeDurability;
    ToolSlots[0].MaxDurability = FBlockRealmSurvivalData::WoodenPickaxeDurability;
    ToolSlots[0].Quantity = 1;
    ToolSlots[2].Type = EBlockRealmToolType::Torch;
    ToolSlots[2].Quantity = 4;
    SelectedToolSlot = INDEX_NONE;
    RefreshHeldItemVisual();
}

void ABlockRealmCharacter::TickSurvival(float DeltaSeconds, bool bRunning)
{
    const float SafeDelta = FMath::Clamp(DeltaSeconds, 0.f, .25f);
    if (SafeDelta <= 0.f || Health <= 0.f) return;
    const UCharacterMovementComponent* Movement = GetCharacterMovement();
    const bool bMoving = Movement && !Movement->Velocity.IsNearlyZero(30.f);
    float DrainRate = .025f;
    if (bRunning && bMoving) DrainRate += .12f;
    if (bMiningActive) DrainRate += .04f;
    HungerDrainAccumulator += DrainRate * SafeDelta;
    if (HungerDrainAccumulator >= .1f)
    {
        Hunger = FMath::Clamp(Hunger - HungerDrainAccumulator, 0.f, MaxHunger);
        HungerDrainAccumulator = 0.f;
    }

    if (Hunger <= 0.f)
    {
        StarvationAccumulator += SafeDelta;
        if (StarvationAccumulator >= 2.f)
        {
            StarvationAccumulator = 0.f;
            ApplySurvivalDamage(1.f, TEXT("饥饿"));
        }
    }
    else
    {
        StarvationAccumulator = 0.f;
    }

    if (Hunger >= 80.f && Health < MaxHealth)
    {
        RegenerationAccumulator += SafeDelta;
        if (RegenerationAccumulator >= 8.f)
        {
            RegenerationAccumulator = 0.f;
            RestoreSurvivalHealth(1.f);
            Hunger = FMath::Max(0.f, Hunger - 1.f);
        }
    }
    else
    {
        RegenerationAccumulator = 0.f;
    }
}

void ABlockRealmCharacter::ApplySurvivalDamage(float Amount, const FString& Reason)
{
    if (!FMath::IsFinite(Amount) || Amount <= 0.f || Health <= 0.f) return;
    DamageFeedbackUntil = FPlatformTime::Seconds() + .35;
    Health = FMath::Clamp(Health - Amount, 0.f, MaxHealth);
    Hunger = FMath::Clamp(Hunger - Amount * .15f, 0.f, MaxHunger);
    if (!Reason.IsEmpty())
    {
        SetInteractionNotice(FString::Printf(TEXT("%s -%.0f 生命"), *Reason, Amount));
    }
    if (Health > 0.f) return;

    // There is no separate death screen in this slice.  Restore at the safe
    // world spawn and reset transient survival values/tools.
    ResetGameplayState();
    if (BlockWorld)
    {
        ABlockRealmGameMode* Game = GetRealmMode(this);
        const FVector Respawn = Game ? Game->GetAdventureRespawnLocation() : BlockWorld->GetSpawnLocation();
        BlockWorld->RequestStreaming(Respawn);
        SetActorLocation(Respawn, false, nullptr, ETeleportType::TeleportPhysics);
        GetCharacterMovement()->StopMovementImmediately();
    }
    SetInteractionNotice(TEXT("你倒下了，已在安全地点重生。"));
}

void ABlockRealmCharacter::RestoreSurvivalHealth(float Amount)
{
    if (!FMath::IsFinite(Amount) || Amount <= 0.f || Health <= 0.f) return;
    Health = FMath::Clamp(Health + Amount, 0.f, MaxHealth);
}

void ABlockRealmCharacter::RestoreSurvivalHunger(float Amount)
{
    if (!FMath::IsFinite(Amount) || Amount <= 0.f || Health <= 0.f) return;
    Hunger = FMath::Clamp(Hunger + Amount, 0.f, MaxHunger);
}

float ABlockRealmCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (!CanUseGameplayInput() || !FMath::IsFinite(DamageAmount)) return 0.f;
    const float Applied = FMath::Max(0.f, DamageAmount);
    ApplySurvivalDamage(Applied, TEXT("受到伤害"));
    return Applied;
}

void ABlockRealmCharacter::PausePressed()
{
    if (bInventoryOpen)
    {
        ToggleInventory();
        return;
    }
    if (ABlockRealmGameMode* Game = GetRealmMode(this))
    {
        Game->TogglePauseMenu();
    }
}

void ABlockRealmCharacter::SavePressed()
{
    if (ABlockRealmGameMode* Game = GetRealmMode(this))
    {
        if (Game->IsAdventureActive() || Game->IsPausedMenu())
        {
            Game->SaveAdventure();
            SetInteractionNotice(Game->GetNotice());
        }
    }
}

void ABlockRealmCharacter::SetInteractionNotice(const FString& Notice)
{
    InteractionNotice = Notice;
    InteractionNoticeUntil = FPlatformTime::Seconds() + 3.5;
}

FString ABlockRealmCharacter::GetInteractionNotice() const
{
    return FPlatformTime::Seconds() < InteractionNoticeUntil ? InteractionNotice : FString();
}
