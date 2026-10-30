#if WITH_DEV_AUTOMATION_TESTS
#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

// Implemented beside FWorldScope in DWBuildingEffectsTests.cpp so the test uses
// the existing fixture without copying it or including another .cpp file.
namespace DWBuildingEffectsTests
{
    bool RunCloudControls(FAutomationTestBase& Test);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingEffectsCloudControls,
    "DoughWorld.BuildingEffects.CloudControls.CountZeroExpandedCountAndRadiusScale",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWBuildingEffectsCloudControls::RunTest(const FString&)
{
    return DWBuildingEffectsTests::RunCloudControls(*this);
}
#endif
