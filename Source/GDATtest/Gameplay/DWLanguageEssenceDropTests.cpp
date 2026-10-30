#if WITH_DEV_AUTOMATION_TESTS
#include "DWEnemyCombatProfile.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatRules.h"
#include "DWCombatWorldObjectsSave.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
namespace DWLanguageEssenceDropTests
{
FString TrialMap(){return TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest");}
TSharedPtr<FJsonObject> Trial(double Probability,double Count)
{
 auto Root=MakeShared<FJsonObject>();auto Parameters=MakeShared<FJsonObject>();auto P=MakeShared<FJsonObject>();auto C=MakeShared<FJsonObject>();
 P->SetNumberField(TEXT("value"),Probability);C->SetNumberField(TEXT("value"),Count);
 Parameters->SetObjectField(TEXT("essence.drop_probability"),P);Parameters->SetObjectField(TEXT("essence.drop_count"),C);Root->SetObjectField(TEXT("parameters"),Parameters);return Root;
}
FDWCombatWorldObjectsSave WorldWithDrop(const FString& Map,EDWEnemyCombatProfile Profile,FName Item)
{
 FDWCombatWorldObjectsSave S;S.MapPackage=Map;S.Recovery.Schema=1;S.Recovery.PlayerId=FGuid::NewGuid();
 FDWCombatCoreDropSave D;D.DeathId=FGuid::NewGuid();D.StableEnemyId=FGuid::NewGuid();D.Profile=Profile;D.Decision=EDWEnemyCoreDropDecision::SpawnPending;
 D.FatalEpoch=1;D.FatalReceipt=10;D.EnemyEntity=2;D.EnemyLife=1;D.PlayerEntity=3;D.PlayerLife=1;D.PickupId=FGuid::NewGuid();D.ItemId=Item;D.Count=1;
 FDWCombatEnemyObjectSave E;E.StableId=TEXT("enemy:")+D.StableEnemyId.ToString(EGuidFormats::Digits);
 E.ActorPath=TEXT("/Game/QA/EssenceCodec:PersistentLevel.Enemy");E.ClassPath=TEXT("/Script/GDATtest.DWEnemyCharacter");E.Life={2,1};E.Health=0.f;E.MaxHealth=100.f;E.State=3;E.Profile=Profile;E.bEnded=true;
 auto& F=E.Formal;F.Schema=1;F.Profile=Profile;F.StableEnemyId=D.StableEnemyId;F.BirthLocation=E.Home;F.DropDecision=D.Decision;
 F.FatalReceiptEpoch=D.FatalEpoch;F.FatalReceiptId=D.FatalReceipt;F.DeathEntityId=D.EnemyEntity;F.DeathLife=D.EnemyLife;F.CreditedPlayerEntityId=D.PlayerEntity;F.CreditedPlayerLife=D.PlayerLife;
 F.CoreDropDeathId=D.DeathId;F.PickupId=D.PickupId;F.CoreItemId=D.ItemId;F.CoreCount=D.Count;
 S.Enemies.Add(E);S.CoreDrops.Add(D);return S;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageEssenceDecisionTest,
 "DoughWorld.LanguageIntegration.EssenceDrop.CurrentConfigNewIdentityAndNoReroll",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageEssenceDecisionTest::RunTest(const FString&)
{
 using namespace DWLanguageEssenceDropTests;TSharedPtr<const FDWCombatRules> Rules;FString Error;
 if(!TestTrue(TEXT("Current legacy base payload"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
 FDWEnemyCoreDropConfig Legacy;if(!TestTrue(TEXT("Legacy projection"),DWEnemyFormal::BuildCoreDropConfig(*Rules,Legacy,Error)))return false;
 FString Json;TSharedPtr<FJsonObject> Current;
 if(!TestTrue(TEXT("Current actual trial file"),FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWA01TrialDefaults.json"))))||
    !TestTrue(TEXT("Current actual trial JSON"),FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Current)))return false;
 auto Config=Legacy;if(!TestTrue(TEXT("Exact LANG consumes current trial keys"),DWEnemyFormal::ApplyLanguageEssenceDropOverrides(TrialMap(),Current,Config,Error)))return false;
 TestTrue(TEXT("New typed identity selected"),Config.bLanguageEssenceIds);
 const auto Params=Current->GetObjectField(TEXT("parameters"));
 TestEqual(TEXT("Probability came from essence key"),Config.MoldProbability,Params->GetObjectField(TEXT("essence.drop_probability"))->GetNumberField(TEXT("value")));
 TestEqual(TEXT("Count came from essence key"),Config.Count,static_cast<int32>(Params->GetObjectField(TEXT("essence.drop_count"))->GetNumberField(TEXT("value"))));
 // Distinct in-memory values prove the new getters, rather than equality with legacy .5/1.
 if(!TestTrue(TEXT("Config override remains data driven"),DWEnemyFormal::ApplyLanguageEssenceDropOverrides(TrialMap(),Trial(.75,2.),Config,Error)))return false;
 TestEqual(TEXT("New probability overrides legacy"),Config.YeastProbability,.75);TestEqual(TEXT("New count overrides legacy"),Config.Count,2);
 FDWEnemyCoreDropRecord Mold,Yeast,Failed;const FGuid Pickup=FGuid::NewGuid();
 TestTrue(TEXT("Production Mold decision succeeds"),DWEnemyFormal::DecideCoreDrop(Mold,EDWEnemyCombatProfile::MoldGrunt,Config,{1,10},{2,1},{3,1},.5,Pickup));
 TestEqual(TEXT("Real selected item is new MoldEssence"),Mold.CoreItemId,FName(TEXT("MoldEssence")));TestEqual(TEXT("One result carries configured count"),Mold.Count,2);
 const auto DeathOutcome=Mold;
 TestFalse(TEXT("Committed success cannot reroll/double-grant"),DWEnemyFormal::DecideCoreDrop(Mold,EDWEnemyCombatProfile::MoldGrunt,Legacy,{1,10},{2,1},{3,1},0.,FGuid::NewGuid()));
 TestEqual(TEXT("Receipt retained"),Mold.FatalReceipt.Id,DeathOutcome.FatalReceipt.Id);TestEqual(TEXT("Item retained"),Mold.CoreItemId,DeathOutcome.CoreItemId);TestEqual(TEXT("Pickup retained"),Mold.PickupId,DeathOutcome.PickupId);
 TestTrue(TEXT("Production Yeast decision succeeds"),DWEnemyFormal::DecideCoreDrop(Yeast,EDWEnemyCombatProfile::YeastGrunt,Config,{1,11},{4,1},{3,1},.5,FGuid::NewGuid()));
 TestEqual(TEXT("Real selected item is new YeastEssence"),Yeast.CoreItemId,FName(TEXT("YeastEssence")));
 TestTrue(TEXT("Equal boundary records terminal failure"),DWEnemyFormal::DecideCoreDrop(Failed,EDWEnemyCombatProfile::MoldGrunt,Config,{1,12},{5,1},{3,1},.75,FGuid::NewGuid()));
 TestFalse(TEXT("Failure cannot reroll"),DWEnemyFormal::DecideCoreDrop(Failed,EDWEnemyCombatProfile::MoldGrunt,Config,{1,12},{5,1},{3,1},0.,FGuid::NewGuid()));
 FDWEnemyCoreDropRecord Old;auto LegacyGuaranteed=Legacy;LegacyGuaranteed.MoldProbability=1.;
 TestTrue(TEXT("Old projection still produces one old core"),DWEnemyFormal::DecideCoreDrop(Old,EDWEnemyCombatProfile::MoldGrunt,LegacyGuaranteed,{1,13},{6,1},{3,1},0.,FGuid::NewGuid()));
 TestEqual(TEXT("Old ID unchanged"),Old.CoreItemId,FDWCombatItemRegistry::MoldCoreId());
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageEssencePolicyGateTest,
 "DoughWorld.LanguageIntegration.EssenceDrop.ExactMapProfileAndInvalidConfig",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageEssencePolicyGateTest::RunTest(const FString&)
{
 using namespace DWLanguageEssenceDropTests;using P=EDWEnemyCombatProfile;FString Error;
 FDWEnemyCoreDropConfig C;C.MoldProbability=C.YeastProbability=.25;C.Count=1;C.StackLimit=40;C.PickupRadiusCm=150.;C.bLanguageEssenceIds=true;
 TestTrue(TEXT("Old map does not require new trial JSON"),DWEnemyFormal::ApplyLanguageEssenceDropOverrides(TEXT("/Game/QA/OldMap"),{},C,Error));
 TestFalse(TEXT("Old map clears any copied essence flag"),C.bLanguageEssenceIds);TestEqual(TEXT("Old numeric config preserved"),C.MoldProbability,.25);
 const auto Before=C;
 TestFalse(TEXT("Probability >1 rejected"),DWEnemyFormal::ApplyLanguageEssenceDropOverrides(TrialMap(),Trial(1.1,1.),C,Error));
 TestFalse(TEXT("Fractional count rejected"),DWEnemyFormal::ApplyLanguageEssenceDropOverrides(TrialMap(),Trial(.5,1.5),C,Error));
 TestFalse(TEXT("Missing config rejected"),DWEnemyFormal::ApplyLanguageEssenceDropOverrides(TrialMap(),{},C,Error));
 TestEqual(TEXT("Failed prepare preserved config"),C.MoldProbability,Before.MoldProbability);TestEqual(TEXT("Failed prepare preserved count"),C.Count,Before.Count);TestFalse(TEXT("Failed prepare did not enable new IDs"),C.bLanguageEssenceIds);
 for(const FString& Map:{TrialMap(),FString(TEXT("/Game/QA/OldMap"))})
 {
  TestTrue(TEXT("Corresponding legacy Mold ID remains readable"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::MoldGrunt,FDWCombatItemRegistry::MoldCoreId(),Map));
  TestTrue(TEXT("Corresponding legacy Yeast ID remains readable"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::YeastGrunt,FDWCombatItemRegistry::YeastCoreId(),Map));
  TestFalse(TEXT("Cross-faction legacy ID refused"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::MoldGrunt,FDWCombatItemRegistry::YeastCoreId(),Map));
 }
 TestTrue(TEXT("New Mold ID on exact map"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::MoldGrunt,TEXT("MoldEssence"),TrialMap()));
 TestFalse(TEXT("Cross-profile essence refused"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::YeastGrunt,TEXT("MoldEssence"),TrialMap()));
 TestFalse(TEXT("Other profile refused"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::DoughZombie,TEXT("MoldEssence"),TrialMap()));
 TestFalse(TEXT("Arbitrary ID refused"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::MoldGrunt,TEXT("Water"),TrialMap()));
 TestFalse(TEXT("Short map name refused"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::MoldGrunt,TEXT("MoldEssence"),TEXT("L_DoughWorld_LanguagePlaytest")));
 TestFalse(TEXT("Prefix/suffixed map refused"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::MoldGrunt,TEXT("MoldEssence"),TrialMap()+TEXT("_Copy")));
 TestFalse(TEXT("Map spelling remains case sensitive"),DWEnemyFormal::IsSavedCoreDropItemAllowed(P::MoldGrunt,TEXT("MoldEssence"),TrialMap().ToLower()));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWLanguageEssenceCodecTest,
 "DoughWorld.LanguageIntegration.EssenceDrop.FullObjectsCodecNewAndLegacyIdReadback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWLanguageEssenceCodecTest::RunTest(const FString&)
{
 using namespace DWLanguageEssenceDropTests;using P=EDWEnemyCombatProfile;FString Error;TArray<uint8> Bytes;
 auto S=WorldWithDrop(TrialMap(),P::MoldGrunt,TEXT("MoldEssence"));
 if(!TestTrue(TEXT("Complete enemy+tombstone+drop+recovery fixture validates"),DWCombatWorldObjectsSave::Validate(S,Error)))return false;
 if(!TestTrue(TEXT("New-ID world object payload encodes"),DWCombatWorldObjectsSave::Encode(S,Bytes,Error)))return false;
 FDWCombatWorldObjectsSave Read;
 if(!TestTrue(TEXT("New-ID world object payload reads"),DWCombatWorldObjectsSave::Decode(Bytes,Read,Error)))return false;
 TestEqual(TEXT("Exact new persisted item"),Read.CoreDrops[0].ItemId,FName(TEXT("MoldEssence")));TestEqual(TEXT("Enemy-drop identity remains equal"),Read.Enemies[0].Formal.CoreItemId,Read.CoreDrops[0].ItemId);
 auto Bad=S;Bad.MapPackage=TEXT("/Game/QA/OldMap");TestFalse(TEXT("New item rejected on old source map"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 Bad=S;Bad.CoreDrops[0].ItemId=TEXT("YeastEssence");Bad.Enemies[0].Formal.CoreItemId=TEXT("YeastEssence");TestFalse(TEXT("Consistent but wrong-profile ID still rejected"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 Bad=S;Bad.CoreDrops[0].ItemId=TEXT("Water");Bad.Enemies[0].Formal.CoreItemId=TEXT("Water");TestFalse(TEXT("Consistent arbitrary ID still rejected"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 auto Old=WorldWithDrop(TEXT("/Game/QA/OldMap"),P::MoldGrunt,FDWCombatItemRegistry::MoldCoreId());
 if(!TestTrue(TEXT("Unchanged old-ID payload still writes"),DWCombatWorldObjectsSave::Encode(Old,Bytes,Error))||
    !TestTrue(TEXT("Unchanged old-ID payload still reads"),DWCombatWorldObjectsSave::Decode(Bytes,Read,Error)))return false;
 TestEqual(TEXT("Old ID is not bulk converted"),Read.CoreDrops[0].ItemId,FDWCombatItemRegistry::MoldCoreId());
 Old.MapPackage=TrialMap();TestTrue(TEXT("Pre-fix LANG save with old matching core also remains legal"),DWCombatWorldObjectsSave::Validate(Old,Error));
 return true;
}
#endif
