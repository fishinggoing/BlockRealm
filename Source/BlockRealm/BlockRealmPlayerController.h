#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "BlockRealmPlayerController.generated.h"

// Normal play uses the engine input path. Automated validation only accepts
// explicitly simulated events, so desktop activity cannot move its fixtures.
UCLASS()
class BLOCKREALM_API ABlockRealmPlayerController : public APlayerController
{
    GENERATED_BODY()
public:
    virtual bool InputKey(const FInputKeyEventArgs& Params) override;
};
