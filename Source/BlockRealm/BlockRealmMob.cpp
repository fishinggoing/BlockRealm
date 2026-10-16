#include "BlockRealmMob.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmGameMode.h"
#include "BlockWorld.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "VoxelData.h"

namespace
{
    constexpr float FootClearance = 2.f;

    bool IsCreatureSupport(EBlockType Type, bool bSoilOnly)
    {
        if (bSoilOnly) return Type == EBlockType::Grass || Type == EBlockType::Dirt || Type == EBlockType::Sand;
        return Type == EBlockType::Grass || Type == EBlockType::Dirt || Type == EBlockType::Stone
            || Type == EBlockType::Sand || Type == EBlockType::Glow;
    }
}

ABlockRealmMob::ABlockRealmMob(const FObjectInitializer& ObjectInitializer)
    : Super(ObjectInitializer)
{
    PrimaryActorTick.bCanEverTick = true;
    Tags.Add(TEXT("BlockRealmMob"));
    GetCapsuleComponent()->InitCapsuleSize(42.f, 86.f);
    GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
    GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
    GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
    GetCapsuleComponent()->SetCanEverAffectNavigation(false);

    BodyMesh = CreateCreaturePart(TEXT("MobBody"), FVector(0.f, 0.f, -3.f), FVector(72.f, 59.f, 75.f));
    // An asymmetrical stone crest, a broad brow and four separate limbs give the
    // enemy a readable silhouette at a distance. All shapes are authored here.
    HostileParts.Add(CreateCreaturePart(TEXT("StoneHead"), FVector(0.f, 0.f, 58.f), FVector(58.f, 64.f, 44.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneBrow"), FVector(31.f, 0.f, 70.f), FVector(12.f, 72.f, 14.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneEyeLeft"), FVector(31.f, -18.f, 55.f), FVector(4.f, 13.f, 9.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneEyeRight"), FVector(31.f, 18.f, 55.f), FVector(4.f, 13.f, 9.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneCrestLeft"), FVector(-7.f, -23.f, 90.f), FVector(18.f, 17.f, 30.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneCrestRight"), FVector(-8.f, 23.f, 83.f), FVector(16.f, 16.f, 17.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneArmLeft"), FVector(0.f, -43.f, -5.f), FVector(27.f, 24.f, 66.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneArmRight"), FVector(0.f, 43.f, -5.f), FVector(27.f, 24.f, 66.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneLegLeft"), FVector(4.f, -20.f, -65.f), FVector(36.f, 28.f, 42.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneLegRight"), FVector(4.f, 20.f, -65.f), FVector(36.f, 28.f, 42.f)));
    HostileParts.Add(CreateCreaturePart(TEXT("StoneChestMark"), FVector(37.f, 0.f, 7.f), FVector(3.f, 15.f, 27.f)));

    static ConstructorHelpers::FObjectFinderOptional<UMaterialInterface> CreatureMaterial(
        TEXT("/Game/BlockRealm/Materials/M_Creature.M_Creature"), LOAD_Quiet | LOAD_NoWarn);
    static ConstructorHelpers::FObjectFinder<UMaterialInterface> FallbackMaterial(
        TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    PaletteMaterial = CreatureMaterial.Get() ? CreatureMaterial.Get() : FallbackMaterial.Object.Get();

    bUseControllerRotationYaw = bUseControllerRotationPitch = bUseControllerRotationRoll = false;
    GetCharacterMovement()->MaxWalkSpeed = MoveSpeed;
    GetCharacterMovement()->bOrientRotationToMovement = false;
    GetCharacterMovement()->GravityScale = 1.5f;
    GetCharacterMovement()->bRunPhysicsWithNoController = true;
}

UStaticMeshComponent* ABlockRealmMob::CreateCreaturePart(FName Name, const FVector& LocalLocation, const FVector& SizeCm)
{
    UStaticMeshComponent* Part = CreateDefaultSubobject<UStaticMeshComponent>(Name);
    Part->SetupAttachment(GetCapsuleComponent());
    Part->SetMobility(EComponentMobility::Movable);
    Part->SetRelativeLocation(LocalLocation);
    Part->SetRelativeScale3D(SizeCm / 100.f);
    Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Part->SetGenerateOverlapEvents(false);
    Part->SetCanEverAffectNavigation(false);
    static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
    if (Cube.Succeeded()) Part->SetStaticMesh(Cube.Object);
    return Part;
}

void ABlockRealmMob::BeginPlay()
{
    Super::BeginPlay();
    Health = FMath::Max(MaxHealth, 1.f);
    HomePosition = GetActorLocation();
    GetCharacterMovement()->MaxWalkSpeed = FMath::Max(MoveSpeed, 0.f);
    // Content can be generated after the module's CDO was first constructed.
    if (UMaterialInterface* AuthoredPalette = LoadObject<UMaterialInterface>(nullptr,
        TEXT("/Game/BlockRealm/Materials/M_Creature.M_Creature"), nullptr, LOAD_Quiet | LOAD_NoWarn))
        PaletteMaterial = AuthoredPalette;
    ConfigureAppearance();
    if (!TargetPawn && !bHasExplicitTarget) AcquirePlayerTarget();
    if (IsValid(BlockWorld)) SetWorld(BlockWorld);
}

void ABlockRealmMob::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bDead) return;
    if (!CanAct())
    {
        SetSafeStateFrozen(true);
        return;
    }
    SetSafeStateFrozen(false);
    const float SafeDelta = FMath::IsFinite(DeltaSeconds) ? FMath::Clamp(DeltaSeconds, 0.f, .1f) : 0.f;
    MovedDistanceThisFrame = 0.f;
    if (GetActorLocation().ContainsNaN()) { Destroy(); return; }
    if (!IsValid(TargetPawn) && !bHasExplicitTarget) AcquirePlayerTarget();
    if (IsValid(TargetPawn))
    {
        const FVector Offset = TargetPawn->GetActorLocation() - GetActorLocation();
        if (Offset.ContainsNaN() || Offset.SizeSquared() > FMath::Square(FMath::Max(DespawnDistance, 1000.f)))
        {
            Destroy(); // Stream cleanup grants no loot.
            return;
        }
    }
    AttackCooldownRemaining = FMath::Max(0.f, AttackCooldownRemaining - SafeDelta);
    AttackVisualRemaining = FMath::Max(0.f, AttackVisualRemaining - SafeDelta);
    HitFlashRemaining = FMath::Max(0.f, HitFlashRemaining - SafeDelta);
    UpdateGrounding(SafeDelta);
    if (!IsActorBeingDestroyed())
    {
        TickBehavior(SafeDelta);
        TickCreatureVisuals(SafeDelta);
    }
}

float ABlockRealmMob::TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
    AController* EventInstigator, AActor* DamageCauser)
{
    if (!CanAct() || bDead || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0.f) return 0.f;
    const float AppliedDamage = FMath::Min(Health, DamageAmount);
    LastDamageCauser = DamageCauser;
    ApplyDamage(AppliedDamage);
    return AppliedDamage;
}

bool ABlockRealmMob::CanAct() const
{
    if (!GetWorld() || GetWorld()->IsPaused()) return false;
    const ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    if (Game && !Game->IsAdventureActive()) return false;
    const ABlockRealmCharacter* Player = Game ? Game->GetPlayerCharacter()
        : Cast<ABlockRealmCharacter>(TargetPawn.Get());
    return !Player || (!Player->IsInventoryOpen() && Player->GetHealth() > 0.f);
}

void ABlockRealmMob::SetSafeStateFrozen(bool bFrozen)
{
    UCharacterMovementComponent* Movement = GetCharacterMovement();
    if (!Movement) return;
    if (bFrozen)
    {
        if (!bSafeStateFrozen)
        {
            FrozenMovementMode = static_cast<uint8>(Movement->MovementMode);
            FrozenCustomMovementMode = Movement->CustomMovementMode;
            bSafeStateFrozen = true;
            Movement->DisableMovement();
        }
        ConsumeMovementInputVector();
        Movement->StopMovementImmediately();
    }
    else if (bSafeStateFrozen)
    {
        bSafeStateFrozen = false;
        if (!IsValid(BlockWorld))
        {
            Movement->SetMovementMode(FrozenMovementMode == static_cast<uint8>(MOVE_None)
                ? MOVE_Falling : static_cast<EMovementMode>(FrozenMovementMode), FrozenCustomMovementMode);
        }
    }
}

void ABlockRealmMob::InitializeTarget(APawn* InTarget)
{
    bHasExplicitTarget = InTarget != nullptr;
    TargetPawn = InTarget;
}

void ABlockRealmMob::SetTarget(APawn* InTarget) { InitializeTarget(InTarget); }

void ABlockRealmMob::SetWorld(ABlockWorld* InWorld)
{
    BlockWorld = IsValid(InWorld) && InWorld->GetWorld() == GetWorld() ? InWorld : nullptr;
    GroundCheckRemaining = 0.f;
    if (UCharacterMovementComponent* Movement = GetCharacterMovement())
    {
        Movement->StopMovementImmediately();
        // Chunk collision may still be cooking or recently edited. Voxel support
        // and clearance are the sole movement authority for bound creatures.
        Movement->SetComponentTickEnabled(!IsValid(BlockWorld));
        if (IsValid(BlockWorld)) Movement->DisableMovement();
        else Movement->SetMovementMode(MOVE_Falling);
    }
}

void ABlockRealmMob::ConfigureEncounter(float AwarenessRadius, float InitialAttackDelay)
{
    DetectionRadius = FMath::Clamp(FMath::IsFinite(AwarenessRadius) ? AwarenessRadius : 1100.f, 250.f, 2400.f);
    AttackCooldownRemaining = FMath::Clamp(FMath::IsFinite(InitialAttackDelay) ? InitialAttackDelay : 8.f, 0.f, 30.f);
}

FString ABlockRealmMob::GetCreatureDisplayName() const { return TEXT("岩脊守兽"); }

void ABlockRealmMob::ApplyDamage(float DamageAmount)
{
    if (!CanAct() || bDead || !FMath::IsFinite(DamageAmount) || DamageAmount <= 0.f) return;
    Health = FMath::Max(0.f, Health - DamageAmount);
    HitFlashRemaining = .18f;
    if (Health <= 0.f) Die();
    else OnDamaged(DamageAmount, LastDamageCauser.Get());
}

void ABlockRealmMob::OnDamaged(float DamageAmount, AActor* DamageCauser)
{
    // Provocation extends awareness, but never bypasses melee range or cooldown.
    DetectionRadius = FMath::Max(DetectionRadius, 1800.f);
}

void ABlockRealmMob::AcquirePlayerTarget()
{
    TargetPawn = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
}

bool ABlockRealmMob::FindWalkableGround(const ABlockWorld* InWorld, const FVector& DesiredCentre,
    float Radius, float HalfHeight, float MaxRise, float MaxDrop, FVector& OutCentre, bool bNaturalSoilOnly)
{
    if (!IsValid(InWorld) || InWorld->IsActorBeingDestroyed() || DesiredCentre.ContainsNaN()
        || !FMath::IsFinite(Radius) || !FMath::IsFinite(HalfHeight) || Radius < 1.f || Radius > 100.f
        || HalfHeight < Radius || HalfHeight > 200.f || !FMath::IsFinite(MaxRise)
        || !FMath::IsFinite(MaxDrop) || MaxRise < 0.f || MaxDrop < 0.f || MaxRise > 6600.f || MaxDrop > 6600.f
        || DesiredCentre.Z < -6800.f || DesiredCentre.Z > 13200.f) return false;
    const FIntVector Cell = ABlockWorld::WorldToBlock(DesiredCentre);
    if (Cell.X <= -FVoxelData::MaxCoordinate || Cell.X >= FVoxelData::MaxCoordinate
        || Cell.Y <= -FVoxelData::MaxCoordinate || Cell.Y >= FVoxelData::MaxCoordinate) return false;

    const double DesiredFeet = DesiredCentre.Z - HalfHeight - FootClearance;
    const int32 Highest = FMath::Clamp(FMath::FloorToInt((DesiredFeet + MaxRise) / 100.0) - 1, 0, FVoxelData::WorldHeight - 2);
    const int32 Lowest = FMath::Clamp(FMath::CeilToInt((DesiredFeet - MaxDrop) / 100.0) - 1, 0, Highest);
    const FIntVector MinXY = ABlockWorld::WorldToBlock(DesiredCentre - FVector(Radius * .92f, Radius * .92f, 0.f));
    const FIntVector MaxXY = ABlockWorld::WorldToBlock(DesiredCentre + FVector(Radius * .92f, Radius * .92f, 0.f));
    for (int32 Z = Highest; Z >= Lowest; --Z)
    {
        const double FeetZ = (Z + 1) * FVoxelData::BlockSize;
        const double CentreZ = FeetZ + HalfHeight + FootClearance;
        if (CentreZ > DesiredCentre.Z + MaxRise + .1 || CentreZ < DesiredCentre.Z - MaxDrop - .1) continue;
        bool bSupported = false;
        bool bWater = false;
        // A leading foot may step onto a one-block ledge. Requiring a whole
        // footprint at one height would trap controller-free creatures at every step.
        for (int32 Y = MinXY.Y; Y <= MaxXY.Y; ++Y)
            for (int32 X = MinXY.X; X <= MaxXY.X; ++X)
            {
                const EBlockType Type = InWorld->GetBlock(FIntVector(X, Y, Z));
                bSupported |= IsCreatureSupport(Type, bNaturalSoilOnly);
                bWater |= Type == EBlockType::Water;
            }
        if (!bSupported || bWater) continue;
        const int32 HeadZ = FMath::FloorToInt((FeetZ + 2.f * HalfHeight + FootClearance) / 100.0);
        bool bClear = HeadZ < FVoxelData::WorldHeight;
        for (int32 Y = MinXY.Y; bClear && Y <= MaxXY.Y; ++Y)
            for (int32 X = MinXY.X; bClear && X <= MaxXY.X; ++X)
                for (int32 CheckZ = Z + 1; CheckZ <= HeadZ; ++CheckZ)
                    if (InWorld->GetBlock(FIntVector(X, Y, CheckZ)) != EBlockType::Air)
                    { bClear = false; break; }
        if (!bClear) continue;
        OutCentre = FVector(DesiredCentre.X, DesiredCentre.Y, CentreZ);
        return true;
    }
    return false;
}

void ABlockRealmMob::UpdateGrounding(float DeltaSeconds)
{
    if (!IsValid(BlockWorld)) return;
    GroundCheckRemaining -= DeltaSeconds;
    if (bGrounded && GroundCheckRemaining > 0.f) return;
    GroundCheckRemaining = .10f;
    const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    FVector Ground;
    if (!FindWalkableGround(BlockWorld, GetActorLocation(), Radius, HalfHeight, 105.f, 220.f, Ground)
        && !FindWalkableGround(BlockWorld, GetActorLocation(), Radius, HalfHeight, 0.f, 6600.f, Ground))
    {
        // No dry floor exists in this column (water, removed terrain or a world edge).
        // Remove the invalid encounter instead of leaving it floating or granting loot.
        Destroy();
        return;
    }
    FVector Position = GetActorLocation();
    const float HeightDifference = static_cast<float>(Position.Z - Ground.Z);
    bGrounded = HeightDifference <= 3.f;
    if (bGrounded)
    {
        Position.Z = Ground.Z;
        VerticalVelocity = 0.f;
    }
    else
    {
        VerticalVelocity = FMath::Max(VerticalVelocity - 1600.f * DeltaSeconds, -1400.f);
        Position.Z = FMath::Max(Ground.Z, Position.Z + VerticalVelocity * DeltaSeconds);
        bGrounded = Position.Z <= Ground.Z + 3.f;
        if (bGrounded) VerticalVelocity = 0.f;
    }
    SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
}

bool ABlockRealmMob::TryGroundStep(const FVector& Step)
{
    if (!IsValid(BlockWorld))
    {
        FHitResult Hit;
        return SetActorLocation(GetActorLocation() + Step, true, &Hit);
    }
    if (!bGrounded) return false;
    FVector Ground;
    const float Radius = GetCapsuleComponent()->GetScaledCapsuleRadius();
    const float HalfHeight = GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
    if (!FindWalkableGround(BlockWorld, GetActorLocation() + Step, Radius, HalfHeight, 105.f, 105.f, Ground)) return false;
    FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmCreaturePawnClearance), false, this);
    const FCollisionObjectQueryParams PawnObjects(ECC_Pawn);
    if (GetWorld()->OverlapAnyTestByObjectType(Ground, FQuat::Identity, PawnObjects,
        FCollisionShape::MakeCapsule(Radius * .85f, HalfHeight - 2.f), Query)) return false;
    SetActorLocation(Ground, false, nullptr, ETeleportType::TeleportPhysics);
    return true;
}

