#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatPlayerVitals.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWSaveGame.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace DWPlayerVitalsTests
{
FDWCombatPlayerVitalsConfig Config()
{
    FDWCombatPlayerVitalsConfig C;
    auto& V=C.Vitals;V.HealthMax=120.;V.HealthFloor=10.;V.WaterMax=50.;V.WaterFloor=5.;
    V.SatietyMax=80.;V.SatietyFloor=4.;V.SpecialMax=100.;V.SpecialFloor=0.;
    V.InitialHealthFraction=.75;V.InitialWaterFraction=.5;V.InitialSatietyFraction=.5;
    V.RespawnHealthFraction=1.;V.RespawnWaterFraction=.25;V.RespawnSatietyFraction=.25;
    V.NormalSpecialValue=0.;V.WaterDrainPerSecond=.2;V.SatietyDrainPerSecond=.1;
    V.WaterDamagePeriodSeconds=V.SatietyDamagePeriodSeconds=5.;V.WaterDamagePerEvent=1.;V.SatietyDamagePerEvent=2.;
    C.RulesCombatSchema=1;C.FormDurationSeconds=15.;C.InitialFormSpecial=25.;C.SpecialGainPerSecond=5.;
    C.BaseMoveCentimetersPerSecond=600.;C.YeastMoveMultiplier=1.08;
    C.NormalIncomingMultiplier=1.;C.YeastIncomingMultiplier=1.15;C.MoldIncomingMultiplier=.8;return C;
}
bool Advance(FDWCombatPlayerVitalsRuntime& V,double Seconds,double Health)
{
    int32 Guard=0;
    while(Seconds>0.&&++Guard<1000){FDWCombatPlayerVitalsStep S;if(!V.AdvanceNext(Seconds,Health,S))return false;Seconds=FMath::Max(0.,Seconds-S.AdvancedSeconds);}
    return Seconds==0.;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerVitalsPartialTest,"DoughWorld.Combat.PlayerVitals.PartialPeriodsAndHealthOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerVitalsPartialTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerVitalsTests;const auto C=Config();FDWCombatPlayerVitalsRuntime A;
    if(!TestTrue(TEXT("Valid explicit fixture"),A.Initialize(C,EDWCombatVitalsReset::NewCharacter)))return false;
    auto S=A.Capture(70.);S.Water=C.Vitals.WaterFloor;S.Satiety=C.Vitals.SatietyFloor;
    if(!TestTrue(TEXT("Restore empty resources"),A.Restore(C,S)))return false;
    FDWCombatPlayerVitalsStep First;TestTrue(TEXT("Partial advance"),A.AdvanceNext(4.75,70.,First));
    TestEqual(TEXT("No early dehydration"),First.Dehydration.EventCount,uint64(0));
    FDWCombatPlayerVitalsRuntime B;TestTrue(TEXT("Save preserves partial periods"),B.Restore(C,A.Capture(70.)));
    FDWCombatPlayerVitalsStep Last;TestTrue(TEXT("Remaining quarter second"),B.AdvanceNext(.25,70.,Last));
    TestEqual(TEXT("One dehydration"),Last.Dehydration.EventCount,uint64(1));
    TestEqual(TEXT("One starvation, not combined identity"),Last.Starvation.EventCount,uint64(1));
    TestEqual(TEXT("Pure resource owner never deducts player HP"),B.Capture(70.).Health,70.);
    TestEqual(TEXT("Each channel has its own ordinal"),Last.StarvationOrdinal,uint64(1));
    TestEqual(TEXT("Normal form has no special growth"),B.GetSpecial(),0.);
    const auto Before=B.Capture(70.);TestFalse(TEXT("Invalid dt rejects"),B.AdvanceNext(std::numeric_limits<double>::quiet_NaN(),70.,Last));
    TestEqual(TEXT("Rejected step leaves clock"),B.Capture(70.).WaterEmptyElapsedSeconds,Before.WaterEmptyElapsedSeconds);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerVitalsFormTest,"DoughWorld.Combat.PlayerVitals.IndependentFormClockAndPartition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerVitalsFormTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerVitalsTests;auto C=Config();C.Vitals.WaterDrainPerSecond=C.Vitals.SatietyDrainPerSecond=0.;
    FDWCombatPlayerVitalsRuntime A,B;A.Initialize(C,EDWCombatVitalsReset::NewCharacter);B.Initialize(C,EDWCombatVitalsReset::NewCharacter);
    TestTrue(TEXT("Committed form seam"),A.EnterCommittedForm(EDWCombatBaseForm::Yeast));
    TestTrue(TEXT("Zero special spend valid"),A.SetSpecialAfterCommittedCost(0.));
    TestTrue(TEXT("Zero special does not end form"),A.GetForm()==EDWCombatBaseForm::Yeast);
    TestTrue(TEXT("Round-trip form state"),B.Restore(C,A.Capture(90.)));
    TestTrue(TEXT("Advance ten seconds"),Advance(A,10.,90.));
    for(int32 I=0;I<100;++I)if(!TestTrue(TEXT("Partition advance"),Advance(B,.1,90.)))return false;
    TestTrue(TEXT("Partition preserves remaining duration"),FMath::IsNearlyEqual(A.GetFormRemaining(),B.GetFormRemaining(),1.e-10));
    TestTrue(TEXT("Partition preserves special growth"),FMath::IsNearlyEqual(A.GetSpecial(),B.GetSpecial(),1.e-10));
    TestTrue(TEXT("Expiration uses time, not special"),Advance(A,A.GetFormRemaining(),90.));
    TestTrue(TEXT("Expired form is explicitly Normal"),A.GetForm()==EDWCombatBaseForm::Normal);
    TestEqual(TEXT("Expiration clears special"),A.GetSpecial(),C.Vitals.NormalSpecialValue);
    TestEqual(TEXT("Form never heals"),A.Capture(90.).Health,90.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerVitalsValidationTest,"DoughWorld.Combat.PlayerVitals.StrictSnapshotValidation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerVitalsValidationTest::RunTest(const FString& Parameters)
{
    using namespace DWPlayerVitalsTests;const auto C=Config();FDWCombatPlayerVitalsRuntime R;R.Initialize(C,EDWCombatVitalsReset::NewCharacter);
    const auto Good=R.Capture(90.);FString Error;
    auto Reject=[&](const TCHAR* Label,const FDWCombatPlayerVitalsSnapshot& Bad)
    {TestFalse(Label,FDWCombatPlayerVitalsRuntime::Validate(C,Bad,Error));TestFalse(TEXT("Bad restore rejects"),R.Restore(C,Bad));TestEqual(TEXT("Bad restore leaves health mirror"),R.Capture(90.).Water,Good.Water);};
    auto S=Good;S.ResourceSchema=0;Reject(TEXT("Missing schema"),S);
    S=Good;S.RulesCombatSchema=2;Reject(TEXT("Mismatched combat schema"),S);
    S=Good;S.Water=std::numeric_limits<double>::infinity();Reject(TEXT("Nonfinite resource"),S);
    S=Good;S.WaterEmptyElapsedSeconds=1.;Reject(TEXT("Positive resource cannot retain empty timer"),S);
    S=Good;S.Water=C.Vitals.WaterFloor;S.WaterEmptyElapsedSeconds=C.Vitals.WaterDamagePeriodSeconds;Reject(TEXT("Completed timer not yet committed"),S);
    S=Good;S.BaseForm=EDWCombatBaseForm::Mold;Reject(TEXT("Active form needs remaining time"),S);
    S=Good;S.FormRemainingSeconds=1.;Reject(TEXT("Normal cannot hold hidden form time"),S);
    S=Good;S.SpecialGrowthCarry=1.;Reject(TEXT("Invalid compensation carry"),S);
    S=Good;S.DehydrationOrdinal=MAX_uint64;Reject(TEXT("Exhausted ordinal"),S);
    S=Good;S.BaseForm=static_cast<EDWCombatBaseForm>(255);Reject(TEXT("Unknown base form"),S);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerSaveSchemaTest,"DoughWorld.Combat.PlayerVitals.SaveOwnedLoadedAndMissingBlock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerSaveSchemaTest::RunTest(const FString& Parameters)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;
    if(!TestTrue(TEXT("Validated project payload"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
    auto* Save=NewObject<UDWSaveGame>();
    TestFalse(TEXT("Legacy save missing block rejects without migration"),DWCombatPlayerSave::Validate(*Rules,*Save,Error));
    if(!TestTrue(TEXT("New resource block"),DWCombatPlayerSave::MakeNew(*Rules,*Save,Error)))return false;
    TestEqual(TEXT("No free shells"),Save->CombatPlayerResources.LoadedShotgunShells,0);
    TestEqual(TEXT("No free permanent weapons"),Save->CombatPlayerResources.OwnedPermanentWeaponIds.Num(),0);
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Shotgun);
    if(!TestNotNull(TEXT("Shotgun binding"),Binding))return false;
    Save->CombatPlayerResources.LoadedShotgunShells=1;
    TestFalse(TEXT("Loaded without ownership rejected"),DWCombatPlayerSave::Validate(*Rules,*Save,Error));
    Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(Binding->ItemId);
    TestTrue(TEXT("Owned plus one loaded shell valid"),DWCombatPlayerSave::Validate(*Rules,*Save,Error));
    Save->CombatPlayerResources.OwnedPermanentWeaponIds.Add(Binding->ItemId);
    TestFalse(TEXT("Duplicate permanent ids reject"),DWCombatPlayerSave::Validate(*Rules,*Save,Error));
    Save->CombatPlayerResources.OwnedPermanentWeaponIds.Pop();
    int32 Capacity=0;Rules->TryGetCount(TEXT("weapon.shotgun.magazine"),Capacity,Error);
    Save->CombatPlayerResources.LoadedShotgunShells=Capacity+1;
    TestFalse(TEXT("Loaded beyond capacity reject"),DWCombatPlayerSave::Validate(*Rules,*Save,Error));
    Save->CombatPlayerResources.LoadedShotgunShells=0;Save->Health=0.f;
    TestFalse(TEXT("Old health mirror cannot disagree"),DWCombatPlayerSave::Validate(*Rules,*Save,Error));return true;
}
#endif
