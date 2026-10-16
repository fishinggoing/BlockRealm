#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VoxelData.h"
#include "BlockChunk.generated.h"

class UProceduralMeshComponent;
class UMaterialInterface;
class UPointLightComponent;

UCLASS()
class BLOCKREALM_API ABlockChunk : public AActor
{
    GENERATED_BODY()

public:
    ABlockChunk();
    void InitializeChunk(const FIntPoint& Coordinate);
    void ApplyMesh(const FVoxelChunkMesh& Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Materials,
                   bool bImmediateCollision);
    // Apply only the authoritative collision geometry while an asynchronous visual mesh catches up.
    void ApplyCollision(const FVoxelChunkMesh& Mesh, bool bImmediateCollision = true);
    bool IsCollisionReady() const;
    FIntPoint GetChunkCoordinate() const { return ChunkCoordinate; }
    int64 GetVisibleFaceCount() const { return VisibleFaces; }
    uint64 GetMeshRevision() const { return MeshRevision; }
    uint64 GetCollisionRevision() const { return CollisionRevision; }

private:
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> TerrainMesh;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> GlassMesh;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> WaterMesh;
    UPROPERTY(VisibleAnywhere)
    TObjectPtr<UProceduralMeshComponent> CollisionMesh;

    // Transient cave lights are rebuilt with the visual mesh and disappear with
    // their owning chunk actor.  They are deliberately capped by VoxelData.
    UPROPERTY(Transient)
    TArray<TObjectPtr<UPointLightComponent>> GlowLights;

    FIntPoint ChunkCoordinate = FIntPoint::ZeroValue;
    int64 VisibleFaces = 0;
    uint64 MeshRevision = 0;
    uint64 CollisionRevision = 0;
    bool bHasMesh = false;
    bool bHasCollisionGeometry = false;

    void RebuildGlowLights(const TArray<FVector>& LocalPositions);
    void ClearGlowLights();
};
