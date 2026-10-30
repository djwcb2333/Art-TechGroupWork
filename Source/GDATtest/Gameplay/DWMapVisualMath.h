#pragma once
#include <algorithm>
#include <cmath>
#include <vector>

// Pure UI geometry and the same analytic damped-spring model used by DWUIBounceComponent.
// Coordinates are viewport-local Slate units; this header does not project world positions.
namespace DWMapVisualMath
{
    struct Point {double X=0,Y=0;};
    struct Rect {double Left=0,Top=0,Right=0,Bottom=0;};
    inline bool Finite(Point P){return std::isfinite(P.X)&&std::isfinite(P.Y);}
    inline bool Finite(Rect R){return Finite(Point{R.Left,R.Top})&&Finite(Point{R.Right,R.Bottom});}
    inline double Lerp(double A,double B,double T){return A+(B-A)*T;}
    inline bool Morph(Rect Mini,Point View,double Progress,Rect& Out,double& Radius)
    {
        if(!Finite(Mini)||!Finite(View)||!std::isfinite(Progress)||View.X<=0||View.Y<=0||
           Mini.Right<=Mini.Left||Mini.Bottom<=Mini.Top)return false;
        const double P=std::clamp(Progress,0.,1.);
        // An actual minimap is circular: remove any footer/elliptical extension.
        const double Side=std::min({Mini.Right-Mini.Left,Mini.Bottom-Mini.Top,View.X,View.Y});
        const double CX=(Mini.Left+Mini.Right)*.5,CY=(Mini.Top+Mini.Bottom)*.5;
        const double L=std::clamp(CX-Side*.5,0.,View.X-Side),T=std::clamp(CY-Side*.5,0.,View.Y-Side);
        Out={Lerp(L,0.,P),Lerp(T,0.,P),Lerp(L+Side,View.X,P),Lerp(T+Side,View.Y,P)};
        Radius=Lerp(Side*.5,0.,P);Radius=std::min(Radius,std::min(Out.Right-Out.Left,Out.Bottom-Out.Top)*.5);
        return Finite(Out)&&std::isfinite(Radius);
    }
    inline std::vector<Point> Outline(Rect R,double Radius,int StepsPerCorner=12)
    {
        if(!Finite(R)||!std::isfinite(Radius)||R.Right<=R.Left||R.Bottom<=R.Top)return {};
        const int N=std::clamp(StepsPerCorner,2,32);
        const double Rad=std::clamp(Radius,0.,std::min(R.Right-R.Left,R.Bottom-R.Top)*.5);
        if(Rad<1.e-6)return {{R.Left,R.Top},{R.Right,R.Top},{R.Right,R.Bottom},{R.Left,R.Bottom}};
        constexpr double PiValue=3.14159265358979323846;
        const Point Centers[]={{R.Left+Rad,R.Top+Rad},{R.Right-Rad,R.Top+Rad},{R.Right-Rad,R.Bottom-Rad},{R.Left+Rad,R.Bottom-Rad}};
        const double Start[]={PiValue,1.5*PiValue,0.,.5*PiValue};
        std::vector<Point> P;P.reserve(4*(N+1));
        for(int Corner=0;Corner<4;++Corner)for(int I=0;I<=N;++I)
        {
            const double A=Start[Corner]+.5*PiValue*double(I)/N;
            P.push_back({Centers[Corner].X+Rad*std::cos(A),Centers[Corner].Y+Rad*std::sin(A)});
        }
        return P;
    }
    inline bool UV(Point P,Rect MaterialFrame,Point& Out)
    {
        if(!Finite(P)||!Finite(MaterialFrame)||MaterialFrame.Right<=MaterialFrame.Left||MaterialFrame.Bottom<=MaterialFrame.Top)return false;
        Out={(P.X-MaterialFrame.Left)/(MaterialFrame.Right-MaterialFrame.Left),
             (P.Y-MaterialFrame.Top)/(MaterialFrame.Bottom-MaterialFrame.Top)};
        return Finite(Out);
    }
    inline bool InsideRounded(Point P,Rect R,double Radius)
    {
        if(!Finite(P)||!Finite(R)||!std::isfinite(Radius)||R.Right<=R.Left||R.Bottom<=R.Top||
           P.X<R.Left||P.X>R.Right||P.Y<R.Top||P.Y>R.Bottom)return false;
        const double Q=std::clamp(Radius,0.,std::min(R.Right-R.Left,R.Bottom-R.Top)*.5);
        const double X=std::clamp(P.X,R.Left+Q,R.Right-Q),Y=std::clamp(P.Y,R.Top+Q,R.Bottom-Q);
        return (P.X-X)*(P.X-X)+(P.Y-Y)*(P.Y-Y)<=Q*Q+1.e-8;
    }
    inline bool PlayerPixelsToLocal(Rect PlayerPixels,Rect ViewPixels,Point LocalSize,Rect& Out)
    {
        if(!Finite(PlayerPixels)||!Finite(ViewPixels)||!Finite(LocalSize)||LocalSize.X<=0.||LocalSize.Y<=0.||
           PlayerPixels.Right<=PlayerPixels.Left||PlayerPixels.Bottom<=PlayerPixels.Top||
           ViewPixels.Right<=ViewPixels.Left||ViewPixels.Bottom<=ViewPixels.Top)return false;
        // Permit at most one physical pixel of viewport rounding, never a different player's rectangle.
        if(ViewPixels.Left<PlayerPixels.Left-1.||ViewPixels.Top<PlayerPixels.Top-1.||
           ViewPixels.Right>PlayerPixels.Right+1.||ViewPixels.Bottom>PlayerPixels.Bottom+1.)return false;
        const double SX=LocalSize.X/(PlayerPixels.Right-PlayerPixels.Left),SY=LocalSize.Y/(PlayerPixels.Bottom-PlayerPixels.Top);
        Out={(ViewPixels.Left-PlayerPixels.Left)*SX,(ViewPixels.Top-PlayerPixels.Top)*SY,
             (ViewPixels.Right-PlayerPixels.Left)*SX,(ViewPixels.Bottom-PlayerPixels.Top)*SY};
        return Finite(Out)&&Out.Right>Out.Left&&Out.Bottom>Out.Top;
    }
    inline double Bounce(double Time,double Amplitude,double Frequency,double Damping)
    {
        if(!std::isfinite(Time)||!std::isfinite(Amplitude)||!std::isfinite(Frequency)||!std::isfinite(Damping))return 1.;
        const double T=std::clamp(Time,0.,10.),A=std::clamp(Amplitude,0.,.4),F=std::clamp(Frequency,.1,30.),D=std::clamp(Damping,.05,3.);
        constexpr double PiValue=3.14159265358979323846;const double Omega=2.*PiValue*F;
        // Exactly the DWUIBounceComponent spring solution with position=target, velocity=A*omega.
        const double V=A*Omega;
        if(D<.9999){const double W=Omega*std::sqrt(1.-D*D);return 1.+std::exp(-D*Omega*T)*V*std::sin(W*T)/W;}
        if(D<=1.0001)return 1.+std::exp(-Omega*T)*V*T;
        const double Root=std::sqrt(D*D-1.),R1=-Omega*(D-Root),R2=-Omega*(D+Root);
        return 1.+V*(std::exp(R1*T)-std::exp(R2*T))/(R1-R2);
    }
}
