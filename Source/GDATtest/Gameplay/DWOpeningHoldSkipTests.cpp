#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWOpeningHoldSkipCore.h"
#include <limits>
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWOpeningHoldSkipTest,"DoughWorld.Opening.HoldSkipClockAndPolicy",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWOpeningHoldSkipTest::RunTest(const FString&)
{
 using namespace DWOpeningHoldSkip;
 Clock C;C.NewScene(1);C.Release();C.Press(10.,true);
 TestFalse(TEXT("Less than two seconds"),C.Tick(1,11.999,2.,true,true));
 TestTrue(TEXT("Exactly two unscaled seconds"),C.Tick(1,12.,2.,true,true));
 TestFalse(TEXT("Same hold cannot fire twice"),C.Tick(1,50.,2.,true,true));
 C.Release();C.Press(60.,true);TestTrue(TEXT("New press after release"),C.Tick(1,62.,2.,true,true));
 C.NewScene(2);C.Press(63.,true);TestFalse(TEXT("Held across scene must release"),C.Tick(2,100.,2.,true,true));
 C.Release();C.Press(110.,true);C.Tick(2,111.,2.,true,false);C.Press(111.,true);
 TestFalse(TEXT("Early release resets"),C.Tick(2,112.5,2.,true,true));
 TestTrue(TEXT("New uninterrupted two seconds"),C.Tick(2,113.,2.,true,true));
 for(int I=0;I<3;++I)
 {
  C.Release();C.Press(200.,true);C.Tick(2,201.,2.,false,true);
  TestFalse(TEXT("Pause focus death: no time debt"),C.Tick(2,300.,2.,true,true));
  C.Press(300.,true);TestFalse(TEXT("Must observe release after invalidation"),C.Tick(2,303.,2.,true,true));
 }
 C.Release();C.Press(400.,true);C.Invalidate();TestFalse(TEXT("Focus delegate invalidates between actor ticks"),C.Tick(2,500.,2.,true,true));
 for(double Bad:{0.,-1.,std::numeric_limits<double>::infinity(),std::numeric_limits<double>::quiet_NaN()})
 {C.Release();C.Press(10.,true);TestFalse(TEXT("Invalid threshold rejected"),C.Tick(2,20.,Bad,true,true));}
 C.Release();C.Press(10.,true);TestFalse(TEXT("Backward timestamp rejected"),C.Tick(2,9.,2.,true,true));
 C.Release();C.Press(10.,true);TestFalse(TEXT("Nonfinite timestamp rejected"),C.Tick(2,std::numeric_limits<double>::infinity(),2.,true,true));
 for(int Fps:{15,30,60,144})
 {
  C.Release();C.Press(0.,true);
  for(int Frame=0;Frame<2*Fps;++Frame)TestFalse(TEXT("No premature trigger for frame rate"),C.Tick(2,double(Frame)/Fps,2.,true,true));
  TestTrue(TEXT("Same real-time threshold at any dilation/frame rate"),C.Tick(2,2.,2.,true,true));
 }
 TestTrue(TEXT("Wake destination"),EndOf(Segment::Wake)==Destination::StretchPrompt);
 TestTrue(TEXT("Stretch destination"),EndOf(Segment::Stretch)==Destination::Escape);
 TestTrue(TEXT("Reveal destination"),EndOf(Segment::Reveal)==Destination::Approach);
 TestTrue(TEXT("Encounter destination"),EndOf(Segment::Encounter)==Destination::Combat);
 TestTrue(TEXT("Non-cinematic cannot skip"),EndOf(Segment::None)==Destination::None);
 for(double Target:{.35,1.,1.2})for(double Applied:{0.,.1, .35,1.,1.2,2.})
 {
  const double Added=Remaining(Applied,Target);
  TestTrue(TEXT("Partial state adds only remaining amount"),Added>=0.&&Applied+Added>=Target);
  TestTrue(TEXT("Second settlement is idempotent"),Remaining(Applied+Added,Target)==0.);
 }
 return true;
}
#endif
