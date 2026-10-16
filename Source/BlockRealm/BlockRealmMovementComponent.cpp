#include "BlockRealmMovementComponent.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmGameMode.h"
#include "BlockWorld.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"

void UBlockRealmMovementComponent::SetWaterInput(float VerticalInput, float Speed)
{
    WaterVerticalInput = FMath::Clamp(VerticalInput, -1.f, 1.f);
    WaterSpeed = FMath::Max(0.f, Speed);
}

float UBlockRealmMovementComponent::GetMaxSpeed() const
{
    return IsInVoxelWaterMode() ? WaterSpeed : Super::GetMaxSpeed();
}

void UBlockRealmMovementComponent::SetUpdatedComponent(USceneComponent* NewUpdatedComponent)
{
    if (USceneComponent* Previous = FallObservedComponent.Get())
        Previous->TransformUpdated.Remove(FallTransformHandle);
    FallObservedComponent.Reset();
    FallTransformHandle.Reset();
    ResetFallDamageTracking();
    Super::SetUpdatedComponent(NewUpdatedComponent);
    if (UpdatedComponent)
    {
        FallObservedComponent = UpdatedComponent.Get();
        FallTransformHandle = UpdatedComponent->TransformUpdated.AddUObject(this,
            &UBlockRealmMovementComponent::HandleFallTransformUpdated);
    }
}

void UBlockRealmMovementComponent::HandleFallTransformUpdated(USceneComponent*,
    EUpdateTransformFlags, ETeleportType Teleport)
{
    // APawn::TeleportTo calls OnTeleported, while SetActorLocation(...,
    // TeleportPhysics) goes straight to MoveComponent. Observe the documented
    // scene transform event as well so both real teleport paths clear peaks.
    if (Teleport != ETeleportType::None) ResetFallDamageTracking();
}

void UBlockRealmMovementComponent::ClearFallDamageTracking(bool bRequireGroundContact)
{
    FallTracker.Reset();
    bIgnoreFallUntilGrounded = bRequireGroundContact;
    bFallTrackingSuspended = false;
    bHasFallLocation = false;
    FallStartFloorClearanceCm = 0.0;
}

void UBlockRealmMovementComponent::ResetFallDamageTracking()
{
    ClearFallDamageTracking(true);
    GroundFloorClearanceCm = 0.0;
}

bool UBlockRealmMovementComponent::CanTrackFallDamage() const
{
    const ABlockRealmCharacter* Player = Cast<ABlockRealmCharacter>(CharacterOwner);
    const UWorld* World = GetWorld();
    const ABlockRealmGameMode* Game = World ? World->GetAuthGameMode<ABlockRealmGameMode>() : nullptr;
    return HasValidData() && Player && IsValid(Player->GetBlockWorld()) && Game
        && Game->IsAdventureActive() && !Player->IsInventoryOpen() && Player->GetHealth() > 0.f
        && Player->GetActorEnableCollision() && GetGravityZ() < 0.f;
}

bool UBlockRealmMovementComponent::IsFallPause() const
{
    const ABlockRealmCharacter* Player = Cast<ABlockRealmCharacter>(CharacterOwner);
    const UWorld* World = GetWorld();
    const ABlockRealmGameMode* Game = World ? World->GetAuthGameMode<ABlockRealmGameMode>() : nullptr;
    return Player && (Player->IsInventoryOpen() || (Game && Game->IsPausedMenu()));
}

bool UBlockRealmMovementComponent::HasVoxelWaterAlongPath(const FVector& Start, const FVector& End) const
{
    const ABlockRealmCharacter* Player = Cast<ABlockRealmCharacter>(CharacterOwner);
    const ABlockWorld* Blocks = Player ? Player->GetBlockWorld() : nullptr;
    if (!Blocks || Start.ContainsNaN() || End.ContainsNaN()) return false;
    const UCapsuleComponent* Capsule = Player->GetCapsuleComponent();
    const double HalfHeight = Capsule ? Capsule->GetScaledCapsuleHalfHeight() : 90.0;
    const double Radius = Capsule ? Capsule->GetScaledCapsuleRadius() * .6 : 20.0;
    const FVector Offsets[] = {FVector::ZeroVector, FVector(Radius, 0.0, 0.0),
        FVector(-Radius, 0.0, 0.0), FVector(0.0, Radius, 0.0), FVector(0.0, -Radius, 0.0)};
    const double Heights[] = {-HalfHeight + 4.0, -HalfHeight * .22, HalfHeight * .45};
    // Half-block samples also detect a water landing when one long physics
    // frame crosses the entire water cell before Character::Tick can update
    // its cached immersion. The world itself is only 64 blocks tall.
    const double Distance = FVector::Distance(Start, End);
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(FMath::Min(Distance, 100000.0) / 50.0));
    for (int32 Step = 0; Step <= Steps; ++Step)
    {
        const FVector Center = FMath::Lerp(Start, End, static_cast<double>(Step) / Steps);
        for (const double Height : Heights)
        {
            for (const FVector& Offset : Offsets)
            {
                if (Blocks->GetBlock(ABlockWorld::WorldToBlock(Center + Offset
                    + FVector(0.0, 0.0, Height))) == EBlockType::Water)
                {
                    return true;
                }
            }
        }
    }
    return false;
}

