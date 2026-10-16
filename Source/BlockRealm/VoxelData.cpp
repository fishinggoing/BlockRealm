#include "VoxelData.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

#include <cmath>
#include <limits>

namespace
{
    uint32 HashCoordinates(int32 X, int32 Y, int32 Seed, uint32 Salt)
    {
        uint32 Hash = static_cast<uint32>(Seed) ^ Salt;
        Hash ^= static_cast<uint32>(X) * 0x9E3779B9u;
        Hash ^= static_cast<uint32>(Y) * 0x85EBCA6Bu;
        Hash ^= Hash >> 16;
        Hash *= 0x7FEB352Du;
        Hash ^= Hash >> 15;
        Hash *= 0x846CA68Bu;
        return Hash ^ (Hash >> 16);
    }

    // The 3-D hash uses the same finalizer as the existing 2-D terrain hash.
    // Keeping the coordinates as integers means cave and ore decisions do not
    // depend on chunk load order, floating point world positions, or a worker's
    // local origin.
    uint32 HashCoordinates3D(int32 X, int32 Y, int32 Z, int32 Seed, uint32 Salt)
    {
        uint32 Hash = static_cast<uint32>(Seed) ^ Salt;
        Hash ^= static_cast<uint32>(X) * 0x9E3779B9u;
        Hash ^= static_cast<uint32>(Y) * 0x85EBCA6Bu;
        Hash ^= static_cast<uint32>(Z) * 0xC2B2AE35u;
        Hash ^= Hash >> 16;
        Hash *= 0x7FEB352Du;
        Hash ^= Hash >> 15;
        Hash *= 0x846CA68Bu;
        return Hash ^ (Hash >> 16);
    }

    double LatticeValue(int32 X, int32 Y, int32 Seed, uint32 Salt)
    {
        return (HashCoordinates(X, Y, Seed, Salt) & 0x00FFFFFFu) / 8388607.5 - 1.0;
    }

    double LatticeValue3D(int32 X, int32 Y, int32 Z, int32 Seed, uint32 Salt)
    {
        return (HashCoordinates3D(X, Y, Z, Seed, Salt) & 0x00FFFFFFu) / 8388607.5 - 1.0;
    }

    double ValueNoise(int32 X, int32 Y, int32 Scale, int32 Seed, uint32 Salt)
    {
        const int32 CellX = FVoxelData::FloorDivide(X, Scale);
        const int32 CellY = FVoxelData::FloorDivide(Y, Scale);
        double U = static_cast<double>(X - CellX * Scale) / Scale;
        double V = static_cast<double>(Y - CellY * Scale) / Scale;
        U = U * U * (3.0 - 2.0 * U);
        V = V * V * (3.0 - 2.0 * V);
        return FMath::Lerp(
            FMath::Lerp(LatticeValue(CellX, CellY, Seed, Salt), LatticeValue(CellX + 1, CellY, Seed, Salt), U),
            FMath::Lerp(LatticeValue(CellX, CellY + 1, Seed, Salt), LatticeValue(CellX + 1, CellY + 1, Seed, Salt), U), V);
    }

    double ValueNoise3D(int32 X, int32 Y, int32 Z, int32 Scale, int32 Seed, uint32 Salt)
    {
        const int32 CellX = FVoxelData::FloorDivide(X, Scale);
        const int32 CellY = FVoxelData::FloorDivide(Y, Scale);
        const int32 CellZ = FVoxelData::FloorDivide(Z, Scale);
        double U = static_cast<double>(X - CellX * Scale) / Scale;
        double V = static_cast<double>(Y - CellY * Scale) / Scale;
        double W = static_cast<double>(Z - CellZ * Scale) / Scale;
        U = U * U * (3.0 - 2.0 * U);
        V = V * V * (3.0 - 2.0 * V);
        W = W * W * (3.0 - 2.0 * W);

        const double C000 = LatticeValue3D(CellX, CellY, CellZ, Seed, Salt);
        const double C100 = LatticeValue3D(CellX + 1, CellY, CellZ, Seed, Salt);
        const double C010 = LatticeValue3D(CellX, CellY + 1, CellZ, Seed, Salt);
        const double C110 = LatticeValue3D(CellX + 1, CellY + 1, CellZ, Seed, Salt);
        const double C001 = LatticeValue3D(CellX, CellY, CellZ + 1, Seed, Salt);
        const double C101 = LatticeValue3D(CellX + 1, CellY, CellZ + 1, Seed, Salt);
        const double C011 = LatticeValue3D(CellX, CellY + 1, CellZ + 1, Seed, Salt);
        const double C111 = LatticeValue3D(CellX + 1, CellY + 1, CellZ + 1, Seed, Salt);
        const double Low = FMath::Lerp(FMath::Lerp(C000, C100, U), FMath::Lerp(C010, C110, U), V);
        const double High = FMath::Lerp(FMath::Lerp(C001, C101, U), FMath::Lerp(C011, C111, U), V);
        return FMath::Lerp(Low, High, W);
    }

    EBlockType GroundBlock(int32 Height, int32 Z)
    {
        if (Z < 0) return EBlockType::Stone;
        if (Z >= FVoxelData::WorldHeight) return EBlockType::Air;
        if (Z <= Height)
        {
            if (Z == 0) return EBlockType::Stone;
            const bool bBeach = Height <= FVoxelData::SeaLevel + 1;
            if (Z == Height) return bBeach ? EBlockType::Sand : EBlockType::Grass;
            if (Z >= Height - 3) return bBeach ? EBlockType::Sand : EBlockType::Dirt;
            return EBlockType::Stone;
        }
        return Z <= FVoxelData::SeaLevel ? EBlockType::Water : EBlockType::Air;
    }

    bool IsWithinCaveProtectedRadius(int32 X, int32 Y)
    {
        const int64 DistanceSquared = static_cast<int64>(X) * X + static_cast<int64>(Y) * Y;
        return DistanceSquared <= static_cast<int64>(FVoxelData::CaveProtectedRadius)
            * FVoxelData::CaveProtectedRadius;
    }

    bool IsCaveVolume(const FIntVector& Position, int32 Height, int32 Seed)
    {
        // Leave a solid roof under the surface and a foundation above z=0.  Low
        // beaches and the sea never enter this branch because they do not expose
        // enough stone depth for a cave.
        if (Position.Z < FVoxelData::CaveMinZ || Height < FVoxelData::SeaLevel + FVoxelData::CaveSurfaceBuffer
            || Position.Z > Height - FVoxelData::CaveSurfaceBuffer
            || IsWithinCaveProtectedRadius(Position.X, Position.Y)) return false;

        // A broad field gives connected chambers while a smaller field breaks up
        // the edges into walkable tunnels.  Absolute values make the low-density
        // center of each field the cavity rather than isolated salt-and-pepper
        // holes; all samples are integer-coordinate deterministic.
        const double Primary = FMath::Abs(ValueNoise3D(Position.X, Position.Y, Position.Z,
            FVoxelData::CavePrimaryScale, Seed, 0xCA7E1234u));
        const double Detail = FMath::Abs(ValueNoise3D(Position.X, Position.Y, Position.Z,
            FVoxelData::CaveDetailScale, Seed, 0x71B5D3E9u));
        const double Density = Primary * 0.72 + Detail * 0.28;
        return Density < FVoxelData::CaveDensityThreshold;
    }

    bool IsGlowOreVolume(const FIntVector& Position, int32 Height, int32 Seed)
    {
        if (Position.Z <= 1 || Position.Z >= FVoxelData::WorldHeight || Position.Z > Height
            || Height - Position.Z < FVoxelData::CaveSurfaceBuffer
            || IsWithinCaveProtectedRadius(Position.X, Position.Y)) return false;

        // One four-by-four-by-two hash cell selects a vein, then a second hash
        // gives sparse blocks within it.  All depth tiers use Glow so save files
        // and the existing nine-slot inventory remain compatible; the depth tiers
        // provide coal/iron/gold/diamond-like rarity without adding EBlockType.
        const int32 ClusterX = FVoxelData::FloorDivide(Position.X, 4);
        const int32 ClusterY = FVoxelData::FloorDivide(Position.Y, 4);
        const int32 ClusterZ = FVoxelData::FloorDivide(Position.Z, 2);
        if (HashCoordinates3D(ClusterX, ClusterY, ClusterZ, Seed, 0xD15EA5E5u) % 100u >= 16u) return false;

        const int32 LayerChancePerThousand = Position.Z <= 10 ? 70
            : (Position.Z <= 20 ? 55 : (Position.Z <= 32 ? 42 : 20));
        return HashCoordinates3D(Position.X, Position.Y, Position.Z, Seed, 0x0FE5EED1u)
            % 1000u < static_cast<uint32>(LayerChancePerThousand);
    }

    EBlockType BaseTerrainBlockAtHeight(const FIntVector& Position, int32 Height, int32 Seed)
    {
        const EBlockType Ground = GroundBlock(Height, Position.Z);
        if (Ground != EBlockType::Stone) return Ground;
        if (IsCaveVolume(Position, Height, Seed)) return EBlockType::Air;
        if (IsGlowOreVolume(Position, Height, Seed)) return EBlockType::Glow;
        return Ground;
    }

    EBlockType BaseTerrainBlock(const FIntVector& Position, int32 Seed)
    {
        if (Position.X < -FVoxelData::MaxCoordinate || Position.X > FVoxelData::MaxCoordinate
            || Position.Y < -FVoxelData::MaxCoordinate || Position.Y > FVoxelData::MaxCoordinate)
            return EBlockType::Air;
        return BaseTerrainBlockAtHeight(Position, FVoxelData::TerrainHeight(Position.X, Position.Y, Seed), Seed);
    }

    bool HasTreeRoot(int32 X, int32 Y, int32 Seed, int32& OutGround, int32& OutHeight)
    {
        // A dry clearing at the origin also excludes neighboring canopies.
        if (FMath::Abs(X) <= 8 && FMath::Abs(Y) <= 8) return false;
        const int32 CellX = FVoxelData::FloorDivide(X, 5);
        const int32 CellY = FVoxelData::FloorDivide(Y, 5);
        const uint32 Hash = HashCoordinates(CellX, CellY, Seed, 0xB5297A4Du);
        if (Hash % 100u >= 38u || X != CellX * 5 + 1 + static_cast<int32>((Hash >> 8) % 3u)
            || Y != CellY * 5 + 1 + static_cast<int32>((Hash >> 16) % 3u)) return false;
        OutGround = FVoxelData::TerrainHeight(X, Y, Seed);
        OutHeight = 4 + static_cast<int32>((Hash >> 24) % 3u);
        return OutGround > FVoxelData::SeaLevel + 2 && OutGround < FVoxelData::WorldHeight - 10;
    }

    bool HasCanopy(int32 DX, int32 DY, int32 DZ)
    {
        if (DZ < -2 || DZ > 1) return false;
        const int32 Radius = DZ == 1 ? 1 : 2;
        return FMath::Abs(DX) <= Radius && FMath::Abs(DY) <= Radius
            && (Radius == 1 || FMath::Abs(DX) + FMath::Abs(DY) <= 3);
    }

    int32 BufferIndex(const FIntVector& Position)
    {
        return (Position.X + 1) + FVoxelData::BufferWidth * ((Position.Y + 1)
            + FVoxelData::BufferWidth * (Position.Z + 1));
    }

    bool WithinBuffer(const FIntVector& Position)
    {
        return Position.X >= -1 && Position.X <= FVoxelData::ChunkSize
            && Position.Y >= -1 && Position.Y <= FVoxelData::ChunkSize
            && Position.Z >= -1 && Position.Z <= FVoxelData::WorldHeight;
    }

    constexpr uint8 MaxSkyLevel = 15;

    float SkyTransmission(EBlockType Type)
    {
        switch (Type)
        {
        case EBlockType::Air:
        case EBlockType::Glass: return 1.0f;
        case EBlockType::Leaves: return 0.75f;
        case EBlockType::Water: return 0.90f;
        default: return 0.0f;
        }
    }

    void BuildSkyLevels(const TArray<EBlockType>& Blocks, TArray<uint8>& OutSkyLevels)
    {
        OutSkyLevels.SetNumUninitialized(Blocks.Num());
        // The existing halo contains complete vertical columns, including edits
        // and neighboring trees. No actor queries or loaded neighbors are needed.
        for (int32 Y = -1; Y <= FVoxelData::ChunkSize; ++Y)
        {
            for (int32 X = -1; X <= FVoxelData::ChunkSize; ++X)
            {
                float Transmission = 1.0f;
                for (int32 Z = FVoxelData::WorldHeight; Z >= -1; --Z)
                {
                    const int32 Index = BufferIndex(FIntVector(X, Y, Z));
                    // Store incoming sky before this cell attenuates it. Visible
                    // faces only sample their adjacent air/glass/water cell.
                    OutSkyLevels[Index] = static_cast<uint8>(FMath::RoundToInt(Transmission * MaxSkyLevel));
                    Transmission *= SkyTransmission(Blocks[Index]);
                }
            }
        }
    }

    bool FaceIsVisible(EBlockType Current, EBlockType Neighbor)
    {
        if (Current == EBlockType::Air) return false;
        if (Current == EBlockType::Water)
            return Neighbor == EBlockType::Air || Neighbor == EBlockType::Glass;
        if (Current == EBlockType::Glass)
            return Neighbor == EBlockType::Air || Neighbor == EBlockType::Water;
        return Neighbor == EBlockType::Air || Neighbor == EBlockType::Water || Neighbor == EBlockType::Glass;
    }

    template <bool bCollisionOnly, bool bRecordFaceStats>
    void AppendQuad(FVoxelChunkMesh& Mesh, EBlockType Type, int32 Axis, int32 Sign,
                    int32 Slice, int32 U, int32 V, int32 Width, int32 Height, uint8 SkyLevel)
    {
        const int32 UAxis = (Axis + 1) % 3;
        const int32 VAxis = (Axis + 2) % 3;
        FVector Origin = FVector::ZeroVector;
        FVector DU = FVector::ZeroVector;
        FVector DV = FVector::ZeroVector;
        Origin[Axis] = Slice + (Sign > 0 ? 1 : 0);
        Origin[UAxis] = U;
        Origin[VAxis] = V;
        DU[UAxis] = Width;
        DV[VAxis] = Height;
        const FVector Points[4] = { Origin * FVoxelData::BlockSize, (Origin + DU) * FVoxelData::BlockSize,
            (Origin + DU + DV) * FVoxelData::BlockSize, (Origin + DV) * FVoxelData::BlockSize };
        // Unreal's front-facing triangle order is clockwise; cross products point inward.
        const int32 PositiveIndices[6] = { 0, 2, 1, 0, 3, 2 };
        const int32 NegativeIndices[6] = { 0, 1, 2, 0, 2, 3 };
        const int32* Indices = Sign > 0 ? PositiveIndices : NegativeIndices;
        if constexpr (!bCollisionOnly)
        {
            FVector Normal = FVector::ZeroVector;
            FVector Tangent = FVector::ZeroVector;
            Normal[Axis] = Sign;
            Tangent[UAxis] = 1.0;
            const FVector2D UVs[4] = { FVector2D(0, 0), FVector2D(Width, 0),
                FVector2D(Width, Height), FVector2D(0, Height) };
            FVoxelMeshSection& Section = Mesh.Sections[static_cast<uint8>(Type)];
            const int32 FirstVertex = Section.Vertices.Num();
            // Alpha distinguishes these encoded vertices from white/default
            // vertex colors on other meshes that reuse the voxel materials.
            const FLinearColor SkyColor(static_cast<float>(SkyLevel) / MaxSkyLevel, 0.0f, 0.0f, 0.0f);
            for (int32 Corner = 0; Corner < 4; ++Corner)
            {
                Section.Vertices.Add(Points[Corner]);
                Section.Normals.Add(Normal);
                Section.UVs.Add(UVs[Corner]);
                Section.Colors.Add(SkyColor);
                Section.Tangents.Add({ Tangent, Sign < 0 });
            }
            for (int32 Index = 0; Index < 6; ++Index) Section.Triangles.Add(FirstVertex + Indices[Index]);
        }
        else
        {
            // Only the solid-only greedy pass reaches this branch. Visual
            // material/sky splits must never change collision tessellation.
            const int32 FirstCollisionVertex = Mesh.CollisionVertices.Num();
            for (const FVector& Point : Points) Mesh.CollisionVertices.Add(Point);
            for (int32 Index = 0; Index < 6; ++Index) Mesh.CollisionTriangles.Add(FirstCollisionVertex + Indices[Index]);
        }
        if constexpr (bRecordFaceStats)
        {
            Mesh.VisibleFaces += static_cast<int64>(Width) * Height;
            ++Mesh.QuadCount;
        }
    }