bool ABlockRealmMob::MoveAlongGround(const FVector& Direction, float Speed, float DeltaSeconds)
{
    const FVector FlatDirection = FVector(Direction.X, Direction.Y, 0.f).GetSafeNormal();
    if (FlatDirection.IsNearlyZero() || DeltaSeconds <= 0.f || Speed <= 0.f) return false;
    const FVector Before = GetActorLocation();
    const float Distance = FMath::Min(Speed * DeltaSeconds, 60.f);
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(Distance / 20.f));
    for (int32 Index = 0; Index < Steps; ++Index)
    {
        bool bMoved = false;
        const float Alternatives[] = { 0.f, 55.f, -55.f, 90.f, -90.f };
        for (float Angle : Alternatives)
        {
            const FVector StepDirection = FlatDirection.RotateAngleAxis(Angle, FVector::UpVector);
            if (TryGroundStep(StepDirection * (Distance / Steps))) { bMoved = true; break; }
        }
        if (!bMoved) break;
    }
    const FVector ActualDirection = GetActorLocation() - Before;
    const float ActualDistance = ActualDirection.Size2D();
    MovedDistanceThisFrame += ActualDistance;
    if (ActualDistance > .05f)
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), FVector(ActualDirection.X, ActualDirection.Y, 0.f).Rotation(), DeltaSeconds, 9.f));
    return ActualDistance > .05f;
}

