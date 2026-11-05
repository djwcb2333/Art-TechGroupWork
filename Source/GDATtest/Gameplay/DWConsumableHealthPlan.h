#pragma once
#include <algorithm>
#include <cmath>
namespace DWConsumableHealth
{
// Pure projection only. Player.Health remains the single current-health authority.
inline bool Prepare(double Before,double Ceiling,double Amount,float& After)
{
    if(!std::isfinite(Before)||!std::isfinite(Ceiling)||!std::isfinite(Amount)||
       Before<=0.||Ceiling<=0.||Before>Ceiling||Amount<0.)return false;
    const double Added=std::min(Amount,Ceiling-Before);
    After=static_cast<float>(Before+Added);
    // Never round the native float upward beyond the effective A01 ceiling.
    if(static_cast<double>(After)>Ceiling)After=std::nextafter(After,0.f);
    return std::isfinite(After)&&After>=Before&&After<=Ceiling;
}
}
