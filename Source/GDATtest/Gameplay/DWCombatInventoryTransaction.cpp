#include "DWCombatInventoryTransaction.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWPlayerCharacter.h"
#include "Engine/World.h"

FDWCombatWeaponResourceResult FDWCombatInventoryTransaction::CommitReloadShell(UDWWeaponAnimationComponent& Weapon)
{ return Commit(Weapon,EDWWeaponResourceOperation::ReloadShell); }
FDWCombatWeaponResourceResult FDWCombatInventoryTransaction::CommitMagazineShot(UDWWeaponAnimationComponent& Weapon)
{ return Commit(Weapon,EDWWeaponResourceOperation::MagazineShot); }

FDWCombatWeaponResourceResult FDWCombatInventoryTransaction::Commit(UDWWeaponAnimationComponent& Weapon,EDWWeaponResourceOperation Operation)
{
    using namespace DWCombatCommit;
    FDWCombatWeaponResourceResult Result;
    ADWPlayerCharacter* Owner=Weapon.Player();
    UDWInventoryComponent* Inventory=Owner?Owner->GetInventory():nullptr;
    UDWCombatAuthoritySubsystem* Authority=UDWCombatAuthoritySubsystem::Get(Owner);
    if(!Authority||!Weapon.CanUseShotgunResources()||!Inventory||!Inventory->CanWriteInventory())return Result;
    const bool bReload=Operation==EDWWeaponResourceOperation::ReloadShell;
    const FActionId Action=bReload?Weapon.ReloadResourceAction:Weapon.MagazineResourceAction;
    const FId Ordinal=bReload?Weapon.ReloadShellOrdinal:1;
    // The action comes from BeginAction, never from visual ActionSerial or an external supplied id.
    if(!Weapon.bActive||!Action.Id||!Ordinal||!Authority->IsActionOwnedBy(Action,Owner))return Result;
    if(bReload&&(!Weapon.bReloading||Weapon.ActiveClip.Role!=TEXT("ReloadOne")||!Weapon.bReloadShellCompletionDue||Weapon.bReloadShellCommitted))return Result;
    if(!bReload&&(Weapon.ActiveClip.Role!=TEXT("Attack")||!Weapon.bMagazineShotEventDue))return Result;
    auto* Ledger=Authority->GetLedgerForResourceTransaction();
    if(!Ledger)return Result;
    const auto Admission=Ledger->AdmitResource({Action,1,bReload?EResourceKind::ReloadShell:EResourceKind::WeaponUse,Ordinal});
    Result.Admission=Admission.Result;Result.Receipt=Admission.Receipt;
    if(Admission.Result!=EAdmission::Admitted)return Result;
    auto Reject=[&]()
    {
        Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);
        if(const auto Receipt=Ledger->FindReceipt(Admission.Receipt.Handle))Result.Receipt=*Receipt;
        return Result;
    };
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    const int64 ExpectedWeaponRevision=Weapon.CombatWeaponResources.Revision;
    const int32 Before=Weapon.CombatWeaponResources.Snapshot.LoadedShotgunShells;
    if(!Binding||Binding->AmmoItemId.IsNone()||ExpectedWeaponRevision==MAX_int64)return Reject();
    const int32 ShotCost=Weapon.CombatWeaponResources.ShotgunRoundsPerShot;
    if((bReload&&Before>=Weapon.CombatWeaponResources.ShotgunCapacity)||(!bReload&&(ShotCost<1||Before<ShotCost)))return Reject();
    FDWPreparedInventoryChange InventoryPlan;
    if(bReload&&!Inventory->PrepareRemoveItem(Binding->AmmoItemId,1,InventoryPlan))return Reject();
    const auto Life=Authority->CaptureActorLife(Owner);
    if(!Life.Entity||!Authority->ReserveWeaponResourceNoticeSlot())return Reject();
    const int32 After=Before+(bReload?1:-ShotCost);
    FDWCombatWeaponResourceNotice Notice;
    Notice.Owner=Owner;Notice.Weapon=&Weapon;Notice.Inventory=Inventory;Notice.OwnerLife=Life;
    Notice.Receipt=Admission.Receipt.Handle;Notice.Operation=Operation;Notice.bInventoryChanged=bReload;
    Notice.LoadedBefore=Before;Notice.LoadedAfter=After;Notice.WeaponRevision=ExpectedWeaponRevision+1;
    Notice.InventoryRevision=Inventory->GetInventoryRevision()+(bReload?1:0);
    Notice.WeaponRestoreEpoch=Weapon.CombatWeaponResources.RestoreEpoch;
    // Only native validation has occurred. All possible rejection/allocation precedes Resolve.
    if(!Weapon.CanUseShotgunResources()||Weapon.CombatWeaponResources.Revision!=ExpectedWeaponRevision||
        !Authority->IsActionOwnedBy(Action,Owner)||(bReload&&!Inventory->ValidatePreparedForCommit(InventoryPlan)))return Reject();
    if(Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved)
    {
        if(const auto Receipt=Ledger->FindReceipt(Admission.Receipt.Handle))Result.Receipt=*Receipt;
        return Result;
    }
    if(bReload)Inventory->AdoptPreparedUnchecked(MoveTemp(InventoryPlan));
    Weapon.CombatWeaponResources.Snapshot.LoadedShotgunShells=After;
    ++Weapon.CombatWeaponResources.Revision;
    Weapon.AmmoInMagazine=After;
    Weapon.LastWeaponResourceReceipt=Admission.Receipt.Handle;
    if(bReload){Weapon.bReloadShellCompletionDue=false;Weapon.bReloadShellCommitted=true;}
    else Weapon.bMagazineShotEventDue=false;
    // No cooldown change: the accepted action already committed its lock before this event.
    // Enqueue uses the capacity reserved above; it does not invoke gameplay.
    Authority->EnqueueCommittedWeaponResource(Notice);
    Result.Receipt.State=EReceiptState::Committed;Result.bCommittedThisCall=true;
    Authority->DrainNotifications();
    return Result; // Never access Weapon/Inventory/Owner after synchronous external callbacks.
}

