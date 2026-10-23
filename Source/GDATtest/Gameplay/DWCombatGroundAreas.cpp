#include "DWCombatGroundAreas.h"
#include <limits>
namespace
{
bool Positive(double V){return FMath::IsFinite(V)&&V>0.&&V<=MAX_flt;}
bool Fraction(double V){return FMath::IsFinite(V)&&V>=0.&&V<=1.;}
}
bool FDWBowSlowAreaSnapshot::IsValid()const
{return Positive(RadiusCm)&&Positive(DurationSeconds)&&Fraction(SlowFraction);}
bool FDWAlcoholAreaSnapshot::IsValid()const
{return Positive(Damage)&&Positive(RadiusCm)&&Positive(DurationSeconds)&&Positive(TickIntervalSeconds)&&Fraction(SlowFraction);}
bool FDWCombatAreaTargetClock::Start(double First,double Period)
{
    if(NextOrdinal||!FMath::IsFinite(First)||First<0.||!Positive(Period)||!FMath::IsFinite(First+Period)||First+Period<=First)return false;
    AnchorSeconds=First;PeriodSeconds=Period;NextOrdinal=1;return true;
}
double FDWCombatAreaTargetClock::NextSeconds()const
{return NextOrdinal?AnchorSeconds+static_cast<double>(NextOrdinal)*PeriodSeconds:std::numeric_limits<double>::infinity();}
bool FDWCombatAreaTargetClock::ConsumeNext(double Through,double& Due,uint64& Ordinal)
{
    const double Next=NextSeconds();
    if(!NextOrdinal||NextOrdinal==MAX_uint64||!FMath::IsFinite(Through)||!FMath::IsFinite(Next)||Through<Next||
        AnchorSeconds+static_cast<double>(NextOrdinal+1)*PeriodSeconds<=Next)return false;
    Due=Next;Ordinal=NextOrdinal++;return true;
}
