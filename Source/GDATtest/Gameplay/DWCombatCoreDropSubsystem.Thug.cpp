#include "DWCombatCoreDropSubsystem.h"
#include "DWCreatureDropConfiguration.h"
#include "DWThugDropPolicy.h"
#include "DWEnemyCharacter.h"
#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWCombatRules.h"
#include "DWLanguageSubsystem.h"
#include "DWFormalLanguagePersistence.h"
#include "Engine/World.h"

bool UDWCombatCoreDropSubsystem::PrepareThugFatal(ADWEnemyCharacter& E,DWCombatCommit::FReceiptId Receipt,
 DWCombatCommit::FEntityLife Life,DWCombatCommit::FEntityLife Player,FDWPreparedCoreDrop& Out,
 const FVector* FallPoint,DWCombatCommit::FReceiptId FallReceipt)
{
 const auto* D=E.CreatureDefinition.Get();auto* W=GetWorld();
 if(!Operational()||Revision==MAX_uint64||!D||!DWThugDropPolicy::IsTarget(D->GetPathName(),E.CombatProfile)||
  !E.bCreatureDefinitionValid||!E.IsFormalCombatReady()||E.GetWorld()!=W||E.IsActorBeingDestroyed()||
  !E.FormalEnemyId.IsValid()||E.bGenericDropsCommitted||E.FormalCoreDrop.Decision!=EDWEnemyCoreDropDecision::Unresolved||
  !Receipt.Epoch||!Receipt.Id||!Life.Entity||!Life.Life||(Player.Entity==0)!=(Player.Life==0)||
  (FallPoint?(FallPoint->ContainsNaN()||!FallReceipt.Epoch||!FallReceipt.Id||!Player.Entity||!Player.Life):
   (FallReceipt.Epoch!=0||FallReceipt.Id!=0)))return false;
 auto* GI=W->GetGameInstance<UDWGameInstance>();const FString Map=UWorld::RemovePIEPrefix(W->GetOutermost()->GetName());
 const auto* Config=GI?GI->GetConfigForMap(Map):nullptr;auto Drop=E.FormalCoreDropConfig;Drop.bLanguageEssenceIds=false;
 if(!GI||!Config||!FMath::IsFinite(Drop.PickupRadiusCm)||Drop.PickupRadiusCm<=0.||Drop.PickupRadiusCm>MAX_flt)return false;
 auto* Language=GI->GetSubsystem<UDWLanguageSubsystem>();const bool Formal=Language&&FDWFormalLanguagePersistence::IsCurrent(*Language,true);
 FString Error;
 if(DWEnemyFormal::IsLanguageEssenceDropMap(Map)||Formal)
 {
  if(!Language||!Language->IsReady()||(!Language->IsTrialWorld()&&!Language->IsFormalSession())||
   !DWEnemyFormal::ApplyLanguageEssenceDropOverrides(Map,Language->GetTrialConfig(),Drop,Error,Formal))return false;
 }
 // The guaranteed row replaces either spelling of this creature's core, never adds on top of it.
 FDWCreatureResolvedDrops Effective;if(!E.ResolveCreatureDrops(Effective,Error))return false;
 TArray<FDWCreatureDropEntry> Rows;
 FDWCreatureDropEntry Core;Core.ItemId=DWThugDropPolicy::CoreId(E.CombatProfile,Drop.bLanguageEssenceIds);Rows.Add(Core);
 if(Effective.Policy==EDWCreatureDropPolicy::ConfiguredItems)
  for(const auto& Row:Effective.Entries)if(!DWThugDropPolicy::IsCoreAlias(Row.ItemId,E.CombatProfile))Rows.Add(Row);
 if(!DWCreatureDropPolicy::ValidateEntriesForConfig(Rows,Config,Error)||Records.Num()>65536-Rows.Num())return false;
 FDWPreparedCoreDrop P;P.Revision=Revision;P.Enemy=&E;P.bGeneric=true;P.bBoundedSpawnRetry=true;P.GenericGroup=FGuid::NewGuid();
 P.GenericSourceStableId=TEXT("enemy:")+E.FormalEnemyId.ToString(EGuidFormats::Digits);P.GenericPickupRadiusCm=Drop.PickupRadiusCm;
 for(int32 I=0;I<Rows.Num();++I)
 {
  const auto& Row=Rows[I];FDWCombatCoreDropSave S;S.Schema=2;S.DeathId=FGuid::NewGuid();S.StableEnemyId=E.FormalEnemyId;
  S.Profile=E.CombatProfile;S.FatalEpoch=Receipt.Epoch;S.FatalReceipt=Receipt.Id;S.EnemyEntity=Life.Entity;S.EnemyLife=Life.Life;
  S.PlayerEntity=Player.Entity;S.PlayerLife=Player.Life;S.SourceStableId=P.GenericSourceStableId;S.GenericGroup=P.GenericGroup;
  S.GenericOrdinal=I+1;S.Location=FallPoint?*FallPoint:E.GetActorLocation();
  S.ForcedFallReceiptEpoch=FallReceipt.Epoch;S.ForcedFallReceiptId=FallReceipt.Id;
  // Only the guaranteed first row ignores player attribution. Other configured drops retain their old rule.
  if(I==0||(Player.Entity&&Player.Life&&FMath::FRand()<Row.Probability))
  {S.Decision=EDWEnemyCoreDropDecision::SpawnPending;S.ItemId=Row.ItemId;S.Count=I==0?1:FMath::RandRange(Row.MinimumCount,Row.MaximumCount);S.PickupId=FGuid::NewGuid();}
  else S.Decision=EDWEnemyCoreDropDecision::Failed;
  if(!DWCreatureDropPolicy::ValidateGenericShape(S))return false;P.GenericRows.Add(MoveTemp(S));
 }
 Records.Reserve(Records.Num()+P.GenericRows.Num());P.bValid=true;Out=MoveTemp(P);return true;
}
