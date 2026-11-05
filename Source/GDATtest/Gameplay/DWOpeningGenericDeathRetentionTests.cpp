#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatEnemyRetentionPolicy.h"
#include "DWCombatWorldObjectsSave.h"
#include "Misc/AutomationTest.h"

namespace DWOpeningRetentionTests
{
static FDWCombatWorldObjectsSave Snapshot(bool UseOldRetentionGate)
{
 FDWCombatWorldObjectsSave World;World.MapPackage=TEXT("/Game/QA/OpeningHandoff");World.Recovery.Schema=1;World.Recovery.PlayerId=FGuid::NewGuid();
 for(int32 I=0;I<3;++I)
 {
  FDWCombatEnemyObjectSave Enemy;
  Enemy.StableId=FString::Printf(TEXT("enemy:scene:TeachingSour_%d"),I);Enemy.ActorPath=FString::Printf(TEXT("/Game/QA/OpeningHandoff.OpeningHandoff:PersistentLevel.TeachingSour_%d"),I);
  Enemy.ClassPath=TEXT("/Game/DoughWorld/Characters/Enemies/Sour/Blueprints/BP_DWSourDoughEnemy.BP_DWSourDoughEnemy_C");
  Enemy.Profile=EDWEnemyCombatProfile::Legacy;Enemy.Life={static_cast<uint64>(I+2),1};Enemy.MaxHealth=60;Enemy.Health=0;Enemy.State=3;
  Enemy.bEnded=true;Enemy.bHidden=true;Enemy.bActorTick=false;Enemy.bCollision=false;Enemy.bMeshTick=false;
  Enemy.GenericDropGroup=FGuid::NewGuid();Enemy.GenericDropRows=1;Enemy.bGenericDropsCommitted=true;
  FDWCombatCoreDropSave Drop;Drop.Schema=2;Drop.DeathId=FGuid::NewGuid();Drop.StableEnemyId=Enemy.GenericDropGroup;Drop.Profile=Enemy.Profile;
  Drop.FatalEpoch=1;Drop.FatalReceipt=static_cast<uint64>(100+I);Drop.EnemyEntity=Enemy.Life.Entity;Drop.EnemyLife=Enemy.Life.Life;Drop.PlayerEntity=1;Drop.PlayerLife=1;
  Drop.SourceStableId=Enemy.StableId;Drop.GenericGroup=Enemy.GenericDropGroup;Drop.GenericOrdinal=1;
  // Already claimed sugar still retains its immutable fatal source; nothing rolls or grants here.
  Drop.Decision=EDWEnemyCoreDropDecision::Claimed;Drop.PickupId=FGuid::NewGuid();Drop.ItemId=TEXT("SugarChunk");Drop.Count=1;
  World.CoreDrops.Add(Drop);
  if(!UseOldRetentionGate&&DWCombatEnemyRetentionPolicy::NeedsSourceTombstone(Enemy.Profile,false,Enemy.bGenericDropsCommitted))World.Enemies.Add(Enemy);
 }
 return World;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWOpeningGenericDeathRetentionTest,"DoughWorld.Combat.Persistence.Opening.LegacyGenericDeathRetention",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWOpeningGenericDeathRetentionTest::RunTest(const FString&)
{
 // DTOs + in-memory codec only: no World, actor, player input, slot I/O, death commit, drop RNG or travel.
 using DWCombatEnemyRetentionPolicy::NeedsSourceTombstone;FString Error;
 TestTrue(TEXT("Legacy/no-definition committed generic group requires a source"),NeedsSourceTombstone(EDWEnemyCombatProfile::Legacy,false,true));
 TestFalse(TEXT("Original unrelated Legacy/no-definition/no-generic omission preserved"),NeedsSourceTombstone(EDWEnemyCombatProfile::Legacy,false,false));
 TestTrue(TEXT("Definition Legacy source retained as before"),NeedsSourceTombstone(EDWEnemyCombatProfile::Legacy,true,false));
 for(auto Profile:{EDWEnemyCombatProfile::MoldGrunt,EDWEnemyCombatProfile::YeastGrunt,EDWEnemyCombatProfile::DoughZombie,EDWEnemyCombatProfile::DoughElite})
  TestTrue(TEXT("All original formal retention paths unchanged"),NeedsSourceTombstone(Profile,false,false));
 const auto Broken=DWOpeningRetentionTests::Snapshot(true);
 TestFalse(TEXT("Actual old missing-source shape reproduces handoff failure under unchanged validator"),DWCombatWorldObjectsSave::Validate(Broken,Error));
 TestEqual(TEXT("Same diagnostic as user log"),Error,FString(TEXT("Generic death has no retained source tombstone")));
 const auto Good=DWOpeningRetentionTests::Snapshot(false);
 if(!TestTrue(TEXT("Three retained Legacy sources satisfy original strict world snapshot validator"),DWCombatWorldObjectsSave::Validate(Good,Error))){AddError(Error);return false;}
 TestEqual(TEXT("Three destroyed source tombstones"),Good.Enemies.Num(),3);TestEqual(TEXT("Exactly original three terminal drop facts, no extra loot"),Good.CoreDrops.Num(),3);
 TArray<uint8> Bytes;FDWCombatWorldObjectsSave Decoded;
 if(!TestTrue(TEXT("Existing schema codec roundtrip in memory only"),DWCombatWorldObjectsSave::Encode(Good,Bytes,Error)&&DWCombatWorldObjectsSave::Decode(Bytes,Decoded,Error))){AddError(Error);return false;}
 for(int32 I=0;I<3;++I)
 {
  const auto& E=Decoded.Enemies[I];const auto& D=Decoded.CoreDrops[I];
  TestTrue(TEXT("Death source, group, claim, life and HP60 stay exact after codec"),E.bEnded&&E.bGenericDropsCommitted&&E.MaxHealth==60&&E.Health==0&&D.SourceStableId==E.StableId&&D.GenericGroup==E.GenericDropGroup&&D.EnemyEntity==E.Life.Entity&&D.EnemyLife==E.Life.Life&&D.Decision==EDWEnemyCoreDropDecision::Claimed&&D.Count==1);
 }
 auto Bad=Good;Bad.Enemies.RemoveAt(1);TestFalse(TEXT("Removing even one corpse source remains rejected"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 Bad=Good;Bad.CoreDrops[0].GenericGroup=FGuid::NewGuid();TestFalse(TEXT("Forged group cannot bypass matching"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 Bad=Good;Bad.CoreDrops[0].SourceStableId=TEXT("enemy:scene:Forged");TestFalse(TEXT("Forged source stable ID rejected"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 Bad=Good;const auto Duplicate=Bad.CoreDrops[0];Bad.CoreDrops.Add(Duplicate);TestFalse(TEXT("Duplicate fatal fact remains rejected"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 Bad=Good;Bad.CoreDrops[0].FatalReceipt=0;TestFalse(TEXT("Missing fatal provenance still rejected"),DWCombatWorldObjectsSave::Validate(Bad,Error));
 auto EmptyGroup=Good;EmptyGroup.CoreDrops.RemoveAt(0);EmptyGroup.Enemies[0].GenericDropRows=0;
 TestTrue(TEXT("Committed zero-row group retains source without manufacturing drops"),NeedsSourceTombstone(EDWEnemyCombatProfile::Legacy,false,true)&&DWCombatWorldObjectsSave::Validate(EmptyGroup,Error));
 auto Failed=Good;auto& F=Failed.CoreDrops[0];F.Decision=EDWEnemyCoreDropDecision::Failed;F.Count=0;F.ItemId=NAME_None;F.PickupId.Invalidate();
 TestTrue(TEXT("Failed drop result remains terminal and valid with retained source"),DWCombatWorldObjectsSave::Validate(Failed,Error));
 return true;
}
#endif