    template <typename MaskKey, bool bCollisionOnly, bool bRecordFaceStats = true>
    void BuildGreedyQuads(const TArray<EBlockType>& Blocks, const TArray<uint8>* SkyLevels, FVoxelChunkMesh& Mesh)
    {
        const int32 Dimensions[3] = { FVoxelData::ChunkSize, FVoxelData::ChunkSize, FVoxelData::WorldHeight };
        TArray<MaskKey> Mask;
        for (int32 Axis = 0; Axis < 3; ++Axis)
        {
            const int32 UAxis = (Axis + 1) % 3;
            const int32 VAxis = (Axis + 2) % 3;
            const int32 USize = Dimensions[UAxis];
            const int32 VSize = Dimensions[VAxis];
            Mask.SetNumUninitialized(USize * VSize);
            for (int32 Sign = -1; Sign <= 1; Sign += 2)
            {
                for (int32 Slice = 0; Slice < Dimensions[Axis]; ++Slice)
                {
                    for (int32 V = 0; V < VSize; ++V)
                    {
                        for (int32 U = 0; U < USize; ++U)
                        {
                            FIntVector Position = FIntVector::ZeroValue;
                            Position[Axis] = Slice;
                            Position[UAxis] = U;
                            Position[VAxis] = V;
                            FIntVector Neighbor = Position;
                            Neighbor[Axis] += Sign;
                            const EBlockType Current = FVoxelData::BufferBlock(Blocks, Position);
                            const EBlockType Adjacent = FVoxelData::BufferBlock(Blocks, Neighbor);
                            if constexpr (bCollisionOnly)
                            {
                                // Keep the original byte mask and solid-only hot
                                // path for immediate collision rebuilds.
                                Mask[U + V * USize] = FVoxelData::IsSolid(Current) && !FVoxelData::IsSolid(Adjacent)
                                    ? static_cast<uint8>(EBlockType::Stone) : 0;
                            }
                            else
                            {
                                // A face depends only on its adjacent column, so
                                // the existing halo1 edit invalidation is enough.
                                Mask[U + V * USize] = FaceIsVisible(Current, Adjacent)
                                    ? static_cast<uint16>(static_cast<uint8>(Current)
                                        | (static_cast<uint16>((*SkyLevels)[BufferIndex(Neighbor)]) << 8)) : 0;
                            }
                        }
                    }
                    for (int32 V = 0; V < VSize; ++V)
                    {
                        for (int32 U = 0; U < USize;)
                        {
                            const MaskKey Key = Mask[U + V * USize];
                            if (Key == 0) { ++U; continue; }
                            int32 Width = 1;
                            while (U + Width < USize && Mask[U + Width + V * USize] == Key) ++Width;
                            int32 Height = 1;
                            bool bCanExtend = true;
                            while (V + Height < VSize && bCanExtend)
                            {
                                for (int32 Offset = 0; Offset < Width; ++Offset)
                                    if (Mask[U + Offset + (V + Height) * USize] != Key) { bCanExtend = false; break; }
                                if (bCanExtend) ++Height;
                            }
                            uint8 FaceSkyLevel = 0;
                            if constexpr (!bCollisionOnly) FaceSkyLevel = static_cast<uint8>(Key >> 8);
                            AppendQuad<bCollisionOnly, bRecordFaceStats>(Mesh, static_cast<EBlockType>(Key & 0xFF),
                                Axis, Sign, Slice, U, V, Width, Height, FaceSkyLevel);
                            for (int32 DY = 0; DY < Height; ++DY)
                                for (int32 DX = 0; DX < Width; ++DX) Mask[U + DX + (V + DY) * USize] = 0;
                            U += Width;
                        }
                    }
                }
            }
        }
    }

    bool ReadInteger(const TSharedPtr<FJsonValue>& Value, int32 Minimum, int32 Maximum, int32& Out)
    {
        double Number = 0.0;
        if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Number)
            || !FMath::IsFinite(Number) || Number < Minimum || Number > Maximum) return false;
        const int32 Integer = static_cast<int32>(Number);
        if (Number != static_cast<double>(Integer)) return false;
        Out = Integer;
        return true;
    }

    bool ReadBoundedNumber(const TSharedPtr<FJsonValue>& Value, double Minimum, double Maximum, float& Out)
    {
        double Number = 0.0;
        if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Number)
            || !FMath::IsFinite(Number) || Number < Minimum || Number > Maximum) return false;
        Out = static_cast<float>(Number);
        return true;
    }

    bool ReadCell(const TSharedPtr<FJsonValue>& Value, FIntVector& Out, int32 MinimumZ = 1)
    {
        const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
        return Value.IsValid() && Value->Type == EJson::Array && Value->TryGetArray(Fields)
            && Fields && Fields->Num() == 3
            && ReadInteger((*Fields)[0], -FVoxelData::MaxCoordinate, FVoxelData::MaxCoordinate, Out.X)
            && ReadInteger((*Fields)[1], -FVoxelData::MaxCoordinate, FVoxelData::MaxCoordinate, Out.Y)
            && ReadInteger((*Fields)[2], MinimumZ, FVoxelData::WorldHeight - 1, Out.Z);
    }

    FIntVector RestPointSecondCell(const FBlockRealmSavedRestPoint& RestPoint)
    {
        switch (RestPoint.Heading)
        {
        case 0: return RestPoint.Cell + FIntVector(1, 0, 0);
        case 1: return RestPoint.Cell + FIntVector(0, 1, 0);
        case 2: return RestPoint.Cell + FIntVector(-1, 0, 0);
        case 3: return RestPoint.Cell + FIntVector(0, -1, 0);
        default: return RestPoint.Cell; // Validation rejects unknown headings.
        }
    }

    bool ReadCookProgress(const TSharedPtr<FJsonValue>& Value, FBlockRealmSavedFurnace& Furnace)
    {
        double Number = 0.0;
        if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Number)
            || !FMath::IsFinite(Number) || Number < 0.0 || Number >= FVoxelData::FurnaceCookSeconds
            || (Number > 0.0 && (Furnace.InputSand == 0 || Furnace.FuelWood == 0
                || Furnace.OutputGlass == FVoxelData::MaxSavedFurnaceStack))) return false;
        // A valid double just below 8 may round to 8 as a float. Keep it below
        // completion; gameplay must commit the batch before saving progress 8.
        Furnace.CookProgress = FMath::Min(static_cast<float>(Number),
            std::nextafter(FVoxelData::FurnaceCookSeconds, 0.f));
        return true;
    }

    bool ReadNumberArray(const FJsonObject& Object, const TCHAR* Field, double Out[3])
    {
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (!Object.TryGetArrayField(Field, Array) || !Array || Array->Num() != 3) return false;
        for (int32 Index = 0; Index < 3; ++Index)
        {
            const TSharedPtr<FJsonValue>& Value = (*Array)[Index];
            if (!Value.IsValid() || Value->Type != EJson::Number || !Value->TryGetNumber(Out[Index])
                || !FMath::IsFinite(Out[Index])) return false;
        }
        return true;
    }

    TArray<TSharedPtr<FJsonValue>> NumberArray(double X, double Y, double Z)
    {
        TArray<TSharedPtr<FJsonValue>> Result;
        Result.Add(MakeShared<FJsonValueNumber>(X));
        Result.Add(MakeShared<FJsonValueNumber>(Y));
        Result.Add(MakeShared<FJsonValueNumber>(Z));
        return Result;
    }
}

int32 FVoxelData::FloorDivide(int32 Value, int32 Divisor)
{
    check(Divisor > 0);
    const int32 Quotient = Value / Divisor;
    return Quotient - (Value % Divisor < 0 ? 1 : 0);
}

FIntPoint FVoxelData::ChunkFromBlock(const FIntVector& Position)
{
    return FIntPoint(FloorDivide(Position.X, ChunkSize), FloorDivide(Position.Y, ChunkSize));
}

bool FVoxelData::IsValidPosition(const FIntVector& Position)
{
    return Position.X >= -MaxCoordinate && Position.X <= MaxCoordinate
        && Position.Y >= -MaxCoordinate && Position.Y <= MaxCoordinate
        && Position.Z >= 0 && Position.Z < WorldHeight;
}

bool FVoxelData::IsLegalType(EBlockType Type)
{
    return static_cast<uint8>(Type) <= static_cast<uint8>(EBlockType::Glow);
}

bool FVoxelData::IsSolid(EBlockType Type)
{
    return IsLegalType(Type) && Type != EBlockType::Air && Type != EBlockType::Water;
}

int32 FVoxelData::TerrainHeight(int32 X, int32 Y, int32 Seed)
{
    const double Base = 19.0 + 13.0 * ValueNoise(X, Y, 64, Seed, 0x68E31DA4u)
        + 6.0 * ValueNoise(X, Y, 24, Seed, 0x1B56C4E9u)
        + 2.0 * ValueNoise(X, Y, 8, Seed, 0xA1E38B93u);
    const double Distance = FMath::Sqrt(static_cast<double>(X) * X + static_cast<double>(Y) * Y);
    const double ClearingBlend = FMath::Clamp((Distance - 5.0) / 9.0, 0.0, 1.0);
    return FMath::Clamp(FMath::RoundToInt32(FMath::Lerp(21.0, Base, ClearingBlend)), 5, 42);
}

EBlockType FVoxelData::GeneratedBlock(const FIntVector& Position, int32 Seed)
{
    if (Position.X < -MaxCoordinate || Position.X > MaxCoordinate
        || Position.Y < -MaxCoordinate || Position.Y > MaxCoordinate) return EBlockType::Air;
    if (Position.Z < 0) return EBlockType::Stone;
    if (Position.Z >= WorldHeight) return EBlockType::Air;
    const EBlockType Ground = BaseTerrainBlock(Position, Seed);
    if (Ground != EBlockType::Air || Position.Z > 50) return Ground;
    bool bLeaves = false;
    for (int32 Y = Position.Y - 2; Y <= Position.Y + 2; ++Y)
    {
        for (int32 X = Position.X - 2; X <= Position.X + 2; ++X)
        {
            int32 TreeGround = 0;
            int32 TreeHeight = 0;
            if (!HasTreeRoot(X, Y, Seed, TreeGround, TreeHeight)) continue;
            const int32 DX = Position.X - X;
            const int32 DY = Position.Y - Y;
            if (DX == 0 && DY == 0 && Position.Z > TreeGround && Position.Z <= TreeGround + TreeHeight)
                return EBlockType::Wood;
            bLeaves |= HasCanopy(DX, DY, Position.Z - TreeGround - TreeHeight);
        }
    }
    return bLeaves ? EBlockType::Leaves : EBlockType::Air;
}

bool FVoxelData::IsCaveCell(const FIntVector& Position, int32 Seed)
{
    if (Position.X < -MaxCoordinate || Position.X > MaxCoordinate
        || Position.Y < -MaxCoordinate || Position.Y > MaxCoordinate
        || Position.Z < 0 || Position.Z >= WorldHeight) return false;
    const int32 Height = TerrainHeight(Position.X, Position.Y, Seed);
    return GroundBlock(Height, Position.Z) == EBlockType::Stone && IsCaveVolume(Position, Height, Seed);
}

bool FVoxelData::IsGlowOreCell(const FIntVector& Position, int32 Seed)
{
    if (Position.X < -MaxCoordinate || Position.X > MaxCoordinate
        || Position.Y < -MaxCoordinate || Position.Y > MaxCoordinate
        || Position.Z < 0 || Position.Z >= WorldHeight) return false;
    const int32 Height = TerrainHeight(Position.X, Position.Y, Seed);
    return GroundBlock(Height, Position.Z) == EBlockType::Stone
        && !IsCaveVolume(Position, Height, Seed)
        && IsGlowOreVolume(Position, Height, Seed);
}

void FVoxelData::BuildChunkBlocks(const FVoxelBuildInput& Input, TArray<EBlockType>& OutBlocks)
{
    OutBlocks.SetNumUninitialized(BufferWidth * BufferWidth * BufferHeight);
    const FIntVector Origin(Input.Chunk.X * ChunkSize, Input.Chunk.Y * ChunkSize, 0);
    for (int32 Y = -1; Y <= ChunkSize; ++Y)
    {
        for (int32 X = -1; X <= ChunkSize; ++X)
        {
            const int32 WorldX = Origin.X + X;
            const int32 WorldY = Origin.Y + Y;
            const bool bInBounds = WorldX >= -MaxCoordinate && WorldX <= MaxCoordinate
                && WorldY >= -MaxCoordinate && WorldY <= MaxCoordinate;
            const int32 Height = bInBounds ? TerrainHeight(WorldX, WorldY, Input.Seed) : 0;
            for (int32 Z = -1; Z <= WorldHeight; ++Z)
                OutBlocks[BufferIndex(FIntVector(X, Y, Z))] = bInBounds
                    ? BaseTerrainBlockAtHeight(FIntVector(WorldX, WorldY, Z), Height, Input.Seed) : EBlockType::Air;
        }
    }

    auto Stamp = [&OutBlocks, &Origin](const FIntVector& WorldPosition, EBlockType Type)
    {
        const FIntVector Local = WorldPosition - Origin;
        if (!WithinBuffer(Local) || !FVoxelData::IsValidPosition(WorldPosition)) return;
        EBlockType& Existing = OutBlocks[BufferIndex(Local)];
        if (Existing == EBlockType::Air || (Type == EBlockType::Wood && Existing == EBlockType::Leaves)) Existing = Type;
    };

    for (int32 Y = Origin.Y - 3; Y <= Origin.Y + ChunkSize + 2; ++Y)
    {
        for (int32 X = Origin.X - 3; X <= Origin.X + ChunkSize + 2; ++X)
        {
            int32 Ground = 0;
            int32 Height = 0;
            if (!HasTreeRoot(X, Y, Input.Seed, Ground, Height)) continue;
            for (int32 Z = Ground + 1; Z <= Ground + Height; ++Z) Stamp(FIntVector(X, Y, Z), EBlockType::Wood);
            for (int32 DZ = -2; DZ <= 1; ++DZ)
                for (int32 DY = -2; DY <= 2; ++DY)
                    for (int32 DX = -2; DX <= 2; ++DX)
                        if (HasCanopy(DX, DY, DZ)) Stamp(FIntVector(X + DX, Y + DY, Ground + Height + DZ), EBlockType::Leaves);
        }
    }

    for (const TPair<FIntVector, EBlockType>& Edit : Input.Overrides)
    {
        const FIntVector Local = Edit.Key - Origin;
        if (WithinBuffer(Local) && IsValidPosition(Edit.Key) && IsLegalType(Edit.Value)) OutBlocks[BufferIndex(Local)] = Edit.Value;
    }
}

EBlockType FVoxelData::BufferBlock(const TArray<EBlockType>& Blocks, const FIntVector& LocalPosition)
{
    if (!WithinBuffer(LocalPosition) || Blocks.Num() != BufferWidth * BufferWidth * BufferHeight) return EBlockType::Air;
    return Blocks[BufferIndex(LocalPosition)];
}

FVoxelChunkMesh FVoxelData::BuildMesh(const FVoxelBuildInput& Input)
{
    return BuildMeshInternal(Input, false);
}

FVoxelChunkMesh FVoxelData::BuildCollisionMesh(const FVoxelBuildInput& Input)
{
    return BuildMeshInternal(Input, true);
}

