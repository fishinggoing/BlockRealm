#include "BlockWorld.h"

#include "BlockChunk.h"
#include "VoxelData.h"
#include "Async/Async.h"
#include "CollisionQueryParams.h"
#include "Containers/Queue.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Templates/Atomic.h"
#include "UObject/UObjectGlobals.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"
#else
#include <cstdio>
#endif

DEFINE_LOG_CATEGORY_STATIC(LogBlockRealmWorld, Log, All);

struct FBlockWorldAsyncState
{
    TQueue<FVoxelChunkMesh, EQueueMode::Mpsc> CompletedMeshes;
    TAtomic<bool> bAcceptResults { true };
};

namespace
{
    int32 ChunkDistance(const FIntPoint& A, const FIntPoint& B)
    {
        return FMath::Max(FMath::Abs(A.X - B.X), FMath::Abs(A.Y - B.Y));
    }

    bool ReadSaveFile(const FString& Path, FString& OutJson)
    {
        const int64 Size = IFileManager::Get().FileSize(*Path);
        return Size > 0 && Size <= FVoxelData::MaxSaveBytes && FFileHelper::LoadFileToString(OutJson, *Path);
    }

    bool WriteAtomicSave(const FString& Path, const FString& Json)
    {
        IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
        if (!PlatformFile.CreateDirectoryTree(*FPaths::GetPath(Path))) return false;
        const FString TemporaryPath = Path + TEXT(".tmp-") + FGuid::NewGuid().ToString(EGuidFormats::Digits);
        const FTCHARToUTF8 UTF8(*Json);
        if (UTF8.Length() <= 0 || UTF8.Length() > FVoxelData::MaxSaveBytes) return false;
        TUniquePtr<IFileHandle> Handle(PlatformFile.OpenWrite(*TemporaryPath, false, false));
        if (!Handle) return false;
        const bool bWritten = Handle->Write(reinterpret_cast<const uint8*>(UTF8.Get()), UTF8.Length()) && Handle->Flush(true);
        Handle.Reset();
        bool bCommitted = false;
        if (bWritten)
        {
#if PLATFORM_WINDOWS
            FString NativeDestination = Path;
            FString NativeTemporary = TemporaryPath;
            FPaths::MakePlatformFilename(NativeDestination);
            FPaths::MakePlatformFilename(NativeTemporary);
            // Unlike delete-then-MoveFile, this preserves the last good file when replacement fails.
            bCommitted = !!::MoveFileExW(*NativeTemporary, *NativeDestination, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
#else
            const FTCHARToUTF8 DestinationUTF8(*Path);
            const FTCHARToUTF8 TemporaryUTF8(*TemporaryPath);
            bCommitted = std::rename(TemporaryUTF8.Get(), DestinationUTF8.Get()) == 0;
#endif
        }
        if (!bCommitted) PlatformFile.DeleteFile(*TemporaryPath);
        return bCommitted;
    }

    bool IsSpawnGround(EBlockType Type)
    {
        return FVoxelData::IsSolid(Type) && Type != EBlockType::Wood && Type != EBlockType::Leaves;
    }
}

ABlockWorld::ABlockWorld()
{
    PrimaryActorTick.bCanEverTick = true;
    PrimaryActorTick.TickGroup = TG_PrePhysics;
    // World streaming continues when the game is paused at a menu.
    PrimaryActorTick.bTickEvenWhenPaused = true;
    AsyncState = MakeShared<FBlockWorldAsyncState, ESPMode::ThreadSafe>();
}

void ABlockWorld::BeginPlay()
{
    Super::BeginPlay();
    StreamingRadius = FMath::Clamp(StreamingRadius, 2, 5);
    MaxConcurrentJobs = FMath::Clamp(FPlatformMisc::NumberOfCoresIncludingHyperthreads() / 2, 2, 4);
    LoadMaterials();
    FWorldSaveState SavedState;
    const bool bHasSave = LoadWorld(SavedState);
    const FVector InitialPosition = bHasSave ? SavedState.Position : GetSpawnLocation();
    InitialStreamingCenter = FVoxelData::ChunkFromBlock(WorldToBlock(InitialPosition));
    RequestStreaming(InitialPosition);
    StartPendingTasks();
    UE_LOG(LogBlockRealmWorld, Log, TEXT("World seed=%d, chunks=16x16x64, radius=%d, save=%s"), Seed, StreamingRadius, *GetSavePath());
}

void ABlockWorld::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bEndingPlay) return;
    ConsumeCompletedMeshes();
    if (!bInitialReady && IsInitialTerrainReady()) bInitialReady = true;
    FVector TargetPosition = LastStreamingPosition;
    if (APlayerController* Controller = GetWorld()->GetFirstPlayerController())
        if (APawn* Pawn = Controller->GetPawn()) TargetPosition = Pawn->GetActorLocation();
    RequestStreaming(TargetPosition);
    StartPendingTasks();
}

void ABlockWorld::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
    bEndingPlay = true;
    if (AsyncState) AsyncState->bAcceptResults.Store(false);
    // Workers retain their own value snapshots and mailbox, so they can finish after actor destruction.
    AsyncState.Reset();
    PendingJobs.Empty();
    DestroyAllChunks();
    Super::EndPlay(EndPlayReason);
}

void ABlockWorld::LoadMaterials()
{
    BlockMaterials.SetNum(static_cast<uint8>(EBlockType::Glow) + 1);
    const TCHAR* Names[] = { TEXT("Air"), TEXT("Grass"), TEXT("Dirt"), TEXT("Stone"), TEXT("Sand"),
        TEXT("Wood"), TEXT("Leaves"), TEXT("Glass"), TEXT("Water"), TEXT("Glow") };
    for (int32 Type = 1; Type < BlockMaterials.Num(); ++Type)
    {
        const FString Path = FString::Printf(TEXT("/Game/BlockRealm/Materials/M_%s.M_%s"), Names[Type], Names[Type]);
        BlockMaterials[Type] = LoadObject<UMaterialInterface>(nullptr, *Path);
        if (!BlockMaterials[Type])
        {
            BlockMaterials[Type] = UMaterial::GetDefaultMaterial(MD_Surface);
            UE_LOG(LogBlockRealmWorld, Warning, TEXT("Missing block material: %s"), *Path);
        }
    }
}

EBlockType ABlockWorld::GetBlock(const FIntVector& Position) const
{
    if (const EBlockType* Edited = BlockOverrides.Find(Position)) return *Edited;
    return FVoxelData::GeneratedBlock(Position, Seed);
}

bool ABlockWorld::SetBlock(const FIntVector& Position, EBlockType Type)
{
    check(IsInGameThread());
    // An immutable floor prevents the player from digging out of the finite-height world.
    if (!FVoxelData::IsValidPosition(Position) || Position.Z == 0 || !FVoxelData::IsLegalType(Type)
        || Type == EBlockType::Water) return false;
    if (GetBlock(Position) == Type) return false;
    const EBlockType Base = FVoxelData::GeneratedBlock(Position, Seed);
    if (Type == Base) BlockOverrides.Remove(Position);
    else
    {
        if (!BlockOverrides.Contains(Position) && BlockOverrides.Num() >= FVoxelData::MaxSavedEdits) return false;
        BlockOverrides.Add(Position, Type);
    }
    bSpawnLocationDirty = true;
    MarkBlockNeighborhoodDirty(Position);
    UpdateEditedCollision(Position);
    return true;
}

