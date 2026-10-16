#include "BlockRealmFurnace.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmGameMode.h"
#include "BlockRealmPlacement.h"
#include "BlockWorld.h"
#include "Components/PointLightComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelData.h"

#include <cmath>

namespace
{
    bool HasValidCell(const FIntVector& Cell)
    {
        return FVoxelData::IsValidPosition(Cell) && Cell.Z > 0;
    }

    bool IsFurnaceSupport(EBlockType Type)
    {
        return FVoxelData::IsSolid(Type) && Type != EBlockType::Leaves
            && Type != EBlockType::Glass;
    }

    float StrictProgressLimit(float Duration)
    {
        // Casting an accrued double to float can otherwise round an unfinished portion to 8.
        return std::nextafter(Duration, 0.f);
    }

    bool IsValidStoredState(int32 Sand, int32 Wood, int32 Glass, float Progress,
        float Duration, bool bAllowCompleteBoundary)
    {
        if (Sand < 0 || Sand > ABlockRealmFurnace::ItemCapacity
            || Wood < 0 || Wood > ABlockRealmFurnace::ItemCapacity
            || Glass < 0 || Glass > ABlockRealmFurnace::ItemCapacity
            || !FMath::IsFinite(Duration)
            || Duration < ABlockRealmFurnace::MinCookDurationSeconds
            || Duration > ABlockRealmFurnace::DefaultCookDurationSeconds
            || !FMath::IsFinite(Progress) || Progress < 0.f
            || (bAllowCompleteBoundary ? Progress > Duration : Progress >= Duration)) return false;

        // A partial recipe always has both unconsumed ingredients and a destination slot.
        return Progress == 0.f || (Sand > 0 && Wood > 0
            && Glass < ABlockRealmFurnace::ItemCapacity);
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
        // Keep the ray parameter in world centimeters after nonuniform mesh scaling.
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

ABlockRealmFurnace::ABlockRealmFurnace()
{
    PrimaryActorTick.bCanEverTick = true;
    Tags.Add(TEXT("BlockRealmFurnace"));

    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("FurnaceRoot"));
    SetRootComponent(SceneRoot);
    SceneRoot->SetMobility(EComponentMobility::Movable);

    struct FStonePart
    {
        const TCHAR* Name;
        FVector Location;
        FVector Scale;
    };
    // All visible geometry stays inside one cell. Bottom z=-50 meets its support;
    // the opening is y=-18..18, z=-24..4 on the +X side, with no solid front cube.
    const FStonePart PartSpecs[] = {
        { TEXT("FurnaceBase"), FVector(0.f, 0.f, -43.f), FVector(.88f, .88f, .14f) },
        { TEXT("FurnaceTop"), FVector(0.f, 0.f, 31.f), FVector(.88f, .88f, .14f) },
        { TEXT("FurnaceBack"), FVector(-37.f, 0.f, -6.f), FVector(.14f, .88f, .60f) },
        { TEXT("FurnaceLeft"), FVector(0.f, -37.f, -6.f), FVector(.88f, .14f, .60f) },
        { TEXT("FurnaceRight"), FVector(0.f, 37.f, -6.f), FVector(.88f, .14f, .60f) },
        { TEXT("FurnaceLintel"), FVector(37.f, 0.f, 14.f), FVector(.14f, .60f, .20f) },
        { TEXT("FurnaceSill"), FVector(37.f, 0.f, -30.f), FVector(.14f, .60f, .12f) },
        { TEXT("FurnaceDoorLeft"), FVector(37.f, -24.f, -10.f), FVector(.14f, .12f, .28f) },
        { TEXT("FurnaceDoorRight"), FVector(37.f, 24.f, -10.f), FVector(.14f, .12f, .28f) }
    };
    for (const FStonePart& Spec : PartSpecs)
    {
        UStaticMeshComponent* Mesh = CreateDefaultSubobject<UStaticMeshComponent>(Spec.Name);
        Mesh->SetupAttachment(SceneRoot);
        Mesh->SetRelativeLocation(Spec.Location);
        Mesh->SetRelativeScale3D(Spec.Scale);
        StoneParts.Add(Mesh);
    }

    FuelStack = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FurnaceFuelStack"));
    FuelStack->SetupAttachment(SceneRoot);
    FuelStack->SetRelativeLocation(FVector(10.f, 0.f, -30.f));
    FuelStack->SetRelativeScale3D(FVector(.38f, .34f, .08f));

    FireFace = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FurnaceFireFace"));
    FireFace->SetupAttachment(SceneRoot);
    FireFace->SetRelativeLocation(FVector(29.f, 0.f, -10.f));
    FireFace->SetRelativeScale3D(FVector(.02f, .30f, .20f));
    FireFace->SetVisibility(false);

    static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> StoneMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Stone.M_Stone"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> WoodMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Wood.M_Wood"));
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlowMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Glow.M_Glow"));

    auto ConfigureMesh = [](UStaticMeshComponent* Mesh)
    {
        Mesh->SetMobility(EComponentMobility::Movable);
        Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Mesh->SetGenerateOverlapEvents(false);
        Mesh->SetCanEverAffectNavigation(false);
        Mesh->SetCastShadow(false);
        Mesh->SetVisibleInRayTracing(true);
        if (CubeMesh.Succeeded()) Mesh->SetStaticMesh(CubeMesh.Object);
    };
    for (const TObjectPtr<UStaticMeshComponent>& Mesh : StoneParts)
    {
        ConfigureMesh(Mesh.Get());
        Mesh->SetCastShadow(true);
        if (StoneMaterial.Succeeded()) Mesh->SetMaterial(0, StoneMaterial.Object);
    }
    ConfigureMesh(FuelStack.Get());
    ConfigureMesh(FireFace.Get());
    if (WoodMaterial.Succeeded()) FuelStack->SetMaterial(0, WoodMaterial.Object);
    if (GlowMaterial.Succeeded()) FireFace->SetMaterial(0, GlowMaterial.Object);
    SetActorEnableCollision(false);

    FurnaceLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FurnaceLight"));
    FurnaceLight->SetupAttachment(SceneRoot);
    FurnaceLight->SetRelativeLocation(FVector(40.f, 0.f, -10.f));
    FurnaceLight->SetMobility(EComponentMobility::Movable);
    FurnaceLight->SetUseInverseSquaredFalloff(false);
    FurnaceLight->SetIntensityUnits(ELightUnits::Unitless);
    FurnaceLight->SetLightFalloffExponent(2.3f);
    FurnaceLight->SetInverseExposureBlend(1.f);
    FurnaceLight->SetIntensity(.7f);
    FurnaceLight->SetAttenuationRadius(650.f);
    FurnaceLight->SetSourceRadius(8.f);
    FurnaceLight->SetSoftSourceRadius(10.f);
    FurnaceLight->SetLightColor(FLinearColor(1.f, .52f, .19f));
    FurnaceLight->SetCastShadows(true);
    FurnaceLight->SetAffectTranslucentLighting(false);
    FurnaceLight->SetVolumetricScatteringIntensity(0.f);
    FurnaceLight->SetVisibility(false);
}

