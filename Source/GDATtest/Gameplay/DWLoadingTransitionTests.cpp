#if WITH_DEV_AUTOMATION_TESTS
#include "DWLoadingTransition.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWIrisCurveTest,"DoughWorld.Loading.IrisSpringEndpoints",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWIrisCurveTest::RunTest(const FString&)
{
 for(float Damping:{.2f,.65f,1.f,1.5f})for(float Duration:{.1f,.85f,2.f})
 {
  TestEqual(TEXT("Closing starts uncovered"),UDWLoadingTransitionSubsystem::EvaluateIris(0,Duration,2.6f,Damping,false),1.f);
  TestEqual(TEXT("Closing ends fully covered"),UDWLoadingTransitionSubsystem::EvaluateIris(Duration,Duration,2.6f,Damping,false),0.f);
  TestEqual(TEXT("Opening starts covered"),UDWLoadingTransitionSubsystem::EvaluateIris(0,Duration,2.6f,Damping,true),0.f);
  TestEqual(TEXT("Opening ends fully uncovered"),UDWLoadingTransitionSubsystem::EvaluateIris(Duration,Duration,2.6f,Damping,true),1.f);
  for(int I=0;I<=100;++I){float X=UDWLoadingTransitionSubsystem::EvaluateIris(Duration*I/100,Duration,2.6f,Damping,true);TestTrue(TEXT("No invalid or negative radii"),FMath::IsFinite(X)&&X>=0&&X<=1);}
 }
 const float Nonlinear=UDWLoadingTransitionSubsystem::EvaluateIris(.2f,.95f,2.6f,.65f,true);
 TestTrue(TEXT("Uses a spring response, not linear interpolation"),FMath::Abs(Nonlinear-.2f/.95f)>.1f);
 for(int I=0;I<=100;++I)TestEqual(TEXT("Disabled completion bounce never scales the bar"),UDWLoadingTransitionSubsystem::EvaluateCompletionScale(I*.01f,.7f,false,.16f,3.5f,.5f),1.f);
 TestTrue(TEXT("Enabled completion bounce changes scale"),UDWLoadingTransitionSubsystem::EvaluateCompletionScale(.1f,.7f,true,.16f,3.5f,.5f)>1.02f);
 TestEqual(TEXT("Completion bounce settles"),UDWLoadingTransitionSubsystem::EvaluateCompletionScale(.7f,.7f,true,.16f,3.5f,.5f),1.f);
 return true;
}
#endif
