#include "DWCombatCoreDropSubsystem.h"
#include "DWGameplayConfig.h"
#include "DWCombatItemRegistry.h"
bool DWCreatureDropPolicy::ValidateEntries(const TArray<FDWCreatureDropEntry>& Entries,TFunctionRef<bool(FName)> Exists,FString& Error)
{
 if(Entries.Num()>16){Error=TEXT("Creature drop table exceeds 16 rows");return false;}
 int64 Budget=0;
 for(const auto& R:Entries)
 {
  if(R.ItemId.IsNone()||!Exists(R.ItemId)||!FMath::IsFinite(R.Probability)||R.Probability<0.f||R.Probability>1.f||R.MinimumCount<1||R.MaximumCount<R.MinimumCount||R.MaximumCount>10000)
  {Error=TEXT("Creature drop item/probability/count is invalid or not in the current item catalog");return false;}
  Budget+=R.MaximumCount;
 }
 if(Budget>10000){Error=TEXT("Creature drop quantity budget exceeds 10000");return false;}Error.Reset();return true;
}
bool DWCreatureDropPolicy::ValidateGenericShape(const FDWCombatCoreDropSave& S)
{
 if(S.Schema!=2||!S.DeathId.IsValid()||!S.StableEnemyId.IsValid()||!S.GenericGroup.IsValid()||
  S.GenericOrdinal<1||S.GenericOrdinal>16||S.SourceStableId.IsEmpty()||S.SourceStableId.Len()>4096||S.SourceStableId.Len()!=FCString::Strlen(*S.SourceStableId)||
  S.Location.ContainsNaN()||!S.FatalEpoch||!S.FatalReceipt||!S.EnemyEntity||!S.EnemyLife||S.Profile>EDWEnemyCombatProfile::DoughElite||
  (S.PlayerEntity==0)!=(S.PlayerLife==0)||(S.ForcedFallReceiptEpoch==0)!=(S.ForcedFallReceiptId==0)||(S.ForcedFallReceiptId&&!S.PlayerEntity))return false;
 if(S.Decision==EDWEnemyCoreDropDecision::Failed)return !S.PickupId.IsValid()&&S.ItemId.IsNone()&&S.Count==0;
 return (S.Decision==EDWEnemyCoreDropDecision::SpawnPending||S.Decision==EDWEnemyCoreDropDecision::Spawned||S.Decision==EDWEnemyCoreDropDecision::Claimed)&&
  S.PickupId.IsValid()&&!S.ItemId.IsNone()&&S.Count>0&&S.Count<=10000;
}

bool DWCreatureDropPolicy::ValidateEntriesForConfig(const TArray<FDWCreatureDropEntry>& Entries,const UDWGameplayConfig* C,FString& Error)
{
 if(!C||!ValidateEntries(Entries,[C](FName Id){return C->IsInventoryItem(Id)&&C->GetConfiguredStackLimit(Id)>0&&FDWCombatItemRegistry::GetItemRole(Id)!=EDWCombatItemRole::PermanentWeapon;},Error))return false;
 for(const auto& R:Entries)if(R.MaximumCount>C->GetConfiguredStackLimit(R.ItemId))
 {Error=TEXT("Each creature drop row must fit the current item's single stack limit; permanent weapons require their ownership transaction and are not supported by generic rows");return false;}
 return true;
}
