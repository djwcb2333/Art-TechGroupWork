#include "DWFormalLanguageProjection.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatRules.h"
#include "DWSaveGame.h"
#include "DWFormalSaveVersionPolicy.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWFormalLanguageProjectionTest,"DoughWorld.FormalLanguage.Projection",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWFormalLanguageProjectionTest::RunTest(const FString& Parameters)
{
 using namespace DWFormalLanguageProjection;
 FString Error,Text;TSharedPtr<FJsonObject> Trial;TSharedPtr<const FDWCombatRules> Rules;
 if(!FDWCombatRules::LoadProjectPayload(Rules,Error)){AddError(Error);return false;}
 const FString Path=FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWA01TrialDefaults.json");
 if(!FFileHelper::LoadFileToString(Text,*Path)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Trial)||!Trial)
 {AddError(TEXT("Read-only trial configuration fixture unavailable."));return false;}
 // A small legitimate test catalog; production must pass the actual loaded catalog and trial config.
 DWLanguage::FCatalog Catalog;DWLanguage::FTalentDefinition A4;A4.Cost=1;Catalog.Talents.Add(TEXT("a4"),A4);
 TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());
 if(!Save.IsValid()||!DWCombatPlayerSave::MakeNew(*Rules,*Save,Error)){AddError(Error);return false;}
 Save->CombatWorld.bNewAdventure=true;
 FDWAdventureFlowSave Flow;Flow.Schema=1;Flow.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 Flow.ScenarioId=TEXT("SC0101");Flow.OpeningMap=TEXT("/Game/DoughWorld/OpeningPrototype/L_SC0101_Awakening");
 Flow.DestinationMap=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_Map1");
 Flow.Phase=EDWAdventureOpeningPhase::Pending;Flow.bStarterGranted=false;
 FDWFormalLanguageSave Envelope;EBlockKind Kind=EBlockKind::Formal;
 TestTrue(TEXT("Genuine old envelope classifies without changing old save"),Classify(Envelope,*Save,Kind,Error));
 TestTrue(TEXT("Empty legacy is legacy"),Kind==EBlockKind::Legacy);
 FDWAdventureFlowSave LegacyFlow;
 TestTrue(TEXT("Legacy flow and empty block remain readable"),ValidateBound(Envelope,LegacyFlow,0,*Save,Catalog,Trial,*Rules,Kind,Error));
 Save->Version=5;Save->AdventureFlow=Flow;
 TestFalse(TEXT("New adventure cannot silently drop its formal block"),ValidateBound(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Kind,Error));
 // Existing v4 Flow1 remains old mechanics, including every persisted phase.
 Save->Version=4;
 for(const auto Phase:{EDWAdventureOpeningPhase::Pending,EDWAdventureOpeningPhase::CompletedPendingDestination,EDWAdventureOpeningPhase::Completed})
 {
  Save->AdventureFlow.Phase=Phase;const FDWAdventureFlowSave OldFlow=Save->AdventureFlow;
  TestTrue(TEXT("Deployed v4 Flow1 may omit Formal2 without migration"),ValidateBound(Envelope,OldFlow,0,*Save,Catalog,Trial,*Rules,Kind,Error));
  TestTrue(TEXT("Old Flow1 classification remains legacy"),Kind==EBlockKind::Legacy);
 }
 DWFormalSaveVersionPolicy::EFamily Family;
 Save->Version=0;TestFalse(TEXT("Container zero rejected"),DWFormalSaveVersionPolicy::Classify(*Save,Family,Error));
 Save->Version=6;TestFalse(TEXT("Future container rejected"),DWFormalSaveVersionPolicy::Classify(*Save,Family,Error));
 Save->Version=5;Save->AdventureFlow={};TestFalse(TEXT("Version 5 cannot erase both Flow1 and Formal2"),DWFormalSaveVersionPolicy::Classify(*Save,Family,Error));
 Save->AdventureFlow=Flow;TestFalse(TEXT("Version 5 may not silently become old Flow1"),DWFormalSaveVersionPolicy::Classify(*Save,Family,Error));
 Save->Version=4;Save->AdventureFlow={};Save->LanguageTrialSchema=1;
 TestTrue(TEXT("Trial remains delegated to its existing validator"),Classify(Envelope,*Save,Kind,Error));
 TestTrue(TEXT("Trial is distinct from formal"),Kind==EBlockKind::Trial);
 FDWFormalLanguageSave Partial;Partial.SlotIndex=0;
 TestFalse(TEXT("Partially populated schema zero is rejected"),Classify(Partial,*Save,Kind,Error));
 Partial={};Partial.Schema=2;Partial.SlotIndex=0;
 TestFalse(TEXT("Missing formal payloads are rejected"),Classify(Partial,*Save,Kind,Error));
 Save->LanguageTrialSchema=0;Save->Version=5;Save->AdventureFlow=Flow;

 // The new full snapshot is accepted without mutating the older pending slot fixture.
 TStrongObjectPtr<UDWSaveGame> FullSave(NewObject<UDWSaveGame>());
 if(!FullSave.IsValid()||!DWCombatPlayerSave::MakeNewFullSurvival(*Rules,*FullSave,Error)){AddError(Error);return false;}
 FullSave->Version=5;FullSave->AdventureFlow=Flow;FullSave->CombatWorld.bNewAdventure=true;
 FDWCombatPlayerVitalsConfig NewConfig;
 if(!FDWCombatPlayerVitalsConfig::Build(*Rules,NewConfig,Error)){AddError(Error);return false;}
 TestTrue(TEXT("New formal water starts at maximum"),FullSave->CombatPlayerResources.Vitals.Water==NewConfig.Vitals.WaterMax);
 TestTrue(TEXT("New formal satiety starts at maximum"),FullSave->CombatPlayerResources.Vitals.Satiety==NewConfig.Vitals.SatietyMax);
 FDWFormalLanguageSave FullEnvelope;FDWA01SavedProjection FullProjection;
 TestTrue(TEXT("Full new slot passes formal projection"),InitializeNew(Flow,0,*FullSave,Catalog,Trial,*Rules,FullEnvelope,FullProjection,Error));
 TestTrue(TEXT("Full projection matches saved water and satiety"),FullProjection.Runtime.GetState().Water==FullSave->CombatPlayerResources.Vitals.Water&&FullProjection.Runtime.GetState().Satiety==FullSave->CombatPlayerResources.Vitals.Satiety);
 FullSave->CombatPlayerResources.Vitals.Water=Save->CombatPlayerResources.Vitals.Water;
 FDWFormalLanguageSave MixedEnvelope;FDWA01SavedProjection MixedProjection;
 if(FullSave->CombatPlayerResources.Vitals.Water!=NewConfig.Vitals.WaterMax)
  TestFalse(TEXT("Mixed legacy and full initial snapshot rejected"),InitializeNew(Flow,0,*FullSave,Catalog,Trial,*Rules,MixedEnvelope,MixedProjection,Error));

 const auto BeforeVitals=Save->CombatPlayerResources.Vitals;
 const float BeforeHealth=Save->Health,BeforeSpecial=Save->Transformation;
 FDWA01SavedProjection Projection;
 if(!InitializeNew(Flow,0,*Save,Catalog,Trial,*Rules,Envelope,Projection,Error))
 {AddError(FString::Printf(TEXT("Legacy-compatible new slot initialization failed: %s"),*Error));return false;}
 TestTrue(TEXT("Slot index zero is valid"),Envelope.Schema==2&&Envelope.SlotIndex==0);
 TestTrue(TEXT("New slot preserves the exact legacy water"),Projection.Runtime.GetState().Water==BeforeVitals.Water);
 TestTrue(TEXT("New slot preserves the exact legacy satiety"),Projection.Runtime.GetState().Satiety==BeforeVitals.Satiety);
 TestTrue(TEXT("New slot preserves the exact legacy health"),Projection.Runtime.GetState().CurrentHealth==BeforeVitals.Health);
 TestTrue(TEXT("Initialize does not mutate stored mirror"),FDWCombatPlayerVitalsSnapshot::StaticStruct()->CompareScriptStruct(&BeforeVitals,&Save->CombatPlayerResources.Vitals,PPF_None));
 TestTrue(TEXT("Initialize does not mutate top-level mirror"),Save->Health==BeforeHealth&&Save->Transformation==BeforeSpecial);
 TestTrue(TEXT("Pending new slot accepts real ungranted starter receipt"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 TestTrue(TEXT("Formal-only validator accepts pending envelope"),ValidateFormal(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Error));
 TestTrue(TEXT("Formal block bound to active flow"),ValidateBound(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Kind,Error));
 TestTrue(TEXT("Binding returns formal classification"),Kind==EBlockKind::Formal);
 Save->Version=4;TestFalse(TEXT("Formal2 cannot be disguised as deployed v4"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));Save->Version=5;
 TestFalse(TEXT("Unset expected slot cannot identify a formal save"),Prepare(Envelope,Flow,INDEX_NONE,*Save,Catalog,Trial,*Rules,Projection,Error));
 FDWFormalLanguageSave CaseEnvelope=Envelope;TSharedPtr<FJsonObject> CaseRoot;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(CaseEnvelope.ProgressJson),CaseRoot)||!CaseRoot.IsValid())
 {AddError(TEXT("Unable to parse wrapper for GUID case test."));return false;}
 CaseRoot->SetStringField(TEXT("playthrough_id"),Flow.PlaythroughId.ToLower());FString CaseJson;
 if(!FJsonSerializer::Serialize(CaseRoot.ToSharedRef(),TJsonWriterFactory<>::Create(&CaseJson)))
 {AddError(TEXT("Unable to serialize wrapper for GUID case test."));return false;}
 CaseEnvelope.ProgressJson=MoveTemp(CaseJson);
 TestTrue(TEXT("Same GUID accepts alternate letter case"),Prepare(CaseEnvelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 FDWAdventureFlowSave CaseFlow=Flow;CaseFlow.PlaythroughId=Flow.PlaythroughId.ToLower();
 TestTrue(TEXT("Supplied/stored flow compares GUID values regardless of letter case"),Prepare(Envelope,CaseFlow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 TestFalse(TEXT("Wrong active slot fails"),Prepare(Envelope,Flow,1,*Save,Catalog,Trial,*Rules,Projection,Error));
 FDWAdventureFlowSave Other=Flow;Other.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 TestFalse(TEXT("Other valid playthrough ID fails"),Prepare(Envelope,Other,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Other=Flow;Other.PlaythroughId=TEXT("invalid");
 TestFalse(TEXT("Malformed flow GUID fails"),Prepare(Envelope,Other,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Other=Flow;Other.bStarterGranted=true;
 TestFalse(TEXT("New marker cannot pretend starter already committed"),Prepare(Envelope,Other,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 const FDWAdventureFlowSave OriginalFlow=Save->AdventureFlow;
 const FDWFormalLanguageSave OriginalEnvelope=Envelope;
 Save->AdventureFlow.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 Projection.MaximumCooldown=-12345.;Kind=EBlockKind::Legacy;
 TestFalse(TEXT("Same slot but different saved GUID rejects caller flow"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 TestTrue(TEXT("Rejected prepare preserves projection output"),Projection.MaximumCooldown==-12345.);
 TestFalse(TEXT("Bound validator also rejects different saved GUID"),ValidateBound(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Kind,Error));
 TestTrue(TEXT("Rejected bound validation preserves output kind"),Kind==EBlockKind::Legacy);
 TestTrue(TEXT("Rejected validation preserves saved GUID and envelope"),Save->AdventureFlow.PlaythroughId!=OriginalFlow.PlaythroughId&&Envelope.ProgressJson==OriginalEnvelope.ProgressJson);
 Save->AdventureFlow=OriginalFlow;Save->AdventureFlow.Phase=EDWAdventureOpeningPhase::Completed;
 TestFalse(TEXT("Same slot but different saved phase rejected"),ValidateFormal(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Error));
 Save->AdventureFlow=OriginalFlow;Save->AdventureFlow.bStarterGranted=true;
 TestFalse(TEXT("Same slot but different saved receipt rejected"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->AdventureFlow=OriginalFlow;Save->AdventureFlow.ScenarioId=TEXT("OTHER");
 TestFalse(TEXT("Different saved scenario rejected"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->AdventureFlow=OriginalFlow;Save->AdventureFlow.OpeningMap+=TEXT("_Other");
 TestFalse(TEXT("Different saved opening map rejected"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->AdventureFlow=OriginalFlow;Save->AdventureFlow.DestinationMap+=TEXT("_Other");
 TestFalse(TEXT("Different saved destination rejected"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->AdventureFlow=OriginalFlow;
 TestTrue(TEXT("Original projection remains valid after failed identity checks"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->LanguageTrialSchema=1;
 TestFalse(TEXT("A formal block cannot coexist with trial fields"),Classify(Envelope,*Save,Kind,Error));
 Save->LanguageTrialSchema=0;
 FDWFormalLanguageSave Broken=Envelope;Broken.A01StateJson.Reset();
 TestFalse(TEXT("Partial formal state is rejected"),Prepare(Broken,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Broken=Envelope;TSharedPtr<FJsonObject> BadRoot;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Broken.ProgressJson),BadRoot)||!BadRoot)
 {AddError(TEXT("Unable to parse candidate wrapper for mutation test."));return false;}
 BadRoot->SetNumberField(TEXT("schema_version"),1.);FString BadJson;
 if(!FJsonSerializer::Serialize(BadRoot.ToSharedRef(),TJsonWriterFactory<>::Create(&BadJson)))
 {AddError(TEXT("Unable to serialize deliberately invalid wrapper."));return false;}
 Broken.ProgressJson=MoveTemp(BadJson);
 TestFalse(TEXT("Tampered formal wrapper version rejected"),Prepare(Broken,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->CombatPlayerResources.Vitals.Water+=1.;
 TestFalse(TEXT("Water mirror tampering rejected"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->CombatPlayerResources.Vitals=BeforeVitals;Save->CombatPlayerResources.Vitals.StarvationOrdinal++;
 TestFalse(TEXT("Legacy ordinal tampering rejected"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 Save->CombatPlayerResources.Vitals=BeforeVitals;
 FDWFormalLanguageSave EmptyOut;FDWA01SavedProjection OtherProjection;
 Save->CombatPlayerResources.LoadedShotgunShells=1;
 TestFalse(TEXT("Modified new-slot resource subblock is rejected"),InitializeNew(Flow,0,*Save,Catalog,Trial,*Rules,EmptyOut,OtherProjection,Error));
 TestTrue(TEXT("Failed initialize leaves output empty"),EmptyOut.Schema==0&&EmptyOut.SlotIndex==INDEX_NONE&&EmptyOut.ProgressJson.IsEmpty());
 Save->CombatPlayerResources.LoadedShotgunShells=0;
 Save->CombatWorld.bNewAdventure=false;
 TestFalse(TEXT("Running save without committed starter is rejected"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 FDWAdventureFlowSave PreparedFlow;FDWFormalLanguageSave PreparedEnvelope;FDWA01SavedProjection PreparedProjection;
 FDWAdventureFlowSave WrongSource=Flow;WrongSource.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 PreparedProjection.MaximumCooldown=-777.;
 TestFalse(TEXT("Prepared capture rejects different saved flow"),CapturePreparedFirstStarter(WrongSource,0,*Save,Catalog,Trial,*Rules,Projection.LanguageState,Projection.Runtime,0.,PreparedFlow,PreparedEnvelope,PreparedProjection,Error));
 TestTrue(TEXT("Rejected prepared capture preserves outputs and input receipt"),PreparedProjection.MaximumCooldown==-777.&&PreparedEnvelope.Schema==0&&PreparedFlow.Schema==0&&!Save->AdventureFlow.bStarterGranted);
 TestFalse(TEXT("Prepared flow must not alias source save"),CapturePreparedFirstStarter(Flow,0,*Save,Catalog,Trial,*Rules,Projection.LanguageState,Projection.Runtime,0.,Save->AdventureFlow,PreparedEnvelope,PreparedProjection,Error));
 TestTrue(TEXT("Rejected flow alias does not mutate saved receipt"),!Save->AdventureFlow.bStarterGranted);
 TestFalse(TEXT("Prepared projection must not alias source runtime"),CapturePreparedFirstStarter(Flow,0,*Save,Catalog,Trial,*Rules,Projection.LanguageState,Projection.Runtime,0.,PreparedFlow,PreparedEnvelope,Projection,Error));
 TestTrue(TEXT("First starter can prepare without pretending the source receipt is already persisted"),CapturePreparedFirstStarter(Flow,0,*Save,Catalog,Trial,*Rules,Projection.LanguageState,Projection.Runtime,0.,PreparedFlow,PreparedEnvelope,PreparedProjection,Error));
 TestTrue(TEXT("Prepared flow alone carries true receipt and does not mutate source"),PreparedFlow.bStarterGranted&&!Save->AdventureFlow.bStarterGranted);
 TestFalse(TEXT("Prepared flow is not yet valid as a persisted record"),ValidateFormal(PreparedEnvelope,PreparedFlow,0,*Save,Catalog,Trial,*Rules,Error));
 TestFalse(TEXT("Bound validation also rejects the uncommitted flow"),ValidateBound(PreparedEnvelope,PreparedFlow,0,*Save,Catalog,Trial,*Rules,Kind,Error));
 Save->AdventureFlow=PreparedFlow;Flow=PreparedFlow;
 // This fixture stages Flow only. FormalLanguage is not yet a field of the candidate Save;
 // after integration the persisted caller must read the envelope from that SAME Save.
 TestTrue(TEXT("Staged flow matches candidate; external envelope passes preliminary binding"),ValidateBound(PreparedEnvelope,Flow,0,*Save,Catalog,Trial,*Rules,Kind,Error));
 TestTrue(TEXT("Running save with true starter receipt accepted"),Prepare(Envelope,Flow,0,*Save,Catalog,Trial,*Rules,Projection,Error));
 FDWFormalLanguageSave Captured;FDWA01SavedProjection CapturedProjection;
 TestTrue(TEXT("Capture produces a validated formal envelope"),Capture(Flow,0,*Save,Catalog,Trial,*Rules,Projection.LanguageState,Projection.Runtime,0.,Captured,CapturedProjection,Error));
 TestTrue(TEXT("Capture uses same slot and flow GUID"),Captured.Schema==2&&Captured.SlotIndex==0&&Captured.ProgressJson.Contains(Flow.PlaythroughId));
 TestFalse(TEXT("Capture rejects excessive cooldown"),Capture(Flow,0,*Save,Catalog,Trial,*Rules,Projection.LanguageState,Projection.Runtime,1.e9,Captured,CapturedProjection,Error));
 return true;
}
#endif
