#include "DWLanguageItemTooltipFactory.h"
#include "DWGameplayWidget.h"
#include "DWGameplayHUD.h"
#include "DWPlayerCharacter.h"
#include "DWLanguageSubsystem.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.h"
#include "DWGatheringComponent.h"
#include "DWBuildingDefinition.h"
#include "DWCombatItemRegistry.h"
#include "DWGameInstance.h"
#include "DWCombatRules.h"
#include "DWCharacterA01Component.h"
#include "DWLocalizationLibrary.h"
#include "Engine/GameInstance.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SToolTip.h"
#include "Dom/JsonObject.h"
namespace DWLanguageItemTooltip
{
namespace
{
 FText Name(const UDWGameplayWidget* S,FName Id)
 {const auto* C=S?S->GetGameplayConfig():nullptr;const auto* D=C?C->GetItemDefinition(Id):nullptr;return D?UDWLocalizationLibrary::GetItemDisplayName(S,*D):FText::FromName(Id);}
 FText Join(const TArray<FString>& Lines){return FText::FromString(FString::Join(Lines,TEXT("\n")));}
 TSoftObjectPtr<UTexture2D> Icon(const UDWGameplayWidget* S,FName Id)
 {const auto* C=S?S->GetGameplayConfig():nullptr;const auto* D=C?C->GetItemDefinition(Id):nullptr;return D?TSoftObjectPtr<UTexture2D>(D->Icon.Get()):TSoftObjectPtr<UTexture2D>();}
 // Read-only projection of the immutable, already loaded formal rules. Never read
 // legacy ItemDef damage, source clip length, or guessed fallback gameplay values.
 void AppendWeaponAttributes(UDWGameplayWidget* S,FName ItemId,TArray<FText>& Out)
 {
  const auto* Binding=FDWCombatItemRegistry::FindItem(ItemId);if(!Binding)return;
  const auto* GI=S?S->GetGameInstance<UDWGameInstance>():nullptr;
  const auto Rules=GI?GI->GetCombatRules():TSharedPtr<const FDWCombatRules>();
  if(!Rules){Out.Add(DWText(S,TEXT("正式武器参数尚未加载。"),TEXT("Formal weapon parameters are not loaded.")));return;}
  const FString Prefix=TEXT("weapon.")+Binding->DesignId.ToString()+TEXT(".");const int32 Before=Out.Num();
  const auto Id=[&](const TCHAR* Field){return FName(*(Prefix+Field));};
  const auto Read=[&](const TCHAR* Field,EDWCombatRuleUnit Unit,double& Value)
  {FString Error;return Rules->TryGetNumber(Id(Field),Unit,Value,Error)&&FMath::IsFinite(Value);};
  const auto Suffix=[](EDWCombatRuleUnit Unit)
  {return FText::FromString(Unit==EDWCombatRuleUnit::Meters?TEXT(" m"):Unit==EDWCombatRuleUnit::Seconds?TEXT(" s"):Unit==EDWCombatRuleUnit::Fraction?TEXT("%"):TEXT(""));};
  const auto Number=[&](const TCHAR* Field,EDWCombatRuleUnit Unit,const TCHAR* Chinese,const TCHAR* English)
  {double Value=0.;if(Read(Field,Unit,Value))Out.Add(FText::Format(DWText(S,TEXT("{0}：{1}{2}"),TEXT("{0}: {1}{2}")),DWText(S,Chinese,English),FText::AsNumber(Unit==EDWCombatRuleUnit::Fraction?Value*100.:Value),Suffix(Unit)));};
  const auto Count=[&](const TCHAR* Field,const TCHAR* Chinese,const TCHAR* English)
  {int32 Value=0;FString Error;if(Rules->TryGetCount(Id(Field),Value,Error))Out.Add(FText::Format(DWText(S,TEXT("{0}：{1}"),TEXT("{0}: {1}")),DWText(S,Chinese,English),FText::AsNumber(Value)));};
  const auto Pair=[&](const TCHAR* A,const TCHAR* B,EDWCombatRuleUnit Unit,const TCHAR* Chinese,const TCHAR* English)
  {double X=0.,Y=0.;if(Read(A,Unit,X)&&Read(B,Unit,Y))Out.Add(FText::Format(DWText(S,TEXT("{0}：{1} / {2}{3}"),TEXT("{0}: {1} / {2}{3}")),DWText(S,Chinese,English),FText::AsNumber(X),FText::AsNumber(Y),Suffix(Unit)));};
  const auto MeleeTiming=[&]()
  {double W=0.,A=0.,R=0.;if(Read(TEXT("windup_s"),EDWCombatRuleUnit::Seconds,W)&&Read(TEXT("active_s"),EDWCombatRuleUnit::Seconds,A)&&Read(TEXT("recovery_s"),EDWCombatRuleUnit::Seconds,R))
   Out.Add(FText::Format(DWText(S,TEXT("前摇 / 判定 / 后摇：{0} / {1} / {2} s"),TEXT("Windup / Active / Recovery: {0} / {1} / {2} s")),FText::AsNumber(W),FText::AsNumber(A),FText::AsNumber(R)));};
  const FName Weapon=Binding->DesignId;
  if(Weapon==TEXT("sword"))Pair(TEXT("slam_damage"),TEXT("sweep_damage"),EDWCombatRuleUnit::Points,TEXT("下砸 / 横扫伤害"),TEXT("Slam / Sweep damage"));
  else if(Weapon==TEXT("shotgun"))Number(TEXT("pellet_damage"),EDWCombatRuleUnit::Points,TEXT("单枚弹丸伤害"),TEXT("Damage per pellet"));
  else if(Weapon==TEXT("alcohol"))Number(TEXT("impact_damage"),EDWCombatRuleUnit::Points,TEXT("直接命中伤害"),TEXT("Direct impact damage"));
  else Number(TEXT("damage"),EDWCombatRuleUnit::Points,TEXT("基础伤害"),TEXT("Base damage"));
  Number(TEXT("range_m"),EDWCombatRuleUnit::Meters,TEXT("基础判定距离 / 射程"),TEXT("Base gameplay reach / range"));
  if(Weapon==TEXT("spear"))
  {MeleeTiming();Number(TEXT("min_damage_fraction"),EDWCombatRuleUnit::Fraction,TEXT("边缘伤害最低比例"),TEXT("Minimum edge damage"));}
  else if(Weapon==TEXT("axe"))
  {MeleeTiming();Number(TEXT("block_reduction"),EDWCombatRuleUnit::Fraction,TEXT("格挡减伤"),TEXT("Block damage reduction"));}
  else if(Weapon==TEXT("sword"))
  {
   Number(TEXT("charge_s"),EDWCombatRuleUnit::Seconds,TEXT("完整蓄力"),TEXT("Full charge"));
   Pair(TEXT("slam_active_s"),TEXT("sweep_active_s"),EDWCombatRuleUnit::Seconds,TEXT("下砸 / 横扫判定时长"),TEXT("Slam / Sweep active time"));
   Number(TEXT("follow_gap_s"),EDWCombatRuleUnit::Seconds,TEXT("后续横扫间隔"),TEXT("Follow-up sweep gap"));
   Number(TEXT("recovery_s"),EDWCombatRuleUnit::Seconds,TEXT("后摇"),TEXT("Recovery"));
  }
  else if(Weapon==TEXT("dagger"))
  {
   double T[4]={};bool Complete=true;for(int32 I=0;I<4;++I)Complete=Read(*FString::Printf(TEXT("stage%d_duration_s"),I+1),EDWCombatRuleUnit::Seconds,T[I])&&Complete;
   if(Complete)Out.Add(FText::Format(DWText(S,TEXT("四段动作时长：{0} / {1} / {2} / {3} s"),TEXT("Four action segments: {0} / {1} / {2} / {3} s")),FText::AsNumber(T[0]),FText::AsNumber(T[1]),FText::AsNumber(T[2]),FText::AsNumber(T[3])));
  }
  else if(Weapon==TEXT("gun")||Weapon==TEXT("bow"))
  {
   Pair(TEXT("windup_s"),TEXT("recovery_s"),EDWCombatRuleUnit::Seconds,TEXT("前摇 / 后摇"),TEXT("Windup / Recovery"));
   Number(TEXT("interval_s"),EDWCombatRuleUnit::Seconds,TEXT("射击间隔"),TEXT("Shot interval"));
   Count(TEXT("ammo_per_shot"),TEXT("每次射击消耗弹药"),TEXT("Ammunition spent per shot"));
   if(!Binding->AmmoItemId.IsNone())Out.Add(FText::Format(DWText(S,TEXT("弹药：{0}"),TEXT("Ammunition: {0}")),Name(S,Binding->AmmoItemId)));
  }
  else if(Weapon==TEXT("shotgun"))
  {
   Count(TEXT("pellets"),TEXT("每发弹丸数"),TEXT("Pellets per shot"));
   Number(TEXT("full_damage_range_m"),EDWCombatRuleUnit::Meters,TEXT("满额伤害距离"),TEXT("Full-damage range"));
   Count(TEXT("magazine"),TEXT("弹匣容量（发）"),TEXT("Magazine capacity (rounds)"));
   Count(TEXT("ammo_per_shot"),TEXT("每次射击消耗（发）"),TEXT("Rounds spent per shot"));
   Number(TEXT("reload_one_s"),EDWCombatRuleUnit::Seconds,TEXT("装填一发"),TEXT("Reload one round"));
   if(!Binding->AmmoItemId.IsNone())Out.Add(FText::Format(DWText(S,TEXT("弹药：{0}"),TEXT("Ammunition: {0}")),Name(S,Binding->AmmoItemId)));
  }
  else if(Weapon==TEXT("alcohol"))
  {
   Number(TEXT("tick_damage"),EDWCombatRuleUnit::Points,TEXT("区域每跳伤害"),TEXT("Area damage per tick"));
   Number(TEXT("tick_interval_s"),EDWCombatRuleUnit::Seconds,TEXT("区域伤害间隔"),TEXT("Area tick interval"));
   Number(TEXT("area_radius_m"),EDWCombatRuleUnit::Meters,TEXT("区域半径"),TEXT("Area radius"));
   Number(TEXT("duration_s"),EDWCombatRuleUnit::Seconds,TEXT("区域持续时间"),TEXT("Area duration"));
   Count(TEXT("stock_per_throw"),TEXT("每次投掷消耗（瓶）"),TEXT("Bottles spent per throw"));
  }
  if(Out.Num()>Before)Out.Add(DWText(S,TEXT("正式基础参数；实际效果受形态、天赋与距离等影响。"),TEXT("Formal base parameters; form, talents and distance may affect the result.")));
 }
}
bool IsTooltipEnabled(const UDWGameplayWidget* S)
{
 const auto* H=IsValid(S)?S->GetDWHUD():nullptr;
 return H&&H->GameUIFont&&S->GetGameplayConfig(); // Read existing presentation data only.
}
bool IsLanguageSessionReady(const UDWGameplayWidget* S)
{const auto* GI=IsValid(S)?S->GetGameInstance():nullptr;const auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;return L&&L->IsReady();}
bool IsEnabled(const UDWGameplayWidget* S)
{return IsLanguageSessionReady(S);}
TSharedPtr<FDWLanguageUIContext> MakeContext(UDWGameplayWidget* S)
{
 auto* H=S?S->GetDWHUD():nullptr;if(!H||!H->GameUIFont)return nullptr;
 auto C=MakeShared<FDWLanguageUIContext>();C->Snapshot.bEnglish=UDWLocalizationLibrary::GetLanguage(S)==EDWGameLanguage::English;
 const TWeakObjectPtr<ADWGameplayHUD> WeakH(H);C->FontProvider=[WeakH](int32 Size)
 {return FSlateFontInfo(WeakH.IsValid()?WeakH->GameUIFont.Get():nullptr,Size);};return C;
}
FDWItemTooltipView Item(UDWGameplayWidget* S,const FDWItemStack& Stack)
{
 FDWItemTooltipView V;const auto* C=S?S->GetGameplayConfig():nullptr;const auto* Bag=S?S->GetInventory():nullptr;
 const auto* D=C?C->GetItemDefinition(Stack.ItemId):nullptr;
 if(!D||Stack.ItemId.IsNone()||Stack.Quantity<=0)return V;
 V.bOccupied=true;V.ItemId=Stack.ItemId;V.Name=Name(S,Stack.ItemId);V.Icon=Icon(S,Stack.ItemId);V.Quantity=Stack.Quantity;
 V.StackLimit=Bag?Bag->GetEffectiveStackLimit(Stack.ItemId):0;
 const auto Role=FDWCombatItemRegistry::GetItemRole(Stack.ItemId);
 if(Role==EDWCombatItemRole::PermanentWeapon)V.Use=DWText(S,TEXT("在背包中装备后使用对应武器攻击。"),TEXT("Equip from the inventory to use this weapon."));
 else if(Role==EDWCombatItemRole::ReserveAmmo)V.Use=DWText(S,TEXT("对应武器使用的备用弹药。"),TEXT("Reserve ammunition for its weapon."));
 else if(Role==EDWCombatItemRole::ThrowableStock)V.Use=DWText(S,TEXT("装备并投掷；已发出的投掷物按实际规则扣除库存。"),TEXT("Equip and throw; committed throws spend real inventory stock."));
 else if(Role==EDWCombatItemRole::MoldCore||Role==EDWCombatItemRole::YeastCore)V.Use=DWText(S,TEXT("使用后进入对应形态的精华。"),TEXT("An essence used to enter its corresponding form."));
 else if(C->GetBuildingDefinition(Stack.ItemId))V.Use=DWText(S,TEXT("在背包中使用，选择实际可用的位置放置。"),TEXT("Use from the inventory and choose a valid placement location."));
 else if(D->bUsable||(S->GetGameInstance()&&S->GetGameInstance()->GetSubsystem<UDWLanguageSubsystem>()&&S->GetGameInstance()->GetSubsystem<UDWLanguageSubsystem>()->IsRuntimeConsumable(Stack.ItemId)))V.Use=DWText(S,TEXT("可在背包或已解锁的快捷格使用。"),TEXT("Use from the inventory or an unlocked quick slot."));
 else V.Use=DWText(S,TEXT("制作或交换使用的物品；相关配方读取当前配置。"),TEXT("An item for crafting or exchange; related recipes use the current configuration."));
 const auto* Profile=UDWGameplayConfig::AsGatheringConfig(C);
 if(const auto* Tool=Profile?Profile->Tools.Find(Stack.ItemId):nullptr)
 {
  V.Use=DWText(S,TEXT("装备为采集工具。"),TEXT("Equip as a gathering tool."));
  const auto* Player=S->GetPlayer();const auto* Gathering=Player?Player->FindComponentByClass<UDWGatheringComponent>():nullptr;
  const float Cycle=Gathering?Gathering->GetEffectiveToolSwingSeconds(Stack.ItemId):0.f;
  if(Cycle>0.f)
  {
   const bool bHammer=Tool->ToolType==TEXT("Hammer"),bSpear=Tool->ToolType==TEXT("Spear");
   const FText Timing=bHammer?DWText(S,TEXT("拆除挥动周期"),TEXT("Demolition swing cycle")):
       (bSpear?DWText(S,TEXT("使用间隔"),TEXT("Use interval")):DWText(S,TEXT("挥动周期"),TEXT("Swing cycle")));
   V.Attributes.Add(FText::Format(DWText(S,TEXT("锋利度 {0} · {1} {2} 秒 · 距离 {3} cm"),TEXT("Sharpness {0} · {1} {2} s · Reach {3} cm")),FText::AsNumber(Tool->Sharpness),Timing,FText::AsNumber(Cycle),FText::AsNumber(Tool->ReachCm)));
  }
  else V.Attributes.Add(DWText(S,TEXT("采集周期配置不可用。"),TEXT("Gathering cycle is unavailable.")));
 }
 AppendWeaponAttributes(S,Stack.ItemId,V.Attributes);
 // These legacy use values are not the A01 survival rules. Never advertise them for the trial overlay.
 const auto* P=S->GetPlayer();const auto* A=P?P->FindComponentByClass<UDWCharacterA01Component>():nullptr;
 if(A&&A->IsEnabled()&&IsLanguageSessionReady(S))if(const auto* GI=S->GetGameInstance())if(const auto* L=GI->GetSubsystem<UDWLanguageSubsystem>())
 {
  const TSharedPtr<FJsonObject> Config=L->GetTrialConfig();const TSharedPtr<FJsonObject>* Uses=nullptr,*Use=nullptr;
  if(Config&&Config->TryGetObjectField(TEXT("runtime_consumables"),Uses)&&Uses&&(*Uses)->TryGetObjectField(Stack.ItemId.ToString(),Use)&&Use)
  {
   double Water=0.,Food=0.,Count=0.;
   if((*Use)->TryGetNumberField(TEXT("consume"),Count)&&FMath::IsFinite(Count)&&Count>=1.)
    V.Attributes.Add(FText::Format(DWText(S,TEXT("每次消耗 {0}"),TEXT("Consumed per use: {0}")),FText::AsNumber(Count)));
   if((*Use)->TryGetNumberField(TEXT("hydration"),Water)&&FMath::IsFinite(Water)&&Water>0.)
    V.Attributes.Add(FText::Format(DWText(S,TEXT("补充水分 {0}"),TEXT("Water restored: {0}")),FText::AsNumber(Water)));
   if((*Use)->TryGetNumberField(TEXT("satiety"),Food)&&FMath::IsFinite(Food)&&Food>0.)
    V.Attributes.Add(FText::Format(DWText(S,TEXT("补充饱腹 {0}"),TEXT("Food restored: {0}")),FText::AsNumber(Food)));
   FString Returned;double Amount=0.;
   if((*Use)->TryGetStringField(TEXT("return_item"),Returned)&&!Returned.IsEmpty()&&(*Use)->TryGetNumberField(TEXT("return_count"),Amount)&&FMath::IsFinite(Amount)&&Amount>0.)
    V.Attributes.Add(FText::Format(DWText(S,TEXT("使用后返还 {0} × {1}"),TEXT("Returned after use: {0} x {1}")),Name(S,FName(*Returned)),FText::AsNumber(Amount)));
  }
 }
 if(!A||!A->IsEnabled())
 {if(D->HealAmount>0.)V.Attributes.Add(FText::Format(DWText(S,TEXT("恢复生命 {0}"),TEXT("Health restored {0}")),FText::AsNumber(D->HealAmount)));
  if(D->TransformationGainPercent>0.)V.Attributes.Add(FText::Format(DWText(S,TEXT("形态资源 {0}%"),TEXT("Form resource {0}%")),FText::AsNumber(D->TransformationGainPercent)));}
 return V;
}
TArray<FDWCraftingConditionView> Conditions(UDWGameplayWidget* S,FName RecipeId)
{
 TArray<FDWCraftingConditionView> Out;const auto* C=S?S->GetGameplayConfig():nullptr;const auto* R=C?C->GetRecipeDefinition(RecipeId):nullptr;const auto* P=S?S->GetPlayer():nullptr;
 if(!C||!R)return Out;TSet<FName> StationIds;
 if(const auto* Profile=UDWGameplayConfig::AsGatheringConfig(C)){const FName Id=Profile->GetRequiredStation(RecipeId);if(!Id.IsNone())StationIds.Add(Id);}
 for(const auto& O:R->Outputs)if(const auto* B=C->GetBuildingDefinition(O.ItemId))if(!B->RequiredCraftingStation.IsNone())StationIds.Add(B->RequiredCraftingStation);
 if(StationIds.IsEmpty())
 {
  FDWCraftingConditionView V;V.Name=DWText(S,TEXT("无需工位 / 徒手制作"),TEXT("No station / Hand crafting"));V.Icon=S->LanguageHandcraftIcon;V.bSatisfied=true;
  V.Description=DWText(S,TEXT("仍需满足配方材料与产物空间要求。"),TEXT("Recipe materials and output capacity are still required."));Out.Add(MoveTemp(V));
 }
 TArray<FName> Sorted=StationIds.Array();Sorted.Sort(FNameLexicalLess());
 for(FName Id:Sorted)
 {
  FDWCraftingConditionView V;V.ConditionId=Id;V.Name=FText::FromName(Id);
  if(Id==TEXT("GatherStation_WaterPond"))
  {V.Name=DWText(S,TEXT("水源"),TEXT("Water source"));V.Icon=S->LanguageWaterSourceIcon;
   V.Description=DWText(S,TEXT("在真实供水交互载体或可用水塘的制作范围内。"),TEXT("Within crafting reach of a real usable water source or pond."));}
  else for(const auto& Ref:C->BuildingDefinitions)if(const auto* B=Ref.LoadSynchronous();B&&B->StationId==Id)
  {V.Name=Name(S,B->BuildingItemId);V.Icon=Icon(S,B->BuildingItemId);break;}
  V.bSatisfied=P&&P->GatheringComponent&&P->GatheringComponent->HasCraftingContext(RecipeId,Id,NAME_None);
  if(!V.bSatisfied)V.MissingReason=FText::Format(DWText(S,TEXT("缺少可用的 {0}：请移动到其真实交互范围内。"),TEXT("A usable {0} is missing. Move into its actual interaction range.")),V.Name);
  Out.Add(MoveTemp(V));
 }
 if(C->bRequireTransformationForCrafting&&R->bRequiresYeast)
 {
  FDWCraftingConditionView V;V.ConditionId=TEXT("RequiresYeast");V.Name=DWText(S,TEXT("酵母形态"),TEXT("Yeast form"));V.Icon=Icon(S,FDWCombatItemRegistry::YeastCoreId());V.bSatisfied=P&&P->IsYeastForm();
  if(!V.bSatisfied)V.MissingReason=DWText(S,TEXT("当前配方启用了酵母形态条件；需要先进入酵母形态。"),TEXT("This recipe requires Yeast form. Enter that form first."));Out.Add(MoveTemp(V));
 }
 return Out;
}
FDWItemTooltipView Recipe(UDWGameplayWidget* S,FName RecipeId)
{
 const auto* C=S?S->GetGameplayConfig():nullptr;const auto* R=C?C->GetRecipeDefinition(RecipeId):nullptr;const auto* Bag=S?S->GetInventory():nullptr;const auto* P=S?S->GetPlayer():nullptr;
 FDWItemTooltipView V;if(!R||R->Outputs.IsEmpty())return V;
 V=Item(S,R->Outputs[0]);V.Name=UDWLocalizationLibrary::GetRecipeDisplayName(S,*R);V.Conditions=Conditions(S,RecipeId);
 TArray<FString> Products,Missing;TMap<FName,int32> Required;
 for(const auto& O:R->Outputs)Products.Add(FText::Format(DWText(S,TEXT("产物：{0} × {1}"),TEXT("Output: {0} x {1}")),Name(S,O.ItemId),FText::AsNumber(O.Quantity)).ToString());
 V.Product=Join(Products);
 for(const auto& I:R->Inputs)Required.FindOrAdd(I.ItemId)+=I.Quantity;
 for(const auto& I:Required)
 {const int32 Have=Bag?Bag->CountItem(I.Key):0;
  V.Materials.Add(FText::Format(DWText(S,TEXT("材料：{0}  {1} / {2}"),TEXT("Material: {0}  {1} / {2}")),Name(S,I.Key),FText::AsNumber(Have),FText::AsNumber(I.Value)));
  if(Have<I.Value)Missing.Add(FText::Format(DWText(S,TEXT("{0} 还缺 {1}"),TEXT("{0}: {1} more needed")),Name(S,I.Key),FText::AsNumber(I.Value-Have)).ToString());}
 for(const auto& Condition:V.Conditions)if(!Condition.bSatisfied&&!Condition.MissingReason.IsEmpty())Missing.Add(Condition.MissingReason.ToString());
 const bool Can=Bag&&P&&Bag->CanCraft(RecipeId,P->IsYeastForm());
 if(!Can&&Missing.IsEmpty())Missing.Add(DWText(S,TEXT("当前制作检查未通过：请检查产物空间、永久装备唯一性与当前角色状态。"),TEXT("Crafting is currently blocked. Check output space, unique weapon ownership and the current character state.")).ToString());
 V.BlockedReason=Join(Missing);return V;
}
// A length-prefixed snapshot prevents false equality even when text contains separators.
static void Field(FString& Key,const FString& Value){Key+=FString::Printf(TEXT("%d:"),Value.Len());Key+=Value;}
static void ConditionKey(FString& K,const FDWCraftingConditionView& V)
{Field(K,V.ConditionId.ToString());Field(K,V.Icon.ToSoftObjectPath().ToString());Field(K,V.Name.ToString());Field(K,V.Description.ToString());Field(K,V.MissingReason.ToString());Field(K,V.bSatisfied?TEXT("1"):TEXT("0"));}
static FString ViewKey(const FDWItemTooltipView& V,const FText& EmptyText,int32 Locale)
{
 FString K;Field(K,FString::FromInt(Locale));Field(K,V.ItemId.ToString());Field(K,V.bOccupied?TEXT("1"):TEXT("0"));
 Field(K,V.Name.ToString());Field(K,V.Use.ToString());Field(K,FString::FromInt(V.Quantity));Field(K,FString::FromInt(V.StackLimit));
 Field(K,V.Icon.ToSoftObjectPath().ToString());Field(K,V.Product.ToString());Field(K,V.BlockedReason.ToString());Field(K,EmptyText.ToString());
 Field(K,FString::FromInt(V.Attributes.Num()));for(const auto& T:V.Attributes)Field(K,T.ToString());
 Field(K,FString::FromInt(V.Materials.Num()));for(const auto& T:V.Materials)Field(K,T.ToString());
 Field(K,FString::FromInt(V.Conditions.Num()));for(const auto& X:V.Conditions)ConditionKey(K,X);return K;
}
struct FTipCache
{
 TWeakObjectPtr<UDWGameplayWidget> Screen;TWeakPtr<SWidget> Widget;TWeakPtr<SToolTip> Tip;FString Key;bool bEmpty=false;
};
static TMap<TWeakObjectPtr<UWidget>,FTipCache> TooltipCache;
static void Attach(UWidget* Target,UDWGameplayWidget* S,const FDWItemTooltipView& V,const FText& EmptyText)
{
 if(!IsValid(Target)||!IsTooltipEnabled(S))return;
 static uint32 Calls=0;if((++Calls&255u)==0)for(auto It=TooltipCache.CreateIterator();It;++It)if(!It.Key().IsValid()||!It.Value().Widget.IsValid())It.RemoveCurrent();
 auto Widget=Target->TakeWidget();auto& Cache=TooltipCache.FindOrAdd(TWeakObjectPtr<UWidget>(Target));
 const FString Key=ViewKey(V,EmptyText,UDWLocalizationLibrary::GetRevision(S));
 const bool Empty=!V.bOccupied||(V.ItemId.IsNone()&&V.Conditions.IsEmpty());
 const auto OldTip=Cache.Tip.Pin();const bool SameWidget=Cache.Widget.Pin()==Widget&&Cache.Screen.Get()==S;
 const bool TipStillAttached=OldTip.IsValid()&&Widget->GetToolTip()==OldTip;
 if(SameWidget&&Cache.Key==Key&&((Empty&&Cache.bEmpty)||(!Empty&&TipStillAttached)))return;
 if(Empty)
 {
  Target->SetToolTipText(EmptyText);Cache.Tip.Reset();Cache.bEmpty=true;
 }
 else
 {
  auto C=MakeContext(S);if(!C)return;
  if(SameWidget&&TipStillAttached)DWItemTooltip::Update(OldTip.ToSharedRef(),C.ToSharedRef(),V);
  else {Target->SetToolTipText(FText::GetEmpty());auto Tip=DWItemTooltip::Make(C.ToSharedRef(),V);Widget->SetToolTip(Tip);Cache.Tip=Tip;}
  Cache.bEmpty=false;
 }
 Cache.Screen=S;Cache.Widget=Widget;Cache.Key=Key;
}
void AttachItem(UWidget* Target,UDWGameplayWidget* S,const FDWItemStack& Stack,const FText& EmptyText)
{if(!IsValid(Target))return;if(!IsTooltipEnabled(S)){const bool HadCache=TooltipCache.Remove(TWeakObjectPtr<UWidget>(Target))>0;if(HadCache||!Target->GetToolTipText().EqualTo(EmptyText))Target->SetToolTipText(EmptyText);return;}Attach(Target,S,Item(S,Stack),EmptyText);}
void AttachRecipe(UWidget* Target,UDWGameplayWidget* S,FName Id)
{if(!IsValid(Target))return;if(!IsTooltipEnabled(S)){if(TooltipCache.Remove(TWeakObjectPtr<UWidget>(Target)))Target->SetToolTipText(FText::GetEmpty());return;}Attach(Target,S,Recipe(S,Id),FText::GetEmpty());}
}
void UDWLanguageRecipeNeedsWidget::SetRecipeContext(UDWGameplayWidget* S,FName Id){OwnerScreen=S;CurrentRecipe=Id;RefreshNeeds();}
TSharedRef<SWidget> UDWLanguageRecipeNeedsWidget::RebuildWidget(){SAssignNew(Host,SBox);bNeedsSignatureValid=false;RefreshNeeds();return Host.ToSharedRef();}
void UDWLanguageRecipeNeedsWidget::RefreshNeeds()
{
 if(!Host)return;auto* S=OwnerScreen.Get();auto C=DWLanguageItemTooltip::MakeContext(S);
 if(!C.IsValid()){if(!bNeedsSignatureValid||!LastNeedsSignature.IsEmpty())Host->SetContent(SNullWidget::NullWidget);LastNeedsSignature.Reset();bNeedsSignatureValid=true;return;}
 const auto Conditions=DWLanguageItemTooltip::Conditions(S,CurrentRecipe);FString Key;
 DWLanguageItemTooltip::Field(Key,CurrentRecipe.ToString());DWLanguageItemTooltip::Field(Key,FString::FromInt(UDWLocalizationLibrary::GetRevision(S)));
 DWLanguageItemTooltip::Field(Key,FString::Printf(TEXT("%p"),S));
 for(const auto& Condition:Conditions)DWLanguageItemTooltip::ConditionKey(Key,Condition);
 if(bNeedsSignatureValid&&Key==LastNeedsSignature)return;
 Host->SetContent(DWItemTooltip::Needs(C.ToSharedRef(),Conditions));LastNeedsSignature=MoveTemp(Key);bNeedsSignatureValid=true;
}
void UDWLanguageRecipeNeedsWidget::ReleaseSlateResources(bool Children){Host.Reset();LastNeedsSignature.Reset();bNeedsSignatureValid=false;Super::ReleaseSlateResources(Children);}
