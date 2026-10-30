#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWLanguagePIEVerificationLibrary.generated.h"
class ADWPlayerCharacter;
/** Finite exact-map PIE evidence. No resource setter or independent clock. */
UCLASS()
class GDATTEST_API UDWLanguagePIEVerificationLibrary : public UBlueprintFunctionLibrary
{
 GENERATED_BODY()
public:
 /** Reads actual facts. Complete aggregate capture does not write or restore a file. */
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Language|Verification",meta=(DevelopmentOnly))
 static FString InspectTrial(ADWPlayerCharacter* Player,bool bCaptureAggregate=false);
 /** Real Interact front door: existing unique in-reach carrier, never teleport or spawn. */
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Language|Verification",meta=(DevelopmentOnly))
 static FString InteractTrialCarrier(ADWPlayerCharacter* Player,FName StableId,bool bLanguageOnly=false);
 /** Automation fixture teardown via the real bridge lease, never evidence of a human Esc press. */
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Language|Verification",meta=(DevelopmentOnly))
 static FString CloseTrialPanel(ADWPlayerCharacter* Player);
 /** Exact gated idle capture comparison; only local codec candidates are normalized. */
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Language|Verification",meta=(DevelopmentOnly))
 static FString CompareIdleTrialCaptures(ADWPlayerCharacter* Player,const FString& BeforeNativeJson,const FString& AfterNativeJson);
};