void ABlockRealmMob::TickBehavior(float DeltaSeconds) { ChaseOrAttack(DeltaSeconds); }

void ABlockRealmMob::ChaseOrAttack(float DeltaSeconds)
{
    if (!IsValid(TargetPawn)) return;
    const FVector ToTarget = TargetPawn->GetActorLocation() - GetActorLocation();
    const float Distance = ToTarget.Size2D();
    if (!FMath::IsFinite(Distance)) return;
    if (Distance > DetectionRadius)
    {
        IdleDecisionRemaining -= DeltaSeconds;
        if (IdleDecisionRemaining <= 0.f)
        {
            const float Angle = GetUniqueID() * 1.73f + AnimationPhase * .23f;
            IdleDirection = FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f);
            if (FVector::DistSquared2D(GetActorLocation(), HomePosition) > FMath::Square(350.f))
                IdleDirection = (HomePosition - GetActorLocation()).GetSafeNormal2D();
            IdleDecisionRemaining = 2.8f;
        }
        if (IdleDecisionRemaining > 1.8f) MoveAlongGround(IdleDirection, MoveSpeed * .30f, DeltaSeconds);
        return;
    }
    if (ToTarget.SizeSquared() <= FMath::Square(FMath::Max(AttackRange, 0.f)))
    {
        SetActorRotation(FMath::RInterpTo(GetActorRotation(), FVector(ToTarget.X, ToTarget.Y, 0.f).Rotation(), DeltaSeconds, 9.f));
        if (AttackCooldownRemaining <= 0.f && AttackDamage > 0.f)
        {
            const FVector Origin = GetActorLocation();
            const FVector Ray = TargetPawn->GetActorLocation() - Origin;
            FIntVector HitBlock, Adjacent;
            if (IsValid(BlockWorld) && BlockWorld->TraceBlocks(Origin, Ray, Ray.Size(), HitBlock, Adjacent)) return;
            FCollisionQueryParams Query(SCENE_QUERY_STAT(BlockRealmMobMelee), false, this);
            Query.AddIgnoredActor(TargetPawn.Get());
            FHitResult Blocker;
            if (GetWorld()->LineTraceSingleByChannel(Blocker, Origin, TargetPawn->GetActorLocation(), ECC_Visibility, Query)) return;
            AttackCooldownRemaining = FMath::Max(AttackInterval, .05f);
            AttackVisualRemaining = .28f;
            UGameplayStatics::ApplyDamage(TargetPawn.Get(), AttackDamage, GetController(), this, UDamageType::StaticClass());
        }
        return;
    }
    MoveAlongGround(ToTarget, FMath::Max(MoveSpeed, 0.f), DeltaSeconds);
}

