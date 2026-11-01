#pragma once
#include <algorithm>
#include <cmath>

// Presentation only. No world time, pause authority, rendering or save operations.
namespace DWMapOpenPhases
{
struct Sample {double Mini=1.,Shell=0.,Full=0.;bool NeedCapture=false,Complete=false,Closed=true;};
inline double Ease(double T){T=std::clamp(T,0.,1.);return T*T*(3.-2.*T);}
class Timeline
{
 double Time=0.,MiniSeconds=.14,ShellSeconds=.32,FullSeconds=.18;
public:
 void Reset(double Mini,double Shell,double Full)
 {Time=0.;MiniSeconds=std::isfinite(Mini)?std::clamp(Mini,.01,5.):.14;ShellSeconds=std::isfinite(Shell)?std::clamp(Shell,.01,5.):.32;FullSeconds=std::isfinite(Full)?std::clamp(Full,.01,5.):.18;}
 void Advance(double Delta,bool Closing,bool Ready)
 {
  if(!std::isfinite(Delta)||Delta<=0.)return;
  const double End=MiniSeconds+ShellSeconds+FullSeconds;
  Time=std::clamp(Time+(Closing?-1.:1.)*std::min(Delta,.1),0.,Closing||Ready?End:MiniSeconds+ShellSeconds);
 }
 Sample Read(bool Ready) const
 {
  Sample S;S.Mini=1.-Ease(Time/MiniSeconds);S.Shell=Ease((Time-MiniSeconds)/ShellSeconds);
  S.NeedCapture=Time>=MiniSeconds+ShellSeconds-1.e-12;
  S.Full=Ready?Ease((Time-MiniSeconds-ShellSeconds)/FullSeconds):0.;
  S.Complete=Ready&&Time>=MiniSeconds+ShellSeconds+FullSeconds-1.e-12;S.Closed=Time<=0.;return S;
 }
};
struct Circle {double X=0.,Y=0.,Radius=0.;};
inline bool RevealCircle(double MiniX,double MiniY,double MiniW,double MiniH,double ViewW,double ViewH,double Progress,Circle& Out)
{
 for(double V:{MiniX,MiniY,MiniW,MiniH,ViewW,ViewH,Progress})if(!std::isfinite(V))return false;
 if(MiniW<=0.||MiniH<=0.||ViewW<=0.||ViewH<=0.)return false;
 const double Side=std::min({MiniW,MiniH,ViewW,ViewH}),P=std::clamp(Progress,0.,1.);
 const double CX=std::clamp(MiniX+MiniW*.5,Side*.5,ViewW-Side*.5),CY=std::clamp(MiniY+MiniH*.5,Side*.5,ViewH-Side*.5);
 // Mask outline uses 48 arc segments. Its inscribed polygon, not only the ideal circle,
 // must cover the final viewport corners (including one Slate unit of rounding margin).
 constexpr double Pi=3.14159265358979323846;
 const double EndRadius=std::hypot(ViewW,ViewH)*.5/std::cos(Pi/48.)+1.;
 Out={CX+(ViewW*.5-CX)*P,CY+(ViewH*.5-CY)*P,Side*.5+(EndRadius-Side*.5)*P};return std::isfinite(Out.X)&&std::isfinite(Out.Y)&&std::isfinite(Out.Radius);
}
inline int CaptureResolution(double Width,double Height,int Preferred)
{
 const int Base=std::clamp(Preferred,256,2048);
 if(!std::isfinite(Width)||!std::isfinite(Height)||Width<=0.||Height<=0.)return Base;
 const double Needed=std::ceil(std::min(Width,Height)/256.)*256.;
 return int(std::clamp(std::max(double(Base),Needed),256.,2048.));
}
}
