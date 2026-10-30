#pragma once
#include <cmath>

// Pure, engine-independent calculations shared by the editor candidate and offline checks.
namespace DWResourcePlacementMath
{
inline bool SeparatedXY(double X,double Y,double OtherX,double OtherY,double MinimumCm)
{
    if(!std::isfinite(X)||!std::isfinite(Y)||!std::isfinite(OtherX)||!std::isfinite(OtherY)||
       !std::isfinite(MinimumCm)||MinimumCm<0.)return false;
    const double DX=X-OtherX,DY=Y-OtherY;
    return DX*DX+DY*DY>=MinimumCm*MinimumCm;
}
inline bool AcceptSlope(double UpNormal,double MaxSlopeDegrees)
{
    if(!std::isfinite(UpNormal)||UpNormal< -1.||UpNormal>1.||!std::isfinite(MaxSlopeDegrees)||
       MaxSlopeDegrees<0.||MaxSlopeDegrees>=90.)return false;
    constexpr double Pi=3.14159265358979323846;
    return UpNormal>=std::cos(MaxSlopeDegrees*Pi/180.);
}
// Depth is world-space cm. All height inputs/outputs are actor-local cm.
// Never changes the caller's output on failure.
inline bool TryBedHeight(double Water,double OuterGround,double CrestAboveWater,double ScaleZ,double DepthWorld,double& OutBed)
{
    if(!std::isfinite(Water)||Water<=0.||Water>1000.||!std::isfinite(OuterGround)||OuterGround< -10000.||
       !std::isfinite(CrestAboveWater)||CrestAboveWater<=0.||CrestAboveWater>1000.||
       OuterGround>Water+CrestAboveWater||!std::isfinite(ScaleZ)||ScaleZ<=0.||ScaleZ>100.||
       !std::isfinite(DepthWorld)||DepthWorld<=0.||DepthWorld>10000.)return false;
    const double Bed=Water-DepthWorld/ScaleZ;
    if(!std::isfinite(Bed)||Bed< -10000.||Bed>=Water)return false;
    OutBed=Bed;return true;
}
}
