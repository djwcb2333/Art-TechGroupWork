#if WITH_DEV_AUTOMATION_TESTS
#include "DWGatheringPondMath.h"
#include "DWCombatPlayerVitals.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace DWGatheringPondTests
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
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringWaterCapacityTest,
    "DoughWorld.Gathering.Pond.RefillCapacityReserveAndNoOp",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringWaterCapacityTest::RunTest(const FString& Parameters)
{
    using namespace DWGatheringPondTests;
    const auto C=Config();FDWCombatPlayerVitalsRuntime R;
    if(!TestTrue(TEXT("Explicit resource fixture is valid"),R.Initialize(C,EDWCombatVitalsReset::NewCharacter)))return false;
    double Added=-1.;const uint64 Before=R.GetMutationRevision();
    TestTrue(TEXT("One second refill accepted"),R.AddWater(DWGatheringPondMath::RefillRequest(20.,1.,300.),90.,Added));
    TestEqual(TEXT("Exactly twenty accepted"),Added,20.);
    TestEqual(TEXT("Water increased from 25 to 45"),R.GetWater(),45.);
    TestEqual(TEXT("One mutation revision per refill"),R.GetMutationRevision(),Before+1);
    double Reserve=3.;
    TestTrue(TEXT("Last reserve is accepted"),R.AddWater(DWGatheringPondMath::RefillRequest(20.,1.,Reserve),90.,Added));
    Reserve-=Added;
    TestEqual(TEXT("Never grant beyond remaining pond water"),Added,3.);
    TestEqual(TEXT("Pond empties exactly"),Reserve,0.);
    TestEqual(TEXT("Player is still below full after pond exhaustion"),R.GetWater(),48.);
    TestTrue(TEXT("Another pond fills only missing water"),R.AddWater(20.,90.,Added));
    TestEqual(TEXT("Only missing two points charged"),Added,2.);
    TestEqual(TEXT("Water caps at maximum"),R.GetWater(),50.);
    const uint64 FullRevision=R.GetMutationRevision();
    TestTrue(TEXT("Full water is a valid no-op"),R.AddWater(20.,90.,Added));
    TestEqual(TEXT("Full player spends no pond water"),Added,0.);
    TestEqual(TEXT("Full player creates no mutation"),R.GetMutationRevision(),FullRevision);
    TestTrue(TEXT("Zero request is a no-op"),R.AddWater(0.,90.,Added));
    TestEqual(TEXT("Zero request creates no mutation"),R.GetMutationRevision(),FullRevision);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringWaterValidationTest,
    "DoughWorld.Gathering.Pond.RefillValidationAndIndependentClocks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringWaterValidationTest::RunTest(const FString& Parameters)
{
    using namespace DWGatheringPondTests;const auto C=Config();FDWCombatPlayerVitalsRuntime R;
    if(!TestTrue(TEXT("Initialize fixture"),R.Initialize(C,EDWCombatVitalsReset::NewCharacter)))return false;
    auto S=R.Capture(70.);S.Water=C.Vitals.WaterFloor;S.WaterEmptyElapsedSeconds=4.75;
    S.Satiety=C.Vitals.SatietyFloor;S.SatietyEmptyElapsedSeconds=2.5;
    if(!TestTrue(TEXT("Restore partial independent empty clocks"),R.Restore(C,S)))return false;
    TestTrue(TEXT("Enter existing committed core form"),R.EnterCommittedForm(EDWCombatBaseForm::Mold));
    const auto Before=R.Capture(70.);double Added=-1.;
    TestTrue(TEXT("Positive refill clears dehydration progress"),R.AddWater(1.,70.,Added));
    const auto After=R.Capture(70.);
    TestEqual(TEXT("Exactly one added"),Added,1.);
    TestEqual(TEXT("Dehydration timer resets"),After.WaterEmptyElapsedSeconds,0.);
    TestEqual(TEXT("Dehydration carry resets"),After.WaterEmptyTimeCarry,0.);
    TestEqual(TEXT("Starvation clock is preserved"),After.SatietyEmptyElapsedSeconds,Before.SatietyEmptyElapsedSeconds);
    TestEqual(TEXT("Health is not healed"),After.Health,Before.Health);
    TestEqual(TEXT("Form duration is not renewed"),After.FormRemainingSeconds,Before.FormRemainingSeconds);
    TestEqual(TEXT("Special resource is not changed"),After.Special,Before.Special);
    TestTrue(TEXT("Form remains Mold"),After.BaseForm==EDWCombatBaseForm::Mold);
    const uint64 Revision=R.GetMutationRevision();
    for(const double Invalid:{-1.,std::numeric_limits<double>::quiet_NaN(),std::numeric_limits<double>::infinity()})
    {
        Added=-1.;TestFalse(TEXT("Invalid refill rejects"),R.AddWater(Invalid,70.,Added));
        TestEqual(TEXT("Rejected refill reports zero"),Added,0.);
        TestEqual(TEXT("Rejected refill preserves revision"),R.GetMutationRevision(),Revision);
        TestEqual(TEXT("Rejected refill preserves water"),R.GetWater(),After.Water);
    }
    TestFalse(TEXT("Dead player cannot be refilled"),R.AddWater(1.,C.Vitals.HealthFloor,Added));
    TestEqual(TEXT("Dead rejection preserves water"),R.GetWater(),After.Water);
    FDWCombatPlayerVitalsRuntime RoundTrip;
    TestTrue(TEXT("Refilled snapshot remains save-valid"),RoundTrip.Restore(C,R.Capture(70.)));
    TestEqual(TEXT("Round trip preserves refill"),RoundTrip.GetWater(),R.GetWater());
    FDWCombatPlayerVitalsRuntime Unready;
    TestFalse(TEXT("Uninitialized runtime rejects refill"),Unready.AddWater(1.,70.,Added));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPondGeometryTest,
    "DoughWorld.Gathering.Pond.CanyonHeightAndRateBounds",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPondGeometryTest::RunTest(const FString& Parameters)
{
    using namespace DWGatheringPondMath;
    const FVector Center(6000.,-11000.,0.);
    TestTrue(TEXT("Feet on surface are inside"),ContainsFeet(Center,Center,250.,250.,120.));
    TestTrue(TEXT("Exact circular boundary is accepted"),ContainsFeet(Center,Center+FVector(250.,0.,0.),250.,250.,120.));
    TestFalse(TEXT("Box corner outside pond circle rejects"),ContainsFeet(Center,Center+FVector(250.,250.,0.),250.,250.,120.));
    TestFalse(TEXT("Same XY at canyon bottom rejects"),ContainsFeet(Center,Center+FVector(0.,0.,-4500.),250.,250.,120.));
    TestFalse(TEXT("Above surface tolerance rejects"),ContainsFeet(Center,Center+FVector(0.,0.,121.),250.,250.,120.));
    TestTrue(TEXT("Station can be used from nearby shore"),ContainsFeet(Center,Center+FVector(350.,0.,0.),250.,250.,120.,150.));
    TestFalse(TEXT("Shore station radius does not mean standing in water"),ContainsFeet(Center,Center+FVector(350.,0.,0.),250.,250.,120.));
    TestFalse(TEXT("Negative radius rejects"),ContainsFeet(Center,Center,-1.,250.,120.));
    TestFalse(TEXT("NaN radius rejects"),ContainsFeet(Center,Center,std::numeric_limits<double>::quiet_NaN(),250.,120.));
    TestEqual(TEXT("Half-second rate"),RefillRequest(20.,.5,300.),10.);
    TestEqual(TEXT("Reserve caps oversized time without overflow"),RefillRequest(20.,std::numeric_limits<double>::max(),3.),3.);
    TestEqual(TEXT("Dry pond requests zero"),RefillRequest(20.,1.,0.),0.);
    TestEqual(TEXT("Paused/zero step requests zero"),RefillRequest(20.,0.,300.),0.);
    TestEqual(TEXT("Negative step requests zero"),RefillRequest(20.,-1.,300.),0.);
    TestEqual(TEXT("Nonfinite step rejects"),RefillRequest(20.,std::numeric_limits<double>::infinity(),300.),0.);
    return true;
}
#endif
