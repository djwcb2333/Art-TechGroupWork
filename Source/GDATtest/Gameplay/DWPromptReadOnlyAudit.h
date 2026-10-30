#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWPromptReadOnlyAudit.generated.h"
class UTexture2D;

/** Editor-only authored-tree inspection. Does not create widgets, change, compile or save assets. */
UCLASS()
class GDATTEST_API UDWPromptReadOnlyAudit : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Diagnostics")
    static FString InspectInteractionPrompt();
    /** Opt-in editor-only probe; refuses unsafe authored trees and never creates a game world. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Diagnostics")
    static FString InspectTransientPromptLayout(UTexture2D* Icon);
};
