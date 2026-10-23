#include "DWCombatUnarmed.h"
#include "DWCombatRules.h"
bool FDWCombatUnarmedTuning::Build(const FDWCombatRules& Rules,FDWCombatUnarmedTuning& Out,FString& Error)
{
    FDWCombatUnarmedTuning Candidate;auto& C=Candidate.Attack;
    struct FRead{const TCHAR* Id;EDWCombatRuleUnit Unit;double* Value;};
    const FRead Fields[]={
        {TEXT("unarmed.damage"),EDWCombatRuleUnit::Points,&C.Damage},
        {TEXT("unarmed.range_m"),EDWCombatRuleUnit::Centimeters,&C.RangeCm},
        {TEXT("unarmed.arc_deg"),EDWCombatRuleUnit::FullDegrees,&C.FullArcDegrees},
        {TEXT("unarmed.interval_s"),EDWCombatRuleUnit::Seconds,&C.IntervalSeconds},
        {TEXT("unarmed.windup_s"),EDWCombatRuleUnit::Seconds,&C.WindupSeconds},
        {TEXT("unarmed.active_s"),EDWCombatRuleUnit::Seconds,&C.ActiveSeconds},
        {TEXT("unarmed.recovery_s"),EDWCombatRuleUnit::Seconds,&C.RecoverySeconds},
        {TEXT("unarmed.knockback_m"),EDWCombatRuleUnit::Centimeters,&C.KnockbackCm},
        {TEXT("weapon.knockback_duration_s"),EDWCombatRuleUnit::Seconds,&C.KnockbackSeconds},
        {TEXT("yeast.unarmed.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastDamageMultiplier},
        {TEXT("yeast.unarmed.rate_multiplier"),EDWCombatRuleUnit::Multiplier,&C.YeastRateMultiplier},
        {TEXT("mold.root.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&C.RootedDamageMultiplier},
        {TEXT("weapon.switch_lock_s"),EDWCombatRuleUnit::Seconds,&C.SwitchLockSeconds}};
    for(const auto& F:Fields)if(!Rules.TryGetNumber(F.Id,F.Unit,*F.Value,Error))return false;
    C.EdgeBandFraction=0.;C.MinimumDamageFraction=1.; // A08 has no spear edge falloff.
    int32 Hits=0;if(!Rules.TryGetCount(TEXT("weapon.hits_per_target_per_stage"),Hits,Error))return false;
    if(Hits!=1||!C.IsValid()){Error=TEXT("Unarmed phases require finite, matching formal interval and one hit per target.");return false;}
    Out=Candidate;Error.Reset();return true;
}
