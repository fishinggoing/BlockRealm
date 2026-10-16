#include "BlockRealmCrop.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmGameMode.h"
#include "BlockRealmPlayerActions.h"
#include "BlockRealmTorch.h"
#include "BlockWorld.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelData.h"

namespace
{
    bool IsPlantableSoil(EBlockType Type)
    {
        return Type == EBlockType::Grass || Type == EBlockType::Dirt;
    }

    bool HasValidCells(const FIntVector& Cell, const FIntVector& Support)
    {
        // Check bounds before adding one to an external integer coordinate.
        return FVoxelData::IsValidPosition(Support)
            && Support.Z < FVoxelData::WorldHeight - 1
            && FVoxelData::IsValidPosition(Cell)
            && Cell == Support + FIntVector(0, 0, 1);
    }

    void ConfigureVisualMesh(UStaticMeshComponent* Mesh, UStaticMesh* Cube)
    {
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->SetVisibleInRayTracing(true);
        if (Cube) Mesh->SetStaticMesh(Cube);
    }

    bool IntersectVisualMesh(const UStaticMeshComponent* Mesh, const FVector& Origin,
        const FVector& UnitDirection, double MaxDistance, double& OutDistance)
    {
        if (!IsValid(Mesh) || !Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame
            || !IsValid(Mesh->GetStaticMesh())) return false;

        const FBox Box = Mesh->GetStaticMesh()->GetBoundingBox();
        const FTransform Transform = Mesh->GetComponentTransform();
        if (!Box.IsValid || Transform.ContainsNaN()
            || Transform.GetScale3D().GetAbsMin() <= UE_SMALL_NUMBER) return false;

        const FVector LocalOrigin = Transform.InverseTransformPosition(Origin);
        // Do not normalize this vector: the ray parameter must remain world centimetres.
        const FVector LocalDirection = Transform.InverseTransformVector(UnitDirection);
        if (LocalOrigin.ContainsNaN() || LocalDirection.ContainsNaN()) return false;

        double Entry = 0.0;
        double Exit = MaxDistance;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (LocalDirection[Axis] == 0.0)
            {
                if (LocalOrigin[Axis] < Box.Min[Axis] || LocalOrigin[Axis] > Box.Max[Axis]) return false;
                continue;
            }
            double Near = (Box.Min[Axis] - LocalOrigin[Axis]) / LocalDirection[Axis];
            double Far = (Box.Max[Axis] - LocalOrigin[Axis]) / LocalDirection[Axis];
            if (Near > Far) Swap(Near, Far);
            Entry = FMath::Max(Entry, Near);
            Exit = FMath::Min(Exit, Far);
            if (Entry > Exit) return false;
        }
        OutDistance = Entry;
        return true;
    }
}

ABlockRealmCrop::ABlockRealmCrop()
{
    // Per-frame Delta avoids accumulating paused time through a coarse actor tick interval.
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.f;
    Tags.Add(TEXT("BlockRealmCrop"));

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("CropRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Movable);

    Stem = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("CropStem"));
    Stem->SetupAttachment(SceneRoot);

    const FName LeafNames[] = { TEXT("CropLowerLeafLeft"), TEXT("CropLowerLeafRight"),
        TEXT("CropUpperLeafLeft"), TEXT("CropUpperLeafRight") };
    for (const FName Name : LeafNames)
    {
        UStaticMeshComponent* Leaf = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Leaf->SetupAttachment(SceneRoot);
        LeafMeshes.Add(Leaf);
    }
    const FName HeadNames[] = { TEXT("CropCentreGrain"), TEXT("CropLeftGrain"), TEXT("CropRightGrain") };
    for (const FName Name : HeadNames)
    {
        UStaticMeshComponent* Head = CreateDefaultSubobject<UStaticMeshComponent>(Name);
        Head->SetupAttachment(SceneRoot);
        GrainHeads.Add(Head);
    }

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> LeavesMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Leaves.M_Leaves"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SandMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Sand.M_Sand"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Wood.M_Wood"));

    ConfigureVisualMesh(Stem, CubeMesh.Object);
    if (LeavesMaterial.Succeeded()) Stem->SetMaterial(0, LeavesMaterial.Object);
    for (UStaticMeshComponent* Leaf : LeafMeshes)
    {
        ConfigureVisualMesh(Leaf, CubeMesh.Object);
        if (LeavesMaterial.Succeeded()) Leaf->SetMaterial(0, LeavesMaterial.Object);
    }
    for (UStaticMeshComponent* Head : GrainHeads)
    {
        ConfigureVisualMesh(Head, CubeMesh.Object);
        if (SandMaterial.Succeeded()) Head->SetMaterial(0, SandMaterial.Object);
        else if (WoodMaterial.Succeeded()) Head->SetMaterial(0, WoodMaterial.Object);
    }
    SetActorEnableCollision(false);
    UpdateGrowthVisuals();
}