void UBlockRealmMovementComponent::SampleFallPath(const FVector& Location)
{
    if (!FallTracker.IsTracking()) return;
    if (Location.ContainsNaN() || HasVoxelWaterAlongPath(bHasFallLocation ? LastFallLocation : Location, Location))
    {
        ClearFallDamageTracking(true);
        return;
    }
    FallTracker.Sample(Location.Z);
    LastFallLocation = Location;
    bHasFallLocation = true;
}

void UBlockRealmMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
    const FVector Location = UpdatedComponent ? UpdatedComponent->GetComponentLocation() : FVector::ZeroVector;
    const bool bWasGrounded = PreviousMovementMode == MOVE_Walking || PreviousMovementMode == MOVE_NavWalking;
    if (bWasGrounded && CurrentFloor.IsWalkableFloor())
        GroundFloorClearanceCm = FMath::Max(0.0, static_cast<double>(CurrentFloor.GetDistanceToFloor()));

    if (MovementMode == MOVE_None && PreviousMovementMode == MOVE_Falling
        && FallTracker.IsTracking() && IsFallPause())
    {
        SampleFallPath(Location);
        bFallTrackingSuspended = FallTracker.IsTracking();
        SuspendedFallLocation = Location;
    }
    else if (MovementMode == MOVE_Falling)
    {
        const bool bResumeFall = PreviousMovementMode == MOVE_None && bFallTrackingSuspended
            && FallTracker.IsTracking() && Location.Equals(SuspendedFallLocation, .1)
            && !bHandlingFallTeleport && CanTrackFallDamage();
        if (!bResumeFall)
        {
            const bool bCanBeginFall = !bIgnoreFallUntilGrounded && !bHandlingFallTeleport && CanTrackFallDamage();
            ClearFallDamageTracking(true);
            if (bCanBeginFall)
            {
                bIgnoreFallUntilGrounded = false;
                FallStartFloorClearanceCm = bWasGrounded ? GroundFloorClearanceCm : 0.0;
                FallTracker.Begin(Location.Z);
                SampleFallPath(Location);
            }
        }
        bFallTrackingSuspended = false;
        if (FallTracker.IsTracking()) bNotifyApex = true;
    }
    else
    {
        // Flight and voxel swimming cannot accumulate a lethal vertical peak.
        // Their first landing, like a load/spawn landing, only rearms tracking.
        ClearFallDamageTracking(true);
    }

    Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);
    if (IsMovingOnGround() && CurrentFloor.IsWalkableFloor() && !bHandlingFallTeleport && CanTrackFallDamage())
    {
        GroundFloorClearanceCm = FMath::Max(0.0, static_cast<double>(CurrentFloor.GetDistanceToFloor()));
        bIgnoreFallUntilGrounded = false;
    }
}

void UBlockRealmMovementComponent::PhysFalling(float DeltaTime, int32 Iterations)
{
    if (!CanTrackFallDamage()) ClearFallDamageTracking(true);
    else if (UpdatedComponent) SampleFallPath(UpdatedComponent->GetComponentLocation());
    Super::PhysFalling(DeltaTime, Iterations);
    if (HasValidData() && IsFalling()) SampleFallPath(UpdatedComponent->GetComponentLocation());
}

void UBlockRealmMovementComponent::NotifyJumpApex()
{
    // UE's apex substep reaches the peak even when the outer frame is long.
    if (UpdatedComponent) SampleFallPath(UpdatedComponent->GetComponentLocation());
    Super::NotifyJumpApex();
}

