#pragma once
#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "DWCombatResourceVerificationLibrary.generated.h"

class ADWPlayerCharacter;

/** Fixed editor PIE QA fixtures only. No arbitrary resource setter or file-writing API. */
UCLASS()
class GDATTEST_API UDWCombatResourceVerificationLibrary : public UBlueprintFunctionLibrary
{
    GENERATED_BODY()
public:
    // Fixed map-gated user-requested test stock; not exposed as a general Blueprint setter.
    static bool ApplyCombatTestAmmo(ADWPlayerCharacter* Player);
    // Fixed two-core testing stock; native only, same precise PIE/map/no-slot boundary.
    static bool ApplyCombatTestCores(ADWPlayerCharacter* Player);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Resource Verification",meta=(DevelopmentOnly))
    static FString ApplyInitialShotgunFixture(ADWPlayerCharacter* Player);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Resource Verification",meta=(DevelopmentOnly))
    static FString ApplyMutatedShotgunFixture(ADWPlayerCharacter* Player);
    /** Fixed isolated PIE fixture: only permanent spear ownership, zero loaded shells; no file writes. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Resource Verification",meta=(DevelopmentOnly))
    static FString ApplySpearVerificationFixture(ADWPlayerCharacter* Player);
    /** All weapon supplies only in the existing isolated active PIE QA context; no file write. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Resource Verification",meta=(DevelopmentOnly))
    static FString ApplyWeaponMechanicsFixture(ADWPlayerCharacter* Player);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Resource Verification",meta=(DevelopmentOnly))
    static FString InspectPlayerResources(ADWPlayerCharacter* Player);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Resource Verification",meta=(DevelopmentOnly))
    static FString InspectActiveSlotResources(ADWPlayerCharacter* Player);
    /** Fixed isolated PIE observation only: complete native capture or read the active isolated file. No writes or restore. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Editor|Resource Verification",meta=(DevelopmentOnly))
    static FString InspectCompleteCombatSnapshot(ADWPlayerCharacter* Player,bool bReadActiveSlot);
};