void FDWCombatInventoryTransaction::PublishWeaponResourceNotice(const FDWCombatWeaponResourceNotice& Notice)
{
    auto IsCurrent=[&]()
    {
        AActor* Owner=Notice.Owner.Get();auto* Weapon=Notice.Weapon.Get();
        auto* Authority=UDWCombatAuthoritySubsystem::Get(Owner);
        if(!Authority||!IsValid(Owner)||Owner->IsActorBeingDestroyed()||!IsValid(Weapon)||Weapon->IsBeingDestroyed()||
            Weapon->GetOwner()!=Owner||Weapon->CombatWeaponResources.RestoreEpoch!=Notice.WeaponRestoreEpoch)return false;
        const auto Life=Authority->CaptureActorLife(Owner);
        return Life.Entity==Notice.OwnerLife.Entity&&Life.Life==Notice.OwnerLife.Life;
    };
    if(!IsCurrent())return;
    if(Notice.bInventoryChanged)
    {
        auto* Inventory=Notice.Inventory.Get();
        if(!Inventory||!Inventory->CanWriteInventory()||Inventory->GetOwner()!=Notice.Owner.Get())return;
        Inventory->OnChanged.Broadcast();
        if(!IsCurrent())return;
    }
    Notice.Weapon.Get()->OnWeaponResourceChanged.Broadcast(Notice.LoadedAfter,Notice.WeaponRevision,
        Notice.Operation==EDWWeaponResourceOperation::ReloadShell);
    if(Notice.bPublishCommittedAttack&&IsCurrent())
        Notice.Weapon->OnAnimationGameplayEvent.Broadcast(static_cast<EDWAnimatedWeapon>(Notice.CommittedWeapon),Notice.CommittedRole,Notice.CommittedEvent);
}