ABlockRealmFurnace* ABlockRealmFurnace::TryPlace(UWorld* World, ABlockWorld* InBlockWorld,
    FIntVector Cell, int32 InInputSand, int32 InFuelWood, int32 InOutputGlass, float InCookProgress)
{
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || !HasValidCell(Cell) || InBlockWorld->GetBlock(Cell) != EBlockType::Air
        || !IsFurnaceSupport(InBlockWorld->GetBlock(Cell - FIntVector(0, 0, 1)))
        || !IsValidStoredState(InInputSand, InFuelWood, InOutputGlass, InCookProgress,
            DefaultCookDurationSeconds, true)
        || BlockRealmPlacement::IsCellOccupied(World, InBlockWorld, Cell)) return nullptr;

    int32 PlacedCount = 0;
    for (TActorIterator<ABlockRealmFurnace> It(World); It; ++It)
    {
        const ABlockRealmFurnace* Existing = *It;
        if (!IsValid(Existing) || Existing->IsActorBeingDestroyed()) continue;
        if (Existing->GetBlockPosition() == Cell || ++PlacedCount >= MaxPlacedFurnaces) return nullptr;
    }

    const FTransform Transform(FRotator::ZeroRotator, ABlockWorld::BlockToWorld(Cell));
    ABlockRealmFurnace* Furnace = World->SpawnActorDeferred<ABlockRealmFurnace>(StaticClass(), Transform,
        InBlockWorld, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Furnace) return nullptr;

    Furnace->BlockWorld = InBlockWorld;
    Furnace->BlockPosition = Cell;
    Furnace->InputSand = InInputSand;
    Furnace->FuelWood = InFuelWood;
    Furnace->OutputGlass = InOutputGlass;
    Furnace->CookProgress = FMath::Min(InCookProgress, StrictProgressLimit(DefaultCookDurationSeconds));
    Furnace->bPlacementInitialized = true;
    Furnace->FinishSpawning(Transform);
    return IsValid(Furnace) && !Furnace->IsActorBeingDestroyed() ? Furnace : nullptr;
}

