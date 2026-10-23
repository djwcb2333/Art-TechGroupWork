#include "DWCombatPlayerVitals.h"
#include "DWCombatVitalsRules.h"
#include "DWCombatRules.h"
#include <cmath>
#include <limits>

namespace
{
bool FiniteNonNegative(double V){return std::isfinite(V)&&V>=0.;}
void AddCompensated(double& Value,double& Carry,double Delta)
{const double Y=Delta-Carry;const double T=Value+Y;Carry=(T-Value)-Y;Value=T;}
FDWCombatVitalsState ToPure(const FDWCombatPlayerVitalsSnapshot& S)
{
    FDWCombatVitalsState V;
    V.Health=S.Health;V.Water=S.Water;V.Satiety=S.Satiety;V.Special=S.Special;
    V.WaterEmptyElapsedSeconds=S.WaterEmptyElapsedSeconds;V.SatietyEmptyElapsedSeconds=S.SatietyEmptyElapsedSeconds;
    V.WaterDrainCarry=S.WaterDrainCarry;V.SatietyDrainCarry=S.SatietyDrainCarry;
    V.WaterEmptyTimeCarry=S.WaterEmptyTimeCarry;V.SatietyEmptyTimeCarry=S.SatietyEmptyTimeCarry;return V;
}
void FromPure(FDWCombatPlayerVitalsSnapshot& S,const FDWCombatVitalsState& V)
{
    S.Health=V.Health;S.Water=V.Water;S.Satiety=V.Satiety;S.Special=V.Special;
    S.WaterEmptyElapsedSeconds=V.WaterEmptyElapsedSeconds;S.SatietyEmptyElapsedSeconds=V.SatietyEmptyElapsedSeconds;
    S.WaterDrainCarry=V.WaterDrainCarry;S.SatietyDrainCarry=V.SatietyDrainCarry;
    S.WaterEmptyTimeCarry=V.WaterEmptyTimeCarry;S.SatietyEmptyTimeCarry=V.SatietyEmptyTimeCarry;
}
double UntilDamage(double Value,double Carry,double Floor,double Rate,double Elapsed,double TimeCarry,double Period,double Damage)
{
    if(Damage==0.)return std::numeric_limits<double>::infinity();
    if(Value>Floor||(Value==Floor&&Carry<0.))
        return Rate==0.?std::numeric_limits<double>::infinity():((Value-Floor)-Carry)/Rate+Period;
    return (Period-Elapsed)+TimeCarry;
}
bool ValidCompensated(double Value,double Carry,double Floor,double Max)
{
    return std::isfinite(Value)&&std::isfinite(Carry)&&Value>=Floor&&Value<=Max
        &&std::abs(Carry)<=std::numeric_limits<double>::epsilon()*std::max(1.,Max)
        &&Carry<=Value-Floor&&-Carry<=Max-Value;
}
}

