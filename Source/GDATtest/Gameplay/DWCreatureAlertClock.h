#pragma once
#include <algorithm>
#include <cmath>

/** Pure presentation sample of the host's existing game-time deadline. Never advances that clock. */
namespace DWCreatureAlertClock
{
 inline float SourceSeconds(double Now,double End,double Duration,float SourceLength)
 {
  if(!std::isfinite(Now)||!std::isfinite(End)||!std::isfinite(Duration)||Duration<0.||
   !std::isfinite(SourceLength)||SourceLength<=0.f)return 0.f;
  if(Duration==0.)return SourceLength;
  const double Remaining=End-Now;
  if(!std::isfinite(Remaining))return 0.f;
  const double Progress=std::clamp(1.-Remaining/Duration,0.,1.);
  return static_cast<float>(static_cast<double>(SourceLength)*Progress);
 }
}