ABlockRealmFurnace* ABlockRealmFurnace::FindAimedFurnace(UWorld* World, ABlockWorld* InBlockWorld,
    FVector Origin, FVector Direction, float MaxDistance)
{
    if (!IsInGameThread() || !IsValid(World) || World->bIsTearingDown || !IsValid(InBlockWorld)
        || InBlockWorld->IsActorBeingDestroyed() || InBlockWorld->GetWorld() != World
        || Origin.ContainsNaN() || Direction.ContainsNaN()
        || !FMath::IsFinite(MaxDistance) || MaxDistance <= 0.f) return nullptr;

    const double DirectionScale = Direction.GetAbsMax();
    if (!FMath::IsFinite(DirectionScale) || DirectionScale == 0.0) return nullptr;
    const FVector UnitDirection = (Direction / DirectionScale).GetSafeNormal();

    ABlockRealmFurnace* NearestFurnace = nullptr;
    double NearestDistance = MaxDistance;
    for (TActorIterator<ABlockRealmFurnace> It(World); It; ++It)
    {
        ABlockRealmFurnace* Furnace = *It;
        if (!IsValid(Furnace) || Furnace->IsActorBeingDestroyed() || Furnace->IsHidden()
            || Furnace->GetBlockWorld() != InBlockWorld || !Furnace->IsAnchored()
            || !Furnace->IsStateValid()) continue;

        bool bHit = false;
        double HitDistance = NearestDistance;
        auto ConsiderMesh = [&](const UStaticMeshComponent* Mesh)
        {
            double MeshDistance = 0.0;
            if (IntersectVisualMesh(Mesh, Origin, UnitDirection, HitDistance, MeshDistance))
            {
                HitDistance = MeshDistance;
                bHit = true;
            }
        };
        for (const TObjectPtr<UStaticMeshComponent>& Mesh : Furnace->StoneParts) ConsiderMesh(Mesh.Get());
        ConsiderMesh(Furnace->FuelStack.Get());
        ConsiderMesh(Furnace->FireFace.Get());
        if (!bHit || (NearestFurnace && HitDistance >= NearestDistance)) continue;

        FIntVector WallBlock, WallAdjacent;
        if (InBlockWorld->TraceBlocks(Origin, UnitDirection,
            FMath::Min(MaxDistance, static_cast<float>(HitDistance)), WallBlock, WallAdjacent)) continue;

        NearestDistance = HitDistance;
        NearestFurnace = Furnace;
    }
    return NearestFurnace;
}

void ABlockRealmFurnace::BeginPlay()
{
    Super::BeginPlay();
    if (!IsAnchored() || !IsStateValid())
    {
        Destroy();
        return;
    }
    SetBurningVisuals(HasRecipeReady() && IsCookingAllowed());
}

void ABlockRealmFurnace::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (!IsAnchored() || !IsStateValid())
    {
        // First version intentionally destroys the inventory with an invalid placement.
        SetBurningVisuals(false);
        Destroy();
        return;
    }
    if (!IsCookingAllowed() || !HasRecipeReady())
    {
        SetBurningVisuals(false);
        return;
    }
    if (FurnaceLight)
    {
        const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
        const float Phase = static_cast<float>(GetUniqueID() & 255u) * 0.083f;
        const float Flicker = 0.90f + 0.06f * FMath::Sin(Time * 6.2f + Phase)
            + 0.04f * FMath::Sin(Time * 11.9f + Phase * 1.9f);
        FurnaceLight->SetIntensity(.7f * FMath::Clamp(Flicker, 0.78f, 1.08f));
    }

    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f)
    {
        SetBurningVisuals(HasRecipeReady());
        return;
    }

    // No offline backlog: huge frame gaps are capped and excess elapsed time is discarded.
    double TimeRemaining = FMath::Min(static_cast<double>(DeltaSeconds),
        static_cast<double>(CookDurationSeconds) * MaxCookCompletionsPerTick);
    int32 Completed = 0;
    while (TimeRemaining > 0.0 && Completed < MaxCookCompletionsPerTick && HasRecipeReady())
    {
        const double UntilComplete = static_cast<double>(CookDurationSeconds) - CookProgress;
        if (TimeRemaining < UntilComplete)
        {
            CookProgress = FMath::Min(static_cast<float>(CookProgress + TimeRemaining),
                StrictProgressLimit(CookDurationSeconds));
            break;
        }

        TimeRemaining -= UntilComplete;
        // Each recipe commits its two ingredients and one output together, only on completion.
        --InputSand;
        --FuelWood;
        ++OutputGlass;
        CookProgress = 0.f;
        ++Completed;
    }
    SetBurningVisuals(HasRecipeReady());
}

