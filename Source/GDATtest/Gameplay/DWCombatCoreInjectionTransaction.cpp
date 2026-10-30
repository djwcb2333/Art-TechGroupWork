#include "DWCombatCoreInjectionTransaction.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWPlayerCharacter.h"

bool FDWCombatCoreInjectionTransaction::Commit(ADWPlayerCharacter& P)
{
    using namespace DWCombatCommit;
    auto* A=UDWCombatAuthoritySubsystem::Get(&P);auto* I=P.Inventory.Get();auto& Session=P.CoreInjection;
    if(!A||!I||!P.bCoreInjectionCompletionDue||!Session.Plan.IsReady()||!P.IsCoreInjectionCurrent()||
        !P.IsCombatActionAllowed()||!I->CanWriteInventory())return false;
    auto CandidateVitals=P.CombatVitals;auto CandidatePlan=Session.Plan;
    const bool A01=P.CharacterA01&&P.CharacterA01->IsEnabled();FDWCharacterA01Runtime A01Candidate;
    const uint64 A01Revision=A01?P.CharacterA01->GetResourceRevision():0;
    if((A01?!P.CharacterA01->PrepareFormEntry(Session.TargetForm,A01Candidate):!CandidateVitals.EnterCommittedForm(Session.TargetForm))||!CandidatePlan.Commit())return false;
    FDWPreparedInventoryChange Prepared;
    if(!I->PrepareRemoveSnapshot(Session.Slot,Session.Config.CoreCost,Prepared)||!I->ValidatePreparedForCommit(Prepared))return false;
    auto* Ledger=A->GetLedgerForResourceTransaction();
    if(!Ledger||!A->IsActionOwnedBy(Session.Action,&P)||!A->ReserveCoreInjectionNoticeSlot())return false;
    const auto Admission=Ledger->AdmitResource({Session.Action,1,EResourceKind::CoreInjection,1});
    if(Admission.Result!=EAdmission::Admitted)return false;
    const auto Reject=[&](){Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Rejected);return false;};
    if(!P.IsCoreInjectionCurrent()||!I->ValidatePreparedForCommit(Prepared)||(A01&&P.CharacterA01->GetResourceRevision()!=A01Revision))return Reject();
    FDWCombatCoreInjectionNotice Notice;Notice.Player=&P;Notice.Inventory=I;Notice.Life=Session.Life;
    Notice.WorldEpoch=Session.WorldEpoch;Notice.Receipt=Admission.Receipt.Handle;Notice.Form=Session.TargetForm;
    // The final fallible step precedes every live assignment. All state is native and callback-free here.
    if(Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved)return false;
    I->AdoptPreparedUnchecked(MoveTemp(Prepared));
    if(A01)P.CharacterA01->AdoptRuntimeUnchecked(MoveTemp(A01Candidate),true);else P.CombatVitals=MoveTemp(CandidateVitals);
    Session.Plan=MoveTemp(CandidatePlan);P.bCoreInjectionCompletionDue=false;
    P.LastCoreInjectionReceipt=Admission.Receipt.Handle;
    A->EndAction(Session.Action);Session.Action={};
    P.bHasCombatFormFrameSlice=false;P.SyncCombatVitalsCompatibility();
    A->EnqueueCommittedCoreInjection(Notice);A->DrainNotifications();
    return true; // No access to player, inventory or session after callbacks.
}

void FDWCombatCoreInjectionTransaction::Publish(const FDWCombatCoreInjectionNotice& Notice)
{
    const auto Current=[&]()
    {
        auto* P=Notice.Player.Get();auto* I=Notice.Inventory.Get();auto* A=UDWCombatAuthoritySubsystem::Get(P);
        if(!A||!P||P->IsActorBeingDestroyed()||P->IsDead()||!I||!I->CanWriteInventory()||I->GetOwner()!=P||
            A->GetWorldEpoch()!=Notice.WorldEpoch||P->GetCombatBaseForm()!=Notice.Form||
            P->LastCoreInjectionReceipt.Epoch!=Notice.Receipt.Epoch||P->LastCoreInjectionReceipt.Id!=Notice.Receipt.Id)return false;
        const auto Life=A->CaptureActorLife(P);return Life.Entity==Notice.Life.Entity&&Life.Life==Notice.Life.Life;
    };
    if(!Current())return;
    Notice.Inventory->OnChanged.Broadcast();if(!Current())return;
    auto* P=Notice.Player.Get();P->UpdateFormAppearance();if(!Current())return;
    P->PlayFormTransitionVFX(true);if(!Current())return;
    P->OnFormChanged(Notice.Form==EDWCombatBaseForm::Yeast);
}
