#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWOpeningExitVolume.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWOpeningExitGeometryTest,"DoughWorld.Opening.ExitVolumeGeometry",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWOpeningExitGeometryTest::RunTest(const FString&)
{
 const FVector Extent(140.,100.,180.);
 for(const FVector Scale:{FVector(1.),FVector(2.,.5,1.5),FVector(.5,1.,2.)})
 for(const FRotator Rotation:{FRotator::ZeroRotator,FRotator(15.,90.,30.),FRotator(-40.,-23.,12.)})
 {
  const FTransform T(Rotation,FVector(4450.,200.,280.),Scale);
  TestTrue(TEXT("Rotated positive nonuniform scaled box valid"),ADWOpeningExitVolume::IsValidBoxGeometry(T,Extent));
 }
 TestFalse(TEXT("Zero scale rejected"),ADWOpeningExitVolume::IsValidBoxGeometry(FTransform(FQuat::Identity,FVector::ZeroVector,FVector(1.,0.,1.)),Extent));
 TestFalse(TEXT("Negative scale rejected explicitly"),ADWOpeningExitVolume::IsValidBoxGeometry(FTransform(FQuat::Identity,FVector::ZeroVector,FVector(-1.,1.,1.)),Extent));
 TestFalse(TEXT("Zero extent rejected"),ADWOpeningExitVolume::IsValidBoxGeometry(FTransform::Identity,FVector(1.,1.,0.)));
 TestFalse(TEXT("Negative extent rejected"),ADWOpeningExitVolume::IsValidBoxGeometry(FTransform::Identity,FVector(-1.,1.,1.)));
 return true;
}
#endif
