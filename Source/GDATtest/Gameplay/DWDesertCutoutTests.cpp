#include "DWDesertCutoutMath.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDesertCutoutProjectionTest,"DoughWorld.Rendering.DesignerCutout.Projection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDesertCutoutProjectionTest::RunTest(const FString&)
{
 using namespace DWDesertCutoutMath;
 for(double W:{720.,1920.,3440.})for(double H:{720.,1080.,1440.})
 {
  Frame F;TestTrue(TEXT("Actual projected off-center frame"),Make(W*.37,H*.61,W*.37+100.,H*.61,W,H,180.,25.,F));
  TestTrue(TEXT("Horizontal 100px radius"),FMath::IsNearlyEqual(CircleMask(F,F.X+100./W,F.Y),1.,1e-9));
  TestTrue(TEXT("Vertical 100px radius matches on all aspects"),FMath::IsNearlyEqual(CircleMask(F,F.X,F.Y+100./H),1.,1e-9));
  TestEqual(TEXT("Center cut out"),CircleMask(F,F.X,F.Y),0.);
 }
 Frame F;TestFalse(TEXT("Invalid viewport fails closed"),Make(0,0,100,0,0,1080,180,25,F));
 TestFalse(TEXT("Off-screen focus fails closed"),Make(-10,100,90,100,1920,1080,180,25,F));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDesertCutoutGateTest,"DoughWorld.Rendering.DesignerCutout.GateAndProtection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDesertCutoutGateTest::RunTest(const FString&)
{
 using namespace DWDesertCutoutMath;
 for(double A:{0.,.17,.5,1.})for(double C:{0.,.2,1.})
 {
  TestEqual(TEXT("Non-designer gate 0 preserves ANY original mask"),GatedMask(A,C,0,1,true,true),A);
  TestEqual(TEXT("View disabled is identity"),GatedMask(A,C,1,0,true,true),A);
  TestEqual(TEXT("Pixels behind player retain original mask"),GatedMask(A,C,1,1,false,true),A);
  TestEqual(TEXT("Ground below feet retains original mask"),GatedMask(A,C,1,1,true,false),A);
 }
 TestEqual(TEXT("Qualified foreground can open"),GatedMask(1,0,1,1,true,true),0.);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDesertCutoutLeaseTest,"DoughWorld.Rendering.DesignerCutout.FlagOwnership",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDesertCutoutLeaseTest::RunTest(const FString&)
{
 using namespace DWDesertCutoutMath;
 TestTrue(TEXT("Only free zero flag can be acquired"),CanAcquire(0.));TestFalse(TEXT("Foreign flag is not overwritten"),CanAcquire(2.));
 TestEqual(TEXT("Owned unchanged flag restores prior value"),ReleaseValue(1.,0.),0.);
 TestEqual(TEXT("Foreign later write preserved"),ReleaseValue(2.,0.),2.);return true;
}
#endif
