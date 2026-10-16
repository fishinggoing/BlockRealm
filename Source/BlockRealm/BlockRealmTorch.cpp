#include "BlockRealmTorch.h"

#include "BlockRealmPlacement.h"
#include "BlockWorld.h"
#include "Components/PointLightComponent.h"
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
    bool HasValidCells(const FIntVector& Cell, const FIntVector& Support)
    {
        if (!FVoxelData::IsValidPosition(Cell) || Cell.Z <= 0
            || !FVoxelData::IsValidPosition(Support))
        {
            return false;
        }
        // Bounds were checked first, so subtracting these integer coordinates is safe.
        const FIntVector Offset = Cell - Support;
        return FMath::Abs(Offset.X) + FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z) == 1;
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
        // Preserve the world-distance parameter under the mesh's nonuniform scale.
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

ABlockRealmTorch::ABlockRealmTorch()
{
    PrimaryActorTick.bCanEverTick = true;
    // A light-weight update is enough for a readable flame shimmer and avoids
    // ticking every frame for every placed torch.
    PrimaryActorTick.TickInterval = 0.12f;
    Tags.Add(TEXT("BlockRealmTorch"));

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("TorchRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Movable);

    WoodPost = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TorchWoodPost"));
    WoodPost->SetupAttachment(SceneRoot);
    WoodPost->SetRelativeLocation(FVector(0.f, 0.f, -14.f));
    WoodPost->SetRelativeScale3D(FVector(.11f, .11f, .66f));

    GlowCap = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TorchGlowCap"));
    GlowCap->SetupAttachment(SceneRoot);
    GlowCap->SetRelativeLocation(FVector(0.f, 0.f, 27.f));
    GlowCap->SetRelativeScale3D(FVector(.18f, .18f, .18f));

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    UStaticMeshComponent* VisualMeshes[] = { WoodPost.Get(), GlowCap.Get() };
    for (UStaticMeshComponent* Mesh : VisualMeshes)
    {
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->SetVisibleInRayTracing(true);
        if (CubeMesh.Succeeded()) Mesh->SetStaticMesh(CubeMesh.Object);
    }
    SetActorEnableCollision(false);

    static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Wood.M_Wood"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Glow.M_Glow"));
    if (WoodMaterial.Succeeded()) WoodPost->SetMaterial(0, WoodMaterial.Object);
    if (GlowMaterial.Succeeded()) GlowCap->SetMaterial(0, GlowMaterial.Object);

    TorchLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("TorchLight"));
    TorchLight->SetupAttachment(SceneRoot);
    TorchLight->SetRelativeLocation(GlowCap->GetRelativeLocation());
    TorchLight->SetMobility(EComponentMobility::Movable);
    TorchLight->SetUseInverseSquaredFalloff(false);
    TorchLight->SetIntensityUnits(ELightUnits::Unitless);
    TorchLight->SetLightFalloffExponent(2.3f);
    TorchLight->SetInverseExposureBlend(1.f);
    TorchLight->SetIntensity(1.8f);
    TorchLight->SetAttenuationRadius(1100.f);
    TorchLight->SetSourceRadius(9.f);
    TorchLight->SetSoftSourceRadius(12.f);
    TorchLight->SetLightColor(FLinearColor(1.f, .62f, .28f));
    TorchLight->SetCastShadows(true);
    TorchLight->SetAffectTranslucentLighting(false);
    TorchLight->SetVolumetricScatteringIntensity(0.f);
}

