#include "DWCharacterA01.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
bool A01ResourceNonnegative(double V) { return std::isfinite(V)&&V>=0.; }
bool A01ResourcePositive(double V) { return std::isfinite(V)&&V>0.; }
bool A01ResourceFail(std::string* Error, const char* Message) { if(Error)*Error=Message; return false; }
bool A01ResourceFormValid(EDWA01Form Form) { return static_cast<unsigned>(Form)<=static_cast<unsigned>(EDWA01Form::Yeast); }
double A01ResourceAllowedLoss(const FDWCharacterA01Config& C)
{ return std::max(0.,std::min(C.HealthyMax-C.MinimumEffectiveMax,C.HealthyMax*C.MaximumLossFraction)); }
double A01ResourceCeiling(const FDWCharacterA01Config& C,const FDWCharacterA01State& S)
{ return std::max(C.MinimumEffectiveMax,C.HealthyMax-std::min(S.RecordedCapLoss,A01ResourceAllowedLoss(C))); }
bool A01ResourceClockValid(double Elapsed,double Period) { return A01ResourceNonnegative(Elapsed)&&Elapsed<Period; }
bool A01ResourceAdvanceClock(double& Elapsed,double Seconds,double Period)
{
    const double Until=Period-Elapsed;
    if(Seconds>=Until) { Elapsed=0.; return true; }
    Elapsed+=Seconds; return false;
}
double A01ResourceDrain(double Value,double Rate,double Seconds)
{ return Rate>0.&&Seconds>=Value/Rate?0.:std::max(0.,Value-Rate*Seconds); }
void A01ResourceRebaseClock(double& Elapsed,double OldPeriod,double NewPeriod)
{ Elapsed=std::min(std::nextafter(NewPeriod,0.),(Elapsed/OldPeriod)*NewPeriod); }
}

