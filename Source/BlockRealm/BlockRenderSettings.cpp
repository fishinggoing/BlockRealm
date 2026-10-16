#include "BlockRenderSettings.h"

#include "DLSSLibrary.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/GameUserSettings.h"
#include "GenericPlatform/GenericApplication.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Modules/ModuleManager.h"
#include "RHI.h"
#include "StreamlineLibrary.h"
#include "StreamlineLibraryDLSSG.h"
#include "StreamlineLibraryReflex.h"
#include "UnrealClient.h"

DEFINE_LOG_CATEGORY_STATIC(LogBlockRenderSettings, Log, All);

namespace
{
	int32 AppliedMode = 0;
	float AppliedScreenPercentage = 100.0f;
	FString LastFallbackReason;
	bool bDisplayDefaultsApplied = false;

	bool IsDLSSInitialized()
	{
		// DLSS loads at PostEngineInit. Never force it to initialize against an absent RHI.
		return FModuleManager::Get().IsModuleLoaded(TEXT("DLSS"));
	}

	bool IsStreamlineInitialized()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("StreamlineRHI"));
	}

	bool SetConsoleInt(const TCHAR* Name, int32 Value)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		if (!Variable)
		{
			UE_LOG(LogBlockRenderSettings, Warning, TEXT("Missing setting: %s"), Name);
			return false;
		}
		// Match EnableDLSS's priority so its API can update the shared temporal CVars.
		Variable->Set(Value, ECVF_SetByCommandline);
		return Variable->GetInt() == Value;
	}

	bool SetScreenPercentage(float Value)
	{
		IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		if (!Variable)
		{
			return false;
		}
		Variable->Set(Value, ECVF_SetByCommandline);
		return FMath::IsNearlyEqual(Variable->GetFloat(), Value, 0.05f);
	}

	float GetScreenPercentage()
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
		return Variable ? Variable->GetFloat() : 100.0f;
	}

	int32 GetConsoleInt(const TCHAR* Name)
	{
		const IConsoleVariable* Variable = IConsoleManager::Get().FindConsoleVariable(Name);
		return Variable ? Variable->GetInt() : -1;
	}

	UDLSSMode ToDLSSMode(int32 Mode)
	{
		switch (Mode)
		{
		case 1: return UDLSSMode::Quality;
		case 2: return UDLSSMode::Balanced;
		case 3: return UDLSSMode::Performance;
		default: return UDLSSMode::Off;
		}
	}

	const TCHAR* GetModeLabel(int32 Mode)
	{
		switch (Mode)
		{
		case 1: return TEXT("DLSS Quality");
		case 2: return TEXT("DLSS Balanced");
		case 3: return TEXT("DLSS Performance");
		default: return TEXT("TSR");
		}
	}

	FString GetDLSSUnavailableReason()
	{
		if (!IsDLSSInitialized())
		{
			return TEXT("DLSS 插件尚未初始化");
		}

		switch (UDLSSLibrary::QueryDLSSSupport())
		{
		case UDLSSSupport::Supported:
			return FString();
		case UDLSSSupport::NotSupportedIncompatibleHardware:
			return TEXT("当前显卡不支持 DLSS，需要 NVIDIA RTX");
		case UDLSSSupport::NotSupportedDriverOutOfDate:
		{
			int32 Major = 0;
			int32 Minor = 0;
			UDLSSLibrary::GetDLSSMinimumDriverVersion(Major, Minor);
			const FString Driver = GRHIAdapterUserDriverVersion.IsEmpty() ? TEXT("未识别") : GRHIAdapterUserDriverVersion;
			return Major > 0
				? FString::Printf(TEXT("驱动过旧：当前 %s，DLSS 至少需要 %d.%02d"), *Driver, Major, Minor)
				: FString::Printf(TEXT("驱动过旧：当前 %s，插件未报告最低版本"), *Driver);
		}
		case UDLSSSupport::NotSupportedOperatingSystemOutOfDate:
			return TEXT("系统过旧：DLSS 需要 Windows 10 1709 或更新版本");
		case UDLSSSupport::NotSupportedByPlatformAtBuildTime:
			return TEXT("此构建平台未编入 DLSS 支持");
		case UDLSSSupport::NotSupportedIncompatibleAPICaptureToolActive:
			return TEXT("图形捕获工具与 DLSS 不兼容，请关闭后重启");
		case UDLSSSupport::NotSupported:
		default:
			return TEXT("DLSS 初始化失败，请查看 LogDLSS/LogNGX（显卡、RHI 或 NGX）");
		}
	}

	void ApplyTSR()
	{
		if (IsDLSSInitialized())
		{
			UDLSSLibrary::EnableDLSSRR(false);
			UDLSSLibrary::EnableDLSS(false);
		}
		const bool bAASet = SetConsoleInt(TEXT("r.AntiAliasingMethod"), 4);
		const bool bUpscalerSet = SetConsoleInt(TEXT("r.TemporalAA.Upscaler"), 1);
		const bool bUpsamplingSet = SetConsoleInt(TEXT("r.TemporalAA.Upsampling"), 1);
		const bool bPercentageSet = SetScreenPercentage(100.0f);
		AppliedMode = 0;
		AppliedScreenPercentage = 100.0f;
		if (!bAASet || !bUpscalerSet || !bUpsamplingSet || !bPercentageSet)
		{
			LastFallbackReason += TEXT(" TSR 配置未完全生效，请查看日志");
		}
	}

	void ConfigureLatencyAndFrameGeneration()
	{
		// SR validation uses rendered frames only. The Off mode also clears MFG modes.
		UStreamlineLibraryDLSSG::SetDLSSGMode(EStreamlineDLSSGMode::Off);
		if (IsStreamlineInitialized())
		{
			const bool bReflexSupported = UStreamlineLibraryReflex::QueryReflexSupport() == EStreamlineFeatureSupport::Supported;
			UStreamlineLibraryReflex::SetReflexMode(bReflexSupported ? EStreamlineReflexMode::Enabled : EStreamlineReflexMode::Off);
		}
	}
}