FVoxelChunkMesh FVoxelData::BuildMeshInternal(const FVoxelBuildInput& Input, bool bCollisionOnly)
{
    FVoxelChunkMesh Mesh;
    Mesh.Chunk = Input.Chunk;
    Mesh.Revision = Input.Revision;
    Mesh.Epoch = Input.Epoch;
    if (!bCollisionOnly) Mesh.Sections.SetNum(static_cast<uint8>(EBlockType::Glow) + 1);
    TArray<EBlockType> Blocks;
    BuildChunkBlocks(Input, Blocks);
    if (!bCollisionOnly)
    {
        // Point lights are a presentation aid for the dynamic PMC world.  Only
        // Glow blocks that touch an actual air cavity qualify, and at most two
        // evenly spaced anchors are returned per chunk to bound GPU/light-grid
        // cost while keeping a nearby tunnel readable.
        TArray<FVector> GlowCandidates;
        GlowCandidates.Reserve(12);
        const FIntVector FaceOffsets[] = { FIntVector(1, 0, 0), FIntVector(-1, 0, 0),
            FIntVector(0, 1, 0), FIntVector(0, -1, 0), FIntVector(0, 0, 1), FIntVector(0, 0, -1) };
        for (int32 Z = 0; Z < WorldHeight; ++Z)
        {
            for (int32 Y = 0; Y < ChunkSize; ++Y)
            {
                for (int32 X = 0; X < ChunkSize; ++X)
                {
                    const FIntVector Local(X, Y, Z);
                    if (BufferBlock(Blocks, Local) != EBlockType::Glow) continue;
                    bool bExposed = false;
                    FVector LightPosition((X + 0.5) * BlockSize, (Y + 0.5) * BlockSize,
                        (Z + 0.5) * BlockSize);
                    for (const FIntVector& FaceOffset : FaceOffsets)
                    {
                        if (BufferBlock(Blocks, Local + FaceOffset) == EBlockType::Air)
                        {
                            bExposed = true;
                            // Keep the light (including its 28 cm source radius)
                            // outside the emitting cube so its own shadow cannot
                            // swallow the cave light. The adjacent halo cell is air.
                            LightPosition += FVector(FaceOffset) * (BlockSize * 0.85);
                            break;
                        }
                    }
                    if (!bExposed || GlowCandidates.Num() >= 12) continue;
                    GlowCandidates.Add(LightPosition);
                }
            }
        }
        const int32 LightCount = FMath::Min(2, GlowCandidates.Num());
        for (int32 Index = 0; Index < LightCount; ++Index)
        {
            const int32 CandidateIndex = (Index * GlowCandidates.Num()) / LightCount;
            Mesh.GlowLightPositions.Add(GlowCandidates[CandidateIndex]);
        }
    }
    if (bCollisionOnly)
    {
        BuildGreedyQuads<uint8, true>(Blocks, nullptr, Mesh);
    }
    else
    {
        TArray<uint8> SkyLevels;
        BuildSkyLevels(Blocks, SkyLevels);
        BuildGreedyQuads<uint16, false>(Blocks, &SkyLevels, Mesh);
        // Reuse the same block snapshot, but build authoritative collision with
        // exactly the same solid-only pass as BuildCollisionMesh. No visual
        // collision buffers are generated or discarded, and these extra quads
        // do not inflate the visual counters.
        BuildGreedyQuads<uint8, true, false>(Blocks, nullptr, Mesh);
    }
    return Mesh;
}

bool FVoxelData::ValidatePlayerState(const FWorldSaveState& State)
{
    const double Limit = static_cast<double>(MaxCoordinate) * BlockSize;
    if (!FMath::IsFinite(State.Position.X) || !FMath::IsFinite(State.Position.Y) || !FMath::IsFinite(State.Position.Z)
        || FMath::Abs(State.Position.X) > Limit || FMath::Abs(State.Position.Y) > Limit || FMath::Abs(State.Position.Z) > Limit
        || !FMath::IsFinite(State.Rotation.Pitch) || !FMath::IsFinite(State.Rotation.Yaw) || !FMath::IsFinite(State.Rotation.Roll)
        || FMath::Abs(State.Rotation.Pitch) > 1000000.0 || FMath::Abs(State.Rotation.Yaw) > 1000000.0
        || FMath::Abs(State.Rotation.Roll) > 1000000.0 || State.SelectedSlot < 0 || State.SelectedSlot > 8
        || State.Inventory.Num() != 9) return false;
    for (int32 Count : State.Inventory) if (Count < 0 || Count > 1000000) return false;

    // World objects are independent extras. Validate them even in a legacy-style
    // player record so missing survival never hides corruption or conflicts.
    if (State.PlacedTorches.Num() > MaxSavedTorches || State.Crops.Num() > MaxSavedCrops
        || State.RestPoints.Num() > MaxSavedRestPoints || State.Furnaces.Num() > MaxSavedFurnaces) return false;
    TSet<FIntVector> OccupiedCells;
    auto ClaimCell = [&OccupiedCells](const FIntVector& Cell)
    {
        if (!FVoxelData::IsValidPosition(Cell) || Cell.Z == 0 || OccupiedCells.Contains(Cell)) return false;
        OccupiedCells.Add(Cell);
        return true;
    };
    for (const FIntVector& Cell : State.PlacedTorches) if (!ClaimCell(Cell)) return false;
    if (!State.PlacedTorchSupports.IsEmpty())
    {
        if (State.PlacedTorchSupports.Num() != State.PlacedTorches.Num()) return false;
        for (int32 Index = 0; Index < State.PlacedTorches.Num(); ++Index)
        {
            const FIntVector& Support = State.PlacedTorchSupports[Index];
            if (!IsValidPosition(Support)) return false;
            const FIntVector Offset = State.PlacedTorches[Index] - Support;
            if (FMath::Abs(Offset.X) + FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z) != 1) return false;
        }
    }
    for (const FBlockRealmSavedCrop& Crop : State.Crops)
        if (!FMath::IsFinite(Crop.GrowthSeconds) || Crop.GrowthSeconds < 0.f
            || Crop.GrowthSeconds > MaxCropGrowthSeconds || !ClaimCell(Crop.Cell)) return false;
    TSet<FIntVector> BedAnchors;
    for (const FBlockRealmSavedRestPoint& RestPoint : State.RestPoints)
    {
        if (RestPoint.Heading < 0 || RestPoint.Heading > 3 || !ClaimCell(RestPoint.Cell)
            || !ClaimCell(RestPointSecondCell(RestPoint))) return false;
        BedAnchors.Add(RestPoint.Cell);
    }
    for (const FBlockRealmSavedFurnace& Furnace : State.Furnaces)
        if (Furnace.InputSand < 0 || Furnace.InputSand > MaxSavedFurnaceStack
            || Furnace.FuelWood < 0 || Furnace.FuelWood > MaxSavedFurnaceStack
            || Furnace.OutputGlass < 0 || Furnace.OutputGlass > MaxSavedFurnaceStack
            || !FMath::IsFinite(Furnace.CookProgress) || Furnace.CookProgress < 0.f
            || Furnace.CookProgress >= FurnaceCookSeconds
            || (Furnace.CookProgress > 0.f && (Furnace.InputSand == 0 || Furnace.FuelWood == 0
                || Furnace.OutputGlass == MaxSavedFurnaceStack)) || !ClaimCell(Furnace.Cell)) return false;
    if (State.bHasRespawnPoint && !BedAnchors.Contains(State.RespawnPoint)) return false;

    if (!State.bHasSurvivalState) return true;
    if (!FMath::IsFinite(State.Health) || State.Health < 0.f || State.Health > 100.f
        || !FMath::IsFinite(State.Hunger) || State.Hunger < 0.f || State.Hunger > 100.f
        || State.CraftedPlanks < 0 || State.CraftedPlanks > 1000000
        || State.FoodCount < 0 || State.FoodCount > 1000000
        || State.SelectedToolSlot < -1 || State.SelectedToolSlot >= SavedToolSlotCount
        || State.ToolSlots.Num() != SavedToolSlotCount
        || !FMath::IsFinite(State.TimeOfDay) || State.TimeOfDay < 0.f || State.TimeOfDay > 1.f) return false;
    for (const FBlockRealmSavedTool& Tool : State.ToolSlots)
    {
        if (Tool.Type < 0 || Tool.Type > 3 || Tool.Quantity < 0 || Tool.Quantity > MaxSavedToolQuantity
            || Tool.Durability < 0 || Tool.Durability > MaxSavedToolDurability
            || Tool.MaxDurability < 0 || Tool.MaxDurability > MaxSavedToolDurability
            || Tool.Durability > Tool.MaxDurability) return false;
        if (Tool.Type == 0)
        {
            if (Tool.Quantity != 0 || Tool.Durability != 0 || Tool.MaxDurability != 0) return false;
        }
        else if (Tool.Quantity == 0 || (Tool.Type != 3 && Tool.Durability == 0)) return false;
    }
    return true;
}

bool FVoxelData::SerializeRecord(const FVoxelSaveRecord& Record, FString& OutJson)
{
    if (!ValidatePlayerState(Record.Player) || Record.Overrides.Num() > MaxSavedEdits) return false;
    TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("format"), TEXT("BlockRealmWorld"));
    Root->SetNumberField(TEXT("version"), 1);
    Root->SetNumberField(TEXT("seed"), Record.Seed);
    Root->SetNumberField(TEXT("chunkSize"), ChunkSize);
    Root->SetNumberField(TEXT("worldHeight"), WorldHeight);
    Root->SetNumberField(TEXT("blockSize"), BlockSize);
    TSharedRef<FJsonObject> Player = MakeShared<FJsonObject>();
    Player->SetArrayField(TEXT("position"), NumberArray(Record.Player.Position.X, Record.Player.Position.Y, Record.Player.Position.Z));
    Player->SetArrayField(TEXT("rotation"), NumberArray(Record.Player.Rotation.Pitch, Record.Player.Rotation.Yaw, Record.Player.Rotation.Roll));
    Player->SetNumberField(TEXT("selectedSlot"), Record.Player.SelectedSlot);
    TArray<TSharedPtr<FJsonValue>> Inventory;
    for (int32 Count : Record.Player.Inventory) Inventory.Add(MakeShared<FJsonValueNumber>(Count));
    Player->SetArrayField(TEXT("inventory"), Inventory);
    Root->SetObjectField(TEXT("player"), Player);
    if (Record.Player.bHasSurvivalState)
    {
        TSharedRef<FJsonObject> Survival = MakeShared<FJsonObject>();
        Survival->SetNumberField(TEXT("health"), Record.Player.Health);
        Survival->SetNumberField(TEXT("hunger"), Record.Player.Hunger);
        Survival->SetNumberField(TEXT("craftedPlanks"), Record.Player.CraftedPlanks);
        Survival->SetNumberField(TEXT("foodCount"), Record.Player.FoodCount);
        Survival->SetNumberField(TEXT("selectedToolSlot"), Record.Player.SelectedToolSlot);
        Survival->SetBoolField(TEXT("bareHandsEquipped"), Record.Player.bBareHandsEquipped);
        Survival->SetNumberField(TEXT("timeOfDay"), Record.Player.TimeOfDay);
        TArray<TSharedPtr<FJsonValue>> Tools;
        Tools.Reserve(SavedToolSlotCount);
        for (const FBlockRealmSavedTool& Tool : Record.Player.ToolSlots)
        {
            TSharedRef<FJsonObject> SavedTool = MakeShared<FJsonObject>();
            SavedTool->SetNumberField(TEXT("type"), Tool.Type);
            SavedTool->SetNumberField(TEXT("durability"), Tool.Durability);
            SavedTool->SetNumberField(TEXT("maxDurability"), Tool.MaxDurability);
            SavedTool->SetNumberField(TEXT("quantity"), Tool.Quantity);
            Tools.Add(MakeShared<FJsonValueObject>(SavedTool));
        }
        Survival->SetArrayField(TEXT("toolSlots"), Tools);
        Root->SetObjectField(TEXT("survival"), Survival);
    }
    if (Record.Player.bHasSurvivalState || !Record.Player.PlacedTorches.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> Torches;
        Torches.Reserve(Record.Player.PlacedTorches.Num());
        for (const FIntVector& Position : Record.Player.PlacedTorches)
            Torches.Add(MakeShared<FJsonValueArray>(NumberArray(Position.X, Position.Y, Position.Z)));
        Root->SetArrayField(TEXT("placedTorches"), Torches);
        if (!Record.Player.PlacedTorchSupports.IsEmpty())
        {
            TArray<TSharedPtr<FJsonValue>> Supports;
            for (const FIntVector& Support : Record.Player.PlacedTorchSupports)
                Supports.Add(MakeShared<FJsonValueArray>(NumberArray(Support.X, Support.Y, Support.Z)));
            Root->SetArrayField(TEXT("placedTorchSupports"), Supports);
        }
    }
    if (!Record.Player.Crops.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> Crops;
        Crops.Reserve(Record.Player.Crops.Num());
        for (const FBlockRealmSavedCrop& Crop : Record.Player.Crops)
        {
            TSharedRef<FJsonObject> SavedCrop = MakeShared<FJsonObject>();
            SavedCrop->SetArrayField(TEXT("cell"), NumberArray(Crop.Cell.X, Crop.Cell.Y, Crop.Cell.Z));
            SavedCrop->SetNumberField(TEXT("growthSeconds"), Crop.GrowthSeconds);
            Crops.Add(MakeShared<FJsonValueObject>(SavedCrop));
        }
        Root->SetArrayField(TEXT("crops"), Crops);
    }
    if (!Record.Player.RestPoints.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> RestPoints;
        RestPoints.Reserve(Record.Player.RestPoints.Num());
        for (const FBlockRealmSavedRestPoint& RestPoint : Record.Player.RestPoints)
        {
            TSharedRef<FJsonObject> SavedRestPoint = MakeShared<FJsonObject>();
            SavedRestPoint->SetArrayField(TEXT("cell"), NumberArray(RestPoint.Cell.X, RestPoint.Cell.Y, RestPoint.Cell.Z));
            SavedRestPoint->SetNumberField(TEXT("heading"), RestPoint.Heading);
            RestPoints.Add(MakeShared<FJsonValueObject>(SavedRestPoint));
        }
        Root->SetArrayField(TEXT("restPoints"), RestPoints);
    }
    if (!Record.Player.Furnaces.IsEmpty())
    {
        TArray<TSharedPtr<FJsonValue>> Furnaces;
        Furnaces.Reserve(Record.Player.Furnaces.Num());
        for (const FBlockRealmSavedFurnace& Furnace : Record.Player.Furnaces)
        {
            TSharedRef<FJsonObject> SavedFurnace = MakeShared<FJsonObject>();
            SavedFurnace->SetArrayField(TEXT("cell"), NumberArray(Furnace.Cell.X, Furnace.Cell.Y, Furnace.Cell.Z));
            SavedFurnace->SetNumberField(TEXT("inputSand"), Furnace.InputSand);
            SavedFurnace->SetNumberField(TEXT("fuelWood"), Furnace.FuelWood);
            SavedFurnace->SetNumberField(TEXT("outputGlass"), Furnace.OutputGlass);
            SavedFurnace->SetNumberField(TEXT("cookProgress"), Furnace.CookProgress);
            Furnaces.Add(MakeShared<FJsonValueObject>(SavedFurnace));
        }
        Root->SetArrayField(TEXT("furnaces"), Furnaces);
    }
    if (Record.Player.bHasRespawnPoint)
        Root->SetArrayField(TEXT("respawnPoint"), NumberArray(Record.Player.RespawnPoint.X,
            Record.Player.RespawnPoint.Y, Record.Player.RespawnPoint.Z));
    TArray<FIntVector> Keys;
    Record.Overrides.GetKeys(Keys);
    Keys.Sort([](const FIntVector& A, const FIntVector& B)
    {
        if (A.X != B.X) return A.X < B.X;
        if (A.Y != B.Y) return A.Y < B.Y;
        return A.Z < B.Z;
    });
    TArray<TSharedPtr<FJsonValue>> Edits;
    Edits.Reserve(Keys.Num());
    for (const FIntVector& Position : Keys)
    {
        const EBlockType Type = Record.Overrides.FindChecked(Position);
        if (!IsValidPosition(Position) || Position.Z == 0 || !IsLegalType(Type) || Type == EBlockType::Water) return false;
        TArray<TSharedPtr<FJsonValue>> Edit = NumberArray(Position.X, Position.Y, Position.Z);
        Edit.Add(MakeShared<FJsonValueNumber>(static_cast<uint8>(Type)));
        Edits.Add(MakeShared<FJsonValueArray>(MoveTemp(Edit)));
    }
    Root->SetArrayField(TEXT("edits"), Edits);
    FString Json;
    const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
    if (!FJsonSerializer::Serialize(Root, Writer) || Json.Len() > MaxSaveBytes) return false;
    OutJson = MoveTemp(Json);
    return true;
}

