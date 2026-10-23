// Standalone: compile with DW_COMBAT_LEDGER_STANDALONE; UE registration is optional.
#if WITH_DEV_AUTOMATION_TESTS || defined(DW_COMBAT_LEDGER_STANDALONE)
#include "DWCombatCommitLedger.h"
#include <array>
#include <limits>
#if defined(DW_COMBAT_LEDGER_STANDALONE)
#include <cstdio>
#else
#include "Misc/AutomationTest.h"
#endif

namespace DWCombatCommitTests
{
using namespace DWCombatCommit;
struct FContext
{
    void* Owner;
    void (*Sink)(void*, const char*, bool);
    void Check(bool Passed, const char* Label) { Sink(Owner, Label, Passed); }
};
struct FFixture
{
    FLedger Ledger;
    FEntityLife Player{1, 1}, Enemy{2, 1}, Other{3, 1};
    FActionId Action;
    FFixture()
    {
        Ledger.SetWorldEpoch(1); Ledger.SetLife(Player); Ledger.SetLife(Enemy); Ledger.SetLife(Other);
        Action = Ledger.BeginAction(Player);
    }
    FResourceRequest Resource(FId Ordinal = 1, EResourceKind Kind = EResourceKind::WeaponUse) const
    { return {Action, 1, Kind, Ordinal}; }
    FDamageRequest Hit(FEntityLife Target, FId Packet = 1) const
    { return {FActionDamage{Action, 1, EActionDamageKind::Melee, Packet}, Target}; }
};

void ReentryAndDistinctRequests(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    const auto Resource = L.AdmitResource(F.Resource());
    const auto Hit = L.AdmitDamage(F.Hit(F.Enemy));
    T.Check(Resource.Result == EAdmission::Admitted && Hit.Result == EAdmission::Admitted, "Resource and target have separate reservations");
    int ResourceEffects = 0, DamageEffects = 0, CallbackCount = 0;
    if (Resource.Result == EAdmission::Admitted) ++ResourceEffects;
    if (Hit.Result == EAdmission::Admitted)
    {
        ++DamageEffects;
        const auto ReentrantCallback = [&]()
        {
            ++CallbackCount;
            const auto RepeatedResource = L.AdmitResource(F.Resource());
            const auto RepeatedHit = L.AdmitDamage(F.Hit(F.Enemy));
            if (RepeatedResource.Result == EAdmission::Admitted) ++ResourceEffects;
            if (RepeatedHit.Result == EAdmission::Admitted) ++DamageEffects;
            T.Check(RepeatedResource.Result == EAdmission::Duplicate && RepeatedHit.Result == EAdmission::Duplicate,
                "Same-key callback sees existing pending receipts before original resolution");
            T.Check(RepeatedHit.Receipt.Handle.Id == Hit.Receipt.Handle.Id, "Duplicate retains receipt identity");
            T.Check(L.AdmitDamage(F.Hit(F.Other)).Result == EAdmission::Admitted,
                "Second legitimate target is not swallowed by a global in-progress guard");
            const auto CounterAction = L.BeginAction(F.Enemy);
            T.Check(L.AdmitDamage({FActionDamage{CounterAction, 1, EActionDamageKind::EnemyMelee, 1}, F.Player}).Result == EAdmission::Admitted,
                "Distinct legal counterattack remains admissible while original request is pending");
        };
        ReentrantCallback();
    }
    T.Check(CallbackCount == 1 && ResourceEffects == 1 && DamageEffects == 1, "Fixture really entered callback; repeated request did not execute twice");
    T.Check(L.Resolve(Resource.Receipt.Handle, EOutcome::Committed) == EResolve::Resolved, "Commit resource once");
    T.Check(L.Resolve(Hit.Receipt.Handle, EOutcome::Committed) == EResolve::Resolved, "Commit target once");
    T.Check(L.AdmitDamage(F.Hit(F.Enemy, 2)).Result == EAdmission::Admitted, "A genuine second packet can hit the same target");
}

void PendingSurvivesSourceExit(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    const auto Resource = L.AdmitResource(F.Resource());
    const auto Hit = L.AdmitDamage(F.Hit(F.Enemy));
    T.Check(L.EndAction(F.Action), "Action ends");
    T.Check(L.Resolve(Resource.Receipt.Handle, EOutcome::Committed) == EResolve::Stale, "Uncommitted resource cannot survive action exit");
    T.Check(L.AdmitDamage(F.Hit(F.Enemy)).Result == EAdmission::Duplicate, "Landed pending packet remains identifiable after source action ends");
    T.Check(L.AdmitDamage(F.Hit(F.Other)).Result == EAdmission::Stale, "Ended source cannot admit a new hit");
    T.Check(L.SetLife({F.Player.Entity, 2}), "Source life changes");
    const auto PendingReceipt = L.FindReceipt(Hit.Receipt.Handle);
    T.Check(PendingReceipt && PendingReceipt->State == EReceiptState::Pending, "Source life change does not cancel landed axe-window packet");
    T.Check(L.Resolve(Hit.Receipt.Handle, EOutcome::Committed) == EResolve::Resolved, "Pending hit resolves after source death/new life");
    T.Check(L.Resolve(Hit.Receipt.Handle, EOutcome::Committed) == EResolve::AlreadyFinal, "Repeated resolve cannot apply twice");
    T.Check(L.Resolve(Hit.Receipt.Handle, EOutcome::Rejected) == EResolve::AlreadyFinal, "Terminal committed receipt cannot be rewritten");
    const auto CommittedReceipt = L.FindReceipt(Hit.Receipt.Handle);
    T.Check(CommittedReceipt && CommittedReceipt->State == EReceiptState::Committed, "Committed state is immutable");
    const auto NewAction = L.BeginAction({F.Player.Entity, 2});
    const auto Unspent = L.AdmitResource({NewAction, 1, EResourceKind::WeaponUse, 1});
    L.SetLife({F.Player.Entity, 3});
    T.Check(L.Resolve(Unspent.Receipt.Handle, EOutcome::Committed) == EResolve::Stale, "Source life invalidates pending resource even without explicit EndAction");
}

void TargetLifeAndExplicitRejection(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    const auto Hit = L.AdmitDamage(F.Hit(F.Enemy));
    const auto OtherHit = L.AdmitDamage(F.Hit(F.Other));
    T.Check(L.SetLife({F.Enemy.Entity, 2}), "Target is restored into a new life");
    T.Check(L.Resolve(Hit.Receipt.Handle, EOutcome::Committed) == EResolve::Stale, "Old pending packet cannot damage revived target");
    const auto InvalidatedReceipt = L.FindReceipt(Hit.Receipt.Handle);
    T.Check(InvalidatedReceipt && InvalidatedReceipt->State == EReceiptState::Invalidated, "Target life marks pending packet invalidated");
    T.Check(L.Resolve(OtherHit.Receipt.Handle, EOutcome::Rejected) == EResolve::Resolved, "Explicit rule adjudication can reject pending damage");
    T.Check(L.Resolve(OtherHit.Receipt.Handle, EOutcome::Committed) == EResolve::AlreadyFinal, "Rejected packet can never later commit");
    const auto RejectedReceipt = L.FindReceipt(OtherHit.Receipt.Handle);
    T.Check(RejectedReceipt && RejectedReceipt->State == EReceiptState::Rejected, "Rejected terminal state is immutable");
    T.Check(L.AdmitDamage(F.Hit(F.Enemy)).Result == EAdmission::Stale, "Old target token is rejected");
    T.Check(L.AdmitDamage(F.Hit({F.Enemy.Entity, 2})).Result == EAdmission::Admitted, "Independent new-life hit has a new target key");
}

void ReleasedEmitterLifetime(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    const auto Parent = L.AdmitResource(F.Resource());
    T.Check(L.ReleaseEmitter(Parent.Receipt.Handle, 1).Result == EEmission::Invalid, "Pending resource cannot release an effect");
    L.Resolve(Parent.Receipt.Handle, EOutcome::Committed);
    const auto Release = L.ReleaseEmitter(Parent.Receipt.Handle, 1);
    const auto Emitter = Release.Handle;
    const auto Repeat = L.ReleaseEmitter(Parent.Receipt.Handle, 1);
    T.Check(Release.Result == EEmission::Created && Emitter.Id != 0, "Initial release explicitly permits one spawn");
    T.Check(Repeat.Result == EEmission::Existing && Repeat.Handle.Id == Emitter.Id, "Duplicate release returns existing identity without permission to spawn");
    int SpawnCount = 0;
    for (const auto& Result : {Release, Repeat, L.ReleaseEmitter(Parent.Receipt.Handle, 1)})
        if (Result.Result == EEmission::Created) ++SpawnCount;
    T.Check(SpawnCount == 1, "Repeated release cannot request another actor spawn");
    L.EndAction(F.Action); L.SetLife({F.Player.Entity, 2});
    const auto Hit = L.AdmitDamage({FEmitterDamage{Emitter, EEmitterDamageKind::AreaTick, 1}, F.Enemy});
    T.Check(Hit.Result == EAdmission::Admitted, "Released effect survives source action exit and life replacement");
    T.Check(L.ReleaseEmitter(Parent.Receipt.Handle, 2).Result == EEmission::Stale, "Old committed action cannot mint a new effect after exit");
    T.Check(L.RetireEmitter(Emitter), "Explicitly retire effect");
    T.Check(L.Resolve(Hit.Receipt.Handle, EOutcome::Committed) == EResolve::Resolved, "Retirement cannot undo damage already admitted at impact");
    T.Check(L.AdmitDamage({FEmitterDamage{Emitter, EEmitterDamageKind::AreaTick, 2}, F.Enemy}).Result == EAdmission::Stale, "Retired effect cannot admit next tick");
    const auto Retired = L.ReleaseEmitter(Parent.Receipt.Handle, 1);
    T.Check(Retired.Result == EEmission::Retired && Retired.Handle.Id == Emitter.Id && !L.RetireEmitter(Emitter), "Duplicate release explicitly reports retired identity and cannot resurrect it");
    T.Check(L.AdmitDamage({FRootMineDamage{Emitter}, F.Other}).Result == EAdmission::Invalid, "Ordinary projectile cannot claim root-ring policy");
}

void TwoVitalsSources(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    const FDamageRequest Water{FVitalsDamage{1, F.Player, EVitalsSource::Dehydration, 1}, F.Player};
    const FDamageRequest Food{FVitalsDamage{1, F.Player, EVitalsSource::Starvation, 1}, F.Player};
    const auto W = L.AdmitDamage(Water), S = L.AdmitDamage(Food);
    T.Check(W.Result == EAdmission::Admitted && S.Result == EAdmission::Admitted && W.Receipt.Handle.Id != S.Receipt.Handle.Id,
        "Same-time water and food periods are two independent packets");
    T.Check(L.AdmitDamage(Water).Result == EAdmission::Duplicate && L.AdmitDamage(Food).Result == EAdmission::Duplicate,
        "Each resource period separately rejects replay");
    T.Check(L.AdmitDamage({FVitalsDamage{1, F.Player, EVitalsSource::Dehydration, 2}, F.Player}).Result == EAdmission::Admitted,
        "Next water period remains legal");
    T.Check(L.AdmitDamage({FVitalsDamage{1, F.Player, EVitalsSource::Dehydration, 3}, F.Enemy}).Result == EAdmission::Invalid,
        "Vitals source cannot be redirected to another target");
}

void SharedRootRing(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    const auto Deployment = L.AdmitResource(F.Resource(1, EResourceKind::RootDeployment));
    L.Resolve(Deployment.Receipt.Handle, EOutcome::Committed);
    std::array<FEmitterId, 6> Mines;
    FReceiptId First;
    for (std::size_t I = 0; I < Mines.size(); ++I)
    {
        Mines[I] = L.ReleaseEmitter(Deployment.Receipt.Handle, static_cast<FId>(I + 1)).Handle;
        const auto Hit = L.AdmitDamage({FRootMineDamage{Mines[I]}, F.Enemy});
        T.Check(Hit.Result == (I == 0 ? EAdmission::Admitted : EAdmission::Duplicate), "Six mines share one target entry for the whole root episode");
        if (I == 0) First = Hit.Receipt.Handle;
        else T.Check(Hit.Receipt.Handle.Id == First.Id, "Mine identity does not split root hit receipt");
    }
    L.Resolve(First, EOutcome::Committed);
    T.Check(L.AdmitDamage({FEmitterDamage{Mines[1], EEmitterDamageKind::AreaTick, 99}, F.Enemy}).Result == EAdmission::Invalid,
        "Root mine cannot bypass ring ledger by pretending to be an ordinary emitter packet");
    T.Check(L.AdmitDamage({FRootMineDamage{Mines[0]}, F.Other}).Result == EAdmission::Admitted, "A different entity can still be hit");
    L.SetLife({F.Enemy.Entity, 2});
    T.Check(L.AdmitDamage({FRootMineDamage{Mines[2]}, {F.Enemy.Entity, 2}}).Result == EAdmission::Duplicate,
        "Same target entity remains recorded across revival within the same root episode");
    T.Check(L.AdmitDamage({FRootMineDamage{Mines[2]}, F.Enemy}).Result == EAdmission::Stale, "Hit admission still requires current target Life");
    const auto Redeploy = L.AdmitResource(F.Resource(2, EResourceKind::RootDeployment));
    L.Resolve(Redeploy.Receipt.Handle, EOutcome::Committed);
    T.Check(L.AdmitDamage({FRootMineDamage{L.ReleaseEmitter(Redeploy.Receipt.Handle, 1).Handle}, {F.Enemy.Entity, 2}}).Result == EAdmission::Duplicate,
        "Another deployment in the same root action keeps its shared hit-set");
    const auto NewRoot = L.BeginAction(F.Player);
    const auto NewDeployment = L.AdmitResource({NewRoot, 1, EResourceKind::RootDeployment, 1});
    L.Resolve(NewDeployment.Receipt.Handle, EOutcome::Committed);
    T.Check(L.AdmitDamage({FRootMineDamage{L.ReleaseEmitter(NewDeployment.Receipt.Handle, 1).Handle}, {F.Enemy.Entity, 2}}).Result == EAdmission::Admitted,
        "A genuinely new root episode has an independent hit-set");
}

void EpochAndIdentityRules(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    T.Check(!L.SetWorldEpoch(0) && !L.SetWorldEpoch(1), "Zero/equal epoch cannot reset ledger");
    T.Check(!L.SetLife({0, 1}) && !L.SetLife({1, 0}) && !L.SetLife(F.Player), "Zero/equal Life identity cannot replace existing actor");
    T.Check(L.BeginAction({1, 0}).Id == 0, "Default/invalid life cannot issue an action");
    T.Check(L.AdmitResource({}).Result == EAdmission::Invalid && L.AdmitDamage({}).Result == EAdmission::Invalid,
        "Zero-initialized requests are never valid");
    const auto Legacy = L.AdmitDamage({FActionDamage{F.Action, 1, EActionDamageKind::LegacyExternal, 1}, F.Enemy});
    T.Check(Legacy.Result == EAdmission::Admitted, "Explicit imported legacy domain is supported");
    T.Check(L.AdmitDamage(F.Hit(F.Enemy)).Result == EAdmission::Admitted, "Legacy and native melee keys cannot alias");
    T.Check(L.AdmitDamage({FActionDamage{F.Action, 1, static_cast<EActionDamageKind>(255), 1}, F.Enemy}).Result == EAdmission::Invalid,
        "Out-of-domain action damage kind rejected after extending legacy domain");
    auto Bad = F.Resource(); Bad.Ordinal = 0;
    T.Check(L.AdmitResource(Bad).Result == EAdmission::Invalid, "Zero event ordinal rejected");
    Bad = F.Resource(); Bad.Kind = static_cast<EResourceKind>(255);
    T.Check(L.AdmitResource(Bad).Result == EAdmission::Invalid, "Out-of-domain typed kind rejected");
    const auto R = L.AdmitResource(F.Resource()); L.Resolve(R.Receipt.Handle, EOutcome::Committed);
    const auto E = L.ReleaseEmitter(R.Receipt.Handle, 1).Handle;
    const auto Pending = L.AdmitDamage(F.Hit(F.Enemy));
    T.Check(L.SetWorldEpoch(2) && !L.SetWorldEpoch(1), "Epoch can only move forward");
    T.Check(L.Resolve(R.Receipt.Handle, EOutcome::Rejected) == EResolve::Stale &&
        L.Resolve(Pending.Receipt.Handle, EOutcome::Committed) == EResolve::Stale, "Old committed and pending handles cannot cross world epoch");
    T.Check(!L.FindReceipt(R.Receipt.Handle).has_value(), "Reset world exposes no old receipt");
    T.Check(L.ReleaseEmitter(R.Receipt.Handle, 1).Result == EEmission::Stale, "Old release parent cannot be reused in a new epoch");
    T.Check(L.ReleaseEmitter({}, 1).Result == EEmission::Invalid && L.ReleaseEmitter({2, 1}, 0).Result == EEmission::Invalid,
        "Zero parent and emission ordinal are invalid");
    L.SetLife(F.Player); L.SetLife(F.Enemy);
    const auto A2 = L.BeginAction(F.Player);
    const auto R2 = L.AdmitResource({A2, 1, EResourceKind::WeaponUse, 1});
    L.Resolve(R2.Receipt.Handle, EOutcome::Committed);
    const auto E2 = L.ReleaseEmitter(R2.Receipt.Handle, 1).Handle;
    T.Check(A2.Id > F.Action.Id && R2.Receipt.Handle.Id > Pending.Receipt.Handle.Id && E2.Id > E.Id,
        "Action/receipt/emitter IDs remain monotonic across epoch reset");
    T.Check(L.AdmitDamage({FEmitterDamage{E, EEmitterDamageKind::AreaTick, 1}, F.Enemy}).Result == EAdmission::Stale,
        "Old emitter cannot affect actors registered in new world epoch");
    L.SetLife({1, 2});
    T.Check(!L.SetLife({1, 1}) && L.BeginAction(F.Player).Id == 0, "Life rollback cannot revive old source token");
    T.Check(L.SetWorldEpoch(std::numeric_limits<FId>::max()) && !L.SetWorldEpoch(1), "Maximum epoch never wraps to an old epoch");
}

void PhaseAndResourcePolicies(FContext& T)
{
    FFixture F; auto& L = F.Ledger;
    const auto First = L.AdmitResource(F.Resource());
    auto Phase2 = F.Resource(); Phase2.Phase = 2;
    T.Check(L.AdmitResource(Phase2).Result == EAdmission::Admitted, "New semantic phase can submit its independent operation");
    const auto Reload = L.AdmitResource(F.Resource(1, EResourceKind::ReloadShell));
    T.Check(Reload.Result == EAdmission::Admitted, "Reload shell and weapon use have separate typed keys");
    L.Resolve(Reload.Receipt.Handle, EOutcome::Committed);
    T.Check(L.ReleaseEmitter(Reload.Receipt.Handle, 1).Result == EEmission::Invalid, "Reload cannot generate a projectile");
    T.Check(L.Resolve(First.Receipt.Handle, EOutcome::Rejected) == EResolve::Resolved, "Failed external preparation can terminally reject reservation");
    T.Check(L.AdmitResource(F.Resource()).Result == EAdmission::Duplicate, "Rejected resource operation cannot retry under the same key");
    T.Check(L.Resolve(First.Receipt.Handle, EOutcome::Committed) == EResolve::AlreadyFinal, "Rejected resource cannot be rewritten committed");
    T.Check(L.ReleaseEmitter(First.Receipt.Handle, 1).Result == EEmission::Invalid, "Rejected resource cannot release effect");
}
}

