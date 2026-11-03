#if WITH_DEV_AUTOMATION_TESTS
#include "DWThugDropPolicy.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatWorldObjectsSave.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWThugDropPolicyTest,"DoughWorld.CreatureSystems.ThugGuaranteedCore",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWThugDropPolicyTest::RunTest(const FString&)
{
 const FString Mold=TEXT("/Game/DoughWorld/Core/Data/DA_DWCreature_MoldThug.DA_DWCreature_MoldThug");
 const FString Yeast=TEXT("/Game/DoughWorld/Core/Data/DA_DWCreature_YeastThug.DA_DWCreature_YeastThug");
 TestTrue(TEXT("Exact mold definition and profile"),DWThugDropPolicy::IsTarget(Mold,EDWEnemyCombatProfile::MoldGrunt));
 TestTrue(TEXT("Exact yeast definition and profile"),DWThugDropPolicy::IsTarget(Yeast,EDWEnemyCombatProfile::YeastGrunt));
 TestFalse(TEXT("Mismatched profile is not silently replaced"),DWThugDropPolicy::IsTarget(Mold,EDWEnemyCombatProfile::YeastGrunt));
 TestFalse(TEXT("Other same-profile creature keeps its policy"),DWThugDropPolicy::IsTarget(Mold+TEXT("_Copy"),EDWEnemyCombatProfile::MoldGrunt));
 const FString MoldClass=TEXT("/Game/DoughWorld/Characters/Enemies/Blueprints/BP_DWCreature_MoldThug.BP_DWCreature_MoldThug_C");
 TestTrue(TEXT("Ended source policy reconstructs from exact saved class/profile without live corpse"),DWThugDropPolicy::IsTargetSavedClass(MoldClass,EDWEnemyCombatProfile::MoldGrunt));
 TestFalse(TEXT("Other saved enemy has no new retry policy"),DWThugDropPolicy::IsTargetSavedClass(TEXT("/Script/GDATtest.DWEnemyCharacter"),EDWEnemyCombatProfile::MoldGrunt));
 TestTrue(TEXT("First attempt allowed"),DWThugDropPolicy::CanAttemptSpawn(true,0,0.,0.));
 TestFalse(TEXT("Retry waits for one second"),DWThugDropPolicy::CanAttemptSpawn(true,1,.5,1.));
 TestTrue(TEXT("Second attempt allowed at deadline"),DWThugDropPolicy::CanAttemptSpawn(true,1,1.,1.));
 TestFalse(TEXT("Exhausted record stays bounded without any origin parameter"),DWThugDropPolicy::CanAttemptSpawn(true,3,999.,2.));
 TestTrue(TEXT("Unrelated legacy record keeps original retry admission"),DWThugDropPolicy::CanAttemptSpawn(false,3,0.,99.));
 TestEqual(TEXT("Existing ordinary mold identity"),DWThugDropPolicy::CoreId(EDWEnemyCombatProfile::MoldGrunt,false),FName(TEXT("DWCore_Mold")));
 TestEqual(TEXT("Existing formal yeast identity"),DWThugDropPolicy::CoreId(EDWEnemyCombatProfile::YeastGrunt,true),FName(TEXT("YeastEssence")));
 TestTrue(TEXT("Both own aliases deduplicate"),DWThugDropPolicy::IsCoreAlias(TEXT("DWCore_Mold"),EDWEnemyCombatProfile::MoldGrunt)&&DWThugDropPolicy::IsCoreAlias(TEXT("MoldEssence"),EDWEnemyCombatProfile::MoldGrunt));
 TestFalse(TEXT("Other creature core is not removed"),DWThugDropPolicy::IsCoreAlias(TEXT("YeastEssence"),EDWEnemyCombatProfile::MoldGrunt));
 FDWCombatCoreDropSave S;S.Schema=2;S.DeathId=FGuid::NewGuid();S.StableEnemyId=FGuid::NewGuid();S.Profile=EDWEnemyCombatProfile::MoldGrunt;
 S.FatalEpoch=1;S.FatalReceipt=3;S.EnemyEntity=2;S.EnemyLife=1;S.SourceStableId=TEXT("enemy:")+S.StableEnemyId.ToString(EGuidFormats::Digits);
 S.GenericGroup=FGuid::NewGuid();S.GenericOrdinal=1;S.Decision=EDWEnemyCoreDropDecision::SpawnPending;S.PickupId=FGuid::NewGuid();S.ItemId=TEXT("MoldEssence");S.Count=1;
 TestTrue(TEXT("Uncredited new death uses supported V2 shape without forged player"),DWCreatureDropPolicy::ValidateGenericShape(S));
 S.Decision=EDWEnemyCoreDropDecision::Claimed;TestTrue(TEXT("Claim fact remains terminal supported shape"),DWCreatureDropPolicy::ValidateGenericShape(S));
 S.PlayerEntity=1;TestFalse(TEXT("Partial player attribution still rejected"),DWCreatureDropPolicy::ValidateGenericShape(S));S.PlayerEntity=0;
 S.FatalReceipt=0;TestFalse(TEXT("No synthetic death without fatal receipt"),DWCreatureDropPolicy::ValidateGenericShape(S));
 S.FatalReceipt=3;S.Decision=EDWEnemyCoreDropDecision::SpawnPending;
 FDWCombatWorldObjectsSave World;World.MapPackage=TEXT("/Game/QA/ThugCoreFacts");World.Recovery.Schema=1;World.Recovery.PlayerId=FGuid::NewGuid();
 FDWCombatEnemyObjectSave Enemy;Enemy.StableId=S.SourceStableId;Enemy.ActorPath=TEXT("/Game/QA/ThugCoreFacts.ThugCoreFacts:PersistentLevel.Enemy");
 Enemy.ClassPath=MoldClass;Enemy.Life={2,1};Enemy.MaxHealth=100;Enemy.Health=0;Enemy.State=3;Enemy.Profile=S.Profile;
 Enemy.Formal.Schema=1;Enemy.Formal.Profile=S.Profile;Enemy.Formal.StableEnemyId=S.StableEnemyId;
 Enemy.GenericDropGroup=S.GenericGroup;Enemy.GenericDropRows=1;Enemy.bGenericDropsCommitted=true;World.Enemies.Add(Enemy);World.CoreDrops.Add(S);
 FString Error;TArray<uint8> Bytes;FDWCombatWorldObjectsSave Restored;
 TestTrue(TEXT("Formal corpse with uncredited guaranteed row encodes"),DWCombatWorldObjectsSave::Encode(World,Bytes,Error));
 TestTrue(TEXT("Full V2 facts decode without constructing gameplay world or using real saves"),DWCombatWorldObjectsSave::Decode(Bytes,Restored,Error));
 TestTrue(TEXT("Decoded core count and absent player remain factual"),Restored.CoreDrops.Num()==1&&Restored.CoreDrops[0].Count==1&&Restored.CoreDrops[0].PlayerEntity==0);
 auto Invalid=World;Invalid.CoreDrops.Add(S);TestFalse(TEXT("Duplicate death record cannot add a second core"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=World;Invalid.CoreDrops[0].GenericGroup=FGuid::NewGuid();TestFalse(TEXT("Unbound group cannot bypass corpse identity"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=World;Invalid.Enemies[0].Formal.DropDecision=EDWEnemyCoreDropDecision::Claimed;
 TestFalse(TEXT("Old core branch and new group are exclusive"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 World.CoreDrops[0].Decision=EDWEnemyCoreDropDecision::Claimed;
 TestTrue(TEXT("Full claimed facts round-trip with no reroll"),DWCombatWorldObjectsSave::Encode(World,Bytes,Error)&&DWCombatWorldObjectsSave::Decode(Bytes,Restored,Error)&&Restored.CoreDrops[0].Decision==EDWEnemyCoreDropDecision::Claimed);
 World.Enemies[0].bEnded=true;
 TestTrue(TEXT("Destroyed corpse tombstone retains the validated source used for transient retry policy"),DWCombatWorldObjectsSave::Encode(World,Bytes,Error)&&DWCombatWorldObjectsSave::Decode(Bytes,Restored,Error)&&Restored.Enemies[0].bEnded&&DWThugDropPolicy::IsTargetSavedClass(Restored.Enemies[0].ClassPath,Restored.Enemies[0].Profile));
 return true;
}
#endif
