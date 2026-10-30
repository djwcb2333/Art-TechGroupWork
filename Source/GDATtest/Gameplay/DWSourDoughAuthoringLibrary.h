#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWSourDoughAuthoringLibrary.generated.h"
class UAnimBlueprint; class UAnimSequenceBase; class USkeleton;
UCLASS()
class GDATTEST_API UDWSourDoughAuthoringLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Sour Dough",meta=(DevelopmentOnly))
    static UAnimBlueprint* CreateSourAnimationBlueprint(const FString& PackagePath,USkeleton* Skeleton,UAnimSequenceBase* Idle,
        UAnimSequenceBase* Enter,UAnimSequenceBase* Loop,UAnimSequenceBase* Exit,FString& OutMessage);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Sour Dough",meta=(DevelopmentOnly))
    static bool MountZombieSourcePack(const FString& Directory,FString& OutMessage);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Sour Dough",meta=(DevelopmentOnly))
    static void UnmountZombieSourcePack(const FString& Directory);
};
