#pragma once
#include "CoreMinimal.h"
#include "DWAdventureFlowSave.h"
#include "DWGameplayTypes.h"
#include "DWCombatItemRegistry.h"
#include "Dom/JsonObject.h"
/** Pure manifest/compatibility policy. No inventory mutation, gameplay owner or save IO. */
namespace DWStarterPolicy
{
 inline constexpr int32 Legacy=0, BasicTools=1;
 inline bool IsKnown(int32 Policy){return Policy==Legacy||Policy==BasicTools;}
 inline bool IsBasicTool(FName Id)
 {return Id==TEXT("SimpleSugarSpear")||Id==TEXT("SimpleSugarAxe")||Id==TEXT("SimpleSugarPickaxe");}
 inline bool ValidateBasicBatch(const TArray<FDWItemStack>& Items,FString& Error)
 {
  TSet<FName> Seen;
  for(const auto& Row:Items)
  {
   if(!IsBasicTool(Row.ItemId)||Row.Quantity!=1||Seen.Contains(Row.ItemId))
   {Error=TEXT("Basic starter must contain only three distinct simple tools, one each.");return false;}
   Seen.Add(Row.ItemId);
  }
  if(Seen.Num()!=3){Error=TEXT("Incomplete basic starter; no partial grant.");return false;}
  Error.Reset();return true;
 }
 inline bool ReadBasicBatch(const TSharedPtr<FJsonObject>& Trial,TArray<FDWItemStack>& Out,FString& Error)
 {
  const TSharedPtr<FJsonObject>* Policy=nullptr;const TSharedPtr<FJsonObject>* Parameters=nullptr;
  const TArray<TSharedPtr<FJsonValue>>* Items=nullptr;double StarterPolicyNumber=0.;TArray<FDWItemStack> Rows;
  if(!Trial||!Trial->TryGetObjectField(TEXT("weapon_policy"),Policy)||!Policy||!Policy->IsValid()||
     !(*Policy)->TryGetNumberField(TEXT("starter_policy_version"),StarterPolicyNumber)||StarterPolicyNumber!=BasicTools||
     !(*Policy)->TryGetArrayField(TEXT("starter_items"),Items)||!Trial->TryGetObjectField(TEXT("parameters"),Parameters)||!Parameters||!Parameters->IsValid())
  {Error=TEXT("Missing or invalid basic starter policy.");return false;}
  for(const auto& Value:*Items)
  {
   const TSharedPtr<FJsonObject>* Row=nullptr;const TSharedPtr<FJsonObject>* CountRow=nullptr;FString Id,Parameter;double Count=0.;
   if(!Value||!Value->TryGetObject(Row)||!Row||!Row->IsValid()||
      !(*Row)->TryGetStringField(TEXT("item_id"),Id)||!(*Row)->TryGetStringField(TEXT("count_parameter"),Parameter)||
      !(*Parameters)->TryGetObjectField(Parameter,CountRow)||!CountRow||!CountRow->IsValid()||
      !(*CountRow)->TryGetNumberField(TEXT("value"),Count)||!FMath::IsFinite(Count)||Count!=1.)
   {Error=TEXT("Invalid basic starter row/count; no partial grant.");return false;}
   Rows.Emplace(FName(*Id),static_cast<int32>(Count));
  }
  if(!ValidateBasicBatch(Rows,Error))return false;Out=MoveTemp(Rows);Error.Reset();return true;
 }
 inline bool ValidateGrantedOwnership(const FDWAdventureFlowSave& Flow,const TArray<FName>& Owned,FString& Error)
 {
  if(!IsKnown(Flow.StarterPolicyVersion)){Error=TEXT("Unknown starter policy; original save preserved.");return false;}
  // Preserve the deployed legacy contract. Basic tools may later be dropped/used and are not permanent licenses.
  if(Flow.bStarterGranted&&Flow.StarterPolicyVersion==Legacy)
   for(const TCHAR* Id:{TEXT("SugarSpear"),TEXT("DWWeapon_ShieldAxe"),TEXT("DWWeapon_Greatsword"),TEXT("DWWeapon_DualDaggers"),TEXT("DWWeapon_SporeGun"),TEXT("DWWeapon_MoldBow"),TEXT("DWWeapon_Shotgun")})
    if(!Owned.Contains(FDWCombatItemRegistry::CanonicalWeaponItemId(FName(Id))))
    {Error=TEXT("Legacy starter ownership is incomplete.");return false;}
  Error.Reset();return true;
 }
}
