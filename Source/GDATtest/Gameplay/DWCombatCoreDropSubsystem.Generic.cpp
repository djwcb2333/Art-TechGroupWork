#include "DWCombatCoreDropSubsystem.h"
#include "DWCreatureDropConfiguration.h"
#include "DWEnemyCharacter.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWCombatRules.h"
#include "Engine/World.h"
bool UDWCombatCoreDropSubsystem::PrepareGenericFatal(ADWEnemyCharacter& E,DWCombatCommit::FReceiptId Receipt,DWCombatCommit::FEntityLife Life,DWCombatCommit::FEntityLife Player,
 FDWPreparedCoreDrop& Out,const FVector* FallPoint,DWCombatCommit::FReceiptId FallReceipt)
{
 FDWCreatureResolvedDrops Drops;FString DropError;if(!E.ResolveCreatureDrops(Drops,DropError))return false;auto* GI=GetWorld()?GetWorld()->GetGameInstance<UDWGameInstance>():nullptr;
 const auto* C=GI?GI->GetConfigForMap(UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())):nullptr;
 FString Error;FDWEnemyCoreDropConfig Pickup;
 if(!Operational()||Revision==MAX_uint64||Drops.Policy!=EDWCreatureDropPolicy::ConfiguredItems||!E.bCreatureDefinitionValid||
  E.bGenericDropsCommitted||E.GetWorld()!=GetWorld()||E.IsActorBeingDestroyed()||!Receipt.Epoch||!Receipt.Id||!Life.Entity||!Life.Life||
  !C||!GI->GetCombatRules()||!DWEnemyFormal::BuildCoreDropConfig(*GI->GetCombatRules(),Pickup,Error)||
  !DWCreatureDropPolicy::ValidateEntriesForConfig(Drops.Entries,C,Error)||
  (FallPoint?FallPoint->ContainsNaN():false)||Records.Num()>65536-Drops.Entries.Num())return false;
 FDWPreparedCoreDrop P;P.Revision=Revision;P.Enemy=&E;P.bGeneric=true;P.GenericGroup=FGuid::NewGuid();P.GenericPickupRadiusCm=Pickup.PickupRadiusCm;
 P.GenericSourceStableId=E.CombatProfile!=EDWEnemyCombatProfile::Legacy?TEXT("enemy:")+E.FormalEnemyId.ToString(EGuidFormats::Digits):E.CombatWorldStableId;
 if(P.GenericSourceStableId.IsEmpty())P.GenericSourceStableId=E.SourceNest?TEXT("enemy:runtime:")+FGuid::NewGuid().ToString(EGuidFormats::Digits):TEXT("enemy:scene:")+UWorld::RemovePIEPrefix(E.GetPathName());
 for(int32 I=0;I<Drops.Entries.Num();++I)
 {
  const auto& Row=Drops.Entries[I];FDWCombatCoreDropSave S;S.Schema=2;S.DeathId=FGuid::NewGuid();S.StableEnemyId=E.FormalEnemyId.IsValid()?E.FormalEnemyId:P.GenericGroup;
  S.Profile=E.CombatProfile;S.FatalEpoch=Receipt.Epoch;S.FatalReceipt=Receipt.Id;S.EnemyEntity=Life.Entity;S.EnemyLife=Life.Life;S.PlayerEntity=Player.Entity;S.PlayerLife=Player.Life;
  S.SourceStableId=P.GenericSourceStableId;S.GenericGroup=P.GenericGroup;S.GenericOrdinal=I+1;S.Location=FallPoint?*FallPoint:E.GetActorLocation();
  S.ForcedFallReceiptEpoch=FallReceipt.Epoch;S.ForcedFallReceiptId=FallReceipt.Id;
  // Like the original core policy, only credited player kills draw a drop. Environmental deaths freeze failed rows.
  if(Player.Entity&&Player.Life&&FMath::FRand()<Row.Probability)
  {S.Decision=EDWEnemyCoreDropDecision::SpawnPending;S.ItemId=Row.ItemId;S.Count=FMath::RandRange(Row.MinimumCount,Row.MaximumCount);S.PickupId=FGuid::NewGuid();}
  else S.Decision=EDWEnemyCoreDropDecision::Failed;
  if(!DWCreatureDropPolicy::ValidateGenericShape(S))return false;P.GenericRows.Add(MoveTemp(S));
 }
 Records.Reserve(Records.Num()+P.GenericRows.Num());P.bValid=true;Out=MoveTemp(P);return true;
}
