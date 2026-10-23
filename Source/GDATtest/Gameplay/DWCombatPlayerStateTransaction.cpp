#include "DWCombatPlayerStateTransaction.h"
#include "DWCombatEquipment.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGameplayConfig.h"
#include "DWSaveGame.h"

bool FDWCombatPlayerStateTransaction::Restore(ADWPlayerCharacter& P,const UDWSaveGame& Save,const FDWCombatRules& Rules,
    FDWCombatPlayerStateNotice& OutNotice,FString& Error)
{
    auto* I=P.Inventory.Get();auto* W=P.WeaponAnimationComponent.Get();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    if(!A||!I||!W||W->IsBeingDestroyed()||!I->CanWriteInventory()||!P.bRestoringSave||
        W->CombatWeaponResources.Revision==MAX_int64||W->CombatWeaponResources.RestoreEpoch==MAX_uint64)
    {Error=TEXT("Player resource owner is not ready for atomic restore.");return false;}
    if(!DWCombatPlayerSave::Validate(Rules,Save,Error))return false;
    FDWCombatPlayerVitalsConfig Config;FDWCombatPlayerVitalsRuntime Vitals;FDWCombatWeaponResourceState Weapon;
    if(!FDWCombatPlayerVitalsConfig::Build(Rules,Config,Error)||!Vitals.Restore(Config,Save.CombatPlayerResources.Vitals)||
        !DWCombatPlayerSave::BuildWeaponCandidate(Rules,Save.CombatPlayerResources,Weapon,Error))return false;
    EDWAnimatedWeapon Selection;double SwitchLock=0.;
    if(!DWCombatEquipment::ToPresentation(Weapon.Snapshot.CurrentEquipmentId,Selection)||
        !DWCombatEquipment::BuildSwitchLock(Rules,SwitchLock,Error))return false;
    FDWPreparedInventoryChange Prepared;TArray<FDWItemStack> Slots=Save.Inventory;
    if(!I->MakePrepared(MoveTemp(Slots),Prepared)||!I->ValidatePreparedForCommit(Prepared))
    {Error=TEXT("Saved inventory cannot be adopted without loss.");return false;}
    const auto OldLife=A->CaptureActorLife(&P);
    if(!OldLife.Entity||!A->CanRestorePlayerState(&P)||!A->ReservePlayerStateNoticeSlot())
    {Error=TEXT("Player lifecycle or notification capacity exhausted.");return false;}
    Weapon.Revision=W->CombatWeaponResources.Revision+1;Weapon.RestoreEpoch=W->CombatWeaponResources.RestoreEpoch+1;
    // Validation/allocation is complete. These native facts run without Blueprint or delegates.
    A->NotifyActorRestored(&P);
    P.ResetCombatKnockbackFacts();
    W->EndWeaponResourceActions();
    I->AdoptPreparedUnchecked(MoveTemp(Prepared));
    W->CombatWeaponResources=MoveTemp(Weapon);
    W->SelectedWeapon=Selection;W->EquipmentSwitchLockSeconds=SwitchLock;W->bEquipmentRulesReady=true;
    W->AmmoInMagazine=W->SelectedWeapon==EDWAnimatedWeapon::Shotgun?W->CombatWeaponResources.Snapshot.LoadedShotgunShells:0;
    W->LastWeaponResourceReceipt={};
    P.CombatVitals=MoveTemp(Vitals);P.Health=Save.Health;P.bDeathHandled=false;P.bCombatDeathEffectsPending=false;P.bCombatRecoveryPositionPending=false;
    P.bHasCombatFormFrameSlice=false;P.SyncCombatVitalsCompatibility();
    W->RefreshDaggerEquipmentFacts();
    OutNotice.Player=&P;OutNotice.Inventory=I;OutNotice.Weapon=W;OutNotice.Life=A->CaptureActorLife(&P);
    OutNotice.RestoreEpoch=W->CombatWeaponResources.RestoreEpoch;OutNotice.WeaponRevision=W->CombatWeaponResources.Revision;
    OutNotice.LoadedShells=W->CombatWeaponResources.Snapshot.LoadedShotgunShells;OutNotice.bRestore=true;
    Error.Reset();return true;
}

