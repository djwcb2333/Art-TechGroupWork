#pragma once
#include "CoreMinimal.h"
#include <cmath>

/** Pure geometry/rate guards shared by the pond and its automation checks. */
namespace DWGatheringPondMath
{
inline bool ContainsFeet(const FVector& Center,const FVector& Feet,double RadiusX,double RadiusY,
    double HeightTolerance,double ExtraRadius=0.)
{
    if(Center.ContainsNaN()||Feet.ContainsNaN()||!std::isfinite(RadiusX)||!std::isfinite(RadiusY)||
        !std::isfinite(HeightTolerance)||!std::isfinite(ExtraRadius)||RadiusX<=0.||RadiusY<=0.||
        HeightTolerance<0.||ExtraRadius<0.)return false;
    const double XRadius=RadiusX+ExtraRadius,YRadius=RadiusY+ExtraRadius;
    if(!std::isfinite(XRadius)||!std::isfinite(YRadius))return false;
    const FVector Delta=Feet-Center;
    if(!std::isfinite(Delta.X)||!std::isfinite(Delta.Y)||!std::isfinite(Delta.Z)||
        FMath::Abs(Delta.Z)>HeightTolerance)return false;
    const double X=Delta.X/XRadius,Y=Delta.Y/YRadius;
    return std::isfinite(X)&&std::isfinite(Y)&&X*X+Y*Y<=1.;
}
inline double RefillRequest(double RatePerSecond,double DeltaSeconds,double Reserve)
{
    if(!std::isfinite(RatePerSecond)||!std::isfinite(DeltaSeconds)||!std::isfinite(Reserve)||
        RatePerSecond<=0.||DeltaSeconds<=0.||Reserve<=0.)return 0.;
    return DeltaSeconds>=Reserve/RatePerSecond?Reserve:RatePerSecond*DeltaSeconds;
}
}
