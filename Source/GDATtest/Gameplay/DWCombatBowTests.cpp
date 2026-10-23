#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatBow.h"
#include "DWCombatRules.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBowPlanTest,"DoughWorld.Combat.Bow.PhaseAndReleasedSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBowPlanTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FDWCombatRules> R;FString Error;FDWBowConfig C;
    if(!TestTrue(TEXT("Strict rules"),FDWCombatRules::LoadProjectPayload(R,Error)&&FDWBowConfig::Build(*R,C,Error)))return false;
    FDWBowPlan P;TestTrue(TEXT("Tap begins windup"),P.Start(C,EDWBowForm::Normal));double Taken=0.;
    P.Advance(P.Remaining(),Taken);TestTrue(TEXT("One release boundary"),P.EnterNext(EDWBowForm::MoldRooted)&&P.Phase==EDWBowPhase::Release);
    const auto Shot=C.Shot(EDWBowForm::MoldRooted);TestEqual(TEXT("Root damage frozen at release"),Shot.Damage,C.Base.Damage*C.RootDamageMultiplier);
    P.EnterNext(EDWBowForm::MoldRooted);TestEqual(TEXT("Recovery snapshots current root rate"),P.Remaining(),C.RecoverySeconds/C.RootRateMultiplier);
    P.Advance(P.Remaining(),Taken);P.EnterNext(EDWBowForm::Normal);TestEqual(TEXT("Next gap uses current normal rate"),P.Remaining(),(C.IntervalSeconds-C.WindupSeconds-C.RecoverySeconds)/C.NormalRateMultiplier);
    TestEqual(TEXT("Released area has fixed life"),Shot.Area.DurationSeconds,C.Base.Area.DurationSeconds);
    auto Bad=C;Bad.RecoverySeconds=C.IntervalSeconds;TestFalse(TEXT("Impossible phase budget rejected"),Bad.IsValid());return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGroundAreaClockTest,"DoughWorld.Combat.GroundAreas.NoFirstTickAndPersistentTargetPhase",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGroundAreaClockTest::RunTest(const FString& Parameters)
{
    FDWCombatAreaTargetClock C;TestTrue(TEXT("First cover initializes one target clock"),C.Start(10.,.5));double Due=0.;uint64 Ordinal=0;
    TestFalse(TEXT("No first-contact hit"),C.ConsumeNext(10.,Due,Ordinal));TestFalse(TEXT("Changing pools cannot restart the clock"),C.Start(10.25,.5));
    TestTrue(TEXT("First complete interval"),C.ConsumeNext(10.5,Due,Ordinal));TestEqual(TEXT("One tick"),Ordinal,uint64(1));
    TestFalse(TEXT("Same-time reentry cannot consume again"),C.ConsumeNext(10.5,Due,Ordinal));
    for(int I=2;I<=12;++I){TestTrue(TEXT("Phase continues even with no coverage"),C.ConsumeNext(10.+I*.5,Due,Ordinal));TestEqual(TEXT("Ordinal retained"),Ordinal,uint64(I));}
    TestEqual(TEXT("Six-second last endpoint is the twelfth period"),Due,16.);TestEqual(TEXT("No thirteenth at lifetime end"),C.NextSeconds(),16.5);return true;
}
#endif
