#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatNumericProvider.h"
#include "DWPlayerCombatTuningSubsystem.h"
#include "DWCombatRules.h"
#include "DWCombatSpear.h"
#include "DWCombatHUDModel.h"
#include "DWCombatPlayerSave.h"
#include "DWSaveGame.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWNewAdventureNumericRegression,"DoughWorld.Combat.NumericSource.NewAdventureAndHUDPreflight",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWNewAdventureNumericRegression::RunTest(const FString&)
{
    // Read the migrated live asset, but create only an unsaved transient save object.
    // No GameInstance, gameplay World, slot access, travel, input, or player-save writes.
    const auto* Settings=LoadObject<UDWPlayerCombatTuningSettings>(nullptr,TEXT("/Game/DoughWorld/Core/Data/DA_DWPlayerCombatTuning.DA_DWPlayerCombatTuning"));
    if(!TestNotNull(TEXT("Current numeric asset exists"),Settings))return false;
    FString Error;TSharedPtr<const FDWCombatRules> Base;
    if(!TestTrue(TEXT("Structural source loads"),FDWCombatRules::LoadProjectPayload(Base,Error)))return false;
    TSharedPtr<const FDWCombatNumericProvider> Provider;
    if(!TestTrue(TEXT("Current complete numeric source initializes"),FDWCombatNumericProvider::Build(*Settings,Base,Provider,Error))||!Provider){AddError(Error);return false;}
    FDWSpearTuning Attack;
    if(!TestTrue(TEXT("Current spear timing is jointly valid"),FDWSpearTuning::Build(*Provider->GetRules(),Attack,Error)))return false;
    TestTrue(TEXT("All phases fit the attack cycle"),Attack.WindupSeconds+Attack.ActiveSeconds+Attack.RecoverySeconds<=Attack.IntervalSeconds+1.e-8);
    FDWCombatHUDConfig HUDConfig;
    if(!TestTrue(TEXT("Same source supplies a real HUD config"),FDWCombatHUDConfig::Build(*Provider->GetRules(),HUDConfig,Error)))return false;
    TestTrue(TEXT("HUD health interval is initialized"),HUDConfig.Player.Vitals.HealthMax>HUDConfig.Player.Vitals.HealthFloor);
    TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());Save->Version=UDWSaveGame::CurrentVersion;
    if(!TestTrue(TEXT("Same validated source builds a new-adventure resource projection"),DWCombatPlayerSave::MakeNewFullSurvival(*Provider->GetRules(),*Save.Get(),Error))){AddError(Error);return false;}
    TestTrue(TEXT("New save projection validates without disk access"),DWCombatPlayerSave::Validate(*Provider->GetRules(),*Save.Get(),Error));
    TestTrue(TEXT("New player starts alive"),Save->Health>HUDConfig.Player.Vitals.HealthFloor);
    TestEqual(TEXT("New adventure has full water"),Save->CombatPlayerResources.Vitals.Water,HUDConfig.Player.Vitals.WaterMax);
    TestEqual(TEXT("New adventure has full satiety"),Save->CombatPlayerResources.Vitals.Satiety,HUDConfig.Player.Vitals.SatietyMax);
    return true;
}
#endif
