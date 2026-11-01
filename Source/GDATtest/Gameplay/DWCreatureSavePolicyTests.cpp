#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatWorldObjectsSave.h"
#include "Misc/AutomationTest.h"
#include "DWCreatureDefinition.h"
#include "DWGameplayConfig.h"
#include "DWCombatRules.h"
#include "DWCombatMoldRoot.h"
#include "DWCombatBow.h"
#include "DWCombatShotgun.h"
#include "DWCharacterA01.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCreatureSavePolicyTest,"DoughWorld.CreatureSystems.LegacyNPCSnapshot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCreatureSavePolicyTest::RunTest(const FString&)
{
 FDWCombatWorldObjectsSave S;S.MapPackage=TEXT("/Game/QA/CreatureSnapshot");S.Recovery.Schema=1;S.Recovery.PlayerId=FGuid::NewGuid();
 FDWCombatEnemyObjectSave NPC;NPC.StableId=TEXT("enemy:scene:NPC");NPC.ActorPath=TEXT("/Game/QA/CreatureSnapshot.CreatureSnapshot:PersistentLevel.NPC");
 NPC.ClassPath=TEXT("/Script/GDATtest.DWEnemyCharacter");NPC.PersistentId=TEXT("CreaturePolicyNPC");NPC.Life={2,1};NPC.Health=77;NPC.MaxHealth=100;
 NPC.Profile=EDWEnemyCombatProfile::Legacy;NPC.Home=FVector(100,200,0);NPC.PatrolGoal=FVector(300,400,0);NPC.bHasPatrolGoal=true;NPC.PatrolRemaining=1.25;
 S.Enemies.Add(NPC);FString Error;TArray<uint8> Bytes;FDWCombatWorldObjectsSave D;
 TestTrue(TEXT("Registered legacy NPC snapshot accepted without formal core identity"),DWCombatWorldObjectsSave::Encode(S,Bytes,Error));
 TestEqual(TEXT("Legacy-only snapshots retain V1 encoding"),Bytes[4],uint8(1));
 TestTrue(TEXT("Plain facts decode without constructing a world or touching saves"),DWCombatWorldObjectsSave::Decode(Bytes,D,Error));
 if(D.Enemies.Num()!=1){AddError(Error);return false;}
 TestEqual(TEXT("Same host health"),D.Enemies[0].Health,77.f);TestEqual(TEXT("Same host max health"),D.Enemies[0].MaxHealth,100.f);
 TestTrue(TEXT("Patrol goal preserved"),D.Enemies[0].PatrolGoal==NPC.PatrolGoal&&D.Enemies[0].bHasPatrolGoal);TestEqual(TEXT("Remaining wait preserved"),D.Enemies[0].PatrolRemaining,1.25);
 S.Enemies[0].Health=0;S.Enemies[0].State=3;S.Enemies[0].bHasPatrolGoal=false;
 TestTrue(TEXT("Neutral empty-drop death uses same legacy tombstone"),DWCombatWorldObjectsSave::Encode(S,Bytes,Error));
 TestTrue(TEXT("Dead NPC decodes"),DWCombatWorldObjectsSave::Decode(Bytes,D,Error));
 TestEqual(TEXT("Death state preserved"),D.Enemies[0].State,uint8(3));
 auto Invalid=S;Invalid.Enemies[0].Formal.Schema=1;
 TestFalse(TEXT("Definition does not bypass legacy/formal identity validation"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=S;Invalid.Enemies[0].MaxHealth=0;
 TestFalse(TEXT("Invalid health remains rejected"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 // V2 stores frozen results and row identities; no UWorld, actor spawn, inventory or file I/O is used.
 S.Enemies[0].bGenericDropsCommitted=true;S.Enemies[0].GenericDropGroup=FGuid::NewGuid();S.Enemies[0].GenericDropRows=2;
 FDWCombatCoreDropSave Row;Row.Schema=2;Row.DeathId=FGuid::NewGuid();Row.StableEnemyId=S.Enemies[0].GenericDropGroup;
 Row.Profile=EDWEnemyCombatProfile::Legacy;Row.Decision=EDWEnemyCoreDropDecision::Spawned;Row.FatalEpoch=1;Row.FatalReceipt=7;Row.EnemyEntity=2;Row.EnemyLife=1;
 Row.PlayerEntity=1;Row.PlayerLife=1;Row.PickupId=FGuid::NewGuid();Row.ItemId=TEXT("Flour");Row.Count=3;
 Row.SourceStableId=NPC.StableId;Row.GenericGroup=S.Enemies[0].GenericDropGroup;Row.GenericOrdinal=1;S.CoreDrops.Add(Row);
 Row.DeathId=FGuid::NewGuid();Row.GenericOrdinal=2;Row.Decision=EDWEnemyCoreDropDecision::Failed;Row.PickupId.Invalidate();Row.ItemId=NAME_None;Row.Count=0;S.CoreDrops.Add(Row);
 TestTrue(TEXT("Two frozen rows of one death encode"),DWCombatWorldObjectsSave::Encode(S,Bytes,Error));
 TestEqual(TEXT("Generic extension uses explicit V2 encoding"),Bytes[4],uint8(2));
 TestTrue(TEXT("V2 generic group decodes"),DWCombatWorldObjectsSave::Decode(Bytes,D,Error));
 if(D.CoreDrops.Num()!=2){AddError(Error);return false;}
 TestEqual(TEXT("Frozen item count preserved"),D.CoreDrops[0].Count,3);TestEqual(TEXT("Failed RNG remains failed"),uint8(D.CoreDrops[1].Decision),uint8(EDWEnemyCoreDropDecision::Failed));
 Invalid=S;Invalid.CoreDrops[1].GenericOrdinal=1;TestFalse(TEXT("Duplicate ordinal rejected"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=S;Invalid.CoreDrops[1].FatalReceipt=8;TestFalse(TEXT("Mixed death receipts rejected"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=S;Invalid.CoreDrops[0].SourceStableId=TEXT("other");TestFalse(TEXT("Unbound source rejected"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=S;Invalid.CoreDrops.RemoveAt(1);TestFalse(TEXT("Missing row rejected"),DWCombatWorldObjectsSave::Validate(Invalid,Error));
 Invalid=S;Invalid.CoreDrops[0].Decision=EDWEnemyCoreDropDecision::Claimed;
 TestTrue(TEXT("Claim is a persisted terminal fact"),DWCombatWorldObjectsSave::Encode(Invalid,Bytes,Error)&&DWCombatWorldObjectsSave::Decode(Bytes,D,Error)&&D.CoreDrops[0].Decision==EDWEnemyCoreDropDecision::Claimed);
 FDWCreatureDropEntry Entry;Entry.ItemId=TEXT("Flour");TArray<FDWCreatureDropEntry> Entries{Entry};
 const auto Exists=[](FName Id){return Id==TEXT("Flour");};
 TestTrue(TEXT("Current catalog row accepted"),DWCreatureDropPolicy::ValidateEntries(Entries,Exists,Error));
 Entries[0].ItemId=TEXT("Unknown");TestFalse(TEXT("Unknown item rejected before RNG"),DWCreatureDropPolicy::ValidateEntries(Entries,Exists,Error));
 Entries[0]=Entry;Entries[0].MinimumCount=2;TestFalse(TEXT("Invalid min/max rejected"),DWCreatureDropPolicy::ValidateEntries(Entries,Exists,Error));
 Entries[0]=Entry;Entries[0].Probability=-1;TestFalse(TEXT("Invalid probability rejected"),DWCreatureDropPolicy::ValidateEntries(Entries,Exists,Error));
 auto* Config=NewObject<UDWGameplayConfig>();Config->Items.Reset();FDWItemDefinition Def;Def.ItemId=TEXT("Flour");Config->Items.Add(Def);Config->MaxStackSize=40;
 Entries={Entry};TestTrue(TEXT("Normal current material fits a legal stack"),DWCreatureDropPolicy::ValidateEntriesForConfig(Entries,Config,Error));
 Entries[0].MaximumCount=41;TestFalse(TEXT("A row too large for any single stack is rejected"),DWCreatureDropPolicy::ValidateEntriesForConfig(Entries,Config,Error));
 Def.ItemId=TEXT("SugarSpear");Config->Items.Add(Def);Entries[0]=Entry;Entries[0].ItemId=TEXT("SugarSpear");
 TestFalse(TEXT("Permanent weapon alias cannot bypass ownership transaction"),DWCreatureDropPolicy::ValidateEntriesForConfig(Entries,Config,Error));
 Entries.Reset();for(int I=0;I<17;++I)Entries.Add(Entry);TestFalse(TEXT("Unbounded row count rejected"),DWCreatureDropPolicy::ValidateEntries(Entries,Exists,Error));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRootModifierProjectionTest,"DoughWorld.CreatureSystems.RootModifierProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRootModifierProjectionTest::RunTest(const FString&)
{
 TSharedPtr<const FDWCombatRules> Rules;FString Error;FDWMoldRootConfig Root;FDWSpearTuning Spear;FDWBowConfig Bow;FDWShotgunConfig Shotgun;
 if(!TestTrue(TEXT("Load current project numeric rules without creating a world"),FDWCombatRules::LoadProjectPayload(Rules,Error)))return false;
 if(!TestTrue(TEXT("Build retained stance and weapon projections"),FDWMoldRootConfig::Build(*Rules,Root,Error)&&FDWSpearTuning::Build(*Rules,Spear,Error)&&FDWBowConfig::Build(*Rules,Bow,Error)&&FDWShotgunConfig::Build(*Rules,Shotgun,Error)))return false;
 TestEqual(TEXT("Rooted spear uses the retained independent damage multiplier"),Spear.DamageMultiplier(EDWSpearForm::MoldRooted),Root.DamageMultiplier);
 TestEqual(TEXT("Rooted bow uses retained ranged rate"),Bow.Rate(EDWBowForm::MoldRooted),Root.RangedRateMultiplier);
 TestEqual(TEXT("Rooted shotgun uses retained ranged rate"),Shotgun.Rate(EDWSpearForm::MoldRooted),Root.RangedRateMultiplier);
 TestEqual(TEXT("Rooted shotgun uses retained damage"),Shotgun.DamageScale(EDWSpearForm::MoldRooted),Root.DamageMultiplier);
 TestTrue(TEXT("Rooted bow uses damage once"),FMath::IsNearlyEqual(Bow.Shot(EDWBowForm::MoldRooted).Damage,Bow.Base.Damage*Root.DamageMultiplier,1.e-8));
 TestEqual(TEXT("Normal A01 weapon view does not inherit Yeast damage"),Spear.DamageMultiplier(EDWSpearForm::Normal),1.);
 TestEqual(TEXT("Root stance movement tuning remains zero"),Root.MoveCentimetersPerSecond,0.);
 FDWCharacterA01Config C;FDWCharacterA01Runtime Runtime;Runtime.Initialize(C);Runtime.EnterForm(EDWA01Form::Mold);
 TestTrue(TEXT("Root startup pays from the same A01 pool"),Runtime.SpendCorruption(Root.StartCost));
 FDWCharacterA01Activity Activity;Activity.CorruptionSpendPerSecond=Root.SpendPerSecond;Activity.bSuppressCorruptionRegeneration=true;FDWCharacterA01Step Step;
 TestTrue(TEXT("A01 root time advances using nominal clock while movement is zero"),Runtime.AdvanceNext(.5,Activity,Step)&&Step.AdvancedSeconds==.5);
 TestTrue(TEXT("Root time drains without old or new passive refund"),FMath::IsNearlyEqual(Runtime.GetState().Corruption,C.CorruptionMax-Root.StartCost-.5*Root.SpendPerSecond,1.e-8));
 Runtime.BeginToolAction(1,true,false);const auto Bonus=Runtime.ResolveToolHit(1,true,true);
 TestTrue(TEXT("Corruption enhancement remains a separate paid source"),Bonus.bChargedThisCall&&FMath::IsNearlyEqual(Bonus.DamageMultiplier,1.+C.CorruptionDamageBonus,1.e-8));
 AddInfo(FString::Printf(TEXT("Current retained root: incoming=%g damage=%g ranged_rate=%g; paid corruption damage=%g"),Root.IncomingMultiplier,Root.DamageMultiplier,Root.RangedRateMultiplier,Bonus.DamageMultiplier));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDefinitionLegacySourceTest,"DoughWorld.CreatureSystems.DefinitionLegacySource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDefinitionLegacySourceTest::RunTest(const FString&)
{
 auto* D=NewObject<UDWCreatureDefinition>();D->NumericSource=EDWCreatureNumericSource::DefinitionLegacyHost;D->MaxHealth=222.f;D->MoveSpeed=190.f;
 FString Error;
 TestTrue(TEXT("Class definition 222/190 accepts its own live/saved projection, independent of actor CDO HP defaults"),DWCreaturePersistencePolicy::ValidateSource(D,D,EDWEnemyCombatProfile::Legacy,222.f,222.f,190.f,Error));
 TestFalse(TEXT("Old saved cap100 cannot replace current definition222"),DWCreaturePersistencePolicy::ValidateSource(D,D,EDWEnemyCombatProfile::Legacy,100.f,222.f,190.f,Error));
 TestFalse(TEXT("Forged saved cap333 rejected"),DWCreaturePersistencePolicy::ValidateSource(D,D,EDWEnemyCombatProfile::Legacy,333.f,222.f,190.f,Error));
 TestFalse(TEXT("A live mixed100/190 projection cannot be captured"),DWCreaturePersistencePolicy::ValidateSource(D,D,EDWEnemyCombatProfile::Legacy,222.f,100.f,190.f,Error));
 TestFalse(TEXT("Definition move source is checked before commit"),DWCreaturePersistencePolicy::ValidateSource(D,D,EDWEnemyCombatProfile::Legacy,222.f,222.f,250.f,Error));
 auto* Override=NewObject<UDWCreatureDefinition>();Override->MaxHealth=222.f;Override->MoveSpeed=190.f;
 TestFalse(TEXT("Equal numeric values do not make instance definition identity persistable"),DWCreaturePersistencePolicy::ValidateSource(Override,D,EDWEnemyCombatProfile::Legacy,222.f,222.f,190.f,Error));
 TestFalse(TEXT("Instance-only definition rejected"),DWCreaturePersistencePolicy::ValidateSource(D,nullptr,EDWEnemyCombatProfile::Legacy,222.f,222.f,190.f,Error));
 TestFalse(TEXT("Removed class-default definition rejected"),DWCreaturePersistencePolicy::ValidateSource(nullptr,D,EDWEnemyCombatProfile::Legacy,222.f,222.f,190.f,Error));
 TestTrue(TEXT("Old no-definition Legacy saved cap contract unchanged"),DWCreaturePersistencePolicy::ValidateSource(nullptr,nullptr,EDWEnemyCombatProfile::Legacy,100.f,222.f,190.f,Error));
 TestEqual(TEXT("Failed validation never mutates definition HP"),D->MaxHealth,222.f);TestEqual(TEXT("Failed validation never mutates definition speed"),D->MoveSpeed,190.f);
 return true;
}
#endif
