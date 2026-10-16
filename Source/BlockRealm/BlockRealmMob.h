#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "BlockTypes.h"
#include "BlockRealmMob.generated.h"

class ABlockWorld;
class ABlockRealmMob;
class APawn;
class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FBlockRealmMobDiedSignature, ABlockRealmMob*, Mob);

/** Original box-built rock creature. Movement uses authoritative voxels, without a navmesh. */
UCLASS()
class BLOCKREALM_API ABlockRealmMob : public ACharacter
{
    GENERATED_BODY()

public:
    ABlockRealmMob(const FObjectInitializer& ObjectInitializer);

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent,
        AController* EventInstigator, AActor* DamageCauser) override;

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Mob")
    void InitializeTarget(APawn* InTarget);

    /** Alias convenient for a spawn director or Blueprint. */
    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Mob")
    void SetTarget(APawn* InTarget);

    // Plain C++ hook; the world type is intentionally forward declared.
    void SetWorld(ABlockWorld* InWorld);

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Mob")
    void ApplyDamage(float DamageAmount);

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Mob")
    virtual bool IsHostile() const { return true; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Mob")
    virtual FString GetCreatureDisplayName() const;

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Mob")
    bool IsGrounded() const { return bGrounded; }

    /** Population encounters can have a smaller awareness range and a safe arrival delay. */
    void ConfigureEncounter(float AwarenessRadius, float InitialAttackDelay);

    /** Centre position on dry, clear voxel support; never accepts water, tree tops or unloaded mesh hits. */
    static bool FindWalkableGround(const ABlockWorld* InWorld, const FVector& DesiredCentre,
        float Radius, float HalfHeight, float MaxRise, float MaxDrop, FVector& OutCentre,
        bool bNaturalSoilOnly = false);

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Mob")
    bool IsDead() const { return bDead; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Mob")
    float GetHealth() const { return Health; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Mob")
    APawn* GetTarget() const { return TargetPawn; }

    ABlockWorld* GetBlockWorld() const { return BlockWorld; }

    UPROPERTY(BlueprintAssignable, Category = "BlockRealm|Mob")
    FBlockRealmMobDiedSignature OnMobDied;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlockRealm|Mob|Visual")
    TObjectPtr<UStaticMeshComponent> BodyMesh;

protected:
    virtual void TickBehavior(float DeltaSeconds);
    virtual void TickCreatureVisuals(float DeltaSeconds);
    virtual void ConfigureAppearance();
    virtual void OnDamaged(float DamageAmount, AActor* DamageCauser);
    virtual void SpawnDeathDrops(const FVector& DropOrigin);

    bool CanAct() const;
    bool MoveAlongGround(const FVector& Direction, float Speed, float DeltaSeconds);
    UStaticMeshComponent* CreateCreaturePart(FName Name, const FVector& LocalLocation, const FVector& SizeCm);
    void SetPartColor(UStaticMeshComponent* Part, const FLinearColor& Color);
    void SpawnDrop(EBlockType DropType, const FVector& Location);
    void SpawnFoodDrop(const FVector& Location);
    float GetMovedDistanceThisFrame() const { return MovedDistanceThisFrame; }

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlockRealm|Mob|Visual")
    TArray<TObjectPtr<UStaticMeshComponent>> HostileParts;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Stats", meta = (ClampMin = "1.0"))
    float MaxHealth = 20.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Stats", meta = (ClampMin = "0.0"))
    float AttackDamage = 4.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Stats", meta = (ClampMin = "0.0"))
    float AttackRange = 145.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Stats", meta = (ClampMin = "0.0"))
    float AttackInterval = 1.20f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Stats", meta = (ClampMin = "0.0"))
    float DetectionRadius = 1800.0f;

    /** Remove creatures outside the streamed player area without granting drops. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Stats", meta = (ClampMin = "1000.0"))
    float DespawnDistance = 8000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Stats", meta = (ClampMin = "0.0"))
    float MoveSpeed = 220.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Mob|Visual")
    FLinearColor BodyColor = FLinearColor(0.16f, 0.22f, 0.26f, 1.0f);

private:
    void SetSafeStateFrozen(bool bFrozen);
    void AcquirePlayerTarget();
    void ChaseOrAttack(float DeltaSeconds);
    void UpdateGrounding(float DeltaSeconds);
    bool TryGroundStep(const FVector& Step);
    void Die();
    void SpawnPickup(EBlockType DropType, const FVector& Location, bool bFood);

    UPROPERTY()
    TObjectPtr<UMaterialInterface> PaletteMaterial;

    UPROPERTY(Transient)
    TArray<TObjectPtr<UMaterialInstanceDynamic>> CreatureMaterials;

    TArray<FLinearColor> MaterialColors;

    UPROPERTY()
    TWeakObjectPtr<AActor> LastDamageCauser;

    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;

    UPROPERTY()
    TObjectPtr<APawn> TargetPawn;

    float Health = 20.0f;
    float AttackCooldownRemaining = 0.0f;
    float AttackVisualRemaining = 0.0f;
    float HitFlashRemaining = 0.0f;
    float AnimationPhase = 0.0f;
    float MovedDistanceThisFrame = 0.0f;
    float VerticalVelocity = 0.0f;
    float GroundCheckRemaining = 0.0f;
    float IdleDecisionRemaining = 0.0f;
    FVector HomePosition = FVector::ZeroVector;
    FVector IdleDirection = FVector::ZeroVector;
    bool bGrounded = false;
    bool bWasHitFlashing = false;
    bool bDead = false;
    bool bHasExplicitTarget = false;
    bool bSafeStateFrozen = false;
    uint8 FrozenMovementMode = 1;
    uint8 FrozenCustomMovementMode = 0;
};
