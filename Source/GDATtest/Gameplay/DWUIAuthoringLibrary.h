#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWUIAuthoringLibrary.generated.h"
/** Editor authoring only: builds real, saved UMG Designer trees, never a Slate wrapper. */
UCLASS()
class GDATTEST_API UDWUIAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,CallInEditor,Category="DoughWorld|UI Authoring") static bool CreatePrototypeUIAssets(bool bReplaceExisting=false);
    UFUNCTION(BlueprintCallable,CallInEditor,Category="DoughWorld|UI Authoring") static bool UpgradeMenuSettingsAssets();
    UFUNCTION(BlueprintPure,Category="DoughWorld|UI Authoring") static FString GetLastBuildReport();
    /** Repairs missing serializable font references only; preserves Designer layout and custom fonts. */
    UFUNCTION(BlueprintCallable,CallInEditor,Category="DoughWorld|UI Authoring") static bool RepairPrototypeFonts();
};