#if defined(DW_COMBAT_LEDGER_STANDALONE)
int main()
{
    int Failures = 0, Checks = 0;
    struct FCounters { int* Failures; int* Checks; } Counters{&Failures, &Checks};
    DWCombatCommitTests::FContext T{&Counters, [](void* Owner, const char* Label, bool Passed)
    {
        auto& C = *static_cast<FCounters*>(Owner); ++*C.Checks;
        if (!Passed) { ++*C.Failures; std::fprintf(stderr, "FAIL: %s\n", Label); }
    }};
    using namespace DWCombatCommitTests;
    ReentryAndDistinctRequests(T); PendingSurvivesSourceExit(T); TargetLifeAndExplicitRejection(T);
    ReleasedEmitterLifetime(T); TwoVitalsSources(T); SharedRootRing(T); EpochAndIdentityRules(T); PhaseAndResourcePolicies(T);
    std::printf("DWCombatCommitLedger: 8 scenarios, %d checks, %d failures\n", Checks, Failures);
    return Failures == 0 ? 0 : 1;
}
#else
namespace DWCombatCommitTests
{
void AutomationSink(void* Owner, const char* Label, bool Passed)
{ static_cast<FAutomationTestBase*>(Owner)->TestTrue(UTF8_TO_TCHAR(Label), Passed); }
}
#define DW_LEDGER_AUTOMATION(Name) \
    IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatLedger##Name, "DoughWorld.Combat.CommitLedger." #Name, \
        EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter) \
    bool FDWCombatLedger##Name::RunTest(const FString& Parameters) \
    { DWCombatCommitTests::FContext Context{this, DWCombatCommitTests::AutomationSink}; DWCombatCommitTests::Name(Context); return true; }
DW_LEDGER_AUTOMATION(ReentryAndDistinctRequests)
DW_LEDGER_AUTOMATION(PendingSurvivesSourceExit)
DW_LEDGER_AUTOMATION(TargetLifeAndExplicitRejection)
DW_LEDGER_AUTOMATION(ReleasedEmitterLifetime)
DW_LEDGER_AUTOMATION(TwoVitalsSources)
DW_LEDGER_AUTOMATION(SharedRootRing)
DW_LEDGER_AUTOMATION(EpochAndIdentityRules)
DW_LEDGER_AUTOMATION(PhaseAndResourcePolicies)
#undef DW_LEDGER_AUTOMATION
#endif
#endif
