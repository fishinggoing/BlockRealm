#include "BlockRealmRestPoint.h"

#include "BlockRealmTorch.h"
#include "BlockWorld.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelData.h"

namespace
{
    FIntVector HeadingOffset(int32 Heading)
    {
        switch (Heading)
        {
        case 0: return FIntVector(1, 0, 0);
        case 1: return FIntVector(0, 1, 0);
        case 2: return FIntVector(-1, 0, 0);
        case 3: return FIntVector(0, -1, 0);
        default: return FIntVector::ZeroValue;
        }
    }

    bool HasValidCells(const FIntVector& Cell, int32 Heading, FIntVector& OutSecondCell)
    {
        if (Heading < 0 || Heading > 3 || !FVoxelData::IsValidPosition(Cell) || Cell.Z <= 0)
        {
            return false;
        }
        // Validate before arithmetic so untrusted save/input coordinates cannot overflow.
        const FIntVector SecondCell = Cell + HeadingOffset(Heading);
        if (!FVoxelData::IsValidPosition(SecondCell)) return false;
        OutSecondCell = SecondCell;
        return true;
    }

    bool IsReliableSupport(EBlockType Type)
    {
        return FVoxelData::IsSolid(Type) && Type != EBlockType::Leaves && Type != EBlockType::Glass;
    }

    bool HasPlacementOccupancy(UWorld* World, const FIntVector& FirstCell,
        const FIntVector& SecondCell, const AActor* IgnoredActor = nullptr)
    {
        if (!IsValid(World) || World->bIsTearingDown) return true;
        for (TActorIterator<AActor> It(World); It; ++It)
        {
            const AActor* Actor = *It;
            if (!IsValid(Actor) || Actor == IgnoredActor || Actor->IsActorBeingDestroyed()) continue;

            if (const ABlockRealmRestPoint* Bed = Cast<ABlockRealmRestPoint>(Actor))
            {
                if (Bed->OccupiesCell(FirstCell) || Bed->OccupiesCell(SecondCell)) return true;
                continue;
            }
            if (const ABlockRealmTorch* Torch = Cast<ABlockRealmTorch>(Actor))
            {
                const FIntVector Cell = Torch->GetBlockPosition();
                if (Cell == FirstCell || Cell == SecondCell) return true;
                continue;
            }

            // Farming can be integrated after this module without a crop header dependency.
            // Tagged placement actors must retain their occupied air cell as their actor origin.
            if (Actor->ActorHasTag(TEXT("BlockRealmCrop"))
                || Actor->ActorHasTag(TEXT("BlockRealmTorch"))
                || Actor->ActorHasTag(TEXT("BlockRealmRestPoint")))
            {
                const FVector Location = Actor->GetActorLocation();
                if (Location.ContainsNaN()) continue;
                const FIntVector Cell = ABlockWorld::WorldToBlock(Location);
                if (Cell == FirstCell || Cell == SecondCell) return true;
            }
        }
        return false;
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
        // Do not renormalize: the parameter must remain a world distance under nonuniform scale.
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

ABlockRealmRestPoint::ABlockRealmRestPoint()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickInterval = 0.5f;
    Tags.Add(TEXT("BlockRealmRestPoint"));

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("RestPointRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Movable);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Wood.M_Wood"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> SandMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Sand.M_Sand"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> StoneMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Stone.M_Stone"));

    UStaticMesh* Cube = CubeMesh.Succeeded() ? CubeMesh.Object : nullptr;
    UMaterialInterface* Wood = WoodMaterial.Succeeded() ? WoodMaterial.Object : nullptr;
    UMaterialInterface* Sand = SandMaterial.Succeeded() ? SandMaterial.Object : nullptr;
    UMaterialInterface* Stone = StoneMaterial.Succeeded() ? StoneMaterial.Object : nullptr;
    auto AddPart = [this, Cube](const TCHAR* Name, FVector Center, FVector Dimensions,
        UMaterialInterface* Material)
    {
        UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(FName(Name));
        Part->SetupAttachment(SceneRoot);
        Part->SetRelativeLocation(Center);
        Part->SetRelativeScale3D(Dimensions / 100.0);
        Part->SetMobility(EComponentMobility::Movable);
        Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Part->SetGenerateOverlapEvents(false);
        Part->SetCanEverAffectNavigation(false);
        Part->SetCastShadow(false);
        Part->SetVisibleInRayTracing(true);
        if (Cube) Part->SetStaticMesh(Cube);
        if (Material) Part->SetMaterial(0, Material);
        VisualMeshes.Add(Part);
    };

