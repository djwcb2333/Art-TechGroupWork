#include "DWEnemyNumericTuning.h"

bool DWEnemyNumericPolicy::ValidateLegacyProjection(float ExpectedMax,float ExpectedSpeed,float SavedMax,float ActualMax,float ActualSpeed)
{return FMath::IsFinite(ExpectedMax)&&ExpectedMax>=1.f&&FMath::IsFinite(ExpectedSpeed)&&ExpectedSpeed>=0.f&&SavedMax==ExpectedMax&&ActualMax==ExpectedMax&&ActualSpeed==ExpectedSpeed;}

const FDWEnemyTypeNumericTuning* DWEnemyNumericPolicy::Select(const TArray<FDWEnemyTypeNumericTuning>& Types,const TArray<FString>& Ancestry,EDWEnemyCombatProfile Profile)
{
 // Nearest actual class first. A dedicated Sour row shadows generic Mold/Zombie rows even when disabled.
 for(const auto& ClassPath:Ancestry)
 {
  const FDWEnemyTypeNumericTuning* Any=nullptr;
  for(const auto& Type:Types)if(!Type.TargetClass.IsNull()&&Type.TargetClass.ToString()==ClassPath)
  {if(!Type.bAnyProfile&&Type.Profile==Profile)return &Type;if(Type.bAnyProfile)Any=&Type;}
  if(Any)return Any;
 }
 for(const auto& Type:Types)if(Type.TargetClass.IsNull()&&!Type.bAnyProfile&&Type.Profile==Profile)return &Type;
 return nullptr;
}
bool DWEnemyNumericPolicy::IsSupplement(EDWEnemyNumericField Field)
{return Field==EDWEnemyNumericField::PatrolRadius||Field==EDWEnemyNumericField::PatrolWaitSeconds||Field==EDWEnemyNumericField::DeathCleanupSeconds;}
bool DWEnemyNumericPolicy::ValidateValue(EDWEnemyNumericField Field,double Value)
{
 if(static_cast<uint8>(Field)>static_cast<uint8>(EDWEnemyNumericField::VariantWeight)||!FMath::IsFinite(Value)||Value<0.||Value>MAX_flt||(Value>0.&&static_cast<float>(Value)==0.f))return false;
 if((Field==EDWEnemyNumericField::MaxHealth||Field==EDWEnemyNumericField::Active||Field==EDWEnemyNumericField::Range||Field==EDWEnemyNumericField::Height||Field==EDWEnemyNumericField::Interval||Field==EDWEnemyNumericField::FullAngle)&&Value<=0.)return false;
 return (Field!=EDWEnemyNumericField::FullAngle||Value<=360.)&&(Field!=EDWEnemyNumericField::WindupLockFraction||Value<=1.);
}
bool DWEnemyNumericPolicy::LegacyBounds(EDWEnemyNumericField Field,double& Minimum,double& Maximum)
{
 using F=EDWEnemyNumericField;double Low=0.,High=0.;
 switch(Field)
 {
 case F::MaxHealth:Low=1.;High=1.e8;break;
 case F::Damage:High=1.e8;break;
 case F::Range:case F::AlertRadius:case F::DisengageDistance:Low=1.;High=1.e5;break;
 case F::MoveSpeed:case F::PatrolRadius:High=1.e5;break;
 case F::Windup:case F::Recovery:case F::AlertSeconds:High=60.;break;
 case F::Active:Low=.01;High=60.;break;
 case F::Interval:Low=.05;High=3600.;break;
 case F::FullAngle:Low=2.;High=360.;break;
 case F::PatrolWaitSeconds:High=3600.;break;
 case F::DeathCleanupSeconds:Low=.1;High=3600.;break;
 case F::VariantWeight:High=MAX_flt;break;
 default:return false;
 }
 Minimum=Low;Maximum=High;return true;
}
bool DWEnemyNumericPolicy::ValidateLegacyValue(EDWEnemyNumericField Field,double Value)
{
 double Low=0.,High=0.;
 return ValidateValue(Field,Value)&&LegacyBounds(Field,Low,High)&&Value>=Low&&Value<=High;
}
bool DWEnemyNumericPolicy::ValidateTypes(const TArray<FDWEnemyTypeNumericTuning>& Types,FString& Error)
{
 TSet<FName> Names;TSet<FString> Matches;
 for(const auto& Type:Types)
 {
  const FString Match=Type.TargetClass.ToString()+TEXT("/")+(Type.bAnyProfile?TEXT("any"):FString::FromInt(static_cast<uint8>(Type.Profile)));
  if(Type.TypeKey.IsNone()||Names.Contains(Type.TypeKey)||Matches.Contains(Match)||Type.Profile>EDWEnemyCombatProfile::DoughElite)
  {Error=TEXT("重复或无效的敌人种类配置 / Invalid enemy binding");return false;}
  Names.Add(Type.TypeKey);Matches.Add(Match);TSet<FString> Fields;
  for(const auto& Row:Type.Values)if(Row.bEnabled)
  {
   const FString Key=FString::FromInt(Row.VariantIndex)+TEXT("/")+FString::FromInt(static_cast<uint8>(Row.Field));
   if(Row.VariantIndex<INDEX_NONE||Fields.Contains(Key)||!ValidateValue(Row.Field,Row.Value))
   {Error=TEXT("重复或无效的敌人字段 / Invalid enemy numeric value");return false;}
   Fields.Add(Key);
  }
 }
 Error.Reset();return true;
}
bool DWEnemyNumericPolicy::ApplyFormal(const FDWEnemyTypeNumericTuning* Type,const FDWEnemyCombatConfig& Base,FDWEnemyCombatConfig& Out,FDWEnemyActorNumericSupplement& Extra,FString& Error)
{
 FDWEnemyCombatConfig C=Base;FDWEnemyActorNumericSupplement Pending;
 if(Type)for(const auto& Row:Type->Values)if(Row.bEnabled)
 {
  if(!ValidateValue(Row.Field,Row.Value)||Row.VariantIndex!=INDEX_NONE)
  {Error=TEXT("正式敌人不接入旧变招配置 / Formal enemy variant mismatch");return false;}
  if(IsSupplement(Row.Field)){Pending.Values.Add(Row.Field,Row.Value);continue;}
  switch(Row.Field)
  {
  case EDWEnemyNumericField::MaxHealth:C.Health=Row.Value;break;
  case EDWEnemyNumericField::MoveSpeed:C.MoveSpeedCmPerSecond=Row.Value;break;
  case EDWEnemyNumericField::Damage:C.Damage=Row.Value;break;
  case EDWEnemyNumericField::Windup:C.WindupSeconds=Row.Value;break;
  case EDWEnemyNumericField::Active:C.ActiveSeconds=Row.Value;break;
  case EDWEnemyNumericField::Recovery:C.RecoverySeconds=Row.Value;break;
  case EDWEnemyNumericField::ExtraCooldown:C.ExtraCooldownSeconds=Row.Value;break;
  case EDWEnemyNumericField::Range:C.RangeCm=Row.Value;break;
  case EDWEnemyNumericField::FullAngle:C.AttackFullAngleDegrees=Row.Value;break;
  case EDWEnemyNumericField::Height:C.AttackHeightCm=Row.Value;break;
  case EDWEnemyNumericField::IncomingKnockbackMultiplier:C.KnockbackMultiplier=Row.Value;break;
  case EDWEnemyNumericField::AttackKnockbackMultiplier:C.AttackKnockbackMultiplier=Row.Value;break;
  case EDWEnemyNumericField::OutgoingKnockback:C.OutgoingKnockbackCm=Row.Value;break;
  case EDWEnemyNumericField::OutgoingKnockbackSeconds:C.OutgoingKnockbackSeconds=Row.Value;break;
  case EDWEnemyNumericField::AlertRadius:C.AlertRadiusCm=Row.Value;break;
  case EDWEnemyNumericField::DisengageDistance:C.DisengageDistanceCm=Row.Value;break;
  case EDWEnemyNumericField::AlertSeconds:C.AlertDelaySeconds=Row.Value;break;
  case EDWEnemyNumericField::DisengageSeconds:C.DisengageSeconds=Row.Value;break;
  case EDWEnemyNumericField::WindupLockFraction:C.WindupLockFraction=Row.Value;break;
  case EDWEnemyNumericField::ReturnSpeedMultiplier:C.ReturnSpeedMultiplier=Row.Value;break;
  default:Error=TEXT("该字段不属于正式敌人数值 / Unsupported formal field");return false;
  }
 }
 if(Type&&!DWEnemyFormal::ValidateConfig(C,Error))return false;
 Out=C;Extra=MoveTemp(Pending);Error.Reset();return true;
}
