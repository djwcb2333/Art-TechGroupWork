#if WITH_DEV_AUTOMATION_TESTS
#include "DWDemolitionWork.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDemolitionWorkAutomation,
    "DoughWorld.Gathering.Demolition.ContinuityAndSpeed",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDemolitionWorkAutomation::RunTest(const FString&)
{
    using namespace DWDemolitionWork;
    FWork Work;TestTrue(TEXT("Session starts"),Work.Begin(1,2,4.));
    TestFalse(TEXT("Partial work is not complete"),Work.Advance(1,2,1.,1.,true));
    TestEqual(TEXT("Partial progress"),Work.Progress(),.25);
    TestFalse(TEXT("Release or invalid context cannot complete"),Work.Advance(1,2,0.,1.,false));
    TestTrue(TEXT("Interruption clears progress"),Work.State==EState::Idle&&Work.Progress()==0.);
    TestTrue(TEXT("Restart is fresh"),Work.Begin(1,2,4.)&&Work.Progress()==0.);
    TestFalse(TEXT("Changing target does not complete"),Work.Advance(3,2,1.,1.,true));
    TestTrue(TEXT("Changing target clears"),Work.State==EState::Idle);
    Work.Begin(1,2,4.);TestFalse(TEXT("Changing tool does not complete"),Work.Advance(1,3,1.,1.,true));
    TestTrue(TEXT("Changing tool clears"),Work.State==EState::Idle);
    Work.Begin(1,2,4.);TestTrue(TEXT("Advanced 2x completes in half time"),Work.Advance(1,2,2.,2.,true));
    TestTrue(TEXT("One committed outcome"),Work.Commit());TestFalse(TEXT("Cannot commit twice"),Work.Commit());
    return true;
}
#endif
