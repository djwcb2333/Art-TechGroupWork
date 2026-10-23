#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatCoreInjection.h"
#include "DWCombatRules.h"
#include "Misc/AutomationTest.h"
#include <cmath>
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionPlanTest,"DoughWorld.Combat.CoreInjection.ClockReadyIsNotCommit",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionPlanTest::RunTest(const FString& Parameters)
{
    FDWCoreInjectionPlan Plan;
    TestTrue(TEXT("No initial boundary"),std::isinf(Plan.GetNextBoundarySeconds()));
    TestFalse(TEXT("Reject zero duration"),Plan.Start(0.));TestFalse(TEXT("Reject infinity"),Plan.Start(std::numeric_limits<double>::infinity()));
    TestEqual(TEXT("Rejected starts issue no identity"),Plan.GetSerial(),uint64(0));
    TestTrue(TEXT("Start isolated duration"),Plan.Start(.6));const uint64 First=Plan.GetSerial();
    TestFalse(TEXT("No overwrite of an active clock"),Plan.Start(1.));TestFalse(TEXT("Incomplete cannot commit"),Plan.Commit());
    const double Gap=std::ldexp(1.,-30);
    TestTrue(TEXT("Advance before boundary"),Plan.Advance(.6-Gap));TestFalse(TEXT("Not prematurely ready"),Plan.IsReady());
    TestTrue(TEXT("Zero adds no time"),Plan.Advance(0.));TestFalse(TEXT("Still incomplete"),Plan.IsReady());TestFalse(TEXT("Negative delta rejected"),Plan.Advance(-1.));
    TestTrue(TEXT("Advance exact remaining time"),Plan.Advance(Plan.GetRemainingSeconds()));
    TestTrue(TEXT("Ready is still pending"),Plan.IsReady()&&Plan.IsPending());TestEqual(TEXT("Due immediately"),Plan.GetNextBoundarySeconds(),0.);
    TestEqual(TEXT("Identity unchanged"),Plan.GetSerial(),First);
    TestTrue(TEXT("One commit"),Plan.Commit());TestFalse(TEXT("No double commit"),Plan.Commit());
    TestTrue(TEXT("No committed boundary"),std::isinf(Plan.GetNextBoundarySeconds()));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionCancelTest,"DoughWorld.Combat.CoreInjection.CancelIncludingReadyDoesNotBecomeCommitted",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionCancelTest::RunTest(const FString& Parameters)
{
    FDWCoreInjectionPlan Plan;Plan.Start(.6);const uint64 First=Plan.GetSerial();Plan.Advance(.3);Plan.Cancel();
    TestFalse(TEXT("Cancelled cannot commit"),Plan.Commit());TestFalse(TEXT("Old delta cannot revive"),Plan.Advance(100.));
    TestTrue(TEXT("New deliberate request"),Plan.Start(.6));TestTrue(TEXT("Distinct request identity"),Plan.GetSerial()>First);
    Plan.Advance(.6);TestTrue(TEXT("Ready before same-instant damage"),Plan.IsReady());Plan.Cancel();TestFalse(TEXT("Ready can still be cancelled before commit"),Plan.Commit());
    TestTrue(TEXT("Next deliberate request"),Plan.Start(.6));Plan.Advance(.6);Plan.Commit();Plan.Cancel();
    TestTrue(TEXT("Committed terminal fact cannot be rewritten"),Plan.GetPhase()==EDWCoreInjectionPhase::Committed);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreInjectionRulesTest,"DoughWorld.Combat.CoreInjection.ConfigUsesFormalRulesAndPreservesTuning",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreInjectionRulesTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWCoreInjectionConfig Config;
    if(!TestTrue(TEXT("Formal payload loads"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
    if(!TestTrue(TEXT("Build core configuration"),FDWCoreInjectionConfig::Build(*Rules,Config,Error)))return false;
    double Duration=0.;int32 Cost=0;Rules->TryGetNumber(TEXT("form.injection_s"),EDWCombatRuleUnit::Seconds,Duration,Error);Rules->TryGetCount(TEXT("form.core_cost"),Cost,Error);
    TestEqual(TEXT("Duration is from formal rule"),Config.DurationSeconds,Duration);TestEqual(TEXT("Cost is from formal rule"),Config.CoreCost,Cost);
    auto Tuned=Config;Tuned.DurationSeconds=.375;Tuned.CoreCost=2;TestTrue(TEXT("Alternate positive tuning remains legal"),Tuned.IsValid());
    Tuned.DurationSeconds=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("NaN rejected"),Tuned.IsValid());
    Tuned=Config;Tuned.CoreCost=0;TestFalse(TEXT("Zero cost rejected"),Tuned.IsValid());return true;
}
#endif
