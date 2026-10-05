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
    /** Inspect first, then append only the new controls/confirmation widgets to existing trees. */
    UFUNCTION(BlueprintCallable,CallInEditor,Category="DoughWorld|UI Authoring") static bool UpgradeGameplayUXAssets(bool bInspectOnly=true);
    /** Reparents only the existing settings content into a five-tab book; retains all WidgetTrees. */
    UFUNCTION(BlueprintCallable,CallInEditor,Category="DoughWorld|UI Authoring") static bool UpgradeSettingsBookAssets(bool bInspectOnly=true);
};
