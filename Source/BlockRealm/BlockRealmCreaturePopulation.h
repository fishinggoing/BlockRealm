#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BlockRealmAnimal.h"
#include "BlockRealmCreaturePopulation.generated.h"

class ABlockWorld;
class ABlockRealmDayNight;
class APawn;

/** A bounded local population; no navigation build, timers or unbounded spawn loops. */
UCLASS()
class BLOCKREALM_API ABlockRealmCreaturePopulation : public AActor
{
    GENERATED_BODY()

public:
    ABlockRealmCreaturePopulation();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

    /** Call after the player/world are ready. First active tick places both animals ahead of the view. */
    void Initialize(ABlockWorld* InWorld, APawn* InPlayer, ABlockRealmDayNight* InClock = nullptr);

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Creatures")
    void PopulateNow();

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Creatures")
    void ClearPopulation();

    void SetPopulationEnabled(bool bInEnabled);

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Creatures")
    int32 GetAnimalCount() const;

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Creatures")
    int32 GetHostileCount() const;

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Creatures")
    int32 GetSpeciesCount(EBlockRealmAnimalSpecies Species) const;

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Creatures")
    FString GetStatusText() const;

    const TArray<TObjectPtr<ABlockRealmMob>>& GetCreatures() const { return Creatures; }
    ABlockRealmMob* GetNearestCreature(bool bHostile) const;
    bool ValidatePopulation(TArray<FString>& Failures) const;

    static constexpr int32 MaximumAnimals = 8;
    static constexpr int32 MaximumHostiles = 3;

protected:
    UPROPERTY(EditAnywhere, Category = "BlockRealm|Creatures", meta = (ClampMin = "2", ClampMax = "8"))
    int32 DesiredAnimals = 6;

    UPROPERTY(EditAnywhere, Category = "BlockRealm|Creatures", meta = (ClampMin = "0", ClampMax = "3"))
    int32 DesiredNightHostiles = 3;

    UPROPERTY(EditAnywhere, Category = "BlockRealm|Creatures", meta = (ClampMin = "0", ClampMax = "1"))
    int32 DesiredDayHostiles = 1;

private:
    bool CanPopulate() const;
    bool IsNight() const;
    void PrunePopulation();
    bool SpawnAnimal(EBlockRealmAnimalSpecies Species, bool bInOpeningView);
    bool SpawnHostile();
    bool FindSpawnLocation(bool bHostile, bool bInOpeningView, FVector& OutLocation);
    bool IsSpawnSpaceFree(const FVector& Location, float Radius, float HalfHeight) const;
    bool HasRoof(const FVector& Location) const;
    bool IsOpeningViewClear(const FVector& Location) const;

    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;

    UPROPERTY()
    TObjectPtr<APawn> Player;

    UPROPERTY()
    TObjectPtr<ABlockRealmDayNight> Clock;

    UPROPERTY()
    TArray<TObjectPtr<ABlockRealmMob>> Creatures;

    FRandomStream PopulationRandom;
    FVector LastPlayerLocation = FVector::ZeroVector;
    FVector SafeSpawnAnchor = FVector::ZeroVector;
    float AnimalRefillRemaining = 0.f;
    float HostileRefillRemaining = 0.f;
    float RetryRemaining = 0.f;
    bool bEnabled = true;
    bool bPopulationStarted = false;
};