ABlockRealmCrop* ABlockRealmCrop::TryPlant(UWorld* World, ABlockWorld* InBlockWorld,
    FIntVector Support, float RestoredGrowthSeconds)
{
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || !FVoxelData::IsValidPosition(Support) || Support.Z >= FVoxelData::WorldHeight - 1
        || !FMath::IsFinite(RestoredGrowthSeconds) || RestoredGrowthSeconds < 0.f
        || RestoredGrowthSeconds > DefaultGrowthDurationSeconds) return nullptr;

    const FIntVector Cell = Support + FIntVector(0, 0, 1);
    if (!HasValidCells(Cell, Support) || !IsPlantableSoil(InBlockWorld->GetBlock(Support))
        || InBlockWorld->GetBlock(Cell) != EBlockType::Air) return nullptr;

    int32 PlacedCount = 0;
    for (TActorIterator<ABlockRealmCrop> It(World); It; ++It)
    {
        const ABlockRealmCrop* Existing = *It;
        if (!IsValid(Existing) || Existing->IsActorBeingDestroyed()
            || !Existing->bPlacementInitialized || Existing->bHarvested) continue;
        if (Existing->GetBlockPosition() == Cell || ++PlacedCount >= MaxPlacedCrops) return nullptr;
    }
    for (TActorIterator<ABlockRealmTorch> It(World); It; ++It)
    {
        const ABlockRealmTorch* Torch = *It;
        if (IsValid(Torch) && !Torch->IsActorBeingDestroyed() && Torch->GetBlockPosition() == Cell)
        {
            return nullptr;
        }
    }

    // The root is the exact top face of the soil, not the centre of the empty cell.
    const FVector RootLocation = ABlockWorld::BlockToWorld(Cell)
        - FVector(0.0, 0.0, FVoxelData::BlockSize * 0.5);
    const FTransform Transform(FRotator::ZeroRotator, RootLocation);
    ABlockRealmCrop* Crop = World->SpawnActorDeferred<ABlockRealmCrop>(StaticClass(), Transform,
        InBlockWorld, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Crop) return nullptr;

    Crop->BlockWorld = InBlockWorld;
    Crop->BlockPosition = Cell;
    Crop->SupportBlock = Support;
    Crop->GrowthSeconds = RestoredGrowthSeconds;
    Crop->bPlacementInitialized = true;
    Crop->UpdateGrowthVisuals();
    Crop->FinishSpawning(Transform);
    return IsValid(Crop) && !Crop->IsActorBeingDestroyed() ? Crop : nullptr;
}

