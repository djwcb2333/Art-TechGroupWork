#include "DWCombatShieldAxe.h"
#include "DWCombatRules.h"

bool FDWShieldAxeTuning::Build(const FDWCombatRules& R,FDWShieldAxeTuning& Out,FString& Error)
{
    FDWShieldAxeTuning C;auto& A=C.Attack;
    struct FRead{const TCHAR* Id;EDWCombatRuleUnit Unit;double* Value;};
    const FRead Fields[]={
        {TEXT("weapon.axe.damage"),EDWCombatRuleUnit::Points,&A.Damage},
        {TEXT("weapon.axe.range_m"),EDWCombatRuleUnit::Centimeters,&A.RangeCm},
        {TEXT("weapon.axe.arc_deg"),EDWCombatRuleUnit::FullDegrees,&A.FullArcDegrees},
        {TEXT("weapon.axe.interval_s"),EDWCombatRuleUnit::Seconds,&A.IntervalSeconds},
        {TEXT("weapon.axe.windup_s"),EDWCombatRuleUnit::Seconds,&A.WindupSeconds},
        {TEXT("weapon.axe.active_s"),EDWCombatRuleUnit::Seconds,&A.ActiveSeconds},
        {TEXT("weapon.axe.recovery_s"),EDWCombatRuleUnit::Seconds,&A.RecoverySeconds},
        {TEXT("weapon.axe.knockback_m"),EDWCombatRuleUnit::Centimeters,&A.KnockbackCm},
        {TEXT("weapon.knockback_duration_s"),EDWCombatRuleUnit::Seconds,&A.KnockbackSeconds},
        {TEXT("yeast.weapon.axe.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&A.YeastDamageMultiplier},
        {TEXT("yeast.weapon.axe.rate_multiplier"),EDWCombatRuleUnit::Multiplier,&A.YeastRateMultiplier},
        {TEXT("mold.root.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&A.RootedDamageMultiplier},
        {TEXT("weapon.switch_lock_s"),EDWCombatRuleUnit::Seconds,&A.SwitchLockSeconds},
        {TEXT("weapon.axe.block_reduction"),EDWCombatRuleUnit::Fraction,&C.BlockReduction},
        {TEXT("weapon.axe.block_arc_deg"),EDWCombatRuleUnit::FullDegrees,&C.BlockFullArcDegrees},
        {TEXT("weapon.axe.parry_before_s"),EDWCombatRuleUnit::Seconds,&C.ParryBeforeSeconds},
        {TEXT("weapon.axe.parry_after_s"),EDWCombatRuleUnit::Seconds,&C.ParryAfterSeconds},
        {TEXT("weapon.axe.re_raise_cooldown_s"),EDWCombatRuleUnit::Seconds,&C.ReopenCooldownSeconds},
        {TEXT("weapon.axe.counter_multiplier"),EDWCombatRuleUnit::Multiplier,&C.CounterMultiplier},
        {TEXT("weapon.axe.raise_visual_s"),EDWCombatRuleUnit::Seconds,&C.RaiseVisualSeconds},
        {TEXT("weapon.axe.lower_visual_s"),EDWCombatRuleUnit::Seconds,&C.LowerVisualSeconds}};
    for(const auto& F:Fields)if(!R.TryGetNumber(F.Id,F.Unit,*F.Value,Error))return false;
    int32 Hits=0;if(!R.TryGetCount(TEXT("weapon.axe.counter_hits"),C.CounterHits,Error)||
        !R.TryGetCount(TEXT("weapon.hits_per_target_per_stage"),Hits,Error))return false;
    A.EdgeBandFraction=0.;A.MinimumDamageFraction=1.;
    if(Hits!=1||!C.IsValid()){Error=TEXT("Invalid formal shield-axe clocks, arc or counter limits.");return false;}
    Out=C;Error.Reset();return true;
}
bool FDWShieldAxeTuning::IsValid()const
{
    const double Values[]={ParryBeforeSeconds,ParryAfterSeconds,ReopenCooldownSeconds,RaiseVisualSeconds,LowerVisualSeconds};
    for(double V:Values)if(!FMath::IsFinite(V)||V<0.)return false;
    return Attack.IsValid()&&FMath::IsFinite(BlockReduction)&&BlockReduction>=0.&&BlockReduction<=1.&&
        FMath::IsFinite(BlockFullArcDegrees)&&BlockFullArcDegrees>0.&&BlockFullArcDegrees<=360.&&
        CounterHits>=0&&FMath::IsFinite(CounterMultiplier)&&CounterMultiplier>=1.;
}
bool FDWShieldAxeDefense::Configure(const FDWShieldAxeTuning& C)
{if(!C.IsValid())return false;Tuning=C;bConfigured=true;Release();PerfectUntil=ReopenAt=0.;CounterCharges=0;return true;}
bool FDWShieldAxeDefense::Raise(double Now,uint8 Form)
{
    if(!bConfigured||bHeld||!FMath::IsFinite(Now)||Now<0.||Form>2)return false;
    NotifyBaseForm(Form);bHeld=true;bEligible=Now>=ReopenAt;
    if(bEligible){PerfectUntil=Now+Tuning.ParryBeforeSeconds;ReopenAt=Now+Tuning.ReopenCooldownSeconds;}
    return true;
}
bool FDWShieldAxeDefense::CanPerfectAt(double Now)const
{return bConfigured&&bHeld&&bEligible&&FMath::IsFinite(Now)&&Now>=0.&&Now<=PerfectUntil;}
FDWShieldImpactPolicy FDWShieldAxeDefense::PrepareImpact(double Now,EDWShieldIncomingKind Kind,FVector Facing,FVector Incoming)const
{
    FDWShieldImpactPolicy Result;
    if(!bConfigured||(Kind!=EDWShieldIncomingKind::Melee&&Kind!=EDWShieldIncomingKind::PhysicalProjectile)||Facing.ContainsNaN()||Incoming.ContainsNaN()||!FMath::IsFinite(Now)||Now<0.)return Result;
    Facing=Facing.GetSafeNormal2D();Incoming=Incoming.GetSafeNormal2D();
    if(Facing.IsNearlyZero()||Incoming.IsNearlyZero()||FVector::DotProduct(Facing,Incoming)+1.e-12<FMath::Cos(FMath::DegreesToRadians(Tuning.BlockFullArcDegrees*.5)))return Result;
    Result.bEligible=true;Result.bPerfectNow=CanPerfectAt(Now);Result.bOrdinaryBlock=bHeld;
    Result.OrdinaryDamageMultiplier=bHeld?1.-Tuning.BlockReduction:1.;Result.AfterSeconds=Tuning.ParryAfterSeconds;return Result;
}
double FDWShieldAxeDefense::ConsumeCounterAtSwing()
{if(CounterCharges<=0)return 1.;--CounterCharges;return Tuning.CounterMultiplier;}
FDWShieldAxeDefenseSnapshot FDWShieldAxeDefense::Capture(double Now)const
{
    FDWShieldAxeDefenseSnapshot S;S.CounterCharges=CounterCharges;S.BaseForm=CounterBaseForm;
    S.bPerfectEligible=bEligible;S.PerfectRemainingSeconds=bEligible?FMath::Max(0.,PerfectUntil-Now):0.;
    S.ReopenRemainingSeconds=FMath::Max(0.,ReopenAt-Now);return S;
}
bool FDWShieldAxeDefense::Restore(const FDWShieldAxeDefenseSnapshot& S,double Now)
{
    if(!bConfigured||S.Schema!=1||S.CounterCharges<0||S.CounterCharges>Tuning.CounterHits||S.BaseForm>2||
        !FMath::IsFinite(Now)||Now<0.||!FMath::IsFinite(S.PerfectRemainingSeconds)||S.PerfectRemainingSeconds<0.||S.PerfectRemainingSeconds>Tuning.ParryBeforeSeconds||
        !FMath::IsFinite(S.ReopenRemainingSeconds)||S.ReopenRemainingSeconds<0.||S.ReopenRemainingSeconds>Tuning.ReopenCooldownSeconds||
        (!S.bPerfectEligible&&S.PerfectRemainingSeconds!=0.))return false;
    CounterCharges=S.CounterCharges;CounterBaseForm=S.BaseForm;PerfectUntil=Now+S.PerfectRemainingSeconds;
    ReopenAt=Now+S.ReopenRemainingSeconds;bEligible=S.bPerfectEligible;bHeld=false;return true;
}