ABlockRealmTorch* ABlockRealmTorch::TryPlace(UWorld* World, ABlockWorld* InBlockWorld,
    FIntVector Cell, FIntVector Support)
{
    if (!IsInGameThread() || !IsValid(World) || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || !HasValidCells(Cell, Support)
        || InBlockWorld->GetBlock(Cell) != EBlockType::Air
        || !FVoxelData::IsSolid(InBlockWorld->GetBlock(Support))
        || BlockRealmPlacement::IsCellOccupied(World, InBlockWorld, Cell))
    {
        return nullptr;
    }

    int32 PlacedCount = 0;
    for (TActorIterator<ABlockRealmTorch> It(World); It; ++It)
    {
        const ABlockRealmTorch* Existing = *It;
        if (!IsValid(Existing) || Existing->IsActorBeingDestroyed()) continue;
        if (Existing->GetBlockPosition() == Cell || ++PlacedCount >= MaxPlacedTorches)
        {
            return nullptr;
        }
    }

    // Local -Z meets the supporting face; +Z carries the cap into the air cell.
    // The same geometry therefore works on floors, walls, and ceilings.
    const FVector Direction(Cell - Support);
    const FTransform Transform(FRotationMatrix::MakeFromZ(Direction).Rotator(),
        ABlockWorld::BlockToWorld(Cell));
    ABlockRealmTorch* Torch = World->SpawnActorDeferred<ABlockRealmTorch>(StaticClass(), Transform,
        InBlockWorld, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Torch) return nullptr;

    Torch->BlockWorld = InBlockWorld;
    Torch->BlockPosition = Cell;
    Torch->SupportBlock = Support;
    Torch->bPlacementInitialized = true;
    Torch->FinishSpawning(Transform);
    return IsValid(Torch) && !Torch->IsActorBeingDestroyed() ? Torch : nullptr;
}

ABlockRealmTorch* ABlockRealmTorch::FindAimedTorch(UWorld* World, ABlockWorld* InBlockWorld,
    FVector Origin, FVector Direction, float MaxDistance)
{
    if (!IsInGameThread() || !IsValid(World) || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || Origin.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.f) return nullptr;

    const double DirectionScale = Direction.GetAbsMax();
    if (!FMath::IsFinite(DirectionScale) || DirectionScale == 0.0) return nullptr;
    const FVector UnitDirection = (Direction / DirectionScale).GetSafeNormal();

    ABlockRealmTorch* NearestTorch = nullptr;
    double NearestDistance = MaxDistance;
    for (TActorIterator<ABlockRealmTorch> It(World); It; ++It)
    {
        ABlockRealmTorch* Torch = *It;
        if (!IsValid(Torch) || Torch->IsActorBeingDestroyed() || Torch->IsHidden()
            || Torch->GetBlockWorld() != InBlockWorld || !Torch->IsAnchored()) continue;

        double HitDistance = NearestDistance;
        bool bHit = false;
        for (const UStaticMeshComponent* Mesh : { Torch->WoodPost.Get(), Torch->GlowCap.Get() })
        {
            double MeshDistance = 0.0;
            if (IntersectVisualMesh(Mesh, Origin, UnitDirection, HitDistance, MeshDistance))
            {
                HitDistance = MeshDistance;
                bHit = true;
            }
        }
        if (!bHit || (NearestTorch && HitDistance >= NearestDistance)) continue;

        FIntVector WallBlock, WallAdjacent;
        if (InBlockWorld->TraceBlocks(Origin, UnitDirection,
            FMath::Min(MaxDistance, static_cast<float>(HitDistance)), WallBlock, WallAdjacent)) continue;

        NearestDistance = HitDistance;
        NearestTorch = Torch;
    }
    return NearestTorch;
}

void ABlockRealmTorch::BeginPlay()
{
    Super::BeginPlay();
    if (!IsAnchored()) Destroy();
}

void ABlockRealmTorch::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsAnchored())
    {
        SpawnRecoveryDrop();
        Destroy();
        return;
    }

    if (TorchLight)
    {
        // Deterministic per-instance flicker keeps a row of torches from
        // pulsing in lockstep while retaining a stable average light level.
        const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        const float Phase = static_cast<float>(GetUniqueID() & 255u) * 0.071f;
        const float Flicker = 0.91f + 0.055f * FMath::Sin(Time * 7.1f + Phase)
            + 0.035f * FMath::Sin(Time * 13.7f + Phase * 1.7f);
        TorchLight->SetIntensity(1.8f * FMath::Clamp(Flicker, 0.78f, 1.08f));
    }
}

