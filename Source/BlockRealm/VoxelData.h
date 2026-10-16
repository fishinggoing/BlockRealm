#pragma once

#include "CoreMinimal.h"
#include "BlockTypes.h"

// These types contain values only. Mesh workers never capture an actor, component, or material.
struct FVoxelMeshTangent
{
    FVector X = FVector::ForwardVector;
    bool bFlipY = false;
};

struct FVoxelMeshSection
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector> Normals;
    TArray<FVector2D> UVs;
    // R is quantized local sky access; A=0 identifies voxel vertices to the
    // shared material. G/B are reserved and carry no lighting semantics.
    TArray<FLinearColor> Colors;
    TArray<FVoxelMeshTangent> Tangents;
};

struct FVoxelBuildInput
{
    FIntPoint Chunk = FIntPoint::ZeroValue;
    int32 Seed = 42137;
    uint64 Revision = 0;
    uint64 Epoch = 0;
    TMap<FIntVector, EBlockType> Overrides;
};

struct FVoxelChunkMesh
{
    FIntPoint Chunk = FIntPoint::ZeroValue;
    uint64 Revision = 0;
    uint64 Epoch = 0;
    TArray<FVoxelMeshSection> Sections;
    // Solid-only greedy geometry, identical for BuildMesh/BuildCollisionMesh.
    // Visual material, transparency and sky-level splits cannot subdivide it.
    TArray<FVector> CollisionVertices;
    TArray<int32> CollisionTriangles;
    // A tiny, deterministic set of local light anchors for visible Glow ore
    // exposed to an air cavity.  The chunk actor owns the transient point-light
    // components; workers only return value data.
    TArray<FVector> GlowLightPositions;
    int64 VisibleFaces = 0;
    int32 QuadCount = 0;
};

struct FVoxelSaveRecord
{
    int32 Seed = 42137;
    FWorldSaveState Player;
    TMap<FIntVector, EBlockType> Overrides;
};

class BLOCKREALM_API FVoxelData
{
public:
    static constexpr int32 ChunkSize = 16;
    static constexpr int32 WorldHeight = 64;
    static constexpr int32 SeaLevel = 15;
    static constexpr int32 DefaultSeed = 42137;
    static constexpr double BlockSize = 100.0;
    static constexpr int32 MaxCoordinate = 1000000;
    static constexpr int32 MaxSavedEdits = 100000;
    static constexpr int64 MaxSaveBytes = 16 * 1024 * 1024;
    static constexpr int32 SavedToolSlotCount = 6;
    static constexpr int32 MaxSavedToolDurability = 131;
    static constexpr int32 MaxSavedToolQuantity = 64;
    static constexpr int32 MaxSavedTorches = 512;
    static constexpr int32 MaxSavedCrops = 256;
    static constexpr int32 MaxSavedRestPoints = 32;
    static constexpr int32 MaxSavedFurnaces = 64;
    static constexpr int32 MaxSavedFurnaceStack = 64;
    static constexpr float MaxCropGrowthSeconds = 120.f;
    // Completed batches are committed by gameplay; only [0, 8) is saved in flight.
    static constexpr float FurnaceCookSeconds = 8.f;
    // Underground generation is intentionally expressed in block units so it stays
    // deterministic for both an unloaded GetBlock query and an async chunk worker.
    // The protected radius keeps the deterministic spawn clearing and its initial
    // 3x3 streaming area on solid, surface terrain.
    static constexpr int32 CaveMinZ = 3;
    static constexpr int32 CaveSurfaceBuffer = 5;
    static constexpr int32 CaveProtectedRadius = 14;
    static constexpr int32 CavePrimaryScale = 32;
    static constexpr int32 CaveDetailScale = 12;
    static constexpr double CaveDensityThreshold = 0.22;
    static constexpr int32 BufferWidth = ChunkSize + 2;
    static constexpr int32 BufferHeight = WorldHeight + 2;

    static int32 FloorDivide(int32 Value, int32 Divisor);
    static FIntPoint ChunkFromBlock(const FIntVector& Position);
    static bool IsValidPosition(const FIntVector& Position);
    static bool IsLegalType(EBlockType Type);
    static bool IsSolid(EBlockType Type);
    static int32 TerrainHeight(int32 X, int32 Y, int32 Seed);
    static EBlockType GeneratedBlock(const FIntVector& Position, int32 Seed);
    // Procedural cave/ore queries deliberately ignore sparse edits.  They are
    // useful for diagnostics and let self-tests assert cross-chunk stability.
    static bool IsCaveCell(const FIntVector& Position, int32 Seed);
    static bool IsGlowOreCell(const FIntVector& Position, int32 Seed);

    // The one-cell halo is generated from the same seed, including trees from neighboring chunks.
    static void BuildChunkBlocks(const FVoxelBuildInput& Input, TArray<EBlockType>& OutBlocks);
    static EBlockType BufferBlock(const TArray<EBlockType>& Blocks, const FIntVector& LocalPosition);
    static FVoxelChunkMesh BuildMesh(const FVoxelBuildInput& Input);
    // Greedy solid-only geometry: no materials, UVs, tangents or visual sections are built.
    static FVoxelChunkMesh BuildCollisionMesh(const FVoxelBuildInput& Input);

    static bool ValidatePlayerState(const FWorldSaveState& State);
    static bool SerializeRecord(const FVoxelSaveRecord& Record, FString& OutJson);
    // Parse into a temporary record; malformed files must never partially alter the live world.
    static bool ParseRecord(const FString& Json, FVoxelSaveRecord& OutRecord);
    // Pure save-schema and mesh checks reached by the existing world self-tests.
    static bool RunSaveSelfTests(TArray<FString>& Failures);
    // Pure worker/mesh checks, also reached by the existing self-test entry point.
    static bool RunMeshSelfTests(TArray<FString>& Failures);

private:
    static FVoxelChunkMesh BuildMeshInternal(const FVoxelBuildInput& Input, bool bCollisionOnly);
};
