#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatMoldRoot.h"
#include "DWCombatRules.h"
#include "Misc/AutomationTest.h"
#include <cmath>
#include <limits>
namespace DWMoldRootTests
{
bool Config(FDWMoldRootConfig& C)
{TSharedPtr<const FDWCombatRules> R;FString E;return FDWCombatRules::LoadProjectPayload(R,E)&&FDWMoldRootConfig::Build(*R,C,E);}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootQualificationTest,"DoughWorld.Combat.MoldRoot.StrictPositiveBudgetAndGround",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootQualificationTest::RunTest(const FString& Parameters)
{
    FDWMoldRootConfig C;if(!TestTrue(TEXT("Formal rules"),DWMoldRootTests::Config(C)))return false;FDWMoldRootPlan P;
    const FVector Anchor(100,200,300);const double Exact=C.StartCost+C.SpecialFloor;
    TestFalse(TEXT("Exactly enough fee has zero duration, reject"),P.Start(C,EDWCombatBaseForm::Mold,Exact,true,Anchor));
    TestEqual(TEXT("Invalid start issues no episode"),P.GetSerial(),uint64(0));
    const double Above=std::nextafter(Exact,std::numeric_limits<double>::infinity());
    TestFalse(TEXT("Illegal ground does not cancel another action or grant stance"),P.Start(C,EDWCombatBaseForm::Mold,Above,false,Anchor));
    TestFalse(TEXT("Yeast cannot root"),P.Start(C,EDWCombatBaseForm::Yeast,Above,true,Anchor));
    TestTrue(TEXT("Positive finite duration accepted without arbitrary minimum"),P.Start(C,EDWCombatBaseForm::Mold,Above,true,Anchor));
    TestEqual(TEXT("Fixed anchor copied once"),P.GetAnchor(),Anchor);const uint64 Serial=P.GetSerial();
    TestFalse(TEXT("Active root cannot reset episode"),P.Start(C,EDWCombatBaseForm::Mold,100.,true,FVector::ZeroVector));
    TestEqual(TEXT("Episode unchanged"),P.GetSerial(),Serial);TestEqual(TEXT("Anchor unchanged"),P.GetAnchor(),Anchor);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootLifecycleTest,"DoughWorld.Combat.MoldRoot.EndIsImmediateAndNewEpisodeIsDistinct",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootLifecycleTest::RunTest(const FString& Parameters)
{
    FDWMoldRootConfig C;if(!DWMoldRootTests::Config(C))return false;FDWMoldRootPlan P;
    const double Start=C.StartCost+C.SpecialFloor+1.;P.Start(C,EDWCombatBaseForm::Mold,Start,true,FVector::ZeroVector);
    const uint64 First=P.GetSerial();P.End(EDWMoldRootEnd::Manual);TestFalse(TEXT("No visual-time extension"),P.IsActive());
    P.End(EDWMoldRootEnd::Death);TestTrue(TEXT("Second End cannot rewrite final reason"),P.GetEndReason()==EDWMoldRootEnd::Manual);
    TestTrue(TEXT("New deliberate root"),P.Start(C,EDWCombatBaseForm::Mold,Start,true,FVector(1,0,0)));
    TestTrue(TEXT("New episode identity"),P.GetSerial()>First);P.End(EDWMoldRootEnd::FormExpired);
    TestFalse(TEXT("Form expiry terminates stance immediately"),P.IsActive());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootRateTest,"DoughWorld.Combat.MoldRoot.NetSpendAndTuningRemainSeparateFromVisualDuration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootRateTest::RunTest(const FString& Parameters)
{
    FDWMoldRootConfig C;if(!DWMoldRootTests::Config(C))return false;FDWMoldRootPlan P;
    C.StartCost=3.;C.SpecialFloor=0.;C.SpendPerSecond=12.;C.SpecialGainPerSecond=2.;C.EnterVisualSeconds=7.;C.ExitVisualSeconds=9.;
    TestTrue(TEXT("Legal tuning is not clamped to published defaults"),C.IsValid());P.Start(C,EDWCombatBaseForm::Mold,23.,true,FVector::ZeroVector);
    TestEqual(TEXT("Net rate predicts 20/10 seconds, no visual delay"),P.EstimateResourceBoundarySeconds(20.),2.);
    TestEqual(TEXT("Exact committed floor produces zero boundary"),P.EstimateResourceBoundarySeconds(0.),0.);
    P.End(EDWMoldRootEnd::SpecialExhausted);C.SpendPerSecond=C.SpecialGainPerSecond;
    TestTrue(TEXT("Tuned non-draining root remains bounded by independent form clock"),P.Start(C,EDWCombatBaseForm::Mold,23.,true,FVector::ZeroVector));
    TestTrue(TEXT("No fake exhaustion for zero net loss"),std::isinf(P.EstimateResourceBoundarySeconds(20.)));
    C.MoveCentimetersPerSecond=1.;TestFalse(TEXT("Nonzero movement conflicts with fixed-position formal rule"),C.IsValid());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMoldRootSnapshotTest,"DoughWorld.Combat.MoldRoot.StrictSnapshotAndFreshIdentityBindingSeam",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMoldRootSnapshotTest::RunTest(const FString& Parameters)
{
    FDWMoldRootConfig C;if(!DWMoldRootTests::Config(C))return false;
    FDWMoldRootPlan P;FDWMoldRootSnapshot S{true,FVector(1,2,3)};
    TestFalse(TEXT("Rooted snapshot requires Mold"),P.Restore(C,S,EDWCombatBaseForm::Normal,1.,1.));
    TestFalse(TEXT("Rooted snapshot requires positive remaining form"),P.Restore(C,S,EDWCombatBaseForm::Mold,1.,0.));
    TestFalse(TEXT("Rooted snapshot cannot be at resource floor"),P.Restore(C,S,EDWCombatBaseForm::Mold,C.SpecialFloor,1.));
    TestTrue(TEXT("Ongoing episode can restore below entry fee without paying again"),P.Restore(C,S,EDWCombatBaseForm::Mold,C.SpecialFloor+.5,1.));
    const uint64 First=P.GetSerial();
    FDWMoldRootIdentity I{{2,10},{2,11},{12,3},2,P.GetSerial(),S.Anchor};
    TestTrue(TEXT("Explicit current-world identity candidate"),FDWMoldRootPlan::ValidateBinding(P,I,2,{12,3}));
    TestFalse(TEXT("Old world identity rejected"),FDWMoldRootPlan::ValidateBinding(P,I,3,{12,3}));
    TestFalse(TEXT("Old life identity rejected"),FDWMoldRootPlan::ValidateBinding(P,I,2,{12,4}));
    I.Receipt.Id=0;TestFalse(TEXT("Zero receipt never authenticates restored root"),FDWMoldRootPlan::ValidateBinding(P,I,2,{12,3}));
    S.Anchor.X=std::numeric_limits<double>::quiet_NaN();TestFalse(TEXT("Invalid anchor rejects atomically"),P.Restore(C,S,EDWCombatBaseForm::Mold,1.,1.));
    TestEqual(TEXT("Rejected snapshot keeps serial"),P.GetSerial(),First);
    S={false,FVector(1,0,0)};TestFalse(TEXT("Inactive snapshot cannot hide a stale anchor"),P.Restore(C,S,EDWCombatBaseForm::Normal,C.SpecialFloor,0.));
    return true;
}
#endif
