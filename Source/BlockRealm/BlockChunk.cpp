#include "BlockChunk.h"

#include "Materials/MaterialInterface.h"
#include "Components/PointLightComponent.h"
#include "PhysicsEngine/BodySetup.h"
#include "ProceduralMeshComponent.h"

ABlockChunk::ABlockChunk()
{
    PrimaryActorTick.bCanEverTick = false;
    Tags.Add(TEXT("BlockTerrain"));
    TerrainMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
    SetRootComponent(TerrainMesh);
    GlassMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("GlassMesh"));
    GlassMesh->SetupAttachment(TerrainMesh);
    WaterMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("WaterMesh"));
    WaterMesh->SetupAttachment(TerrainMesh);
    CollisionMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("CollisionMesh"));
    CollisionMesh->SetupAttachment(TerrainMesh);

    UProceduralMeshComponent* VisualMeshes[] = { TerrainMesh.Get(), GlassMesh.Get(), WaterMesh.Get() };
    for (UProceduralMeshComponent* Mesh : VisualMeshes)
    {
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        // UE's PMC scene proxy supplies dynamic ray tracing geometry when hardware RT is enabled.
        Mesh->SetVisibleInRayTracing(true);
        Mesh->bUseAsyncCooking = true;
    }
    WaterMesh->SetCastShadow(false);
    GlassMesh->SetCastShadow(false);
    CollisionMesh->SetMobility(EComponentMobility::Movable);
    CollisionMesh->SetCollisionObjectType(ECC_WorldStatic);
    CollisionMesh->SetCollisionResponseToAllChannels(ECR_Block);
    CollisionMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    CollisionMesh->SetGenerateOverlapEvents(false);
    CollisionMesh->SetCanEverAffectNavigation(false);
    CollisionMesh->bUseComplexAsSimpleCollision = true;
    CollisionMesh->bUseAsyncCooking = true;
    CollisionMesh->SetVisibility(false);
    CollisionMesh->SetHiddenInGame(true);
    CollisionMesh->SetCastShadow(false);
    CollisionMesh->SetVisibleInRayTracing(false);
}

void ABlockChunk::InitializeChunk(const FIntPoint& Coordinate)
{
    check(IsInGameThread());
    ChunkCoordinate = Coordinate;
    SetActorLocation(FVector(static_cast<double>(Coordinate.X) * FVoxelData::ChunkSize * FVoxelData::BlockSize,
        static_cast<double>(Coordinate.Y) * FVoxelData::ChunkSize * FVoxelData::BlockSize, 0.0));
}

void ABlockChunk::ApplyMesh(const FVoxelChunkMesh& Mesh, const TArray<TObjectPtr<UMaterialInterface>>& Materials,
                           bool bImmediateCollision)
{
    check(IsInGameThread());
    if (Mesh.Chunk != ChunkCoordinate) return;
    TerrainMesh->ClearAllMeshSections();
    GlassMesh->ClearAllMeshSections();
    WaterMesh->ClearAllMeshSections();
    for (int32 Type = 1; Type < Mesh.Sections.Num(); ++Type)
    {
        const FVoxelMeshSection& Section = Mesh.Sections[Type];
        if (Section.Vertices.IsEmpty()) continue;
        UProceduralMeshComponent* Component = Type == static_cast<uint8>(EBlockType::Water) ? WaterMesh.Get()
            : (Type == static_cast<uint8>(EBlockType::Glass) ? GlassMesh.Get() : TerrainMesh.Get());
        TArray<FProcMeshTangent> Tangents;
        Tangents.Reserve(Section.Tangents.Num());
        for (const FVoxelMeshTangent& Tangent : Section.Tangents) Tangents.Emplace(Tangent.X, Tangent.bFlipY);
        Component->CreateMeshSection_LinearColor(Type, Section.Vertices, Section.Triangles, Section.Normals,
            Section.UVs, Section.Colors, Tangents, false, false);
        if (Materials.IsValidIndex(Type)) Component->SetMaterial(Type, Materials[Type]);
    }

    // An edit may already have installed this revision's collision before the worker finishes.
    // Never recook it or replace a newer authoritative collision with an old visual snapshot.
    if (!bHasMesh || Mesh.Revision > CollisionRevision) ApplyCollision(Mesh, bImmediateCollision);
    MeshRevision = Mesh.Revision;
    VisibleFaces = Mesh.VisibleFaces;
    bHasMesh = true;
    RebuildGlowLights(Mesh.GlowLightPositions);
}