bool ABlockRealmTorch::IsAnchored() const
{
    return bPlacementInitialized && IsValid(BlockWorld) && !BlockWorld->IsActorBeingDestroyed()
        && BlockWorld->GetWorld() == GetWorld() && HasValidCells(BlockPosition, SupportBlock)
        && FVoxelData::IsSolid(BlockWorld->GetBlock(SupportBlock))
        && !FVoxelData::IsSolid(BlockWorld->GetBlock(BlockPosition));
}

void ABlockRealmTorch::SpawnRecoveryDrop()
{
    UWorld* World = GetWorld();
    if (bRecoveryDropSpawned || !bPlacementInitialized || !IsValid(World) || World->bIsTearingDown
        || !IsValid(BlockWorld) || BlockWorld->IsActorBeingDestroyed()
        || BlockWorld->GetWorld() != World || !HasValidCells(BlockPosition, SupportBlock)) return;

    FIntVector DropCell = BlockPosition;
    if (BlockWorld->GetBlock(DropCell) != EBlockType::Air)
    {
        const FIntVector Offsets[] = { FIntVector(0, 0, 1), FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
            FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, -1) };
        for (const FIntVector& Offset : Offsets)
        {
            const FIntVector Candidate = BlockPosition + Offset;
            if (FVoxelData::IsValidPosition(Candidate) && BlockWorld->GetBlock(Candidate) == EBlockType::Air)
            {
                DropCell = Candidate;
                break;
            }
        }
    }

    FVector DropLocation = ABlockWorld::BlockToWorld(DropCell);
    if (!FVoxelData::IsSolid(BlockWorld->GetBlock(DropCell)))
    {
        // Settle directly above the authoritative floor, including for ceiling-mounted torches.
        // This keeps a small non-physical pickup reachable without a new ticking/physics actor.
        for (int32 Z = DropCell.Z - 1; Z >= 0; --Z)
        {
            if (FVoxelData::IsSolid(BlockWorld->GetBlock(FIntVector(DropCell.X, DropCell.Y, Z))))
            {
                DropLocation.Z = (static_cast<double>(Z) + 1.0) * FVoxelData::BlockSize + 24.0;
                break;
            }
        }
    }

    FActorSpawnParameters Parameters;
    Parameters.Owner = BlockWorld;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AStaticMeshActor* Drop = World->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
        DropLocation, FRotator::ZeroRotator, Parameters);
    if (!Drop) return;

    UStaticMeshComponent* DropPost = Drop->GetStaticMeshComponent();
    if (!DropPost)
    {
        Drop->Destroy();
        return;
    }
    DropPost->SetMobility(EComponentMobility::Movable);
    DropPost->SetStaticMesh(WoodPost->GetStaticMesh());
    DropPost->SetMaterial(0, WoodPost->GetMaterial(0));
    DropPost->SetRelativeScale3D(FVector(.08f, .08f, .36f));
    DropPost->SetSimulatePhysics(false);
    DropPost->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DropPost->SetGenerateOverlapEvents(false);
    DropPost->SetCanEverAffectNavigation(false);
    DropPost->SetCastShadow(false);

    UStaticMeshComponent* DropCap = NewObject<UStaticMeshComponent>(Drop, TEXT("TorchDropCap"));
    Drop->AddInstanceComponent(DropCap);
    DropCap->SetupAttachment(DropPost);
    // Inherit location/rotation but keep the cap's scale independent of the slender post.
    DropCap->SetAbsolute(false, false, true);
    DropCap->SetRelativeLocation(FVector(0.f, 0.f, 62.5f));
    DropCap->SetRelativeScale3D(FVector(.14f));
    DropCap->SetMobility(EComponentMobility::Movable);
    DropCap->SetStaticMesh(GlowCap->GetStaticMesh());
    DropCap->SetMaterial(0, GlowCap->GetMaterial(0));
    DropCap->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DropCap->SetGenerateOverlapEvents(false);
    DropCap->SetCanEverAffectNavigation(false);
    DropCap->SetCastShadow(false);
    DropCap->RegisterComponent();

    Drop->SetActorEnableCollision(false);
    Drop->SetActorTickEnabled(false);
    Drop->Tags.Add(TEXT("BlockRealmDrop"));
    Drop->Tags.Add(TEXT("BlockRealmTorchDrop"));
    bRecoveryDropSpawned = true;
}