void UBlockRealmMovementComponent::ProcessLanded(const FHitResult& Hit, float RemainingTime, int32 Iterations)
{
    double DistanceCm = 0.0;
    float Damage = 0.f;
    if (CanTrackFallDamage() && !bIgnoreFallUntilGrounded && FallTracker.IsTracking())
    {
        const FVector Location = UpdatedComponent->GetComponentLocation();
        SampleFallPath(Location);
        // Walking intentionally hovers ~2 cm over the collision floor. Remove
        // that initial clearance so a three-block ledge remains exactly safe.
        DistanceCm = FallTracker.GetFallDistance(Location.Z + FallStartFloorClearanceCm);
        // Sub-centimeter collision rounding must not turn that safe edge into
        // a damage flash. Larger fractional drops retain proportional damage.
        Damage = DistanceCm <= 300.5 ? 0.f : FBlockRealmSurvivalData::CalculateFallDamage(DistanceCm);
    }
    ClearFallDamageTracking(true);
    LastFallDistanceCm = DistanceCm;
    LastFallDamage = 0.f;
    Super::ProcessLanded(Hit, RemainingTime, Iterations);
    // Finish ordinary landing physics before damage can trigger a respawn.
    if (Damage > 0.f && CanTrackFallDamage())
    {
        if (ABlockRealmCharacter* Player = Cast<ABlockRealmCharacter>(CharacterOwner))
        {
            LastFallDamage = Damage;
            UE_LOG(LogTemp, Display, TEXT("BR_FALL_DAMAGE distance_cm=%.2f damage=%.2f"), DistanceCm, Damage);
            Player->ApplySurvivalDamage(Damage, TEXT("摔落"));
        }
    }
}

void UBlockRealmMovementComponent::OnTeleported()
{
    bHandlingFallTeleport = true;
    ResetFallDamageTracking();
    Super::OnTeleported();
    bHandlingFallTeleport = false;
    ResetFallDamageTracking();
}

void UBlockRealmMovementComponent::OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity)
{
    Super::OnMovementUpdated(DeltaSeconds, OldLocation, OldVelocity);
    if (!HasValidData()) return;
    if (IsMovingOnGround() && CurrentFloor.IsWalkableFloor() && CanTrackFallDamage())
    {
        ClearFallDamageTracking(false);
        GroundFloorClearanceCm = FMath::Max(0.0, static_cast<double>(CurrentFloor.GetDistanceToFloor()));
    }
    else if (IsFalling() && CanTrackFallDamage())
    {
        SampleFallPath(UpdatedComponent->GetComponentLocation());
    }
    else if (!(MovementMode == MOVE_None && bFallTrackingSuspended && IsFallPause()))
    {
        ClearFallDamageTracking(true);
    }
}

void UBlockRealmMovementComponent::PhysCustom(float DeltaTime, int32 Iterations)
{
    if (!IsInVoxelWaterMode())
    {
        Super::PhysCustom(DeltaTime, Iterations);
        return;
    }
    if (DeltaTime < MIN_TICK_TIME || !HasValidData())
    {
        return;
    }

    float RemainingTime = DeltaTime;
    while (RemainingTime >= MIN_TICK_TIME && Iterations < MaxSimulationIterations
        && HasValidData() && IsInVoxelWaterMode())
    {
        ++Iterations;
        const float TimeStep = GetSimulationTimeStep(RemainingTime, Iterations);
        RemainingTime -= TimeStep;
        const FVector OldLocation = UpdatedComponent->GetComponentLocation();
        const float OldVerticalVelocity = Velocity.Z;
        const float OldVerticalAcceleration = Acceleration.Z;
        bJustTeleported = false;

        // Apply fluid drag and the same acceleration/collision machinery as land movement.
        Velocity.Z = 0.f;
        Acceleration.Z = 0.f;
        CalcVelocity(TimeStep, 3.5f, true, 900.f);
        Acceleration.Z = OldVerticalAcceleration;
        const float DesiredVerticalVelocity = WaterVerticalInput >= 0.f
            ? WaterVerticalInput * 250.f : WaterVerticalInput * 190.f;
        // Buoyancy offsets most gravity; drag leaves a gentle ~35 cm/s idle descent.
        Velocity.Z = FMath::FInterpTo(OldVerticalVelocity, DesiredVerticalVelocity, TimeStep, 5.f)
            + GetGravityZ() * .12f * TimeStep;
        Velocity.Z = FMath::Clamp(Velocity.Z, -240.f, 250.f);

        FHitResult Hit;
        const FVector Delta = Velocity * TimeStep;
        SafeMoveUpdatedComponent(Delta, UpdatedComponent->GetComponentQuat(), true, Hit);
        if (Hit.IsValidBlockingHit())
        {
            HandleImpact(Hit, TimeStep, Delta);
            if (HasValidData() && IsInVoxelWaterMode())
            {
                SlideAlongSurface(Delta, 1.f - Hit.Time, Hit.Normal, Hit, true);
            }
        }
        if (!bJustTeleported && HasValidData() && IsInVoxelWaterMode())
        {
            Velocity = (UpdatedComponent->GetComponentLocation() - OldLocation) / TimeStep;
        }
    }
}
