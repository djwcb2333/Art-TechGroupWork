#if WITH_DEV_AUTOMATION_TESTS
#include "DWYeastDashPlan.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatRules.h"
#include "Misc/AutomationTest.h"
#include <cmath>
#include <limits>

namespace DWYeastDashTests
{
FDWYeastDashConfig Config()
{
    FDWYeastDashConfig C;C.StartCost=10.;C.SpendPerSecond=30.;C.GainPerSecond=5.;C.SpecialFloor=0.;C.SpecialMax=100.;
    C.FormDuration=15.;C.MinimumSeconds=1.;C.SpeedCmPerSecond=1200.;C.CooldownSeconds=.2;
    C.Damage=30.;C.KnockbackCm=150.;C.KnockbackSeconds=.2;C.QueryRadiusCm=60.;C.HitsPerTarget=1;return C;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWYeastDashGateTest,"DoughWorld.Combat.YeastDash.GateAndFailedPress",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWYeastDashGateTest::RunTest(const FString&)
{
    const auto C=DWYeastDashTests::Config();FDWYeastDashPlan P;
    TestEqual(TEXT("Derived minimum budget"),C.MinimumBudget(),35.);
    TestFalse(TEXT("Initial25 cannot start"),P.Start(C,25.,15.,{1,0},{0,1},1));
    TestFalse(TEXT("Same failed press cannot auto-start once 35 reached"),P.Start(C,35.,13.,{1,0},{0,1},1));
    TestFalse(TEXT("Insufficient form fails"),P.Start(C,35.,.999,{1,0},{0,1},2));
    TestTrue(TEXT("Exactly gate + full minimum succeeds on new press"),P.Start(C,35.,1.,{3,4},{0,-1},3));
    TestTrue(TEXT("WASD direction wins and normalizes"),P.Direction().Equals(FVector2D(.6,.8),1.e-12));
    TestFalse(TEXT("Pressed again during active dash does not replace origin"),P.Start(C,100.,10.,{-1,0},{0,1},4));
    TestEqual(TEXT("Original origin survives rejected second press"),P.OriginPress(),uint64(3));
    TestFalse(TEXT("Unrelated release cannot end hold"),P.Release(4));TestTrue(TEXT("Original release accepted"),P.Release(3));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWYeastDashMinimumTest,"DoughWorld.Combat.YeastDash.MinimumAndContinuousBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWYeastDashMinimumTest::RunTest(const FString&)
{
    const auto C=DWYeastDashTests::Config();FDWYeastDashPlan Short,Long;FDWYeastDashStep S;
    TestTrue(TEXT("Short start"),Short.Start(C,100.,15.,{0,0},{0,5},1));Short.Release(1);
    TestTrue(TEXT("Minimum clip"),Short.Advance(10.,90.,15.,S));
    TestEqual(TEXT("Short movement is full1second"),S.Seconds,1.);TestEqual(TEXT("Short travel12m"),S.DistanceCm,1200.);
    TestTrue(TEXT("Release marks a clean endpoint"),S.End==EDWYeastDashEnd::Released);Short.End(S.End);
    TestEqual(TEXT("Cooldown starts at end, not start"),Short.CooldownSeconds(),.2);
    TestTrue(TEXT("Long start"),Long.Start(C,100.,15.,{1,0},{0,1},2));
    TestTrue(TEXT("Two seconds continuous"),Long.Advance(2.,90.,15.,S));TestEqual(TEXT("Long distance doubles"),S.DistanceCm,2400.);
    TestTrue(TEXT("Holding does not stop at minimum"),S.End==EDWYeastDashEnd::None);
    Long.Release(2);TestEqual(TEXT("Release after minimum is zero-time boundary"),Long.NextBoundary(40.,13.),0.);
    Long.Advance(0.,40.,13.,S);TestTrue(TEXT("No extra epsilon travel"),S.Seconds==0.&&S.DistanceCm==0.&&S.End==EDWYeastDashEnd::Released);
    FDWYeastDashPlan Exact;Exact.Start(C,35.,15.,{1,0},{0,1},1);
    Exact.Advance(50.,25.,15.,S);TestEqual(TEXT("Gate only deducts start10: remaining25 supports one second"),S.Seconds,1.);
    TestTrue(TEXT("Floor stops ability, not form"),S.End==EDWYeastDashEnd::SpecialFloor);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWYeastDashStopTest,"DoughWorld.Combat.YeastDash.WallControlCooldownAndRestoreSeam",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWYeastDashStopTest::RunTest(const FString&)
{
    const auto C=DWYeastDashTests::Config();FDWYeastDashPlan P;FDWYeastDashStep S;
    P.Start(C,100.,15.,{1,0},{0,1},1);P.Advance(.125,90.,15.,S);P.End(EDWYeastDashEnd::Wall);
    TestEqual(TEXT("Wall stops before minimum with no distance compensation"),P.ElapsedSeconds(),.125);
    TestFalse(TEXT("Repeated ending cannot reset cooldown"),P.End(EDWYeastDashEnd::Control));
    P.AdvanceCooldown(.125);const double Remaining=P.CooldownSeconds();
    TestFalse(TEXT("Cooldown blocks fresh physical press"),P.Start(C,100.,15.,{1,0},{0,1},2));
    TestFalse(TEXT("Malformed D22 seam rejected without clearing existing cooldown"),P.RestoreCooldown(-1.));
    TestEqual(TEXT("Invalid restore leaves state"),P.CooldownSeconds(),Remaining);
    TestTrue(TEXT("Explicit cooldown seam restores remaining"),P.RestoreCooldown(Remaining));
    P.AdvanceCooldown(Remaining);TestFalse(TEXT("Old rejected press cannot restart after CD"),P.Start(C,100.,15.,{1,0},{0,1},2));
    TestTrue(TEXT("New press after CD"),P.Start(C,100.,15.,{1,0},{0,1},3));P.End(EDWYeastDashEnd::Control);
    TestTrue(TEXT("Explicit control ends"),!P.IsActive()&&P.LastEnd()==EDWYeastDashEnd::Control);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWYeastDashRulesTest,"DoughWorld.Combat.YeastDash.StrictRulesAndCombinedSpecialRate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWYeastDashRulesTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWYeastDashConfig Dash;FDWCombatPlayerVitalsConfig VC;
    if(!TestTrue(TEXT("Read actual numeric payload"),FDWCombatRules::LoadProjectPayload(Rules,Error))||
       !TestTrue(TEXT("Explicit config bridge"),FDWYeastDashConfig::Build(*Rules,Dash,Error))||
       !TestTrue(TEXT("Vitals config"),FDWCombatPlayerVitalsConfig::Build(*Rules,VC,Error)))return false;
    double SpeedMeters=0.;Rules->TryGetNumber(TEXT("yeast.dash.speed_mps"),EDWCombatRuleUnit::MetersPerSecond,SpeedMeters,Error);
    TestEqual(TEXT("Centimeter conversion occurs once"),Dash.SpeedCmPerSecond,SpeedMeters*100.);
    FDWCombatPlayerVitalsRuntime V;V.Initialize(VC,EDWCombatVitalsReset::NewCharacter);V.EnterCommittedForm(EDWCombatBaseForm::Yeast);
    V.SetSpecialAfterCommittedCost(VC.Vitals.SpecialMax);FDWCombatPlayerVitalsStep Step;
    const double Dt=FMath::Min(.125,FMath::Min(VC.FormDurationSeconds*.25,(VC.Vitals.SpecialMax-VC.Vitals.SpecialFloor)/Dash.NetSpend()*.25));
    TestTrue(TEXT("Combined gain minus spend accepted at cap"),V.AdvanceNext(Dt,VC.Vitals.HealthMax,Step,Dash.SpendPerSecond));
    TestTrue(TEXT("Cap does not discard gain before subtracting cost"),FMath::IsNearlyEqual(V.GetSpecial(),VC.Vitals.SpecialMax-Dash.NetSpend()*Step.AdvancedSeconds,1.e-10));
    const double Before=V.GetSpecial();TestFalse(TEXT("Invalid spend rejected"),V.AdvanceNext(.1,VC.Vitals.HealthMax,Step,std::numeric_limits<double>::quiet_NaN()));
    TestEqual(TEXT("Invalid rate has no mutation"),V.GetSpecial(),Before);
    auto Invalid=DWYeastDashTests::Config();Invalid.SpendPerSecond=Invalid.GainPerSecond;TestFalse(TEXT("Non-decreasing budget invalid"),Invalid.IsValid());
    Invalid=DWYeastDashTests::Config();Invalid.SpeedCmPerSecond=std::numeric_limits<double>::infinity();TestFalse(TEXT("Infinity invalid"),Invalid.IsValid());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWYeastDashDebitTest,"DoughWorld.Combat.YeastDash.TypedStartDebitPreservesCompensatedResource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWYeastDashDebitTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWCombatPlayerVitalsConfig C;
    if(!FDWCombatRules::LoadProjectPayload(Rules,Error)||!FDWCombatPlayerVitalsConfig::Build(*Rules,C,Error))return false;
    // Explicit arithmetic fixture: this does not install or alter production tuning.
    C.Vitals.SpecialFloor=0.;C.Vitals.SpecialMax=100.;C.Vitals.NormalSpecialValue=0.;C.InitialFormSpecial=25.;
    FDWCombatPlayerVitalsRuntime V;if(!V.Initialize(C,EDWCombatVitalsReset::NewCharacter)||!V.EnterCommittedForm(EDWCombatBaseForm::Yeast))return false;
    auto S=V.Capture(C.Vitals.HealthMax);S.Special=64.;S.SpecialGrowthCarry=std::ldexp(1.,-50);
    if(!V.Restore(C,S))return false;
    TestTrue(TEXT("Typed fee commits"),V.DebitSpecialAfterCommittedCost(16.));
    auto After=V.Capture(C.Vitals.HealthMax);
    TestEqual(TEXT("Fee subtracts exactly once"),After.Special,48.);
    TestEqual(TEXT("Sub-ULP accumulated remainder is not discarded"),After.SpecialGrowthCarry,S.SpecialGrowthCarry);
    const uint64 Revision=V.GetMutationRevision();
    TestFalse(TEXT("Unaffordable fee rejected atomically"),V.DebitSpecialAfterCommittedCost(49.));
    TestEqual(TEXT("Rejected fee retains revision"),V.GetMutationRevision(),Revision);
    TestEqual(TEXT("Rejected fee retains carry"),V.Capture(C.Vitals.HealthMax).SpecialGrowthCarry,After.SpecialGrowthCarry);return true;
}
#endif