bool ABlockWorld::TraceBlocks(const FVector& Origin, const FVector& Direction, float MaxDistance,
    FIntVector& OutBlock, FIntVector& OutAdjacent) const
{
    if (Origin.ContainsNaN() || Direction.ContainsNaN() || !FMath::IsFinite(MaxDistance) || MaxDistance < 0.0f)
        return false;
    // Scale before normalizing so even very large finite input directions cannot overflow their length.
    const double DirectionScale = Direction.GetAbsMax();
    if (!FMath::IsFinite(DirectionScale) || DirectionScale == 0.0) return false;
    const FVector Ray = (Direction / DirectionScale).GetSafeNormal();
    const FVector GridOrigin = Origin / FVoxelData::BlockSize;
    const int32 Minimum[3] = { -FVoxelData::MaxCoordinate, -FVoxelData::MaxCoordinate, 0 };
    const int32 Maximum[3] = { FVoxelData::MaxCoordinate + 1, FVoxelData::MaxCoordinate + 1, FVoxelData::WorldHeight };
    double Entry = 0.0;
    double Exit = static_cast<double>(MaxDistance) / FVoxelData::BlockSize;
    int32 EntryAxis = INDEX_NONE;
    double EntryPlane = 0.0;
    // Clip to the finite, collidable world first. Below z=0 GetBlock has a generator sentinel,
    // but that sentinel is not an editable/rendered block and must not become a targeting hit.
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        if (Ray[Axis] == 0.0)
        {
            if (GridOrigin[Axis] < Minimum[Axis] || GridOrigin[Axis] >= Maximum[Axis]) return false;
            continue;
        }
        const double NearPlane = Ray[Axis] > 0.0 ? Minimum[Axis] : Maximum[Axis];
        const double FarPlane = Ray[Axis] > 0.0 ? Maximum[Axis] : Minimum[Axis];
        const double Near = (NearPlane - GridOrigin[Axis]) / Ray[Axis];
        const double Far = (FarPlane - GridOrigin[Axis]) / Ray[Axis];
        if (Near > Entry) { Entry = Near; EntryAxis = Axis; EntryPlane = NearPlane; }
        Exit = FMath::Min(Exit, Far);
        if (Entry > Exit) return false;
    }

    FVector GridStart = GridOrigin + Ray * Entry;
    // The slab calculation can lose low bits when an origin is far outside the world.
    if (EntryAxis != INDEX_NONE) GridStart[EntryAxis] = EntryPlane;
    FIntVector Cell = FIntVector::ZeroValue;
    FIntVector Step = FIntVector::ZeroValue;
    double Next[3];
    double Delta[3];
    int32 HitAxis = INDEX_NONE;
    double NearestBackFace = TNumericLimits<double>::Max();
    for (int32 Axis = 0; Axis < 3; ++Axis)
    {
        // An exact grid plane belongs to the cell the ray enters. A parallel ray keeps floor ownership.
        if (GridStart[Axis] < Minimum[Axis] || GridStart[Axis] > Maximum[Axis]) return false;
        Cell[Axis] = FMath::FloorToInt32(GridStart[Axis]);
        Step[Axis] = Ray[Axis] > 0.0 ? 1 : (Ray[Axis] < 0.0 ? -1 : 0);
        if (Step[Axis] < 0 && GridStart[Axis] == static_cast<double>(Cell[Axis])) --Cell[Axis];
        if (Step[Axis] == 0)
        {
            Next[Axis] = Delta[Axis] = TNumericLimits<double>::Max();
            continue;
        }
        const double ForwardPlane = Cell[Axis] + (Step[Axis] > 0 ? 1.0 : 0.0);
        Next[Axis] = Entry + (ForwardPlane - GridStart[Axis]) / Ray[Axis];
        Delta[Axis] = 1.0 / FMath::Abs(Ray[Axis]);
        // Starting inside a solid has no preceding empty sample. Use the closest face reached
        // by extending this ray backwards; strict comparison makes X/Y/Z ties deterministic.
        const double BackPlane = Cell[Axis] + (Step[Axis] < 0 ? 1.0 : 0.0);
        const double BackDistance = (GridStart[Axis] - BackPlane) / Ray[Axis];
        if (BackDistance < NearestBackFace) { NearestBackFace = BackDistance; HitAxis = Axis; }
    }

    while (FVoxelData::IsValidPosition(Cell))
    {
        if (FVoxelData::IsSolid(GetBlock(Cell)))
        {
            OutBlock = Cell;
            OutAdjacent = Cell;
            OutAdjacent[HitAxis] -= Step[HitAxis];
            return true;
        }
        const double Crossing = FMath::Min3(Next[0], Next[1], Next[2]);
        if (Crossing > Exit) break;
        const double TieTolerance = 1.0e-12 * FMath::Max(1.0, FMath::Abs(Crossing));
        HitAxis = INDEX_NONE;
        // Advance all coincident axes before sampling. Cells touched only along an edge or a
        // corner are skipped, and the first crossed axis supplies an actual face-sharing neighbor.
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            if (Step[Axis] != 0 && FMath::Abs(Next[Axis] - Crossing) <= TieTolerance)
            {
                if (HitAxis == INDEX_NONE) HitAxis = Axis;
                Cell[Axis] += Step[Axis];
                Next[Axis] += Delta[Axis];
            }
        }
    }
    return false;
}

FIntVector ABlockWorld::WorldToBlock(const FVector& Position)
{
    auto Convert = [](double Value)
    {
        if (!FMath::IsFinite(Value)) return 0;
        const double Blocks = FMath::Clamp(Value / FVoxelData::BlockSize, static_cast<double>(MIN_int32), static_cast<double>(MAX_int32));
        return FMath::FloorToInt32(Blocks);
    };
    return FIntVector(Convert(Position.X), Convert(Position.Y), Convert(Position.Z));
}

FVector ABlockWorld::BlockToWorld(const FIntVector& Position)
{
    return FVector((static_cast<double>(Position.X) + 0.5) * FVoxelData::BlockSize,
        (static_cast<double>(Position.Y) + 0.5) * FVoxelData::BlockSize,
        (static_cast<double>(Position.Z) + 0.5) * FVoxelData::BlockSize);
}

FVector ABlockWorld::GetSpawnLocation() const
{
    if (!bSpawnLocationDirty) return CachedSpawnLocation;
    auto TryColumn = [this](int32 X, int32 Y, FVector& OutPosition)
    {
        for (int32 Z = FVoxelData::WorldHeight - 4; Z >= 1; --Z)
        {
            if (!IsSpawnGround(GetBlock(FIntVector(X, Y, Z)))) continue;
            bool bClear = true;
            for (int32 Head = 1; Head <= 3; ++Head)
                if (GetBlock(FIntVector(X, Y, Z + Head)) != EBlockType::Air) { bClear = false; break; }
            if (!bClear) continue;
            // Default player capsule half-height is <=96cm, leaving 14cm above the ground.
            OutPosition = FVector((static_cast<double>(X) + 0.5) * FVoxelData::BlockSize,
                (static_cast<double>(Y) + 0.5) * FVoxelData::BlockSize, (Z + 1) * FVoxelData::BlockSize + 110.0);
            return true;
        }
        return false;
    };
    for (int32 Radius = 0; Radius <= 64; ++Radius)
    {
        for (int32 Y = -Radius; Y <= Radius; ++Y)
        {
            for (int32 X = -Radius; X <= Radius; ++X)
            {
                if (FMath::Max(FMath::Abs(X), FMath::Abs(Y)) != Radius) continue;
                if (TryColumn(X, Y, CachedSpawnLocation))
                {
                    bSpawnLocationDirty = false;
                    return CachedSpawnLocation;
                }
            }
        }
    }
    // An intentionally filled spawn area is exceptionally rare. Search dry deterministic terrain farther out.
    for (int32 Distance = 80; Distance <= 2048; Distance += 16)
        for (int32 Sign = -1; Sign <= 1; Sign += 2)
            if (TryColumn(Distance * Sign, Distance / 2, CachedSpawnLocation))
            {
                bSpawnLocationDirty = false;
                return CachedSpawnLocation;
            }
    // The protected z=0 floor always exists. This fallback stays above the world's finite volume.
    CachedSpawnLocation = FVector(50.0, 50.0, (FVoxelData::WorldHeight + 2) * FVoxelData::BlockSize);
    bSpawnLocationDirty = false;
    return CachedSpawnLocation;
}

void ABlockWorld::RequestStreaming(const FVector& Position)
{
    check(IsInGameThread());
    if (bEndingPlay || Position.ContainsNaN()) return;
    FIntVector Block = WorldToBlock(Position);
    const int32 SafeBound = FVoxelData::MaxCoordinate - FVoxelData::ChunkSize * (StreamingRadius + 2);
    Block.X = FMath::Clamp(Block.X, -SafeBound, SafeBound);
    Block.Y = FMath::Clamp(Block.Y, -SafeBound, SafeBound);
    const FIntPoint NewCenter = FVoxelData::ChunkFromBlock(Block);
    LastStreamingPosition = Position;
    if (!bStreamingInitialized)
    {
        InitialStreamingCenter = NewCenter;
        bStreamingInitialized = true;
    }
    StreamingCenter = NewCenter;
    // Readiness can be observed while the title camera is far from a saved player position.
    // Keep that collision until streaming observes the player arriving in the prepared area.
    if (bInitialReady && ChunkDistance(NewCenter, InitialStreamingCenter) <= 1) bKeepInitialChunks = false;
    RequestedChunks.Empty();
    for (int32 Y = -StreamingRadius; Y <= StreamingRadius; ++Y)
        for (int32 X = -StreamingRadius; X <= StreamingRadius; ++X)
            RequestedChunks.Add(NewCenter + FIntPoint(X, Y));
    if (bKeepInitialChunks)
        for (int32 Y = -1; Y <= 1; ++Y)
            for (int32 X = -1; X <= 1; ++X)
                RequestedChunks.Add(InitialStreamingCenter + FIntPoint(X, Y));

    for (auto Iterator = LoadedChunks.CreateIterator(); Iterator; ++Iterator)
    {
        const FIntPoint Coordinate = Iterator.Key();
        const bool bPinnedInitial = bKeepInitialChunks && ChunkDistance(Coordinate, InitialStreamingCenter) <= 1;
        if (ChunkDistance(Coordinate, NewCenter) > StreamingRadius + 1 && !bPinnedInitial)
        {
            if (IsValid(Iterator.Value())) Iterator.Value()->Destroy();
            DesiredRevisions.Remove(Coordinate);
            DirtyChunks.Remove(Coordinate);
            Iterator.RemoveCurrent();
        }
    }
    for (const FIntPoint& Coordinate : RequestedChunks)
        if (!LoadedChunks.Contains(Coordinate) && !DirtyChunks.Contains(Coordinate) && !PendingJobs.Contains(Coordinate)) MarkChunkDirty(Coordinate);
    for (auto Iterator = DirtyChunks.CreateIterator(); Iterator; ++Iterator)
    {
        if (!IsWanted(*Iterator))
        {
            DesiredRevisions.Remove(*Iterator);
            Iterator.RemoveCurrent();
        }
    }
}

