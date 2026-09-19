#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWGameplayDebugLibrary.generated.h"
class APlayerController;

/** Local editor-only test input. The packaged game cannot inject inputs through this helper. */
UCLASS()
class GDATTEST_API UDWGameplayDebugLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor Verification")
    static bool SetTestKey(APlayerController* PlayerController,FName KeyName,bool bPressed);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor Verification")
    static bool SendTestAxis(APlayerController* PlayerController,FName AxisName,float Delta);
    /** Test fixture only; returns false outside PIE and in packaged builds. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor Verification")
    static bool SetPlayerTestState(APlayerController* PlayerController,float Health,float Transformation,float SprintElapsed,float DecayElapsed);
};