bool ABlockRealmFurnace::CanAddBatch(int32 Sand, int32 Wood) const
{
    return IsInGameThread() && !IsActorBeingDestroyed() && IsAnchored() && IsStateValid()
        && Sand >= 0 && Wood >= 0 && (Sand > 0 || Wood > 0)
        && Sand <= ItemCapacity - InputSand && Wood <= ItemCapacity - FuelWood;
}

bool ABlockRealmFurnace::AddBatch(int32 Sand, int32 Wood)
{
    if (!CanAddBatch(Sand, Wood)) return false;
    InputSand += Sand;
    FuelWood += Wood;
    SetBurningVisuals(HasRecipeReady() && IsCookingAllowed());
    return true;
}

int32 ABlockRealmFurnace::TakeGlass(int32 Count)
{
    if (!IsInGameThread() || IsActorBeingDestroyed() || !IsAnchored() || !IsStateValid()
        || Count <= 0 || Count > OutputGlass) return 0;
    OutputGlass -= Count;
    SetBurningVisuals(HasRecipeReady() && IsCookingAllowed());
    return Count;
}

bool ABlockRealmFurnace::SetCookDurationSeconds(float Seconds)
{
    if (!IsInGameThread() || IsActorBeingDestroyed() || !IsStateValid()
        || !FMath::IsFinite(Seconds) || Seconds < MinCookDurationSeconds
        || Seconds > DefaultCookDurationSeconds) return false;

    const double CookedFraction = static_cast<double>(CookProgress) / CookDurationSeconds;
    CookDurationSeconds = Seconds;
    CookProgress = FMath::Min(static_cast<float>(CookedFraction * Seconds), StrictProgressLimit(Seconds));
    return true;
}

bool ABlockRealmFurnace::IsAnchored() const
{
    UWorld* World = GetWorld();
    return bPlacementInitialized && IsValid(World) && !World->bIsTearingDown
        && IsValid(BlockWorld) && !BlockWorld->IsActorBeingDestroyed()
        && BlockWorld->GetWorld() == World && HasValidCell(BlockPosition)
        && BlockWorld->GetBlock(BlockPosition) == EBlockType::Air
        && IsFurnaceSupport(BlockWorld->GetBlock(BlockPosition - FIntVector(0, 0, 1)))
        && !BlockRealmPlacement::IsCellOccupied(World, BlockWorld, BlockPosition, this);
}

bool ABlockRealmFurnace::IsStateValid() const
{
    return IsValidStoredState(InputSand, FuelWood, OutputGlass, CookProgress,
        CookDurationSeconds, false);
}

bool ABlockRealmFurnace::HasRecipeReady() const
{
    return InputSand > 0 && FuelWood > 0 && OutputGlass < ItemCapacity;
}

bool ABlockRealmFurnace::IsCookingAllowed() const
{
    UWorld* World = GetWorld();
    const ABlockRealmGameMode* Game = IsValid(World) ? World->GetAuthGameMode<ABlockRealmGameMode>() : nullptr;
    if (!IsValid(Game) || Game->IsActorBeingDestroyed() || !Game->IsAdventureActive()) return false;
    const ABlockRealmCharacter* Player = Game->GetPlayerCharacter();
    return IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->GetBlockWorld() == BlockWorld
        && !Player->IsInventoryOpen();
}

void ABlockRealmFurnace::SetBurningVisuals(bool bNewBurning)
{
    bBurning = bNewBurning;
    if (FireFace) FireFace->SetVisibility(bNewBurning);
    if (FurnaceLight) FurnaceLight->SetVisibility(bNewBurning);
}