void FBlockRenderSettings::ApplyDisplayDefaults()
{
	check(IsInGameThread());
	if (bDisplayDefaultsApplied || !GEngine || !FApp::CanEverRender()) return;
	// A PIE viewport belongs to the editor, not to the game's display settings.
	const TCHAR* CommandLine = FCommandLine::Get();
	if (GIsEditor && !FParse::Param(CommandLine, TEXT("game"))) return;
	UGameUserSettings* Settings = GEngine->GetGameUserSettings();
	if (!Settings) return;
	bDisplayDefaultsApplied = true;

	int32 ExplicitDimension = 0;
	const bool bExplicitDisplay = FParse::Value(CommandLine, TEXT("ResX="), ExplicitDimension)
		|| FParse::Value(CommandLine, TEXT("ResY="), ExplicitDimension)
		|| FParse::Param(CommandLine, TEXT("windowed"))
		|| FParse::Param(CommandLine, TEXT("window"))
		|| FParse::Param(CommandLine, TEXT("fullscreen"));
	if (bExplicitDisplay)
	{
		UE_LOG(LogBlockRenderSettings, Log, TEXT("Display defaults: preserving explicit launch resolution/window flags"));
		return;
	}

	FDisplayMetrics DisplayMetrics;
	FDisplayMetrics::RebuildDisplayMetrics(DisplayMetrics);
	FIntPoint DesktopResolution(DisplayMetrics.PrimaryDisplayWidth, DisplayMetrics.PrimaryDisplayHeight);
	for (const FMonitorInfo& Monitor : DisplayMetrics.MonitorInfo)
	{
		// Borderless uses the ACTIVE desktop mode, not the panel's preferred mode.
		// For example, a 2560x1600 panel can run a 2560x1440 desktop. The early
		// HighDPI/unattended config makes DisplayRect physical pixels before any
		// game window is created; changing process DPI here would be too late.
		const FIntPoint ActiveSize(Monitor.DisplayRect.Right - Monitor.DisplayRect.Left,
			Monitor.DisplayRect.Bottom - Monitor.DisplayRect.Top);
		if (Monitor.bIsPrimary && ActiveSize.X > 0 && ActiveSize.Y > 0)
		{
			DesktopResolution = ActiveSize;
			break;
		}
	}
	if (DesktopResolution.X <= 0 || DesktopResolution.Y <= 0) return;
	Settings->SetFullscreenMode(EWindowMode::WindowedFullscreen);
	Settings->SetScreenResolution(DesktopResolution);
	Settings->SetDynamicResolutionEnabled(false);
	Settings->SetResolutionScaleValueEx(100.0f);
	Settings->ApplyResolutionSettings(true);
	Settings->ConfirmVideoMode();
	Settings->SaveSettings();
	const FIntPoint CurrentViewport = GEngine->GameViewport && GEngine->GameViewport->Viewport
		? GEngine->GameViewport->Viewport->GetSizeXY() : FIntPoint::ZeroValue;
	UE_LOG(LogBlockRenderSettings, Log,
		TEXT("Display defaults: requested active desktop %dx%d borderless; current viewport=%dx%d (resize may be deferred); render scale 100%%"),
		DesktopResolution.X, DesktopResolution.Y, CurrentViewport.X, CurrentViewport.Y);
}

