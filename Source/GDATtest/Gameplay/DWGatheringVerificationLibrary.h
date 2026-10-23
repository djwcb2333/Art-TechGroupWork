#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWGatheringVerificationLibrary.generated.h"

/** Explicit QA entry points. Only the isolated PIE gathering test map is admitted. */
UCLASS()
class GDATTEST_API UDWGatheringVerificationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Gathering|Verification", meta=(WorldContext="WorldContextObject", DevelopmentOnly))
    static FString RunRuntimeChecks(const UObject* WorldContextObject);
};
