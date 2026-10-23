#include "DWCombatCoreInjection.h"
#include "DWCombatRules.h"
#include <cmath>
#include <limits>
bool FDWCoreInjectionConfig::IsValid() const
{return std::isfinite(DurationSeconds)&&DurationSeconds>0.&&CoreCost>0&&Vitals.IsValid();}
bool FDWCoreInjectionConfig::Build(const FDWCombatRules& Rules,FDWCoreInjectionConfig& Out,FString& Error)
{
    FDWCoreInjectionConfig Candidate;
    if(!Rules.TryGetNumber(TEXT("form.injection_s"),EDWCombatRuleUnit::Seconds,Candidate.DurationSeconds,Error)||
        !Rules.TryGetCount(TEXT("form.core_cost"),Candidate.CoreCost,Error)||
        !FDWCombatPlayerVitalsConfig::Build(Rules,Candidate.Vitals,Error))return false;
    if(!Candidate.IsValid()){Error=TEXT("Core injection requires positive finite duration, positive integer cost and valid form rules.");return false;}
    Out=Candidate;Error.Reset();return true;
}
bool FDWCoreInjectionPlan::Start(double Seconds)
{
    if(IsPending()||Serial==MAX_uint64||!std::isfinite(Seconds)||Seconds<=0.)return false;
    ++Serial;Duration=Remaining=Seconds;Carry=0.;Phase=EDWCoreInjectionPhase::Injecting;return true;
}
double FDWCoreInjectionPlan::GetRemainingSeconds() const
{return IsPending()?Remaining-Carry:0.;}
double FDWCoreInjectionPlan::GetNextBoundarySeconds() const
{return IsPending()?GetRemainingSeconds():std::numeric_limits<double>::infinity();}
bool FDWCoreInjectionPlan::Advance(double Seconds)
{
    if(!IsPending()||!std::isfinite(Seconds)||Seconds<0.)return false;
    if(IsReady())return Seconds==0.;
    if(Seconds>=GetRemainingSeconds())
    {Remaining=Carry=0.;Phase=EDWCoreInjectionPhase::Ready;return true;}
    const double Delta=-Seconds-Carry,Next=Remaining+Delta;
    Carry=(Next-Remaining)-Delta;Remaining=Next;return true;
}
bool FDWCoreInjectionPlan::Commit()
{if(!IsReady())return false;Phase=EDWCoreInjectionPhase::Committed;return true;}
void FDWCoreInjectionPlan::Cancel()
{if(!IsPending())return;Phase=EDWCoreInjectionPhase::Cancelled;Remaining=Carry=0.;}