bool FVoxelData::ParseRecord(const FString& Json, FVoxelSaveRecord& OutRecord)
{
    if (Json.IsEmpty() || Json.Len() > MaxSaveBytes) return false;
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid()) return false;
    FString Format;
    int32 Version = 0;
    int32 SavedChunkSize = 0;
    int32 SavedHeight = 0;
    int32 SavedBlockSize = 0;
    FVoxelSaveRecord Temporary;
    if (!Root->TryGetStringField(TEXT("format"), Format) || Format != TEXT("BlockRealmWorld")
        || !ReadInteger(Root->TryGetField(TEXT("version")), 1, 1, Version)
        || !ReadInteger(Root->TryGetField(TEXT("seed")), MIN_int32, MAX_int32, Temporary.Seed)
        || !ReadInteger(Root->TryGetField(TEXT("chunkSize")), ChunkSize, ChunkSize, SavedChunkSize)
        || !ReadInteger(Root->TryGetField(TEXT("worldHeight")), WorldHeight, WorldHeight, SavedHeight)
        || !ReadInteger(Root->TryGetField(TEXT("blockSize")), 100, 100, SavedBlockSize)) return false;
    const TSharedPtr<FJsonObject>* Player = nullptr;
    if (!Root->TryGetObjectField(TEXT("player"), Player) || !Player || !Player->IsValid()) return false;
    double SavedPosition[3];
    double Rotation[3];
    if (!ReadNumberArray(**Player, TEXT("position"), SavedPosition) || !ReadNumberArray(**Player, TEXT("rotation"), Rotation)
        || !ReadInteger((*Player)->TryGetField(TEXT("selectedSlot")), 0, 8, Temporary.Player.SelectedSlot)) return false;
    Temporary.Player.Position = FVector(SavedPosition[0], SavedPosition[1], SavedPosition[2]);
    Temporary.Player.Rotation = FRotator(Rotation[0], Rotation[1], Rotation[2]);
    const TArray<TSharedPtr<FJsonValue>>* Inventory = nullptr;
    if (!(*Player)->TryGetArrayField(TEXT("inventory"), Inventory) || !Inventory || Inventory->Num() != 9) return false;
    for (const TSharedPtr<FJsonValue>& Value : *Inventory)
    {
        int32 Count = 0;
        if (!ReadInteger(Value, 0, 1000000, Count)) return false;
        Temporary.Player.Inventory.Add(Count);
    }
    // Absence means a legacy save. Present-but-null or incomplete extras are corruption.
    if (Root->HasField(TEXT("survival")))
    {
        const TSharedPtr<FJsonObject>* Survival = nullptr;
        if (!Root->TryGetObjectField(TEXT("survival"), Survival) || !Survival || !Survival->IsValid()
            || !ReadBoundedNumber((*Survival)->TryGetField(TEXT("health")), 0.0, 100.0, Temporary.Player.Health)
            || !ReadBoundedNumber((*Survival)->TryGetField(TEXT("hunger")), 0.0, 100.0, Temporary.Player.Hunger)
            || !ReadInteger((*Survival)->TryGetField(TEXT("craftedPlanks")), 0, 1000000, Temporary.Player.CraftedPlanks)
            || !ReadInteger((*Survival)->TryGetField(TEXT("foodCount")), 0, 1000000, Temporary.Player.FoodCount)
            || !ReadInteger((*Survival)->TryGetField(TEXT("selectedToolSlot")), -1, SavedToolSlotCount - 1,
                Temporary.Player.SelectedToolSlot)
            || !ReadBoundedNumber((*Survival)->TryGetField(TEXT("timeOfDay")), 0.0, 1.0, Temporary.Player.TimeOfDay)) return false;
        // Old version-1 saves omit this flag; a present value must be a real boolean.
        if ((*Survival)->HasField(TEXT("bareHandsEquipped")))
        {
            const TSharedPtr<FJsonValue> BareHands = (*Survival)->TryGetField(TEXT("bareHandsEquipped"));
            if (!BareHands.IsValid() || BareHands->Type != EJson::Boolean) return false;
            Temporary.Player.bBareHandsEquipped = BareHands->AsBool();
        }
        const TArray<TSharedPtr<FJsonValue>>* Tools = nullptr;
        if (!(*Survival)->TryGetArrayField(TEXT("toolSlots"), Tools) || !Tools || Tools->Num() != SavedToolSlotCount) return false;
        Temporary.Player.ToolSlots.Reserve(SavedToolSlotCount);
        for (const TSharedPtr<FJsonValue>& Value : *Tools)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) return false;
            const TSharedPtr<FJsonObject> Tool = Value->AsObject();
            FBlockRealmSavedTool SavedTool;
            if (!Tool.IsValid()
                || !ReadInteger(Tool->TryGetField(TEXT("type")), 0, 3, SavedTool.Type)
                || !ReadInteger(Tool->TryGetField(TEXT("durability")), 0, MaxSavedToolDurability, SavedTool.Durability)
                || !ReadInteger(Tool->TryGetField(TEXT("maxDurability")), 0, MaxSavedToolDurability, SavedTool.MaxDurability)
                || !ReadInteger(Tool->TryGetField(TEXT("quantity")), 0, MaxSavedToolQuantity, SavedTool.Quantity)) return false;
            Temporary.Player.ToolSlots.Add(SavedTool);
        }
        Temporary.Player.bHasSurvivalState = true;
    }
    // Torch positions are independently optional so missing survival data never hides a malformed array.
    if (Root->HasField(TEXT("placedTorches")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Torches = nullptr;
        if (!Root->TryGetArrayField(TEXT("placedTorches"), Torches) || !Torches || Torches->Num() > MaxSavedTorches) return false;
        Temporary.Player.PlacedTorches.Reserve(Torches->Num());
        TSet<FIntVector> TorchPositions;
        for (const TSharedPtr<FJsonValue>& Value : *Torches)
        {
            const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
            if (!Value.IsValid() || !Value->TryGetArray(Fields) || !Fields || Fields->Num() != 3) return false;
            FIntVector Position;
            if (!ReadInteger((*Fields)[0], -MaxCoordinate, MaxCoordinate, Position.X)
                || !ReadInteger((*Fields)[1], -MaxCoordinate, MaxCoordinate, Position.Y)
                || !ReadInteger((*Fields)[2], 1, WorldHeight - 1, Position.Z)
                || TorchPositions.Contains(Position)) return false;
            TorchPositions.Add(Position);
            Temporary.Player.PlacedTorches.Add(Position);
        }
    }
    if (Root->HasField(TEXT("placedTorchSupports")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Supports = nullptr;
        if (!Root->TryGetArrayField(TEXT("placedTorchSupports"), Supports) || !Supports
            || Supports->Num() != Temporary.Player.PlacedTorches.Num()) return false;
        for (int32 Index = 0; Index < Supports->Num(); ++Index)
        {
            const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
            const TSharedPtr<FJsonValue>& Value = (*Supports)[Index];
            FIntVector Support;
            if (!Value.IsValid() || !Value->TryGetArray(Fields) || !Fields || Fields->Num() != 3
                || !ReadInteger((*Fields)[0], -MaxCoordinate, MaxCoordinate, Support.X)
                || !ReadInteger((*Fields)[1], -MaxCoordinate, MaxCoordinate, Support.Y)
                || !ReadInteger((*Fields)[2], 0, WorldHeight - 1, Support.Z)) return false;
            const FIntVector Offset = Temporary.Player.PlacedTorches[Index] - Support;
            if (FMath::Abs(Offset.X) + FMath::Abs(Offset.Y) + FMath::Abs(Offset.Z) != 1) return false;
            Temporary.Player.PlacedTorchSupports.Add(Support);
        }
    }
    if (Root->HasField(TEXT("crops")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Crops = nullptr;
        if (!Root->TryGetArrayField(TEXT("crops"), Crops) || !Crops || Crops->Num() > MaxSavedCrops) return false;
        Temporary.Player.Crops.Reserve(Crops->Num());
        for (const TSharedPtr<FJsonValue>& Value : *Crops)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) return false;
            const TSharedPtr<FJsonObject> Crop = Value->AsObject();
            FBlockRealmSavedCrop SavedCrop;
            if (!Crop.IsValid() || !ReadCell(Crop->TryGetField(TEXT("cell")), SavedCrop.Cell)
                || !ReadBoundedNumber(Crop->TryGetField(TEXT("growthSeconds")), 0.0,
                    MaxCropGrowthSeconds, SavedCrop.GrowthSeconds)) return false;
            Temporary.Player.Crops.Add(SavedCrop);
        }
    }
    if (Root->HasField(TEXT("restPoints")))
    {
        const TArray<TSharedPtr<FJsonValue>>* RestPoints = nullptr;
        if (!Root->TryGetArrayField(TEXT("restPoints"), RestPoints) || !RestPoints
            || RestPoints->Num() > MaxSavedRestPoints) return false;
        Temporary.Player.RestPoints.Reserve(RestPoints->Num());
        for (const TSharedPtr<FJsonValue>& Value : *RestPoints)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) return false;
            const TSharedPtr<FJsonObject> RestPoint = Value->AsObject();
            FBlockRealmSavedRestPoint SavedRestPoint;
            if (!RestPoint.IsValid() || !ReadCell(RestPoint->TryGetField(TEXT("cell")), SavedRestPoint.Cell)
                || !ReadInteger(RestPoint->TryGetField(TEXT("heading")), 0, 3, SavedRestPoint.Heading)) return false;
            Temporary.Player.RestPoints.Add(SavedRestPoint);
        }
    }
    if (Root->HasField(TEXT("furnaces")))
    {
        const TArray<TSharedPtr<FJsonValue>>* Furnaces = nullptr;
        if (!Root->TryGetArrayField(TEXT("furnaces"), Furnaces) || !Furnaces
            || Furnaces->Num() > MaxSavedFurnaces) return false;
        Temporary.Player.Furnaces.Reserve(Furnaces->Num());
        for (const TSharedPtr<FJsonValue>& Value : *Furnaces)
        {
            if (!Value.IsValid() || Value->Type != EJson::Object) return false;
            const TSharedPtr<FJsonObject> Furnace = Value->AsObject();
            FBlockRealmSavedFurnace SavedFurnace;
            if (!Furnace.IsValid() || !ReadCell(Furnace->TryGetField(TEXT("cell")), SavedFurnace.Cell)
                || !ReadInteger(Furnace->TryGetField(TEXT("inputSand")), 0, MaxSavedFurnaceStack, SavedFurnace.InputSand)
                || !ReadInteger(Furnace->TryGetField(TEXT("fuelWood")), 0, MaxSavedFurnaceStack, SavedFurnace.FuelWood)
                || !ReadInteger(Furnace->TryGetField(TEXT("outputGlass")), 0, MaxSavedFurnaceStack, SavedFurnace.OutputGlass)
                || !ReadCookProgress(Furnace->TryGetField(TEXT("cookProgress")), SavedFurnace)) return false;
            Temporary.Player.Furnaces.Add(SavedFurnace);
        }
    }
    if (Root->HasField(TEXT("respawnPoint")))
    {
        if (!ReadCell(Root->TryGetField(TEXT("respawnPoint")), Temporary.Player.RespawnPoint)) return false;
        Temporary.Player.bHasRespawnPoint = true;
    }
    if (!ValidatePlayerState(Temporary.Player)) return false;
    const TArray<TSharedPtr<FJsonValue>>* Edits = nullptr;
    if (!Root->TryGetArrayField(TEXT("edits"), Edits) || !Edits || Edits->Num() > MaxSavedEdits) return false;
    Temporary.Overrides.Reserve(Edits->Num());
    for (const TSharedPtr<FJsonValue>& Value : *Edits)
    {
        const TArray<TSharedPtr<FJsonValue>>* Fields = nullptr;
        if (!Value.IsValid() || !Value->TryGetArray(Fields) || !Fields || Fields->Num() != 4) return false;
        FIntVector EditPosition;
        int32 Type = 0;
        if (!ReadInteger((*Fields)[0], -MaxCoordinate, MaxCoordinate, EditPosition.X)
            || !ReadInteger((*Fields)[1], -MaxCoordinate, MaxCoordinate, EditPosition.Y)
            || !ReadInteger((*Fields)[2], 1, WorldHeight - 1, EditPosition.Z)
            || !ReadInteger((*Fields)[3], 0, static_cast<uint8>(EBlockType::Glow), Type)
            || Type == static_cast<uint8>(EBlockType::Water) || Temporary.Overrides.Contains(EditPosition)) return false;
        Temporary.Overrides.Add(EditPosition, static_cast<EBlockType>(Type));
    }
    OutRecord = MoveTemp(Temporary);
    return true;
}

