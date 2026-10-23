#pragma once
#include "CoreMinimal.h"
#include "DWGameplayTypes.h"
#include "DWCombatRecoveryData.generated.h"
USTRUCT()
struct FDWDeathBagStack
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) FName ItemId;
    UPROPERTY(SaveGame) int32 Original=0;
    UPROPERTY(SaveGame) int32 Remaining=0;
};
USTRUCT()
struct FDWDeathBagSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=0;
    UPROPERTY(SaveGame) FGuid BagId;
    UPROPERTY(SaveGame) FGuid PlayerId;
    UPROPERTY(SaveGame) FString MapPackage;
    UPROPERTY(SaveGame) FVector Location=FVector::ZeroVector;
    UPROPERTY(SaveGame) TArray<FDWDeathBagStack> Contents;
    UPROPERTY(SaveGame) uint64 Revision=1;
    UPROPERTY(SaveGame) bool bClaimed=false;
};
USTRUCT()
struct FDWCombatSafePointSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) bool bActive=false;
    UPROPERTY(SaveGame) FGuid PointId;
    UPROPERTY(SaveGame) FString MapPackage;
    // Authored capsule-centre transform, not an arbitrary ground contact point.
    UPROPERTY(SaveGame) FTransform Transform=FTransform::Identity;
};
USTRUCT()
struct FDWCombatRecoverySave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=0;
    UPROPERTY(SaveGame) FGuid PlayerId;
    UPROPERTY(SaveGame) uint64 Revision=1;
    UPROPERTY(SaveGame) bool bAwaitingRespawn=false;
    UPROPERTY(SaveGame) FGuid CurrentDeathBagId;
    UPROPERTY(SaveGame) FDWCombatSafePointSave SafePoint;
    UPROPERTY(SaveGame) TArray<FDWDeathBagSave> Bags;
};
namespace DWCombatRecovery
{
    bool IsDeathDropItem(FName ItemId);
    bool Partition(const TArray<FDWItemStack>& Inventory,TArray<FDWItemStack>& Kept,TArray<FDWDeathBagStack>& Dropped);
    bool ValidateBag(const FDWDeathBagSave& Bag,FString& Error);
    bool Validate(const FDWCombatRecoverySave& Save,FString& Error);
    bool IsEmpty(const FDWDeathBagSave& Bag);
    // Copied capacity policy, no callback or live inventory mutation.
    bool PlanClaim(const FDWDeathBagSave& Bag,const TArray<FDWItemStack>& Inventory,int32 MaxSlots,
        const TMap<FName,int32>& Limits,TArray<FDWItemStack>& OutInventory,FDWDeathBagSave& OutBag,int64& OutTransferred);
}