ABlockRealmCrop* ABlockRealmCrop::FindAimedCrop(UWorld* World, ABlockWorld* InBlockWorld,
    FVector Origin, FVector Direction, float MaxDistance)
{
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || Origin.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.f) return nullptr;

    const double DirectionScale = Direction.GetAbsMax();
    if (!FMath::IsFinite(DirectionScale) || DirectionScale == 0.0) return nullptr;
    const FVector UnitDirection = (Direction / DirectionScale).GetSafeNormal();

    ABlockRealmCrop* NearestCrop = nullptr;
    double NearestDistance = MaxDistance;
    for (TActorIterator<ABlockRealmCrop> It(World); It; ++It)
    {
        ABlockRealmCrop* Crop = *It;
        if (!IsValid(Crop) || Crop->IsActorBeingDestroyed() || Crop->IsHidden()
            || Crop->bHarvested || Crop->GetBlockWorld() != InBlockWorld || !Crop->IsAnchored()) continue;

        double HitDistance = NearestDistance;
        bool bHit = false;
        auto TestMesh = [&](const UStaticMeshComponent* Mesh)
        {
            double MeshDistance = 0.0;
            if (IntersectVisualMesh(Mesh, Origin, UnitDirection, HitDistance, MeshDistance))
            {
                HitDistance = MeshDistance;
                bHit = true;
            }
        };
        TestMesh(Crop->Stem);
        for (const UStaticMeshComponent* Leaf : Crop->LeafMeshes) TestMesh(Leaf);
        for (const UStaticMeshComponent* Head : Crop->GrainHeads) TestMesh(Head);
        if (!bHit || (NearestCrop && HitDistance >= NearestDistance)) continue;

        FIntVector WallBlock, WallAdjacent;
        if (InBlockWorld->TraceBlocks(Origin, UnitDirection,
            FMath::Min(MaxDistance, static_cast<float>(HitDistance)), WallBlock, WallAdjacent)) continue;

        NearestDistance = HitDistance;
        NearestCrop = Crop;
    }
    return NearestCrop;
}

void ABlockRealmCrop::BeginPlay()
{
    Super::BeginPlay();
    // Direct, uninitialised spawns and invalid restores do not create free food.
    if (!IsAnchored())
    {
        Destroy();
        return;
    }
    UpdateGrowthVisuals();
}

void ABlockRealmCrop::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bHarvested || IsActorBeingDestroyed()) return;
    if (!IsAnchored())
    {
        SpawnRecoveryDrop();
        Destroy();
        return;
    }
    if (!CanGrow() || IsMature() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f) return;

    GrowthSeconds = FMath::Min(static_cast<double>(GrowthDurationSeconds),
        GrowthSeconds + static_cast<double>(DeltaSeconds));
    UpdateGrowthVisuals();
}

bool ABlockRealmCrop::SetGrowthDurationSeconds(float Seconds)
{
    if (!IsInGameThread() || bHarvested || IsActorBeingDestroyed()
        || !FMath::IsFinite(Seconds) || Seconds <= 0.f || Seconds > DefaultGrowthDurationSeconds) return false;

    const double Progress = FMath::Clamp(GrowthSeconds / static_cast<double>(GrowthDurationSeconds), 0.0, 1.0);
    GrowthDurationSeconds = Seconds;
    GrowthSeconds = Progress * static_cast<double>(Seconds);
    UpdateGrowthVisuals();
    return true;
}

bool ABlockRealmCrop::TryHarvest(UBlockRealmPlayerActionsComponent* Actions)
{
    if (!IsInGameThread() || bHarvested || IsActorBeingDestroyed() || !IsAnchored() || !IsMature()
        || !CanGrow() || !IsValid(Actions) || Actions->GetWorld() != GetWorld()) return false;

    const ABlockRealmCharacter* Player = Cast<ABlockRealmCharacter>(Actions->GetOwner());
    if (IsValid(Player) && (Player->IsInventoryOpen() || Player->GetHealth() <= 0.f)) return false;
    if (!Actions->TryAddFood(HarvestFoodCount)) return false;

    // Claim before Destroy: repeated calls and a deferred destruction cannot pay twice.
    bHarvested = true;
    Tags.Remove(TEXT("BlockRealmCrop"));
    SetActorHiddenInGame(true);
    SetActorTickEnabled(false);
    Destroy();
    return true;
}

