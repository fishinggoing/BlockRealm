#include "BlockRealmPlayerController.h"

#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

bool ABlockRealmPlayerController::InputKey(const FInputKeyEventArgs& Params)
{
    static const bool bAutomatedValidation = FParse::Param(FCommandLine::Get(), TEXT("BlockRealmSmoke"));
    if (bAutomatedValidation && !Params.IsSimulatedInput()) return true;
    return Super::InputKey(Params);
}
