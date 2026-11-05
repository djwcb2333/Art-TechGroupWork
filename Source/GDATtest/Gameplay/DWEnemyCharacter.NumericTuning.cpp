#include "DWEnemyCharacter.h"
#include "DWOpeningPrototype.h"
#include "DWEnemyNumericTuning.h"
#include "DWPlayerCombatTuningSubsystem.h"
#include "Engine/GameInstance.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Animation/AnimationAsset.h"

namespace DWEnemyNumericSource
{
static bool Ready(const ADWEnemyCharacter& Enemy,FString& Error)
{
 auto* GI=Enemy.GetGameInstance();const auto* Settings=GI?GI->GetSubsystem<UDWPlayerCombatTuningSubsystem>():nullptr;
 if(Settings&&!Settings->IsConfigurationValid()){Error=Settings->GetConfigurationError();return false;}return true;
}
static const FDWEnemyNumbers* Resolve(const ADWEnemyCharacter& Enemy,FString& Error)
{
 auto* GI=Enemy.GetGameInstance();const auto* Settings=GI?GI->GetSubsystem<UDWPlayerCombatTuningSubsystem>():nullptr;
 const FDWEnemyNumbers* Selected=nullptr;
 if(!Enemy.ResolveNumericSource(Settings?Settings->FindEnemyType(Enemy.GetClass(),Enemy.CombatProfile):nullptr,Selected,Error))return nullptr;
 return Selected;
}
static const FDWEnemyNumber* Find(const FDWEnemyNumbers* Type,EDWEnemyNumericField Field)
{return Type?Type->Values.FindByPredicate([&](const auto& Row){return Row.VariantIndex==INDEX_NONE&&Row.Field==Field;}):nullptr;}
}

