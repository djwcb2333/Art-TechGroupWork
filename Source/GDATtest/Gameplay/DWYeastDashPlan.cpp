#include "DWYeastDashPlan.h"
#include "DWCombatRules.h"
#include <cmath>
#include <limits>
namespace DWYeastDashPrivate
{
bool Nonnegative(double V){return std::isfinite(V)&&V>=0.;}
bool Positive(double V){return std::isfinite(V)&&V>0.;}
void Add(double& V,double& Carry,double Delta){const double Y=Delta-Carry,N=V+Y;Carry=(N-V)-Y;V=N;}
bool Direction(FVector2D Move,FVector2D Aim,FVector2D& Out)
{
    if(Move.ContainsNaN()||Aim.ContainsNaN())return false;
    const double M=std::hypot(Move.X,Move.Y),A=std::hypot(Aim.X,Aim.Y);
    if(!std::isfinite(M)||!std::isfinite(A))return false;
    if(M>0.)Out=Move/M;else if(A>0.)Out=Aim/A;else return false;return true;
}
}
bool FDWYeastDashConfig::IsValid()const
{
    using namespace DWYeastDashPrivate;
    return Nonnegative(StartCost)&&Positive(SpendPerSecond)&&Nonnegative(GainPerSecond)&&SpendPerSecond>GainPerSecond&&
        Nonnegative(SpecialFloor)&&Positive(SpecialMax)&&SpecialMax>SpecialFloor&&Positive(FormDuration)&&
        Positive(MinimumSeconds)&&MinimumSeconds<=FormDuration&&Positive(SpeedCmPerSecond)&&Nonnegative(CooldownSeconds)&&
        Nonnegative(Damage)&&Damage<=MAX_flt&&Nonnegative(KnockbackCm)&&KnockbackCm<=MAX_flt&&Positive(KnockbackSeconds)&&
        KnockbackSeconds<=MAX_flt&&Positive(QueryRadiusCm)&&QueryRadiusCm<=MAX_flt&&HitsPerTarget==1&&
        std::isfinite(MinimumBudget())&&MinimumBudget()<=SpecialMax-SpecialFloor&&std::isfinite(SpeedCmPerSecond*FormDuration);
}
bool FDWYeastDashConfig::Build(const FDWCombatRules& R,FDWYeastDashConfig& Out,FString& Error)
{
    FDWYeastDashConfig C;
    struct FRead{const TCHAR* Id;EDWCombatRuleUnit Unit;double FDWYeastDashConfig::*Field;};
    const FRead Fields[]={
        {TEXT("yeast.dash.start_cost"),EDWCombatRuleUnit::Points,&FDWYeastDashConfig::StartCost},
        {TEXT("yeast.dash.spend_per_second"),EDWCombatRuleUnit::PointsPerSecond,&FDWYeastDashConfig::SpendPerSecond},
        {TEXT("yeast.dash.min_duration_s"),EDWCombatRuleUnit::Seconds,&FDWYeastDashConfig::MinimumSeconds},
        {TEXT("yeast.dash.speed_mps"),EDWCombatRuleUnit::CentimetersPerSecond,&FDWYeastDashConfig::SpeedCmPerSecond},
        {TEXT("yeast.dash.damage"),EDWCombatRuleUnit::Points,&FDWYeastDashConfig::Damage},
        {TEXT("yeast.dash.knockback_m"),EDWCombatRuleUnit::Centimeters,&FDWYeastDashConfig::KnockbackCm},
        {TEXT("yeast.dash.knockback_duration_s"),EDWCombatRuleUnit::Seconds,&FDWYeastDashConfig::KnockbackSeconds},
        {TEXT("yeast.dash.collision_radius_m"),EDWCombatRuleUnit::Centimeters,&FDWYeastDashConfig::QueryRadiusCm},
        {TEXT("yeast.dash.cooldown_s"),EDWCombatRuleUnit::Seconds,&FDWYeastDashConfig::CooldownSeconds},
        {TEXT("special.gain_per_second"),EDWCombatRuleUnit::PointsPerSecond,&FDWYeastDashConfig::GainPerSecond},
        {TEXT("special.floor"),EDWCombatRuleUnit::Points,&FDWYeastDashConfig::SpecialFloor},
        {TEXT("special.max"),EDWCombatRuleUnit::Points,&FDWYeastDashConfig::SpecialMax},
        {TEXT("form.duration_s"),EDWCombatRuleUnit::Seconds,&FDWYeastDashConfig::FormDuration}};
    for(const auto& F:Fields)if(!R.TryGetNumber(F.Id,F.Unit,C.*F.Field,Error))return false;
    if(!R.TryGetCount(TEXT("yeast.dash.hits_per_target"),C.HitsPerTarget,Error))return false;
    if(!C.IsValid()){Error=TEXT("D09 dash rules cannot fund a finite minimum duration or contain invalid units/ranges.");return false;}
    Out=C;Error.Reset();return true;
}
bool FDWYeastDashPlan::CanStart(const FDWYeastDashConfig& C,double Special,double Form)const
{
    return !bActive&&CooldownSeconds()<=0.&&C.IsValid()&&std::isfinite(Special)&&Special<=C.SpecialMax&&
        Special-C.SpecialFloor>=C.MinimumBudget()&&std::isfinite(Form)&&Form>=C.MinimumSeconds&&Form<=C.FormDuration;
}
bool FDWYeastDashPlan::Start(const FDWYeastDashConfig& C,double Special,double Form,FVector2D Move,FVector2D Aim,uint64 Press)
{
    if(!Press||Press<=LastAttempt||ActionSerial==MAX_uint64)return false;
    LastAttempt=Press;FVector2D Direction;
    if(!CanStart(C,Special,Form)||!DWYeastDashPrivate::Direction(Move,Aim,Direction))return false;
    Config=C;bActive=bHeld=true;Elapsed=Carry=0.;ActivePress=Press;LockedDirection=Direction;EndReason=EDWYeastDashEnd::None;++ActionSerial;return true;
}
bool FDWYeastDashPlan::Release(uint64 Press)
{
    if(!bActive||!Press||Press!=ActivePress)return false;bHeld=false;
    // Runtime consumes the zero boundary after input release, keeping cleanup in one native seam.
    return true;
}
double FDWYeastDashPlan::NextBoundary(double Special,double Form)const
{
    if(!bActive)return CooldownSeconds()>0.?CooldownSeconds():std::numeric_limits<double>::infinity();
    if(!std::isfinite(Special)||!std::isfinite(Form)||Special<Config.SpecialFloor||Special>Config.SpecialMax||Form<0.)return 0.;
    double Next=FMath::Min(Form,(Special-Config.SpecialFloor)/Config.NetSpend());
    if(!bHeld)Next=FMath::Min(Next,FMath::Max(0.,Config.MinimumSeconds-ElapsedSeconds()));return Next;
}
bool FDWYeastDashPlan::Advance(double Seconds,double Special,double Form,FDWYeastDashStep& Out)
{
    Out={};if(!bActive||!DWYeastDashPrivate::Nonnegative(Seconds)||!std::isfinite(Special)||Special<Config.SpecialFloor||
        Special>Config.SpecialMax||!DWYeastDashPrivate::Nonnegative(Form))return false;
    const double Budget=(Special-Config.SpecialFloor)/Config.NetSpend(),Minimum=FMath::Max(0.,Config.MinimumSeconds-ElapsedSeconds());
    Out.Seconds=FMath::Min(Seconds,FMath::Min(Form,Budget));if(!bHeld)Out.Seconds=FMath::Min(Out.Seconds,Minimum);
    return AdvanceAtResourceBoundary(Out.Seconds,Out.Seconds>=Form,Out.Seconds>=Budget,Out);
}
bool FDWYeastDashPlan::AdvanceAtResourceBoundary(double Seconds,bool bFormExpired,bool bSpecialFloor,FDWYeastDashStep& Out)
{
    Out={};if(!bActive||!DWYeastDashPrivate::Nonnegative(Seconds))return false;
    const double Minimum=FMath::Max(0.,Config.MinimumSeconds-ElapsedSeconds());
    if(!bHeld&&Seconds>Minimum)return false;
    Out.Seconds=Seconds;Out.DistanceCm=Config.SpeedCmPerSecond*Seconds;if(!std::isfinite(Out.DistanceCm))return false;
    DWYeastDashPrivate::Add(Elapsed,Carry,Seconds);
    if(bFormExpired)Out.End=EDWYeastDashEnd::FormExpired;
    else if(bSpecialFloor)Out.End=EDWYeastDashEnd::SpecialFloor;
    else if(!bHeld&&Seconds>=Minimum)Out.End=EDWYeastDashEnd::Released;
    // Ending is a separate fact so the caller can finish this last swept segment and hit identity.
    return true;
}
bool FDWYeastDashPlan::End(EDWYeastDashEnd Reason)
{
    if(!bActive||Reason==EDWYeastDashEnd::None)return false;
    bActive=bHeld=false;EndReason=Reason;Cooldown=Config.CooldownSeconds;CooldownCarry=0.;return true;
}
bool FDWYeastDashPlan::AdvanceCooldown(double Seconds)
{
    if(bActive||!DWYeastDashPrivate::Nonnegative(Seconds))return false;
    if(Seconds>=CooldownSeconds())Cooldown=CooldownCarry=0.;else DWYeastDashPrivate::Add(Cooldown,CooldownCarry,-Seconds);return true;
}
bool FDWYeastDashPlan::RestoreCooldown(double Remaining)
{
    if(bActive||!Config.IsValid()||!DWYeastDashPrivate::Nonnegative(Remaining)||Remaining>Config.CooldownSeconds)return false;
    Cooldown=Remaining;CooldownCarry=0.;return true;
}

bool FDWYeastDashPlan::RestoreCooldown(const FDWYeastDashConfig& C,double Remaining)
{
    if(!C.IsValid()||!DWYeastDashPrivate::Nonnegative(Remaining)||Remaining>C.CooldownSeconds)return false;
    FDWYeastDashPlan Candidate;Candidate.Config=C;
    if(!Candidate.RestoreCooldown(Remaining))return false;
    *this=MoveTemp(Candidate);return true;
}
