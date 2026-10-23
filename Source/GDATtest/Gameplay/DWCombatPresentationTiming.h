#pragma once
#include "CoreMinimal.h"

// Pure presentation mapping. Gameplay owns time; this utility never ticks or commits an event.
namespace DWCombatPresentationTiming
{
inline double GetNormalizedProgress(double T,double Begin,double End)
{
    if(!FMath::IsFinite(T)||!FMath::IsFinite(Begin)||!FMath::IsFinite(End)||End<=Begin)return 0.;
    return FMath::Clamp((T-Begin)/(End-Begin),0.,1.);
}
inline double MapSegment(double T,double FromBegin,double FromEnd,double ToBegin,double ToEnd)
{
    if(!FMath::IsFinite(ToBegin)||!FMath::IsFinite(ToEnd))return 0.;
    return FMath::Lerp(ToBegin,ToEnd,GetNormalizedProgress(T,FromBegin,FromEnd));
}
}
