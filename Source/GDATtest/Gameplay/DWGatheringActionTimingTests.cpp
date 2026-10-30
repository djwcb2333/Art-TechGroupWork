#include "DWGatheringActionTiming.h"
#include <functional>
#include <limits>

namespace DWGatheringTimingTests
{
void Run(const std::function<void(const char*,bool)>& Check)
{
    using namespace DWGatheringActionTiming;
    double Scaled=0.;
    Check("base tool retains duration",TryScaleDuration(1.,1.,Scaled)&&std::abs(Scaled-1.)<1e-9);
    Check("advanced tool halves duration",TryScaleDuration(1.,2.,Scaled)&&std::abs(Scaled-.5)<1e-9);
    Check("reject zero multiplier",!TryScaleDuration(1.,0.,Scaled));
    Check("reject negative multiplier",!TryScaleDuration(1.,-1.,Scaled));
    Check("reject nonfinite multiplier",!TryScaleDuration(1.,std::numeric_limits<double>::quiet_NaN(),Scaled));
    Check("reject too short cycle instead of clamping",!TryScaleDuration(.1,3.,Scaled));
    FProfile Profile;
    Check("contact pose is exactly the authored slam contact",std::abs(Profile.SourceSeconds(Profile.ContactProgress())-1.8)<1e-8);
    Check("contact follows anticipation",Profile.ContactProgress()>.4);
    Check("contact precedes grounded hold",Profile.ContactProgress()<.55);
    Check("impact hold freezes source pose",std::abs(Profile.SourceSeconds(.6)-1.95)<1e-8);
    Check("recovery returns to starting pose",std::abs(Profile.SourceSeconds(1.)-.75)<1e-8);
    for(double Duration : {.8,1.,1.6})
        for(double Hz : {15.,30.,60.,144.})
        {
            FClock Clock;Clock.Start(Duration,Profile.ContactProgress());int Hits=0;
            while(Clock.Elapsed<Clock.Duration)
            {
                const auto Step=Clock.Advance(1./Hz);
                if(Step.bImpact)
                {
                    ++Hits;
                    Check("no contact before the authored phase",Clock.Progress()+1e-8>=Profile.ContactProgress());
                }
            }
            Check("exactly one resource contact per complete swing at each FPS",Hits==1);
            Check("recovery cannot hit a second time",!Clock.Advance(Duration).bImpact);
        }
    FClock Cancelled;Cancelled.Start(1.,Profile.ContactProgress());
    Check("windup has no resource contact",!Cancelled.Advance(.35).bImpact);
    Cancelled.Cancel();Check("cancelled windup remains inert after a large time jump",!Cancelled.Advance(100.).bImpact);
    FClock Hitch;Hitch.Start(1.,Profile.ContactProgress());
    const auto HitchStep=Hitch.Advance(5.);
    Check("one long frame cannot skip the contact",HitchStep.bImpact&&HitchStep.bFinished);
    Check("reentry after contact cannot hit again",!Hitch.Advance(0.).bImpact);
    FClock Invalid;Invalid.Start(1.,Profile.ContactProgress());
    Check("NaN tick cannot advance an action",!Invalid.Advance(std::numeric_limits<double>::quiet_NaN()).bImpact&&Invalid.Elapsed==0.);
    Check("negative tick cannot advance an action",!Invalid.Advance(-1.).bImpact&&Invalid.Elapsed==0.);
    Invalid.Cancel();Invalid.Start(1.,Profile.ContactProgress());
    Check("a new accepted action receives a fresh contact",Invalid.Advance(.5).bImpact);
}
}

#if defined(DW_GATHERING_STANDALONE_TEST)
#include <iostream>
int main()
{
    int Passed=0,Failed=0;
    DWGatheringTimingTests::Run([&](const char* Label,bool Result)
    {if(Result)++Passed;else{++Failed;std::cerr<<"FAILED "<<Label<<'\n';}});
    std::cout<<"Gathering action timing checks: "<<Passed<<" passed, "<<Failed<<" failed\n";
    return Failed?1:0;
}
#elif WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringActionTimingTest,
    "DoughWorld.Gathering.Action.SourceContactOnceAndCancel",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringActionTimingTest::RunTest(const FString&)
{
    DWGatheringTimingTests::Run([this](const char* Label,bool Result){TestTrue(UTF8_TO_TCHAR(Label),Result);});
    return true;
}
#endif