void ABlockChunk::ClearGlowLights()
{
    for (TObjectPtr<UPointLightComponent>& Light : GlowLights)
        if (IsValid(Light)) Light->DestroyComponent();
    GlowLights.Empty();
}

void ABlockChunk::RebuildGlowLights(const TArray<FVector>& LocalPositions)
{
    check(IsInGameThread());
    ClearGlowLights();
    for (const FVector& LocalPosition : LocalPositions)
    {
        UPointLightComponent* Light = NewObject<UPointLightComponent>(this);
        if (!Light) continue;
        Light->SetMobility(EComponentMobility::Movable);
        Light->SetLightColor(FLinearColor(1.0f, 0.55f, 0.18f));
        // The bounded local source supplements the emissive mineral. Its anchor
        // is in the exposed air cell so terrain shadows can stop light at walls.
        Light->SetUseInverseSquaredFalloff(false);
        Light->SetIntensityUnits(ELightUnits::Unitless);
        Light->SetLightFalloffExponent(3.0f);
        Light->SetInverseExposureBlend(1.0f);
        Light->SetIntensity(1.2f);
        Light->SetAttenuationRadius(950.0f);
        // Keep distant generated veins emissive without scheduling hundreds
        // of shadow maps for lights whose small pool cannot aid navigation.
        Light->SetMaxDrawDistance(4000.0f);
        Light->SetMaxDistanceFadeRange(800.0f);
        Light->SetSourceRadius(28.0f);
        Light->SetSoftSourceRadius(44.0f);
        Light->SetIndirectLightingIntensity(1.3f);
        Light->SetVolumetricScatteringIntensity(0.0f);
        Light->SetCastShadows(true);
        Light->SetAffectTranslucentLighting(false);
        Light->RegisterComponent();
        Light->AttachToComponent(TerrainMesh, FAttachmentTransformRules::KeepRelativeTransform);
        Light->SetRelativeLocation(LocalPosition);
        GlowLights.Add(Light);
    }
}

void ABlockChunk::ApplyCollision(const FVoxelChunkMesh& Mesh, bool bImmediateCollision)
{
    check(IsInGameThread());
    if (Mesh.Chunk != ChunkCoordinate || Mesh.Revision < CollisionRevision) return;
    // A single collision section avoids cooking the same chunk once for every material.
    // Synchronous replacement also empties PMC's async cook queue, preventing an older cook
    // callback from resurrecting collision removed by a newer edit.
    CollisionMesh->bUseAsyncCooking = !bImmediateCollision;
    bHasCollisionGeometry = !Mesh.CollisionTriangles.IsEmpty();
    if (bHasCollisionGeometry)
    {
        const TArray<FVector> EmptyNormals;
        const TArray<FVector2D> EmptyUVs;
        const TArray<FProcMeshTangent> EmptyTangents;
        const TArray<FLinearColor> EmptyColors;
        CollisionMesh->CreateMeshSection_LinearColor(0, Mesh.CollisionVertices, Mesh.CollisionTriangles,
            EmptyNormals, EmptyUVs, EmptyColors, EmptyTangents, true);
    }
    else CollisionMesh->ClearAllMeshSections();
    CollisionRevision = Mesh.Revision;
}

bool ABlockChunk::IsCollisionReady() const
{
    if (!bHasMesh) return false;
    if (!bHasCollisionGeometry) return true;
    const UBodySetup* BodySetup = CollisionMesh->GetBodySetup();
    return BodySetup && BodySetup->bCreatedPhysicsMeshes && !BodySetup->bFailedToCreatePhysicsMeshes
        && CollisionMesh->IsPhysicsStateCreated();
}
