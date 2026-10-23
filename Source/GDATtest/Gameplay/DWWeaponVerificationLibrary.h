#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWWeaponVerificationLibrary.generated.h"
class USkeletalMesh;
class UAnimSequence;

/** Editor verification only; transient registered components never alter a level or saved asset. */
UCLASS()
class GDATTEST_API UDWWeaponVerificationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Weapon Verification",meta=(WorldContext="WorldContextObject"))
    static FString SampleDeformedSurface(UObject* WorldContextObject,USkeletalMesh* Mesh,UAnimSequence* Animation,float TimeSeconds);
};
