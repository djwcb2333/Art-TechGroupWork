#include "DWCombatSporeGun.h"
#include "DWCombatRules.h"
#include <limits>
bool FDWSporeShotSnapshot::IsValid()const
{
    for(double V:{Damage,RangeCm,SpeedCmPerSecond,TurnDegreesPerSecond,RadiusCm,KnockbackSeconds})if(!FMath::IsFinite(V)||V<=0.||V>MAX_flt)return false;
    return FMath::IsFinite(KnockbackCm)&&KnockbackCm>=0.&&KnockbackCm<=MAX_flt;
}
bool FDWSporeGunConfig::IsValid()const
{
    for(double V:{IntervalSeconds,WindupSeconds,RecoverySeconds,TargetArcDegrees,NormalDamageMultiplier,NormalRateMultiplier,
        YeastDamageMultiplier,YeastRateMultiplier,YeastRangeCm,RootDamageMultiplier,RootRateMultiplier})if(!FMath::IsFinite(V)||V<=0.)return false;
    if(TargetArcDegrees>360.||AmmoPerShot<=0||WindupSeconds+RecoverySeconds>IntervalSeconds)return false;
    return Shot(EDWSporeForm::Normal).IsValid()&&Shot(EDWSporeForm::Yeast).IsValid()&&Shot(EDWSporeForm::MoldRooted).IsValid();
}
bool FDWSporeGunConfig::Build(const FDWCombatRules& R,FDWSporeGunConfig& Out,FString& Error)
{
    FDWSporeGunConfig C;using U=EDWCombatRuleUnit;
    struct FRead{const TCHAR* Id;U Unit;double* Value;};
    const FRead Reads[]={
        {TEXT("weapon.gun.damage"),U::Points,&C.Damage},{TEXT("weapon.gun.range_m"),U::Centimeters,&C.RangeCm},
        {TEXT("weapon.gun.interval_s"),U::Seconds,&C.IntervalSeconds},{TEXT("weapon.gun.windup_s"),U::Seconds,&C.WindupSeconds},
        {TEXT("weapon.gun.recovery_s"),U::Seconds,&C.RecoverySeconds},{TEXT("weapon.gun.target_arc_deg"),U::FullDegrees,&C.TargetArcDegrees},
        {TEXT("weapon.gun.projectile_speed_mps"),U::CentimetersPerSecond,&C.SpeedCmPerSecond},
        {TEXT("weapon.gun.turn_rate_degps"),U::DegreesPerSecond,&C.TurnDegreesPerSecond},
        {TEXT("weapon.gun.collision_radius_m"),U::Centimeters,&C.RadiusCm},
        {TEXT("weapon.gun.knockback_m"),U::Centimeters,&C.KnockbackCm},{TEXT("weapon.knockback_duration_s"),U::Seconds,&C.KnockbackSeconds},
        {TEXT("player.normal.damage_multiplier"),U::Multiplier,&C.NormalDamageMultiplier},{TEXT("player.normal.rate_multiplier"),U::Multiplier,&C.NormalRateMultiplier},
        {TEXT("yeast.weapon.gun.damage_multiplier"),U::Multiplier,&C.YeastDamageMultiplier},
        {TEXT("yeast.weapon.gun.rate_multiplier"),U::Multiplier,&C.YeastRateMultiplier},{TEXT("yeast.weapon.gun.range_m"),U::Centimeters,&C.YeastRangeCm},
        {TEXT("mold.root.damage_multiplier"),U::Multiplier,&C.RootDamageMultiplier},{TEXT("mold.root.ranged_rate_multiplier"),U::Multiplier,&C.RootRateMultiplier}};
    for(const auto& V:Reads)if(!R.TryGetNumber(V.Id,V.Unit,*V.Value,Error))return false;
    if(!R.TryGetCount(TEXT("weapon.gun.ammo_per_shot"),C.AmmoPerShot,Error)||!C.IsValid())
    {if(Error.IsEmpty())Error=TEXT("Invalid formal spore gun timing, units or snapshot range.");return false;}
    Out=C;Error.Reset();return true;
}
double FDWSporeGunConfig::Rate(EDWSporeForm F)const
{return F==EDWSporeForm::Yeast?YeastRateMultiplier:F==EDWSporeForm::MoldRooted?RootRateMultiplier:NormalRateMultiplier;}
FDWSporeShotSnapshot FDWSporeGunConfig::Shot(EDWSporeForm F)const
{
    return {Damage*(F==EDWSporeForm::Yeast?YeastDamageMultiplier:F==EDWSporeForm::MoldRooted?RootDamageMultiplier:NormalDamageMultiplier),
        F==EDWSporeForm::Yeast?YeastRangeCm:RangeCm,SpeedCmPerSecond,TurnDegreesPerSecond,RadiusCm,KnockbackCm,KnockbackSeconds};
}
bool FDWSporeShotPlan::Start(const FDWSporeGunConfig& C,EDWSporeForm F)
{if(IsRunning()||!C.IsValid()||static_cast<uint8>(F)>static_cast<uint8>(EDWSporeForm::MoldRooted))return false;Config=C;Phase=EDWSporePhase::Windup;Elapsed=0.;PhaseRate=C.Rate(F);return true;}
double FDWSporeShotPlan::Duration()const
{
    switch(Phase){case EDWSporePhase::Windup:return Config.WindupSeconds;case EDWSporePhase::Recovery:return Config.RecoverySeconds;
        case EDWSporePhase::Gap:return Config.IntervalSeconds-Config.WindupSeconds-Config.RecoverySeconds;default:return 0.;}
}
double FDWSporeShotPlan::Remaining()const
{return IsRunning()?FMath::Max(0.,Duration()-Elapsed)/PhaseRate:std::numeric_limits<double>::infinity();}
bool FDWSporeShotPlan::Advance(double Seconds,double& Consumed)
{
    if(!IsRunning()||!FMath::IsFinite(Seconds)||Seconds<0.)return false;
    Consumed=FMath::Min(Seconds,Remaining());
    Elapsed=Consumed>=Remaining()?Duration():Elapsed+Consumed*PhaseRate;return true;
}
bool FDWSporeShotPlan::EnterNext(EDWSporeForm F)
{
    if(!AtBoundary()||static_cast<uint8>(F)>static_cast<uint8>(EDWSporeForm::MoldRooted))return false;
    switch(Phase){case EDWSporePhase::Windup:Phase=EDWSporePhase::Release;break;
    case EDWSporePhase::Release:Phase=EDWSporePhase::Recovery;break;case EDWSporePhase::Recovery:Phase=EDWSporePhase::Gap;break;
    case EDWSporePhase::Gap:Phase=EDWSporePhase::Complete;break;default:return false;}
    Elapsed=0.;PhaseRate=Config.Rate(F);return true;
}
double FDWSporeShotPlan::GetSourceSeconds()const
{
    switch(Phase){case EDWSporePhase::Windup:return Elapsed;case EDWSporePhase::Release:return Config.WindupSeconds;
    case EDWSporePhase::Recovery:return Config.WindupSeconds+Elapsed;case EDWSporePhase::Gap:return Config.WindupSeconds+Config.RecoverySeconds+Elapsed;
    case EDWSporePhase::Complete:return Config.IntervalSeconds;default:return 0.;}
}
double FDWSporeShotPlan::EstimateRemaining(EDWSporeForm F)const
{return IsRunning()?Remaining()+FMath::Max(0.,Config.IntervalSeconds-GetSourceSeconds()-(Duration()-Elapsed))/Config.Rate(F):0.;}
FVector DWSporeGun::TurnTowards(FVector Current,FVector Desired,double Degrees)
{
    Current=Current.GetSafeNormal();Desired=Desired.GetSafeNormal();
    if(Current.IsNearlyZero()||Desired.IsNearlyZero()||!FMath::IsFinite(Degrees)||Degrees<=0.)return Current;
    const double Angle=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(Current,Desired),-1.,1.)));
    if(Angle<=Degrees)return Desired;
    return FQuat::Slerp(FQuat::Identity,FQuat::FindBetweenNormals(Current,Desired),Degrees/Angle).RotateVector(Current).GetSafeNormal();
}
