#pragma once
#include <cmath>
#include <cstdint>

namespace DWOpeningHoldSkip
{
 enum class Segment : std::uint8_t { None, Wake, Stretch, Reveal, Encounter };
 enum class Destination : std::uint8_t { None, StretchPrompt, Escape, Approach, Combat };
 inline Destination EndOf(Segment S)
 {
  switch(S){case Segment::Wake:return Destination::StretchPrompt;case Segment::Stretch:return Destination::Escape;
   case Segment::Reveal:return Destination::Approach;case Segment::Encounter:return Destination::Combat;default:return Destination::None;}
 }
 inline double Remaining(double Applied,double Target)
 {return std::isfinite(Applied)&&std::isfinite(Target)&&Applied>=0.&&Target>Applied?Target-Applied:0.;}

 // Monotonic timestamps only. No world clock, UI, input ownership or gameplay facts.
 class Clock
 {
 public:
  void Invalidate(){Pressed=false;Armed=false;Fired=false;Fraction=0.;}
  void NewScene(std::uint64_t Id){Scene=Id;Invalidate();}
  void Release(){Pressed=false;Armed=true;Fired=false;Fraction=0.;}
  void Press(double Now,bool Allowed)
  {
   if(!Allowed||!Armed||Pressed||!std::isfinite(Now))return;
   Start=Now;Pressed=true;Armed=false;Fired=false;Fraction=0.;
  }
  bool Tick(std::uint64_t Id,double Now,double Seconds,bool Allowed,bool Down)
  {
   if(Id!=Scene){NewScene(Id);return false;}
   if(!Allowed||!std::isfinite(Now)||!std::isfinite(Seconds)||Seconds<=0.){Invalidate();return false;}
   if(!Down){Release();return false;}
   if(!Pressed||Fired)return false;
   const double Elapsed=Now-Start;
   if(!std::isfinite(Elapsed)||Elapsed<0.){Invalidate();return false;}
   Fraction=Elapsed/Seconds;
   if(Fraction<1.)return false;
   Fraction=1.;Fired=true;Pressed=false;Armed=false;return true;
  }
  double Progress()const{return Fraction;}
 private:
  std::uint64_t Scene=0;double Start=0.,Fraction=0.;bool Pressed=false,Armed=false,Fired=false;
 };
}
