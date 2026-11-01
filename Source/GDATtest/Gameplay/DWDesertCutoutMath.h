#pragma once
#include <algorithm>
#include <cmath>
// Pure presentation math. No UE world, collision, player state, rendering or disk IO.
namespace DWDesertCutoutMath
{
struct Frame{double X=.5,Y=.5,RadiusY=0.,Aspect=1.,FeatherY=0.;};
inline bool Make(double X,double Y,double RightX,double RightY,double Width,double Height,double RadiusCm,double FeatherCm,Frame& Out)
{
 for(double V:{X,Y,RightX,RightY,Width,Height,RadiusCm,FeatherCm})if(!std::isfinite(V))return false;
 if(Width<=0.||Height<=0.||RadiusCm<=0.||FeatherCm<0.)return false;
 const double Pixels=std::hypot(RightX-X,RightY-Y);if(Pixels<=.001)return false;
 Frame F{X/Width,Y/Height,Pixels/Height,Width/Height,Pixels/Height*std::clamp(FeatherCm/RadiusCm,0.,1.)};
 if(F.X<0.||F.X>1.||F.Y<0.||F.Y>1.)return false;Out=F;return true;
}
inline double CircleMask(const Frame& F,double U,double V,double Pow=27.385183334)
{
 const double D=std::hypot((U-F.X)*F.Aspect,V-F.Y),Feather=std::max(F.FeatherY,1.e-6);
 double T=std::clamp((D-(F.RadiusY-Feather))/Feather,0.,1.);T=T*T*(3.-2.*T);
 return std::pow(T,std::max(.1,Pow));
}
inline double GatedMask(double Old,double Circle,double Eligible,double Enabled,bool InFront,bool AboveFloor)
{
 if(!InFront||!AboveFloor)return Old;
 return Old*(1.-std::clamp(Eligible,0.,1.)*std::clamp(Enabled,0.,1.)*(1.-Circle));
}
inline bool CanAcquire(double Current){return std::isfinite(Current)&&Current==0.;}
inline double ReleaseValue(double Current,double Previous){return Current==1.?Previous:Current;}
}
