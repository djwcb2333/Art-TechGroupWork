#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWFrontendAuthoringLibrary.generated.h"

/** Creates an independent, editable frontend example without replacing existing UI. */
UCLASS()
class GDATTEST_API UDWFrontendAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Duplicates the complete gameplay Designer tree once, then adds frontend artwork and real UMG timelines. */
    UFUNCTION(BlueprintCallable, CallInEditor, Category="DoughWorld|Frontend Authoring")
    static bool CreateFrontendUIAssets();

    UFUNCTION(BlueprintPure, Category="DoughWorld|Frontend Authoring")
    static FString GetLastBuildReport();
};
