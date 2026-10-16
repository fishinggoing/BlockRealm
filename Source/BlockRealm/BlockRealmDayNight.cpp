#include "BlockRealmDayNight.h"

#include "BlockRealmCharacter.h"
#include "BlockRealmGameMode.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/SkyLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialParameterCollection.h"
#include "Materials/MaterialParameterCollectionInstance.h"

namespace
{
    constexpr float TwoPi = 6.28318530717958647692f;
    constexpr float TwilightSolarAltitude = 0.08f;
    constexpr float FullDaySolarAltitude = 0.22f;
    // Start the sky ramp before the sun reaches the horizon. A fixed exposure
    // otherwise makes the low-angle moon/sun handover darker than midnight.
    constexpr float SkyDawnSolarAltitude = -0.35f;
    constexpr float DefaultSkyAmbientDayStrength = 1000.0f;
    constexpr float DefaultSkyAmbientNightStrength = 300.0f;
    const FName SkyAmbientStrengthName(TEXT("SkyAmbientStrength"));
    const FName SkyAmbientTintName(TEXT("SkyAmbientTint"));

    TAutoConsoleVariable<float> CVarSkyAmbientDayStrength(
        TEXT("r.BlockRealm.SkyAmbientDayStrength"), DefaultSkyAmbientDayStrength,
        TEXT("Voxel surface sky compensation at full day. 0 disables this endpoint; clamped to [0, 2000]."));
    TAutoConsoleVariable<float> CVarSkyAmbientNightStrength(
        TEXT("r.BlockRealm.SkyAmbientNightStrength"), DefaultSkyAmbientNightStrength,
        TEXT("Voxel surface sky compensation at full night. 0 disables this endpoint; clamped to [0, 2000]."));

    float SmoothWeight(float Value, float Minimum, float Maximum)
    {
        const float Weight = FMath::Clamp((Value - Minimum) / FMath::Max(Maximum - Minimum, 0.001f), 0.0f, 1.0f);
        return Weight * Weight * (3.0f - 2.0f * Weight);
    }

    float PositiveOrDefault(float Value, float Default)
    {
        return FMath::IsFinite(Value) && Value > 0.0f ? Value : Default;
    }

    float BoundedSkyAmbientStrength(float Value, float Default)
    {
        return FMath::IsFinite(Value) ? FMath::Clamp(Value, 0.0f, 2000.0f) : Default;
    }
}

ABlockRealmDayNight::ABlockRealmDayNight()
{
    PrimaryActorTick.bCanEverTick = true;
    SkyAmbientCollection = FSoftObjectPath(TEXT("/Game/BlockRealm/Materials/MPC_VoxelSky.MPC_VoxelSky"));
}

void ABlockRealmDayNight::BeginPlay()
{
    Super::BeginPlay();
    TimeOfDay = NormalizeTime(TimeOfDay);
    BindMapLights();
    BindSkyAmbientCollection();
    UpdateLighting();
}

void ABlockRealmDayNight::Tick(float DeltaSeconds)
{
    Super::Tick(DeltaSeconds);
    if (bAutoAdvance && CanAutoAdvance())
    {
        Advance(DeltaSeconds);
    }
    else
    {
        // Keep console comparison controls responsive while the clock is
        // frozen for snapshots or paused by a gameplay overlay.
        UpdateSkyAmbient(SmoothWeight(GetSolarAltitude(), SkyDawnSolarAltitude, FullDaySolarAltitude));
    }
}

bool ABlockRealmDayNight::CanAutoAdvance() const
{
    const ABlockRealmGameMode* Game = Cast<ABlockRealmGameMode>(UGameplayStatics::GetGameMode(this));
    if (!Game) return true;
    if (!Game->IsAdventureActive()) return false;
    const ABlockRealmCharacter* Player = Game->GetPlayerCharacter();
    return !Player || !Player->IsInventoryOpen();
}