    // The origin stays at the foot air-cell center. Geometry stays inside the two-cell footprint:
    // local X [-45, 145], Y [-42, 42], Z [-49, 18] cm, rotated in 90-degree steps.
    AddPart(TEXT("BedDeck"), FVector(50, 0, -26), FVector(180, 72, 8), Wood);
    AddPart(TEXT("LeftBedRail"), FVector(50, -39, -23), FVector(180, 6, 10), Wood);
    AddPart(TEXT("RightBedRail"), FVector(50, 39, -23), FVector(180, 6, 10), Wood);
    AddPart(TEXT("HeadBoard"), FVector(141, 0, -14), FVector(8, 84, 64), Wood);
    AddPart(TEXT("FootBoard"), FVector(-41, 0, -22), FVector(8, 84, 32), Wood);
    AddPart(TEXT("FrontLeftLeg"), FVector(-32, -32, -37), FVector(12, 12, 24), Wood);
    AddPart(TEXT("FrontRightLeg"), FVector(-32, 32, -37), FVector(12, 12, 24), Wood);
    AddPart(TEXT("BackLeftLeg"), FVector(132, -32, -37), FVector(12, 12, 24), Wood);
    AddPart(TEXT("BackRightLeg"), FVector(132, 32, -37), FVector(12, 12, 24), Wood);
    AddPart(TEXT("BedMattress"), FVector(50, 0, -14), FVector(172, 72, 16), Sand);
    AddPart(TEXT("BedBlanket"), FVector(29, 0, -4), FVector(118, 74, 6), Stone);
    AddPart(TEXT("BlanketBand"), FVector(17, 0, -0.25), FVector(8, 74, 1.5), Sand);
    AddPart(TEXT("BedPillow"), FVector(112, 0, -0.5), FVector(38, 60, 9), Sand);
    SetActorEnableCollision(false);
}

ABlockRealmRestPoint* ABlockRealmRestPoint::TryPlace(UWorld* World, ABlockWorld* InBlockWorld,
    FIntVector Cell, int32 InHeading)
{
    FIntVector SecondCell;
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || !HasValidCells(Cell, InHeading, SecondCell)
        || InBlockWorld->GetBlock(Cell) != EBlockType::Air
        || InBlockWorld->GetBlock(SecondCell) != EBlockType::Air
        || !IsReliableSupport(InBlockWorld->GetBlock(Cell - FIntVector(0, 0, 1)))
        || !IsReliableSupport(InBlockWorld->GetBlock(SecondCell - FIntVector(0, 0, 1)))
        || HasPlacementOccupancy(World, Cell, SecondCell))
    {
        return nullptr;
    }

    int32 PlacedCount = 0;
    for (TActorIterator<ABlockRealmRestPoint> It(World); It; ++It)
    {
        if (!IsValid(*It) || It->IsActorBeingDestroyed()) continue;
        if (++PlacedCount >= MaxPlacedRestPoints) return nullptr;
    }

    const FTransform Transform(FRotator(0.f, InHeading * 90.f, 0.f), ABlockWorld::BlockToWorld(Cell));
    ABlockRealmRestPoint* Bed = World->SpawnActorDeferred<ABlockRealmRestPoint>(StaticClass(), Transform,
        InBlockWorld, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Bed) return nullptr;

    Bed->BlockWorld = InBlockWorld;
    Bed->BlockPosition = Cell;
    Bed->Heading = InHeading;
    Bed->bPlacementInitialized = true;
    Bed->FinishSpawning(Transform);
    return IsValid(Bed) && !Bed->IsActorBeingDestroyed() ? Bed : nullptr;
}

