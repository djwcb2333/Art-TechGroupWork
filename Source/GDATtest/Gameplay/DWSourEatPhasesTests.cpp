#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWSourEatPhases.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSourEatPhasesTest,"DoughWorld.Animation.SourDough.Phases",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSourEatPhasesTest::RunTest(const FString&)
{
    FDWSourEatPhases P;
    TestTrue(TEXT("request starts enter"),P.Step(true,true,0.f,2.f,.5f)==EDWSourEatPhase::Enter);
    TestTrue(TEXT("enter stays before boundary"),P.Step(true,true,1.f,2.f,.5f)==EDWSourEatPhase::Enter);
    TestTrue(TEXT("enter completes at boundary"),P.Step(true,true,1.f,2.f,.5f)==EDWSourEatPhase::Loop);
    TestTrue(TEXT("request holds loop"),P.Step(true,true,.2f,2.f,.5f)==EDWSourEatPhase::Loop);
    TestTrue(TEXT("cancel begins exit"),P.Step(true,false,0.f,2.f,.5f)==EDWSourEatPhase::Exit);
    TestTrue(TEXT("exit completes"),P.Step(true,false,.5f,2.f,.5f)==EDWSourEatPhase::None);
    P.Step(true,true,0.f,2.f,.5f);
    TestTrue(TEXT("combat interruption clears eating immediately"),P.Step(false,true,.1f,2.f,.5f)==EDWSourEatPhase::None);
    TestTrue(TEXT("invalid delta cannot advance"),P.Step(true,true,0.f,2.f,.5f)==EDWSourEatPhase::Enter && P.Step(true,true,std::numeric_limits<float>::quiet_NaN(),2.f,.5f)==EDWSourEatPhase::Enter);
    return true;
}
#endif
