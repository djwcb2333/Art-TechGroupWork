#include "DWCombatNumericProvider.h"
#include "DWPlayerCombatTuningSubsystem.h"
#include "DWPlayerCombatTuningPolicy.h"
#include "DWCombatRules.h"
#include "DWCombatSpear.h"
#include "DWCombatSword.h"
#include "DWCombatShieldAxe.h"
#include "DWCombatDagger.h"
#include "DWCombatSporeGun.h"
#include "DWCombatBow.h"
#include "DWCombatShotgun.h"
#include "DWCombatBottle.h"
#include "DWCombatUnarmed.h"
#include "DWCombatMoldRoot.h"
namespace DWNumericSource
{
static bool Read(const TArray<FDWCombatNumber>& Rows,bool Formal,TMap<FName,double>& Out,FString& Error)
{
 TMap<FName,double> Values;
 for(const auto& Row:Rows)
 {
  if(!(Formal?DWPlayerCombatTuningPolicy::RuleAllowed(Row.ParameterId):DWPlayerCombatTuningPolicy::A01Allowed(Row.ParameterId))||!FMath::IsFinite(Row.Value)||Row.Value<0.||Values.Contains(Row.ParameterId)||(!Formal&&!DWPlayerCombatTuningPolicy::A01ValueAllowed(Row.ParameterId,Row.Value)))
  {Error=TEXT("统一数值有重复、缺失或非法项 / Invalid complete numeric source");return false;}
  Values.Add(Row.ParameterId,Row.Value);
 }
 if(Values.Num()!=(Formal?147:17)){Error=TEXT("统一数值必须完整，不能回退到逐项继承 / Incomplete numeric source");return false;}
 Out=MoveTemp(Values);return true;
}
static const FDWEnemyNumber* Field(const FDWEnemyNumbers& Type,EDWEnemyNumericField F,int32 Variant=INDEX_NONE)
{return Type.Values.FindByPredicate([&](const auto& R){return R.Field==F&&R.VariantIndex==Variant;});}
static bool ValidateEnemy(const FDWEnemyNumbers& Type,FString& Error)
{
 if(Type.bNeutral&&Type.Profile!=EDWEnemyCombatProfile::Legacy){Error=TEXT("中立角色类型与原来源不一致 / Neutral source profile mismatch");return false;}
 TSet<FString> Seen;
 for(const auto& Row:Type.Values)
 {
  const auto Key=FString::FromInt(Row.VariantIndex)+TEXT("/")+FString::FromInt(static_cast<uint8>(Row.Field));
  if(Seen.Contains(Key)||Row.VariantIndex<INDEX_NONE||!(Type.Profile==EDWEnemyCombatProfile::Legacy?DWEnemyNumericPolicy::ValidateLegacyValue(Row.Field,Row.Value):DWEnemyNumericPolicy::ValidateValue(Row.Field,Row.Value)))
  {Error=TEXT("敌人完整数值重复或超界 / Invalid complete enemy source");return false;}
  Seen.Add(Key);
 }
 if(Type.Profile!=EDWEnemyCombatProfile::Legacy){FDWEnemyCombatConfig C;FDWEnemyActorNumericSupplement X;return FDWCombatNumericProvider::ProjectEnemy(Type,C,X,Error);}
 for(auto F:{EDWEnemyNumericField::MaxHealth,EDWEnemyNumericField::MoveSpeed,EDWEnemyNumericField::Damage,EDWEnemyNumericField::Range,EDWEnemyNumericField::Windup,EDWEnemyNumericField::Active,EDWEnemyNumericField::Recovery,EDWEnemyNumericField::Interval,EDWEnemyNumericField::FullAngle,EDWEnemyNumericField::AlertRadius,EDWEnemyNumericField::DisengageDistance,EDWEnemyNumericField::AlertSeconds,EDWEnemyNumericField::PatrolRadius,EDWEnemyNumericField::PatrolWaitSeconds,EDWEnemyNumericField::DeathCleanupSeconds})
  if(!Field(Type,F)){Error=TEXT("旧敌人完整字段缺失 / Incomplete Legacy source");return false;}
 TSet<int32> Variants;for(const auto& R:Type.Values)if(R.VariantIndex!=INDEX_NONE)Variants.Add(R.VariantIndex);
 if(Type.Values.Num()!=15+Variants.Num()*7){Error=TEXT("旧角色存在不支持或缺失字段 / Incomplete Legacy field set");return false;}
 for(int32 I:Variants)
 {
  if(I>=Variants.Num()){Error=TEXT("变招索引不连续 / Invalid variant identity");return false;}
  const FSoftObjectPath* Animation=nullptr;
  for(auto F:{EDWEnemyNumericField::Damage,EDWEnemyNumericField::Range,EDWEnemyNumericField::Windup,EDWEnemyNumericField::Active,EDWEnemyNumericField::Recovery,EDWEnemyNumericField::Interval,EDWEnemyNumericField::VariantWeight})
  {const auto* R=Field(Type,F,I);if(!R||R->VariantAnimation.IsNull()||(Animation&&*Animation!=R->VariantAnimation)){Error=TEXT("变招字段或动作身份缺失 / Incomplete variant source");return false;}Animation=&R->VariantAnimation;}
 }
 return true;
}
}
bool DWCombatNumericPolicy::ValidateSimple(const FDWSimpleSpearNumbers& N)
{
 for(double V:{N.Damage,N.IntervalSeconds,N.RangeCm,N.KnockbackCm,N.KnockbackSeconds})if(!FMath::IsFinite(V)||V<0.||V>MAX_flt||(V>0.&&static_cast<float>(V)==0.f))return false;
 return N.IntervalSeconds>0.&&N.RangeCm>0.&&N.KnockbackSeconds>0.;
}
bool DWCombatNumericPolicy::CoordinateTiming(TArray<FDWCombatNumber>& Numbers,FName Edited,double PreviousValue,FString& Notice)
{
 auto Fail=[&](){Notice=TEXT("无法按此次编辑意图联动阶段，请检查原周期、阶段比例及数值范围；未采用任何调整。");return false;};
 const TCHAR* Prefixes[]={TEXT("weapon.spear."),TEXT("weapon.axe."),TEXT("weapon.dagger."),TEXT("unarmed."),TEXT("weapon.gun."),TEXT("weapon.bow."),TEXT("weapon.shotgun."),TEXT("weapon.alcohol.")};
 for(int32 I=0;I<UE_ARRAY_COUNT(Prefixes);++I)
 {
  const FString Prefix=Prefixes[I],Id=Edited.ToString();
  const FName PeriodId(*(Prefix+TEXT("interval_s")));
  const bool Period=Edited==PeriodId,Phase=Id==Prefix+TEXT("windup_s")||Id==Prefix+TEXT("recovery_s")||(I<4&&Id==Prefix+TEXT("active_s"));
  if(!Period&&!Phase)continue;
  auto Pending=Numbers;
  auto Find=[&](const FString& Key){return Pending.FindByPredicate([&](const auto& R){return R.ParameterId==FName(*Key);});};
  auto* Cycle=Find(Prefix+TEXT("interval_s"));TArray<FDWCombatNumber*> Stages;
  for(const TCHAR* S:{TEXT("windup_s"),TEXT("active_s"),TEXT("recovery_s")})if(I<4||FString(S)!=TEXT("active_s")){auto* R=Find(Prefix+S);if(!R)return Fail();Stages.Add(R);}
  if(!Cycle||!FMath::IsFinite(Cycle->Value)||Cycle->Value<=0.)return Fail();
  double Sum=0.;for(const auto* R:Stages){if(!FMath::IsFinite(R->Value)||R->Value<0.)return Fail();Sum+=R->Value;}
  if(!FMath::IsFinite(Sum)||Sum<=0.)return Fail();
  if(Period)
  {
   // Caller passes the new period but retains old phase proportions. Their sum is mapped to
   // that requested cycle. Existing gap, if any, is supplied by caller via the old-period ratio.
   if(!FMath::IsFinite(PreviousValue)||PreviousValue<=0.||Sum>PreviousValue+1.e-8)return Fail();
   const double Scale=Cycle->Value/PreviousValue;for(auto* R:Stages)R->Value*=Scale;
   Notice=TEXT("已按原阶段比例联动时长，保持本次填写的攻击周期。");
  }
  else if(Sum>Cycle->Value){Cycle->Value=Sum;Notice=TEXT("保留阶段时长；完整攻击周期已增加至阶段总长。");}
  Numbers=MoveTemp(Pending);return true;
 }
 Notice.Reset();return true;
}
bool FDWCombatNumericProvider::Build(const UDWPlayerCombatTuningSettings& S,const TSharedPtr<const FDWCombatRules>& Base,TSharedPtr<const FDWCombatNumericProvider>& Out,FString& Error)
{
 if(S.NumericSchema!=1||!Base){Error=TEXT("战斗数值需先完成保值迁移 / Numeric migration required");return false;}
 TMap<FName,double> Values,AbilityValues;if(!DWNumericSource::Read(S.PlayerNumbers,true,Values,Error)||!DWNumericSource::Read(S.AbilityNumbers,false,AbilityValues,Error))return false;
 if(!DWCombatNumericPolicy::ValidateSimple(S.SimpleSpearNumbers)){Error=TEXT("简易糖矛数值无效：周期、范围、击退时长须大于零，数值须可表达 / Invalid simple spear source");return false;}
 TSharedPtr<const FDWCombatRules> Candidate;
 if(!FDWCombatRules::FromCompletePlayerValues(Base,Values,Candidate,Error))return false;
 FDWSpearTuning Spear;FDWSwordTuning Sword;FDWShieldAxeTuning Axe;FDWDaggerTuning Dagger;
 FDWSporeGunConfig Spore;FDWBowConfig Bow;FDWShotgunConfig Shotgun;FDWBottleConfig Bottle;FDWCombatUnarmedTuning Unarmed;FDWMoldRootConfig Root;
 if(!FDWSpearTuning::Build(*Candidate,Spear,Error)||!FDWSwordTuning::Build(*Candidate,Sword,Error)||
    !FDWShieldAxeTuning::Build(*Candidate,Axe,Error)||!FDWDaggerTuning::Build(*Candidate,Dagger,Error)||
    !FDWSporeGunConfig::Build(*Candidate,Spore,Error)||!FDWBowConfig::Build(*Candidate,Bow,Error)||
    !FDWShotgunConfig::Build(*Candidate,Shotgun,Error)||!FDWBottleConfig::Build(*Candidate,Bottle,Error)||
    !FDWCombatUnarmedTuning::Build(*Candidate,Unarmed,Error)||!FDWMoldRootConfig::Build(*Candidate,Root,Error))return false;

 TSet<FName> Keys;TSet<FString> Bindings;
 for(const auto& Type:S.EnemyNumbers)
 {
  const FString Binding=Type.TargetClass.ToString()+TEXT("/")+(Type.bAnyProfile?TEXT("any"):FString::FromInt(static_cast<uint8>(Type.Profile)));
  if(Type.TypeKey.IsNone()||Type.TargetClass.IsNull()||Keys.Contains(Type.TypeKey)||Bindings.Contains(Binding)||Type.Profile>EDWEnemyCombatProfile::DoughElite||!DWNumericSource::ValidateEnemy(Type,Error)){if(Error.IsEmpty())Error=TEXT("敌人种类绑定重复或无效 / Invalid enemy identity");return false;}
  Keys.Add(Type.TypeKey);Bindings.Add(Binding);
 }
 if(Keys.Num()<11){Error=TEXT("完整角色种类表缺失，须包含九组敌人与两类中立NPC / Incomplete character catalog");return false;}
 auto Result=MakeShared<FDWCombatNumericProvider>();Result->Rules=Candidate;Result->Abilities=MoveTemp(AbilityValues);Result->Enemies=S.EnemyNumbers;Result->Simple=S.SimpleSpearNumbers;
 Out=Result;Error.Reset();return true;
}
bool FDWCombatNumericProvider::ProjectAbilities(TSharedPtr<FJsonObject>& InOut,FString& Error)const
{return DWPlayerCombatTuningPolicy::ApplyA01Overrides(InOut,Abilities,Error);}
const FDWEnemyNumbers* FDWCombatNumericProvider::FindEnemy(const TArray<FString>& Ancestry,EDWEnemyCombatProfile Profile)const
{
 for(const auto& ClassPath:Ancestry)
 {const FDWEnemyNumbers* Any=nullptr;for(const auto& T:Enemies)if(T.TargetClass.ToString()==ClassPath){if(!T.bAnyProfile&&T.Profile==Profile)return &T;if(T.bAnyProfile)Any=&T;}if(Any)return Any;}
 return nullptr;
}
bool FDWCombatNumericProvider::ResolveEnemySource(const FDWEnemyNumbers* ClassNumbers,EDWEnemyCombatProfile Requested,const FDWEnemyNumbers* OpeningLegacy,const FDWEnemyNumbers*& Out,FString& Error)
{
 if(OpeningLegacy)
 {
  if(Requested!=EDWEnemyCombatProfile::Legacy||OpeningLegacy->Profile!=EDWEnemyCombatProfile::Legacy||OpeningLegacy->TypeKey!=FName(TEXT("OpeningLegacy"))||OpeningLegacy->bNeutral||OpeningLegacy->TargetClass.IsNull()||
     (ClassNumbers&&(ClassNumbers->TargetClass!=OpeningLegacy->TargetClass||ClassNumbers->bNeutral)))
  {Error=TEXT("教学角色数值上下文与实例身份不一致 / Invalid opening numeric source identity");return false;}
  if(!DWNumericSource::ValidateEnemy(*OpeningLegacy,Error))return false;
  Out=OpeningLegacy;Error.Reset();return true;
 }
 if(!ClassNumbers){Error=TEXT("角色缺少统一数值来源 / Missing character numeric source");return false;}
 if(ClassNumbers->Profile!=Requested){Error=TEXT("角色数值类型不匹配，未获得教学来源绑定 / Numeric profile mismatch without opening binding");return false;}
 Out=ClassNumbers;Error.Reset();return true;
}
bool FDWCombatNumericProvider::ProjectEnemy(const FDWEnemyNumbers& Numbers,FDWEnemyCombatConfig& Out,FDWEnemyActorNumericSupplement& Extra,FString& Error)
{
 if(Numbers.Profile==EDWEnemyCombatProfile::Legacy){Error=TEXT("旧敌人不采用正式投影 / Legacy source mismatch");return false;}
 FDWEnemyCombatConfig C;C.Profile=Numbers.Profile;C.HitsPerTarget=1;FDWEnemyActorNumericSupplement Pending;
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::MaxHealth);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.Health=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::MoveSpeed);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.MoveSpeedCmPerSecond=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::Damage);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.Damage=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::Windup);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.WindupSeconds=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::Active);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.ActiveSeconds=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::Recovery);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.RecoverySeconds=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::ExtraCooldown);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.ExtraCooldownSeconds=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::Range);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.RangeCm=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::FullAngle);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.AttackFullAngleDegrees=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::Height);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.AttackHeightCm=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::IncomingKnockbackMultiplier);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.KnockbackMultiplier=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::AttackKnockbackMultiplier);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.AttackKnockbackMultiplier=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::OutgoingKnockback);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.OutgoingKnockbackCm=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::OutgoingKnockbackSeconds);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.OutgoingKnockbackSeconds=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::AlertRadius);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.AlertRadiusCm=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::DisengageDistance);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.DisengageDistanceCm=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::AlertSeconds);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.AlertDelaySeconds=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::DisengageSeconds);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.DisengageSeconds=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::WindupLockFraction);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.WindupLockFraction=R->Value;}
 {const auto* R=DWNumericSource::Field(Numbers,EDWEnemyNumericField::ReturnSpeedMultiplier);if(!R||!DWEnemyNumericPolicy::ValidateValue(R->Field,R->Value)){Error=TEXT("正式敌人字段缺失或非法 / Incomplete formal source");return false;}C.ReturnSpeedMultiplier=R->Value;}

 for(auto F:{EDWEnemyNumericField::PatrolRadius,EDWEnemyNumericField::PatrolWaitSeconds,EDWEnemyNumericField::DeathCleanupSeconds})
 {const auto* R=DWNumericSource::Field(Numbers,F);if(!R||!DWEnemyNumericPolicy::ValidateValue(F,R->Value)){Error=TEXT("敌人行为字段缺失 / Incomplete enemy behavior");return false;}Pending.Values.Add(F,R->Value);}
 if(Numbers.Values.Num()!=23){Error=TEXT("正式角色必须有完整二十三项数值 / Incomplete formal field set");return false;}
 if(!DWEnemyFormal::ValidateConfig(C,Error))return false;
 Out=C;Extra=MoveTemp(Pending);Error.Reset();return true;
}
