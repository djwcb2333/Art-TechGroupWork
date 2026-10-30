#pragma once
// Portable calculations for UI/map capture. No UE, renderer, disk IO or game input.
#include <algorithm>
#include <cmath>

namespace DWMapMotionMath
{
struct P2{double X=0.,Y=0.;};
struct P3{double X=0.,Y=0.,Z=0.;};
struct Rect{double X=0.,Y=0.,W=0.,H=0.;};
inline bool Finite(double V){return std::isfinite(V);}
inline bool Valid(P2 V){return Finite(V.X)&&Finite(V.Y);}
inline bool Valid(P3 V){return Finite(V.X)&&Finite(V.Y)&&Finite(V.Z);}
inline bool Valid(Rect R){return Finite(R.X)&&Finite(R.Y)&&Finite(R.W)&&Finite(R.H)&&R.W>0.&&R.H>0.&&Finite(R.X+R.W)&&Finite(R.Y+R.H);}
inline double Dot(P3 A,P3 B){return A.X*B.X+A.Y*B.Y+A.Z*B.Z;}
inline P3 Sub(P3 A,P3 B){return {A.X-B.X,A.Y-B.Y,A.Z-B.Z};}
inline P3 Lerp(P3 A,P3 B,double T){return {A.X+(B.X-A.X)*T,A.Y+(B.Y-A.Y)*T,A.Z+(B.Z-A.Z)*T};}
inline double WrapDelta(double A,double B){double D=std::fmod(B-A+180.,360.);if(D<0.)D+=360.;return D-180.;}
inline double Smooth(double T){T=std::max(0.,std::min(1.,T));return T*T*(3.-2.*T);}
struct View
{
    P3 Center,Right,Up;
    Rect Frame;
    double UnitsPerPixel=0.,NorthScale=1.,Yaw=0.,Pitch=-90.;
};
inline bool Valid(const View& V)
{
    const double R=Dot(V.Right,V.Right),U=Dot(V.Up,V.Up),RU=Dot(V.Right,V.Up);
    return Valid(V.Center)&&Valid(V.Right)&&Valid(V.Up)&&Valid(V.Frame)&&Finite(V.UnitsPerPixel)&&V.UnitsPerPixel>0.
        &&Finite(V.NorthScale)&&V.NorthScale>=.5&&V.NorthScale<=1.000001&&Finite(V.Yaw)&&Finite(V.Pitch)
        &&std::abs(R-1.)<.0001&&std::abs(U-1.)<.0001&&std::abs(RU)<.0001;
}
inline void Basis(double Yaw,double Pitch,P3& Right,P3& Up,double& NorthScale)
{
    constexpr double K=.017453292519943295769;const double Y=Yaw*K,P=Pitch*K;
    Right={-std::sin(Y),std::cos(Y),0.};
    Up={-std::sin(P)*std::cos(Y),-std::sin(P)*std::sin(Y),std::cos(P)};
    NorthScale=std::max(.5,std::abs(std::sin(P)));
}
inline bool Make(P3 Center,Rect Frame,double UnitsPerPixel,double Yaw,double Pitch,View& Out)
{
    if(!Valid(Center)||!Valid(Frame)||!Finite(UnitsPerPixel)||UnitsPerPixel<=0.||!Finite(Yaw)||!Finite(Pitch)||Pitch < -90.||Pitch > -45.)return false;
    View V;V.Center=Center;V.Frame=Frame;V.UnitsPerPixel=UnitsPerPixel;V.Yaw=Yaw;V.Pitch=Pitch;
    Basis(Yaw,Pitch,V.Right,V.Up,V.NorthScale);if(!Valid(V))return false;Out=V;return true;
}
// Eased progress is supplied once by UI, and the same value drives its mask.
inline bool Interpolate(const View& Start,const View& End,Rect CurrentFrame,double Eased,View& Out)
{
    if(!Valid(Start)||!Valid(End)||!Valid(CurrentFrame)||!Finite(Eased)||Eased<0.||Eased>1.)return false;
    const double Yaw=Start.Yaw+WrapDelta(Start.Yaw,End.Yaw)*Eased;
    const double Pitch=Start.Pitch+(End.Pitch-Start.Pitch)*Eased;
    // Geometric interpolation avoids rapid zoom change when whole-world scale is large.
    const double Scale=std::exp(std::log(Start.UnitsPerPixel)*(1.-Eased)+std::log(End.UnitsPerPixel)*Eased);
    return Make(Lerp(Start.Center,End.Center,Eased),CurrentFrame,Scale,Yaw,Pitch,Out);
}
inline bool WorldToScreen(const View& V,P3 World,P2& Out)
{
    if(!Valid(V)||!Valid(World))return false;const P3 D=Sub(World,V.Center);
    Out={V.Frame.X+V.Frame.W*.5+Dot(D,V.Right)/V.UnitsPerPixel,
         V.Frame.Y+V.Frame.H*.5-Dot(D,V.Up)/(V.UnitsPerPixel*V.NorthScale)};
    return Valid(Out);
}
// Existing full-map View uses captureWidth / (min(viewportW,viewportH)*zoom).
inline bool FullMapUnits(double CaptureWidth,Rect Frame,double Zoom,double& Out)
{
    if(!Finite(CaptureWidth)||CaptureWidth<=0.||!Valid(Frame)||!Finite(Zoom)||Zoom<=0.)return false;
    Out=CaptureWidth/(std::min(Frame.W,Frame.H)*Zoom);return Finite(Out)&&Out>0.;
}
// Square orthographic RT at View.Center. q=(UV-.5)*UVScale+.5.
inline bool CaptureScale(const View& V,double& OrthoWidth,P2& UVScale)
{
    if(!Valid(V))return false;
    const double X=V.Frame.W*V.UnitsPerPixel,Y=V.Frame.H*V.UnitsPerPixel*V.NorthScale;
    OrthoWidth=std::max(X,Y);if(!Finite(OrthoWidth)||OrthoWidth<=0.)return false;
    UVScale={X/OrthoWidth,Y/OrthoWidth};return Valid(UVScale);
}
// Match the real mini shader's q=(uv.x+dx,.5+(uv.y-.5+dy)*northScale)
// using the full-map shader's q=(uv-.5)*scale+offset.
inline bool MiniMaterialMapping(double NorthScale,P2 PlayerOffset,P2& Scale,P2& Offset)
{
    if(!Finite(NorthScale)||NorthScale<.5||NorthScale>1.000001||!Valid(PlayerOffset))return false;
    Scale={1.,NorthScale};Offset={.5+PlayerOffset.X,.5+NorthScale*PlayerOffset.Y};
    return Valid(Scale)&&Valid(Offset);
}
enum class TargetKind{Invalid,Inside,Outside};
struct Target {TargetKind Kind=TargetKind::Invalid;P2 Position,Direction;bool Behind=false;};
// Clip coordinates use conventional screen-up Y. Viewport and Safe use the same
// player-local Slate units; caller converts from physical pixels/DPI exactly once.
inline Target ProjectTarget(double ClipX,double ClipY,double ClipW,Rect Viewport,Rect Safe,P2 PreviousDirection={0.,-1.},
    bool UseCameraDepth=false,double CameraDepth=0.)
{
    Target Out;if(!Finite(ClipX)||!Finite(ClipY)||!Finite(ClipW)||!Valid(Viewport)||!Valid(Safe)||!Valid(PreviousDirection))return Out;
    if(UseCameraDepth&&!Finite(CameraDepth))return Out;
    if(Safe.X<Viewport.X||Safe.Y<Viewport.Y||Safe.X+Safe.W>Viewport.X+Viewport.W||Safe.Y+Safe.H>Viewport.Y+Viewport.H)return Out;
    const double Cx=Safe.X+Safe.W*.5,Cy=Safe.Y+Safe.H*.5;
    const bool Front=ClipW>1e-8&&(!UseCameraDepth||CameraDepth>1e-8);
    Out.Behind=UseCameraDepth?CameraDepth<0.:ClipW<0.;
    P2 D;
    if(std::abs(ClipW)>1e-8)
    {
        // abs(w) preserves the physical camera-plane side for targets behind.
        const double Div=std::abs(ClipW);
        const P2 P{Viewport.X+Viewport.W*.5+(ClipX/Div)*Viewport.W*.5,
                   Viewport.Y+Viewport.H*.5-(ClipY/Div)*Viewport.H*.5};
        if(!Valid(P))return Out;
        if(Front&&P.X>=Safe.X&&P.X<=Safe.X+Safe.W&&P.Y>=Safe.Y&&P.Y<=Safe.Y+Safe.H)
        {Out.Kind=TargetKind::Inside;Out.Position=P;return Out;}
        // For behind-camera targets use camera-plane direction, independent of
        // an asymmetric safe rectangle; centered-behind must use stable fallback.
        D=Out.Behind?P2{ClipX*Viewport.W*.5,-ClipY*Viewport.H*.5}:P2{P.X-Cx,P.Y-Cy};
    }
    else D={ClipX*Viewport.W*.5,-ClipY*Viewport.H*.5};
    double L=std::hypot(D.X,D.Y);
    if(!Finite(L))return Out;
    if(L<1e-8){D=PreviousDirection;L=std::hypot(D.X,D.Y);if(L<1e-8){D={0.,-1.};L=1.;}}
    D={D.X/L,D.Y/L};
    const double Tx=std::abs(D.X)>1e-12?(Safe.W*.5)/std::abs(D.X):1e300;
    const double Ty=std::abs(D.Y)>1e-12?(Safe.H*.5)/std::abs(D.Y):1e300;
    const double T=std::min(Tx,Ty);
    Out.Kind=TargetKind::Outside;Out.Direction=D;Out.Position={Cx+D.X*T,Cy+D.Y*T};return Out;
}
} // namespace DWMapMotionMath