bool FVoxelData::RunMeshSelfTests(TArray<FString>& Failures)
{
    const int32 InitialFailureCount = Failures.Num();
    auto Expect = [&Failures](bool bCondition, const TCHAR* Message)
    {
        if (!bCondition) Failures.Add(FString(TEXT("Voxel sky: ")) + Message);
    };
    auto CheckColors = [&Expect](const FVoxelChunkMesh& Mesh)
    {
        bool bValid = true;
        for (const FVoxelMeshSection& Section : Mesh.Sections)
        {
            bValid &= Section.Colors.Num() == Section.Vertices.Num() && Section.Vertices.Num() % 4 == 0;
            for (int32 Index = 0; Index < Section.Colors.Num(); ++Index)
            {
                const FLinearColor& Color = Section.Colors[Index];
                bValid &= FMath::IsFinite(Color.R) && Color.R >= 0.0f && Color.R <= 1.0f
                    && Color.G == 0.0f && Color.B == 0.0f && Color.A == 0.0f;
                bValid &= Color == Section.Colors[Index - Index % 4];
            }
        }
        Expect(bValid, TEXT("Every visual vertex must have a bounded R sky value, A=0 marker, reserved G/B=0 and uniform quad colors"));
    };
    auto FaceSky = [](const FVoxelChunkMesh& Mesh, EBlockType Type, const FIntVector& LocalCell,
        int32 Axis, int32 Sign, int32* OutQuad = nullptr)
    {
        if (OutQuad) *OutQuad = INDEX_NONE;
        if (!Mesh.Sections.IsValidIndex(static_cast<uint8>(Type))) return -1.0f;
        const FVoxelMeshSection& Section = Mesh.Sections[static_cast<uint8>(Type)];
        FVector Probe((LocalCell.X + 0.5) * BlockSize, (LocalCell.Y + 0.5) * BlockSize,
            (LocalCell.Z + 0.5) * BlockSize);
        Probe[Axis] = (LocalCell[Axis] + (Sign > 0 ? 1 : 0)) * BlockSize;
        const int32 UAxis = (Axis + 1) % 3;
        const int32 VAxis = (Axis + 2) % 3;
        for (int32 First = 0; First + 3 < Section.Vertices.Num(); First += 4)
        {
            if (!Section.Normals.IsValidIndex(First) || !Section.Colors.IsValidIndex(First)
                || Section.Normals[First][Axis] != Sign || Section.Vertices[First][Axis] != Probe[Axis]) continue;
            FBox Bounds(ForceInit);
            for (int32 Corner = 0; Corner < 4; ++Corner) Bounds += Section.Vertices[First + Corner];
            if (Probe[UAxis] >= Bounds.Min[UAxis] && Probe[UAxis] <= Bounds.Max[UAxis]
                && Probe[VAxis] >= Bounds.Min[VAxis] && Probe[VAxis] <= Bounds.Max[VAxis])
            {
                if (OutQuad) *OutQuad = First / 4;
                return Section.Colors[First].R;
            }
        }
        return -1.0f;
    };
    auto DecodeSkyLevel = [](float Sky)
    {
        return FMath::IsFinite(Sky) && Sky >= 0.0f && Sky <= 1.0f
            ? FMath::RoundToInt(Sky * MaxSkyLevel) : INDEX_NONE;
    };
    auto SameCollision = [](const FVoxelChunkMesh& A, const FVoxelChunkMesh& B)
    {
        return A.CollisionVertices == B.CollisionVertices && A.CollisionTriangles == B.CollisionTriangles;
    };

    // These cells sit above the maximum terrain/tree height. Only the explicit
    // fixture edits can obstruct them, even when the procedural seed changes.
    const FIntVector FirstCell(5, 5, 60);
    const FIntVector SecondCell(6, 5, 60);
    const FIntVector RoofCell(5, 5, 62);
    FVoxelBuildInput OpenInput;
    OpenInput.Revision = 700;
    OpenInput.Epoch = 9;
    OpenInput.Overrides.Add(FirstCell, EBlockType::Grass);
    OpenInput.Overrides.Add(SecondCell, EBlockType::Grass);
    const FVoxelChunkMesh OpenMesh = BuildMesh(OpenInput);
    int32 OpenFirstQuad = INDEX_NONE;
    int32 OpenSecondQuad = INDEX_NONE;
    const float OpenSky = FaceSky(OpenMesh, EBlockType::Grass, FirstCell, 2, 1, &OpenFirstQuad);
    const float OpenSecondSky = FaceSky(OpenMesh, EBlockType::Grass, SecondCell, 2, 1, &OpenSecondQuad);
    Expect(OpenSky == 1.0f && OpenSky == OpenSecondSky && OpenFirstQuad == OpenSecondQuad,
        TEXT("Open-sky adjacent faces of the same material must receive sky and retain greedy merging"));

    FVoxelBuildInput RoofInput = OpenInput;
    RoofInput.Revision++;
    RoofInput.Overrides.Add(RoofCell, EBlockType::Stone);
    const FVoxelChunkMesh RoofMesh = BuildMesh(RoofInput);
    int32 RoofFirstQuad = INDEX_NONE;
    int32 RoofSecondQuad = INDEX_NONE;
    const float ClosedSky = FaceSky(RoofMesh, EBlockType::Grass, FirstCell, 2, 1, &RoofFirstQuad);
    const float UncoveredSky = FaceSky(RoofMesh, EBlockType::Grass, SecondCell, 2, 1, &RoofSecondQuad);
    Expect(ClosedSky == 0.0f && UncoveredSky == OpenSky,
        TEXT("An opaque roof must eliminate sky below it without changing an adjacent uncovered column"));
    Expect(RoofFirstQuad != INDEX_NONE && RoofSecondQuad != INDEX_NONE && RoofFirstQuad != RoofSecondQuad,
        TEXT("Same-material faces with different sky levels must not merge across the roof edge"));

    FVoxelBuildInput OneLeafInput = RoofInput;
    OneLeafInput.Overrides.Add(RoofCell, EBlockType::Leaves);
    const FVoxelChunkMesh OneLeafMesh = BuildMesh(OneLeafInput);
    FVoxelBuildInput TwoLeafInput = OneLeafInput;
    TwoLeafInput.Overrides.Add(RoofCell + FIntVector(0, 0, 1), EBlockType::Leaves);
    const FVoxelChunkMesh TwoLeafMesh = BuildMesh(TwoLeafInput);
    const float OneLeafSky = FaceSky(OneLeafMesh, EBlockType::Grass, FirstCell, 2, 1);
    const float TwoLeafSky = FaceSky(TwoLeafMesh, EBlockType::Grass, FirstCell, 2, 1);
    Expect(OpenSky > OneLeafSky && OneLeafSky > TwoLeafSky && TwoLeafSky > 0.0f
        && DecodeSkyLevel(OneLeafSky) == 11 && DecodeSkyLevel(TwoLeafSky) == 8,
        *FString::Printf(TEXT("Successive leaf layers must attenuate sky monotonically while leaving a readable canopy shadow"
            " (open=%.9f one=%.9f level=%d two=%.9f level=%d)"),
            OpenSky, OneLeafSky, DecodeSkyLevel(OneLeafSky), TwoLeafSky, DecodeSkyLevel(TwoLeafSky)));

    FVoxelBuildInput GlassRoofInput = RoofInput;
    GlassRoofInput.Overrides.Add(RoofCell, EBlockType::Glass);
    const FVoxelChunkMesh GlassRoofMesh = BuildMesh(GlassRoofInput);
    FVoxelBuildInput WaterRoofInput = RoofInput;
    WaterRoofInput.Overrides.Add(RoofCell, EBlockType::Water);
    const FVoxelChunkMesh WaterRoofMesh = BuildMesh(WaterRoofInput);
    const float GlassSky = FaceSky(GlassRoofMesh, EBlockType::Grass, FirstCell, 2, 1);
    const float WaterSky = FaceSky(WaterRoofMesh, EBlockType::Grass, FirstCell, 2, 1);
    // Under /fp:fast runtime level/15 can become level*(1/15), one float ULP
    // away from a folded 14.f/15.f literal. The contract is the integer bucket,
    // not a representation match within IsNearlyEqual's default 1e-8 tolerance.
    Expect(GlassSky == OpenSky && DecodeSkyLevel(WaterSky) == 14,
        *FString::Printf(TEXT("Glass must transmit sky fully while a water layer retains its quantized partial transmission"
            " (open=%.9f glass=%.9f level=%d water=%.9f level=%d expected=14)"),
            OpenSky, GlassSky, DecodeSkyLevel(GlassSky), WaterSky, DecodeSkyLevel(WaterSky)));
    UE_LOG(LogTemp, Display, TEXT("BR_VOXEL_SKY_SELFTEST open=%.9f opaque=%.9f leaf1=%.9f leaf2=%.9f glass=%.9f water=%.9f waterLevel=%d expectedWaterLevel=14"),
        OpenSky, ClosedSky, OneLeafSky, TwoLeafSky, GlassSky, WaterSky, DecodeSkyLevel(WaterSky));

    FVoxelBuildInput ReopenedInput = RoofInput;
    ReopenedInput.Revision++;
    ReopenedInput.Overrides.Add(RoofCell, EBlockType::Air);
    const FVoxelChunkMesh ReopenedMesh = BuildMesh(ReopenedInput);
    Expect(FaceSky(ReopenedMesh, EBlockType::Grass, FirstCell, 2, 1) == OpenSky
        && RoofInput.Overrides.FindChecked(RoofCell) == EBlockType::Stone
        && ReopenedMesh.Revision == ReopenedInput.Revision && ReopenedMesh.Epoch == ReopenedInput.Epoch,
        TEXT("A new edited snapshot must restore sky after roof removal without mutating the older worker snapshot"));
    for (const FVoxelChunkMesh* Mesh : { &OpenMesh, &RoofMesh, &OneLeafMesh, &TwoLeafMesh,
        &GlassRoofMesh, &WaterRoofMesh, &ReopenedMesh }) CheckColors(*Mesh);

    const FVoxelChunkMesh OpaqueCollision = BuildCollisionMesh(RoofInput);
    const FVoxelChunkMesh LeafCollision = BuildCollisionMesh(OneLeafInput);
    Expect(OpaqueCollision.Sections.IsEmpty() && LeafCollision.Sections.IsEmpty()
        && SameCollision(OpaqueCollision, LeafCollision),
        TEXT("Changing sky transmission without changing solid occupancy must preserve collision-only geometry and allocate no visual sections"));
    Expect(SameCollision(RoofMesh, OpaqueCollision) && SameCollision(OneLeafMesh, LeafCollision)
        && SameCollision(GlassRoofMesh, OpaqueCollision),
        *FString::Printf(TEXT("Full meshes must match solid-only collision exactly across opaque, leaf and glass roofs"
            " (opaque=%d leaf=%d glass=%d solidOnly=%d)"), RoofMesh.CollisionTriangles.Num(),
            OneLeafMesh.CollisionTriangles.Num(), GlassRoofMesh.CollisionTriangles.Num(), OpaqueCollision.CollisionTriangles.Num()));
    const FVoxelChunkMesh OpenCollision = BuildCollisionMesh(OpenInput);
    Expect(OpenCollision.Sections.IsEmpty() && SameCollision(OpenMesh, OpenCollision)
        && SameCollision(WaterRoofMesh, OpenCollision),
        *FString::Printf(TEXT("Water sky attenuation must never subdivide solid collision in a full mesh"
            " (open=%d water=%d solidOnly=%d)"), OpenMesh.CollisionTriangles.Num(),
            WaterRoofMesh.CollisionTriangles.Num(), OpenCollision.CollisionTriangles.Num()));
    FVoxelBuildInput MixedSolidsInput = OpenInput;
    MixedSolidsInput.Overrides.Add(FirstCell, EBlockType::Stone);
    MixedSolidsInput.Overrides.Add(SecondCell, EBlockType::Glass);
    const FVoxelChunkMesh MixedSolidsMesh = BuildMesh(MixedSolidsInput);
    Expect(SameCollision(MixedSolidsMesh, OpenCollision),
        *FString::Printf(TEXT("A visible opaque/glass interface must not create an internal solid collision face"
            " (mixed=%d solidOnly=%d)"), MixedSolidsMesh.CollisionTriangles.Num(), OpenCollision.CollisionTriangles.Num()));
    CheckColors(MixedSolidsMesh);

    // The occluding roof belongs to the chunk on the right; only its halo copy
    // is available to the left worker. Negative coordinates exercise the seam.
    const FIntVector SeamCell(-1, 5, 60);
    const FIntVector SeamRoof(0, 5, 62);
    FVoxelBuildInput SeamInput;
    SeamInput.Chunk = FIntPoint(-1, 0);
    SeamInput.Overrides.Add(SeamCell, EBlockType::Grass);
    SeamInput.Overrides.Add(SeamRoof, EBlockType::Stone);
    const FVoxelChunkMesh ClosedSeamMesh = BuildMesh(SeamInput);
    Expect(FaceSky(ClosedSeamMesh, EBlockType::Grass, FIntVector(15, 5, 60), 0, 1) == 0.0f,
        TEXT("A roof in a neighboring chunk must block sky on the seam face using the edited halo column"));
    FVoxelBuildInput NeighborInput = SeamInput;
    NeighborInput.Chunk = FIntPoint::ZeroValue;
    TArray<EBlockType> LeftBlocks;
    TArray<EBlockType> RightBlocks;
    TArray<uint8> LeftSky;
    TArray<uint8> RightSky;
    BuildChunkBlocks(SeamInput, LeftBlocks);
    BuildChunkBlocks(NeighborInput, RightBlocks);
    BuildSkyLevels(LeftBlocks, LeftSky);
    BuildSkyLevels(RightBlocks, RightSky);
    bool bSeamSkyMatches = true;
    for (int32 Z = -1; Z <= WorldHeight; ++Z)
        for (int32 Y = -1; Y <= ChunkSize; ++Y)
            for (int32 X = ChunkSize - 1; X <= ChunkSize; ++X)
                bSeamSkyMatches &= LeftSky[BufferIndex(FIntVector(X, Y, Z))]
                    == RightSky[BufferIndex(FIntVector(X - ChunkSize, Y, Z))];
    Expect(bSeamSkyMatches, TEXT("Overlapping sky columns must agree across chunk origins, generated trees and edited roofs"));
    SeamInput.Overrides.Add(SeamRoof, EBlockType::Air);
    const FVoxelChunkMesh OpenSeamMesh = BuildMesh(SeamInput);
    Expect(FaceSky(OpenSeamMesh, EBlockType::Grass, FIntVector(15, 5, 60), 0, 1) == OpenSky,
        TEXT("Rebuilding after a halo roof edit must restore the adjacent chunk's seam-face sky"));
    CheckColors(ClosedSeamMesh);
    CheckColors(OpenSeamMesh);
    return Failures.Num() == InitialFailureCount;
}