bool ABlockWorld::IsWanted(const FIntPoint& Coordinate) const
{
    return RequestedChunks.Contains(Coordinate) || LoadedChunks.Contains(Coordinate);
}

bool ABlockWorld::IsInitialTerrainReady() const
{
    if (bInitialReady) return true;
    if (!bStreamingInitialized) return false;
    for (int32 Y = -1; Y <= 1; ++Y)
    {
        for (int32 X = -1; X <= 1; ++X)
        {
            const FIntPoint Coordinate = InitialStreamingCenter + FIntPoint(X, Y);
            const TObjectPtr<ABlockChunk>* Chunk = LoadedChunks.Find(Coordinate);
            const uint64* Desired = DesiredRevisions.Find(Coordinate);
            if (!Chunk || !IsValid(*Chunk) || !(*Chunk)->IsCollisionReady()
                || (Desired && (*Chunk)->GetMeshRevision() != *Desired)) return false;
        }
    }
    return true;
}

int32 ABlockWorld::GetPendingChunkCount() const
{
    return PendingJobs.Num() + DirtyChunks.Num();
}

int64 ABlockWorld::GetVisibleFaceCount() const
{
    int64 Faces = 0;
    for (const TPair<FIntPoint, TObjectPtr<ABlockChunk>>& Entry : LoadedChunks)
        if (IsValid(Entry.Value)) Faces += Entry.Value->GetVisibleFaceCount();
    return Faces;
}

void ABlockWorld::MarkChunkDirty(const FIntPoint& Coordinate)
{
    if (!IsWanted(Coordinate)) return;
    DesiredRevisions.Add(Coordinate, NextRevision++);
    DirtyChunks.Add(Coordinate);
}

void ABlockWorld::MarkBlockNeighborhoodDirty(const FIntVector& Position)
{
    const FIntPoint Chunk = FVoxelData::ChunkFromBlock(Position);
    MarkChunkDirty(Chunk);
    const int32 LocalX = Position.X - Chunk.X * FVoxelData::ChunkSize;
    const int32 LocalY = Position.Y - Chunk.Y * FVoxelData::ChunkSize;
    if (LocalX == 0) MarkChunkDirty(Chunk + FIntPoint(-1, 0));
    if (LocalX == FVoxelData::ChunkSize - 1) MarkChunkDirty(Chunk + FIntPoint(1, 0));
    if (LocalY == 0) MarkChunkDirty(Chunk + FIntPoint(0, -1));
    if (LocalY == FVoxelData::ChunkSize - 1) MarkChunkDirty(Chunk + FIntPoint(0, 1));
}

void ABlockWorld::UpdateEditedCollision(const FIntVector& Position)
{
    const FIntPoint Coordinate = FVoxelData::ChunkFromBlock(Position);
    auto Refresh = [this](const FIntPoint& ChunkCoordinate)
    {
        ABlockChunk* Chunk = LoadedChunks.FindRef(ChunkCoordinate);
        const uint64* Revision = DesiredRevisions.Find(ChunkCoordinate);
        if (!IsValid(Chunk) || !Revision) return;
        // Only collision is rebuilt on the caller's thread, for the edited chunk and any seam
        // neighbor it actually touches (at most three). Visual/material meshing remains asynchronous.
        Chunk->ApplyCollision(FVoxelData::BuildCollisionMesh(MakeBuildInput(ChunkCoordinate, *Revision)));
    };
    Refresh(Coordinate);
    const int32 LocalX = Position.X - Coordinate.X * FVoxelData::ChunkSize;
    const int32 LocalY = Position.Y - Coordinate.Y * FVoxelData::ChunkSize;
    if (LocalX == 0) Refresh(Coordinate + FIntPoint(-1, 0));
    if (LocalX == FVoxelData::ChunkSize - 1) Refresh(Coordinate + FIntPoint(1, 0));
    if (LocalY == 0) Refresh(Coordinate + FIntPoint(0, -1));
    if (LocalY == FVoxelData::ChunkSize - 1) Refresh(Coordinate + FIntPoint(0, 1));
}

FVoxelBuildInput ABlockWorld::MakeBuildInput(const FIntPoint& Coordinate, uint64 Revision) const
{
    FVoxelBuildInput Input;
    Input.Chunk = Coordinate;
    Input.Seed = Seed;
    Input.Revision = Revision;
    Input.Epoch = WorldEpoch;
    const int32 MinimumX = Coordinate.X * FVoxelData::ChunkSize - 1;
    const int32 MinimumY = Coordinate.Y * FVoxelData::ChunkSize - 1;
    for (const TPair<FIntVector, EBlockType>& Entry : BlockOverrides)
        if (Entry.Key.X >= MinimumX && Entry.Key.X <= MinimumX + FVoxelData::ChunkSize + 1
            && Entry.Key.Y >= MinimumY && Entry.Key.Y <= MinimumY + FVoxelData::ChunkSize + 1) Input.Overrides.Add(Entry.Key, Entry.Value);
    return Input;
}

void ABlockWorld::StartPendingTasks()
{
    if (!AsyncState || bEndingPlay) return;
    while (PendingJobs.Num() < MaxConcurrentJobs)
    {
        bool bFound = false;
        FIntPoint BestCoordinate = FIntPoint::ZeroValue;
        int64 BestScore = MAX_int64;
        for (const FIntPoint& Coordinate : DirtyChunks)
        {
            if (PendingJobs.Contains(Coordinate) || !IsWanted(Coordinate)) continue;
            const FIntPoint Delta = Coordinate - StreamingCenter;
            int64 Score = static_cast<int64>(Delta.X) * Delta.X + static_cast<int64>(Delta.Y) * Delta.Y;
            if (!bInitialReady && ChunkDistance(Coordinate, InitialStreamingCenter) <= 1) Score -= 1000000000000LL;
            if (!bFound || Score < BestScore) { BestCoordinate = Coordinate; BestScore = Score; bFound = true; }
        }
        if (!bFound) break;
        const uint64 Revision = DesiredRevisions.FindChecked(BestCoordinate);
        FVoxelBuildInput Input = MakeBuildInput(BestCoordinate, Revision);
        const TSharedPtr<FBlockWorldAsyncState, ESPMode::ThreadSafe> Mailbox = AsyncState;
        PendingJobs.Add(BestCoordinate, Revision);
        DirtyChunks.Remove(BestCoordinate);
        Async(EAsyncExecution::ThreadPool, [Input = MoveTemp(Input), Mailbox]() mutable
        {
            if (!Mailbox->bAcceptResults.Load()) return;
            FVoxelChunkMesh Mesh = FVoxelData::BuildMesh(Input);
            if (Mailbox->bAcceptResults.Load()) Mailbox->CompletedMeshes.Enqueue(MoveTemp(Mesh));
        });
    }
}

