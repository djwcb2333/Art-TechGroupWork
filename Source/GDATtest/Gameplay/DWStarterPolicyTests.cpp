#if WITH_DEV_AUTOMATION_TESTS
#include "DWStarterPolicy.h"
#include "DWSaveGame.h"
#include "DWFormalSaveVersionPolicy.h"
#include "DWFormalLanguageProjection.h"
#include "DWCombatRules.h"
#include "DWCombatPlayerSave.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Kismet/GameplayStatics.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWStarterPolicyTest,"DoughWorld.Opening.BasicStarterPolicy",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWStarterPolicyTest::RunTest(const FString&)
{
 FString Error,Text;TSharedPtr<FJsonObject> Trial;
 if(!TestTrue(TEXT("Read current starter data"),FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWA01TrialDefaults.json")))&&
    FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Trial)))return false;
 TArray<FDWItemStack> Rows;
 if(!TestTrue(TEXT("Current policy parses exact basic tools"),DWStarterPolicy::ReadBasicBatch(Trial,Rows,Error)))return false;
 TestEqual(TEXT("Three rows"),Rows.Num(),3);
 for(const auto& Row:Rows)
 {TestEqual(TEXT("Each quantity one"),Row.Quantity,1);TestTrue(TEXT("Simple tools are not permanent licenses"),FDWCombatItemRegistry::GetItemRole(Row.ItemId)!=EDWCombatItemRole::PermanentWeapon);}
 auto Bad=Rows;Bad.Emplace(FName(TEXT("DWAmmo_Spore")),100);
 TestFalse(TEXT("No extra hidden ammo"),DWStarterPolicy::ValidateBasicBatch(Bad,Error));
 Bad=Rows;Bad[0].ItemId=TEXT("SugarSpear");TestFalse(TEXT("Permanent spear cannot replace simple spear"),DWStarterPolicy::ValidateBasicBatch(Bad,Error));
 Bad=Rows;Bad[0].Quantity=2;TestFalse(TEXT("No doubled tool"),DWStarterPolicy::ValidateBasicBatch(Bad,Error));
 Bad=Rows;Bad[0]=Bad[1];TestFalse(TEXT("Duplicate tool rejects"),DWStarterPolicy::ValidateBasicBatch(Bad,Error));
 Bad=Rows;Bad.Pop();TestFalse(TEXT("Partial grant rejects"),DWStarterPolicy::ValidateBasicBatch(Bad,Error));
 FDWAdventureFlowSave Legacy;
 TestEqual(TEXT("Default tagged policy remains legacy"),Legacy.StarterPolicyVersion,0);
 Legacy.bStarterGranted=true;TArray<FName> Owned;
 TestFalse(TEXT("Old full ownership contract retained"),DWStarterPolicy::ValidateGrantedOwnership(Legacy,Owned,Error));
 for(const TCHAR* Id:{TEXT("SugarSpear"),TEXT("DWWeapon_ShieldAxe"),TEXT("DWWeapon_Greatsword"),TEXT("DWWeapon_DualDaggers"),TEXT("DWWeapon_SporeGun"),TEXT("DWWeapon_MoldBow"),TEXT("DWWeapon_Shotgun")})
  Owned.Add(FDWCombatItemRegistry::CanonicalWeaponItemId(FName(Id)));
 const auto OriginalOwned=Owned;
 TestTrue(TEXT("Legacy owned inventory unchanged"),DWStarterPolicy::ValidateGrantedOwnership(Legacy,Owned,Error)&&Owned==OriginalOwned);
 auto Basic=Legacy;Basic.StarterPolicyVersion=DWStarterPolicy::BasicTools;
 TestTrue(TEXT("Basic save needs no phantom permanent license"),DWStarterPolicy::ValidateGrantedOwnership(Basic,TArray<FName>(),Error));
 TestTrue(TEXT("Later acquisitions are not stripped"),DWStarterPolicy::ValidateGrantedOwnership(Basic,Owned,Error)&&Owned==OriginalOwned);
 Basic.StarterPolicyVersion=9;TestFalse(TEXT("Unknown future policy rejects"),DWStarterPolicy::ValidateGrantedOwnership(Basic,Owned,Error));
 // Validate the actual new-policy formal projection, not just the old policy0 regression suite.
 TSharedPtr<const FDWCombatRules> Rules;
 if(!TestTrue(TEXT("Load unchanged combat rules"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
 TStrongObjectPtr<UDWSaveGame> Pending(NewObject<UDWSaveGame>());
 if(!TestTrue(TEXT("Prepare full-survival empty new slot"),DWCombatPlayerSave::MakeNewFullSurvival(*Rules,*Pending,Error)))return false;
 Pending->Version=5;Pending->CombatWorld.bNewAdventure=true;
 auto& Flow=Pending->AdventureFlow;Flow.Schema=1;Flow.StarterPolicyVersion=DWStarterPolicy::BasicTools;
 Flow.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);Flow.ScenarioId=TEXT("SC0101");
 Flow.OpeningMap=TEXT("/Game/Fixture/Opening");Flow.DestinationMap=TEXT("/Game/Fixture/Destination");Flow.Phase=EDWAdventureOpeningPhase::Pending;
 DWLanguage::FCatalog Catalog;DWLanguage::FTalentDefinition A4;A4.Cost=1;Catalog.Talents.Add(TEXT("a4"),A4);
 FDWFormalLanguageSave Envelope;FDWA01SavedProjection Projection;
 if(!TestTrue(TEXT("Basic-tools new formal slot passes exact A01 projection"),DWFormalLanguageProjection::InitializeNew(Flow,0,*Pending,Catalog,Trial,*Rules,Envelope,Projection,Error)))return false;
 Pending->FormalLanguage=Envelope;DWFormalLanguageProjection::EBlockKind Kind;
 TestTrue(TEXT("Stored basic policy binds to the proper slot"),DWFormalLanguageProjection::ValidateBoundStored(*Pending,0,Catalog,Trial,*Rules,Kind,Error));
 auto OtherFlow=Flow;OtherFlow.StarterPolicyVersion=DWStarterPolicy::Legacy;
 TestFalse(TEXT("Same GUID cannot substitute starter policy"),DWFormalLanguageProjection::ValidateBound(Envelope,OtherFlow,0,*Pending,Catalog,Trial,*Rules,Kind,Error));
 DWFormalSaveVersionPolicy::EFamily Family;Pending->Version=4;
 TestFalse(TEXT("Old container cannot claim new starter policy"),DWFormalSaveVersionPolicy::Classify(*Pending,Family,Error));
 Pending->Version=5;
 TestTrue(TEXT("New pending projection grants no permanent weapons or shells"),Pending->CombatPlayerResources.OwnedPermanentWeaponIds.IsEmpty()&&Pending->CombatPlayerResources.LoadedShotgunShells==0&&Pending->Inventory.IsEmpty());
 TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());
 Save->Version=5;Save->AdventureFlow=Legacy;Save->AdventureFlow.StarterPolicyVersion=1;Save->Inventory=Rows;
 TArray<uint8> Bytes;TestTrue(TEXT("Memory serialization"),UGameplayStatics::SaveGameToMemory(Save.Get(),Bytes));
 TStrongObjectPtr<UDWSaveGame> Loaded(Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)));
 if(TestTrue(TEXT("Memory decode"),Loaded.IsValid()))
 {
  TestEqual(TEXT("Policy persists"),Loaded->AdventureFlow.StarterPolicyVersion,1);
  TestTrue(TEXT("Flow identity includes marker"),FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&Save->AdventureFlow,&Loaded->AdventureFlow,PPF_None));
  Loaded->AdventureFlow.StarterPolicyVersion=0;
  TestFalse(TEXT("Policy substitution changes flow identity"),FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&Save->AdventureFlow,&Loaded->AdventureFlow,PPF_None));
 }
 // No World/GI/Player/AI, disk save load/restore or gameplay simulation.
 return true;
}
#endif
