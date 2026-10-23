#include "DWCombatDashTransaction.h"
#include "DWPlayerCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatInputLedger.h"
#include "Engine/World.h"
bool FDWCombatDashTransaction::IsCurrent(const ADWPlayerCharacter& P)
{
    auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    return A&&P.bDashCombatActive&&P.DashCommitAction.Id&&A->IsActionOwnedBy(P.DashCommitAction,const_cast<ADWPlayerCharacter*>(&P));
}
bool FDWCombatDashTransaction::Start(ADWPlayerCharacter& P,const FDWCombatInputPress& Press,const FDWYeastDashPlan& Plan)
{
    using namespace DWCombatCommit;
    auto* A=UDWCombatAuthoritySubsystem::Get(&P);
    if(!A||!P.IsCombatActionAllowed()||P.IsFormalDashing()||!Plan.IsActive()||!P.GetWorld()||
        P.GetCombatBaseForm()!=EDWCombatBaseForm::Yeast||P.CombatKnockback.bActive)return false;
    auto Vitals=P.CombatVitals;
    if(!Vitals.DebitSpecialAfterCommittedCost(Plan.GetConfig().StartCost))return false;
    const auto Life=A->CaptureActorLife(&P);
    const auto Action=A->BeginAction(&P);
    if(!Life.Entity||!Action.Id)return false;
    auto* Ledger=A->GetLedgerForResourceTransaction();
    if(!Ledger||!A->IsActionOwnedBy(Action,&P)){A->EndAction(Action);return false;}
    const auto Receipt=Ledger->AdmitResource({Action,1,EResourceKind::YeastDashStart,1});
    if(Receipt.Result!=EAdmission::Admitted){A->EndAction(Action);return false;}
    if(Ledger->Resolve(Receipt.Receipt.Handle,EOutcome::Committed)!=EResolve::Resolved){A->EndAction(Action);return false;}
    P.CombatVitals=MoveTemp(Vitals);P.FormalDash=Plan;P.DashInputOrigin=Press;
    P.FormalDashLife=Life;P.FormalDashWorld=P.GetWorld();P.FormalDashEpoch=A->GetWorldEpoch();
    P.DashCommitAction=Action;P.bDashCombatActive=true;P.DashHitActors.Reset();
    P.DashCombatDirection=FVector(Plan.Direction().X,Plan.Direction().Y,0.);
    P.LastDashCombatLocation=P.GetActorLocation();P.PaidFormalDashSlice={};
    P.SyncCombatVitalsCompatibility();return true;
}
