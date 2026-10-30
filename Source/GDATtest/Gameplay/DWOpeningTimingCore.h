// External candidate: deterministic timing only. Not yet wired into GDATtest.
#pragma once
#include <algorithm>
#include <cmath>
#include <limits>

namespace DWOpeningTiming
{
enum class Phase { Wake, Reveal, Encounter };
enum class Status { Running, Done, Failed };
enum class BeatState { NotStarted, Active, Finished, Failed };

struct Settings
{
 double wake=20.3, wakeHold=0, reveal=16, revealHold=0, revealReturn=2;
 double encounter=10.5, encounterHold=0, quickSeconds=.2;
 bool quickEncounter=false;
 bool Valid() const
 {
  const double all[]={wake,wakeHold,reveal,revealHold,revealReturn,encounter,encounterHold,quickSeconds};
  for(double x:all) if(!std::isfinite(x)||x<0||x>120) return false;
  return wake>=.1&&reveal>=.1&&encounter>=3&&revealReturn<=30&&quickSeconds<=30;
 }
};

struct Beat
{
 BeatState state=BeatState::NotStarted;
 double held=0;
 bool justCompleted=false;
 void Start(){if(state==BeatState::NotStarted)state=BeatState::Active;}
 // Empty lists must invoke Complete after their explicit beat is processed.
 void Complete(){if(state==BeatState::Active){state=BeatState::Finished;justCompleted=true;}}
 void Fail(){state=BeatState::Failed;}
 bool Ready(double hold)const{return state==BeatState::Finished&&held>=hold;}
};

struct Step
{
 Status status=Status::Running;
 double from=0,to=0;             // One unified shot/action logical interval.
 double returnFrom=0,returnTo=0; // Independent game seconds during Reveal return.
 double sinkLogicalSeconds=0;   // Integral on [6.6,6.95], not frame sampling.
 bool instantReturn=false, instantQuick=false;
};

class Clock
{
public:
 explicit Clock(Phase p,const Settings& s):phase(p),settings(s)
 { if(!settings.Valid())status=Status::Failed; }
 Beat beat;
 double Age()const{return age;}
 double ReturnAge()const{return returnAge;}
 double ReturnDuration()const{return settings.revealReturn;}
 Status GetStatus()const{return status;}
 bool CanProceed()const
 {
  const double hold=phase==Phase::Wake?settings.wakeHold:phase==Phase::Reveal?settings.revealHold:settings.encounterHold;
  return status!=Status::Failed&&beat.Ready(hold);
 }
 // Tick once per rendered frame. At most one meaningful boundary is crossed.
 // Surplus time is deliberately discarded: hitch does not skip an action segment.
 Step Advance(double dt,bool paused=false)
 {
  Step out;out.from=out.to=age;out.returnFrom=out.returnTo=returnAge;out.status=status;
  if(status!=Status::Running||paused)return out;
  if(!std::isfinite(dt)||dt<0||beat.state==BeatState::Failed){status=Status::Failed;out.status=status;return out;}
  const double hold=phase==Phase::Wake?settings.wakeHold:phase==Phase::Reveal?settings.revealHold:settings.encounterHold;
  const double priorHeld=beat.held;
  const bool fresh=beat.justCompleted;
  if(beat.state==BeatState::Finished&&!fresh)beat.held+=dt;
  if(fresh)beat.justCompleted=false;
  if(!std::isfinite(beat.held)){status=Status::Failed;out.status=status;return out;}
  const bool ready=beat.Ready(hold);
  const double afterHold=ready?(fresh&&hold>0?0:std::max(0.0,dt-std::max(0.0,hold-priorHeld))):0;
  if(phase==Phase::Wake&&age>=20.3)
  {
   if(ready){status=Status::Done;out.status=status;return out;}
   // Preserve the authored 90-second slow close-up while dialogue is long.
   age+=dt*20.3/settings.wake;out.to=age;return out;
  }
  if(phase==Phase::Reveal&&age>=16)
  {
   if(!ready)return out;
   if(settings.revealReturn==0){out.instantReturn=true;status=Status::Done;out.status=status;return out;}
   returnAge=std::min(settings.revealReturn,returnAge+afterHold);
   out.returnTo=returnAge;
   if(returnAge>=settings.revealReturn)status=Status::Done;
   out.status=status;return out;
  }
  if(phase==Phase::Encounter&&age>=5.9&&!ready)return out;
  if(phase==Phase::Encounter&&age>=10.5){status=Status::Done;out.status=status;return out;}
  const double duration=phase==Phase::Wake?settings.wake:phase==Phase::Reveal?settings.reveal:settings.encounter;
  const double nominal=phase==Phase::Wake?20.3:phase==Phase::Reveal?16:10.5;
  const double scale=nominal/duration;
  double delta=(phase==Phase::Encounter&&age>=5.9?afterHold:dt)*scale+carryLogical;
  carryLogical=0;
  if(phase==Phase::Encounter&&settings.quickEncounter&&ready&&age<5.9)
  {
   if(!quickStarted){quickStarted=true;quickFrom=age;quickElapsed=0;}
   if(settings.quickSeconds==0){delta=5.9-age;out.instantQuick=true;}
   else {quickElapsed=std::min(settings.quickSeconds,quickElapsed+afterHold);delta=quickFrom+(5.9-quickFrom)*(quickElapsed/settings.quickSeconds)-age;}
  }
  const double desired=std::min(nominal,age+std::max(0.0,delta));
  double target=desired;
  const double wakeBounds[]={1,4.4,14.4,19.5,20.3};
  const double revealBounds[]={3.4,6.6,6.95,8,16};
  const double encounterBounds[]={.5,1.1,5.9,7,7.6,9,9.8,10.18,10.22,10.5};
  const double* bounds=phase==Phase::Wake?wakeBounds:phase==Phase::Reveal?revealBounds:encounterBounds;
  const int count=phase==Phase::Wake?5:phase==Phase::Reveal?5:10;
  for(int i=0;i<count;++i)if(bounds[i]>age+1e-9&&bounds[i]<target){target=bounds[i];break;}
  if(phase==Phase::Encounter&&!ready)target=std::min(target,5.9);
  // Carry normal-frame remainder so cue timing does not lose one frame per boundary.
  // Cap hitch remainder; never store it across a dialogue gate or fast transition.
  if(!(phase==Phase::Encounter&&target>=5.9&&!ready)&&!(quickStarted&&age<5.9))
   carryLogical=std::min(std::max(0.0,desired-target),scale/30.0);
  age=target;out.to=age;
  if(phase==Phase::Reveal)out.sinkLogicalSeconds=std::max(0.0,std::min(age,6.95)-std::max(out.from,6.6));
  return out;
 }
private:
 Phase phase;Settings settings;Status status=Status::Running;
 double age=0,returnAge=0,quickFrom=0,quickElapsed=0,carryLogical=0;bool quickStarted=false;
};
}