bool FDWCombatPlayerStateTransaction::UseLegacyHealingItem(ADWPlayerCharacter& P,int32 SlotIndex)
{
    using namespace DWCombatCommit;
    auto* I=P.Inventory.Get();auto* W=P.WeaponAnimationComponent.Get();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    if(!A||!I||!W||!P.GameplayConfig||!P.HasCombatVitalsReady()||P.IsDead()||P.bRestoringSave||!I->CanWriteInventory())return false;
    FDWInventorySlotSnapshot Slot;if(!I->CaptureSlotSnapshot(SlotIndex,Slot))return false;
    const auto* Definition=P.GameplayConfig->GetItemDefinition(Slot.ItemId);
    if(!Definition||!Definition->bUsable||!FMath::IsFinite(Definition->HealAmount)||Definition->HealAmount<=0.f)return false;
    const float Before=P.Health;
    const float After=DWCombatPlayerSave::NativeHealth(static_cast<double>(Before)+Definition->HealAmount,P.CombatVitals.GetConfig());
    if(After<=Before)return false;
    FDWPreparedInventoryChange Prepared;if(!I->PrepareConsumeSnapshot(Slot,Prepared))return false;
    auto* Ledger=A->GetLedgerForResourceTransaction();const auto Action=A->BeginAction(&P);
    if(!Ledger||!Action.Id)return false;
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::LegacyItemUse,1});
    if(Admission.Result!=EAdmission::Admitted){A->EndAction(Action);return false;}
    if(!I->ValidatePreparedForCommit(Prepared)||P.Health!=Before||!A->ReservePlayerStateNoticeSlot())
    {Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);A->EndAction(Action);return false;}
    FDWCombatPlayerStateNotice Notice;Notice.Player=&P;Notice.Inventory=I;Notice.Weapon=W;
    Notice.Life=A->CaptureActorLife(&P);Notice.RestoreEpoch=W->CombatWeaponResources.RestoreEpoch;
    Notice.UsedItem=Slot.ItemId;Notice.RestoredHealth=After-Before;
    if(Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved){A->EndAction(Action);return false;}
    I->AdoptPreparedUnchecked(MoveTemp(Prepared));P.Health=After;
    P.CancelCoreInjectionFacts(); // A committed use of another item interrupts unfinished injection.
    A->EndAction(Action);A->EnqueueCommittedPlayerState(Notice);A->DrainNotifications();return true;
}

void FDWCombatPlayerStateTransaction::Publish(const FDWCombatPlayerStateNotice& Notice)
{
    const auto Current=[&]()
    {
        auto* P=Notice.Player.Get();auto* I=Notice.Inventory.Get();auto* W=Notice.Weapon.Get();
        auto* A=UDWCombatAuthoritySubsystem::Get(P);
        if(!A||!P||P->IsActorBeingDestroyed()||!I||!I->CanWriteInventory()||I->GetOwner()!=P||!W||W->IsBeingDestroyed()||
            W->GetOwner()!=P||W->CombatWeaponResources.RestoreEpoch!=Notice.RestoreEpoch)return false;
        const auto Life=A->CaptureActorLife(P);return Life.Entity==Notice.Life.Entity&&Life.Life==Notice.Life.Life;
    };
    if(!Current())return;
    if(Notice.bRestore){Notice.Player.Get()->PublishCombatKnockbackEffects();if(!Current())return;}
    Notice.Inventory.Get()->OnChanged.Broadcast();if(!Current())return;
    if(Notice.bRestore)Notice.Weapon.Get()->OnWeaponResourceChanged.Broadcast(Notice.LoadedShells,Notice.WeaponRevision,false);
    else Notice.Player.Get()->OnInventoryItemUsed(Notice.UsedItem,Notice.RestoredHealth,0.f);
}
