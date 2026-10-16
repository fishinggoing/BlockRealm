#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "UObject/SoftObjectPtr.h"
#include "BlockRealmDayNight.generated.h"

class ABlockWorld;
class UWorld;
class UDirectionalLightComponent;
class USkyLightComponent;
class UMaterialParameterCollection;

/**
 * A small, self-contained day/night clock.  TimeOfDay is normalized to [0, 1):
 * 0.00 is midnight, 0.25 is sunrise, 0.50 is noon and 0.75 is sunset.
 * The clock drives the map's existing directional light and sky light.
 * Both sources pause with the gameplay overlays.
 */
UCLASS()
class BLOCKREALM_API ABlockRealmDayNight : public AActor
{
    GENERATED_BODY()

public:
    ABlockRealmDayNight();

    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;

    /** Spawn a clock and bind the authored map lights immediately. */
    static ABlockRealmDayNight* Spawn(UWorld* World, const FTransform& Transform = FTransform::Identity);

    /** Advance the normalized clock by real seconds. Safe to call from a manager or test. */
    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Day Night")
    void Advance(float DeltaSeconds);

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Day Night")
    void SetTimeOfDay(float NewTimeOfDay);

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Day Night")
    void SetDayLengthSeconds(float NewDayLengthSeconds);

    UFUNCTION(BlueprintCallable, Category = "BlockRealm|Day Night")
    void SetAutoAdvance(bool bEnabled) { bAutoAdvance = bEnabled; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Day Night")
    float GetDayLengthSeconds() const { return DayLengthSeconds; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Day Night")
    float GetTimeOfDay() const { return TimeOfDay; }

    UFUNCTION(BlueprintPure, Category = "BlockRealm|Day Night")
    bool IsNight() const;

    /** Optional association for integration and verification code. */
    // Plain C++ hooks keep this actor usable without pulling BlockWorld into
    // every Blueprint header that includes the clock.
    void SetWorld(ABlockWorld* InWorld) { BlockWorld = InWorld; }

    ABlockWorld* GetBlockWorld() const { return BlockWorld; }

    /** Loaded authored collection for integration checks and per-world parameter inspection. */
    UMaterialParameterCollection* GetVoxelSkyCollection() const;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlockRealm|Day Night|Lighting")
    TObjectPtr<UDirectionalLightComponent> SunLight;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "BlockRealm|Day Night|Lighting")
    TObjectPtr<USkyLightComponent> SkyLight;

protected:
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night", meta = (ClampMin = "0.01", UIMin = "0.01"))
    float DayLengthSeconds = 900.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night")
    bool bAutoAdvance = true;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night", meta = (ClampMin = "0.0", ClampMax = "1.0"))
    float TimeOfDay = 0.32f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night|Lighting", meta = (ClampMin = "0.0"))
    float DaySunIntensity = 32000.0f;

    /** Horizontal moonlight floor; low-angle twilight compensates for its shallower incidence. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night|Lighting", meta = (ClampMin = "1.0"))
    // With fixed manual exposure, moonlight illuminates exposed terrain;
    // local detail relies on actual placed world lights, never player fill.
    float NightSunIntensity = 7000.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night|Lighting", meta = (ClampMin = "0.0"))
    float DaySkyIntensity = 1.4f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night|Lighting", meta = (ClampMin = "0.0"))
    float NightSkyIntensity = 1.0f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night|Lighting")
    float SunYawOffsetDegrees = -75.2f;

    /** Keep the shared light above the horizon so its twilight floor reaches the ground. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night|Lighting", meta = (ClampMin = "1.0", ClampMax = "30.0"))
    float MinimumLightElevationDegrees = 18.0f;

    /** Negative altitude below this threshold is treated as night. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "BlockRealm|Day Night", meta = (ClampMin = "-1.0", ClampMax = "0.0"))
    float NightAltitudeThreshold = -0.08f;

    /** Authored asset reference is retained for cooking; runtime only updates its per-world instance. */
    UPROPERTY(EditDefaultsOnly, Category = "BlockRealm|Day Night|Lighting")
    TSoftObjectPtr<UMaterialParameterCollection> SkyAmbientCollection;

private:
    void BindMapLights();
    void BindSkyAmbientCollection();
    void UpdateSkyAmbient(float SkyDayWeight);
    bool CanAutoAdvance() const;
    void UpdateLighting();
    float GetSolarAltitude() const;
    float GetNightThreshold() const;
    static float NormalizeTime(float Value);

    UPROPERTY()
    TObjectPtr<ABlockWorld> BlockWorld;

    UPROPERTY(Transient)
    TObjectPtr<UMaterialParameterCollection> LoadedSkyAmbientCollection;

    bool bLoggedSkyAmbientWarning = false;
};
