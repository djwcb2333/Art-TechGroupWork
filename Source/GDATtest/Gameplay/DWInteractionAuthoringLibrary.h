#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWInteractionAuthoringLibrary.generated.h"
UCLASS()
class GDATTEST_API UDWInteractionAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Interaction Prompt|Authoring") static bool CreateInteractionWidget(bool bReplaceExisting=false);
    UFUNCTION(BlueprintPure,Category="Interaction Prompt|Authoring") static FString GetLastBuildReport();
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Interaction Prompt|Authoring") static bool CreateHandDrawnCompositeFont(const FString& ChineseFontFacePath,const FString& LatinFontFacePath);
    UFUNCTION(BlueprintCallable,CallInEditor,Category="Interaction Prompt|Authoring") static bool AddLanguageSelectorToExistingUI();
};
