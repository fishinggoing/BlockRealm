#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BlockRealmSurvival.h"
#include "BlockRealmMovementComponent.generated.h"

// Voxel water has no PhysicsVolume. Keep its movement separate from explicit flight.
UCLASS()
class BLOCKREALM_API UBlockRealmMovementComponent : public UCharacterMovementComponent
{
    GENERATED_BODY()

public:
    static constexpr uint8 VoxelWaterMode = 1;

    bool IsInVoxelWaterMode() const
    {
        return MovementMode == MOVE_Custom && CustomMovementMode == VoxelWaterMode;
    }
    void SetWaterInput(float VerticalInput, float Speed);
    virtual float GetMaxSpeed() const override;
    virtual void SetUpdatedComponent(USceneComponent* NewUpdatedComponent) override;
    // Adventure boundaries clear the old peak and require a real ground
    // contact before another fall can hurt. Teleports call this automatically.
    void ResetFallDamageTracking();
    // Read-only diagnostics for runtime regression; these describe the last
    // completed physical fall and are not part of the save format.
    double GetLastFallDistanceCm() const { return LastFallDistanceCm; }
    float GetLastFallDamage() const { return LastFallDamage; }

protected:
    virtual void PhysCustom(float DeltaTime, int32 Iterations) override;
    virtual void PhysFalling(float DeltaTime, int32 Iterations) override;
    virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode) override;
    virtual void OnMovementUpdated(float DeltaSeconds, const FVector& OldLocation, const FVector& OldVelocity) override;
    virtual void NotifyJumpApex() override;
    virtual void ProcessLanded(const FHitResult& Hit, float RemainingTime, int32 Iterations) override;
    virtual void OnTeleported() override;

private:
    bool CanTrackFallDamage() const;
    bool IsFallPause() const;
    bool HasVoxelWaterAlongPath(const FVector& Start, const FVector& End) const;
    void SampleFallPath(const FVector& Location);
    void ClearFallDamageTracking(bool bRequireGroundContact);
    void HandleFallTransformUpdated(USceneComponent* Component, EUpdateTransformFlags UpdateFlags,
        ETeleportType Teleport);

    float WaterVerticalInput = 0.f;
    float WaterSpeed = 220.f;
    FBlockRealmFallTracker FallTracker;
    bool bIgnoreFallUntilGrounded = true;
    bool bFallTrackingSuspended = false;
    bool bHandlingFallTeleport = false;
    bool bHasFallLocation = false;
    FVector LastFallLocation = FVector::ZeroVector;
    FVector SuspendedFallLocation = FVector::ZeroVector;
    double GroundFloorClearanceCm = 0.0;
    double FallStartFloorClearanceCm = 0.0;
    double LastFallDistanceCm = 0.0;
    float LastFallDamage = 0.f;
    TWeakObjectPtr<USceneComponent> FallObservedComponent;
    FDelegateHandle FallTransformHandle;
};
