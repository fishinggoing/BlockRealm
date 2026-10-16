#pragma once

#include "CoreMinimal.h"

/** Runtime upscaler settings. Call ApplyMode on the game thread after startup. */
struct BLOCKREALM_API FBlockRenderSettings
{
	/** Select the active desktop's physical borderless size once; explicit launch display flags win. */
	static void ApplyDisplayDefaults();

	/** Uses the official DLSS runtime query, including hardware and driver checks. */
	static bool IsDLSSSupported();

	/** 0 = native-resolution TSR (default), 1 = DLSS Quality, 2 = Balanced, 3 = Performance. */
	static void ApplyMode(int32 Mode);

	/** Reports the actual selected upscaler and any DLSS availability/fallback reason. */
	static FString GetStatus(int32 Mode);
};
