#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWWeaponVfxVariantLibrary.generated.h"
class UNiagaraSystem;
/** Editor-only fixed operations; never saves packages. */
UCLASS()
class DWBUILDINGAUTHORINGEDITOR_API UDWWeaponVfxVariantLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Approved R02")
    static bool ApplyApprovedR02Edits(UNiagaraSystem* System, FString& Report);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Approved R02")
    static bool InspectApprovedR02Inputs(UNiagaraSystem* System, FString& Report);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Approved R02")
    static bool InspectApprovedR02CompileState(UNiagaraSystem* System, bool& Ready, FString& Report);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Approved R02")
    static bool AuthorApprovedGiantSector(UNiagaraSystem* System, FString& Report);
    UFUNCTION(BlueprintCallable, Category="DoughWorld|Editor|Approved R02")
    static bool InspectFixedGiantSourceTopology(FString& Report);
};
