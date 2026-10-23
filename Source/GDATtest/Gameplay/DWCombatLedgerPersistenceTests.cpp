#include "DWCombatCommitLedgerSnapshot.h"
namespace DWLedgerPersistenceTests
{
template<class Check>void PendingAndReleased(Check C)
{
    using namespace DWCombatCommit;FLedger L;L.SetWorldEpoch(2);L.SetLife({1,1});L.SetLife({10,1});L.SetLife({20,1});
    const auto Action=L.BeginAction({10,1});const auto Paid=L.AdmitResource({Action,1,EResourceKind::WeaponUse,1});
    const auto Emit=L.CommitWeaponUseWithEmitter(Paid.Receipt.Handle,1);
    const auto Hit=L.AdmitDamage({FActionDamage{Action,1,EActionDamageKind::Melee,1},{20,1}});
    const auto Unpaid=L.AdmitResource({Action,2,EResourceKind::WeaponUse,2});
    const auto Saved=L.CaptureSnapshot();std::string Error;FLedgerRestorePlan P;
    C("valid snapshot",FLedger::ValidateSnapshot(Saved,Error));C("prepare new world",L.PrepareSnapshotRestore(Saved,7,{},P,Error));
    C("landed pending survives cancelled ordinary attack",P.PendingDamage().size()==1&&P.PendingDamage()[0].Id==Hit.Receipt.Handle.Id);
    const auto R=P.FindReceipt(Unpaid.Receipt.Handle.Id);C("unfinished resource invalidated",R&&R->State==EReceiptState::Invalidated);
    C("released emitter survives source action cancellation",P.IsEmitterActive(Emit.Handle.Id));
    C("ready to adopt",L.CanCommitSnapshotRestore(P));L.CommitSnapshotRestoreUnchecked(std::move(P));
    C("old callback epoch rejected",L.Resolve(Hit.Receipt.Handle,EOutcome::Committed)==EResolve::Stale);
    C("pending resolves once in rebound epoch",L.Resolve({7,Hit.Receipt.Handle.Id},EOutcome::Committed)==EResolve::Resolved);
    C("pending cannot resolve twice",L.Resolve({7,Hit.Receipt.Handle.Id},EOutcome::Rejected)==EResolve::AlreadyFinal);
    C("source death does not retire released effect",L.SetLife({10,2})&&L.IsEmitterActive({7,Emit.Handle.Id}));
    const auto EmittedHit=L.AdmitDamage({FEmitterDamage{{7,Emit.Handle.Id},EEmitterDamageKind::ProjectileImpact,1},{20,1}});
    C("dead source released hit legal",EmittedHit.Result==EAdmission::Admitted);
    C("consumed plan cannot apply again",!L.CanCommitSnapshotRestore(P));
    const auto WorldAction=L.BeginAction({1,1});const auto Fatal=L.AdmitDamage({FActionDamage{WorldAction,1,EActionDamageKind::EnvironmentFatal,1},{20,1}});
    C("environment fatal admitted",Fatal.Result==EAdmission::Admitted);L.Resolve(Fatal.Receipt.Handle,EOutcome::Committed);
    C("environment fatal snapshot supported",FLedger::ValidateSnapshot(L.CaptureSnapshot(),Error));
}
template<class Check>void RootDedupAndStale(Check C)
{
    using namespace DWCombatCommit;FLedger L;L.SetWorldEpoch(1);L.SetLife({1,1});L.SetLife({10,1});L.SetLife({20,1});
    const auto Root=L.BeginAction({10,1});const auto Fee=L.AdmitResource({Root,1,EResourceKind::RootDeployment,1});L.Resolve(Fee.Receipt.Handle,EOutcome::Committed);
    const auto M1=L.ReleaseEmitter(Fee.Receipt.Handle,1);const auto M2=L.ReleaseEmitter(Fee.Receipt.Handle,2);
    const auto H=L.AdmitDamage({FRootMineDamage{M1.Handle},{20,1}});L.Resolve(H.Receipt.Handle,EOutcome::Committed);L.SetLife({20,2});
    auto S=L.CaptureSnapshot();FLedgerRestorePlan P;std::string Error;
    C("root active whitelist accepted",L.PrepareSnapshotRestore(S,5,{Root.Id},P,Error));L.CommitSnapshotRestoreUnchecked(std::move(P));
    C("whole ring remembers entity across target respawn",L.AdmitDamage({FRootMineDamage{{5,M2.Handle.Id}},{20,2}}).Result==EAdmission::Duplicate);
    C("old mine epoch stale",L.AdmitDamage({FRootMineDamage{M2.Handle},{20,2}}).Result==EAdmission::Stale);
    FLedgerRestorePlan A,B;S=L.CaptureSnapshot();C("first optimistic candidate",L.PrepareSnapshotRestore(S,7,{Root.Id},A,Error));
    C("second optimistic candidate",L.PrepareSnapshotRestore(S,8,{Root.Id},B,Error));L.CommitSnapshotRestoreUnchecked(std::move(A));
    C("old candidate invalid after another world adoption",!L.CanCommitSnapshotRestore(B));
    FLedgerRestorePlan Changed;S=L.CaptureSnapshot();L.PrepareSnapshotRestore(S,9,{Root.Id},Changed,Error);L.RetireEmitter({7,M1.Handle.Id});
    C("retirement invalidates prepared candidate even without new ID",!L.CanCommitSnapshotRestore(Changed));
}
template<class Check>void Corruption(Check C)
{
    using namespace DWCombatCommit;FLedger L;L.SetWorldEpoch(1);L.SetLife({1,1});L.SetLife({10,1});L.SetLife({20,1});
    auto Action=L.BeginAction({10,1});auto Paid=L.AdmitResource({Action,1,EResourceKind::WeaponUse,1});L.CommitWeaponUseWithEmitter(Paid.Receipt.Handle,1);
    const auto Good=L.CaptureSnapshot();std::string Error;auto S=Good;S.Keys.push_back(S.Keys.front());C("extra duplicate key rejected",!FLedger::ValidateSnapshot(S,Error));
    S=Good;S.Receipts.front().Receipt.State=EReceiptState::Rejected;C("emitter without committed cost rejected",!FLedger::ValidateSnapshot(S,Error));
    S=Good;S.Emitters.front().Owner={99,1};C("unbound emitter owner rejected",!FLedger::ValidateSnapshot(S,Error));
    S=Good;S.Keys.front().Key[1]=99;C("mixed epoch rejected",!FLedger::ValidateSnapshot(S,Error));
    S=Good;S.Emissions.front().Emitter=99;C("missing emitted identity rejected",!FLedger::ValidateSnapshot(S,Error));
    FLedgerRestorePlan P;C("same epoch cannot restore",!L.PrepareSnapshotRestore(Good,1,{},P,Error));
    C("duplicate action whitelist rejected",!L.PrepareSnapshotRestore(Good,2,{Action.Id,Action.Id},P,Error));
    L.EndAction(Action);S=L.CaptureSnapshot();C("cannot resurrect cancelled action via whitelist",!L.PrepareSnapshotRestore(S,2,{Action.Id},P,Error));
}
}
#if defined(DW_LEDGER_STANDALONE)
#include <iostream>
int main(){int Checks=0,Failures=0;auto C=[&](const char* Text,bool Passed){++Checks;if(!Passed){++Failures;std::cout<<"FAIL "<<Text<<'\n';}};
    DWLedgerPersistenceTests::PendingAndReleased(C);DWLedgerPersistenceTests::RootDedupAndStale(C);DWLedgerPersistenceTests::Corruption(C);
    std::cout<<Checks<<" checks, "<<Failures<<" failures\n";return Failures?1:0;}
#elif WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLedgerPersistenceTest,"DoughWorld.Combat.Persistence.LedgerIdentityPendingAndRing",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLedgerPersistenceTest::RunTest(const FString& Parameters)
{auto C=[&](const char* Text,bool Passed){TestTrue(UTF8_TO_TCHAR(Text),Passed);};DWLedgerPersistenceTests::PendingAndReleased(C);DWLedgerPersistenceTests::RootDedupAndStale(C);DWLedgerPersistenceTests::Corruption(C);return true;}
#endif