bool ABlockRealmCrop::IsAnchored() const
{
    return bPlacementInitialized && !bHarvested && IsValid(BlockWorld) && !BlockWorld->IsActorBeingDestroyed()
        && BlockWorld->GetWorld() == GetWorld() && HasValidCells(BlockPosition, SupportBlock)
        && IsPlantableSoil(BlockWorld->GetBlock(SupportBlock))
        && !FVoxelData::IsSolid(BlockWorld->GetBlock(BlockPosition));
}

bool ABlockRealmCrop::CanGrow() const
{
    const UWorld* World = GetWorld();
    if (!IsValid(World) || World->bIsTearingDown) return false;
    const ABlockRealmGameMode* Game = World->GetAuthGameMode<ABlockRealmGameMode>();
    // Match the day/night actor's behaviour in isolated test worlds without this GameMode.
    if (!IsValid(Game)) return true;
    if (!Game->IsAdventureActive()) return false;
    const ABlockRealmCharacter* Player = Game->GetPlayerCharacter();
    return !IsValid(Player) || !Player->IsInventoryOpen();
}

void ABlockRealmCrop::UpdateGrowthVisuals()
{
    const double Progress = GrowthSeconds / static_cast<double>(GrowthDurationSeconds);
    const int32 Stage = IsMature() ? 3 : (Progress >= 2.0 / 3.0 ? 2 : (Progress >= 1.0 / 3.0 ? 1 : 0));
    if (Stage == VisualStage) return;
    VisualStage = Stage;

    const float Heights[] = { 14.f, 30.f, 50.f, 70.f };
    const float LeafLengths[] = { 12.f, 20.f, 28.f, 30.f };
    const float LeafWidths[] = { 5.f, 6.f, 7.f, 7.5f };
    const float Height = Heights[Stage];
    const float Length = LeafLengths[Stage];
    const float Width = LeafWidths[Stage];
    const float StemWidth = 3.5f + Stage * 0.3f;
    Stem->SetRelativeLocation(FVector(0.f, 0.f, Height * 0.5f));
    Stem->SetRelativeScale3D(FVector(StemWidth / 100.f, StemWidth / 100.f, Height / 100.f));

    for (int32 Index = 0; Index < LeafMeshes.Num(); ++Index)
    {
        UStaticMeshComponent* Leaf = LeafMeshes[Index];
        const bool bUpper = Index >= 2;
        const float Side = Index % 2 == 0 ? -1.f : 1.f;
        Leaf->SetVisibility(!bUpper || Stage >= 1);
        Leaf->SetRelativeLocation(bUpper
            ? FVector(0.f, Side * Length * 0.30f, Height * 0.75f)
            : FVector(Side * Length * 0.42f, 0.f, Height * (Stage == 0 ? 0.60f : 0.45f)));
        const float Yaw = bUpper ? (Side < 0.f ? -90.f : 90.f) : (Side < 0.f ? 180.f : 0.f);
        Leaf->SetRelativeRotation(FRotator(bUpper ? 30.f : 22.f, Yaw, 0.f));
        Leaf->SetRelativeScale3D(FVector(Length * (bUpper ? 0.72f : 1.f) / 100.f,
            Width / 100.f, 0.025f));
    }

    // Three golden, non-emissive ears make the mature silhouette readily recognisable.
    GrainHeads[0]->SetRelativeLocation(FVector(0.f, 0.f, 72.f));
    GrainHeads[0]->SetRelativeScale3D(FVector(0.09f, 0.09f, 0.18f));
    GrainHeads[1]->SetRelativeLocation(FVector(-8.f, 0.f, 65.f));
    GrainHeads[1]->SetRelativeRotation(FRotator(-18.f, 0.f, 0.f));
    GrainHeads[1]->SetRelativeScale3D(FVector(0.075f, 0.075f, 0.16f));
    GrainHeads[2]->SetRelativeLocation(FVector(8.f, 0.f, 65.f));
    GrainHeads[2]->SetRelativeRotation(FRotator(18.f, 0.f, 0.f));
    GrainHeads[2]->SetRelativeScale3D(FVector(0.075f, 0.075f, 0.16f));
    for (UStaticMeshComponent* Head : GrainHeads) Head->SetVisibility(Stage == 3);
}