void ABlockRealmDayNight::BindMapLights()
{
    UWorld* World = GetWorld();
    if (!World) return;

    bool bPreferredSun = false;
    for (TActorIterator<ADirectionalLight> It(World); It; ++It)
    {
        UDirectionalLightComponent* Candidate = Cast<UDirectionalLightComponent>(It->GetLightComponent());
        if (!IsValid(Candidate)) continue;
        const bool bPreferred = It->ActorHasTag(TEXT("BR_MorningSun"));
        if (!SunLight || (bPreferred && !bPreferredSun)
            || (bPreferred == bPreferredSun && Candidate->Intensity > SunLight->Intensity))
        {
            SunLight = Candidate;
            bPreferredSun = bPreferred;
        }
    }

    bool bPreferredSky = false;
    for (TActorIterator<ASkyLight> It(World); It; ++It)
    {
        USkyLightComponent* Candidate = It->GetLightComponent();
        if (!IsValid(Candidate)) continue;
        const bool bPreferred = It->ActorHasTag(TEXT("BR_CoolSkyFill"));
        if (!SkyLight || (bPreferred && !bPreferredSky)
            || (bPreferred == bPreferredSky && Candidate->Intensity > SkyLight->Intensity))
        {
            SkyLight = Candidate;
            bPreferredSky = bPreferred;
        }
    }

    if (SunLight)
    {
        SunLight->SetMobility(EComponentMobility::Movable);
        SunLight->SetVisibility(true);
        SunLight->SetAtmosphereSunLight(true);
        SunLight->SetAtmosphereSunLightIndex(0);
    }
    if (SkyLight)
    {
        SkyLight->SetMobility(EComponentMobility::Movable);
        SkyLight->SetVisibility(true);
        SkyLight->SetRealTimeCaptureEnabled(true);
    }
    UE_LOG(LogTemp, Display, TEXT("BR_DAYNIGHT bound authored lights sun=%s sky=%s"),
        SunLight ? *SunLight->GetPathName() : TEXT("missing"), SkyLight ? *SkyLight->GetPathName() : TEXT("missing"));
    if (!SunLight || !SkyLight)
    {
        UE_LOG(LogTemp, Warning, TEXT("BR_DAYNIGHT requires an authored DirectionalLight and SkyLight in the map"));
    }
}

void ABlockRealmDayNight::BindSkyAmbientCollection()
{
    LoadedSkyAmbientCollection = SkyAmbientCollection.LoadSynchronous();
    if (!LoadedSkyAmbientCollection)
    {
        UE_LOG(LogTemp, Warning, TEXT("BR_SKY_AMBIENT missing authored collection %s; surface compensation unavailable"),
            *SkyAmbientCollection.ToSoftObjectPath().ToString());
        bLoggedSkyAmbientWarning = true;
        return;
    }
    UE_LOG(LogTemp, Display, TEXT("BR_SKY_AMBIENT bound %s day=%.1f night=%.1f (surface compatibility compensation)"),
        *LoadedSkyAmbientCollection->GetPathName(), DefaultSkyAmbientDayStrength, DefaultSkyAmbientNightStrength);
}

UMaterialParameterCollection* ABlockRealmDayNight::GetVoxelSkyCollection() const
{
    return LoadedSkyAmbientCollection.Get();
}

void ABlockRealmDayNight::UpdateSkyAmbient(float SkyDayWeight)
{
    UWorld* World = GetWorld();
    if (!World || !LoadedSkyAmbientCollection) return;
    UMaterialParameterCollectionInstance* Instance = World->GetParameterCollectionInstance(LoadedSkyAmbientCollection);
    if (!IsValid(Instance))
    {
        if (!bLoggedSkyAmbientWarning)
        {
            UE_LOG(LogTemp, Warning, TEXT("BR_SKY_AMBIENT world collection instance unavailable"));
            bLoggedSkyAmbientWarning = true;
        }
        return;
    }

    const float DayStrength = BoundedSkyAmbientStrength(CVarSkyAmbientDayStrength.GetValueOnGameThread(), DefaultSkyAmbientDayStrength);
    const float NightStrength = BoundedSkyAmbientStrength(CVarSkyAmbientNightStrength.GetValueOnGameThread(), DefaultSkyAmbientNightStrength);
    const float SafeDayWeight = FMath::IsFinite(SkyDayWeight) ? FMath::Clamp(SkyDayWeight, 0.0f, 1.0f) : 1.0f;
    const float Strength = FMath::Lerp(NightStrength, DayStrength, SafeDayWeight);
    const FLinearColor Tint = FMath::Lerp(FLinearColor(0.45f, 0.58f, 0.86f),
        FLinearColor(0.72f, 0.82f, 1.0f), SafeDayWeight);
    const bool bStrengthFound = Instance->SetScalarParameterValue(SkyAmbientStrengthName, Strength);
    const bool bTintFound = Instance->SetVectorParameterValue(SkyAmbientTintName, Tint);
    if ((!bStrengthFound || !bTintFound) && !bLoggedSkyAmbientWarning)
    {
        UE_LOG(LogTemp, Warning, TEXT("BR_SKY_AMBIENT collection requires scalar SkyAmbientStrength and vector SkyAmbientTint"));
        bLoggedSkyAmbientWarning = true;
    }
}

