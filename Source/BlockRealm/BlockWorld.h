#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockTypes.h"
#include "BlockWorld.generated.h"

class ABlockChunk;
class UMaterialInterface;
struct FBlockWorldAsyncState;
struct FVoxelBuildInput;

UCLASS()
class BLOCKREALM_API ABlockWorld : public AActor
{
    GENERATED_BODY()

public:
    ABlockWorld();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    EBlockType GetBlock(const FIntVector& Position) const;
    bool SetBlock(const FIntVector& Position, EBlockType Type);
    // Trace authoritative voxel data without depending on a rendered or cooked chunk mesh.
    // OutAdjacent shares the chosen entry face; exact edge/corner ties prefer X, then Y, then Z.
    bool TraceBlocks(const FVector& Origin, const FVector& Direction, float MaxDistance,
        FIntVector& OutBlock, FIntVector& OutAdjacent) const;
    static FIntVector WorldToBlock(const FVector& Position);
    static FVector BlockToWorld(const FIntVector& Position);
    FVector GetSpawnLocation() const;
    void RequestStreaming(const FVector& Position);
    bool IsInitialTerrainReady() const;
    int32 GetLoadedChunkCount() const { return LoadedChunks.Num(); }
    int32 GetPendingChunkCount() const;
    int64 GetVisibleFaceCount() const;
    int32 GetSeed() const { return Seed; }
    bool SaveWorld(const FWorldSaveState& State);
    bool LoadWorld(FWorldSaveState& OutState);
    FString GetSavePath() const;
    void ResetWorld();
    bool RunSelfTests(TArray<FString>& Failures);

protected:
    UPROPERTY(EditAnywhere, Category = "BlockRealm|World")
    int32 Seed = 42137;
    UPROPERTY(EditAnywhere, Category = "BlockRealm|World", meta = (ClampMin = "2", ClampMax = "5"))
    int32 StreamingRadius = 5;

private:
    void LoadMaterials();
    void MarkChunkDirty(const FIntPoint& Coordinate);
    void MarkBlockNeighborhoodDirty(const FIntVector& Position);
    void UpdateEditedCollision(const FIntVector& Position);
    void StartPendingTasks();
    void ConsumeCompletedMeshes();
    void InvalidateMeshes();
    void DestroyAllChunks();
    bool IsWanted(const FIntPoint& Coordinate) const;
    FVoxelBuildInput MakeBuildInput(const FIntPoint& Coordinate, uint64 Revision) const;

    UPROPERTY(Transient)
    TMap<FIntPoint, TObjectPtr<ABlockChunk>> LoadedChunks;
    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInterface>> BlockMaterials;

    TMap<FIntVector, EBlockType> BlockOverrides;
    TSet<FIntPoint> RequestedChunks;
    TSet<FIntPoint> DirtyChunks;
    TMap<FIntPoint, uint64> DesiredRevisions;
    TMap<FIntPoint, uint64> PendingJobs;
    TSharedPtr<FBlockWorldAsyncState, ESPMode::ThreadSafe> AsyncState;
    uint64 NextRevision = 1;
    uint64 WorldEpoch = 1;
    int32 MaxConcurrentJobs = 3;
    FIntPoint StreamingCenter = FIntPoint::ZeroValue;
    FIntPoint InitialStreamingCenter = FIntPoint::ZeroValue;
    FVector LastStreamingPosition = FVector::ZeroVector;
    mutable FVector CachedSpawnLocation = FVector::ZeroVector;
    mutable bool bSpawnLocationDirty = true;
    bool bStreamingInitialized = false;
    bool bInitialReady = false;
    bool bKeepInitialChunks = true;
    bool bEndingPlay = false;
    // Only set by RunSelfTests. Its round trip never touches the player's actual save.
    FString SelfTestSavePath;
};
