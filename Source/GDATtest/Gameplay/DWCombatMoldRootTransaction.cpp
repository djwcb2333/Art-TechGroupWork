#include "DWCombatMoldRootTransaction.h"
#include "DWCombatMineSubsystem.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"

bool FDWCombatMoldRootTransaction::CommitEntry(ADWPlayerCharacter& P)
{
    using namespace DWCombatCommit;
    auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    if(!A||!P.bRootEntryDue||!P.IsCombatActionAllowed()||P.IsFormallyRooted())return false;
    FDWMoldRootConfig Config;if(!P.PrepareFormalRootEntry(Config))return false;
    auto CandidateVitals=P.CombatVitals;auto CandidateRoot=P.FormalRoot;
    const FVector Anchor=P.GetActorLocation();
    if(!CandidateRoot.Start(Config,P.GetCombatBaseForm(),P.GetCombatSpecial(),true,Anchor)||
        !CandidateVitals.DebitSpecialAfterCommittedCost(Config.StartCost))return false;
    auto* Ledger=A->GetLedgerForResourceTransaction();
    if(!Ledger||!A->ReserveMoldRootNoticeSlot())return false;
    const auto Action=A->BeginAction(&P);if(!Action.Id)return false;
    const auto Admission=Ledger->AdmitResource({Action,1,EResourceKind::RootDeployment,1});
    if(Admission.Result!=EAdmission::Admitted){A->EndAction(Action);return false;}
    FDWCombatMoldRootNotice Notice;Notice.Player=&P;
    Notice.Identity={Action,Admission.Receipt.Handle,A->CaptureActorLife(&P),A->GetWorldEpoch(),CandidateRoot.GetSerial(),Anchor};
    // All candidates and notification capacity exist before the final fallible step.
    if(Ledger->Resolve(Admission.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved)
    {A->EndAction(Action);return false;}
    P.CombatVitals=MoveTemp(CandidateVitals);P.FormalRoot=MoveTemp(CandidateRoot);
    P.FormalRootIdentity=Notice.Identity;P.bRootEntryDue=false;P.bRootMovementPending=true;
    P.ResetCombatKnockbackFacts();P.SyncCombatVitalsCompatibility();
    A->EnqueueCommittedMoldRoot(Notice);A->DrainNotifications();
    return true; // Do not inspect actor/session after callbacks.
}
void FDWCombatMoldRootTransaction::Publish(const FDWCombatMoldRootNotice& Notice)
{
    const auto Current=[&]()
    {
        auto* P=Notice.Player.Get();FDWMoldRootIdentity I;
        return P&&P->GetFormalRootIdentity(I)&&I.WorldEpoch==Notice.Identity.WorldEpoch&&
            I.SourceLife.Entity==Notice.Identity.SourceLife.Entity&&I.SourceLife.Life==Notice.Identity.SourceLife.Life&&
            I.RootSerial==Notice.Identity.RootSerial&&I.Receipt.Id==Notice.Identity.Receipt.Id;
    };
    if(!Current())return;
    if(!Notice.Player->PublishFormalRootMovementEffects()||!Current())return;
    // D11 entry hook: OnRootCommitted(Player, Action, Receipt, SourceLife, RootSerial, Anchor).
    // This is one real entry, never a Tick/re-enter/restore grant.
    if(auto* Mines=UDWCombatMineSubsystem::Get(Notice.Player.Get()))Mines->OnRootCommitted(Notice.Player.Get(),Notice.Identity);
}
