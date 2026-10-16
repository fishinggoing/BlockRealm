#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockRealmRestPoint.generated.h"

class ABlockWorld;
class USceneComponent;
class UStaticMeshComponent;
class UWorld;

/** A two-cell wooden bed. Placement and respawn queries use authoritative voxel data. */
UCLASS()
class BLOCKREALM_API ABlockRealmRestPoint : public AActor
{
    GENERATED_BODY()

public:
    ABlockRealmRestPoint();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    static constexpr int32 MaxPlacedRestPoints = 32;

    /** Cell is the foot's air cell; Heading 0/1/2/3 extends toward +X/+Y/-X/-Y. */
    static ABlockRealmRestPoint* TryPlace(UWorld* World, ABlockWorld* BlockWorld,
        FIntVector Cell, int32 Heading = 0);

    /** Finds the closest visible bed part, rejecting authoritative voxel occlusion. */
    static ABlockRealmRestPoint* FindAimedRestPoint(UWorld* World, ABlockWorld* BlockWorld,
        FVector Origin, FVector Direction, float MaxDistance);

    FIntVector GetBlockPosition() const { return BlockPosition; }
    int32 GetHeading() const { return Heading; }
    ABlockWorld* GetBlockWorld() const { return BlockWorld; }
    FIntVector GetSecondBlockPosition() const;
    bool OccupiesCell(const FIntVector& Cell) const;

    /** Returns false, preserving OutLocation, when no legal adjacent standing space exists. */
    bool FindSafeRespawn(FVector& OutLocation) const;

private:
    bool IsAnchored() const;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Rest Point")
    TObjectPtr<USceneComponent> SceneRoot;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Rest Point")
    TArray<TObjectPtr<UStaticMeshComponent>> VisualMeshes;

    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;

    FIntVector BlockPosition = FIntVector::ZeroValue;
    int32 Heading = 0;
    bool bPlacementInitialized = false;
};
