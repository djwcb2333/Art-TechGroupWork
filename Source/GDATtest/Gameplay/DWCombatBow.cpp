#include "DWCombatBow.h"
#include "DWCombatRules.h"
#include <limits>
bool FDWBowShotSnapshot::IsValid()const
{
    for(double V:{Damage,RangeCm,SpeedCmPerSecond,RadiusCm,KnockbackSeconds})if(!FMath::IsFinite(V)||V<=0.||V>MAX_flt)return false;
    return FMath::IsFinite(KnockbackCm)&&KnockbackCm>=0.&&KnockbackCm<=MAX_flt&&Area.IsValid();
}
bool FDWBowConfig::IsValid()const
{
    for(double V:{IntervalSeconds,WindupSeconds,RecoverySeconds,NormalDamageMultiplier,NormalRateMultiplier,
        YeastDamageMultiplier,YeastRateMultiplier,YeastRangeCm,RootDamageMultiplier,RootRateMultiplier})if(!FMath::IsFinite(V)||V<=0.)return false;
    return AmmoPerShot>0&&WindupSeconds+RecoverySeconds<=IntervalSeconds&&Shot(EDWBowForm::Normal).IsValid()&&Shot(EDWBowForm::Yeast).IsValid()&&Shot(EDWBowForm::MoldRooted).IsValid();
}
bool FDWBowConfig::Build(const FDWCombatRules& R,FDWBowConfig& Out,FString& Error)
{
    FDWBowConfig C;using U=EDWCombatRuleUnit;struct FRead{const TCHAR* Id;U Unit;double* Value;};double TickDamage=0.;
    const FRead Reads[]={
        {TEXT("weapon.bow.damage"),U::Points,&C.Base.Damage},{TEXT("weapon.bow.range_m"),U::Centimeters,&C.Base.RangeCm},
        {TEXT("weapon.bow.interval_s"),U::Seconds,&C.IntervalSeconds},{TEXT("weapon.bow.windup_s"),U::Seconds,&C.WindupSeconds},
        {TEXT("weapon.bow.recovery_s"),U::Seconds,&C.RecoverySeconds},{TEXT("weapon.bow.projectile_speed_mps"),U::CentimetersPerSecond,&C.Base.SpeedCmPerSecond},
        {TEXT("weapon.bow.collision_radius_m"),U::Centimeters,&C.Base.RadiusCm},{TEXT("weapon.bow.knockback_m"),U::Centimeters,&C.Base.KnockbackCm},
        {TEXT("weapon.knockback_duration_s"),U::Seconds,&C.Base.KnockbackSeconds},{TEXT("weapon.bow.area_radius_m"),U::Centimeters,&C.Base.Area.RadiusCm},
        {TEXT("weapon.bow.area_duration_s"),U::Seconds,&C.Base.Area.DurationSeconds},{TEXT("weapon.bow.slow_fraction"),U::Fraction,&C.Base.Area.SlowFraction},
        {TEXT("weapon.bow.area_tick_damage"),U::Points,&TickDamage},
        {TEXT("player.normal.damage_multiplier"),U::Multiplier,&C.NormalDamageMultiplier},{TEXT("player.normal.rate_multiplier"),U::Multiplier,&C.NormalRateMultiplier},
        {TEXT("yeast.weapon.bow.damage_multiplier"),U::Multiplier,&C.YeastDamageMultiplier},{TEXT("yeast.weapon.bow.rate_multiplier"),U::Multiplier,&C.YeastRateMultiplier},
        {TEXT("yeast.weapon.bow.range_m"),U::Centimeters,&C.YeastRangeCm},{TEXT("mold.root.damage_multiplier"),U::Multiplier,&C.RootDamageMultiplier},
        {TEXT("mold.root.ranged_rate_multiplier"),U::Multiplier,&C.RootRateMultiplier}};
    for(const auto& V:Reads)if(!R.TryGetNumber(V.Id,V.Unit,*V.Value,Error))return false;
    if(!R.TryGetCount(TEXT("weapon.bow.ammo_per_shot"),C.AmmoPerShot,Error)||TickDamage!=0.||!C.IsValid())
    {if(Error.IsEmpty())Error=TEXT("Invalid bow configuration; bow areas must have zero DOT.");return false;}
    Out=C;Error.Reset();return true;
}
double FDWBowConfig::Rate(EDWBowForm F)const
{return F==EDWBowForm::Yeast?YeastRateMultiplier:F==EDWBowForm::MoldRooted?RootRateMultiplier:NormalRateMultiplier;}
FDWBowShotSnapshot FDWBowConfig::Shot(EDWBowForm F)const
{auto S=Base;S.Damage*=F==EDWBowForm::Yeast?YeastDamageMultiplier:F==EDWBowForm::MoldRooted?RootDamageMultiplier:NormalDamageMultiplier;if(F==EDWBowForm::Yeast)S.RangeCm=YeastRangeCm;return S;}
bool FDWBowPlan::Start(const FDWBowConfig& C,EDWBowForm F)
{if(IsRunning()||!C.IsValid()||static_cast<uint8>(F)>3)return false;Config=C;Phase=EDWBowPhase::Windup;Elapsed=0.;PhaseRate=C.Rate(F);return true;}
double FDWBowPlan::Duration()const
{switch(Phase){case EDWBowPhase::Windup:return Config.WindupSeconds;case EDWBowPhase::Recovery:return Config.RecoverySeconds;case EDWBowPhase::Gap:return Config.IntervalSeconds-Config.WindupSeconds-Config.RecoverySeconds;default:return 0.;}}
double FDWBowPlan::Remaining()const
{return IsRunning()?FMath::Max(0.,Duration()-Elapsed)/PhaseRate:std::numeric_limits<double>::infinity();}
bool FDWBowPlan::Advance(double Seconds,double& Consumed)
{if(!IsRunning()||!FMath::IsFinite(Seconds)||Seconds<0.)return false;const double Left=Remaining();Consumed=FMath::Min(Seconds,Left);Elapsed=Consumed>=Left?Duration():Elapsed+Consumed*PhaseRate;return true;}
bool FDWBowPlan::EnterNext(EDWBowForm F)
{if(!AtBoundary()||static_cast<uint8>(F)>3)return false;switch(Phase){case EDWBowPhase::Windup:Phase=EDWBowPhase::Release;break;case EDWBowPhase::Release:Phase=EDWBowPhase::Recovery;break;case EDWBowPhase::Recovery:Phase=EDWBowPhase::Gap;break;case EDWBowPhase::Gap:Phase=EDWBowPhase::Complete;break;default:return false;}Elapsed=0.;PhaseRate=Config.Rate(F);return true;}
double FDWBowPlan::GetSourceSeconds()const
{switch(Phase){case EDWBowPhase::Windup:return Elapsed;case EDWBowPhase::Release:return Config.WindupSeconds;case EDWBowPhase::Recovery:return Config.WindupSeconds+Elapsed;case EDWBowPhase::Gap:return Config.WindupSeconds+Config.RecoverySeconds+Elapsed;case EDWBowPhase::Complete:return Config.IntervalSeconds;default:return 0.;}}
double FDWBowPlan::EstimateRemaining(EDWBowForm F)const
{return IsRunning()?Remaining()+FMath::Max(0.,Config.IntervalSeconds-GetSourceSeconds()-(Duration()-Elapsed))/Config.Rate(F):0.;}
