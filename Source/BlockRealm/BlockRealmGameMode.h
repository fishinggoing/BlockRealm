#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "BlockTypes.h"
#include "BlockRealmGameMode.generated.h"

class ABlockWorld;
class ABlockRealmCharacter;
class ABlockRealmDayNight;
class ABlockRealmMob;
class ABlockRealmCreaturePopulation;
class ABlockRealmRestPoint;
struct FBlockRealmExtendedSmokeState;
struct FBlockRealmExpansionSmokeState;
struct FBlockRealmV11SmokeState;

UCLASS()
class BLOCKREALM_API ABlockRealmGameMode : public AGameModeBase
{
    GENERATED_BODY()
public:
    ABlockRealmGameMode();
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
    virtual bool AllowCheats(APlayerController* Player) override { return false; }
    ABlockWorld* GetWorldActor() const { return WorldActor; }
    ABlockRealmCharacter* GetPlayerCharacter() const;
    bool IsMenuOpen() const { return bTitleOpen || bPauseOpen; }
    bool IsPausedMenu() const { return bPauseOpen; }
    bool IsAdventureActive() const { return !bTitleOpen && !bPauseOpen; }
    void StartAdventure();
    void TogglePauseMenu();
    bool SaveAdventure();
    void ReturnToTitle();
    void QuitAdventure();
    void SetUpscaleMode(int32 Mode);
    int32 GetUpscaleMode() const { return UpscaleMode; }
    FString GetGraphicsStatus() const;
    FString GetNotice() const { return Notice; }
    bool IsNight() const;
    float GetTimeOfDay() const;
    int32 GetActiveMobCount() const;
    int32 GetActiveAnimalCount() const;
    ABlockRealmCreaturePopulation* GetCreaturePopulation() const { return CreaturePopulation; }
    bool TryRestAt(ABlockRealmRestPoint* RestPoint);
    FVector GetAdventureRespawnLocation() const;
    bool HasRespawnPoint() const { return bHasRespawnPoint; }
    FIntVector GetRespawnPoint() const { return RespawnPoint; }
private:
    void SetMenuInput(bool bMenu);
    void ConfigureLightingPostProcess();
    void TickSmoke(float DeltaSeconds);
    bool TickInteractionSmoke();
    bool TickExtendedSmoke();
    bool TickExpansionSmoke();
    bool TickV11Smoke();
    void FinishSmoke();
    void TickNightMobs(float DeltaSeconds);
    void TickLightingProbe(float DeltaSeconds);
    UPROPERTY() TObjectPtr<ABlockWorld> WorldActor;
    UPROPERTY() TObjectPtr<ABlockRealmDayNight> DayNightActor;
    UPROPERTY() TObjectPtr<ABlockRealmCreaturePopulation> CreaturePopulation;
    UPROPERTY() TArray<TObjectPtr<ABlockRealmMob>> ActiveMobs;
    bool bTitleOpen = true;
    bool bPauseOpen = false;
    bool bStarted = false;
    bool bStartPrepared = false;
    bool bRestoreOnStart = false;
    bool bPendingStart = false;
    FWorldSaveState PreparedState;
    uint8 PausedMovementMode = 1;
    uint8 PausedCustomMode = 0;
    int32 UpscaleMode = 0;
    FString Notice;
    bool bSmoke = false;
    int32 SmokeStage = 0;
    bool bSmokeSelfTestsDone = false;
    bool bSmokeMiningInputSent = false;
    bool bSmokePauseShotDone = false;
    bool bSmokeGraphicsShotDone = false;
    bool bSmokeExplorationPrepared = false;
    int32 SmokeGraphicsStep = 0;
    float SmokePreviousPercentage = 100.f;
    float SmokeTime = 0.f;
    TArray<FString> SmokeFailures;
    TArray<FString> SmokeChecks;
    FIntVector SmokePosition = FIntVector::ZeroValue;
    FVector SmokeStartPosition = FVector::ZeroVector;
    FVector SmokeStreamingPosition = FVector::ZeroVector;
    FIntPoint SmokeOldChunk = FIntPoint::ZeroValue;
    uint8 SmokeBlockType = 0;
    int32 SmokeInventoryCount = 0;
    float SmokeElapsed = 0.f;
    TArray<float> SmokeFrameMs;
    int32 SmokeInteractionStage = 0;
    bool bSmokeInteractionComplete = false;
    bool bSmokeExtendedComplete = false;
    bool bSmokeExpansionComplete = false;
    FIntVector SmokeFixtureRoot = FIntVector::ZeroValue;
    FIntVector SmokeMiningBlock = FIntVector::ZeroValue;
    FIntVector SmokeWaterBlock = FIntVector::ZeroValue;
    FVector SmokeWaterPosition = FVector::ZeroVector;
    float SmokeLandSpeed = 0.f;
    float SmokeWaterSpeed = 0.f;
    float SmokeMiningDuration = 0.f;
    float SmokeShoreStartTime = 0.f;
    float SmokeMiningStartTime = 0.f;
    float NightMobSpawnAccumulator = 0.f;
    TSharedPtr<FBlockRealmExtendedSmokeState> ExtendedSmokeState;
    TSharedPtr<FBlockRealmExpansionSmokeState> ExpansionSmokeState;
    TSharedPtr<FBlockRealmV11SmokeState> V11SmokeState;
    bool bHasRespawnPoint = false;
    FIntVector RespawnPoint = FIntVector::ZeroValue;
    bool bLightingProbe = false;
    bool bLightingProbeTeleported = false;
    bool bLightingProbeShot = false;
    float LightingProbeElapsed = 0.f;
    FRotator LightingProbeRotation = FRotator::ZeroRotator;
    FIntVector LightingProbeCell = FIntVector::ZeroValue;
    bool bLightingProbeFixtureReady = false;
    FString LightingProbeError;
    TArray<float> LightingProbeFrameMs;
};