void ABlockRealmMob::SetPartColor(UStaticMeshComponent* Part, const FLinearColor& Color)
{
    if (!Part || !PaletteMaterial) return;
    UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PaletteMaterial, this);
    if (!Material) return;
    Material->SetVectorParameterValue(TEXT("Color"), Color);
    Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
    Part->SetMaterial(0, Material);
    CreatureMaterials.Add(Material);
    MaterialColors.Add(Color);
}

void ABlockRealmMob::ConfigureAppearance()
{
    CreatureMaterials.Reset();
    MaterialColors.Reset();
    bWasHitFlashing = false;
    SetPartColor(BodyMesh, BodyColor);
    for (int32 Index = 0; Index < HostileParts.Num(); ++Index)
    {
        const bool bEye = Index == 2 || Index == 3 || Index == 10;
        const bool bCrest = Index == 4 || Index == 5;
        SetPartColor(HostileParts[Index], bEye ? FLinearColor(1.f, .45f, .075f)
            : bCrest ? FLinearColor(.34f, .43f, .45f) : FLinearColor(.23f, .30f, .33f));
    }
}

void ABlockRealmMob::TickCreatureVisuals(float DeltaSeconds)
{
    AnimationPhase += DeltaSeconds * (MovedDistanceThisFrame > .1f ? 8.f : 1.5f);
    if (IsHostile() && HostileParts.Num() >= 10)
    {
        const float Swing = MovedDistanceThisFrame > .1f ? FMath::Sin(AnimationPhase) * 22.f : 0.f;
        const float AttackSwing = AttackVisualRemaining > 0.f ? -55.f * FMath::Sin(AttackVisualRemaining / .28f * PI) : 0.f;
        HostileParts[6]->SetRelativeRotation(FRotator(-Swing + AttackSwing, 0.f, 0.f));
        HostileParts[7]->SetRelativeRotation(FRotator(Swing + AttackSwing, 0.f, 0.f));
        HostileParts[8]->SetRelativeRotation(FRotator(Swing * .6f, 0.f, 0.f));
        HostileParts[9]->SetRelativeRotation(FRotator(-Swing * .6f, 0.f, 0.f));
    }
    const bool bFlashing = HitFlashRemaining > 0.f;
    if (bFlashing == bWasHitFlashing) return;
    bWasHitFlashing = bFlashing;
    const float Flash = bFlashing ? .65f : 0.f;
    for (int32 Index = 0; Index < CreatureMaterials.Num(); ++Index)
    {
        if (!CreatureMaterials[Index] || !MaterialColors.IsValidIndex(Index)) continue;
        const FLinearColor Color = FMath::Lerp(MaterialColors[Index], FLinearColor(1.f, .12f, .05f), Flash);
        CreatureMaterials[Index]->SetVectorParameterValue(TEXT("Color"), Color);
        CreatureMaterials[Index]->SetVectorParameterValue(TEXT("BaseColor"), Color);
    }
}

