#if WITH_DEV_AUTOMATION_TESTS
#include "DWCreatureDropConfiguration.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatWorldObjectsSave.h"
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCreatureDropConfigurationTest,"DoughWorld.CreatureSystems.BlueprintDropConfiguration",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCreatureDropConfigurationTest::RunTest(const FString&)
{
 FDWCreatureDropInput In;In.FallbackEntry.ItemId=TEXT("SugarChunk");
 auto R=DWCreatureDropConfiguration::Resolve(In);
 TestTrue(TEXT("Empty hostile gets configurable default"),R.bUsedHostileDefault&&R.Entries.Num()==1&&R.Entries[0].ItemId==TEXT("SugarChunk"));
 In.Disposition=EDWCreatureDisposition::Neutral;R=DWCreatureDropConfiguration::Resolve(In);
 TestTrue(TEXT("Neutral receives no automatic table"),!R.bUsedHostileDefault&&R.Policy==EDWCreatureDropPolicy::None);
 In.Disposition=EDWCreatureDisposition::AlwaysHostile;In.InheritedPolicy=EDWCreatureDropPolicy::ConfiguredItems;FDWCreatureDropEntry Entry;Entry.ItemId=TEXT("Flour");Entry.Probability=.25f;Entry.MinimumCount=2;Entry.MaximumCount=3;In.InheritedEntries={Entry};
 R=DWCreatureDropConfiguration::Resolve(In);TestTrue(TEXT("Inherited valid rule copied unchanged"),R.Entries.Num()==1&&R.Entries[0].Probability==.25f&&R.Entries[0].MaximumCount==3&&!R.bUsedHostileDefault);
 auto OtherClass=In;In.bOverride=true;Entry.ItemId=TEXT("SugarChunk");In.OverrideEntries={Entry};R=DWCreatureDropConfiguration::Resolve(In);
 TestTrue(TEXT("Class override replaces inherited optional rows"),R.bBlueprintOverride&&R.Entries[0].ItemId==TEXT("SugarChunk"));
 TestEqual(TEXT("Another class sharing DA remains inherited"),DWCreatureDropConfiguration::Resolve(OtherClass).Entries[0].ItemId,FName(TEXT("Flour")));
 In.OverrideEntries.Reset();R=DWCreatureDropConfiguration::Resolve(In);TestTrue(TEXT("Explicit empty optional table is not re-filled"),R.Policy==EDWCreatureDropPolicy::ConfiguredItems&&R.Entries.IsEmpty());
 In.bOverride=false;In.InheritedPolicy=EDWCreatureDropPolicy::None;In.InheritedEntries.Reset();In.bFixedThugCore=true;R=DWCreatureDropConfiguration::Resolve(In);
 TestFalse(TEXT("Fixed thug core does not gain fallback sugar"),R.bUsedHostileDefault);
 In.bFixedThugCore=false;In.Profile=EDWEnemyCombatProfile::MoldGrunt;In.InheritedPolicy=EDWCreatureDropPolicy::ExistingFormalCore;
 TestFalse(TEXT("Existing eligible core policy is preserved"),DWCreatureDropConfiguration::Resolve(In).bUsedHostileDefault);
 In.Profile=EDWEnemyCombatProfile::DoughZombie;TestTrue(TEXT("Core-ineligible hostile can receive default"),DWCreatureDropConfiguration::Resolve(In).bUsedHostileDefault);
 FString Error;TArray<FDWCreatureDropEntry> Bad{Entry};Bad[0].Probability=2.f;
 TestFalse(TEXT("Existing validator rejects invalid probabilities"),DWCreatureDropPolicy::ValidateEntries(Bad,[](FName){return true;},Error));
 // Existing Legacy snapshot schema already supports one frozen generic group without formal identity.
 FDWCombatWorldObjectsSave World;World.MapPackage=TEXT("/Game/QA/LegacyHostileDrop");World.Recovery.Schema=1;World.Recovery.PlayerId=FGuid::NewGuid();
 FDWCombatEnemyObjectSave Enemy;Enemy.StableId=TEXT("enemy:scene:LegacyHostile");Enemy.ActorPath=TEXT("/Game/QA/LegacyHostileDrop.LegacyHostileDrop:PersistentLevel.Enemy");Enemy.ClassPath=TEXT("/Script/GDATtest.DWEnemyCharacter");
 Enemy.Life={2,1};Enemy.Health=0;Enemy.MaxHealth=100;Enemy.State=3;Enemy.Profile=EDWEnemyCombatProfile::Legacy;Enemy.Formal.Schema=0;
 Enemy.bGenericDropsCommitted=true;Enemy.GenericDropGroup=FGuid::NewGuid();Enemy.GenericDropRows=1;World.Enemies.Add(Enemy);
 FDWCombatCoreDropSave Drop;Drop.Schema=2;Drop.DeathId=FGuid::NewGuid();Drop.StableEnemyId=Enemy.GenericDropGroup;Drop.Profile=EDWEnemyCombatProfile::Legacy;
 Drop.FatalEpoch=1;Drop.FatalReceipt=9;Drop.EnemyEntity=2;Drop.EnemyLife=1;Drop.PlayerEntity=1;Drop.PlayerLife=1;
 Drop.SourceStableId=Enemy.StableId;Drop.GenericGroup=Enemy.GenericDropGroup;Drop.GenericOrdinal=1;Drop.Decision=EDWEnemyCoreDropDecision::SpawnPending;Drop.PickupId=FGuid::NewGuid();Drop.ItemId=TEXT("SugarChunk");Drop.Count=1;World.CoreDrops.Add(Drop);
 TArray<uint8> Bytes;FDWCombatWorldObjectsSave Restored;
 TestTrue(TEXT("Legacy hostile generic death encodes under existing strict schema"),DWCombatWorldObjectsSave::Encode(World,Bytes,Error));
 TestTrue(TEXT("Legacy frozen sugar count round-trips without world/AI or formal identity"),DWCombatWorldObjectsSave::Decode(Bytes,Restored,Error)&&Restored.CoreDrops.Num()==1&&Restored.CoreDrops[0].Count==1&&Restored.Enemies[0].Formal.Schema==0);
 auto Invalid=World;Invalid.Enemies[0].Formal.Schema=1;TestFalse(TEXT("Legacy still rejects invented formal identity"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=World;Invalid.CoreDrops.Add(Drop);TestFalse(TEXT("Legacy duplicate drop still rejected"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 World.CoreDrops[0].Decision=EDWEnemyCoreDropDecision::Claimed;TestTrue(TEXT("Legacy claim retained, never rerolled on decode"),DWCombatWorldObjectsSave::Encode(World,Bytes,Error)&&DWCombatWorldObjectsSave::Decode(Bytes,Restored,Error)&&Restored.CoreDrops[0].Decision==EDWEnemyCoreDropDecision::Claimed);
 auto* Neutral=NewObject<UDWCreatureDefinition>();Neutral->Disposition=EDWCreatureDisposition::Neutral;Neutral->NumericSource=EDWCreatureNumericSource::DefinitionLegacyHost;Neutral->MaxHealth=100;Neutral->MoveSpeed=190;
 TestTrue(TEXT("Unchanged neutral Definition save source still accepted"),DWCreaturePersistencePolicy::ValidateSource(Neutral,Neutral,EDWEnemyCombatProfile::Legacy,100,100,190,Error));
 auto* InstanceOnly=NewObject<UDWCreatureDefinition>();InstanceOnly->Disposition=EDWCreatureDisposition::Neutral;
 TestFalse(TEXT("Original NPC instance-only Definition save rejection remains"),DWCreaturePersistencePolicy::ValidateSource(InstanceOnly,Neutral,EDWEnemyCombatProfile::Legacy,100,100,190,Error));
 return true;
}
#endif
