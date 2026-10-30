#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWAudioInspectionLibrary.generated.h"

/** External candidate only; inspects loaded assets without executing or mutating them. */
UCLASS()
class GDATTEST_API UDWAudioInspectionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Audio Inspection", meta=(DevelopmentOnly))
    static FString InspectAsset(UObject* Asset);
};