void ABlockRealmMob::Die()
{
    if (bDead) return;
    bDead = true;
    SpawnDeathDrops(GetActorLocation() + FVector(0.f, 0.f, 15.f));
    SetActorEnableCollision(false);
    GetCharacterMovement()->StopMovementImmediately();
    GetCharacterMovement()->DisableMovement();
    SetActorHiddenInGame(true);
    SetLifeSpan(.10f);
    OnMobDied.Broadcast(this);
}

void ABlockRealmMob::SpawnDeathDrops(const FVector& DropOrigin)
{
    // Legacy combat fixtures and real inventory pickup share these exact drops.
    SpawnDrop(EBlockType::Stone, DropOrigin + FVector(-26.f, 0.f, 10.f));
    SpawnDrop(EBlockType::Glow, DropOrigin + FVector(26.f, 0.f, 24.f));
}

void ABlockRealmMob::SpawnDrop(EBlockType DropType, const FVector& Location) { SpawnPickup(DropType, Location, false); }
void ABlockRealmMob::SpawnFoodDrop(const FVector& Location) { SpawnPickup(EBlockType::Air, Location, true); }

void ABlockRealmMob::SpawnPickup(EBlockType DropType, const FVector& Location, bool bFood)
{
    if (!GetWorld() || GetWorld()->bIsTearingDown || Location.ContainsNaN()) return;
    FVector GroundedLocation = Location;
    if (IsValid(BlockWorld))
    {
        FVector Ground;
        if (!FindWalkableGround(BlockWorld, Location, 15.f, 15.f, 110.f, 6600.f, Ground)) return;
        GroundedLocation = Ground;
    }
    FActorSpawnParameters Parameters;
    Parameters.Owner = IsValid(BlockWorld) ? static_cast<AActor*>(BlockWorld.Get()) : this;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    AStaticMeshActor* Drop = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(),
        GroundedLocation, FRotator(0.f, 35.f, 0.f), Parameters);
    if (!Drop) return;
    UStaticMeshComponent* DropMesh = Drop->GetStaticMeshComponent();
    DropMesh->SetMobility(EComponentMobility::Movable);
    DropMesh->SetStaticMesh(BodyMesh->GetStaticMesh());
    DropMesh->SetWorldScale3D(bFood ? FVector(.30f, .24f, .22f) : FVector(.28f));
    DropMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    DropMesh->SetGenerateOverlapEvents(false);
    DropMesh->SetCanEverAffectNavigation(false);
    if (PaletteMaterial)
    {
        UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(PaletteMaterial, Drop);
        const FLinearColor Color = bFood ? FLinearColor(.85f, .24f, .13f) : BlockDisplayColor(DropType);
        Material->SetVectorParameterValue(TEXT("Color"), Color);
        Material->SetVectorParameterValue(TEXT("BaseColor"), Color);
        DropMesh->SetMaterial(0, Material);
    }
    Drop->Tags.Add(bFood ? FName(TEXT("BlockRealmFoodDrop")) : FName(TEXT("BlockRealmDrop")));
    if (!bFood) Drop->Tags.Add(FName(*BlockDisplayName(DropType)));
    Drop->SetLifeSpan(180.f);
}
