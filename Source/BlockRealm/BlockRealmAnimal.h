#pragma once

#include "CoreMinimal.h"
#include "BlockRealmMob.h"
#include "BlockRealmAnimal.generated.h"

UENUM(BlueprintType)
enum class EBlockRealmAnimalSpecies : uint8
{
    Pig,
    Sheep
};

/** Two original, box-built grazing animals sharing the normal damage/pickup path. */
UCLASS()
class BLOCKREALM_API ABlockRealmAnimal : public ABlockRealmMob
{
    GENERATED_BODY()

public:
    ABlockRealmAnimal(const FObjectInitializer& ObjectInitializer);
    virtual void BeginPlay() override;
    virtual bool IsHostile() const override { return false; }
    virtual FString GetCreatureDisplayName() const override;

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Animal")
    void SetSpecies(EBlockRealmAnimalSpecies InSpecies);

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Animal")
    EBlockRealmAnimalSpecies GetSpecies() const { return Species; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Animal")
    bool IsFleeing() const { return FleeRemaining > 0.f && !IsDead(); }

protected:
    virtual void TickBehavior(float DeltaSeconds) override;
    virtual void TickCreatureVisuals(float DeltaSeconds) override;
    virtual void ConfigureAppearance() override;
    virtual void OnDamaged(float DamageAmount, AActor* DamageCauser) override;
    virtual void SpawnDeathDrops(const FVector& DropOrigin) override;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "BlockRealm|Animal")
    EBlockRealmAnimalSpecies Species = EBlockRealmAnimalSpecies::Pig;

private:
    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TObjectPtr<UStaticMeshComponent> AnimalHead;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TObjectPtr<UStaticMeshComponent> Snout;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TObjectPtr<UStaticMeshComponent> Tail;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TArray<TObjectPtr<UStaticMeshComponent>> AnimalLegs;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TArray<TObjectPtr<UStaticMeshComponent>> Ears;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TArray<TObjectPtr<UStaticMeshComponent>> Eyes;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TArray<TObjectPtr<UStaticMeshComponent>> CoatPieces;

    UPROPERTY(VisibleAnywhere, Category = "BlockRealm|Animal|Visual")
    TArray<TObjectPtr<UStaticMeshComponent>> Horns;

    FVector GrazingHome = FVector::ZeroVector;
    FVector WanderDirection = FVector::ForwardVector;
    FVector ThreatPosition = FVector::ZeroVector;
    FRandomStream WanderRandom;
    float DecisionRemaining = 1.5f;
    float WalkRemaining = 0.f;
    float FleeRemaining = 0.f;
    float GaitPhase = 0.f;
};