ABlockRealmRestPoint* ABlockRealmRestPoint::FindAimedRestPoint(UWorld* World, ABlockWorld* InBlockWorld,
    FVector Origin, FVector Direction, float MaxDistance)
{
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || Origin.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.f) return nullptr;

    const double DirectionScale = Direction.GetAbsMax();
    if (!FMath::IsFinite(DirectionScale) || DirectionScale == 0.0) return nullptr;
    const FVector UnitDirection = (Direction / DirectionScale).GetSafeNormal();

    ABlockRealmRestPoint* NearestBed = nullptr;
    double NearestDistance = MaxDistance;
    for (TActorIterator<ABlockRealmRestPoint> It(World); It; ++It)
    {
        ABlockRealmRestPoint* Bed = *It;
        if (!IsValid(Bed) || Bed->IsActorBeingDestroyed() || Bed->IsHidden()
            || Bed->GetBlockWorld() != InBlockWorld || !Bed->IsAnchored()) continue;

        double HitDistance = NearestDistance;
        bool bHit = false;
        for (const TObjectPtr<UStaticMeshComponent>& Mesh : Bed->VisualMeshes)
        {
            double MeshDistance = 0.0;
            if (IntersectVisualMesh(Mesh.Get(), Origin, UnitDirection, HitDistance, MeshDistance))
            {
                HitDistance = MeshDistance;
                bHit = true;
            }
        }
        if (!bHit || (NearestBed && HitDistance >= NearestDistance)) continue;

        FIntVector WallBlock, WallAdjacent;
        if (InBlockWorld->TraceBlocks(Origin, UnitDirection,
            FMath::Min(MaxDistance, static_cast<float>(HitDistance)), WallBlock, WallAdjacent)) continue;

        NearestDistance = HitDistance;
        NearestBed = Bed;
    }
    return NearestBed;
}

void ABlockRealmRestPoint::BeginPlay()
{
    Super::BeginPlay();
    if (!IsAnchored()) Destroy();
}

void ABlockRealmRestPoint::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsAnchored()) Destroy();
}

FIntVector ABlockRealmRestPoint::GetSecondBlockPosition() const
{
    return BlockPosition + HeadingOffset(Heading);
}

bool ABlockRealmRestPoint::OccupiesCell(const FIntVector& Cell) const
{
    return bPlacementInitialized && (Cell == BlockPosition || Cell == GetSecondBlockPosition());
}

bool ABlockRealmRestPoint::IsAnchored() const
{
    FIntVector SecondCell;
    return bPlacementInitialized && IsValid(BlockWorld) && !BlockWorld->IsActorBeingDestroyed()
        && BlockWorld->GetWorld() == GetWorld() && HasValidCells(BlockPosition, Heading, SecondCell)
        && BlockWorld->GetBlock(BlockPosition) == EBlockType::Air
        && BlockWorld->GetBlock(SecondCell) == EBlockType::Air
        && IsReliableSupport(BlockWorld->GetBlock(BlockPosition - FIntVector(0, 0, 1)))
        && IsReliableSupport(BlockWorld->GetBlock(SecondCell - FIntVector(0, 0, 1)))
        && !HasPlacementOccupancy(GetWorld(), BlockPosition, SecondCell, this);
}

bool ABlockRealmRestPoint::FindSafeRespawn(FVector& OutLocation) const
{
    if (!IsInGameThread() || IsActorBeingDestroyed() || !IsAnchored()) return false;

    const FIntVector Forward = HeadingOffset(Heading);
    const FIntVector Side(-Forward.Y, Forward.X, 0);
    const FIntVector Backward(-Forward.X, -Forward.Y, 0);
    const FIntVector OppositeSide(-Side.X, -Side.Y, 0);
    // Eight surrounding positions: both long sides, both ends, and the two foot corners.
    // None lies in the bed footprint; the deterministic order prefers a long-side exit.
    const FIntVector Offsets[] = { Side, Forward + Side, OppositeSide, Forward + OppositeSide,
        Backward, Forward * 2, Backward + Side, Backward + OppositeSide };
    for (const FIntVector& Offset : Offsets)
    {
        const FIntVector FeetCell = BlockPosition + Offset;
        const FIntVector HeadCell = FeetCell + FIntVector(0, 0, 1);
        const FIntVector SupportCell = FeetCell - FIntVector(0, 0, 1);
        if (!FVoxelData::IsValidPosition(FeetCell) || !FVoxelData::IsValidPosition(HeadCell)
            || !FVoxelData::IsValidPosition(SupportCell) || OccupiesCell(FeetCell)
            || BlockWorld->GetBlock(FeetCell) != EBlockType::Air
            || BlockWorld->GetBlock(HeadCell) != EBlockType::Air
            || !IsReliableSupport(BlockWorld->GetBlock(SupportCell))
            || HasPlacementOccupancy(GetWorld(), FeetCell, HeadCell)) continue;

        // A <=96cm half-height capsule fits in the two 100cm air cells with 2cm floor clearance.
        // The current player capsule is 90cm. Centering XY also keeps its 34cm radius in this column.
        FVector Location = ABlockWorld::BlockToWorld(FeetCell);
        Location.Z = static_cast<double>(FeetCell.Z) * FVoxelData::BlockSize + 98.0;
        OutLocation = Location;
        return true;
    }
    return false;
}