void ABlockWorld::ConsumeCompletedMeshes()
{
    if (!AsyncState) return;
    const double StartTime = FPlatformTime::Seconds();
    int32 Applied = 0;
    FVoxelChunkMesh Mesh;
    while (Applied < 2 && AsyncState->CompletedMeshes.Dequeue(Mesh))
    {
        const uint64* Pending = PendingJobs.Find(Mesh.Chunk);
        if (Pending && *Pending == Mesh.Revision) PendingJobs.Remove(Mesh.Chunk);
        const uint64* Desired = DesiredRevisions.Find(Mesh.Chunk);
        if (!IsWanted(Mesh.Chunk))
        {
            DesiredRevisions.Remove(Mesh.Chunk);
            continue;
        }
        if (Mesh.Epoch != WorldEpoch || !Desired || *Desired != Mesh.Revision) continue;
        ABlockChunk* Chunk = LoadedChunks.FindRef(Mesh.Chunk);
        if (!IsValid(Chunk))
        {
            FActorSpawnParameters Parameters;
            Parameters.Owner = this;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            Chunk = GetWorld()->SpawnActor<ABlockChunk>(ABlockChunk::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
            if (!Chunk) { MarkChunkDirty(Mesh.Chunk); continue; }
            Chunk->InitializeChunk(Mesh.Chunk);
            LoadedChunks.Add(Mesh.Chunk, Chunk);
        }
        const bool bImmediateCollision = ChunkDistance(Mesh.Chunk, StreamingCenter) <= 1
            || (bKeepInitialChunks && ChunkDistance(Mesh.Chunk, InitialStreamingCenter) <= 1);
        Chunk->ApplyMesh(Mesh, BlockMaterials, bImmediateCollision);
        ++Applied;
        if (FPlatformTime::Seconds() - StartTime > 0.006) break;
    }
}

void ABlockWorld::InvalidateMeshes()
{
    ++WorldEpoch;
    if (AsyncState) AsyncState->bAcceptResults.Store(false);
    AsyncState = MakeShared<FBlockWorldAsyncState, ESPMode::ThreadSafe>();
    PendingJobs.Empty();
    DirtyChunks.Empty();
    DesiredRevisions.Empty();
    for (const FIntPoint& Coordinate : RequestedChunks) MarkChunkDirty(Coordinate);
    for (const TPair<FIntPoint, TObjectPtr<ABlockChunk>>& Entry : LoadedChunks)
        if (!DirtyChunks.Contains(Entry.Key)) MarkChunkDirty(Entry.Key);
}

void ABlockWorld::DestroyAllChunks()
{
    for (const TPair<FIntPoint, TObjectPtr<ABlockChunk>>& Entry : LoadedChunks)
        if (IsValid(Entry.Value)) Entry.Value->Destroy();
    LoadedChunks.Empty();
}

FString ABlockWorld::GetSavePath() const
{
    if (!SelfTestSavePath.IsEmpty()) return SelfTestSavePath;
    const bool bSmoke = FParse::Param(FCommandLine::Get(), TEXT("BlockRealmSmoke"));
    FString RoofProbeMaterial;
    const bool bLightingProbe = FParse::Value(FCommandLine::Get(), TEXT("BRRoofLighting="), RoofProbeMaterial)
        || FParse::Param(FCommandLine::Get(), TEXT("BRMouthLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRCaveLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRSurfaceLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRNightLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRDawnLighting"))
        || FParse::Param(FCommandLine::Get(), TEXT("BRDuskLighting"));
    return FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
        bSmoke ? TEXT("ValidationWorld") : bLightingProbe ? TEXT("LightingProbeWorld") : TEXT("Worlds"), TEXT("BlockRealmWorld.json")));
}

bool ABlockWorld::SaveWorld(const FWorldSaveState& State)
{
    check(IsInGameThread());
    FVoxelSaveRecord Record;
    Record.Seed = Seed;
    Record.Player = State;
    Record.Overrides = BlockOverrides;
    FString Json;
    if (!FVoxelData::SerializeRecord(Record, Json))
    {
        UE_LOG(LogBlockRealmWorld, Warning, TEXT("Save rejected: invalid player state or edits"));
        return false;
    }
    const bool bSaved = WriteAtomicSave(GetSavePath(), Json);
    if (!bSaved) UE_LOG(LogBlockRealmWorld, Error, TEXT("Failed atomic world save: %s"), *GetSavePath());
    return bSaved;
}

bool ABlockWorld::LoadWorld(FWorldSaveState& OutState)
{
    check(IsInGameThread());
    FString Json;
    if (!ReadSaveFile(GetSavePath(), Json)) return false;
    FVoxelSaveRecord Record;
    if (!FVoxelData::ParseRecord(Json, Record))
    {
        UE_LOG(LogBlockRealmWorld, Warning, TEXT("Invalid save ignored without altering world: %s"), *GetSavePath());
        return false;
    }
    const bool bChanged = Seed != Record.Seed || !BlockOverrides.OrderIndependentCompareEqual(Record.Overrides);
    Seed = Record.Seed;
    BlockOverrides = MoveTemp(Record.Overrides);
    OutState = MoveTemp(Record.Player);
    bSpawnLocationDirty = true;
    bInitialReady = false;
    bKeepInitialChunks = true;
    InitialStreamingCenter = FVoxelData::ChunkFromBlock(WorldToBlock(OutState.Position));
    if (bStreamingInitialized) RequestStreaming(OutState.Position);
    if (bChanged) InvalidateMeshes();
    return true;
}

void ABlockWorld::ResetWorld()
{
    check(IsInGameThread());
    if (IFileManager::Get().FileExists(*GetSavePath()) && !IFileManager::Get().Delete(*GetSavePath(), false, false, true))
        UE_LOG(LogBlockRealmWorld, Error, TEXT("Could not remove world save: %s"), *GetSavePath());
    Seed = FVoxelData::DefaultSeed;
    BlockOverrides.Empty();
    bSpawnLocationDirty = true;
    bInitialReady = false;
    bKeepInitialChunks = true;
    bStreamingInitialized = false;
    DestroyAllChunks();
    RequestedChunks.Empty();
    InvalidateMeshes();
    RequestStreaming(GetSpawnLocation());
    StartPendingTasks();
}

