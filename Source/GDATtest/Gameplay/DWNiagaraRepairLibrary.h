#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWNiagaraRepairLibrary.generated.h"
class UNiagaraSystem;
/** Editor maintenance only. Runtime gameplay does not call this library. */
UCLASS()
class GDATTEST_API UDWNiagaraRepairLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    /** Rebuild a project Niagara system from its graph, bypassing stale compiled results.
     * Asynchronous: wait for compilation before saving. Does not alter graph parameters. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor Repair")
    static bool ForceCompileSystem(UNiagaraSystem* System);
};
