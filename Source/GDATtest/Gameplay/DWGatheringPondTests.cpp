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
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPondConcaveBoundaryTest,
    "DoughWorld.Gathering.Pond.ConcaveBoundaryAndWaterTriangles",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPondConcaveBoundaryTest::RunTest(const FString& Parameters)
{
    using namespace DWGatheringPondMath;
    const TArray<FVector2D> Shape={{-300.,-250.},{300.,-250.},{300.,250.},{100.,250.},
        {100.,-50.},{-100.,-50.},{-100.,250.},{-300.,250.}};
    FString Error;TArray<int32> Triangles;
    TestTrue(TEXT("A U-shaped outline is valid"),ValidateSimplePolygon(Shape,&Error));
    TestFalse(TEXT("The dry concave notch is not water"),ContainsPoint(Shape,FVector2D(0.,150.)));
    TestTrue(TEXT("The left arm is water"),ContainsPoint(Shape,FVector2D(-200.,150.)));
    TestTrue(TEXT("The bottom joins both arms"),ContainsPoint(Shape,FVector2D(0.,-150.)));
    TestTrue(TEXT("Waterline boundary is accepted"),ContainsPoint(Shape,FVector2D(100.,100.)));
    if(!TestTrue(TEXT("Concave shape triangulates without a center fan"),TriangulatePolygon(Shape,Triangles,&Error)))return false;
    TestEqual(TEXT("Simple 8-point polygon has six triangles"),Triangles.Num(),18);
    auto Covered=[&](const FVector2D& Point)
    {
        for(int32 I=0;I<Triangles.Num();I+=3)
        {
            const auto& A=Shape[Triangles[I]];const auto& B=Shape[Triangles[I+1]];const auto& C=Shape[Triangles[I+2]];
            if(Cross2(A,B,Point)>=-.0001&&Cross2(B,C,Point)>=-.0001&&Cross2(C,A,Point)>=-.0001)return true;
        }
        return false;
    };
    TestFalse(TEXT("No Visibility water triangle covers the concave notch"),Covered(FVector2D(0.,150.)));
    TestTrue(TEXT("Generated water triangles cover the arm"),Covered(FVector2D(-200.,150.)));
    double Area=0.;
    for(int32 I=0;I<Triangles.Num();I+=3)
    {
        const auto& A=Shape[Triangles[I]];const auto& B=Shape[Triangles[I+1]];const auto& C=Shape[Triangles[I+2]];
        TestTrue(TEXT("Every generated water face points up"),Cross2(A,B,C)>0.);
        TestTrue(TEXT("Every triangle centroid stays in water"),ContainsPoint(Shape,(A+B+C)/3.));
        Area+=Cross2(A,B,C);
    }
    TestTrue(TEXT("Triangle area exactly covers the polygon without overlaps"),FMath::IsNearlyEqual(Area,SignedArea2(Shape),.001));
    TArray<FVector2D> Clockwise;for(int32 I=Shape.Num()-1;I>=0;--I)Clockwise.Add(Shape[I]);
    TestTrue(TEXT("Clockwise artist outline also triangulates"),TriangulatePolygon(Clockwise,Triangles,&Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPondWorldMetricTest,
    "DoughWorld.Gathering.Pond.SplineWorldCentimetersAndTransform",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPondWorldMetricTest::RunTest(const FString& Parameters)
{
    using namespace DWGatheringPondMath;
    const TArray<FVector2D> Shape={{-300.,-250.},{300.,-250.},{300.,250.},{100.,250.},
        {100.,-50.},{-100.,-50.},{-100.,250.},{-300.,250.}};
    FTransform Transform(FRotator(0.,90.,0.),FVector(1000.,-500.,20.),FVector(2.,.5,1.5));
    const FVector Wet=Transform.TransformPosition(FVector(-200.,150.,55.));
    const FVector Notch=Transform.TransformPosition(FVector(0.,150.,55.));
    TestTrue(TEXT("Yaw and nonuniform positive XYZ scale preserve the arm"),ContainsPolygonFeet(Shape,Transform,Wet,55.,30.));
    TestFalse(TEXT("Yaw and nonuniform scale preserve the dry notch"),ContainsPolygonFeet(Shape,Transform,Notch,55.,30.));
    const FVector Near=Transform.TransformPosition(FVector(340.,0.,55.));
    TestFalse(TEXT("80 world cm from edge rejects a 79 cm station margin"),ContainsPolygonFeet(Shape,Transform,Near,55.,30.,79.));
    TestTrue(TEXT("80 world cm from edge accepts an 81 cm station margin"),ContainsPolygonFeet(Shape,Transform,Near,55.,30.,81.));
    TestFalse(TEXT("Station margin does not mark dry shore as standing in water"),ContainsPolygonFeet(Shape,Transform,Near,55.,30.));
    TestFalse(TEXT("Height compares to scaled water plane, not actor origin"),ContainsPolygonFeet(Shape,Transform,Wet+FVector(0.,0.,31.),55.,30.));
    TestFalse(TEXT("Canyon floor cannot receive water at matching XY"),ContainsPolygonFeet(Shape,Transform,Wet-FVector(0.,0.,4500.),55.,120.));
    Transform.SetScale3D(FVector(3.,.5,1.5));
    const FVector RescaledNear=Transform.TransformPosition(FVector(340.,0.,55.));
    TestFalse(TEXT("Current transform recomputes a 120 cm edge gap; no stale cached world shape"),ContainsPolygonFeet(Shape,Transform,RescaledNear,55.,30.,100.));
    TestTrue(TEXT("World 150 cm margin still works after runtime scale change"),ContainsPolygonFeet(Shape,Transform,RescaledNear,55.,30.,150.));
    Transform.SetScale3D(FVector(-1.,1.,1.));
    TestFalse(TEXT("Mirrored transform rejects"),ContainsPolygonFeet(Shape,Transform,Wet,55.,120.));
    Transform=FTransform(FRotator(20.,0.,0.));
    TestFalse(TEXT("Tilted water plane rejects"),ContainsPolygonFeet(Shape,Transform,FVector::ZeroVector,55.,120.));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPondShapeValidationTest,
    "DoughWorld.Gathering.Pond.InvalidShapeAndBankProfile",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPondShapeValidationTest::RunTest(const FString& Parameters)
{
    using namespace DWGatheringPondMath;
    FString Error;TArray<int32> Triangles;
    const TArray<FVector2D> BowTie={{-100.,-100.},{100.,100.},{-100.,100.},{100.,-100.}};
    TestFalse(TEXT("Crossed outline rejects rather than becoming a circle"),TriangulatePolygon(BowTie,Triangles,&Error));
    TestTrue(TEXT("Invalid shape provides no query triangles"),Triangles.IsEmpty());
    TestFalse(TEXT("A straight line cannot form a pond"),ValidateSimplePolygon({{0.,0.},{100.,0.},{200.,0.}},&Error));
    TestFalse(TEXT("Duplicate adjacent vertices reject"),ValidateSimplePolygon({{0.,0.},{100.,0.},{100.,0.},{0.,100.}},&Error));
    TestFalse(TEXT("Nonfinite vertex rejects"),ValidateSimplePolygon({{0.,0.},{100.,0.},{0.,std::numeric_limits<double>::infinity()}},&Error));
    const TArray<FVector2D> LinearSamples={{0.,0.},{50.,0.},{100.,0.},{100.,100.},{0.,100.}};
    TestTrue(TEXT("Collinear samples on a linear spline remain supported"),TriangulatePolygon(LinearSamples,Triangles,&Error));
    TestEqual(TEXT("Inside deep bed is raised 5 cm without digging the map"),BankHeight(-100.,55.,5.,70.,10.,20.,120.),5.);
    TestEqual(TEXT("Spline marks wet waterline, not dry ridge"),BankHeight(0.,55.,5.,70.,10.,20.,120.),55.);
    TestEqual(TEXT("Ridge stands 10 cm above water, outside wet outline"),BankHeight(20.,55.,5.,70.,10.,20.,120.),65.);
    TestEqual(TEXT("Outer bank returns to actor ground"),BankHeight(120.,55.,5.,70.,10.,20.,120.),0.);
    TestEqual(TEXT("Outer edge can meet existing ground while actor Z remains unchanged"),BankHeight(120.,55.,5.,70.,10.,20.,120.,-5.),-5.);
    TestEqual(TEXT("Drying does not change independent bank geometry"),BankHeight(-35.,55.,5.,70.,10.,20.,120.),30.);
    TestEqual(TEXT("Existing dry reserve remains a zero refill"),RefillRequest(20.,1.,0.),0.);
    return true;
}
#endif
