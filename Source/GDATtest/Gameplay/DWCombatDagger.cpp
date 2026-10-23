#include "DWCombatDagger.h"
#include "DWCombatRules.h"
bool FDWDaggerTuning::Build(const FDWCombatRules& Rules,FDWDaggerTuning& Out,FString& Error)
{
    FDWDaggerTuning C;auto& T=C.Attack;
    struct FRead{const TCHAR* Id;EDWCombatRuleUnit Unit;double* Value;};
    const FRead Reads[]={
        {TEXT("weapon.dagger.damage"),EDWCombatRuleUnit::Points,&T.Damage},
        {TEXT("weapon.dagger.range_m"),EDWCombatRuleUnit::Centimeters,&T.RangeCm},
        {TEXT("weapon.dagger.arc_deg"),EDWCombatRuleUnit::FullDegrees,&T.FullArcDegrees},
        {TEXT("weapon.dagger.interval_s"),EDWCombatRuleUnit::Seconds,&T.IntervalSeconds},
        {TEXT("weapon.dagger.windup_s"),EDWCombatRuleUnit::Seconds,&T.WindupSeconds},
        {TEXT("weapon.dagger.active_s"),EDWCombatRuleUnit::Seconds,&T.ActiveSeconds},
        {TEXT("weapon.dagger.recovery_s"),EDWCombatRuleUnit::Seconds,&T.RecoverySeconds},
        {TEXT("weapon.dagger.stage1_duration_s"),EDWCombatRuleUnit::Seconds,&C.SegmentDurationSeconds[0]},
        {TEXT("weapon.dagger.stage2_duration_s"),EDWCombatRuleUnit::Seconds,&C.SegmentDurationSeconds[1]},
        {TEXT("weapon.dagger.stage3_duration_s"),EDWCombatRuleUnit::Seconds,&C.SegmentDurationSeconds[2]},
        {TEXT("weapon.dagger.stage4_duration_s"),EDWCombatRuleUnit::Seconds,&C.SegmentDurationSeconds[3]},
        {TEXT("weapon.dagger.knockback_m"),EDWCombatRuleUnit::Centimeters,&T.KnockbackCm},
        {TEXT("weapon.knockback_duration_s"),EDWCombatRuleUnit::Seconds,&T.KnockbackSeconds},
        {TEXT("yeast.weapon.dagger.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&T.YeastDamageMultiplier},
        {TEXT("yeast.weapon.dagger.rate_multiplier"),EDWCombatRuleUnit::Multiplier,&T.YeastRateMultiplier},
        {TEXT("mold.root.damage_multiplier"),EDWCombatRuleUnit::Multiplier,&T.RootedDamageMultiplier},
        {TEXT("weapon.switch_lock_s"),EDWCombatRuleUnit::Seconds,&T.SwitchLockSeconds},
        {TEXT("weapon.dagger.back_arc_deg"),EDWCombatRuleUnit::FullDegrees,&C.BackArcDegrees},
        {TEXT("weapon.dagger.grace_s"),EDWCombatRuleUnit::Seconds,&C.GraceSeconds},
        {TEXT("weapon.dagger.refresh_radius_m"),EDWCombatRuleUnit::Centimeters,&C.RefreshRadiusCm},
        {TEXT("weapon.dagger.elite_maxhp_fraction"),EDWCombatRuleUnit::Fraction,&C.EliteMaxHealthFraction}};
    for(const auto& R:Reads)if(!Rules.TryGetNumber(R.Id,R.Unit,*R.Value,Error))return false;
    // Geometry query is shared. Dagger damage is flat, never the spear's distance falloff.
    T.EdgeBandFraction=0.;T.MinimumDamageFraction=1.;
    int32 Hits=0;if(!Rules.TryGetCount(TEXT("weapon.hits_per_target_per_stage"),Hits,Error))return false;
    if(Hits!=1||!T.IsValid()||!FMath::IsFinite(C.BackArcDegrees)||C.BackArcDegrees<=0.||C.BackArcDegrees>360.||
        !FMath::IsFinite(C.GraceSeconds)||C.GraceSeconds<=0.||!FMath::IsFinite(C.RefreshRadiusCm)||C.RefreshRadiusCm<0.||
        !FMath::IsFinite(C.EliteMaxHealthFraction)||C.EliteMaxHealthFraction<=0.||C.EliteMaxHealthFraction>1.)
    {Error=TEXT("Dagger requires valid phase/arc/grace/final-HP rules and one hit per target.");return false;}
    for(int32 I=0;I<4;++I){FDWSpearTuning Segment;if(!C.MakeSegmentTuning(I,Segment)){Error=TEXT("Dagger segment duration must yield finite positive phase timing.");return false;}}
    Out=C;Error.Reset();return true;
}
bool FDWDaggerTuning::MakeSegmentTuning(int32 Segment,FDWSpearTuning& Out)const
{
    if(Segment<0||Segment>=4||!Attack.IsValid()||!FMath::IsFinite(SegmentDurationSeconds[Segment])||SegmentDurationSeconds[Segment]<=0.)return false;
    FDWSpearTuning T=Attack;const double Scale=SegmentDurationSeconds[Segment]/Attack.IntervalSeconds;
    T.IntervalSeconds=SegmentDurationSeconds[Segment];T.WindupSeconds*=Scale;T.ActiveSeconds*=Scale;
    T.RecoverySeconds=T.IntervalSeconds-T.WindupSeconds-T.ActiveSeconds;
    if(!T.IsValid())return false;Out=T;return true;
}
void FDWDaggerAwareness::Observe(double Now,bool Inside,bool Equipped,bool Engaged,double FullGrace)
{
    if(!FMath::IsFinite(Now)||!FMath::IsFinite(FullGrace)||FullGrace<0.)return;
    if(!bInitialized){bInitialized=true;bInside=Inside;bEquipped=Equipped;Deadline=0.;return;}
    if(!Equipped||Engaged)Deadline=0.;
    else if(!bInside&&Inside&&bEquipped)Deadline=Now+FullGrace;
    // Equipping while standing inside is not a geometric crossing.
    bInside=Inside;bEquipped=Equipped;
}
void FDWDaggerAwareness::EquipmentChanged(bool Inside,bool Equipped)
{bInitialized=true;bInside=Inside;bEquipped=Equipped;Deadline=0.;}
void FDWDaggerAwareness::Refresh(double Now,double FullGrace,bool Equipped,bool Engaged)
{if(FMath::IsFinite(Now)&&FMath::IsFinite(FullGrace)&&FullGrace>=0.&&Equipped&&!Engaged)Deadline=Now+FullGrace;}
bool FDWDaggerAwareness::IsProtected(double Now,bool Equipped,bool Engaged)const
{return bInitialized&&bEquipped&&Equipped&&!Engaged&&FMath::IsFinite(Now)&&Now<Deadline;}
FDWDaggerAwarenessSave FDWDaggerAwareness::Capture(double Now)const
{FDWDaggerAwarenessSave S;S.bInitialized=bInitialized;S.bInside=bInside;S.bEquipped=bEquipped;S.GraceRemainingSeconds=FMath::IsFinite(Now)?FMath::Max(0.,Deadline-Now):0.;return S;}
bool FDWDaggerAwareness::Validate(const FDWDaggerAwarenessSave& S)
{return FMath::IsFinite(S.GraceRemainingSeconds)&&S.GraceRemainingSeconds>=0.&&
    (S.bInitialized||(!S.bInside&&!S.bEquipped&&S.GraceRemainingSeconds==0.))&&(S.bEquipped||S.GraceRemainingSeconds==0.);}
bool FDWDaggerAwareness::Restore(const FDWDaggerAwarenessSave& S,double Now)
{
    if(!Validate(S)||!FMath::IsFinite(Now)||!FMath::IsFinite(Now+S.GraceRemainingSeconds))return false;
    bInitialized=S.bInitialized;bInside=S.bInside;bEquipped=S.bEquipped;Deadline=Now+S.GraceRemainingSeconds;return true;
}
