#pragma once

#include "CoreMinimal.h"

class AActor;
class ABlockWorld;
class UWorld;

namespace BlockRealmPlacement
{
    /** Typed facility occupancy in the same voxel world; beds reserve both cells. */
    // Invalid inputs count as occupied so callers cannot place into an unchecked world.
    BLOCKREALM_API bool IsCellOccupied(UWorld* World, ABlockWorld* BlockWorld,
        const FIntVector& Cell, const AActor* Ignore = nullptr);
}
