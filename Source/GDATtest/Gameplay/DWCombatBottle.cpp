#include "DWCombatBottle.h"
#include "DWCombatRules.h"
#include <limits>
bool FDWBottleSnapshot::IsValid()const
{
    for(double V:{RangeCm,FlightSeconds,ArcHeightCm,CollisionRadiusCm,AreaRadiusCm,AreaSeconds,TickSeconds,TickDamage})
        if(!FMath::IsFinite(V)||V<=0.||V>MAX_flt)return false;
    return TickSeconds<=AreaSeconds&&FMath::IsFinite(SlowFraction)&&SlowFraction>=0.&&SlowFraction<1.;
}
bool FDWBottleConfig::IsValid()const
{
    for(double V:{IntervalSeconds,WindupSeconds,RecoverySeconds,NormalDamageMultiplier,NormalRateMultiplier,
        YeastDamageMultiplier,YeastRateMultiplier,YeastRangeCm,RootDamageMultiplier,RootRateMultiplier})
        if(!FMath::IsFinite(V)||V<=0.||V>MAX_flt)return false;
    return StockPerThrow>0&&WindupSeconds+RecoverySeconds<=IntervalSeconds&&Base.IsValid()&&
        Shot(EDWSpearForm::Normal).IsValid()&&Shot(EDWSpearForm::Yeast).IsValid()&&Shot(EDWSpearForm::MoldRooted).IsValid();
}
bool FDWBottleConfig::Build(const FDWCombatRules& Rules,FDWBottleConfig& Out,FString& Error)
{
    FDWBottleConfig C;double Impact=0.,Knockback=0.;using U=EDWCombatRuleUnit;
    struct FRead{const TCHAR* Id;U Unit;double* Value;};
    const FRead Reads[]={
        {TEXT("weapon.alcohol.range_m"),U::Centimeters,&C.Base.RangeCm},
        {TEXT("weapon.alcohol.flight_s"),U::Seconds,&C.Base.FlightSeconds},
        {TEXT("weapon.alcohol.arc_height_m"),U::Centimeters,&C.Base.ArcHeightCm},
        {TEXT("weapon.alcohol.collision_radius_m"),U::Centimeters,&C.Base.CollisionRadiusCm},
        {TEXT("weapon.alcohol.area_radius_m"),U::Centimeters,&C.Base.AreaRadiusCm},
        {TEXT("weapon.alcohol.duration_s"),U::Seconds,&C.Base.AreaSeconds},
        {TEXT("weapon.alcohol.tick_interval_s"),U::Seconds,&C.Base.TickSeconds},
        {TEXT("weapon.alcohol.tick_damage"),U::Points,&C.Base.TickDamage},
        {TEXT("weapon.alcohol.slow_fraction"),U::Fraction,&C.Base.SlowFraction},
        {TEXT("weapon.alcohol.interval_s"),U::Seconds,&C.IntervalSeconds},
        {TEXT("weapon.alcohol.windup_s"),U::Seconds,&C.WindupSeconds},
        {TEXT("weapon.alcohol.recovery_s"),U::Seconds,&C.RecoverySeconds},
        {TEXT("weapon.alcohol.impact_damage"),U::Points,&Impact},
        {TEXT("weapon.alcohol.knockback_m"),U::Centimeters,&Knockback},
        {TEXT("player.normal.damage_multiplier"),U::Multiplier,&C.NormalDamageMultiplier},
        {TEXT("player.normal.rate_multiplier"),U::Multiplier,&C.NormalRateMultiplier},
        {TEXT("yeast.weapon.alcohol.damage_multiplier"),U::Multiplier,&C.YeastDamageMultiplier},
        {TEXT("yeast.weapon.alcohol.rate_multiplier"),U::Multiplier,&C.YeastRateMultiplier},
        {TEXT("yeast.weapon.alcohol.range_m"),U::Centimeters,&C.YeastRangeCm},
        {TEXT("mold.root.damage_multiplier"),U::Multiplier,&C.RootDamageMultiplier},
        {TEXT("mold.root.ranged_rate_multiplier"),U::Multiplier,&C.RootRateMultiplier}};
    for(const auto& R:Reads)if(!Rules.TryGetNumber(R.Id,R.Unit,*R.Value,Error))return false;
    if(!Rules.TryGetCount(TEXT("weapon.alcohol.stock_per_throw"),C.StockPerThrow,Error)||Impact!=0.||Knockback!=0.||!C.IsValid())
    {if(Error.IsEmpty())Error=TEXT("Invalid formal bottle units/timing, or unsupported nonzero direct damage/knockback.");return false;}
    Out=C;Error.Reset();return true;
}
double FDWBottleConfig::Rate(EDWSpearForm F)const
{return F==EDWSpearForm::Yeast?YeastRateMultiplier:F==EDWSpearForm::MoldRooted?RootRateMultiplier:NormalRateMultiplier;}
FDWBottleSnapshot FDWBottleConfig::Shot(EDWSpearForm F)const
{
    auto S=Base;S.RangeCm=F==EDWSpearForm::Yeast?YeastRangeCm:Base.RangeCm;
    S.TickDamage*=F==EDWSpearForm::Yeast?YeastDamageMultiplier:F==EDWSpearForm::MoldRooted?RootDamageMultiplier:NormalDamageMultiplier;
    return S; // Flight, area lifetime and tick cadence never inherit the attack-rate multiplier.
}
bool FDWBottlePlan::Start(const FDWBottleConfig& C,EDWSpearForm F)
{if(IsRunning()||!C.IsValid()||static_cast<uint8>(F)>static_cast<uint8>(EDWSpearForm::MoldRooted))return false;Config=C;Phase=EDWBottlePhase::Windup;Elapsed=0.;PhaseRate=C.Rate(F);return true;}
double FDWBottlePlan::Duration()const
{
    switch(Phase){case EDWBottlePhase::Windup:return Config.WindupSeconds;case EDWBottlePhase::Recovery:return Config.RecoverySeconds;
    case EDWBottlePhase::Gap:return Config.IntervalSeconds-Config.WindupSeconds-Config.RecoverySeconds;default:return 0.;}
}
double FDWBottlePlan::Remaining()const
{return IsRunning()?FMath::Max(0.,Duration()-Elapsed)/PhaseRate:std::numeric_limits<double>::infinity();}
bool FDWBottlePlan::Advance(double Seconds,double& Consumed)
{
    if(!IsRunning()||!FMath::IsFinite(Seconds)||Seconds<0.)return false;
    const double Left=Remaining();Consumed=FMath::Min(Seconds,Left);Elapsed=Consumed>=Left?Duration():Elapsed+Consumed*PhaseRate;return true;
}
bool FDWBottlePlan::EnterNext(EDWSpearForm F)
{
    if(!AtBoundary()||static_cast<uint8>(F)>static_cast<uint8>(EDWSpearForm::MoldRooted))return false;
    switch(Phase){case EDWBottlePhase::Windup:Phase=EDWBottlePhase::Release;break;case EDWBottlePhase::Release:Phase=EDWBottlePhase::Recovery;break;
    case EDWBottlePhase::Recovery:Phase=EDWBottlePhase::Gap;break;case EDWBottlePhase::Gap:Phase=EDWBottlePhase::Complete;break;default:return false;}
    Elapsed=0.;PhaseRate=Config.Rate(F);return true;
}
double FDWBottlePlan::SourceSeconds()const
{
    switch(Phase){case EDWBottlePhase::Windup:return Elapsed;case EDWBottlePhase::Release:return Config.WindupSeconds;
    case EDWBottlePhase::Recovery:return Config.WindupSeconds+Elapsed;case EDWBottlePhase::Gap:return Config.WindupSeconds+Config.RecoverySeconds+Elapsed;
    case EDWBottlePhase::Complete:return Config.IntervalSeconds;default:return 0.;}
}
double FDWBottlePlan::EstimateRemaining(EDWSpearForm F)const
{return IsRunning()?Remaining()+FMath::Max(0.,Config.IntervalSeconds-SourceSeconds()-(Duration()-Elapsed))/Config.Rate(F):0.;}
FVector DWBottle::SampleArc(FVector Start,FVector End,double Flight,double Height,double Age)
{
    if(Start.ContainsNaN()||End.ContainsNaN()||!FMath::IsFinite(Flight)||Flight<=0.||!FMath::IsFinite(Height)||Height<0.||!FMath::IsFinite(Age))return Start;
    const double T=FMath::Clamp(Age/Flight,0.,1.);return FMath::Lerp(Start,End,T)+FVector(0.,0.,4.*Height*T*(1.-T));
}
FVector DWBottle::ClampHorizontalTarget(FVector Origin,FVector Requested,double Range)
{
    if(Origin.ContainsNaN()||Requested.ContainsNaN()||!FMath::IsFinite(Range)||Range<=0.)return Origin;
    const FVector Delta=Requested-Origin;const FVector XY=Delta.GetClampedToMaxSize2D(Range);return FVector(Origin.X+XY.X,Origin.Y+XY.Y,Requested.Z);
}