bool FBlockRenderSettings::IsDLSSSupported()
{
	return IsDLSSInitialized() && UDLSSLibrary::QueryDLSSSupport() == UDLSSSupport::Supported;
}

void FBlockRenderSettings::ApplyMode(int32 Mode)
{
	check(IsInGameThread());
	LastFallbackReason.Reset();
	ConfigureLatencyAndFrameGeneration();
	// A moving resolution fraction would change the selected DLSS quality preset.
	SetConsoleInt(TEXT("r.DynamicRes.OperationMode"), 0);
	SetConsoleInt(TEXT("r.ScreenPercentage.Default.Desktop.Mode"), 0);
	SetConsoleInt(TEXT("r.SecondaryScreenPercentage.GameViewport"), 100);
	// Keep pixel edges readable while moving and disable cinematic lens blur.
	SetConsoleInt(TEXT("r.MotionBlurQuality"), 0);
	SetConsoleInt(TEXT("r.DepthOfFieldQuality"), 0);
	if (IConsoleVariable* NIS = IConsoleManager::Get().FindConsoleVariable(TEXT("r.NIS.Enable")))
	{
		NIS->Set(0, ECVF_SetByCommandline);
	}

	if (Mode < 0 || Mode > 3)
	{
		LastFallbackReason = TEXT("无效的画质模式，已回退 TSR");
		ApplyTSR();
	}
	else if (Mode == 0)
	{
		ApplyTSR();
	}
	else if (!IsDLSSSupported())
	{
		LastFallbackReason = GetDLSSUnavailableReason();
		ApplyTSR();
	}
	else
	{
		const UDLSSMode Quality = ToDLSSMode(Mode);
		bool bModeSupported = false;
		float OptimalPercentage = 0.0f;
		bool bFixedPercentage = false;
		float MinPercentage = 0.0f;
		float MaxPercentage = 0.0f;
		float UnusedSharpness = 0.0f;
		// Explicit modes have resolution-independent optimal fractions in this API.
		UDLSSLibrary::GetDLSSModeInformation(Quality, FVector2D::ZeroVector, bModeSupported,
			OptimalPercentage, bFixedPercentage, MinPercentage, MaxPercentage, UnusedSharpness);

		if (!bModeSupported || !FMath::IsFinite(OptimalPercentage) || OptimalPercentage <= 0.0f
			|| OptimalPercentage >= 100.0f || OptimalPercentage < MinPercentage || OptimalPercentage > MaxPercentage)
		{
			LastFallbackReason = FString::Printf(TEXT("官方插件未提供有效的 %s 超分辨率模式"), GetModeLabel(Mode));
			ApplyTSR();
		}
		else
		{
			// TAA is the engine's temporal route; DLSS replaces its upscaler. TSR is restored on fallback.
			const bool bTemporalAASet = SetConsoleInt(TEXT("r.AntiAliasingMethod"), 2);
			const bool bUpscalerSet = SetConsoleInt(TEXT("r.TemporalAA.Upscaler"), 1);
			const bool bUpsamplingSet = SetConsoleInt(TEXT("r.TemporalAA.Upsampling"), 1);
			const bool bPercentageSet = SetScreenPercentage(OptimalPercentage);
			UDLSSLibrary::EnableDLSSRR(false);
			UDLSSLibrary::EnableDLSS(bTemporalAASet && bUpscalerSet && bUpsamplingSet && bPercentageSet);
			if (bTemporalAASet && bUpscalerSet && bUpsamplingSet && bPercentageSet && UDLSSLibrary::IsDLSSEnabled())
			{
				AppliedMode = Mode;
				AppliedScreenPercentage = OptimalPercentage;
			}
			else
			{
				LastFallbackReason = TEXT("DLSS 画质设置未生效，已回退 TSR");
				ApplyTSR();
			}
		}
	}

	const UDLSSSupport Support = IsDLSSInitialized() ? UDLSSLibrary::QueryDLSSSupport() : UDLSSSupport::NotSupported;
	const FString ReflexSupport = IsStreamlineInitialized()
		? StaticEnum<EStreamlineFeatureSupport>()->GetNameStringByValue(static_cast<int64>(UStreamlineLibraryReflex::QueryReflexSupport()))
		: TEXT("NotInitialized");
	const FString FrameGenerationMode = StaticEnum<EStreamlineDLSSGMode>()->GetNameStringByValue(
		static_cast<int64>(UStreamlineLibraryDLSSG::GetDLSSGMode()));
	UE_LOG(LogBlockRenderSettings, Log,
		TEXT("RequestedMode=%d AppliedMode=%d DLSSSupport=%s GPU=%s Driver=%s ScreenPercentage=%.3f ReflexSupport=%s FG=%s Status=%s"),
		Mode, AppliedMode, *StaticEnum<UDLSSSupport>()->GetNameStringByValue(static_cast<int64>(Support)),
		*GRHIAdapterName, *GRHIAdapterUserDriverVersion, GetScreenPercentage(), *ReflexSupport, *FrameGenerationMode, *GetStatus(Mode));
}