bool FVoxelData::RunSaveSelfTests(TArray<FString>& Failures)
{
    const int32 InitialFailureCount = Failures.Num();
    RunMeshSelfTests(Failures);
    auto Expect = [&Failures](bool bCondition, const TCHAR* Message)
    {
        if (!bCondition) Failures.Add(FString(TEXT("Save schema: ")) + Message);
    };
    auto SamePlayer = [](const FWorldSaveState& A, const FWorldSaveState& B)
    {
        if (!A.Position.Equals(B.Position, 0.0) || !A.Rotation.Equals(B.Rotation, 0.0)
            || A.SelectedSlot != B.SelectedSlot || A.Inventory != B.Inventory
            || A.bHasSurvivalState != B.bHasSurvivalState || A.Health != B.Health || A.Hunger != B.Hunger
            || A.CraftedPlanks != B.CraftedPlanks || A.FoodCount != B.FoodCount
            || A.SelectedToolSlot != B.SelectedToolSlot || A.bBareHandsEquipped != B.bBareHandsEquipped
            || A.TimeOfDay != B.TimeOfDay
            || A.PlacedTorches != B.PlacedTorches || A.PlacedTorchSupports != B.PlacedTorchSupports
            || A.ToolSlots.Num() != B.ToolSlots.Num() || A.Crops.Num() != B.Crops.Num()
            || A.RestPoints.Num() != B.RestPoints.Num() || A.Furnaces.Num() != B.Furnaces.Num()
            || A.bHasRespawnPoint != B.bHasRespawnPoint || A.RespawnPoint != B.RespawnPoint) return false;
        for (int32 Index = 0; Index < A.ToolSlots.Num(); ++Index)
        {
            const FBlockRealmSavedTool& Left = A.ToolSlots[Index];
            const FBlockRealmSavedTool& Right = B.ToolSlots[Index];
            if (Left.Type != Right.Type || Left.Durability != Right.Durability
                || Left.MaxDurability != Right.MaxDurability || Left.Quantity != Right.Quantity) return false;
        }
        for (int32 Index = 0; Index < A.Crops.Num(); ++Index)
            if (A.Crops[Index].Cell != B.Crops[Index].Cell
                || A.Crops[Index].GrowthSeconds != B.Crops[Index].GrowthSeconds) return false;
        for (int32 Index = 0; Index < A.RestPoints.Num(); ++Index)
            if (A.RestPoints[Index].Cell != B.RestPoints[Index].Cell
                || A.RestPoints[Index].Heading != B.RestPoints[Index].Heading) return false;
        for (int32 Index = 0; Index < A.Furnaces.Num(); ++Index)
        {
            const FBlockRealmSavedFurnace& Left = A.Furnaces[Index];
            const FBlockRealmSavedFurnace& Right = B.Furnaces[Index];
            if (Left.Cell != Right.Cell || Left.InputSand != Right.InputSand || Left.FuelWood != Right.FuelWood
                || Left.OutputGlass != Right.OutputGlass || Left.CookProgress != Right.CookProgress) return false;
        }
        return true;
    };

    FVoxelSaveRecord Legacy;
    Legacy.Seed = -91237;
    Legacy.Player.Position = FVector(-12345.25, 67890.5, 2468.75);
    Legacy.Player.Rotation = FRotator(17.25, -33.5, 0.0);
    Legacy.Player.SelectedSlot = 8;
    Legacy.Player.Inventory.Init(64, 9);
    Legacy.Player.Inventory[4] = 57;
    Legacy.Player.Inventory[7] = 0;
    Legacy.Overrides.Add(FIntVector(-17, 16, 3), EBlockType::Glass);
    Legacy.Overrides.Add(FIntVector(16, -17, 63), EBlockType::Glow);
    Legacy.Overrides.Add(FIntVector(0, 0, 22), EBlockType::Air);
    FVoxelSaveRecord Progress = Legacy;
    Progress.Player.bHasSurvivalState = true;
    Progress.Player.Health = 37.25f;
    Progress.Player.Hunger = 11.5f;
    Progress.Player.CraftedPlanks = 1234;
    Progress.Player.FoodCount = 2;
    Progress.Player.SelectedToolSlot = 4;
    Progress.Player.TimeOfDay = .875f;
    Progress.Player.ToolSlots.SetNum(SavedToolSlotCount);
    Progress.Player.ToolSlots[0] = { 1, 12, 59, 1 };
    Progress.Player.ToolSlots[1] = { 2, 87, 131, 1 };
    Progress.Player.ToolSlots[2] = { 3, 0, 0, 64 };
    Progress.Player.ToolSlots[4] = { 1, 59, 59, 1 };
    Progress.Player.ToolSlots[5] = { 3, 0, 0, 3 };
    Progress.Player.PlacedTorches = { FIntVector(-MaxCoordinate, MaxCoordinate, 1),
        FIntVector(-17, 16, WorldHeight - 1), FIntVector(16, -17, 22) };

    FString LegacyJson;
    const bool bSerializedLegacy = SerializeRecord(Legacy, LegacyJson);
    Expect(bSerializedLegacy, TEXT("Legacy state must serialize with version 1"));
    FVoxelSaveRecord RestoredLegacy = Progress;
    const bool bParsedLegacy = bSerializedLegacy && ParseRecord(LegacyJson, RestoredLegacy);
    Expect(bParsedLegacy && RestoredLegacy.Seed == Legacy.Seed && SamePlayer(RestoredLegacy.Player, Legacy.Player)
        && RestoredLegacy.Overrides.OrderIndependentCompareEqual(Legacy.Overrides),
        TEXT("Legacy load must restore base data and replace prior survival values with defaults"));
    TSharedPtr<FJsonObject> LegacyRoot;
    const bool bReadLegacyRoot = bSerializedLegacy
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LegacyJson), LegacyRoot) && LegacyRoot.IsValid();
    int32 LegacyVersion = 0;
    Expect(bReadLegacyRoot && !LegacyRoot->HasField(TEXT("survival")) && !LegacyRoot->HasField(TEXT("placedTorches"))
        && !LegacyRoot->HasField(TEXT("placedTorchSupports")) && !LegacyRoot->HasField(TEXT("crops"))
        && !LegacyRoot->HasField(TEXT("restPoints")) && !LegacyRoot->HasField(TEXT("furnaces"))
        && !LegacyRoot->HasField(TEXT("respawnPoint"))
        && ReadInteger(LegacyRoot->TryGetField(TEXT("version")), 1, 1, LegacyVersion),
        TEXT("Empty legacy state must omit optional extras and retain version 1"));

    FString ProgressJson;
    const bool bSerializedProgress = SerializeRecord(Progress, ProgressJson);
    Expect(bSerializedProgress, TEXT("Survival state with worn pickaxes and zero-durability torches must serialize"));
    FVoxelSaveRecord RestoredProgress;
    const bool bParsedProgress = bSerializedProgress && ParseRecord(ProgressJson, RestoredProgress);
    Expect(bParsedProgress && RestoredProgress.Seed == Progress.Seed && SamePlayer(RestoredProgress.Player, Progress.Player)
        && RestoredProgress.Overrides.OrderIndependentCompareEqual(Progress.Overrides),
        TEXT("Round trip must preserve health, hunger, crafting, food, selection, all tools, time, torches and world data"));
    if (!bSerializedProgress) return false;
    FVoxelSaveRecord BareHands = Progress;
    BareHands.Player.SelectedToolSlot = -1;
    BareHands.Player.bBareHandsEquipped = true;
    FString BareHandsJson;
    FVoxelSaveRecord RestoredBareHands = Progress;
    Expect(SerializeRecord(BareHands, BareHandsJson) && ParseRecord(BareHandsJson, RestoredBareHands)
        && SamePlayer(BareHands.Player, RestoredBareHands.Player)
        && RestoredBareHands.Overrides.OrderIndependentCompareEqual(BareHands.Overrides),
        TEXT("Explicit bare hands must round trip without changing the selected block, inventory or tools"));
    FVoxelSaveRecord SelectedBlock = BareHands;
    SelectedBlock.Player.bBareHandsEquipped = false;
    FString SelectedBlockJson;
    Expect(SerializeRecord(SelectedBlock, SelectedBlockJson) && ParseRecord(SelectedBlockJson, RestoredBareHands)
        && SamePlayer(SelectedBlock.Player, RestoredBareHands.Player),
        TEXT("A selected block with tool slot -1 must clear a previously loaded bare-hands flag"));
    FVoxelSaveRecord OrientedTorches = Progress;
    for (const FIntVector& Cell : OrientedTorches.Player.PlacedTorches)
        OrientedTorches.Player.PlacedTorchSupports.Add(Cell - FIntVector(0, 0, 1));
    FString OrientedJson;
    FVoxelSaveRecord RestoredOriented;
    Expect(SerializeRecord(OrientedTorches, OrientedJson) && ParseRecord(OrientedJson, RestoredOriented)
        && SamePlayer(OrientedTorches.Player, RestoredOriented.Player),
        TEXT("Torch support directions must survive round trip while old position-only saves remain valid"));
    for (bool bUpperBounds : { false, true })
    {
        FVoxelSaveRecord Boundary = Progress;
        Boundary.Player.Health = Boundary.Player.Hunger = bUpperBounds ? 100.f : 0.f;
        Boundary.Player.CraftedPlanks = Boundary.Player.FoodCount = bUpperBounds ? 1000000 : 0;
        Boundary.Player.SelectedToolSlot = bUpperBounds ? SavedToolSlotCount - 1 : -1;
        Boundary.Player.TimeOfDay = bUpperBounds ? 1.f : 0.f;
        Boundary.Player.PlacedTorches.Empty();
        if (bUpperBounds)
            for (int32 Index = 0; Index < MaxSavedTorches; ++Index) Boundary.Player.PlacedTorches.Add(FIntVector(Index, 0, 1));
        FString BoundaryJson;
        FVoxelSaveRecord RestoredBoundary;
        Expect(SerializeRecord(Boundary, BoundaryJson) && ParseRecord(BoundaryJson, RestoredBoundary)
            && SamePlayer(Boundary.Player, RestoredBoundary.Player),
            TEXT("Inclusive survival limits and exactly 512 torches must round trip"));
    }

    auto MakeFixture = [&ProgressJson](auto Mutate, FString& OutJson)
    {
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ProgressJson), Root) || !Root.IsValid()) return false;
        Mutate(Root.ToSharedRef());
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
            = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
        return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    };
    auto CheckRejected = [&Expect, &SamePlayer, &Progress](const FString& BadJson, const TCHAR* Message)
    {
        FVoxelSaveRecord Untouched = Progress;
        Expect(!ParseRecord(BadJson, Untouched) && Untouched.Seed == Progress.Seed
            && SamePlayer(Untouched.Player, Progress.Player)
            && Untouched.Overrides.OrderIndependentCompareEqual(Progress.Overrides), Message);
    };
    auto CheckMutation = [&MakeFixture, &CheckRejected, &Expect](auto Mutate, const TCHAR* Message)
    {
        FString Json;
        const bool bMadeFixture = MakeFixture(Mutate, Json);
        Expect(bMadeFixture, TEXT("Corruption fixture must serialize"));
        if (bMadeFixture) CheckRejected(Json, Message);
    };
    auto CheckInvalidState = [&Progress, &Expect](const FWorldSaveState& State, const TCHAR* Message)
    {
        FVoxelSaveRecord Invalid = Progress;
        Invalid.Player = State;
        FString Untouched = TEXT("previous valid save");
        Expect(!SerializeRecord(Invalid, Untouched) && Untouched == TEXT("previous valid save"), Message);
    };

    FString WithoutBareHands;
    FVoxelSaveRecord OldSurvival = BareHands;
    Expect(MakeFixture([](const TSharedRef<FJsonObject>& Root)
        { Root->GetObjectField(TEXT("survival"))->RemoveField(TEXT("bareHandsEquipped")); }, WithoutBareHands)
        && ParseRecord(WithoutBareHands, OldSurvival) && SamePlayer(OldSurvival.Player, Progress.Player),
        TEXT("Old survival saves without bareHandsEquipped must default to false and preserve tool selection"));
    OldSurvival = BareHands;
    WithoutBareHands.Reset();
    Expect(MakeFixture([](const TSharedRef<FJsonObject>& Root)
    {
        Root->GetObjectField(TEXT("survival"))->RemoveField(TEXT("bareHandsEquipped"));
        Root->GetObjectField(TEXT("survival"))->SetNumberField(TEXT("selectedToolSlot"), -1);
    }, WithoutBareHands) && ParseRecord(WithoutBareHands, OldSurvival)
        && SamePlayer(OldSurvival.Player, SelectedBlock.Player),
        TEXT("Old survival saves with tool slot -1 must keep the selected block and clear bare hands"));
    RestoredBareHands = BareHands;
    Expect(ParseRecord(LegacyJson, RestoredBareHands) && SamePlayer(RestoredBareHands.Player, Legacy.Player),
        TEXT("Legacy saves without survival must clear a previously loaded bare-hands flag"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->GetObjectField(TEXT("survival"))->SetStringField(TEXT("bareHandsEquipped"), TEXT("true")); },
        TEXT("String bare-hands flags must be rejected without changing output"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->GetObjectField(TEXT("survival"))->SetNumberField(TEXT("bareHandsEquipped"), 1); },
        TEXT("Numeric bare-hands flags must be rejected without changing output"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->GetObjectField(TEXT("survival"))->SetField(TEXT("bareHandsEquipped"), MakeShared<FJsonValueNull>()); },
        TEXT("Null bare-hands flags must be rejected without changing output"));
    for (int32 InvalidToolSlot : { -2, SavedToolSlotCount })
        CheckMutation([InvalidToolSlot](const TSharedRef<FJsonObject>& Root)
        {
            Root->GetObjectField(TEXT("survival"))->SetBoolField(TEXT("bareHandsEquipped"), true);
            Root->GetObjectField(TEXT("survival"))->SetNumberField(TEXT("selectedToolSlot"), InvalidToolSlot);
        }, TEXT("Bare hands must not expand the legacy selected tool slot range"));

    FString WithoutTorches;
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    {
        Root->SetArrayField(TEXT("placedTorchSupports"), {
            MakeShared<FJsonValueArray>(NumberArray(0, 0, 1)) });
    }, TEXT("Mismatched torch support count must be rejected without changing output"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    {
        TArray<TSharedPtr<FJsonValue>> Supports;
        for (const TSharedPtr<FJsonValue>& Cell : Root->GetArrayField(TEXT("placedTorches"))) Supports.Add(Cell);
        Root->SetArrayField(TEXT("placedTorchSupports"), Supports);
    }, TEXT("A torch cannot use its own cell as a support"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetStringField(TEXT("placedTorchSupports"), TEXT("invalid")); },
        TEXT("Wrong type for optional torch supports must preserve output on rejection"));
    FVoxelSaveRecord OptionalTorches;
    Expect(MakeFixture([](const TSharedRef<FJsonObject>& Root) { Root->RemoveField(TEXT("placedTorches")); }, WithoutTorches)
        && ParseRecord(WithoutTorches, OptionalTorches) && OptionalTorches.Player.bHasSurvivalState
        && OptionalTorches.Player.PlacedTorches.IsEmpty(), TEXT("Missing optional torch array must default to empty"));
    FString TorchesOnly;
    FVoxelSaveRecord IndependentTorches;
    Expect(MakeFixture([](const TSharedRef<FJsonObject>& Root) { Root->RemoveField(TEXT("survival")); }, TorchesOnly)
        && ParseRecord(TorchesOnly, IndependentTorches) && !IndependentTorches.Player.bHasSurvivalState
        && IndependentTorches.Player.Health == 100.f && IndependentTorches.Player.ToolSlots.IsEmpty()
        && IndependentTorches.Player.PlacedTorches == Progress.Player.PlacedTorches,
        TEXT("Optional torch positions must load independently from survival data"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetField(TEXT("survival"), MakeShared<FJsonValueNull>()); }, TEXT("Null survival object must be rejected without changing output"));
    const TCHAR* RequiredFields[] = { TEXT("health"), TEXT("hunger"), TEXT("craftedPlanks"), TEXT("foodCount"),
        TEXT("selectedToolSlot"), TEXT("toolSlots"), TEXT("timeOfDay") };
    for (const TCHAR* Field : RequiredFields)
    {
        CheckMutation([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetObjectField(TEXT("survival"))->RemoveField(Field); },
            *FString::Printf(TEXT("Missing survival field %s must be rejected without changing output"), Field));
        CheckMutation([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetObjectField(TEXT("survival"))->SetStringField(Field, TEXT("invalid")); },
            *FString::Printf(TEXT("Wrong type for survival field %s must be rejected without changing output"), Field));
    }
    struct FBadNumber { const TCHAR* Field; double Value; };
    const FBadNumber BadNumbers[] = {
        { TEXT("health"), -1.0 }, { TEXT("health"), 100.01 }, { TEXT("hunger"), -1.0 }, { TEXT("hunger"), 101.0 },
        { TEXT("craftedPlanks"), -1.0 }, { TEXT("craftedPlanks"), 1000001.0 }, { TEXT("craftedPlanks"), .5 },
        { TEXT("foodCount"), -1.0 }, { TEXT("foodCount"), 1000001.0 }, { TEXT("foodCount"), .5 },
        { TEXT("selectedToolSlot"), -2.0 }, { TEXT("selectedToolSlot"), 6.0 }, { TEXT("selectedToolSlot"), .5 },
        { TEXT("timeOfDay"), -.01 }, { TEXT("timeOfDay"), 1.001 }
    };
    for (const FBadNumber& Bad : BadNumbers)
        CheckMutation([Bad](const TSharedRef<FJsonObject>& Root)
        { Root->GetObjectField(TEXT("survival"))->SetNumberField(Bad.Field, Bad.Value); },
            *FString::Printf(TEXT("Invalid survival number %s=%g must be rejected without changing output"), Bad.Field, Bad.Value));

    FString OverflowJson;
    const bool bMadeOverflow = MakeFixture([](const TSharedRef<FJsonObject>& Root)
    { Root->GetObjectField(TEXT("survival"))->SetStringField(TEXT("health"), TEXT("overflow-number-fixture")); }, OverflowJson);
    Expect(bMadeOverflow && OverflowJson.ReplaceInline(TEXT("\"overflow-number-fixture\""), TEXT("1e999")) == 1,
        TEXT("Overflow-number fixture must contain a raw numeric token"));
    if (bMadeOverflow) CheckRejected(OverflowJson, TEXT("Non-finite parsed number must be rejected without changing output"));

    for (int32 SlotCount : { SavedToolSlotCount - 1, SavedToolSlotCount + 1 })
        CheckMutation([SlotCount](const TSharedRef<FJsonObject>& Root)
        {
            TArray<TSharedPtr<FJsonValue>> Tools = Root->GetObjectField(TEXT("survival"))->GetArrayField(TEXT("toolSlots"));
            const TSharedPtr<FJsonValue> FirstTool = Tools[0];
            while (Tools.Num() < SlotCount) Tools.Add(FirstTool);
            Tools.SetNum(SlotCount);
            Root->GetObjectField(TEXT("survival"))->SetArrayField(TEXT("toolSlots"), Tools);
        }, TEXT("Tool count other than six must be rejected without changing output"));
    const TCHAR* ToolFields[] = { TEXT("type"), TEXT("durability"), TEXT("maxDurability"), TEXT("quantity") };
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    {
        TArray<TSharedPtr<FJsonValue>> Tools = Root->GetObjectField(TEXT("survival"))->GetArrayField(TEXT("toolSlots"));
        Tools[0] = MakeShared<FJsonValueNull>();
        Root->GetObjectField(TEXT("survival"))->SetArrayField(TEXT("toolSlots"), Tools);
    }, TEXT("Null tool slot must preserve output on rejection"));
    for (const TCHAR* Field : ToolFields)
    {
        CheckMutation([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetObjectField(TEXT("survival"))->GetArrayField(TEXT("toolSlots"))[0]->AsObject()->RemoveField(Field); },
            TEXT("Incomplete tool object must be rejected without changing output"));
        CheckMutation([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetObjectField(TEXT("survival"))->GetArrayField(TEXT("toolSlots"))[0]->AsObject()->SetStringField(Field, TEXT("1")); },
            TEXT("Non-number tool field must be rejected without changing output"));
    }
    auto RejectToolNumber = [&CheckMutation](int32 Slot, const TCHAR* Field, double Value, const TCHAR* Message)
    {
        CheckMutation([Slot, Field, Value](const TSharedRef<FJsonObject>& Root)
        { Root->GetObjectField(TEXT("survival"))->GetArrayField(TEXT("toolSlots"))[Slot]->AsObject()->SetNumberField(Field, Value); }, Message);
    };
    RejectToolNumber(0, TEXT("type"), 4.0, TEXT("Unknown tool type must preserve output on rejection"));
    RejectToolNumber(0, TEXT("type"), 1.5, TEXT("Fractional tool type must preserve output on rejection"));
    RejectToolNumber(0, TEXT("durability"), -1.0, TEXT("Negative durability must preserve output on rejection"));
    RejectToolNumber(0, TEXT("durability"), 132.0, TEXT("Excessive durability must preserve output on rejection"));
    RejectToolNumber(0, TEXT("durability"), 60.0, TEXT("Current durability above maximum must preserve output on rejection"));
    RejectToolNumber(0, TEXT("durability"), 0.0, TEXT("Broken pickaxe must preserve output on rejection"));
    RejectToolNumber(0, TEXT("maxDurability"), 0.0, TEXT("Pickaxe with zero maximum durability must preserve output on rejection"));
    RejectToolNumber(0, TEXT("maxDurability"), 132.0, TEXT("Excessive maximum durability must preserve output on rejection"));
    RejectToolNumber(0, TEXT("quantity"), 0.0, TEXT("Non-empty tool type with zero quantity must preserve output on rejection"));
    RejectToolNumber(0, TEXT("quantity"), 65.0, TEXT("Excessive tool quantity must preserve output on rejection"));
    RejectToolNumber(0, TEXT("quantity"), .5, TEXT("Fractional tool quantity must preserve output on rejection"));
    RejectToolNumber(2, TEXT("quantity"), 0.0, TEXT("Torch with zero quantity must preserve output on rejection"));
    RejectToolNumber(3, TEXT("quantity"), 1.0, TEXT("None tool with quantity must preserve output on rejection"));

    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetField(TEXT("placedTorches"), MakeShared<FJsonValueNull>()); }, TEXT("Null torch array must preserve output on rejection"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    {
        TArray<TSharedPtr<FJsonValue>> Torches;
        for (int32 Index = 0; Index <= MaxSavedTorches; ++Index)
            Torches.Add(MakeShared<FJsonValueArray>(NumberArray(Index, 0, 1)));
        Root->SetArrayField(TEXT("placedTorches"), Torches);
    }, TEXT("More than 512 torch positions must preserve output on rejection"));
    const FIntVector BadPositions[] = { FIntVector(MaxCoordinate + 1, 0, 1), FIntVector(0, -MaxCoordinate - 1, 1),
        FIntVector(0, 0, 0), FIntVector(0, 0, WorldHeight) };
    for (const FIntVector& Position : BadPositions)
        CheckMutation([Position](const TSharedRef<FJsonObject>& Root)
        { Root->SetArrayField(TEXT("placedTorches"), { MakeShared<FJsonValueArray>(NumberArray(Position.X, Position.Y, Position.Z)) }); },
            TEXT("Out-of-world torch coordinate must preserve output on rejection"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetArrayField(TEXT("placedTorches"), { MakeShared<FJsonValueArray>(NumberArray(.5, 0, 1)) }); },
        TEXT("Fractional torch coordinate must preserve output on rejection"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    {
        TArray<TSharedPtr<FJsonValue>> Position = NumberArray(0, 0, 1);
        Position.Pop();
        Root->SetArrayField(TEXT("placedTorches"), { MakeShared<FJsonValueArray>(Position) });
    }, TEXT("Incomplete torch coordinate must preserve output on rejection"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    {
        TArray<TSharedPtr<FJsonValue>> Torches = Root->GetArrayField(TEXT("placedTorches"));
        const TSharedPtr<FJsonValue> Duplicate = Torches[0];
        Torches.Add(Duplicate);
        Root->SetArrayField(TEXT("placedTorches"), Torches);
    }, TEXT("Duplicate torch coordinate must preserve output on rejection"));
    CheckMutation([](const TSharedRef<FJsonObject>& Root)
    {
        Root->RemoveField(TEXT("survival"));
        Root->SetArrayField(TEXT("placedTorches"), { MakeShared<FJsonValueArray>(NumberArray(0, 0, 0)) });
    }, TEXT("Malformed torch array without survival must preserve output on rejection"));

    FWorldSaveState Invalid = Progress.Player;
    Invalid.Health = std::numeric_limits<float>::infinity();
    CheckInvalidState(Invalid, TEXT("Non-finite health must not replace serialized output"));
    Invalid = Progress.Player;
    Invalid.Hunger = std::numeric_limits<float>::quiet_NaN();
    CheckInvalidState(Invalid, TEXT("Non-finite hunger must not replace serialized output"));
    Invalid = Progress.Player;
    Invalid.TimeOfDay = std::numeric_limits<float>::infinity();
    CheckInvalidState(Invalid, TEXT("Non-finite time must not replace serialized output"));
    Invalid = Progress.Player;
    Invalid.ToolSlots[0].Durability = Invalid.ToolSlots[0].MaxDurability + 1;
    CheckInvalidState(Invalid, TEXT("Inconsistent tool durability must not replace serialized output"));
    Invalid = Progress.Player;
    Invalid.PlacedTorches.Add(FIntVector(0, 0, 0));
    CheckInvalidState(Invalid, TEXT("Invalid torch position must not replace serialized output"));

    FVoxelSaveRecord Expansion = OrientedTorches;
    Expansion.Player.Crops = { { FIntVector(-MaxCoordinate, -MaxCoordinate, 1), 0.f },
        { FIntVector(MaxCoordinate, MaxCoordinate, WorldHeight - 1), MaxCropGrowthSeconds },
        { FIntVector(-16, 17, 22), 60.25f } };
    for (int32 Heading = 0; Heading < 4; ++Heading)
        Expansion.Player.RestPoints.Add({ FIntVector(20 + Heading * 4, 20, 22), Heading });
    Expansion.Player.Furnaces = { { FIntVector(40, -40, 1), 64, 64, 63, 7.5f },
        { FIntVector(-40, 40, WorldHeight - 1), 0, 0, 64, 0.f } };
    Expansion.Player.bHasRespawnPoint = true;
    Expansion.Player.RespawnPoint = Expansion.Player.RestPoints[0].Cell;
    FString ExpansionJson;
    const bool bSerializedExpansion = SerializeRecord(Expansion, ExpansionJson);
    FVoxelSaveRecord RestoredExpansion;
    Expect(bSerializedExpansion && ParseRecord(ExpansionJson, RestoredExpansion)
        && RestoredExpansion.Seed == Expansion.Seed && SamePlayer(RestoredExpansion.Player, Expansion.Player)
        && RestoredExpansion.Overrides.OrderIndependentCompareEqual(Expansion.Overrides),
        TEXT("Crops, four bed headings, furnace stacks/progress and respawn anchor must round trip with survival and torch supports"));
    if (!bSerializedExpansion) return false;

    FVoxelSaveRecord ClearedExpansion = Expansion;
    Expect(ParseRecord(LegacyJson, ClearedExpansion) && SamePlayer(ClearedExpansion.Player, Legacy.Player)
        && ClearedExpansion.Seed == Legacy.Seed
        && ClearedExpansion.Overrides.OrderIndependentCompareEqual(Legacy.Overrides),
        TEXT("Legacy load must clear previous expansion objects and reset the respawn flag and cell"));

    FVoxelSaveRecord IndependentExpansion = Legacy;
    IndependentExpansion.Player.PlacedTorches = Expansion.Player.PlacedTorches;
    IndependentExpansion.Player.PlacedTorchSupports = Expansion.Player.PlacedTorchSupports;
    IndependentExpansion.Player.Crops = Expansion.Player.Crops;
    IndependentExpansion.Player.RestPoints = Expansion.Player.RestPoints;
    IndependentExpansion.Player.Furnaces = Expansion.Player.Furnaces;
    IndependentExpansion.Player.bHasRespawnPoint = true;
    IndependentExpansion.Player.RespawnPoint = Expansion.Player.RespawnPoint;
    FString IndependentJson;
    FVoxelSaveRecord RestoredIndependent;
    const bool bSerializedIndependent = SerializeRecord(IndependentExpansion, IndependentJson);
    Expect(bSerializedIndependent && ParseRecord(IndependentJson, RestoredIndependent)
        && SamePlayer(RestoredIndependent.Player, IndependentExpansion.Player),
        TEXT("Expansion objects, respawn and oriented torches must round trip without survival"));
    TSharedPtr<FJsonObject> IndependentRoot;
    Expect(bSerializedIndependent
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(IndependentJson), IndependentRoot)
        && IndependentRoot.IsValid() && !IndependentRoot->HasField(TEXT("survival"))
        && IndependentRoot->HasField(TEXT("placedTorches")) && IndependentRoot->HasField(TEXT("placedTorchSupports"))
        && IndependentRoot->HasField(TEXT("crops")) && IndependentRoot->HasField(TEXT("restPoints"))
        && IndependentRoot->HasField(TEXT("furnaces")) && IndependentRoot->HasField(TEXT("respawnPoint")),
        TEXT("All nonempty extras must serialize as independent root fields without survival"));

    auto MakeExpansionFixture = [&ExpansionJson](auto Mutate, FString& OutJson)
    {
        TSharedPtr<FJsonObject> Root;
        if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ExpansionJson), Root) || !Root.IsValid()) return false;
        Mutate(Root.ToSharedRef());
        const TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer
            = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutJson);
        return FJsonSerializer::Serialize(Root.ToSharedRef(), Writer);
    };
    auto CheckExpansionRejected = [&Expect, &SamePlayer, &Expansion](const FString& BadJson, const TCHAR* Message)
    {
        FVoxelSaveRecord Untouched = Expansion;
        Expect(!ParseRecord(BadJson, Untouched) && Untouched.Seed == Expansion.Seed
            && SamePlayer(Untouched.Player, Expansion.Player)
            && Untouched.Overrides.OrderIndependentCompareEqual(Expansion.Overrides), Message);
    };
    auto CheckExpansionMutation = [&MakeExpansionFixture, &CheckExpansionRejected, &Expect](auto Mutate, const TCHAR* Message)
    {
        FString Json;
        const bool bMadeFixture = MakeExpansionFixture(Mutate, Json);
        Expect(bMadeFixture, TEXT("Expansion corruption fixture must serialize"));
        if (bMadeFixture) CheckExpansionRejected(Json, Message);
    };

    for (bool bEmptyArrays : { false, true })
    {
        FString Json;
        FVoxelSaveRecord Restored = Expansion;
        const bool bMadeFixture = MakeExpansionFixture([bEmptyArrays](const TSharedRef<FJsonObject>& Root)
        {
            for (const TCHAR* Field : { TEXT("crops"), TEXT("restPoints"), TEXT("furnaces") })
            {
                if (bEmptyArrays) Root->SetArrayField(Field, TArray<TSharedPtr<FJsonValue>>());
                else Root->RemoveField(Field);
            }
            Root->RemoveField(TEXT("respawnPoint"));
        }, Json);
        Expect(bMadeFixture && ParseRecord(Json, Restored) && SamePlayer(Restored.Player, OrientedTorches.Player),
            TEXT("Missing or explicitly empty expansion arrays must reset defaults and preserve prior schema data"));
    }
    FString WithoutRespawn;
    FVoxelSaveRecord RestoredWithoutRespawn;
    FWorldSaveState ExpectedWithoutRespawn = Expansion.Player;
    ExpectedWithoutRespawn.bHasRespawnPoint = false;
    ExpectedWithoutRespawn.RespawnPoint = FIntVector::ZeroValue;
    Expect(MakeExpansionFixture([](const TSharedRef<FJsonObject>& Root) { Root->RemoveField(TEXT("respawnPoint")); }, WithoutRespawn)
        && ParseRecord(WithoutRespawn, RestoredWithoutRespawn)
        && SamePlayer(RestoredWithoutRespawn.Player, ExpectedWithoutRespawn),
        TEXT("Absent respawnPoint must default to false and zero without discarding saved beds"));
    FVoxelSaveRecord NoRespawn = Expansion;
    NoRespawn.Player.bHasRespawnPoint = false;
    NoRespawn.Player.RespawnPoint = FIntVector::ZeroValue;
    FString NoRespawnJson;
    TSharedPtr<FJsonObject> NoRespawnRoot;
    Expect(SerializeRecord(NoRespawn, NoRespawnJson)
        && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NoRespawnJson), NoRespawnRoot)
        && NoRespawnRoot.IsValid() && !NoRespawnRoot->HasField(TEXT("respawnPoint")),
        TEXT("Respawn root field must be omitted when its flag is false"));

    // Exact object caps and both inclusive numeric endpoints remain valid.
    FVoxelSaveRecord MaximumExpansion = Legacy;
    for (int32 Index = 0; Index < MaxSavedTorches; ++Index)
        MaximumExpansion.Player.PlacedTorches.Add(FIntVector(Index, 0, 1));
    for (int32 Index = 0; Index < MaxSavedCrops; ++Index)
        MaximumExpansion.Player.Crops.Add({ FIntVector(Index, 100, 1), Index % 2 ? MaxCropGrowthSeconds : 0.f });
    for (int32 Index = 0; Index < MaxSavedRestPoints; ++Index)
        MaximumExpansion.Player.RestPoints.Add({ FIntVector(Index * 3, 200, WorldHeight - 1), Index % 4 });
    for (int32 Index = 0; Index < MaxSavedFurnaces; ++Index)
        MaximumExpansion.Player.Furnaces.Add({ FIntVector(Index, 300, 1), 64, 64, 64, 0.f });
    MaximumExpansion.Player.bHasRespawnPoint = true;
    MaximumExpansion.Player.RespawnPoint = MaximumExpansion.Player.RestPoints.Last().Cell;
    FString MaximumJson;
    FVoxelSaveRecord RestoredMaximum;
    Expect(SerializeRecord(MaximumExpansion, MaximumJson) && ParseRecord(MaximumJson, RestoredMaximum)
        && SamePlayer(RestoredMaximum.Player, MaximumExpansion.Player),
        TEXT("Exactly 512 torches, 256 crops, 32 beds and 64 furnaces must round trip without survival"));
    FVoxelSaveRecord EdgeBeds = Legacy;
    EdgeBeds.Player.RestPoints = { { FIntVector(MaxCoordinate, 0, WorldHeight - 1), 2 },
        { FIntVector(-MaxCoordinate, 0, 1), 0 }, { FIntVector(0, MaxCoordinate, WorldHeight - 1), 3 },
        { FIntVector(0, -MaxCoordinate, 1), 1 } };
    FString EdgeJson;
    FVoxelSaveRecord RestoredEdges;
    Expect(SerializeRecord(EdgeBeds, EdgeJson) && ParseRecord(EdgeJson, RestoredEdges)
        && SamePlayer(RestoredEdges.Player, EdgeBeds.Player),
        TEXT("Bed anchors at coordinate limits must be valid when their second cells point inward"));

    struct FExpansionArray { const TCHAR* Field; int32 Limit; };
    const FExpansionArray ExpansionArrays[] = { { TEXT("crops"), MaxSavedCrops },
        { TEXT("restPoints"), MaxSavedRestPoints }, { TEXT("furnaces"), MaxSavedFurnaces } };
    for (const FExpansionArray& Array : ExpansionArrays)
    {
        CheckExpansionMutation([Array](const TSharedRef<FJsonObject>& Root)
        { Root->SetField(Array.Field, MakeShared<FJsonValueNull>()); },
            TEXT("Null expansion array must preserve the entire output record"));
        CheckExpansionMutation([Array](const TSharedRef<FJsonObject>& Root)
        { Root->SetStringField(Array.Field, TEXT("invalid")); },
            TEXT("Wrong type for expansion array must preserve the entire output record"));
        CheckExpansionMutation([Array](const TSharedRef<FJsonObject>& Root)
        {
            TArray<TSharedPtr<FJsonValue>> Values = Root->GetArrayField(Array.Field);
            const TSharedPtr<FJsonValue> FirstValue = Values[0];
            while (Values.Num() <= Array.Limit) Values.Add(FirstValue);
            Root->SetArrayField(Array.Field, Values);
        }, TEXT("Expansion arrays over their object caps must preserve the entire output record"));
        CheckExpansionMutation([Array](const TSharedRef<FJsonObject>& Root)
        {
            TArray<TSharedPtr<FJsonValue>> Values = Root->GetArrayField(Array.Field);
            const TSharedPtr<FJsonValue> FirstValue = Values[0];
            Values.Add(FirstValue);
            Root->SetArrayField(Array.Field, Values);
        }, TEXT("Duplicate expansion object anchors must preserve the entire output record"));
        for (const TSharedPtr<FJsonValue>& BadValue : { TSharedPtr<FJsonValue>(MakeShared<FJsonValueNull>()),
            TSharedPtr<FJsonValue>(MakeShared<FJsonValueNumber>(1)), TSharedPtr<FJsonValue>(MakeShared<FJsonValueString>(TEXT("invalid"))),
            TSharedPtr<FJsonValue>(MakeShared<FJsonValueArray>(NumberArray(0, 0, 1))) })
            CheckExpansionMutation([Array, BadValue](const TSharedRef<FJsonObject>& Root)
            {
                TArray<TSharedPtr<FJsonValue>> Values = Root->GetArrayField(Array.Field);
                Values[0] = BadValue;
                Root->SetArrayField(Array.Field, Values);
            }, TEXT("Expansion entries must be non-null objects and reject other JSON value types atomically"));
        CheckExpansionMutation([Array](const TSharedRef<FJsonObject>& Root)
        {
            Root->RemoveField(TEXT("survival"));
            Root->SetField(Array.Field, MakeShared<FJsonValueNull>());
        }, TEXT("Missing survival must never bypass validation of malformed expansion arrays"));
    }

    struct FExpansionField { const TCHAR* Array; const TCHAR* Field; };
    const FExpansionField ExpansionFields[] = { { TEXT("crops"), TEXT("cell") }, { TEXT("crops"), TEXT("growthSeconds") },
        { TEXT("restPoints"), TEXT("cell") }, { TEXT("restPoints"), TEXT("heading") },
        { TEXT("furnaces"), TEXT("cell") }, { TEXT("furnaces"), TEXT("inputSand") },
        { TEXT("furnaces"), TEXT("fuelWood") }, { TEXT("furnaces"), TEXT("outputGlass") },
        { TEXT("furnaces"), TEXT("cookProgress") } };
    for (const FExpansionField& Field : ExpansionFields)
    {
        CheckExpansionMutation([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(Field.Array)[0]->AsObject()->RemoveField(Field.Field); },
            TEXT("Missing required expansion object field must preserve the entire output record"));
        CheckExpansionMutation([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(Field.Array)[0]->AsObject()->SetField(Field.Field, MakeShared<FJsonValueNull>()); },
            TEXT("Null expansion object field must preserve the entire output record"));
        CheckExpansionMutation([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(Field.Array)[0]->AsObject()->SetStringField(Field.Field, TEXT("1")); },
            TEXT("Wrong type for expansion object field must preserve the entire output record"));
    }

    const FIntVector BadExpansionCells[] = { FIntVector(MaxCoordinate + 1, 0, 1), FIntVector(-MaxCoordinate - 1, 0, 1),
        FIntVector(0, MaxCoordinate + 1, 1), FIntVector(0, -MaxCoordinate - 1, 1),
        FIntVector(0, 0, 0), FIntVector(0, 0, -1), FIntVector(0, 0, WorldHeight) };
    for (const FExpansionArray& Array : ExpansionArrays)
    {
        for (const FIntVector& Cell : BadExpansionCells)
            CheckExpansionMutation([Array, Cell](const TSharedRef<FJsonObject>& Root)
            {
                Root->RemoveField(TEXT("respawnPoint"));
                Root->GetArrayField(Array.Field)[0]->AsObject()->SetArrayField(TEXT("cell"), NumberArray(Cell.X, Cell.Y, Cell.Z));
            }, TEXT("Out-of-world expansion cell must preserve the entire output record"));
        for (int32 Axis = 0; Axis < 3; ++Axis)
            CheckExpansionMutation([Array, Axis](const TSharedRef<FJsonObject>& Root)
            {
                Root->RemoveField(TEXT("respawnPoint"));
                TArray<TSharedPtr<FJsonValue>> Cell = NumberArray(0, 0, 1);
                Cell[Axis] = MakeShared<FJsonValueNumber>(.5);
                Root->GetArrayField(Array.Field)[0]->AsObject()->SetArrayField(TEXT("cell"), Cell);
            }, TEXT("Fractional expansion coordinate must preserve the entire output record"));
        for (int32 Length : { 2, 4 })
            CheckExpansionMutation([Array, Length](const TSharedRef<FJsonObject>& Root)
            {
                TArray<TSharedPtr<FJsonValue>> Cell = NumberArray(0, 0, 1);
                if (Length == 2) Cell.Pop();
                else Cell.Add(MakeShared<FJsonValueNumber>(0));
                Root->GetArrayField(Array.Field)[0]->AsObject()->SetArrayField(TEXT("cell"), Cell);
            }, TEXT("Expansion cell must contain exactly three numbers"));
    }
    for (double Value : { -.01, 120.01 })
        CheckExpansionMutation([Value](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(TEXT("crops"))[0]->AsObject()->SetNumberField(TEXT("growthSeconds"), Value); },
            TEXT("Crop growth outside 0..120 must preserve the entire output record"));
    for (double Value : { -1.0, 4.0, .5 })
        CheckExpansionMutation([Value](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(TEXT("restPoints"))[0]->AsObject()->SetNumberField(TEXT("heading"), Value); },
            TEXT("Bed heading must be an integer in 0..3"));
    const FBlockRealmSavedRestPoint OutwardBeds[] = { { FIntVector(MaxCoordinate, 0, 1), 0 },
        { FIntVector(0, MaxCoordinate, 1), 1 }, { FIntVector(-MaxCoordinate, 0, 1), 2 },
        { FIntVector(0, -MaxCoordinate, 1), 3 } };
    for (const FBlockRealmSavedRestPoint& Bed : OutwardBeds)
        CheckExpansionMutation([Bed](const TSharedRef<FJsonObject>& Root)
        {
            Root->RemoveField(TEXT("respawnPoint"));
            Root->GetArrayField(TEXT("restPoints"))[0]->AsObject()->SetArrayField(TEXT("cell"), NumberArray(Bed.Cell.X, Bed.Cell.Y, Bed.Cell.Z));
            Root->GetArrayField(TEXT("restPoints"))[0]->AsObject()->SetNumberField(TEXT("heading"), Bed.Heading);
        }, TEXT("Bed second cell must remain within coordinate bounds in every heading"));
    for (const TCHAR* Field : { TEXT("inputSand"), TEXT("fuelWood"), TEXT("outputGlass") })
        for (double Value : { -1.0, 65.0, .5 })
            CheckExpansionMutation([Field, Value](const TSharedRef<FJsonObject>& Root)
            { Root->GetArrayField(TEXT("furnaces"))[0]->AsObject()->SetNumberField(Field, Value); },
                TEXT("Furnace stack must be an integer in 0..64"));
    for (double Value : { -.01, 8.0, 8.01 })
        CheckExpansionMutation([Value](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(TEXT("furnaces"))[0]->AsObject()->SetNumberField(TEXT("cookProgress"), Value); },
            TEXT("Furnace progress must be finite in half-open range [0, 8)"));
    for (const TCHAR* Field : { TEXT("inputSand"), TEXT("fuelWood"), TEXT("outputGlass") })
        CheckExpansionMutation([Field](const TSharedRef<FJsonObject>& Root)
        {
            const double Value = FString(Field) == TEXT("outputGlass") ? MaxSavedFurnaceStack : 0;
            Root->GetArrayField(TEXT("furnaces"))[0]->AsObject()->SetNumberField(Field, Value);
        }, TEXT("Positive furnace progress requires input, fuel and output space"));

    FString NearCompleteJson;
    FVoxelSaveRecord NearComplete;
    Expect(MakeExpansionFixture([](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(TEXT("furnaces"))[0]->AsObject()->SetNumberField(TEXT("cookProgress"), 7.999999999); }, NearCompleteJson)
        && ParseRecord(NearCompleteJson, NearComplete) && NearComplete.Player.Furnaces[0].CookProgress > 7.99f
        && NearComplete.Player.Furnaces[0].CookProgress < FurnaceCookSeconds,
        TEXT("A JSON value just below 8 must stay valid after float conversion without completing a batch"));
    FString UnderflowJson;
    const bool bMadeUnderflow = MakeExpansionFixture([](const TSharedRef<FJsonObject>& Root)
    {
        Root->GetArrayField(TEXT("furnaces"))[0]->AsObject()->SetNumberField(TEXT("inputSand"), 0);
        Root->GetArrayField(TEXT("furnaces"))[0]->AsObject()->SetNumberField(TEXT("cookProgress"), 1e-100);
    }, UnderflowJson);
    Expect(bMadeUnderflow, TEXT("Underflow-progress fixture must serialize"));
    if (bMadeUnderflow) CheckExpansionRejected(UnderflowJson,
        TEXT("A positive JSON progress token that underflows as float must still require input"));
    for (const FExpansionField& Field : { FExpansionField{ TEXT("crops"), TEXT("growthSeconds") },
        FExpansionField{ TEXT("furnaces"), TEXT("cookProgress") } })
    {
        FString NonFiniteJson;
        const bool bMadeFixture = MakeExpansionFixture([Field](const TSharedRef<FJsonObject>& Root)
        { Root->GetArrayField(Field.Array)[0]->AsObject()->SetStringField(Field.Field, TEXT("nonfinite-expansion-fixture")); }, NonFiniteJson);
        Expect(bMadeFixture && NonFiniteJson.ReplaceInline(TEXT("\"nonfinite-expansion-fixture\""), TEXT("1e999")) == 1,
            TEXT("Expansion overflow fixture must contain a raw numeric token"));
        if (bMadeFixture) CheckExpansionRejected(NonFiniteJson,
            TEXT("Non-finite expansion JSON number must preserve the entire output record"));
    }

    // Test every unordered pair of object types, then both bed occupancy cells.
    const TCHAR* ObjectKinds[] = { TEXT("placedTorches"), TEXT("crops"), TEXT("restPoints"), TEXT("furnaces") };
    const FIntVector KindCells[] = { Expansion.Player.PlacedTorches[0], Expansion.Player.Crops[0].Cell,
        Expansion.Player.RestPoints[0].Cell, Expansion.Player.Furnaces[0].Cell };
    auto SetObjectCell = [](const TSharedRef<FJsonObject>& Root, const TCHAR* Kind, const FIntVector& Cell)
    {
        if (FString(Kind) == TEXT("placedTorches"))
        {
            TArray<TSharedPtr<FJsonValue>> Torches = Root->GetArrayField(Kind);
            Torches[0] = MakeShared<FJsonValueArray>(NumberArray(Cell.X, Cell.Y, Cell.Z));
            Root->SetArrayField(Kind, Torches);
            Root->RemoveField(TEXT("placedTorchSupports"));
        }
        else Root->GetArrayField(Kind)[0]->AsObject()->SetArrayField(TEXT("cell"), NumberArray(Cell.X, Cell.Y, Cell.Z));
        Root->RemoveField(TEXT("respawnPoint"));
    };
    for (int32 First = 0; First < 3; ++First)
        for (int32 Second = First + 1; Second < 4; ++Second)
            CheckExpansionMutation([First, Second, &ObjectKinds, &KindCells, &SetObjectCell](const TSharedRef<FJsonObject>& Root)
            { SetObjectCell(Root, ObjectKinds[Second], KindCells[First]); },
                TEXT("Different placed object types may not claim the same cell"));
    const FIntVector BedFoot = RestPointSecondCell(Expansion.Player.RestPoints[0]);
    for (const TCHAR* Kind : { TEXT("placedTorches"), TEXT("crops"), TEXT("furnaces") })
        CheckExpansionMutation([Kind, BedFoot, &SetObjectCell](const TSharedRef<FJsonObject>& Root)
        { SetObjectCell(Root, Kind, BedFoot); }, TEXT("A bed second cell may not overlap another placed object"));
    for (bool bFootToFoot : { false, true })
        CheckExpansionMutation([bFootToFoot, BedFoot](const TSharedRef<FJsonObject>& Root)
        {
            const FIntVector Cell = bFootToFoot ? BedFoot - FIntVector(0, 1, 0) : BedFoot;
            Root->GetArrayField(TEXT("restPoints"))[1]->AsObject()->SetArrayField(TEXT("cell"), NumberArray(Cell.X, Cell.Y, Cell.Z));
        }, TEXT("Beds may not overlap anchor-to-second-cell or second-cell-to-second-cell"));

    CheckExpansionMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetField(TEXT("respawnPoint"), MakeShared<FJsonValueNull>()); }, TEXT("Null respawnPoint must preserve the entire output record"));
    CheckExpansionMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetStringField(TEXT("respawnPoint"), TEXT("invalid")); }, TEXT("Wrong type for respawnPoint must preserve the entire output record"));
    const FIntVector NonBedRespawns[] = { FIntVector(0, 0, 1), BedFoot, Expansion.Player.Crops[0].Cell,
        Expansion.Player.Furnaces[0].Cell, Expansion.Player.PlacedTorches[0] };
    for (const FIntVector& Cell : NonBedRespawns)
        CheckExpansionMutation([Cell](const TSharedRef<FJsonObject>& Root)
        { Root->SetArrayField(TEXT("respawnPoint"), NumberArray(Cell.X, Cell.Y, Cell.Z)); },
            TEXT("RespawnPoint must refer to a saved bed anchor, not an arbitrary cell or bed foot"));
    for (const FIntVector& Cell : BadExpansionCells)
        CheckExpansionMutation([Cell](const TSharedRef<FJsonObject>& Root)
        { Root->SetArrayField(TEXT("respawnPoint"), NumberArray(Cell.X, Cell.Y, Cell.Z)); },
            TEXT("Out-of-world respawn cell must preserve the entire output record"));
    CheckExpansionMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetArrayField(TEXT("respawnPoint"), NumberArray(.5, 20, 22)); }, TEXT("Fractional respawn cell must be rejected atomically"));
    CheckExpansionMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->SetArrayField(TEXT("respawnPoint"), { MakeShared<FJsonValueNumber>(20), MakeShared<FJsonValueNumber>(20) }); },
        TEXT("Respawn cell must contain exactly three integers"));
    CheckExpansionMutation([](const TSharedRef<FJsonObject>& Root)
    { Root->RemoveField(TEXT("restPoints")); }, TEXT("A respawn reference with no saved beds must be rejected atomically"));
    CheckExpansionMutation([](const TSharedRef<FJsonObject>& Root)
    {
        TArray<TSharedPtr<FJsonValue>> Edits = Root->GetArrayField(TEXT("edits"));
        TArray<TSharedPtr<FJsonValue>> FirstEdit = Edits[0]->AsArray();
        FirstEdit[2] = MakeShared<FJsonValueNumber>(0);
        Edits[0] = MakeShared<FJsonValueArray>(FirstEdit);
        Root->SetArrayField(TEXT("edits"), Edits);
    },
        TEXT("An invalid edit after valid expansion data must preserve the entire output record"));

    // Save-side validation has the same guarantees with or without survival.
    for (bool bSurvival : { false, true })
    {
        FWorldSaveState ExpansionState = Expansion.Player;
        ExpansionState.bHasSurvivalState = bSurvival;
        Invalid = ExpansionState;
        Invalid.Crops[0].GrowthSeconds = std::numeric_limits<float>::quiet_NaN();
        CheckInvalidState(Invalid, TEXT("Non-finite crop growth must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.Crops[0].GrowthSeconds = MaxCropGrowthSeconds + 1.f;
        CheckInvalidState(Invalid, TEXT("Excessive crop growth must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.Crops[0].Cell.Z = 0;
        CheckInvalidState(Invalid, TEXT("Invalid crop cell must not replace serialized output"));
        Invalid = ExpansionState;
        const FBlockRealmSavedCrop DuplicateCrop = Invalid.Crops[0];
        Invalid.Crops.Add(DuplicateCrop);
        CheckInvalidState(Invalid, TEXT("Duplicate crop cell must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.RestPoints[0].Heading = 4;
        CheckInvalidState(Invalid, TEXT("Invalid bed heading must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.RestPoints[0] = OutwardBeds[0];
        Invalid.bHasRespawnPoint = false;
        CheckInvalidState(Invalid, TEXT("Out-of-world second bed cell must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.Furnaces[0].CookProgress = std::numeric_limits<float>::infinity();
        CheckInvalidState(Invalid, TEXT("Non-finite furnace progress must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.Furnaces[0].CookProgress = FurnaceCookSeconds;
        CheckInvalidState(Invalid, TEXT("Completed furnace progress must not replace serialized output"));
        for (int32 BadStack : { -1, MaxSavedFurnaceStack + 1 })
        {
            Invalid = ExpansionState;
            Invalid.Furnaces[0].InputSand = BadStack;
            CheckInvalidState(Invalid, TEXT("Out-of-range furnace stack must not replace serialized output"));
        }
        Invalid = ExpansionState;
        Invalid.Furnaces[0].FuelWood = 0;
        CheckInvalidState(Invalid, TEXT("In-flight furnace without fuel must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.Crops[0].Cell = Invalid.PlacedTorches[0];
        CheckInvalidState(Invalid, TEXT("Torch/crop occupancy conflict must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.Furnaces[0].Cell = BedFoot;
        CheckInvalidState(Invalid, TEXT("Bed-foot/furnace occupancy conflict must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.RespawnPoint = BedFoot;
        CheckInvalidState(Invalid, TEXT("Non-anchor respawn cell must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.RestPoints.Empty();
        CheckInvalidState(Invalid, TEXT("Respawn cell without a saved bed must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.PlacedTorches.Add(FIntVector(0, 0, 0));
        CheckInvalidState(Invalid, TEXT("Invalid torch data without survival must not replace serialized output"));
        Invalid = ExpansionState;
        Invalid.PlacedTorchSupports[0] = Invalid.PlacedTorches[0];
        CheckInvalidState(Invalid, TEXT("Invalid torch support without survival must not replace serialized output"));
    }
    Invalid = MaximumExpansion.Player;
    Invalid.Crops.Add({ FIntVector(MaxSavedCrops, 100, 1), 0.f });
    CheckInvalidState(Invalid, TEXT("Crop count over cap must not replace serialized output"));
    Invalid = MaximumExpansion.Player;
    Invalid.RestPoints.Add({ FIntVector(MaxSavedRestPoints * 3, 200, 1), 0 });
    CheckInvalidState(Invalid, TEXT("Bed count over cap must not replace serialized output"));
    Invalid = MaximumExpansion.Player;
    Invalid.Furnaces.Add({ FIntVector(MaxSavedFurnaces, 300, 1), 0, 0, 0, 0.f });
    CheckInvalidState(Invalid, TEXT("Furnace count over cap must not replace serialized output"));
    return Failures.Num() == InitialFailureCount;
}
