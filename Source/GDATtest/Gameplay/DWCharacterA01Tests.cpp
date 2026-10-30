#include "DWCharacterA01.h"

#if defined(DW_A01_STANDALONE_TESTS) || WITH_DEV_AUTOMATION_TESTS
#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>
#include <string>

namespace DWA01Tests
{
using FSink=std::function<void(const char*,bool)>;
bool Near(double A,double B) { return std::abs(A-B)<1.e-8; }
struct FTotals { double Damage=0.,Regen=0.; int Steps=0; };
FTotals Advance(FDWCharacterA01Runtime& R,double Seconds,const FDWCharacterA01Activity& A={},double Partition=100000.)
{
    FTotals Total;
    while(Seconds>1.e-12&&R.GetState().CurrentHealth>0.&&Total.Steps<100000)
    {
        FDWCharacterA01Step Step;
        if(!R.AdvanceNext(std::min(Seconds,Partition),A,Step)||Step.AdvancedSeconds<=0.)break;
        Total.Damage+=Step.HungerDamageDue;Total.Regen+=Step.CurrentHealthRegenDue;++Total.Steps;
        // Simulate the executor's authoritative damage -> regen -> health import contract.
        double Health=std::max(0.,Step.AuthoritativeHealthBeforeAdvance-Step.HungerDamageDue);
        Health=std::min(Health,Step.HealthCeilingBeforeRecovery);
        if(Health>0.)Health=std::min(Step.HealthCeilingBeforeRecovery,Health+Step.CurrentHealthRegenDue);
        R.SyncCurrentHealth(Health);
        Seconds=std::max(0.,Seconds-Step.AdvancedSeconds);
    }
    return Total;
}
void Run(const FSink& Check)
{
    FDWCharacterA01Config C;FDWCharacterA01Runtime R;std::string Error;
    Check("trial config is valid",C.IsValid(&Error));
    Check("new character starts with configured resources",R.Initialize(C)&&Near(R.GetState().CurrentHealth,100.)&&Near(R.GetState().Water,100.)&&Near(R.GetState().Satiety,100.));
    Check("normal has independent uninitialized energies",R.GetState().Gas==0.&&R.GetState().Corruption==0.&&!R.GetState().bCorruptionInitialized);
    auto Bad=C;Bad.GasRegenPerSecond=std::numeric_limits<double>::infinity();
    Check("nonfinite config is rejected transactionally",!R.ApplyConfig(Bad,&Error)&&Near(R.GetConfig().GasRegenPerSecond,20.));
    Check("yeast entry fills gas and starts 180 seconds",R.EnterForm(EDWA01Form::Yeast)&&Near(R.GetState().Gas,100.)&&Near(R.GetState().FormRemainingSeconds,180.));
    Check("dash debit leaves corruption independent",R.SpendGas(30.)&&Near(R.GetState().Gas,70.)&&R.GetState().Corruption==0.);
    FDWCharacterA01Activity Sprint;Sprint.GasSpendPerSecond=10.;Sprint.bSuppressGasRegeneration=true;
    Advance(R,.5,Sprint);
    Check("sprint pays 10 per second without simultaneous regen",Near(R.GetState().Gas,65.));
    Advance(R,.5);
    Check("idle yeast restores 20 per second",Near(R.GetState().Gas,75.));
    FDWCharacterA01Activity Dash;Dash.bSuppressGasRegeneration=true;Advance(R,.25,Dash);
    Check("upfront-paid dash suppresses regen during its whole action",Near(R.GetState().Gas,75.));
    Advance(R,3.25);
    Check("cap debuff has not fired before five seconds",Near(R.GetState().RecordedCapLoss,0.)&&Near(R.GetState().CapLossElapsedSeconds,4.5));
    R.EnterForm(EDWA01Form::Yeast);
    Check("same essence refreshes without stacking or resetting cap timer",Near(R.GetState().FormRemainingSeconds,180.)&&Near(R.GetState().CapLossElapsedSeconds,4.5)&&Near(R.GetState().Gas,100.));
    R.EnterForm(EDWA01Form::Mold);
    Check("crossform fills only the entered energy and preserves timer",Near(R.GetState().Corruption,100.)&&Near(R.GetState().Gas,100.)&&Near(R.GetState().CapLossElapsedSeconds,4.5));
    FDWCharacterA01Step Step;R.AdvanceNext(.5,{},Step);
    Check("one shared cap event clamps current HP without a damage request",Step.bCapLossTick&&Near(R.GetState().RecordedCapLoss,1.)&&Near(R.GetState().CurrentHealth,99.)&&Step.bCurrentHealthClamped&&Step.HungerDamageDue==0.);
    Check("cap event is exactly once across form switching",Near(R.GetState().CapLossElapsedSeconds,0.));

    auto S=R.Capture();S.Form=EDWA01Form::Normal;S.FormRemainingSeconds=S.EntryDurationSeconds=0.;
    S.CapLossElapsedSeconds=0.;S.RecordedCapLoss=12.;S.CurrentHealth=50.;S.Gas=70.;S.Corruption=60.;
    S.CapRecoveryElapsedSeconds=S.NormalRegenElapsedSeconds=0.;R.Restore(C,S);
    R.AdvanceNext(2.,{},Step);
    Check("cap recovery changes loss only and never heals",Step.bCapRecoveryTick&&Near(R.GetState().RecordedCapLoss,11.)&&Near(R.GetState().CurrentHealth,50.)&&Step.CurrentHealthRegenDue==0.);
    R.AdvanceNext(2.,{},Step);R.AdvanceNext(1.,{},Step);
    Check("current HP regeneration is a separate 5 second request",Near(Step.CurrentHealthRegenDue,1.)&&Near(R.GetState().CurrentHealth,50.)&&Near(R.GetState().RecordedCapLoss,10.));
    R.SyncCurrentHealth(51.);
    Check("health import mirrors external committed healing",Near(R.GetState().CurrentHealth,51.));

    S=R.Capture();S.RecordedCapLoss=20.;S.CurrentHealth=20.;S.Satiety=0.;S.Water=0.;
    S.CapRecoveryElapsedSeconds=S.NormalRegenElapsedSeconds=0.;R.Restore(C,S);R.AdvanceNext(1.,{},Step);
    Check("starvation returns 3 HP damage per second without self-debit",Near(Step.HungerDamageDue,3.)&&Near(R.GetState().CurrentHealth,20.));
    Check("thirst reduces movement only",Near(R.MovementMultiplier(),.75)&&Near(Step.HungerDamageDue,3.));
    Check("starvation suppresses regeneration",Step.CurrentHealthRegenDue==0.&&R.GetState().NormalRegenElapsedSeconds==0.);
    R.SyncCurrentHealth(17.);
    S=R.Capture();S.Satiety=1.;S.Water=100.;S.RecordedCapLoss=0.;S.CapRecoveryElapsedSeconds=0.;R.Restore(C,S);
    R.AdvanceNext(10.,{},Step);
    Check("advance stops exactly where satiety reaches zero",Near(Step.AdvancedSeconds,4.5)&&Near(R.GetState().Satiety,0.)&&Step.HungerDamageDue==0.);
    R.AdvanceNext(1.,{},Step);
    Check("hunger begins after depletion rather than retroactively",Near(Step.HungerDamageDue,3.));

    R.Initialize(C);S=R.Capture();S.RecordedCapLoss=70.;S.CurrentHealth=30.;R.Restore(C,S);
    auto B6=C;B6.MaximumLossFraction=.4;R.ApplyConfig(B6);
    Check("b6 protects effective cap while retaining historical loss",Near(R.GetState().RecordedCapLoss,70.)&&Near(R.EffectiveMaxHealth(),60.)&&Near(R.GetState().CurrentHealth,30.));
    R.EnterForm(EDWA01Form::Yeast);Advance(R,10.);
    Check("b6 does not secretly add loss past its limit",Near(R.GetState().RecordedCapLoss,70.));
    R.ExitForm();Advance(R,2.);
    Check("b6 protection survives normal-form debt recovery",Near(R.GetState().RecordedCapLoss,69.)&&Near(R.EffectiveMaxHealth(),60.));
    auto Raised=B6;Raised.HealthyMax=120.;R.ApplyConfig(Raised);
    Check("raising maximum health never heals and never erases loss",Near(R.GetState().CurrentHealth,30.)&&Near(R.GetState().RecordedCapLoss,69.)&&Near(R.EffectiveMaxHealth(),72.));

    R.Initialize(C);R.EnterForm(EDWA01Form::Yeast);R.SpendGas(30.);Advance(R,4.);
    auto More=C;More.GasMax=130.;More.CorruptionMax=130.;More.YeastDurationSeconds=240.;R.ApplyConfig(More);
    Check("energy cap increase does not refill and b3 does not refresh running form",Near(R.GetState().Gas,100.)&&Near(R.GetState().Corruption,0.)&&Near(R.GetState().FormRemainingSeconds,176.));
    R.EnterForm(EDWA01Form::Yeast);
    Check("next essence reads increased duration and fills effective energy cap",Near(R.GetState().FormRemainingSeconds,240.)&&Near(R.GetState().Gas,130.)&&Near(R.GetState().CapLossElapsedSeconds,4.));
    R.ApplyConfig(C);
    Check("energy cap decrease clamps while running duration stays intact",Near(R.GetState().Gas,100.)&&Near(R.GetState().FormRemainingSeconds,240.));
    R.SyncCurrentHealth(0.);R.ApplyConfig(More);
    Check("death clears form and cap recompute never resurrects",R.GetState().Form==EDWA01Form::Normal&&R.GetState().CurrentHealth==0.&&!R.EnterForm(EDWA01Form::Mold));

    R.Initialize(C);R.EnterForm(EDWA01Form::Mold);
    Check("tool action accepts a unique monotonic parent action id",R.BeginToolAction(1,true,false)&&!R.BeginToolAction(1,true,false));
    auto Hit=R.ResolveToolHit(1,false,true);
    Check("invalid hit does not charge or decide the action",!Hit.bEnhanced&&!Hit.bChargedThisCall&&Near(R.GetState().Corruption,100.));
    Hit=R.ResolveToolHit(1,true,true);
    Check("first valid hit charges once and returns a damage multiplier",Hit.bEnhanced&&Hit.bChargedThisCall&&Near(Hit.DamageMultiplier,1.25)&&Near(Hit.HarvestMultiplier,1.)&&Near(R.GetState().Corruption,90.));
    Hit=R.ResolveToolHit(1,true,true);
    Check("another target in the same action shares enhancement without another fee",Hit.bEnhanced&&!Hit.bChargedThisCall&&Near(R.GetState().Corruption,90.));
    Advance(R,1.);
    Check("open tool action suppresses corruption recovery",Near(R.GetState().Corruption,90.));
    R.EndToolAction(1);Advance(R,1.);
    Check("tool end resumes corruption recovery at 1 per second",Near(R.GetState().Corruption,91.));
    R.BeginToolAction(2,false,true);Hit=R.ResolveToolHit(2,true,true);
    Check("gathering gets harvest strength rather than damage or reward count",Hit.bEnhanced&&Near(Hit.HarvestMultiplier,1.5)&&Near(Hit.DamageMultiplier,1.));
    R.EndToolAction(2);R.BeginToolAction(3,false,false);Hit=R.ResolveToolHit(3,true,true);
    Check("unlisted tool never gets corrupted",!Hit.bEnhanced&&!Hit.bChargedThisCall&&Near(R.GetState().Corruption,81.));R.EndToolAction(3);
    R.BeginToolAction(4,true,false);R.ExitForm();R.EnterForm(EDWA01Form::Mold);Hit=R.ResolveToolHit(4,true,true);
    Check("exit before impact permanently invalidates that pending enhancement",!Hit.bEnhanced&&!Hit.bChargedThisCall&&Near(R.GetState().Corruption,100.));R.EndToolAction(4);
    R.BeginToolAction(5,true,false);R.ResolveToolHit(5,true,true);R.ExitForm();Hit=R.ResolveToolHit(5,true,true);
    Check("exit after impact removes remaining enhancement and never refunds",!Hit.bEnhanced&&Near(R.GetState().Corruption,90.));R.EndToolAction(5);

    R.Initialize(C);R.EnterForm(EDWA01Form::Mold);S=R.Capture();S.Corruption=5.;R.Restore(C,S);
    R.BeginToolAction(10,true,false);Hit=R.ResolveToolHit(10,true,true);
    auto Cheaper=C;Cheaper.CorruptionCost=1.;R.ApplyConfig(Cheaper);Hit=R.ResolveToolHit(10,true,true);
    Check("first unaffordable valid hit locks that action plain",!Hit.bEnhanced&&!Hit.bChargedThisCall&&Near(R.GetState().Corruption,5.));
    R.EndToolAction(10);R.BeginToolAction(11,true,false);Hit=R.ResolveToolHit(11,true,true);
    Check("a later action can qualify with its own current budget",Hit.bEnhanced&&Hit.bChargedThisCall&&Near(R.GetState().Corruption,4.));R.EndToolAction(11);
    Check("ended ids cannot be restarted to obtain a second charge",!R.BeginToolAction(11,true,false));

    auto Short=C;Short.YeastDurationSeconds=5.;R.Initialize(Short);R.EnterForm(EDWA01Form::Yeast);R.AdvanceNext(20.,{},Step);
    Check("expiry is a boundary and retires the form",Near(Step.AdvancedSeconds,5.)&&Step.bFormExpired&&R.GetState().Form==EDWA01Form::Normal);
    Check("simultaneous final cap tick is not lost or duplicated",Near(R.GetState().RecordedCapLoss,1.)&&R.GetState().CapLossElapsedSeconds==0.);
    R.Initialize(C);R.EnterForm(EDWA01Form::Mold);Advance(R,8.125);S=R.Capture();
    FDWCharacterA01Runtime Restored;Check("snapshot restores without refreshing form or filling energies",Restored.Restore(C,S)&&Near(Restored.GetState().FormRemainingSeconds,171.875)&&Near(Restored.GetState().CapLossElapsedSeconds,3.125));
    auto Invalid=S;Invalid.CapLossElapsedSeconds=5.;
    Check("invalid restore is rejected without corrupting live state",!Restored.Restore(C,Invalid)&&Near(Restored.GetState().FormRemainingSeconds,171.875));
    FDWCharacterA01Runtime Whole,Partitioned;Whole.Initialize(C);Partitioned.Initialize(C);
    Whole.EnterForm(EDWA01Form::Mold);Partitioned.EnterForm(EDWA01Form::Mold);
    Advance(Whole,60.);Advance(Partitioned,60.,{},.013);
    Check("large and partitioned advances agree on loss, survival and form time",Near(Whole.GetState().RecordedCapLoss,Partitioned.GetState().RecordedCapLoss)&&Near(Whole.GetState().Satiety,Partitioned.GetState().Satiety)&&Near(Whole.GetState().Water,Partitioned.GetState().Water)&&Near(Whole.GetState().FormRemainingSeconds,Partitioned.GetState().FormRemainingSeconds));
    Check("negative/NaN time never mutates state",!Whole.AdvanceNext(-1.,{},Step)&&!Whole.AdvanceNext(std::numeric_limits<double>::quiet_NaN(),{},Step)&&Near(Whole.GetState().RecordedCapLoss,12.));
    auto Protected=C;Protected.MaximumLossFraction=0.;R.Initialize(Protected);S=R.Capture();S.RecordedCapLoss=10.;S.CurrentHealth=50.;
    R.Restore(Protected,S);auto NoRegen=Advance(R,5.);
    Check("M01 recorded debt without effective cap loss does not enable HP regeneration",Near(R.EffectiveMaxHealth(),100.)&&NoRegen.Regen==0.&&Near(R.GetState().CurrentHealth,50.));
    R.Initialize(C);R.EnterForm(EDWA01Form::Yeast);S=R.Capture();S.RecordedCapLoss=98.;S.CurrentHealth=2.;S.Satiety=0.;S.CapLossElapsedSeconds=4.5;
    R.Restore(C,S);R.AdvanceNext(.5,{},Step);
    const double AfterDamageThenClamp=std::min(Step.HealthCeilingBeforeRecovery,std::max(0.,Step.AuthoritativeHealthBeforeAdvance-Step.HungerDamageDue));
    R.SyncCurrentHealth(AfterDamageThenClamp);
    Check("C01 simultaneous hunger and cap decrease leaves 0.5 HP rather than falsely killing",Near(AfterDamageThenClamp,.5)&&Near(R.GetState().CurrentHealth,.5)&&Near(R.EffectiveMaxHealth(),1.));
    R.Initialize(C);S=R.Capture();S.RecordedCapLoss=10.;S.CurrentHealth=90.;S.NormalRegenElapsedSeconds=4.;R.Restore(C,S);R.AdvanceNext(1.,{},Step);
    const double AfterExternalDamageAndRegen=std::min(Step.HealthCeilingBeforeRecovery,Step.AuthoritativeHealthBeforeAdvance-10.+Step.CurrentHealthRegenDue);
    Check("C02 a full old HP mirror does not discard a due regeneration event",Near(Step.CurrentHealthRegenDue,1.)&&Near(AfterExternalDamageAndRegen,81.));
    const double DeadAtDamage=std::max(0.,Step.AuthoritativeHealthBeforeAdvance-100.);
    const double AfterFatal=DeadAtDamage>0.?std::min(Step.HealthCeilingBeforeRecovery,DeadAtDamage+Step.CurrentHealthRegenDue):0.;
    Check("C02 a due regeneration event never revives damage killed characters",AfterFatal==0.);
}
}

#if defined(DW_A01_STANDALONE_TESTS)
#include <iostream>
int main()
{
    int Checks=0,Failures=0;
    DWA01Tests::Run([&](const char* Name,bool Pass){++Checks;if(!Pass){++Failures;std::cerr<<"FAIL: "<<Name<<'\n';}});
    std::cout<<"A01 native checks: "<<Checks<<", failures: "<<Failures<<'\n';return Failures?1:0;
}
#elif WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCharacterA01ModelTest,"DoughWorld.LanguageIntegration.A01.ResourceModel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCharacterA01ModelTest::RunTest(const FString& Parameters)
{
    DWA01Tests::Run([this](const char* Name,bool Pass){TestTrue(UTF8_TO_TCHAR(Name),Pass);});return true;
}
#endif
#endif
