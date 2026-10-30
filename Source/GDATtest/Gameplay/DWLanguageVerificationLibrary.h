#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWLanguageVerificationLibrary.generated.h"

/** Fixed three-building graph evidence. Never executes a graph, spawns, or saves. */
UCLASS()
class GDATTEST_API UDWLanguageVerificationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Optional editor-only compile requires no live target instances and never saves. Not a PIE test. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Language|Verification", meta=(DevelopmentOnly))
    static FString InspectBuildingBlueprints(bool bCompileInMemory=false);
};
