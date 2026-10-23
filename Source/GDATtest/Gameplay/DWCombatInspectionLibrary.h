#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWCombatInspectionLibrary.generated.h"

class UBlueprint;
class UWorld;

/** Read-only evidence for the runtime handoff. Never modifies or saves a Blueprint. */
UCLASS()
class GDATTEST_API UDWCombatInspectionLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Combat", meta=(DevelopmentOnly))
    static FString InspectBlueprintBindings(UBlueprint* Blueprint);
    /** Does not create a missing level script or dirty its package. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Combat", meta=(DevelopmentOnly))
    static UBlueprint* GetExistingLevelScriptBlueprint(UWorld* World);
    /** Read-only current bound targets/functions, including sparse native delegates. */
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Combat", meta=(DevelopmentOnly))
    static FString InspectObjectDelegates(UObject* Object);
};