bool FDWCombatPlayerVitalsConfig::IsValid() const
{
    return Vitals.IsValid()&&RulesCombatSchema>0&&std::isfinite(FormDurationSeconds)&&FormDurationSeconds>0.
        &&FiniteNonNegative(SpecialGainPerSecond)&&std::isfinite(InitialFormSpecial)
        &&InitialFormSpecial>=Vitals.SpecialFloor&&InitialFormSpecial<=Vitals.SpecialMax
        &&std::isfinite(BaseMoveCentimetersPerSecond)&&BaseMoveCentimetersPerSecond>0.&&BaseMoveCentimetersPerSecond<=MAX_flt
        &&std::isfinite(YeastMoveMultiplier)&&YeastMoveMultiplier>0.
        &&std::isfinite(BaseMoveCentimetersPerSecond*YeastMoveMultiplier)&&BaseMoveCentimetersPerSecond*YeastMoveMultiplier<=MAX_flt
        &&FiniteNonNegative(NormalIncomingMultiplier)&&FiniteNonNegative(YeastIncomingMultiplier)&&FiniteNonNegative(MoldIncomingMultiplier)
        &&Vitals.HealthMax<=MAX_flt&&Vitals.HealthFloor<MAX_flt
        &&Vitals.SpecialMax<=MAX_flt&&Vitals.WaterDamagePerEvent<=MAX_flt&&Vitals.SatietyDamagePerEvent<=MAX_flt
        &&(Vitals.WaterDamagePerEvent==0.||static_cast<float>(Vitals.WaterDamagePerEvent)>0.f)
        &&(Vitals.SatietyDamagePerEvent==0.||static_cast<float>(Vitals.SatietyDamagePerEvent)>0.f);
}
bool FDWCombatPlayerVitalsConfig::Build(const FDWCombatRules& Rules,FDWCombatPlayerVitalsConfig& Out,FString& Error)
{
    FDWCombatPlayerVitalsConfig C;
    if(!DWCombatVitals::BuildConfig(Rules,C.Vitals,Error))return false;
    struct FBinding {const TCHAR* Id;EDWCombatRuleUnit Unit;double FDWCombatPlayerVitalsConfig::*Field;};
    const FBinding Bindings[]={
        {TEXT("form.duration_s"),EDWCombatRuleUnit::Seconds,&FDWCombatPlayerVitalsConfig::FormDurationSeconds},
        {TEXT("special.initial"),EDWCombatRuleUnit::Points,&FDWCombatPlayerVitalsConfig::InitialFormSpecial},
        {TEXT("special.gain_per_second"),EDWCombatRuleUnit::PointsPerSecond,&FDWCombatPlayerVitalsConfig::SpecialGainPerSecond},
        {TEXT("player.move.base_mps"),EDWCombatRuleUnit::CentimetersPerSecond,&FDWCombatPlayerVitalsConfig::BaseMoveCentimetersPerSecond},
        {TEXT("yeast.move_multiplier"),EDWCombatRuleUnit::Multiplier,&FDWCombatPlayerVitalsConfig::YeastMoveMultiplier},
        {TEXT("player.normal.incoming_multiplier"),EDWCombatRuleUnit::Multiplier,&FDWCombatPlayerVitalsConfig::NormalIncomingMultiplier},
        {TEXT("yeast.incoming_multiplier"),EDWCombatRuleUnit::Multiplier,&FDWCombatPlayerVitalsConfig::YeastIncomingMultiplier},
        {TEXT("mold.mobile.incoming_multiplier"),EDWCombatRuleUnit::Multiplier,&FDWCombatPlayerVitalsConfig::MoldIncomingMultiplier}
    };
    for(const auto& B:Bindings)if(!Rules.TryGetNumber(B.Id,B.Unit,C.*B.Field,Error))return false;
    C.RulesCombatSchema=Rules.GetCombatSchema();
    if(!C.IsValid()){Error=TEXT("Player vitals runtime configuration cannot be represented by current native health/movement fields.");return false;}
    Out=C;Error.Reset();return true;
}
bool FDWCombatPlayerVitalsRuntime::Validate(const FDWCombatPlayerVitalsConfig& C,const FDWCombatPlayerVitalsSnapshot& S,FString& Error)
{
    if(!C.IsValid()||S.ResourceSchema!=ResourceSchema||S.RulesCombatSchema!=C.RulesCombatSchema||!ToPure(S).IsValid(C.Vitals)
        ||S.DehydrationOrdinal==MAX_uint64||S.StarvationOrdinal==MAX_uint64
        ||!ValidCompensated(S.FormRemainingSeconds,S.FormTimeCarry,0.,C.FormDurationSeconds)
        ||!ValidCompensated(S.Special,S.SpecialGrowthCarry,C.Vitals.SpecialFloor,C.Vitals.SpecialMax))
    {Error=TEXT("Invalid resource schema, finite range, compensated clock, or exhausted damage ordinal.");return false;}
    if(S.Health<=C.Vitals.HealthFloor&&S.BaseForm!=EDWCombatBaseForm::Normal)
    {Error=TEXT("Dead snapshots cannot retain a core form.");return false;}
    if(S.BaseForm==EDWCombatBaseForm::Normal)
    {
        if(S.FormRemainingSeconds!=0.||S.FormTimeCarry!=0.||S.Special!=C.Vitals.NormalSpecialValue||S.SpecialGrowthCarry!=0.)
        {Error=TEXT("Normal form must have zero remaining time and the configured normal special value.");return false;}
    }
    else if((S.BaseForm!=EDWCombatBaseForm::Mold&&S.BaseForm!=EDWCombatBaseForm::Yeast)||S.FormRemainingSeconds-S.FormTimeCarry<=0.)
    {Error=TEXT("A core form needs a valid base form and positive independent remaining time.");return false;}
    Error.Reset();return true;
}
bool FDWCombatPlayerVitalsRuntime::Initialize(const FDWCombatPlayerVitalsConfig& C,EDWCombatVitalsReset Reset)
{
    FDWCombatVitalsState Pure;
    if(!C.IsValid()||!DWCombatVitals::Initialize(Pure,C.Vitals,Reset))return false;
    FDWCombatPlayerVitalsSnapshot Candidate;
    Candidate.ResourceSchema=ResourceSchema;Candidate.RulesCombatSchema=C.RulesCombatSchema;FromPure(Candidate,Pure);
    return Restore(C,Candidate);
}
bool FDWCombatPlayerVitalsRuntime::Restore(const FDWCombatPlayerVitalsConfig& C,const FDWCombatPlayerVitalsSnapshot& S)
{
    FString Error;if(MutationRevision==MAX_uint64||!Validate(C,S,Error))return false;
    Config=C;State=S;bReady=true;++MutationRevision;return true;
}
FDWCombatPlayerVitalsSnapshot FDWCombatPlayerVitalsRuntime::Capture(double AuthoritativeHealth) const
{auto Copy=State;Copy.Health=AuthoritativeHealth;return Copy;}
bool FDWCombatPlayerVitalsRuntime::AdvanceNext(double Seconds,double AuthoritativeHealth,FDWCombatPlayerVitalsStep& Out,double SpecialSpendPerSecond)
{
    Out={};
    if(!bReady||MutationRevision==MAX_uint64||!FiniteNonNegative(SpecialSpendPerSecond)||!FiniteNonNegative(Seconds)||Seconds==0.||!std::isfinite(AuthoritativeHealth)||AuthoritativeHealth<=Config.Vitals.HealthFloor)return false;
    auto Candidate=Capture(AuthoritativeHealth);FString Error;if(!Validate(Config,Candidate,Error))return false;
    const auto& V=Config.Vitals;double Step=Seconds;
    if(SpecialSpendPerSecond>0.&&Candidate.BaseForm==EDWCombatBaseForm::Normal)return false;
    const double NetRate=Config.SpecialGainPerSecond-SpecialSpendPerSecond;
    if(NetRate<0.)Step=std::min(Step,((Candidate.Special-V.SpecialFloor)-Candidate.SpecialGrowthCarry)/(-NetRate));
    Step=std::min(Step,UntilDamage(Candidate.Water,Candidate.WaterDrainCarry,V.WaterFloor,V.WaterDrainPerSecond,
        Candidate.WaterEmptyElapsedSeconds,Candidate.WaterEmptyTimeCarry,V.WaterDamagePeriodSeconds,V.WaterDamagePerEvent));
    Step=std::min(Step,UntilDamage(Candidate.Satiety,Candidate.SatietyDrainCarry,V.SatietyFloor,V.SatietyDrainPerSecond,
        Candidate.SatietyEmptyElapsedSeconds,Candidate.SatietyEmptyTimeCarry,V.SatietyDamagePeriodSeconds,V.SatietyDamagePerEvent));
    if(Candidate.BaseForm!=EDWCombatBaseForm::Normal)Step=std::min(Step,Candidate.FormRemainingSeconds-Candidate.FormTimeCarry);
    if(!std::isfinite(Step)||Step<=0.)return false;
    auto Pure=ToPure(Candidate);const auto Advanced=DWCombatVitals::Advance(Pure,V,Step,false);
    if(!Advanced.bAccepted)return false;
    if((Advanced.Dehydration.EventCount>1&&V.WaterDamagePerEvent>0.)||(Advanced.Starvation.EventCount>1&&V.SatietyDamagePerEvent>0.))return false;
    if(Advanced.Dehydration.EventCount>=MAX_uint64-Candidate.DehydrationOrdinal||Advanced.Starvation.EventCount>=MAX_uint64-Candidate.StarvationOrdinal)return false;
    FromPure(Candidate,Pure);
    Candidate.DehydrationOrdinal+=Advanced.Dehydration.EventCount;Candidate.StarvationOrdinal+=Advanced.Starvation.EventCount;
    if(Candidate.BaseForm!=EDWCombatBaseForm::Normal)
    {
        const double Gain=NetRate*Step;if(!std::isfinite(Gain))return false;
        if(NetRate<0.&&Step>=((Candidate.Special-V.SpecialFloor)-Candidate.SpecialGrowthCarry)/(-NetRate))
        {Candidate.Special=V.SpecialFloor;Candidate.SpecialGrowthCarry=0.;}
        else if(Gain>=(V.SpecialMax-Candidate.Special)+Candidate.SpecialGrowthCarry)
        {Candidate.Special=V.SpecialMax;Candidate.SpecialGrowthCarry=0.;}
        else
        {AddCompensated(Candidate.Special,Candidate.SpecialGrowthCarry,Gain);AddCompensated(Candidate.Special,Candidate.SpecialGrowthCarry,std::fma(NetRate,Step,-Gain));}
        if(Step>=Candidate.FormRemainingSeconds-Candidate.FormTimeCarry)
        {
            Candidate.BaseForm=EDWCombatBaseForm::Normal;Candidate.FormRemainingSeconds=Candidate.FormTimeCarry=0.;
            Candidate.Special=V.NormalSpecialValue;Candidate.SpecialGrowthCarry=0.;Out.bFormExpired=true;
        }
        else AddCompensated(Candidate.FormRemainingSeconds,Candidate.FormTimeCarry,-Step);
    }
    if(!Validate(Config,Candidate,Error)){Out={};return false;}
    State=Candidate;++MutationRevision;Out.AdvancedSeconds=Step;Out.Dehydration=Advanced.Dehydration;Out.Starvation=Advanced.Starvation;
    Out.DehydrationOrdinal=State.DehydrationOrdinal;Out.StarvationOrdinal=State.StarvationOrdinal;return true;
}
bool FDWCombatPlayerVitalsRuntime::AddWater(double Amount,double AuthoritativeHealth,double& OutAdded)
{
    OutAdded=0.;
    if(!bReady||!FiniteNonNegative(Amount)||!std::isfinite(AuthoritativeHealth)||
        AuthoritativeHealth<=Config.Vitals.HealthFloor)return false;
    auto Candidate=Capture(AuthoritativeHealth);FString Error;
    if(!Validate(Config,Candidate,Error))return false;
    const double Accepted=FMath::Min(Amount,Config.Vitals.WaterMax-Candidate.Water);
    if(Accepted<=0.)return true;
    if(MutationRevision==MAX_uint64)return false;
    auto Pure=ToPure(Candidate);
    if(!DWCombatVitals::AddWater(Pure,Config.Vitals,Accepted))return false;
    FromPure(Candidate,Pure);
    if(!Validate(Config,Candidate,Error))return false;
    State=Candidate;++MutationRevision;OutAdded=Accepted;return true;
}
bool FDWCombatPlayerVitalsRuntime::EnterCommittedForm(EDWCombatBaseForm Form)
{
    if(!bReady||MutationRevision==MAX_uint64||State.BaseForm!=EDWCombatBaseForm::Normal||(Form!=EDWCombatBaseForm::Mold&&Form!=EDWCombatBaseForm::Yeast))return false;
    State.BaseForm=Form;State.FormRemainingSeconds=Config.FormDurationSeconds;State.FormTimeCarry=0.;
    State.Special=Config.InitialFormSpecial;State.SpecialGrowthCarry=0.;++MutationRevision;return true;
}
bool FDWCombatPlayerVitalsRuntime::SetSpecialAfterCommittedCost(double Value)
{
    if(!bReady||MutationRevision==MAX_uint64||State.BaseForm==EDWCombatBaseForm::Normal||!std::isfinite(Value)||Value<Config.Vitals.SpecialFloor||Value>Config.Vitals.SpecialMax)return false;
    State.Special=Value;State.SpecialGrowthCarry=0.;++MutationRevision;return true;
}
bool FDWCombatPlayerVitalsRuntime::DebitSpecialAfterCommittedCost(double Cost)
{
    if(!bReady||MutationRevision==MAX_uint64||State.BaseForm==EDWCombatBaseForm::Normal||!FiniteNonNegative(Cost))return false;
    const double Available=(State.Special-Config.Vitals.SpecialFloor)-State.SpecialGrowthCarry;
    if(Cost>Available)return false;
    auto Candidate=State;
    if(Cost==Available){Candidate.Special=Config.Vitals.SpecialFloor;Candidate.SpecialGrowthCarry=0.;}
    else
    {
        // TwoSum keeps the old low component even when the fee is much larger than that carry.
        const double Sum=State.Special-Cost,VirtualDelta=Sum-State.Special;
        const double Error=(State.Special-(Sum-VirtualDelta))+(-Cost-VirtualDelta);
        const double Correction=Error-State.SpecialGrowthCarry;
        Candidate.Special=Sum+Correction;
        Candidate.SpecialGrowthCarry=(Candidate.Special-Sum)-Correction;
    }
    FString Error;if(!Validate(Config,Candidate,Error))return false;
    State=Candidate;++MutationRevision;return true;
}
void FDWCombatPlayerVitalsRuntime::EndForm()
{
    if(!bReady)return;
    State.BaseForm=EDWCombatBaseForm::Normal;State.FormRemainingSeconds=State.FormTimeCarry=State.SpecialGrowthCarry=0.;
    State.Special=Config.Vitals.NormalSpecialValue;
    if(MutationRevision<MAX_uint64)++MutationRevision;
}