bool FDWCharacterA01Config::IsValid(std::string* Error) const
{
    if(!A01ResourcePositive(HealthyMax)||!A01ResourcePositive(MinimumEffectiveMax)||MinimumEffectiveMax>HealthyMax||
       !A01ResourceNonnegative(InitialHealth)||InitialHealth>HealthyMax||!A01ResourcePositive(SatietyMax)||!A01ResourcePositive(WaterMax)||
       !A01ResourceNonnegative(InitialSatiety)||InitialSatiety>SatietyMax||!A01ResourceNonnegative(InitialWater)||InitialWater>WaterMax)
        return A01ResourceFail(Error,"Invalid base health, survival caps or initial resource values.");
    for(double V:{SatietyDrainAmount,WaterDrainAmount,StarvationDamagePerSecond,CapLossAmount,CapRecoveryAmount,
        NormalRegenAmount,YeastSatietyMultiplier,YeastWaterMultiplier,MoldSatietyMultiplier,MoldWaterMultiplier,
        GasRegenPerSecond,CorruptionRegenPerSecond,CorruptionCost,CorruptionDamageBonus,CorruptionHarvestBonus})
        if(!A01ResourceNonnegative(V))return A01ResourceFail(Error,"Rates, bonuses and costs must be finite and nonnegative.");
    for(double V:{SatietyDrainIntervalSeconds,WaterDrainIntervalSeconds,CapLossIntervalSeconds,CapRecoveryIntervalSeconds,
        NormalRegenIntervalSeconds,YeastDurationSeconds,MoldDurationSeconds,GasMax,CorruptionMax})
        if(!A01ResourcePositive(V))return A01ResourceFail(Error,"Intervals, form durations and energy caps must be finite and positive.");
    for(double V:{ThirstMovementMultiplier,MaximumLossFraction,GasEntryFraction,CorruptionEntryFraction})
        if(!A01ResourceNonnegative(V)||V>1.)return A01ResourceFail(Error,"Configured fractions must be in [0,1].");
    if(CorruptionCost>CorruptionMax)return A01ResourceFail(Error,"Corruption action cost exceeds its effective cap.");
    for(double V:{SatietyDrainAmount/SatietyDrainIntervalSeconds,WaterDrainAmount/WaterDrainIntervalSeconds,
        SatietyDrainAmount/SatietyDrainIntervalSeconds*YeastSatietyMultiplier,
        SatietyDrainAmount/SatietyDrainIntervalSeconds*MoldSatietyMultiplier,
        WaterDrainAmount/WaterDrainIntervalSeconds*YeastWaterMultiplier,
        WaterDrainAmount/WaterDrainIntervalSeconds*MoldWaterMultiplier,
        1.+CorruptionDamageBonus,1.+CorruptionHarvestBonus})
        if(!A01ResourceNonnegative(V))return A01ResourceFail(Error,"Derived rate or multiplier overflow.");
    if(Error)Error->clear(); return true;
}
bool FDWCharacterA01Runtime::ValidateState(const FDWCharacterA01Config& C,const FDWCharacterA01State& S,std::string* Error)
{
    if(!C.IsValid(Error))return false;
    if(S.StarvationOrdinal==std::numeric_limits<std::uint64_t>::max()||S.Schema!=1||!A01ResourceFormValid(S.Form)||!A01ResourceNonnegative(S.RecordedCapLoss)||!A01ResourceNonnegative(S.CurrentHealth)||
       S.CurrentHealth>C.HealthyMax||!A01ResourceNonnegative(S.Satiety)||S.Satiety>C.SatietyMax||
       !A01ResourceNonnegative(S.Water)||S.Water>C.WaterMax||!A01ResourceNonnegative(S.Gas)||S.Gas>C.GasMax||
       !A01ResourceNonnegative(S.Corruption)||S.Corruption>C.CorruptionMax||(!S.bCorruptionInitialized&&S.Corruption!=0.))
        return A01ResourceFail(Error,"Invalid A01 snapshot resource or schema.");
    if(C.bClampCurrentHealthToEffectiveMax&&S.CurrentHealth>A01ResourceCeiling(C,S))return A01ResourceFail(Error,"Health exceeds effective cap.");
    if(!A01ResourceClockValid(S.CapLossElapsedSeconds,C.CapLossIntervalSeconds)||
       !A01ResourceClockValid(S.CapRecoveryElapsedSeconds,C.CapRecoveryIntervalSeconds)||
       !A01ResourceClockValid(S.NormalRegenElapsedSeconds,C.NormalRegenIntervalSeconds))return A01ResourceFail(Error,"Invalid A01 periodic elapsed time.");
    if(S.Form==EDWA01Form::Normal)
    {
        if(S.FormRemainingSeconds!=0.||S.EntryDurationSeconds!=0.||S.CapLossElapsedSeconds!=0.)
            return A01ResourceFail(Error,"Normal form retains an inoculation clock.");
    }
    else if(!A01ResourcePositive(S.FormRemainingSeconds)||!A01ResourcePositive(S.EntryDurationSeconds)||
            S.FormRemainingSeconds>S.EntryDurationSeconds||S.CurrentHealth<=0.||
            S.CapRecoveryElapsedSeconds!=0.||S.NormalRegenElapsedSeconds!=0.||
            (S.Form==EDWA01Form::Mold&&!S.bCorruptionInitialized))return A01ResourceFail(Error,"Invalid active form snapshot.");
    if(Error)Error->clear(); return true;
}
bool FDWCharacterA01Runtime::Initialize(const FDWCharacterA01Config& C,std::string* Error)
{
    if(!C.IsValid(Error))return false;
    FDWCharacterA01State Candidate; Candidate.CurrentHealth=C.InitialHealth;
    Candidate.Satiety=C.InitialSatiety;Candidate.Water=C.InitialWater;
    return Restore(C,Candidate,Error);
}
bool FDWCharacterA01Runtime::Restore(const FDWCharacterA01Config& C,const FDWCharacterA01State& S,std::string* Error)
{
    if(!ValidateState(C,S,Error))return false;
    Config=C;State=S;bReady=true;ToolActions.clear();LastToolActionId=0;return true;
}
double FDWCharacterA01Runtime::AllowedNewLoss() const { return A01ResourceAllowedLoss(Config); }
double FDWCharacterA01Runtime::EffectiveLoss() const { return std::min(State.RecordedCapLoss,AllowedNewLoss()); }
double FDWCharacterA01Runtime::EffectiveMaxHealth() const { return A01ResourceCeiling(Config,State); }
double FDWCharacterA01Runtime::MovementMultiplier() const
{ return State.Water<=0.?Config.ThirstMovementMultiplier:1.; }
void FDWCharacterA01Runtime::ClampCurrentHealth()
{ if(Config.bClampCurrentHealthToEffectiveMax)State.CurrentHealth=std::min(State.CurrentHealth,EffectiveMaxHealth()); }
bool FDWCharacterA01Runtime::ApplyConfig(const FDWCharacterA01Config& C,std::string* Error)
{
    if(!bReady||!C.IsValid(Error))return false;
    auto Candidate=*this;
    A01ResourceRebaseClock(Candidate.State.CapLossElapsedSeconds,Config.CapLossIntervalSeconds,C.CapLossIntervalSeconds);
    A01ResourceRebaseClock(Candidate.State.CapRecoveryElapsedSeconds,Config.CapRecoveryIntervalSeconds,C.CapRecoveryIntervalSeconds);
    A01ResourceRebaseClock(Candidate.State.NormalRegenElapsedSeconds,Config.NormalRegenIntervalSeconds,C.NormalRegenIntervalSeconds);
    Candidate.Config=C;Candidate.State.Gas=std::min(State.Gas,C.GasMax);
    Candidate.State.Corruption=std::min(State.Corruption,C.CorruptionMax);
    Candidate.State.Satiety=std::min(State.Satiety,C.SatietyMax);Candidate.State.Water=std::min(State.Water,C.WaterMax);
    Candidate.State.CurrentHealth=std::min(State.CurrentHealth,C.HealthyMax);Candidate.ClampCurrentHealth();
    if(!ValidateState(C,Candidate.State,Error))return false;
    *this=std::move(Candidate);return true;
}
void FDWCharacterA01Runtime::InvalidateCorruptionFacts()
{ for(auto& A:ToolActions){A.bInvalidated=true;A.bEnhanced=false;A.bDecided=true;A.DamageMultiplier=A.HarvestMultiplier=1.;} }
void FDWCharacterA01Runtime::EndFormFacts()
{
    if(State.Form==EDWA01Form::Mold)InvalidateCorruptionFacts();
    State.Form=EDWA01Form::Normal;State.FormRemainingSeconds=State.EntryDurationSeconds=0.;
    State.CapLossElapsedSeconds=State.CapRecoveryElapsedSeconds=State.NormalRegenElapsedSeconds=0.;
}
bool FDWCharacterA01Runtime::SyncCurrentHealth(double Health)
{
    if(!bReady||!A01ResourceNonnegative(Health))return false;
    State.CurrentHealth=std::min(Health,Config.HealthyMax);ClampCurrentHealth();
    if(State.CurrentHealth<=0.){EndFormFacts();CancelToolActions();}return true;
}
bool FDWCharacterA01Runtime::EnterForm(EDWA01Form Form)
{
    if(!bReady||State.CurrentHealth<=0.||!A01ResourceFormValid(Form)||Form==EDWA01Form::Normal)return false;
    const bool bWasNormal=State.Form==EDWA01Form::Normal;
    if(State.Form==EDWA01Form::Mold&&Form!=State.Form)InvalidateCorruptionFacts();
    State.Form=Form;State.EntryDurationSeconds=Form==EDWA01Form::Yeast?Config.YeastDurationSeconds:Config.MoldDurationSeconds;
    State.FormRemainingSeconds=State.EntryDurationSeconds;
    if(bWasNormal)State.CapLossElapsedSeconds=0.;
    State.CapRecoveryElapsedSeconds=State.NormalRegenElapsedSeconds=0.;
    if(Form==EDWA01Form::Yeast)State.Gas=Config.GasMax*Config.GasEntryFraction;
    else {State.Corruption=Config.CorruptionMax*Config.CorruptionEntryFraction;State.bCorruptionInitialized=true;}
    return true;
}
bool FDWCharacterA01Runtime::ExitForm()
{ if(!bReady||State.Form==EDWA01Form::Normal)return false;EndFormFacts();return true; }
bool FDWCharacterA01Runtime::SpendGas(double Cost)
{
    if(!bReady||State.CurrentHealth<=0.||State.Form!=EDWA01Form::Yeast||!A01ResourceNonnegative(Cost)||Cost>State.Gas)return false;
    State.Gas-=Cost;return true;
}
double FDWCharacterA01Runtime::AddSatiety(double Amount)
{
    if(!bReady||State.CurrentHealth<=0.||!A01ResourceNonnegative(Amount))return 0.;
    const double Added=std::min(Amount,Config.SatietyMax-State.Satiety);State.Satiety+=Added;return Added;
}
double FDWCharacterA01Runtime::AddWater(double Amount)
{
    if(!bReady||State.CurrentHealth<=0.||!A01ResourceNonnegative(Amount))return 0.;
    const double Added=std::min(Amount,Config.WaterMax-State.Water);State.Water+=Added;return Added;
}
bool FDWCharacterA01Runtime::AdvanceNext(double Maximum,const FDWCharacterA01Activity& Activity,FDWCharacterA01Step& Out)
{
    Out={};if(!bReady||State.CurrentHealth<=0.||!A01ResourcePositive(Maximum)||!A01ResourceNonnegative(Activity.GasSpendPerSecond))return false;
    if(Activity.GasSpendPerSecond>0.&&State.Form!=EDWA01Form::Yeast)return false;
    const bool bInoculated=State.Form!=EDWA01Form::Normal;
    const double SatietyMultiplier=State.Form==EDWA01Form::Yeast?Config.YeastSatietyMultiplier:State.Form==EDWA01Form::Mold?Config.MoldSatietyMultiplier:1.;
    const double WaterMultiplier=State.Form==EDWA01Form::Yeast?Config.YeastWaterMultiplier:State.Form==EDWA01Form::Mold?Config.MoldWaterMultiplier:1.;
    const double SatietyRate=Config.SatietyDrainAmount/Config.SatietyDrainIntervalSeconds*SatietyMultiplier;
    const double WaterRate=Config.WaterDrainAmount/Config.WaterDrainIntervalSeconds*WaterMultiplier;
    double Seconds=Maximum;
    if(bInoculated)Seconds=std::min(Seconds,State.FormRemainingSeconds);
    if(bInoculated)Seconds=std::min(Seconds,Config.CapLossIntervalSeconds-State.CapLossElapsedSeconds);
    if(!bInoculated&&State.RecordedCapLoss>0.)Seconds=std::min(Seconds,Config.CapRecoveryIntervalSeconds-State.CapRecoveryElapsedSeconds);
    const bool bRegenEligible=!bInoculated&&(!Config.bNormalRegenRequiresCapLoss||EffectiveMaxHealth()<Config.HealthyMax)&&
        (Config.bRegenWhileStarving||State.Satiety>0.);
    if(bRegenEligible)Seconds=std::min(Seconds,Config.NormalRegenIntervalSeconds-State.NormalRegenElapsedSeconds);
    if(State.Satiety>0.&&SatietyRate>0.)Seconds=std::min(Seconds,State.Satiety/SatietyRate);
    if(State.Water>0.&&WaterRate>0.)Seconds=std::min(Seconds,State.Water/WaterRate);
    if(State.Satiety<=0.&&Config.StarvationDamagePerSecond>0.)Seconds=std::min(Seconds,State.CurrentHealth/Config.StarvationDamagePerSecond);
    if(Activity.GasSpendPerSecond>0.)Seconds=std::min(Seconds,State.Gas/Activity.GasSpendPerSecond);
    if(State.Satiety<=0.&&Config.StarvationDamagePerSecond>0.&&State.StarvationOrdinal>=std::numeric_limits<std::uint64_t>::max()-1)return false;
    if(Seconds<=0.){Out.bGasExhausted=State.Gas<=0.&&Activity.GasSpendPerSecond>0.;return Out.bGasExhausted;}
    const double HealthBefore=State.CurrentHealth;
    Out.AuthoritativeHealthBeforeAdvance=HealthBefore;
    Out.AdvancedSeconds=Seconds;
    if(State.Satiety<=0.){Out.HungerDamageDue=Config.StarvationDamagePerSecond*Seconds;if(Out.HungerDamageDue>0.)++State.StarvationOrdinal;}
    State.Satiety=A01ResourceDrain(State.Satiety,SatietyRate,Seconds);State.Water=A01ResourceDrain(State.Water,WaterRate,Seconds);
    if(State.Form==EDWA01Form::Yeast)
    {
        if(Activity.GasSpendPerSecond>0.)State.Gas=A01ResourceDrain(State.Gas,Activity.GasSpendPerSecond,Seconds);
        else if(!Activity.bSuppressGasRegeneration)State.Gas=std::min(Config.GasMax,State.Gas+Config.GasRegenPerSecond*Seconds);
    }
    if(State.Form==EDWA01Form::Mold&&!Activity.bToolActionActive&&ToolActions.empty())
        State.Corruption=std::min(Config.CorruptionMax,State.Corruption+Config.CorruptionRegenPerSecond*Seconds);
    Out.bGasExhausted=Activity.GasSpendPerSecond>0.&&State.Gas<=0.;
    if(bInoculated&&A01ResourceAdvanceClock(State.CapLossElapsedSeconds,Seconds,Config.CapLossIntervalSeconds))
    {
        Out.bCapLossTick=true;
        if(State.RecordedCapLoss<AllowedNewLoss())State.RecordedCapLoss=std::min(AllowedNewLoss(),State.RecordedCapLoss+Config.CapLossAmount);
        ClampCurrentHealth();
    }
    Out.HealthCeilingBeforeRecovery=EffectiveMaxHealth();
    // Trial simultaneous order is damage -> current-HP regen -> cap recovery.
    if(bRegenEligible&&A01ResourceAdvanceClock(State.NormalRegenElapsedSeconds,Seconds,Config.NormalRegenIntervalSeconds)&&
       (Config.bRegenWhileStarving||State.Satiety>0.))
        Out.CurrentHealthRegenDue=Config.NormalRegenAmount; // Authority caps after all same-instant committed damage.
    else if(!bRegenEligible)State.NormalRegenElapsedSeconds=0.;
    if(!bInoculated&&State.RecordedCapLoss>0.&&A01ResourceAdvanceClock(State.CapRecoveryElapsedSeconds,Seconds,Config.CapRecoveryIntervalSeconds))
    {Out.bCapRecoveryTick=true;State.RecordedCapLoss=std::max(0.,State.RecordedCapLoss-Config.CapRecoveryAmount);}
    if(!bInoculated&&State.RecordedCapLoss<=0.)State.CapRecoveryElapsedSeconds=0.;
    if(bInoculated)
    {
        if(Seconds>=State.FormRemainingSeconds){EndFormFacts();Out.bFormExpired=Out.bFormChanged=true;}
        else State.FormRemainingSeconds-=Seconds;
    }
    Out.HealthCeiling=EffectiveMaxHealth();Out.CurrentHealthClamp=State.CurrentHealth;
    Out.bCurrentHealthClamped=State.CurrentHealth<HealthBefore;return true;
}
bool FDWCharacterA01Runtime::BeginToolAction(std::uint64_t Id,bool Damage,bool Harvest)
{
    if(!bReady||State.CurrentHealth<=0.||!Id||Id<=LastToolActionId)return false;
    LastToolActionId=Id;FToolAction A;A.Id=Id;A.bDamageEligible=Damage;A.bHarvestEligible=Harvest;
    ToolActions.push_back(A);return true;
}
FDWA01CorruptionResult FDWCharacterA01Runtime::ResolveToolHit(std::uint64_t Id,bool Valid,bool Current)
{
    FDWA01CorruptionResult Result;
    if(!bReady||State.CurrentHealth<=0.)return Result;
    auto It=std::find_if(ToolActions.begin(),ToolActions.end(),[Id](const FToolAction& A){return A.Id==Id;});
    if(It==ToolActions.end())return Result;
    auto& A=*It;
    if(!Current){A.bInvalidated=true;A.bEnhanced=false;A.bDecided=true;return Result;}
    if(!Valid||State.Form!=EDWA01Form::Mold||A.bInvalidated||(!A.bDamageEligible&&!A.bHarvestEligible))return Result;
    if(!A.bDecided)
    {
        A.bDecided=true;
        if(State.Corruption>=Config.CorruptionCost)
        {
            State.Corruption-=Config.CorruptionCost;A.bEnhanced=true;Result.bChargedThisCall=true;
            A.DamageMultiplier=A.bDamageEligible?1.+Config.CorruptionDamageBonus:1.;
            A.HarvestMultiplier=A.bHarvestEligible?1.+Config.CorruptionHarvestBonus:1.;
        }
    }
    if(A.bEnhanced){Result.bEnhanced=true;Result.DamageMultiplier=A.DamageMultiplier;Result.HarvestMultiplier=A.HarvestMultiplier;}
    return Result;
}
bool FDWCharacterA01Runtime::EndToolAction(std::uint64_t Id)
{
    const auto It=std::find_if(ToolActions.begin(),ToolActions.end(),[Id](const FToolAction& A){return A.Id==Id;});
    if(It==ToolActions.end())return false;ToolActions.erase(It);return true;
}
void FDWCharacterA01Runtime::CancelToolActions() { ToolActions.clear(); }
