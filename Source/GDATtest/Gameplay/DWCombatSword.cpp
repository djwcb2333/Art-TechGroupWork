#include "DWCombatSword.h"
#include "DWCombatRules.h"
bool FDWSwordTuning::IsValid()const
{
    for(double V:{SlamDamage,SweepDamage,RangeCm,ChargeSeconds,SlamSeconds,SweepSeconds,SlamArc,SweepArc,
        KnockbackSeconds,YeastDamageMultiplier,YeastRecoveryRate,RootDamageMultiplier})
        if(!FMath::IsFinite(V)||V<=0.||V>MAX_flt)return false;
    for(double V:{GapSeconds,RecoverySeconds,KnockbackCm})if(!FMath::IsFinite(V)||V<0.||V>MAX_flt)return false;
    return SlamArc<=360.&&SweepArc<=360.&&
        FMath::Max(SlamDamage,SweepDamage)*FMath::Max(1.,FMath::Max(YeastDamageMultiplier,RootDamageMultiplier))<=MAX_flt;
}
bool FDWSwordTuning::Build(const FDWCombatRules& Rules,FDWSwordTuning& Out,FString& Error)
{
    FDWSwordTuning C;struct FRead{const TCHAR* Id;EDWCombatRuleUnit Unit;double* Value;};
    const FRead Reads[]={
        {TEXT("weapon.sword.slam_damage"),EDWCombatRuleUnit::Points,&C.SlamDamage},
        {TEXT("weapon.sword.sweep_damage"),EDWCombatRuleUnit::Points,&C.SweepDamage},
        {TEXT("weapon.sword.range_m"),EDWCombatRuleUnit::Centimeters,&C.RangeCm},
        {TEXT("weapon.sword.charge_s"),EDWCombatRuleUnit::Seconds,&C.ChargeSeconds},
        {TEXT("weapon.sword.follow_gap_s"),EDWCombatRuleUnit::Seconds,&C.GapSeconds},
        {TEXT("weapon.sword.recovery_s"),EDWCombatRuleUnit::Seconds,&C.RecoverySeconds},
        {TEXT("weapon.sword.slam_active_s"),EDWCombatRuleUnit::Seconds,&C.SlamSeconds},
        {TEXT("weapon.sword.sweep_active_s"),EDWCombatRuleUnit::Seconds,&C.SweepSeconds},
        {TEXT("weapon.sword.slam_arc_deg"),EDWCombatRuleUnit::FullDegrees,&C.SlamArc},
        {TEXT("weapon.sword.sweep_arc_deg"),EDWCombatRuleUnit::FullDegrees,&C.SweepArc},
        {TEXT("weapon.sword.knockback_m"),EDWCombatRuleUnit::Centimeters,&C.KnockbackCm},
        {TEXT("weapon.knockback_duration_s"),EDWCombatRuleUnit::Seconds,&C.KnockbackSeconds},
        {TEXT("yeast.weapon.sword.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastDamageMultiplier},
        {TEXT("yeast.weapon.sword.rate_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastRecoveryRate},
        {TEXT("mold.root.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.RootDamageMultiplier}};
    for(const auto& R:Reads)if(!Rules.TryGetNumber(R.Id,R.Unit,*R.Value,Error))return false;
    int32 Hits=0;if(!Rules.TryGetCount(TEXT("weapon.hits_per_target_per_stage"),Hits,Error))return false;
    if(Hits!=1||!C.IsValid()){Error=TEXT("Greatsword requires finite rules and exactly one hit per target per stage.");return false;}
    Out=C;Error.Reset();return true;
}
double FDWSwordTuning::DamageMultiplier(EDWSpearForm Form)const
{return Form==EDWSpearForm::Yeast?YeastDamageMultiplier:Form==EDWSpearForm::MoldRooted?RootDamageMultiplier:1.;}
bool FDWSwordAttackPlan::Start(const FDWSwordTuning& In)
{
    if(IsRunning()||!In.IsValid())return false;
    Tuning=In;Phase=EDWSwordPhase::Charge;Elapsed=0.;Duration=In.ChargeSeconds;DamageScale=0.;return true;
}
bool FDWSwordAttackPlan::AdvanceCurrentPhase(double Seconds,double& Consumed,double& Remaining)
{
    if(!IsRunning()||!FMath::IsFinite(Seconds)||Seconds<0.)return false;
    if(IsChargeReady()){Consumed=0.;Remaining=Seconds;return true;} // Holding is not a timed attack phase.
    const double Used=FMath::Min(Seconds,GetRemainingSeconds());Elapsed=FMath::Min(Duration,Elapsed+Used);
    Consumed=Used;Remaining=Seconds-Used;return true;
}
bool FDWSwordAttackPlan::EnterNextPhase(EDWSpearForm Form)
{
    if(!IsAtBoundary()||static_cast<uint8>(Form)>static_cast<uint8>(EDWSpearForm::MoldRooted))return false;
    switch(Phase)
    {
    case EDWSwordPhase::Charge:return false; // Only an observed release may leave full charge.
    case EDWSwordPhase::Slam:Phase=EDWSwordPhase::Gap;Duration=Tuning.GapSeconds;break;
    case EDWSwordPhase::Gap:Phase=EDWSwordPhase::Sweep;Duration=Tuning.SweepSeconds;break;
    case EDWSwordPhase::Sweep:Phase=EDWSwordPhase::Recovery;Duration=Tuning.RecoverySeconds/(Form==EDWSpearForm::Yeast?Tuning.YeastRecoveryRate:1.);break;
    case EDWSwordPhase::Recovery:Phase=EDWSwordPhase::Complete;Duration=0.;break;
    default:return false;
    }
    Elapsed=0.;return true;
}
bool FDWSwordAttackPlan::ReleaseCharge(EDWSpearForm Form)
{
    if(!IsChargeReady()||static_cast<uint8>(Form)>static_cast<uint8>(EDWSpearForm::MoldRooted))return false;
    Phase=EDWSwordPhase::Slam;Duration=Tuning.SlamSeconds;Elapsed=0.;DamageScale=Tuning.DamageMultiplier(Form);return true;
}
double FDWSwordAttackPlan::EstimateRemainingSeconds(EDWSpearForm Form)const
{
    double Left=IsChargeReady()?0.:GetRemainingSeconds();if(!IsRunning())return 0.;
    if(Phase==EDWSwordPhase::Charge)Left+=Tuning.SlamSeconds;
    if(Phase<=EDWSwordPhase::Slam)Left+=Tuning.GapSeconds;
    if(Phase<=EDWSwordPhase::Gap)Left+=Tuning.SweepSeconds;
    if(Phase<=EDWSwordPhase::Sweep)Left+=Tuning.RecoverySeconds/(Form==EDWSpearForm::Yeast?Tuning.YeastRecoveryRate:1.);
    return Left;
}