ABlockRealmDayNight* ABlockRealmDayNight::Spawn(UWorld* World, const FTransform& Transform)
{
    if (!World)
    {
        return nullptr;
    }

    FActorSpawnParameters Parameters;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    return World->SpawnActor<ABlockRealmDayNight>(StaticClass(), Transform, Parameters);
}

void ABlockRealmDayNight::Advance(float DeltaSeconds)
{
    if (!FMath::IsFinite(DeltaSeconds) || DeltaSeconds == 0.0f)
    {
        return;
    }

    const float SafeDayLength = FMath::Max(PositiveOrDefault(DayLengthSeconds, 900.0f), 0.01f);
    TimeOfDay = NormalizeTime(TimeOfDay + DeltaSeconds / SafeDayLength);
    UpdateLighting();
}

void ABlockRealmDayNight::SetTimeOfDay(float NewTimeOfDay)
{
    if (!FMath::IsFinite(NewTimeOfDay))
    {
        return;
    }

    TimeOfDay = NormalizeTime(NewTimeOfDay);
    UpdateLighting();
}

void ABlockRealmDayNight::SetDayLengthSeconds(float NewDayLengthSeconds)
{
    if (FMath::IsFinite(NewDayLengthSeconds) && NewDayLengthSeconds > 0.0f)
    {
        DayLengthSeconds = FMath::Max(NewDayLengthSeconds, 0.01f);
    }
}

bool ABlockRealmDayNight::IsNight() const
{
    return GetSolarAltitude() <= GetNightThreshold();
}

float ABlockRealmDayNight::GetSolarAltitude() const
{
    // Shift the sine so 0.00/1.00 is midnight and 0.50 is solar noon.
    return FMath::Sin((NormalizeTime(TimeOfDay) - 0.25f) * TwoPi);
}

float ABlockRealmDayNight::GetNightThreshold() const
{
    return FMath::IsFinite(NightAltitudeThreshold)
        ? FMath::Clamp(NightAltitudeThreshold, -1.0f, 0.0f) : -0.08f;
}

float ABlockRealmDayNight::NormalizeTime(float Value)
{
    if (!FMath::IsFinite(Value)) return 0.32f;
    float Normalized = FMath::Fmod(Value, 1.0f);
    if (Normalized < 0.0f)
    {
        Normalized += 1.0f;
    }
    // Fmod can return 1 due to floating point roundoff at the upper edge.
    return FMath::Min(Normalized, 0.99999994f);
}