FString FBlockRenderSettings::GetStatus(int32 Mode)
{
	const bool bSupported = IsDLSSSupported();
	const bool bDLSSEnabled = bSupported && UDLSSLibrary::IsDLSSEnabled();
	const int32 AAMethod = GetConsoleInt(TEXT("r.AntiAliasingMethod"));
	const bool bTemporalPath = (AAMethod == 2 || AAMethod == 4)
		&& GetConsoleInt(TEXT("r.TemporalAA.Upscaler")) == 1
		&& GetConsoleInt(TEXT("r.TemporalAA.Upsampling")) == 1;
	const float ScreenPercentage = GetScreenPercentage();
	FString Status;
	if (bDLSSEnabled && bTemporalPath)
	{
		const bool bKnownQuality = AppliedMode > 0 && FMath::IsNearlyEqual(ScreenPercentage, AppliedScreenPercentage, 0.1f);
		Status = FString::Printf(TEXT("%s · %.1f%%"), bKnownQuality ? GetModeLabel(AppliedMode) : TEXT("DLSS-SR"), ScreenPercentage);
	}
	else
	{
		Status = FString::Printf(TEXT("%s · %.1f%%"), AAMethod == 4 ? TEXT("TSR") : (AAMethod == 2 ? TEXT("TAA") : TEXT("抗锯齿未配置")), ScreenPercentage);
		if (!LastFallbackReason.IsEmpty())
		{
			Status += TEXT(" | ") + LastFallbackReason;
		}
		else if (!bSupported)
		{
			Status += TEXT(" | ") + GetDLSSUnavailableReason();
		}
		else if (bDLSSEnabled && !bTemporalPath)
		{
			Status += TEXT(" | DLSS 时域渲染设置未生效");
		}
		else if (Mode > 0)
		{
			Status += TEXT(" | DLSS 已关闭");
		}
	}
	if (GEngine && GEngine->GameViewport && GEngine->GameViewport->Viewport)
	{
		const FIntPoint Output = GEngine->GameViewport->Viewport->GetSizeXY();
		if (Output.X > 0 && Output.Y > 0)
		{
			const FIntPoint Internal(FMath::RoundToInt(Output.X * ScreenPercentage / 100.0f),
				FMath::RoundToInt(Output.Y * ScreenPercentage / 100.0f));
			Status += Internal == Output
				? FString::Printf(TEXT(" | %d×%d 原生"), Output.X, Output.Y)
				: FString::Printf(TEXT(" | %d×%d → %d×%d"), Internal.X, Internal.Y, Output.X, Output.Y);
		}
	}

	const bool bReflexEnabled = IsStreamlineInitialized()
		&& UStreamlineLibraryReflex::QueryReflexSupport() == EStreamlineFeatureSupport::Supported
		&& UStreamlineLibraryReflex::GetReflexMode() != EStreamlineReflexMode::Off;
	Status += bReflexEnabled ? TEXT(" | Reflex 开") : TEXT(" | Reflex 关");
	Status += UStreamlineLibraryDLSSG::GetDLSSGMode() == EStreamlineDLSSGMode::Off ? TEXT(" | 帧生成 关") : TEXT(" | 帧生成 开");
	return Status;
}
