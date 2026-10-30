#pragma once

// Portable world-map math. Units: world centimeters, view-local pixels, clockwise degrees.
// This header deliberately has no Unreal dependencies so it can be checked without UE.
#include <algorithm>
#include <cmath>

namespace DWWorldMapMath
{
struct Point { double X=0.,Y=0.; };
inline Point Add(Point A,Point B){return {A.X+B.X,A.Y+B.Y};}
inline Point Sub(Point A,Point B){return {A.X-B.X,A.Y-B.Y};}
inline Point Mul(Point A,double S){return {A.X*S,A.Y*S};}
inline double Dot(Point A,Point B){return A.X*B.X+A.Y*B.Y;}
inline bool Valid(Point A){return std::isfinite(A.X)&&std::isfinite(A.Y);}
inline bool Finite(double A){return std::isfinite(A);}
inline double Clamp(double V,double Lo,double Hi){return std::max(Lo,std::min(V,Hi));}
inline Point North(double YawDegrees)
{
    const double R=YawDegrees*0.017453292519943295769;return {std::cos(R),std::sin(R)};
}
inline Point East(double YawDegrees)
{
    const Point N=North(YawDegrees);return {-N.Y,N.X};
}
struct View
{
    Point CaptureCenter,Center,Viewport;
    double CaptureWidth=0.,NorthYaw=0.,Zoom=1.,MinZoom=1.,MaxZoom=8.;
};
inline bool Valid(const View& V)
{
    return Valid(V.CaptureCenter)&&Valid(V.Center)&&Valid(V.Viewport)&&V.Viewport.X>0.&&V.Viewport.Y>0.
        &&Finite(V.CaptureWidth)&&V.CaptureWidth>0.&&Finite(V.NorthYaw)
        &&Finite(V.Zoom)&&Finite(V.MinZoom)&&Finite(V.MaxZoom)&&V.MinZoom>0.
        &&V.MaxZoom>=V.MinZoom&&V.Zoom>=V.MinZoom&&V.Zoom<=V.MaxZoom;
}
inline double UnitsPerPixel(const View& V)
{
    return V.CaptureWidth/(std::min(V.Viewport.X,V.Viewport.Y)*V.Zoom);
}
inline bool PixelToWorld(const View& V,Point Pixel,Point& Out)
{
    if(!Valid(V)||!Valid(Pixel))return false;
    const double Scale=UnitsPerPixel(V);const Point E=East(V.NorthYaw),N=North(V.NorthYaw);
    Out=Add(V.Center,Add(Mul(E,(Pixel.X-V.Viewport.X*.5)*Scale),Mul(N,(V.Viewport.Y*.5-Pixel.Y)*Scale)));
    return Valid(Out);
}
inline bool WorldToPixel(const View& V,Point Position,Point& Out)
{
    if(!Valid(V)||!Valid(Position))return false;
    const double Scale=UnitsPerPixel(V);const Point D=Sub(Position,V.Center);
    Out={V.Viewport.X*.5+Dot(D,East(V.NorthYaw))/Scale,V.Viewport.Y*.5-Dot(D,North(V.NorthYaw))/Scale};
    return Valid(Out);
}
inline bool ContainsPixel(const View& V,Point P)
{return Valid(V)&&Valid(P)&&P.X>=0.&&P.Y>=0.&&P.X<=V.Viewport.X&&P.Y<=V.Viewport.Y;}
inline bool WithinCapture(const View& V,Point World)
{
    if(!Valid(V)||!Valid(World))return false;
    const Point D=Sub(World,V.CaptureCenter);const double Half=V.CaptureWidth*.5;
    return std::abs(Dot(D,East(V.NorthYaw)))<=Half&&std::abs(Dot(D,North(V.NorthYaw)))<=Half;
}
inline void ClampCenter(View& V)
{
    const double PerPixel=UnitsPerPixel(V),Half=V.CaptureWidth*.5;
    const double EastRange=std::max(0.,Half-V.Viewport.X*PerPixel*.5);
    const double NorthRange=std::max(0.,Half-V.Viewport.Y*PerPixel*.5);
    const Point E=East(V.NorthYaw),N=North(V.NorthYaw),D=Sub(V.Center,V.CaptureCenter);
    V.Center=Add(V.CaptureCenter,Add(Mul(E,Clamp(Dot(D,E),-EastRange,EastRange)),Mul(N,Clamp(Dot(D,N),-NorthRange,NorthRange))));
}
inline bool Pan(View& V,Point DeltaPixels)
{
    if(!Valid(V)||!Valid(DeltaPixels))return false;
    View Next=V;const double Scale=UnitsPerPixel(V);
    Next.Center=Add(V.Center,Add(Mul(East(V.NorthYaw),-DeltaPixels.X*Scale),Mul(North(V.NorthYaw),DeltaPixels.Y*Scale)));
    if(!Valid(Next.Center))return false;ClampCenter(Next);V=Next;return true;
}
inline bool ZoomAt(View& V,double Factor,Point Anchor)
{
    if(!Valid(V)||!ContainsPixel(V,Anchor)||!Finite(Factor)||Factor<=0.)return false;
    const double Requested=V.Zoom*Factor;if(!Finite(Requested))return false;
    Point Before,After;if(!PixelToWorld(V,Anchor,Before))return false;
    View Next=V;Next.Zoom=Clamp(Requested,V.MinZoom,V.MaxZoom);
    if(!PixelToWorld(Next,Anchor,After))return false;
    Next.Center=Add(Next.Center,Sub(Before,After));if(!Valid(Next.Center))return false;
    ClampCenter(Next);V=Next;return true;
}
inline bool FitWorldBounds(Point Minimum,Point Maximum,double Yaw,double Padding,Point& Center,double& Width)
{
    if(!Valid(Minimum)||!Valid(Maximum)||!Finite(Yaw)||!Finite(Padding)||Padding<0.
        ||Minimum.X>Maximum.X||Minimum.Y>Maximum.Y)return false;
    const Point E=East(Yaw),N=North(Yaw);
    double MinE=1e300,MaxE=-1e300,MinN=1e300,MaxN=-1e300;
    for(int X=0;X<2;++X)for(int Y=0;Y<2;++Y)
    {
        const Point P{X?Maximum.X:Minimum.X,Y?Maximum.Y:Minimum.Y};
        const double A=Dot(P,E),B=Dot(P,N);MinE=std::min(MinE,A);MaxE=std::max(MaxE,A);MinN=std::min(MinN,B);MaxN=std::max(MaxN,B);
    }
    Width=std::max(MaxE-MinE,MaxN-MinN)+2.*Padding;
    Center=Add(Mul(E,(MinE+MaxE)*.5),Mul(N,(MinN+MaxN)*.5));
    return Valid(Center)&&Finite(Width)&&Width>0.;
}
inline bool MaterialView(const View& V,Point& Scale,Point& Offset)
{
    if(!Valid(V))return false;
    const double P=UnitsPerPixel(V);const Point D=Sub(V.Center,V.CaptureCenter);
    Scale={V.Viewport.X*P/V.CaptureWidth,V.Viewport.Y*P/V.CaptureWidth};
    Offset={.5+Dot(D,East(V.NorthYaw))/V.CaptureWidth,.5-Dot(D,North(V.NorthYaw))/V.CaptureWidth};
    return Valid(Scale)&&Valid(Offset);
}
inline Point WorldDirectionToScreen(Point Direction,double NorthYaw)
{
    if(!Valid(Direction)||!Finite(NorthYaw))return {};
    const Point R{Dot(Direction,East(NorthYaw)),-Dot(Direction,North(NorthYaw))};
    const double L=std::hypot(R.X,R.Y);return L>1e-12?Mul(R,1./L):Point{};
}
template<typename Getter> inline int NearestMarker(const View& V,int Count,Point Pixel,double Radius,Getter GetPosition)
{
    if(!Valid(V)||Count<0||!ContainsPixel(V,Pixel)||!Finite(Radius)||Radius<0.)return -1;
    int BestIndex=-1;double Best=Radius*Radius;
    for(int I=0;I<Count;++I)
    {
        Point Screen;if(!WorldToPixel(V,GetPosition(I),Screen))continue;
        const Point D=Sub(Screen,Pixel);const double Distance=Dot(D,D);
        if(Distance<=Best){Best=Distance;BestIndex=I;}
    }
    return BestIndex;
}
} // namespace DWWorldMapMath
