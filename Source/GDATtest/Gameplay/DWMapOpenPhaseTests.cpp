#include "DWMapOpenPhases.h"
#include "DWMinimapNavigation.h"
#include <utility>
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapOpeningPhasesTest,"DoughWorld.UI.Map.ThreeStagePhases",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMapOpeningPhasesTest::RunTest(const FString&)
{
    using namespace DWMapOpenPhases;Timeline Clock;Clock.Reset(.14,.32,.18);
    for(int I=0;I<1000;++I)
    {
        Clock.Advance(.001,false,false);const auto S=Clock.Read(false);
        if(S.Shell>0.)TestEqual(TEXT("Expansion never overlaps old minimap content"),S.Mini,0.);
        TestEqual(TEXT("No uncommitted full map appears"),S.Full,0.);
    }
    const auto Wait=Clock.Read(false);TestTrue(TEXT("Capture starts after shell fills viewport"),Wait.NeedCapture&&Wait.Shell==1.&&!Wait.Complete);
    for(int I=0;I<200;++I)Clock.Advance(.001,false,true);
    TestTrue(TEXT("GPU-ready map can finish fade"),Clock.Read(true).Complete);
    for(int I=0;I<1000;++I)Clock.Advance(.001,true,true);
    TestTrue(TEXT("Reverse returns exactly to initial content"),Clock.Read(true).Closed&&Clock.Read(true).Mini==1.);
    Clock.Advance(.05,false,false);const double Before=Clock.Read(false).Mini;Clock.Advance(0.,false,false);
    TestEqual(TEXT("Settings pause advances no presentation time"),Clock.Read(false).Mini,Before);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapCircularCoverageTest,"DoughWorld.UI.Map.CircularCoverageBudget",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMapCircularCoverageTest::RunTest(const FString&)
{
    using namespace DWMapOpenPhases;
    for(auto Size:{std::pair<double,double>{1920,1080},{1080,1920},{3440,1440}})
    {
        Circle Shape;TestTrue(TEXT("Circle math valid"),RevealCircle(10,10,180,180,Size.first,Size.second,1.,Shape));
        for(double X:{0.,Size.first})for(double Y:{0.,Size.second})TestTrue(TEXT("Circle covers every viewport corner"),std::hypot(X-Shape.X,Y-Shape.Y)<=Shape.Radius+1e-8);
        TestTrue(TEXT("Inscribed 48-edge rendered outline also covers viewport corners"),Shape.Radius*std::cos(3.14159265358979323846/48.)>=std::hypot(Size.first,Size.second)*.5);
    }
    TestEqual(TEXT("1080p short-axis coverage"),CaptureResolution(1920,1080,1024),1280);
    TestEqual(TEXT("1440p short-axis coverage"),CaptureResolution(2560,1440,1024),1536);
    TestEqual(TEXT("8K is bounded to 2048, not desktop-sized RT"),CaptureResolution(7680,4320,1024),2048);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapCreatureDispositionTest,"DoughWorld.UI.Map.CreatureDispositionColor",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMapCreatureDispositionTest::RunTest(const FString&)
{
    TestTrue(TEXT("Neutral becomes NPC"),DWMinimapMath::CreatureMarkerKind(EDWCreatureDisposition::Neutral)==EDWMinimapMarkerKind::NPC);
    TestTrue(TEXT("AlwaysHostile stays Enemy"),DWMinimapMath::CreatureMarkerKind(EDWCreatureDisposition::AlwaysHostile)==EDWMinimapMarkerKind::Enemy);
    TestTrue(TEXT("Legacy/no definition keeps previous Enemy classification"),DWMinimapMath::CreatureMarkerKind(EDWCreatureDisposition::InheritExisting)==EDWMinimapMarkerKind::Enemy);
    const auto Blue=DWMinimapMath::CategoryColor(EDWMinimapMarkerKind::NPC),Red=DWMinimapMath::CategoryColor(EDWMinimapMarkerKind::Enemy);
    const FLinearColor ExpectedRed(FColor(237,87,97));
    TestTrue(TEXT("NPC marker is blue"),Blue.B>Blue.R);
    TestTrue(TEXT("Enemy keeps original red exactly, channel by channel"),Red.R==ExpectedRed.R&&Red.G==ExpectedRed.G&&Red.B==ExpectedRed.B&&Red.A==ExpectedRed.A);
    return true;
}
#endif