void ABlockRealmCrop::SpawnRecoveryDrop()
{
    UWorld* World = GetWorld();
    if (bRecoveryDropSpawned || bHarvested || !bPlacementInitialized || !IsValid(World) || World->bIsTearingDown
        || !IsValid(BlockWorld) || BlockWorld->IsActorBeingDestroyed()
        || BlockWorld->GetWorld() != World || !HasValidCells(BlockPosition, SupportBlock)) return;

    FIntVector DropCell = BlockPosition;
    if (FVoxelData::IsSolid(BlockWorld->GetBlock(DropCell)))
    {
        bool bFoundAir = false;
        const FIntVector Offsets[] = { FIntVector(0, 0, 1), FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
            FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, -1) };
        for (const FIntVector& Offset : Offsets)
        {
            const FIntVector Candidate = BlockPosition + Offset;
            if (FVoxelData::IsValidPosition(Candidate) && BlockWorld->GetBlock(Candidate) == EBlockType::Air)
            {
                DropCell = Candidate;
                bFoundAir = true;
                break;
            }
        }
        if (!bFoundAir)
        {
            // A sealed neighbouring cell must not bury the refund inside a solid voxel.
            for (int32 Z = BlockPosition.Z + 1; Z < FVoxelData::WorldHeight; ++Z)
            {
                const FIntVector Candidate(BlockPosition.X, BlockPosition.Y, Z);
                if (BlockWorld->GetBlock(Candidate) == EBlockType::Air)
                {
                    DropCell = Candidate;
                    bFoundAir = true;
                    break;
                }
            }
            if (!bFoundAir) DropCell.Z = FVoxelData::WorldHeight;
        }
    }

    FVector DropLocation = ABlockWorld::BlockToWorld(DropCell);
    // Settle against authoritative soil/terrain without physics, collision, or voxel edits.
    for (int32 Z = DropCell.Z - 1; Z >= 0; --Z)
    {
        if (FVoxelData::IsSolid(BlockWorld->GetBlock(FIntVector(DropCell.X, DropCell.Y, Z))))
        {
            DropLocation.Z = (static_cast<double>(Z) + 1.0) * FVoxelData::BlockSize + 16.0;
            break;
        }
    }

    FActorSpawnParameters Parameters;
    Parameters.Owner = BlockWorld;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AStaticMeshActor* Drop = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
        DropLocation, FRotator(0.f, 45.f, 0.f), Parameters);
    if (!Drop) return;

    UStaticMeshComponent* DropMesh = Drop->GetStaticMeshComponent();
    if (!DropMesh)
    {
        Drop->Destroy();
        return;
    }
    ConfigureVisualMesh(DropMesh, Stem->GetStaticMesh());
    DropMesh->SetMaterial(0, GrainHeads[0]->GetMaterial(0));
    DropMesh->SetRelativeScale3D(FVector(0.15f, 0.15f, 0.10f));
    DropMesh->SetSimulatePhysics(false);
    DropMesh->SetEnableGravity(false);
    Drop->SetActorEnableCollision(false);
    Drop->SetActorTickEnabled(false);
    Drop->Tags.Add(TEXT("BlockRealmDrop"));
    Drop->Tags.Add(TEXT("BlockRealmFoodDrop"));
    Drop->SetLifeSpan(300.f);
    bRecoveryDropSpawned = true;
}
