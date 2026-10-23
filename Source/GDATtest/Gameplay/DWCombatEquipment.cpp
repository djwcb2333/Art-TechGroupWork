#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatRules.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInventoryTransaction.h"
#include "DWWeaponAnimationComponent.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"

bool DWCombatEquipment::ToPresentation(FName Id,EDWAnimatedWeapon& Out)
{
    if(Id.IsNone()){Out=EDWAnimatedWeapon::Unarmed;return true;}
    for(const auto& B:FDWCombatItemRegistry::GetWeaponBindings())if(B.ItemId==Id){Out=B.Presentation;return true;}
    return false;
}
bool DWCombatEquipment::ToItemId(EDWAnimatedWeapon Weapon,FName& Out)
{
    if(Weapon==EDWAnimatedWeapon::Unarmed){Out=NAME_None;return true;}
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(Weapon);if(!Binding)return false;
    Out=Binding->ItemId;return true;
}
bool DWCombatEquipment::ValidateIdentity(const FDWCombatWeaponResourceSnapshot& S,FString& Error)
{
    for(FName Id:S.OwnedPermanentWeaponIds)
        if(FDWCombatItemRegistry::GetItemRole(Id)!=EDWCombatItemRole::PermanentWeapon)
        {Error=TEXT("Owned equipment contains an unregistered or consumable identity.");return false;}
    EDWAnimatedWeapon Weapon;if(!ToPresentation(S.CurrentEquipmentId,Weapon))
    {Error=TEXT("Current equipment identity is unknown.");return false;}
    if(Weapon!=EDWAnimatedWeapon::Unarmed&&Weapon!=EDWAnimatedWeapon::Alcohol&&
        !S.OwnedPermanentWeaponIds.Contains(S.CurrentEquipmentId))
    {Error=TEXT("Current permanent equipment is not owned.");return false;}
    Error.Reset();return true;
}
bool DWCombatEquipment::CanSelect(const FDWCombatWeaponResourceSnapshot& S,EDWAnimatedWeapon Weapon,int32 AlcoholStock)
{
    FString Error;FName Id;
    if(!ValidateIdentity(S,Error)||!ToItemId(Weapon,Id))return false;
    if(Weapon==EDWAnimatedWeapon::Unarmed)return true;
    if(Weapon==EDWAnimatedWeapon::Alcohol)return AlcoholStock>0||S.CurrentEquipmentId==Id;
    return S.OwnedPermanentWeaponIds.Contains(Id);
}
bool DWCombatEquipment::BuildSwitchLock(const FDWCombatRules& Rules,double& Out,FString& Error)
{
    double Value=0.;if(!Rules.TryGetNumber(TEXT("weapon.switch_lock_s"),EDWCombatRuleUnit::Seconds,Value,Error))return false;
    if(!FMath::IsFinite(Value)||Value<0.){Error=TEXT("Invalid equipment switch clock.");return false;}
    Out=Value;Error.Reset();return true;
}
FDWCombatEquipmentGrantResult FDWCombatEquipmentTransaction::Grant(UDWWeaponAnimationComponent& W,const FDWCombatEquipmentGrantRequest& R)
{
    using namespace DWCombatCommit;
    FDWCombatEquipmentGrantResult Result;auto* P=W.Player();auto* A=UDWCombatAuthoritySubsystem::Get(P);
    auto* Inventory=P?P->GetInventory():nullptr;
    if(!A||!IsInGameThread()||!IsValid(&W)||W.IsBeingDestroyed()||!P||!P->IsCombatActionAllowed()||
        !Inventory||!Inventory->CanWriteInventory()||Inventory->GetOwner()!=P||
        !W.CombatWeaponResources.bRulesReady||W.CombatWeaponResources.Revision==MAX_int64||
        R.RestoreEpoch!=W.CombatWeaponResources.RestoreEpoch||!R.OperationOrdinal||
        FDWCombatItemRegistry::GetItemRole(R.PermanentItemId)!=EDWCombatItemRole::PermanentWeapon||
        !A->IsActionOwnedBy(R.Action,P))return Result;
    const auto Life=A->CaptureActorLife(P);
    if(!Life.Entity||Life.Entity!=R.OwnerLife.Entity||Life.Life!=R.OwnerLife.Life)return Result;
    const uint64 Epoch=A->GetWorldEpoch();const int64 WeaponRevision=W.CombatWeaponResources.Revision;
    auto* Ledger=A->GetLedgerForResourceTransaction();if(!Ledger)return Result;
    Result.Admission=Ledger->AdmitResource({R.Action,1,EResourceKind::EquipmentGrant,R.OperationOrdinal});
    if(Result.Admission.Result!=EAdmission::Admitted)return Result;
    const auto Reject=[&]()
    {
        Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Rejected);
        if(const auto Receipt=Ledger->FindReceipt(Result.Admission.Receipt.Handle))Result.Admission.Receipt=*Receipt;
        return Result;
    };
    Result.bAlreadyOwned=W.CombatWeaponResources.Snapshot.OwnedPermanentWeaponIds.Contains(R.PermanentItemId);
    if(Result.bAlreadyOwned)return Reject();
    // Prepare only: neither a full bag nor a rejected receipt can consume existing items or grant ownership.
    FDWPreparedInventoryChange InventoryPlan;
    if(!Inventory->PrepareAddItem(R.PermanentItemId,1,InventoryPlan))return Reject();
    auto Candidate=W.CombatWeaponResources.Snapshot;Candidate.OwnedPermanentWeaponIds.Add(R.PermanentItemId);
    FString Error;if(!DWCombatEquipment::ValidateIdentity(Candidate,Error)||!A->ReserveWeaponResourceNoticeSlot())return Reject();
    FDWCombatWeaponResourceNotice Notice;Notice.Owner=P;Notice.Weapon=&W;Notice.Inventory=Inventory;
    Notice.OwnerLife=Life;Notice.Receipt=Result.Admission.Receipt.Handle;Notice.Operation=EDWWeaponResourceOperation::EquipmentGrant;
    Notice.bInventoryChanged=true;Notice.LoadedBefore=Notice.LoadedAfter=Candidate.LoadedShotgunShells;
    Notice.InventoryRevision=InventoryPlan.GetExpectedRevision()+1;
    Notice.WeaponRevision=WeaponRevision+1;Notice.WeaponRestoreEpoch=R.RestoreEpoch;
    // The prepared bag and the owned set share one admitted operation and one notification.
    // All native rejection checks and allocations precede the fixed no-callback fact commit.
    if(!IsValid(&W)||W.IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||W.Player()!=P||
        P->GetInventory()!=Inventory||Inventory->GetOwner()!=P||!P->IsCombatActionAllowed()||
        UDWCombatAuthoritySubsystem::Get(P)!=A||A->GetWorldEpoch()!=Epoch||!A->IsActionOwnedBy(R.Action,P)||
        W.CombatWeaponResources.Revision!=WeaponRevision||W.CombatWeaponResources.RestoreEpoch!=R.RestoreEpoch||
        !Inventory->ValidatePreparedForCommit(InventoryPlan))return Reject();
    const auto CurrentLife=A->CaptureActorLife(P);
    if(CurrentLife.Entity!=Life.Entity||CurrentLife.Life!=Life.Life)return Reject();
    if(Ledger->Resolve(Result.Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved)return Result;
    Inventory->AdoptPreparedUnchecked(MoveTemp(InventoryPlan));
    W.CombatWeaponResources.Snapshot=MoveTemp(Candidate);++W.CombatWeaponResources.Revision;
    W.LastWeaponResourceReceipt=Result.Admission.Receipt.Handle;
    A->EnqueueCommittedWeaponResource(Notice);Result.bCommittedThisCall=true;Result.Admission.Receipt.State=EReceiptState::Committed;
    A->DrainNotifications();return Result; // No owner/component access after callbacks.
}
