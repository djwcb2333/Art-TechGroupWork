#include "DWCombatMines.h"
#include "DWCombatRules.h"

bool FDWMoldMineConfig::IsValid() const
{
    for(double X:{RingRadiusCm,TriggerRadiusCm,DamageRadiusCm,Damage,KnockbackCm,KnockbackSeconds,CooldownSeconds,GroundToleranceCm})
        if(!FMath::IsFinite(X)||X<0.||X>MAX_flt)return false;
    return Count>0&&HitsPerTarget==1&&RingRadiusCm>0.&&TriggerRadiusCm>0.&&DamageRadiusCm>0.&&Damage>0.&&KnockbackSeconds>0.;
}
bool FDWMoldMineConfig::Build(const FDWCombatRules& R,FDWMoldMineConfig& Out,FString& Error)
{
    using U=EDWCombatRuleUnit;FDWMoldMineConfig C;
    if(!R.TryGetCount(TEXT("mold.mine.count"),C.Count,Error)||!R.TryGetCount(TEXT("mold.mine.hits_per_target_per_root"),C.HitsPerTarget,Error)||
       !R.TryGetNumber(TEXT("mold.mine.ring_radius_m"),U::Centimeters,C.RingRadiusCm,Error)||
       !R.TryGetNumber(TEXT("mold.mine.trigger_radius_m"),U::Centimeters,C.TriggerRadiusCm,Error)||
       !R.TryGetNumber(TEXT("mold.mine.damage_radius_m"),U::Centimeters,C.DamageRadiusCm,Error)||
       !R.TryGetNumber(TEXT("mold.mine.damage"),U::Points,C.Damage,Error)||
       !R.TryGetNumber(TEXT("mold.mine.knockback_m"),U::Centimeters,C.KnockbackCm,Error)||
       !R.TryGetNumber(TEXT("mold.mine.knockback_duration_s"),U::Seconds,C.KnockbackSeconds,Error)||
       !R.TryGetNumber(TEXT("mold.mine.deploy_cooldown_s"),U::Seconds,C.CooldownSeconds,Error)||
       !R.TryGetNumber(TEXT("mold.mine.ground_tolerance_m"),U::Centimeters,C.GroundToleranceCm,Error))return false;
    if(!C.IsValid()){Error=TEXT("Invalid mine configuration or unsupported non-one ring hit rule.");return false;}
    Out=C;return true;
}
FVector FDWMoldMineConfig::Point(FVector Anchor,int32 Ordinal) const
{
    if(!IsValid()||Anchor.ContainsNaN()||Ordinal<1||Ordinal>Count)return FVector::ZeroVector;
    const double Angle=2.*UE_DOUBLE_PI*static_cast<double>(Ordinal-1)/Count;
    return Anchor+FVector(FMath::Cos(Angle)*RingRadiusCm,FMath::Sin(Angle)*RingRadiusCm,0.);
}
bool DWMoldMines::ValidateSnapshot(const FDWMoldMineRingSave& S,FString& Error)
{
    if(S.Schema!=1||!FMath::IsFinite(S.CooldownRemainingSeconds)||S.CooldownRemainingSeconds<0.||S.Anchor.ContainsNaN())
    {Error=TEXT("Invalid mine snapshot header.");return false;}
    if(!S.bActive)
    {
        if(S.RingId.IsValid()||!S.Mines.IsEmpty()||!S.HitFormalEnemies.IsEmpty()||!S.HitLegacyEnemies.IsEmpty()||!S.Anchor.IsZero())
        {Error=TEXT("Inactive root cannot retain a defensive ring.");return false;}
        return true;
    }
    if(!S.RingId.IsValid()){Error=TEXT("Active ring needs a stable ID.");return false;}
    TSet<FGuid> IDs;TSet<int32> Ordinals;
    for(const auto& M:S.Mines)
    {
        if(!M.MineId.IsValid()||IDs.Contains(M.MineId)||M.Ordinal<=0||Ordinals.Contains(M.Ordinal)||M.Location.ContainsNaN())
        {Error=TEXT("Duplicate or invalid mine identity/position.");return false;}
        IDs.Add(M.MineId);Ordinals.Add(M.Ordinal);
    }
    IDs.Reset();for(FGuid ID:S.HitFormalEnemies)
    {if(!ID.IsValid()||IDs.Contains(ID)){Error=TEXT("Invalid formal enemy hit identity.");return false;}IDs.Add(ID);}
    TSet<FName> Names;for(FName ID:S.HitLegacyEnemies)
    {if(ID.IsNone()||Names.Contains(ID)){Error=TEXT("Invalid legacy enemy hit identity.");return false;}Names.Add(ID);}
    return true;
}
