#pragma once
#include "CoreMinimal.h"
#include "DWCombatWeaponResourceState.h"
#include "DWCombatCommitLedger.h"
#include "DWWeaponAnimationSet.h"
class FDWCombatRules;
class UDWWeaponAnimationComponent;

/** Technical selection identity only. None is unarmed, never an extra permanent item. */
namespace DWCombatEquipment
{
    bool ToPresentation(FName EquipmentId,EDWAnimatedWeapon& Out);
    bool ToItemId(EDWAnimatedWeapon Presentation,FName& Out);
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
class FDWCombatEquipmentTransaction
{
public:
    static FDWCombatEquipmentGrantResult Grant(UDWWeaponAnimationComponent&,const FDWCombatEquipmentGrantRequest&);
};
