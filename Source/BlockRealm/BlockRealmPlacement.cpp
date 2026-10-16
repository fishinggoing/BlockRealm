#include "BlockRealmPlacement.h"

#include "BlockRealmCrop.h"
#include "BlockRealmFurnace.h"
#include "BlockRealmRestPoint.h"
#include "BlockRealmTorch.h"
#include "BlockWorld.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "VoxelData.h"

namespace
{
    template <typename ActorType>
    bool IsPlacementInWorld(const ActorType* Actor, UWorld* World,
        ABlockWorld* BlockWorld, const AActor* Ignore)
    {
        return IsValid(Actor) && Actor != Ignore && !Actor->IsActorBeingDestroyed()
            && Actor->GetWorld() == World && Actor->GetBlockWorld() == BlockWorld;
    }
}

bool BlockRealmPlacement::IsCellOccupied(UWorld* World, ABlockWorld* BlockWorld,
    const FIntVector& Cell, const AActor* Ignore)
{
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || !IsValid(BlockWorld)
        || BlockWorld->IsActorBeingDestroyed() || BlockWorld->GetWorld() != World
        || !FVoxelData::IsValidPosition(Cell) || Cell.Z <= 0) return true;

    for (TActorIterator<ABlockRealmTorch> It(World); It; ++It)
    {
        const ABlockRealmTorch* Torch = *It;
        if (IsPlacementInWorld(Torch, World, BlockWorld, Ignore)
            && Torch->GetBlockPosition() == Cell) return true;
    }
    for (TActorIterator<ABlockRealmCrop> It(World); It; ++It)
    {
        const ABlockRealmCrop* Crop = *It;
        if (IsPlacementInWorld(Crop, World, BlockWorld, Ignore)
            && !Crop->IsHarvested() && Crop->GetBlockPosition() == Cell) return true;
    }
    for (TActorIterator<ABlockRealmRestPoint> It(World); It; ++It)
    {
        const ABlockRealmRestPoint* Bed = *It;
        if (IsPlacementInWorld(Bed, World, BlockWorld, Ignore) && Bed->OccupiesCell(Cell)) return true;
    }
    for (TActorIterator<ABlockRealmFurnace> It(World); It; ++It)
    {
        const ABlockRealmFurnace* Furnace = *It;
        if (IsPlacementInWorld(Furnace, World, BlockWorld, Ignore)
            && Furnace->GetBlockPosition() == Cell) return true;
    }
    return false;
}
