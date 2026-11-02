#pragma once
#include "CoreMinimal.h"
#include "DWCombatWeaponResourceState.h"
#include "DWCombatCommitLedger.h"
#include "DWWeaponAnimationSet.h"
class FDWCombatRules;
class UDWWeaponAnimationComponent;
class UDWGameInstance;
class UDWInventoryComponent;
struct FDWItemStack;
struct FDWInventorySlotSnapshot;

/** Technical selection identity only. None is unarmed, never an extra permanent item. */
namespace DWCombatEquipment
{
    bool ToPresentation(FName EquipmentId,EDWAnimatedWeapon& Out);
    bool ToItemId(EDWAnimatedWeapon Presentation,FName& Out);
    /** Display/new inventory identity only; ToItemId remains the existing serialized mechanics identity. */
    bool ToInventoryItemId(EDWAnimatedWeapon Presentation,FName& Out);
    bool OwnsPermanent(const FDWCombatWeaponResourceSnapshot&,FName MechanicsOrInventoryItemId);
    bool ValidateIdentity(const FDWCombatWeaponResourceSnapshot&,FString& Error);
    bool CanSelect(const FDWCombatWeaponResourceSnapshot&,EDWAnimatedWeapon,int32 AlcoholStock);
    bool BuildSwitchLock(const FDWCombatRules&,double& Out,FString& Error);
}

/** Future acquisition caller supplies an existing owner action and stable ordinal. No production grant caller. */
struct FDWCombatEquipmentGrantRequest
{
    DWCombatCommit::FActionId Action;
    DWCombatCommit::FEntityLife OwnerLife;
    uint64 OperationOrdinal=0,RestoreEpoch=0;
    FName PermanentItemId=NAME_None;
};
struct FDWCombatEquipmentGrantResult
{
    DWCombatCommit::FAdmission Admission;
    bool bCommittedThisCall=false,bAlreadyOwned=false;
};
/** Optional LANG trial wrapper state, never added to the old combat save schema. */
struct FDWCombatStarterGrantState
{
    FString PlaythroughId;
    bool bGranted=false;
    int64 Revision=0; // Native concurrency revision; wrapper persists ID+granted and recreates this owner on restore.
};
struct FDWCombatEquipmentStarterResult : FDWCombatEquipmentGrantResult
{
    bool bAlreadyGranted=false;
    FString Error;
};
class FDWCombatEquipmentTransaction
{
public:
    static FDWCombatEquipmentGrantResult Grant(UDWWeaponAnimationComponent&,const FDWCombatEquipmentGrantRequest&);
    /** Read-only UI/prepare gate; no action creation, grants or notifications. */
    static bool CanCraft(const UDWInventoryComponent&,FName PermanentItemId);
    /** Production entry creates one owner-bound action and ends it after the fixed commit. */
    static bool TryCraft(UDWWeaponAnimationComponent&,FName RecipeId,bool bYeast);
    /** Stable owner action/ordinal allows native receipt replay tests without replaying acquisition. */
    static FDWCombatEquipmentGrantResult Craft(UDWWeaponAnimationComponent&,const FDWCombatEquipmentGrantRequest&,FName RecipeId,bool bYeast);
    /** Atomic bag/ownership/receipt. Policy0 preserves deployed trial; policy1 only three simple tools. No auto equip/load. */
    static FDWCombatEquipmentStarterResult GrantStarterBatch(UDWWeaponAnimationComponent&,const TArray<FDWItemStack>& Items,
        const FString& StablePlaythroughId,FDWCombatStarterGrantState& Receipt,int32 StarterPolicyVersion=0);
    /** On-demand compatibility for a real existing item: ownership only, no add/remove/migration or auto-equip. */
    static bool ClaimExistingPermanent(UDWWeaponAnimationComponent&,const FDWInventorySlotSnapshot&);
    /** New formal adventure only: one bag + ownership + durable receipt transaction. */
    static FDWCombatEquipmentStarterResult GrantFormalStarterBatch(UDWWeaponAnimationComponent&,UDWGameInstance&,
        const TArray<FDWItemStack>& Items,const FString& StablePlaythroughId);
private:
    static FDWCombatEquipmentGrantResult Commit(UDWWeaponAnimationComponent&,const FDWCombatEquipmentGrantRequest&,FName RecipeId,bool bYeast);
};
