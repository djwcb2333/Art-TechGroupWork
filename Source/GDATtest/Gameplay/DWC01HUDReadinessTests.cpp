#if WITH_DEV_AUTOMATION_TESTS
#include "DWC01HUDAnchor.h"
#include "DWUIOffscreenComponent.h"
#include "DWCombatHUDModel.h"
#include "DWCombatRules.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWC01HUDRulesReadinessTest,"DoughWorld.UI.C01.RulesAndSnapshotReadiness",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWC01HUDRulesReadinessTest::RunTest(const FString&)
{
    TSharedPtr<const FDWCombatRules> Rules;FString Error;
    if(!TestTrue(TEXT("Project numeric payload remains valid"),FDWCombatRules::LoadProjectPayload(Rules,Error))||!Rules)return false;
    FDWCombatHUDConfig Config;
    if(!TestTrue(TEXT("Valid numeric rules produce real HUD configuration"),FDWCombatHUDConfig::Build(*Rules,Config,Error)))return false;
    TestTrue(TEXT("HUD resource ceiling comes from validated vitals"),Config.Player.Vitals.HealthMax>Config.Player.Vitals.HealthFloor);
    FDWCombatHUDSnapshot Snapshot;Snapshot.bReady=true;Snapshot.EquipmentId=TEXT("OldPlayerEquipment");Snapshot.Health=DWCombatHUD::Resource(20.,0.,100.);
    TestFalse(TEXT("Missing player does not authorize a resource snapshot"),DWCombatHUD::Build(nullptr,Config,Snapshot));
    TestFalse(TEXT("Lost owner resets ready instead of retaining stale HUD"),Snapshot.bReady);
    TestTrue(TEXT("Lost owner clears stale inventory equipment projection"),Snapshot.EquipmentId.IsNone());
    TestEqual(TEXT("No old resource value survives owner loss"),Snapshot.Health.Value,0.);
    TestEqual(TEXT("Real resource values normalize with their actual ceiling"),DWCombatHUD::Resource(21.,7.,35.).Fraction,.5);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWC01HUDMenuCinematicSequenceTest,"DoughWorld.UI.C01.MenuCinematicOffsetSequence",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWC01HUDMenuCinematicSequenceTest::RunTest(const FString&)
{
    using Anchor=UDWC01HUDAnchor;
    const FVector2D Visible=FVector2D::ZeroVector,Hidden(0.,500.);
    TestTrue(TEXT("Gameplay starts with no offscreen displacement"),Anchor::ComposePresentationOffsets(Visible,Visible).IsNearlyZero());
    TestTrue(TEXT("Inventory exit requests original bottom displacement"),Anchor::ComposePresentationOffsets(Visible,Hidden).Equals(Hidden));
    TestTrue(TEXT("Map/menu and cinematic together do not double displacement"),Anchor::ComposePresentationOffsets(Hidden,Hidden).Equals(Hidden));
    TestTrue(TEXT("Cinematic ending cannot expose an open map/menu"),Anchor::ComposePresentationOffsets(Visible,Hidden).Equals(Hidden));
    TestTrue(TEXT("Menu closing cannot expose an active cinematic"),Anchor::ComposePresentationOffsets(Hidden,Visible).Equals(Hidden));
    TestTrue(TEXT("One channel's anticipation cannot pull the other hidden channel on screen"),Anchor::ComposePresentationOffsets(Hidden,FVector2D(0.,-18.)).Equals(Hidden));
    for(int32 I=0;I<=100;++I)
    {
        const float T=float(I)/100.f;
        const FVector2D Returning(0.,500.*UDWUIOffscreenComponent::ReturnFraction(T,.65f,1.5f));
        const FVector2D Result=Anchor::ComposePresentationOffsets(Visible,Returning);
        TestFalse(TEXT("Existing return spring remains finite"),Result.ContainsNaN());
        if(I==100)TestTrue(TEXT("All owners released returns HUD to its visible position"),Result.IsNearlyZero(.001));
    }
    return true;
}
#endif