void ABlockRealmDayNight::UpdateLighting()
{
    const float Altitude = GetSolarAltitude();
    const float Phase = NormalizeTime(TimeOfDay);
    const float NightThreshold = GetNightThreshold();
    const float DayWeight = SmoothWeight(Altitude, 0.0f, FullDaySolarAltitude);
    const float SkyDayWeight = SmoothWeight(Altitude, SkyDawnSolarAltitude, FullDaySolarAltitude);
    UpdateSkyAmbient(SkyDayWeight);
    // Multiplying separate sun/moon weights made both vanish at sunrise/sunset.
    // The existing moon strength is now a finite floor for the one bound source.
    // Invalid authored values fall back to the playable defaults; night stays dimmer.
    const float DayLux = FMath::Max(PositiveOrDefault(DaySunIntensity, 32000.0f), 4.0f);
    const float NightLux = FMath::Min(PositiveOrDefault(NightSunIntensity, 7000.0f), DayLux * 0.5f);
    const float DaySky = FMath::Max(PositiveOrDefault(DaySkyIntensity, 1.4f), 0.002f);
    const float NightSky = FMath::Min(PositiveOrDefault(NightSkyIntensity, 1.0f), DaySky * 0.75f);
    const float SkyIntensity = FMath::Lerp(NightSky, DaySky, SkyDayWeight);

    if (SunLight)
    {
        // Turn the same source through twilight instead of snapping its yaw by
        // 180 degrees at a now-lit horizon. IsNight's threshold is the end of the
        // turn: whenever IsNight is true, the source is fully in moon direction.
        const float MoonDirectionWeight = 1.0f - SmoothWeight(Altitude, NightThreshold, TwilightSolarAltitude);
        const float YawOffset = FMath::IsFinite(SunYawOffsetDegrees)
            ? FMath::Fmod(SunYawOffsetDegrees, 360.0f) : -75.2f;
        const float Yaw = YawOffset + Phase * 360.0f + MoonDirectionWeight * 180.0f;
        const float Elevation = FMath::Abs(FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(Altitude, -1.0f, 1.0f))));
        const float MinimumElevation = FMath::Clamp(PositiveOrDefault(MinimumLightElevationDegrees, 18.0f), 1.0f, 30.0f);
        // A smooth floor avoids horizontal rays and the hard derivative of Max.
        // Above twice the minimum elevation, retain the authored solar path.
        const float LitElevation = FMath::Lerp(MinimumElevation, Elevation,
            SmoothWeight(Elevation, MinimumElevation, MinimumElevation * 2.0f));
        // A 7000-lux source at 18 degrees delivers only 2163 lux to a horizontal
        // block, versus 7000 at midnight's zenith. Keep the nighttime horizontal
        // floor through the handover, bounded by the full daytime intensity.
        // This changes irradiance, not occlusion: shadow maps still block it.
        const float HorizontalCosine = FMath::Max(FMath::Sin(FMath::DegreesToRadians(LitElevation)), 0.01f);
        const float LowAngleCompensation = FMath::Min(1.0f / HorizontalCosine, 3.25f);
        const float SunIntensity = FMath::Min(DayLux,
            FMath::Lerp(NightLux * LowAngleCompensation, DayLux, DayWeight));
        // UE light forward follows the rays: negative pitch illuminates the ground.
        SunLight->SetWorldRotation(FRotator(-LitElevation, Yaw, 0.0f));
        SunLight->SetIntensity(SunIntensity);
        const float HorizonWarmth = SmoothWeight(Altitude, 0.0f, TwilightSolarAltitude);
        // Sunrise stays cool while the sunset edge gains a restrained warm
        // tint.  The altitude gate prevents the hue from leaking into the
        // full night and the daytime blend below still returns to white.
        const float DuskWarmth = SmoothWeight(Phase, 0.58f, 0.75f)
            * SmoothWeight(Altitude, NightThreshold, TwilightSolarAltitude);
        const float TwilightWarmth = FMath::Clamp(FMath::Max(HorizonWarmth, DuskWarmth), 0.0f, 1.0f);
        const FLinearColor TwilightColor = FMath::Lerp(FLinearColor(0.56f, 0.66f, 1.0f),
            FLinearColor(1.0f, 0.60f, 0.37f), TwilightWarmth);
        const FLinearColor SunColor = FMath::Lerp(TwilightColor, FLinearColor(1.0f, 0.96f, 0.90f),
            SmoothWeight(Altitude, TwilightSolarAltitude, FullDaySolarAltitude));
        SunLight->SetLightColor(SunColor);
    }
    if (SkyLight)
    {
        SkyLight->SetIntensity(SkyIntensity);
        SkyLight->SetLightColor(FLinearColor::LerpUsingHSV(
            FLinearColor(0.45f, 0.58f, 0.86f), FLinearColor(0.95f, 0.97f, 1.0f), SkyDayWeight));
    }
}