bool ADWEnemyCharacter::PrepareOpeningNumericSource(ADWOpeningPrototype& Director,FString& Error)
{
 if(HasAnyFlags(RF_ClassDefaultObject|RF_ArchetypeObject)||HasActorBegunPlay()||!IsValid(&Director)||Director.IsActorBeingDestroyed()||!GetWorld()||Director.GetWorld()!=GetWorld()||CombatProfile!=EDWEnemyCombatProfile::Legacy||CreatureDefinition||OpeningNumericSource)
 {Error=TEXT("教学数值只能由同场景导演在实例BeginPlay前绑定 / Invalid opening source preparation");return false;}
 auto* GI=GetGameInstance();const auto* Provider=GI?GI->GetSubsystem<UDWPlayerCombatTuningSubsystem>():nullptr;
 if(!Provider||!Provider->IsConfigurationValid()){Error=Provider?Provider->GetConfigurationError():TEXT("教学绑定缺少有效统一数值源 / Missing numeric provider");return false;}
 TSharedPtr<FDWEnemyNumbers> Source=MakeShared<FDWEnemyNumbers>();Source->TypeKey=TEXT("OpeningLegacy");Source->DisplayName=FText::FromString(TEXT("教学导演Legacy实例"));Source->TargetClass=FSoftClassPath(GetClass()->GetPathName());Source->Profile=EDWEnemyCombatProfile::Legacy;
 struct FField{EDWEnemyNumericField Field;double Value;};
 const FField Fields[]={
 {EDWEnemyNumericField::MaxHealth,MaxHealth},{EDWEnemyNumericField::MoveSpeed,MoveSpeed},{EDWEnemyNumericField::Damage,AttackDamage},{EDWEnemyNumericField::Range,AttackRange},
 {EDWEnemyNumericField::Windup,AttackWindup},{EDWEnemyNumericField::Active,AttackActiveDuration},{EDWEnemyNumericField::Recovery,AttackRecovery},{EDWEnemyNumericField::Interval,AttackInterval},
 {EDWEnemyNumericField::FullAngle,AttackHalfAngle*2.},{EDWEnemyNumericField::AlertRadius,DetectionRadius},{EDWEnemyNumericField::DisengageDistance,LoseTargetRadius},{EDWEnemyNumericField::AlertSeconds,AlertDuration},
 {EDWEnemyNumericField::PatrolRadius,PatrolRadius},{EDWEnemyNumericField::PatrolWaitSeconds,PatrolWaitTime},{EDWEnemyNumericField::DeathCleanupSeconds,DeathDestroyDelay}};
 for(const auto& F:Fields){auto& R=Source->Values.AddDefaulted_GetRef();R.Field=F.Field;R.Value=F.Value;}
 for(int32 I=0;I<AttackVariants.Num();++I)
 {
  const auto& V=AttackVariants[I];const FField VariantFields[]={{EDWEnemyNumericField::Damage,V.Damage},{EDWEnemyNumericField::Range,V.Range},{EDWEnemyNumericField::Windup,V.Windup},{EDWEnemyNumericField::Active,V.Active},{EDWEnemyNumericField::Recovery,V.Recovery},{EDWEnemyNumericField::Interval,V.Interval},{EDWEnemyNumericField::VariantWeight,V.SelectionWeight}};
  for(const auto& F:VariantFields){auto& R=Source->Values.AddDefaulted_GetRef();R.Field=F.Field;R.Value=F.Value;R.VariantIndex=I;R.VariantAnimation=FSoftObjectPath(V.Animation.Get());}
 }
 const FDWEnemyNumbers* Selected=nullptr;
 if(!FDWCombatNumericProvider::ResolveEnemySource(Provider->FindEnemyType(GetClass(),CombatProfile),CombatProfile,Source.Get(),Selected,Error))return false;
 OpeningNumericOwner=&Director;OpeningNumericSource=MoveTemp(Source);Error.Reset();return true;
}
bool ADWEnemyCharacter::ResolveNumericSource(const FDWEnemyNumbers* ClassNumbers,const FDWEnemyNumbers*& Out,FString& Error)const
{
 if(OpeningNumericSource)
 {
  const auto* Director=OpeningNumericOwner.Get();
  if(!IsValid(Director)||Director->IsActorBeingDestroyed()||Director->GetWorld()!=GetWorld()||CombatProfile!=EDWEnemyCombatProfile::Legacy||CreatureDefinition)
  {Error=TEXT("教学数值绑定的导演、场景或角色身份已失效 / Opening numeric owner changed");return false;}
 }
 return FDWCombatNumericProvider::ResolveEnemySource(ClassNumbers,CombatProfile,OpeningNumericSource.Get(),Out,Error);
}
bool ADWEnemyCharacter::ProjectFormalNumericTuning(FDWEnemyCombatConfig& Config,FDWEnemyActorNumericSupplement& Extra,FString& Error)const
{
 if(!DWEnemyNumericSource::Ready(*this,Error))return false;
 FDWEnemyCombatConfig Projected;
 const auto* Numbers=DWEnemyNumericSource::Resolve(*this,Error);
 if(!Numbers){Error=TEXT("敌人缺少统一数值种类 / Missing enemy numeric identity");return false;}
 if(Numbers->Profile!=CombatProfile){Error=TEXT("角色数值类型与当前战斗类型不一致 / Numeric profile mismatch");return false;}
 if(!FDWCombatNumericProvider::ProjectEnemy(*Numbers,Projected,Extra,Error))return false;
 Config=Projected;return true;
}
void ADWEnemyCharacter::AdoptNumericSupplement(const FDWEnemyActorNumericSupplement& Extra)
{
 for(const auto& Pair:Extra.Values)switch(Pair.Key)
 {
 case EDWEnemyNumericField::PatrolRadius:PatrolRadius=static_cast<float>(Pair.Value);break;
 case EDWEnemyNumericField::PatrolWaitSeconds:PatrolWaitTime=static_cast<float>(Pair.Value);break;
 case EDWEnemyNumericField::DeathCleanupSeconds:DeathDestroyDelay=static_cast<float>(Pair.Value);break;
 default:checkNoEntry();break;
 }
}
bool ADWEnemyCharacter::ApplyLegacyNumericTuning(FString& Error)
{
 if(!DWEnemyNumericSource::Ready(*this,Error))return false;
 const auto* Type=DWEnemyNumericSource::Resolve(*this,Error);
 if(!Type){Error=TEXT("角色缺少统一数值种类 / Missing character numeric source");return false;}
 if(CombatProfile!=EDWEnemyCombatProfile::Legacy){Error=TEXT("旧版角色投影类型冲突 / Legacy numeric profile mismatch");return false;}
 const bool Neutral=CreatureDefinition&&CreatureDefinition->Disposition==EDWCreatureDisposition::Neutral;
 if(Type->bNeutral!=Neutral){Error=TEXT("中立角色数值来源身份不一致 / Neutral numeric identity mismatch");return false;}
 struct FBinding{EDWEnemyNumericField Field;float* Address;double Scale=1.;};
 const FBinding Bindings[]={
 {EDWEnemyNumericField::MaxHealth,&MaxHealth},{EDWEnemyNumericField::MoveSpeed,&MoveSpeed},
 {EDWEnemyNumericField::Damage,&AttackDamage},{EDWEnemyNumericField::Range,&AttackRange},
 {EDWEnemyNumericField::Windup,&AttackWindup},{EDWEnemyNumericField::Active,&AttackActiveDuration},
 {EDWEnemyNumericField::Recovery,&AttackRecovery},{EDWEnemyNumericField::Interval,&AttackInterval},
 {EDWEnemyNumericField::FullAngle,&AttackHalfAngle,2.},{EDWEnemyNumericField::AlertRadius,&DetectionRadius},
 {EDWEnemyNumericField::DisengageDistance,&LoseTargetRadius},{EDWEnemyNumericField::AlertSeconds,&AlertDuration},
 {EDWEnemyNumericField::PatrolRadius,&PatrolRadius},{EDWEnemyNumericField::PatrolWaitSeconds,&PatrolWaitTime},
 {EDWEnemyNumericField::DeathCleanupSeconds,&DeathDestroyDelay}};
 TMap<EDWEnemyNumericField,double> Changes;auto Variants=AttackVariants;bool ChangedVariants=false;
 for(const auto& Row:Type->Values)
 {
  if(!DWEnemyNumericPolicy::ValidateLegacyValue(Row.Field,Row.Value)){Error=TEXT("旧敌人数值超出原运行范围；未夹限或采用任何覆盖 / Legacy value outside existing runtime bounds");return false;}
  if(Row.VariantIndex==INDEX_NONE)
  {
   bool Found=false;for(const auto& Binding:Bindings)if(Binding.Field==Row.Field){Found=true;break;}
   if(!Found){Error=TEXT("旧敌人未实现此独立数值 / Unsupported Legacy numeric field");return false;}
   Changes.Add(Row.Field,Row.Value);continue;
  }
  if(!Variants.IsValidIndex(Row.VariantIndex)||FSoftObjectPath(Variants[Row.VariantIndex].Animation.Get())!=Row.VariantAnimation)
  {Error=TEXT("变招索引或动作源已改变，请刷新编辑器源 / Variant identity changed");return false;}
  auto& Variant=Variants[Row.VariantIndex];ChangedVariants=true;
  switch(Row.Field)
  {
  case EDWEnemyNumericField::Damage:Variant.Damage=static_cast<float>(Row.Value);break;
  case EDWEnemyNumericField::Range:Variant.Range=static_cast<float>(Row.Value);break;
  case EDWEnemyNumericField::Windup:Variant.Windup=static_cast<float>(Row.Value);break;
  case EDWEnemyNumericField::Active:Variant.Active=static_cast<float>(Row.Value);break;
  case EDWEnemyNumericField::Recovery:Variant.Recovery=static_cast<float>(Row.Value);break;
  case EDWEnemyNumericField::Interval:Variant.Interval=static_cast<float>(Row.Value);break;
  case EDWEnemyNumericField::VariantWeight:Variant.SelectionWeight=static_cast<float>(Row.Value);break;
  default:Error=TEXT("变招不支持此字段 / Unsupported variant field");return false;
  }
 }
 if(const auto* NewMax=Changes.Find(EDWEnemyNumericField::MaxHealth);NewMax&&bHealthRestoredFromSave&&Health>*NewMax)
 {Error=TEXT("已保存生命不符合当前上限；没有截断或修改存档 / Saved health exceeds configured cap");return false;}
 // All validation above is side-effect free; adopt only numeric projections, never a hit/clock/death.
 for(const auto& Binding:Bindings)if(const auto* Value=Changes.Find(Binding.Field))*Binding.Address=static_cast<float>(*Value/Binding.Scale);
 if(ChangedVariants)AttackVariants=MoveTemp(Variants);
 if(Changes.Contains(EDWEnemyNumericField::MoveSpeed))GetCharacterMovement()->MaxWalkSpeed=MoveSpeed;
 Error.Reset();return true;
}
bool ADWEnemyCharacter::ValidateCreatureNumericSource(float SavedMax,FString& Error)const
{
 if(!DWEnemyNumericSource::Ready(*this,Error))return false;
 const auto* Defaults=GetClass()->GetDefaultObject<ADWEnemyCharacter>();
 if(!Defaults){Error=TEXT("Missing creature class defaults");return false;}
 const auto* Type=DWEnemyNumericSource::Resolve(*this,Error);
 if(!Type)return false;
 const auto* HealthOverride=DWEnemyNumericSource::Find(Type,EDWEnemyNumericField::MaxHealth);
 const auto* SpeedOverride=DWEnemyNumericSource::Find(Type,EDWEnemyNumericField::MoveSpeed);
 if(!HealthOverride&&!SpeedOverride)
  return DWCreaturePersistencePolicy::ValidateSource(CreatureDefinition.Get(),Defaults->CreatureDefinition.Get(),CombatProfile,SavedMax,MaxHealth,MoveSpeed,Error);
 if(CreatureDefinition!=Defaults->CreatureDefinition)
 {Error=TEXT("CreatureDefinition identity still must match the saved class source");return false;}
 const auto* Definition=Defaults->CreatureDefinition.Get();
 if(Definition&&Definition->NumericSource==EDWCreatureNumericSource::DefinitionLegacyHost)
 {
  // Preserve original identity/domain validation, then check the explicit effective numeric projection.
  if(!DWCreaturePersistencePolicy::ValidateSource(CreatureDefinition.Get(),Definition,CombatProfile,Definition->MaxHealth,Definition->MaxHealth,Definition->MoveSpeed,Error))return false;
  const float ExpectedMax=HealthOverride?static_cast<float>(HealthOverride->Value):Definition->MaxHealth;
  const float ExpectedSpeed=SpeedOverride?static_cast<float>(SpeedOverride->Value):Definition->MoveSpeed;
  if(!DWEnemyNumericPolicy::ValidateLegacyProjection(ExpectedMax,ExpectedSpeed,SavedMax,MaxHealth,MoveSpeed))
  {Error=TEXT("Saved/live creature values differ from the authorized effective source");return false;}
 }
 else
 {
  if(!DWCreaturePersistencePolicy::ValidateSource(CreatureDefinition.Get(),Definition,CombatProfile,SavedMax,MaxHealth,MoveSpeed,Error))return false;
  if((HealthOverride&&(SavedMax!=static_cast<float>(HealthOverride->Value)||MaxHealth!=static_cast<float>(HealthOverride->Value)))||
     (SpeedOverride&&MoveSpeed!=static_cast<float>(SpeedOverride->Value)))
  {Error=TEXT("Saved/live creature overrides differ from the current configured source");return false;}
 }
 Error.Reset();return true;
}
