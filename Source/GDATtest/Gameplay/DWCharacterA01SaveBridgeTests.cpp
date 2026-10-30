#include "DWCharacterA01SaveBridge.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "DWCombatRules.h"
#include "DWSaveGame.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWA01SaveProjectionTest,"DoughWorld.LanguageIntegration.A01.SaveProjection",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWA01SaveProjectionTest::RunTest(const FString& Parameters)
{
 FString Error,Text;TSharedPtr<FJsonObject> Trial;TSharedPtr<const FDWCombatRules> Rules;
 if(!FDWCombatRules::LoadProjectPayload(Rules,Error)){AddError(Error);return false;}
 const FString Path=FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWA01TrialDefaults.json");
 if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Trial)||!Trial){AddError(TEXT("Trial JSON fixture unavailable."));return false;}
 DWLanguage::FCatalog Catalog;DWLanguage::FTalentDefinition A4;A4.Cost=1;Catalog.Talents.Add(TEXT("a4"),A4);Catalog.PointSources.Add(TEXT("FixtureAward"),1);
 DWLanguage::FState LearnedState;LearnedState.RewardSources.Add(TEXT("FixtureAward"),1);LearnedState.LearnedTalentPaidCosts.Add(TEXT("a4"),1);
 TestTrue(TEXT("Fixture language state has a fully paid legitimate talent"),DWLanguage::ValidateState(Catalog,LearnedState,Error));
 TSet<FName> Learned;Learned.Add(TEXT("a4"));FDWCharacterA01Config C;
 TestTrue(TEXT("Saved talents derive an independent A01 config"),DWCharacterA01SaveBridge::BuildDerivedConfig(Trial,Learned,C,Error));
 TestEqual(TEXT("Default a4 derives maximum 120"),C.HealthyMax,120.);
 FDWCharacterA01Runtime Runtime;TestTrue(TEXT("A01 model initializes"),Runtime.Initialize(C));Runtime.SyncCurrentHealth(120.);
 FDWCombatPlayerVitalsConfig MirrorConfig;FDWCombatPlayerVitalsSnapshot Mirror;
 TestTrue(TEXT("Dynamic mirror preserves 120 rather than clipping to 100"),DWCharacterA01SaveBridge::BuildMirror(*Rules,Runtime,MirrorConfig,Mirror,Error));
 TestEqual(TEXT("Compatibility mirror health stays 120"),Mirror.Health,120.);
 TestEqual(TEXT("Compatibility configuration max stays 120"),MirrorConfig.Vitals.HealthMax,120.);
 FDWCombatPlayerVitalsConfig Old;FDWCombatPlayerVitalsConfig::Build(*Rules,Old,Error);
 TestFalse(TEXT("An ordinary old config cannot validate this 120 snapshot"),FDWCombatPlayerVitalsRuntime::Validate(Old,Mirror,Error));
 TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());Save->LanguageTrialSchema=1;
 Save->MapPackage=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest");
 Save->Health=120.f;Save->Transformation=0.f;Save->bYeastForm=false;Save->CombatPlayerResources.Vitals=Mirror;
 const FString Playthrough=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 const auto Wrap=[&](const DWLanguage::FState& State)
 {
  FString Native;DWLanguage::StateToJson(Catalog,State,Native,Error);TSharedPtr<FJsonObject> Model;
  FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Native),Model);auto Root=MakeShared<FJsonObject>();
  Root->SetNumberField(TEXT("schema_version"),1);Root->SetStringField(TEXT("playthrough_id"),Playthrough);Root->SetObjectField(TEXT("model"),Model);
  auto Starter=MakeShared<FJsonObject>();Starter->SetStringField(TEXT("playthrough_id"),Playthrough);Starter->SetBoolField(TEXT("granted"),true);Root->SetObjectField(TEXT("starter_grant"),Starter);
  FString Wrapped;FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Wrapped));return Wrapped;
 };
 Save->LanguageProgressJson=Wrap(LearnedState);
 TestTrue(TEXT("Resource state serializes with independent energies and loss clocks"),DWCharacterA01SaveBridge::StateToJson(Runtime.Capture(),.2,Save->A01StateJson,Error));
 FDWA01SavedProjection Projection;
 TestTrue(TEXT("Pure whole-envelope projection validates without touching live language"),DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(*Rules,*Save,Catalog,Trial,Projection,Error));
 TestEqual(TEXT("Projection keeps saved health 120"),Projection.Runtime.GetState().CurrentHealth,120.);
 TestEqual(TEXT("Projection keeps saved remaining cooldown"),Projection.CooldownRemaining,.2);
 Save->Health=100.f;
 TestFalse(TEXT("HP mirror mismatch rejects instead of silently clamping"),DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(*Rules,*Save,Catalog,Trial,Projection,Error));Save->Health=120.f;
 Save->CombatPlayerResources.Vitals.Water+=1.;
 TestFalse(TEXT("Water mirror mismatch rejects"),DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(*Rules,*Save,Catalog,Trial,Projection,Error));Save->CombatPlayerResources.Vitals=Mirror;
 Save->MapPackage=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype");
 TestFalse(TEXT("Trial schema never applies to an ordinary map"),DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(*Rules,*Save,Catalog,Trial,Projection,Error));
 Save->MapPackage=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest");
 DWLanguage::FState WithoutA4;WithoutA4.RewardSources.Add(TEXT("FixtureAward"),1);WithoutA4.AvailableTalentPoints=1;Save->LanguageProgressJson=Wrap(WithoutA4);
 TestFalse(TEXT("Removing a4 from saved language cannot authorize 120 HP"),DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(*Rules,*Save,Catalog,Trial,Projection,Error));
 Save->LanguageProgressJson=Wrap(LearnedState);const FString StateJson=Save->A01StateJson;Save->A01StateJson.Reset();
 TestFalse(TEXT("Incomplete parallel block rejects"),DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(*Rules,*Save,Catalog,Trial,Projection,Error));
 Save->A01StateJson=StateJson;Save->LanguageTrialSchema=0;
 TestTrue(TEXT("Partial trial fields are still classified as trial and fail closed"),DWCharacterA01SaveBridge::HasTrialFields(*Save));
 TestFalse(TEXT("Schema zero plus populated trial data is rejected"),DWCharacterA01SaveBridge::ValidateProjectionWithDefinitions(*Rules,*Save,Catalog,Trial,Projection,Error));
 Save->LanguageProgressJson.Reset();Save->A01StateJson.Reset();
 TestFalse(TEXT("Genuine old saves have no trial envelope and retain their original path"),DWCharacterA01SaveBridge::HasTrialFields(*Save));
 return true;
}
#endif