bool ABlockWorld::RunSelfTests(TArray<FString>& Failures)
{
    check(IsInGameThread());
    FVoxelData::RunSaveSelfTests(Failures);
    const int32 StartingFailures = Failures.Num();
    auto Expect = [&Failures](bool bCondition, const TCHAR* Message)
    {
        if (!bCondition) Failures.Add(Message);
    };
    const int32 SavedSeed = Seed;
    const TMap<FIntVector, EBlockType> SavedOverrides = BlockOverrides;
    const FString OriginalTestPath = SelfTestSavePath;
    const FVector SavedStreamingPosition = LastStreamingPosition;
    const FIntPoint SavedInitialCenter = InitialStreamingCenter;
    const bool bSavedInitialReady = bInitialReady;
    const bool bSavedStreamingInitialized = bStreamingInitialized;
    const FString TestDirectory = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(),
        TEXT("ValidationWorld"), TEXT("SelfTests"), FGuid::NewGuid().ToString(EGuidFormats::Digits)));
    SelfTestSavePath = FPaths::Combine(TestDirectory, TEXT("World.json"));

    Expect(WorldToBlock(FVector(-0.01, -100.0, -100.01)) == FIntVector(-1, -1, -2), TEXT("WorldToBlock must floor actual negative world coordinates"));
    Expect(FVoxelData::ChunkFromBlock(FIntVector(-1, -16, 60)) == FIntPoint(-1, -1), TEXT("Negative chunk coordinates must use floor division"));
    Expect(FVoxelData::ChunkFromBlock(FIntVector(-17, -16, 60)) == FIntPoint(-2, -1), TEXT("Negative chunk border must map to the left chunk"));
    Expect(WorldToBlock(BlockToWorld(FIntVector(-17, 16, 60))) == FIntVector(-17, 16, 60), TEXT("Negative block centers must round trip"));

    const FIntVector Left(-1, 2, 60);
    const FIntVector Right(0, 2, 60);
    const FIntVector NegativeBorder(-17, -16, 59);
    BlockOverrides.Remove(Left);
    BlockOverrides.Remove(Right);
    BlockOverrides.Remove(NegativeBorder);
    MarkBlockNeighborhoodDirty(Left);
    MarkBlockNeighborhoodDirty(Right);
    Expect(SetBlock(Left, EBlockType::Glass) && SetBlock(Right, EBlockType::Stone), TEXT("Real SetBlock must support edits on both sides of x=0"));
    Expect(SetBlock(NegativeBorder, EBlockType::Glow), TEXT("Real SetBlock must edit a negative chunk seam"));
    Expect(GetBlock(Left) == EBlockType::Glass && GetBlock(Right) == EBlockType::Stone
        && GetBlock(NegativeBorder) == EBlockType::Glow, TEXT("GetBlock must return real edits across unloaded and loaded chunks"));
    Expect(!SetBlock(FIntVector(2, 2, 61), EBlockType::Water), TEXT("Water placement must be rejected"));
    Expect(!SetBlock(FIntVector(2, 2, 0), EBlockType::Air), TEXT("The world foundation must remain collidable"));
    Expect(!SetBlock(FIntVector(2, 2, 64), EBlockType::Stone), TEXT("Edits above the finite world must be rejected"));
    const FIntPoint LeftChunk(-1, 0);
    const FIntPoint RightChunk(0, 0);
    if (IsWanted(LeftChunk) && IsWanted(RightChunk))
        Expect(DirtyChunks.Contains(LeftChunk) && DirtyChunks.Contains(RightChunk), TEXT("Boundary edits must invalidate both actual neighboring meshes"));

    // Isolate ray fixtures from arbitrary player edits. z=60 has no generated terrain/vegetation.
    // Only these tests inject water directly; it is removed before exercising real save validation.
    const TMap<FIntVector, EBlockType> BeforeTraceTests = BlockOverrides;
    BlockOverrides.Empty();
    const FIntVector RayBlock(-17, -16, 60);
    const FIntVector RayBehind(-16, -16, 60);
    const FVector RayOrigin = BlockToWorld(FIntVector(-20, -16, 60));
    BlockOverrides.Add(RayBlock, EBlockType::Glass);
    FIntVector HitBlock;
    FIntVector HitAdjacent;
    Expect(TraceBlocks(RayOrigin, FVector(2.0, 0.0, 0.0), 250.0f, HitBlock, HitAdjacent)
        && HitBlock == RayBlock && HitAdjacent == RayBlock - FIntVector(1, 0, 0),
        TEXT("Data ray must normalize its direction, hit the negative chunk seam at exact reach and return its entry face"));
    HitBlock = HitAdjacent = FIntVector(99, 98, 97);
    Expect(!TraceBlocks(RayOrigin, FVector::ForwardVector, 249.9f, HitBlock, HitAdjacent)
        && HitBlock == FIntVector(99, 98, 97) && HitAdjacent == FIntVector(99, 98, 97),
        TEXT("Data ray must respect reach and preserve outputs on a miss"));
    const FVector RayCenter = BlockToWorld(RayBlock);
    Expect(TraceBlocks(RayCenter + FVector(50.0, 0.0, 0.0), -FVector::ForwardVector, 0.0f, HitBlock, HitAdjacent)
        && HitBlock == RayBlock && HitAdjacent == RayBehind,
        TEXT("Exact negative-direction grid boundary must enter the cell below the plane"));
    Expect(TraceBlocks(RayCenter - FVector(50.0, 0.0, 0.0), FVector::ForwardVector, 0.0f, HitBlock, HitAdjacent)
        && HitBlock == RayBlock && HitAdjacent == RayBlock - FIntVector(1, 0, 0),
        TEXT("Exact positive-direction grid boundary must enter the cell above the plane"));
    Expect(!TraceBlocks(RayCenter - FVector(50.0, 0.0, 0.0), -FVector::ForwardVector, 0.0f, HitBlock, HitAdjacent),
        TEXT("A ray starting on a solid face and pointing away must not hit the cell behind it"));
    Expect(TraceBlocks(RayCenter, FVector::ForwardVector, 0.0f, HitBlock, HitAdjacent)
        && HitBlock == RayBlock && HitAdjacent == RayBlock - FIntVector(1, 0, 0),
        TEXT("A ray starting inside a solid must hit immediately with a defined backward entry face"));
    const FIntVector FaceOffsets[] = { FIntVector(1, 0, 0), FIntVector(-1, 0, 0), FIntVector(0, 1, 0),
        FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
    for (const FIntVector& FaceOffset : FaceOffsets)
    {
        const FVector FaceDirection(FaceOffset);
        Expect(TraceBlocks(RayCenter + FaceDirection * 200.0, -FaceDirection, 150.0f, HitBlock, HitAdjacent)
            && HitBlock == RayBlock && HitAdjacent == RayBlock + FaceOffset,
            TEXT("Every axis and direction must return the actual face-sharing placement cell"));
    }
    const FIntVector WaterCell = RayBlock - FIntVector(1, 0, 0);
    BlockOverrides.Add(WaterCell, EBlockType::Water);
    Expect(TraceBlocks(RayOrigin, FVector::ForwardVector, 300.0f, HitBlock, HitAdjacent) && HitBlock == RayBlock,
        TEXT("Data targeting must pass through water and hit the solid behind it"));
    BlockOverrides.Remove(RayBlock);
    Expect(!TraceBlocks(RayOrigin, FVector::ForwardVector, 300.0f, HitBlock, HitAdjacent),
        TEXT("Water alone must not become a data-ray hit"));
    BlockOverrides.Remove(WaterCell);

    const FVector DiagonalOrigin = BlockToWorld(FIntVector(2, 2, 60));
    BlockOverrides.Add(FIntVector(3, 2, 60), EBlockType::Glow);
    BlockOverrides.Add(FIntVector(2, 3, 60), EBlockType::Glass);
    BlockOverrides.Add(FIntVector(3, 3, 60), EBlockType::Stone);
    Expect(TraceBlocks(DiagonalOrigin, FVector(1.0, 1.0, 0.0), 100.0f, HitBlock, HitAdjacent)
        && HitBlock == FIntVector(3, 3, 60) && HitAdjacent == FIntVector(2, 3, 60),
        TEXT("An exact edge crossing must skip edge-only cells and deterministically choose a real X face"));
    BlockOverrides.Remove(FIntVector(3, 3, 60));
    BlockOverrides.Add(FIntVector(4, 4, 60), EBlockType::Stone);
    Expect(TraceBlocks(DiagonalOrigin, FVector(1.0, 1.0, 0.0), 300.0f, HitBlock, HitAdjacent)
        && HitBlock == FIntVector(4, 4, 60) && HitAdjacent == FIntVector(3, 4, 60),
        TEXT("Solid cells touched only at an edge must not hide the next traversed solid"));
    BlockOverrides.Add(FIntVector(3, 3, 61), EBlockType::Stone);
    Expect(TraceBlocks(DiagonalOrigin, FVector(1.0, 1.0, 1.0), 200.0f, HitBlock, HitAdjacent)
        && HitBlock == FIntVector(3, 3, 61) && HitAdjacent == FIntVector(2, 3, 61),
        TEXT("A three-axis corner hit must choose one face neighbor rather than a diagonal placement cell"));
    BlockOverrides.Add(FIntVector(-4, -4, 60), EBlockType::Stone);
    Expect(TraceBlocks(BlockToWorld(FIntVector(-3, -3, 60)), FVector(-1.0, -1.0, 0.0), 100.0f, HitBlock, HitAdjacent)
        && HitBlock == FIntVector(-4, -4, 60) && HitAdjacent == FIntVector(-3, -4, 60),
        TEXT("Negative diagonal crossings must retain floor coordinates and a face-sharing neighbor"));
    BlockOverrides.Add(RayBlock, EBlockType::Glass);
    Expect(TraceBlocks(FVector(RayCenter.X, RayCenter.Y, 6500.0), -FVector::UpVector, 400.0f, HitBlock, HitAdjacent)
        && HitBlock == RayBlock && HitAdjacent == RayBlock + FIntVector(0, 0, 1),
        TEXT("A ray above world height must enter at the top bound and retain its exact downward face"));
    Expect(!TraceBlocks(FVector(RayCenter.X, RayCenter.Y, -1.0), -FVector::UpVector, 100.0f, HitBlock, HitAdjacent),
        TEXT("The generated below-world sentinel must not be targeted as a rendered block"));
    Expect(TraceBlocks(FVector(RayCenter.X, RayCenter.Y, -100.0), FVector::UpVector, 100.0f, HitBlock, HitAdjacent)
        && HitBlock == FIntVector(RayBlock.X, RayBlock.Y, 0) && HitAdjacent == FIntVector(RayBlock.X, RayBlock.Y, -1),
        TEXT("A ray entering from below the world must hit the real foundation face at exact reach"));
    Expect(!TraceBlocks(RayOrigin, FVector::ZeroVector, 700.0f, HitBlock, HitAdjacent)
        && !TraceBlocks(RayOrigin, FVector::ForwardVector, -1.0f, HitBlock, HitAdjacent),
        TEXT("Zero directions and negative reach must be rejected"));

    BlockOverrides.Add(RayBehind, EBlockType::Stone);
    Expect(SetBlock(RayBlock, EBlockType::Air)
        && TraceBlocks(RayOrigin, FVector::ForwardVector, 400.0f, HitBlock, HitAdjacent) && HitBlock == RayBehind,
        TEXT("A mine must expose the next authoritative solid before any worker or tick runs"));
    Expect(SetBlock(RayBlock, EBlockType::Glow)
        && TraceBlocks(RayOrigin, FVector::ForwardVector, 400.0f, HitBlock, HitAdjacent) && HitBlock == RayBlock,
        TEXT("A placement must be immediately targetable before its asynchronous visual mesh exists"));

    // Query the actual physics scene without advancing a tick or consuming worker results.
    // Use an existing central chunk when available, otherwise a temporary chunk for direct self-test calls.
    const FIntPoint PhysicsCoordinate = bSavedStreamingInitialized ? SavedInitialCenter : FIntPoint::ZeroValue;
    TArray<FIntPoint> TemporaryPhysicsChunks;
    auto PreparePhysicsChunk = [this, &TemporaryPhysicsChunks](const FIntPoint& Coordinate) -> ABlockChunk*
    {
        ABlockChunk* Chunk = LoadedChunks.FindRef(Coordinate);
        if (IsValid(Chunk) || !GetWorld()) return Chunk;
        FActorSpawnParameters Parameters;
        Parameters.Owner = this;
        Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Chunk = GetWorld()->SpawnActor<ABlockChunk>(ABlockChunk::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Parameters);
        if (Chunk)
        {
            Chunk->InitializeChunk(Coordinate);
            LoadedChunks.Add(Coordinate, Chunk);
            MarkChunkDirty(Coordinate);
            Chunk->ApplyMesh(FVoxelData::BuildMesh(MakeBuildInput(Coordinate, DesiredRevisions.FindChecked(Coordinate))),
                BlockMaterials, true);
            TemporaryPhysicsChunks.Add(Coordinate);
        }
        return Chunk;
    };
    ABlockChunk* PhysicsChunk = PreparePhysicsChunk(PhysicsCoordinate);
    const FIntPoint PhysicsNeighborCoordinate = PhysicsCoordinate + FIntPoint(-1, 0);
    ABlockChunk* PhysicsNeighbor = PreparePhysicsChunk(PhysicsNeighborCoordinate);
    Expect(IsValid(PhysicsChunk) && IsValid(PhysicsNeighbor), TEXT("Immediate collision self tests must have actual registered neighboring chunks"));
    const FIntVector PhysicsCell(PhysicsCoordinate.X * FVoxelData::ChunkSize + 5,
        PhysicsCoordinate.Y * FVoxelData::ChunkSize + 5, 60);
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmImmediateEditSelfTest), true);
    if (GetWorld())
        if (APlayerController* Controller = GetWorld()->GetFirstPlayerController()) Query.AddIgnoredActor(Controller->GetPawn());
    FHitResult PhysicsHit;
    if (IsValid(PhysicsChunk))
    {
        const uint64 VisualRevisionBeforeEdit = PhysicsChunk->GetMeshRevision();
        const FVector PhysicsStart = BlockToWorld(PhysicsCell) + FVector(0.0, 0.0, 200.0);
        const FVector PhysicsEnd = BlockToWorld(PhysicsCell) - FVector(0.0, 0.0, 200.0);
        Expect(SetBlock(PhysicsCell, EBlockType::Stone) && PhysicsChunk->IsCollisionReady()
            && PhysicsChunk->GetCollisionRevision() == DesiredRevisions.FindChecked(PhysicsCoordinate)
            && PhysicsChunk->GetMeshRevision() == VisualRevisionBeforeEdit
            && GetWorld()->LineTraceSingleByChannel(PhysicsHit, PhysicsStart, PhysicsEnd, ECC_Pawn, Query)
            && PhysicsHit.GetActor() == PhysicsChunk
            && FMath::IsNearlyEqual(PhysicsHit.ImpactPoint.Z, (PhysicsCell.Z + 1) * FVoxelData::BlockSize, 0.01),
            TEXT("SetBlock placement must install the real Pawn collision surface immediately while visual work is pending"));
        Expect(SetBlock(PhysicsCell, EBlockType::Air) && PhysicsChunk->IsCollisionReady()
            && PhysicsChunk->GetCollisionRevision() == DesiredRevisions.FindChecked(PhysicsCoordinate)
            && !GetWorld()->LineTraceSingleByChannel(PhysicsHit, PhysicsStart, PhysicsEnd, ECC_Pawn, Query),
            TEXT("SetBlock mining must remove actual Pawn collision immediately, without a worker completion or tick"));
    }
    const FIntVector PhysicsSeamCell(PhysicsCoordinate.X * FVoxelData::ChunkSize, PhysicsCell.Y, PhysicsCell.Z);
    if (IsValid(PhysicsChunk) && IsValid(PhysicsNeighbor))
    {
        const FIntVector NeighborCell = PhysicsSeamCell - FIntVector(1, 0, 0);
        const FVector SeamStart = BlockToWorld(PhysicsSeamCell) + FVector(150.0, 0.0, 0.0);
        const FVector SeamEnd = BlockToWorld(NeighborCell);
        Expect(SetBlock(NeighborCell, EBlockType::Stone) && SetBlock(PhysicsSeamCell, EBlockType::Stone)
            && PhysicsChunk->GetCollisionRevision() == DesiredRevisions.FindChecked(PhysicsCoordinate)
            && PhysicsNeighbor->GetCollisionRevision() == DesiredRevisions.FindChecked(PhysicsNeighborCoordinate)
            && GetWorld()->LineTraceSingleByChannel(PhysicsHit, SeamStart, SeamEnd, ECC_Pawn, Query)
            && PhysicsHit.GetActor() == PhysicsChunk,
            TEXT("Seam placement must immediately update actual collision on both loaded sides of the boundary"));
        const FVoxelChunkMesh StaleCollision = FVoxelData::BuildCollisionMesh(
            MakeBuildInput(PhysicsCoordinate, PhysicsChunk->GetCollisionRevision()));
        Expect(SetBlock(PhysicsSeamCell, EBlockType::Air)
            && PhysicsNeighbor->GetCollisionRevision() == DesiredRevisions.FindChecked(PhysicsNeighborCoordinate)
            && GetWorld()->LineTraceSingleByChannel(PhysicsHit, SeamStart, SeamEnd, ECC_Pawn, Query)
            && PhysicsHit.GetActor() == PhysicsNeighbor
            && FMath::IsNearlyEqual(PhysicsHit.ImpactPoint.X, PhysicsSeamCell.X * FVoxelData::BlockSize, 0.01),
            TEXT("Mining a seam cell must immediately expose the neighboring chunk's real collision face"));
        PhysicsChunk->ApplyCollision(StaleCollision);
        Expect(PhysicsChunk->GetCollisionRevision() == DesiredRevisions.FindChecked(PhysicsCoordinate)
            && GetWorld()->LineTraceSingleByChannel(PhysicsHit, SeamStart, SeamEnd, ECC_Pawn, Query)
            && PhysicsHit.GetActor() == PhysicsNeighbor,
            TEXT("An older collision snapshot must not resurrect a mined seam cell"));
    }
    BlockOverrides = BeforeTraceTests;
    MarkBlockNeighborhoodDirty(RayBlock);
    UpdateEditedCollision(RayBlock);
    MarkBlockNeighborhoodDirty(PhysicsCell);
    UpdateEditedCollision(PhysicsCell);
    MarkBlockNeighborhoodDirty(PhysicsSeamCell);
    UpdateEditedCollision(PhysicsSeamCell);
    for (const FIntPoint& Coordinate : TemporaryPhysicsChunks)
    {
        if (ABlockChunk* TemporaryChunk = LoadedChunks.FindRef(Coordinate)) TemporaryChunk->Destroy();
        LoadedChunks.Remove(Coordinate);
        DirtyChunks.Remove(Coordinate);
        DesiredRevisions.Remove(Coordinate);
    }

    const FVoxelBuildInput LeftInput = MakeBuildInput(LeftChunk, 123);
    TArray<EBlockType> Snapshot;
    FVoxelData::BuildChunkBlocks(LeftInput, Snapshot);
    Expect(FVoxelData::BufferBlock(Snapshot, FIntVector(15, 2, 60)) == GetBlock(Left), TEXT("Worker snapshot must include the edited edge cell"));
    Expect(FVoxelData::BufferBlock(Snapshot, FIntVector(16, 2, 60)) == GetBlock(Right), TEXT("Worker halo must include the edited neighboring chunk"));
    for (int32 Y = -1; Y <= FVoxelData::ChunkSize; Y += 3)
        for (int32 X = -1; X <= FVoxelData::ChunkSize; X += 3)
            for (int32 Z = 0; Z < FVoxelData::WorldHeight; Z += 3)
                Expect(FVoxelData::BufferBlock(Snapshot, FIntVector(X, Y, Z)) == GetBlock(FIntVector(X - 16, Y, Z)),
                    TEXT("Generated terrain and tree snapshot must match deterministic GetBlock"));
    FVoxelBuildInput ControlledInput = LeftInput;
    ControlledInput.Overrides.Empty();
    ControlledInput.Overrides.Add(Left, EBlockType::Glass);
    ControlledInput.Overrides.Add(Right, EBlockType::Stone);
    const FVoxelChunkMesh ClosedSeam = FVoxelData::BuildMesh(ControlledInput);
    // At z=60 no generated vegetation exists; the controlled glass cube touches one solid neighbor.
    Expect(ClosedSeam.Sections[static_cast<uint8>(EBlockType::Glass)].Triangles.Num() == 30,
        TEXT("Meshing must hide the glass face against the solid halo neighbor"));
    FVoxelBuildInput OpenInput = ControlledInput;
    OpenInput.Overrides.Add(Right, EBlockType::Air);
    const FVoxelChunkMesh OpenSeam = FVoxelData::BuildMesh(OpenInput);
    Expect(OpenSeam.Sections[static_cast<uint8>(EBlockType::Glass)].Triangles.Num() == 36,
        TEXT("Meshing must expose the cross-chunk face after the neighbor is removed"));
    Expect(ClosedSeam.QuadCount < ClosedSeam.VisibleFaces && !ClosedSeam.CollisionTriangles.IsEmpty(),
        TEXT("Greedy meshing must reduce exposed unit faces and create real collision triangles"));
    FVoxelBuildInput WaterInput = ControlledInput;
    WaterInput.Overrides.Empty();
    const FVoxelChunkMesh WithoutWaterCube = FVoxelData::BuildMesh(WaterInput);
    const FVoxelChunkMesh CollisionWithoutWater = FVoxelData::BuildCollisionMesh(WaterInput);
    WaterInput.Overrides.Add(Left, EBlockType::Water);
    const FVoxelChunkMesh WithWaterCube = FVoxelData::BuildMesh(WaterInput);
    const FVoxelChunkMesh CollisionWithWater = FVoxelData::BuildCollisionMesh(WaterInput);
    Expect(WithWaterCube.Sections[static_cast<uint8>(EBlockType::Water)].Triangles.Num()
        > WithoutWaterCube.Sections[static_cast<uint8>(EBlockType::Water)].Triangles.Num()
        && WithWaterCube.CollisionTriangles.Num() == WithoutWaterCube.CollisionTriangles.Num(),
        TEXT("Visible water surfaces must never add collision triangles"));
    Expect(CollisionWithWater.Sections.IsEmpty() && !CollisionWithWater.CollisionTriangles.IsEmpty()
        && CollisionWithWater.CollisionTriangles == CollisionWithoutWater.CollisionTriangles
        && CollisionWithWater.CollisionVertices == CollisionWithoutWater.CollisionVertices,
        TEXT("Collision-only rebuilding must keep identical solid geometry across water edits and allocate no visual sections"));
    FVoxelBuildInput GlowLightInput = ControlledInput;
    GlowLightInput.Overrides.Empty();
    GlowLightInput.Overrides.Add(Left, EBlockType::Glow);
    const FVoxelChunkMesh GlowLightMesh = FVoxelData::BuildMesh(GlowLightInput);
    Expect(GlowLightMesh.GlowLightPositions.Num() == 1,
        TEXT("An air-exposed Glow block must produce one bounded local cave-light anchor"));
    bool bClockwiseWinding = true;
    for (const FVoxelMeshSection& Section : ClosedSeam.Sections)
        for (int32 Index = 0; Index < Section.Triangles.Num(); Index += 3)
        {
            const int32 A = Section.Triangles[Index];
            const int32 B = Section.Triangles[Index + 1];
            const int32 C = Section.Triangles[Index + 2];
            bClockwiseWinding &= FVector::DotProduct(FVector::CrossProduct(Section.Vertices[B] - Section.Vertices[A],
                Section.Vertices[C] - Section.Vertices[A]), Section.Normals[A]) < 0.0;
        }
    Expect(bClockwiseWinding, TEXT("Actual generated triangle winding must face outward with Unreal's clockwise convention"));
    bool bSeedChangesTerrain = false;
    for (int32 X = 80; X < 160; X += 7)
        bSeedChangesTerrain |= FVoxelData::TerrainHeight(X, -83, SavedSeed) != FVoxelData::TerrainHeight(X, -83, SavedSeed ^ 0x12345);
    Expect(bSeedChangesTerrain, TEXT("Different seeds must change real terrain"));

    // Cave/ore fixtures are found away from the protected spawn clearing.  The
    // seam probe deliberately lands on the last column of a chunk so the halo
    // generated by both workers can be compared without loading either actor.
    FIntVector CaveProbe = FIntVector::ZeroValue;
    FIntVector CaveSeamProbe = FIntVector::ZeroValue;
    FIntVector GlowProbe = FIntVector::ZeroValue;
    bool bFoundCave = false;
    bool bFoundCaveSeam = false;
    bool bFoundGlow = false;
    for (int32 Y = -96; Y <= 96 && (!bFoundCave || !bFoundCaveSeam || !bFoundGlow); ++Y)
    {
        for (int32 X = -96; X <= 96 && (!bFoundCave || !bFoundCaveSeam || !bFoundGlow); ++X)
        {
            for (int32 Z = FVoxelData::CaveMinZ; Z <= 34 && (!bFoundCave || !bFoundCaveSeam || !bFoundGlow); ++Z)
            {
                const FIntVector Candidate(X, Y, Z);
                if (!bFoundCave && FVoxelData::IsCaveCell(Candidate, SavedSeed))
                {
                    CaveProbe = Candidate;
                    bFoundCave = true;
                }
                if (!bFoundGlow && FVoxelData::IsGlowOreCell(Candidate, SavedSeed))
                {
                    GlowProbe = Candidate;
                    bFoundGlow = true;
                }
                const int32 PositiveModulo = ((X % FVoxelData::ChunkSize) + FVoxelData::ChunkSize) % FVoxelData::ChunkSize;
                if (!bFoundCaveSeam && PositiveModulo == FVoxelData::ChunkSize - 1
                    && FVoxelData::IsCaveCell(Candidate, SavedSeed))
                {
                    CaveSeamProbe = Candidate;
                    bFoundCaveSeam = true;
                }
            }
        }
    }
    Expect(bFoundCave, TEXT("Deterministic terrain must contain at least one underground cave cell"));
    Expect(bFoundGlow, TEXT("Underground stone must expose collectible Glow ore in a depth layer"));
    if (bFoundCave)
    {
        Expect(FVoxelData::GeneratedBlock(CaveProbe, SavedSeed) == EBlockType::Air,
            TEXT("Cave cells must be authoritative air in GeneratedBlock"));
    }
    if (bFoundGlow)
    {
        Expect(FVoxelData::GeneratedBlock(GlowProbe, SavedSeed) == EBlockType::Glow,
            TEXT("Glow ore cells must use the existing Glow block type"));
    }
    if (bFoundCaveSeam)
    {
        const FIntPoint CaveChunk = FVoxelData::ChunkFromBlock(CaveSeamProbe);
        const FIntPoint CaveNeighborChunk = CaveChunk + FIntPoint(1, 0);
        FVoxelBuildInput CaveLeftInput;
        CaveLeftInput.Chunk = CaveChunk;
        CaveLeftInput.Seed = SavedSeed;
        CaveLeftInput.Revision = 301;
        FVoxelBuildInput CaveRightInput = CaveLeftInput;
        CaveRightInput.Chunk = CaveNeighborChunk;
        CaveRightInput.Revision = 302;
        TArray<EBlockType> CaveLeftBlocks;
        TArray<EBlockType> CaveRightBlocks;
        FVoxelData::BuildChunkBlocks(CaveLeftInput, CaveLeftBlocks);
        FVoxelData::BuildChunkBlocks(CaveRightInput, CaveRightBlocks);
        const FIntVector LeftLocal = CaveSeamProbe - FIntVector(CaveChunk.X * FVoxelData::ChunkSize,
            CaveChunk.Y * FVoxelData::ChunkSize, 0);
        const FIntVector RightLocal = CaveSeamProbe + FIntVector(1, 0, 0)
            - FIntVector(CaveNeighborChunk.X * FVoxelData::ChunkSize,
                CaveNeighborChunk.Y * FVoxelData::ChunkSize, 0);
        Expect(FVoxelData::BufferBlock(CaveLeftBlocks, LeftLocal) == FVoxelData::GeneratedBlock(CaveSeamProbe, SavedSeed)
            && FVoxelData::BufferBlock(CaveLeftBlocks, LeftLocal + FIntVector(1, 0, 0))
                == FVoxelData::BufferBlock(CaveRightBlocks, RightLocal)
            && FVoxelData::BufferBlock(CaveRightBlocks, RightLocal) == FVoxelData::GeneratedBlock(CaveSeamProbe + FIntVector(1, 0, 0), SavedSeed),
            TEXT("Cave generation must be deterministic across a chunk seam and its one-cell halo"));
        TArray<EBlockType> CaveRepeat;
        FVoxelData::BuildChunkBlocks(CaveLeftInput, CaveRepeat);
        Expect(CaveRepeat == CaveLeftBlocks, TEXT("Repeating a cave chunk build must produce identical block data"));
    }
    const FVector Spawn = GetSpawnLocation();
    const FIntVector SpawnCell = WorldToBlock(Spawn);
    // The capsule center is about 90 cm above the ground face, so its
    // WorldToBlock cell is two indices above the supporting block.  Check the
    // actual support and every cell intersected by the capsule instead of
    // assuming the center cell is immediately adjacent to the ground.
    Expect(FVoxelData::IsSolid(GetBlock(SpawnCell - FIntVector(0, 0, 2)))
        && GetBlock(SpawnCell - FIntVector(0, 0, 1)) == EBlockType::Air
        && GetBlock(SpawnCell) == EBlockType::Air
        && GetBlock(SpawnCell + FIntVector(0, 0, 1)) == EBlockType::Air,
        TEXT("Spawn capsule must be clear of water, wood and leaves"));
    for (int32 Head = 0; Head <= 2; ++Head)
        Expect(!FVoxelData::IsCaveCell(SpawnCell + FIntVector(0, 0, Head), SavedSeed),
            TEXT("The protected spawn clearing must not be carved into a cave"));

    int32 DynamicGI = 0;
    int32 DiffuseIndirect = 0;
    int32 SkyLightCapture = 0;
    int32 CachedPreExposure = 0;
    int32 HeightFogOnGI = 1;
    auto ReadConfigFlag = [](const TCHAR* Section, const TCHAR* Key, bool& OutValue)
    {
        FString Value;
        if (!GConfig || !GConfig->GetString(Section, Key, Value, GEngineIni)) return false;
        OutValue = Value.Equals(FString(TEXT("True")), ESearchCase::IgnoreCase) || Value == TEXT("1");
        return true;
    };
    bool bHardwareLumen = false;
    bool bRayTracing = false;
    const bool bLightingConfig = GConfig
        && GConfig->GetInt(TEXT("/Script/Engine.RendererSettings"), TEXT("r.DynamicGlobalIlluminationMethod"), DynamicGI, GEngineIni)
        && ReadConfigFlag(TEXT("/Script/Engine.RendererSettings"), TEXT("r.Lumen.HardwareRayTracing"), bHardwareLumen)
        && ReadConfigFlag(TEXT("/Script/Engine.RendererSettings"), TEXT("r.RayTracing"), bRayTracing)
        && GConfig->GetInt(TEXT("ConsoleVariables"), TEXT("r.Lumen.DiffuseIndirect.Allow"), DiffuseIndirect, GEngineIni)
        && GConfig->GetInt(TEXT("ConsoleVariables"), TEXT("r.SkyLight.RealTimeReflectionCapture"), SkyLightCapture, GEngineIni)
        && GConfig->GetInt(TEXT("ConsoleVariables"), TEXT("r.EyeAdaptation.CachedLightingPreExposure"), CachedPreExposure, GEngineIni)
        && GConfig->GetInt(TEXT("ConsoleVariables"), TEXT("r.Lumen.HeightFogOnGI"), HeightFogOnGI, GEngineIni);
    Expect(bLightingConfig && DynamicGI == 1 && bHardwareLumen && bRayTracing
        && DiffuseIndirect != 0 && SkyLightCapture != 0 && CachedPreExposure >= 4 && HeightFogOnGI == 0,
        TEXT("Underground lighting must keep Lumen/RT/GI enabled and exclude height fog from GI"));
    UE_LOG(LogBlockRealmWorld, Log, TEXT("Cave self-test: cave=%s seam=%s glow=%s spawn=%s lighting(GI=%d HWLumen=%d RT=%d SkyCapture=%d PreExposure=%d Diffuse=%d FogOnGI=%d)"),
        bFoundCave ? *CaveProbe.ToString() : TEXT("none"),
        bFoundCaveSeam ? *CaveSeamProbe.ToString() : TEXT("none"),
        bFoundGlow ? *GlowProbe.ToString() : TEXT("none"), *SpawnCell.ToString(),
        DynamicGI, bHardwareLumen ? 1 : 0, bRayTracing ? 1 : 0, SkyLightCapture, CachedPreExposure, DiffuseIndirect, HeightFogOnGI);

    FWorldSaveState TestState;
    TestState.Position = Spawn;
    TestState.Rotation = FRotator(17.0, 33.0, 0.0);
    TestState.SelectedSlot = 8;
    TestState.Inventory.Init(64, 9);
    TestState.Inventory[8] = 93;
    TestState.bHasSurvivalState = true;
    TestState.bBareHandsEquipped = true;
    TestState.ToolSlots.SetNum(FVoxelData::SavedToolSlotCount);
    // Exercise seed persistence, not merely the initial default value.
    Seed = SavedSeed ^ 0x4567;
    bSpawnLocationDirty = true;
    const int32 TestSeed = Seed;
    Expect(SaveWorld(TestState), TEXT("SaveWorld must atomically write the isolated real save"));
    FString GoodJson;
    const bool bReadGood = ReadSaveFile(SelfTestSavePath, GoodJson);
    Expect(bReadGood, TEXT("Saved JSON must exist on disk"));
    SetBlock(Left, EBlockType::Air);
    SetBlock(Right, EBlockType::Air);
    SetBlock(NegativeBorder, EBlockType::Air);
    Seed = FVoxelData::DefaultSeed;
    FWorldSaveState Restored;
    const bool bLoaded = LoadWorld(Restored);
    Expect(bLoaded && Seed == TestSeed && GetBlock(Left) == EBlockType::Glass && GetBlock(Right) == EBlockType::Stone
        && GetBlock(NegativeBorder) == EBlockType::Glow, TEXT("Save/load round trip must restore seed and actual boundary edits"));
    Expect(bLoaded && Restored.Position.Equals(TestState.Position, 0.001) && Restored.Rotation.Equals(TestState.Rotation, 0.001)
        && Restored.SelectedSlot == 8 && Restored.Inventory == TestState.Inventory
        && Restored.bHasSurvivalState && Restored.bBareHandsEquipped && Restored.SelectedToolSlot == -1
        && Restored.ToolSlots.Num() == FVoxelData::SavedToolSlotCount,
        TEXT("Save/load must restore explicit bare hands, the selected block, player transform and all nine inventory slots"));
    TArray<FString> SaveFiles;
    IFileManager::Get().FindFiles(SaveFiles, *FPaths::Combine(TestDirectory, TEXT("*")), true, false);
    Expect(SaveFiles.Num() == 1 && SaveFiles[0] == FPaths::GetCleanFilename(SelfTestSavePath),
        TEXT("Bare-hands persistence must use only the existing world JSON without sidecars or leftover temporary files"));
    FWorldSaveState InvalidState = TestState;
    InvalidState.Inventory[0] = -1;
    Expect(!SaveWorld(InvalidState), TEXT("Invalid inventory must be rejected before file replacement"));
    FString AfterRejectedSave;
    Expect(bReadGood && ReadSaveFile(SelfTestSavePath, AfterRejectedSave) && AfterRejectedSave == GoodJson,
        TEXT("Rejected save must preserve the last valid disk file"));
    FVoxelSaveRecord Parsed;
    Expect(!FVoxelData::ParseRecord(TEXT("{\"format\":\"BlockRealmWorld\",\"version\":1}"), Parsed), TEXT("Truncated save schema must be rejected"));
    if (bReadGood)
    {
        FString FractionalJson = GoodJson;
        FractionalJson.ReplaceInline(TEXT("\"selectedSlot\":8"), TEXT("\"selectedSlot\":8.5"));
        Expect(!FVoxelData::ParseRecord(FractionalJson, Parsed), TEXT("Fractional inventory slot must be rejected"));
        Expect(WriteAtomicSave(SelfTestSavePath, TEXT("{\"broken\":true}")), TEXT("Corruption fixture must use the isolated path"));
        const int32 BeforeInvalidLoadSeed = Seed;
        const TMap<FIntVector, EBlockType> BeforeInvalidLoadEdits = BlockOverrides;
        FWorldSaveState Untouched = TestState;
        Expect(!LoadWorld(Untouched) && Seed == BeforeInvalidLoadSeed && BlockOverrides.OrderIndependentCompareEqual(BeforeInvalidLoadEdits)
            && Untouched.Inventory == TestState.Inventory, TEXT("Corrupt LoadWorld must preserve live world and output state"));
    }
    if (FParse::Param(FCommandLine::Get(), TEXT("BlockRealmSmoke")))
    {
        SelfTestSavePath.Empty();
        Expect(GetSavePath().Contains(TEXT("ValidationWorld")), TEXT("Smoke mode must isolate its real save path"));
    }

    // Restore the caller's state even when a check failed; obsolete worker revisions cannot overwrite it.
    Seed = SavedSeed;
    BlockOverrides = SavedOverrides;
    SelfTestSavePath = OriginalTestPath;
    bSpawnLocationDirty = true;
    InitialStreamingCenter = SavedInitialCenter;
    bInitialReady = bSavedInitialReady;
    bKeepInitialChunks = true;
    if (bSavedStreamingInitialized)
    {
        bStreamingInitialized = true;
        RequestStreaming(SavedStreamingPosition);
        InvalidateMeshes();
        // Existing meshes are retained for display, but initial readiness must wait for restored collision.
        bInitialReady = false;
    }
    else
    {
        RequestedChunks.Empty();
        bStreamingInitialized = false;
        InvalidateMeshes();
    }
    IFileManager::Get().DeleteDirectory(*TestDirectory, false, true);
    UE_LOG(LogBlockRealmWorld, Log, TEXT("World self tests: %s (%d new failures)"), Failures.Num() == StartingFailures ? TEXT("PASS") : TEXT("FAIL"), Failures.Num() - StartingFailures);
    return Failures.Num() == StartingFailures;
}
