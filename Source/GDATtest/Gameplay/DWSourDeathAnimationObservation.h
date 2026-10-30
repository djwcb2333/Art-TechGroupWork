#pragma once
#include <cmath>
#include <cstdint>

// Pure observation of one non-looping, forward-only sequence-player episode.
// Caller resets on identity/state/asset/player change and never samples while paused.
namespace DWSourDeathAnimationObservation
{
inline bool ReadyForDelivery(std::uint64_t CurrentFrame,std::uint64_t EndFrame,bool Paused)
{return !Paused&&CurrentFrame>EndFrame;}
struct FTracker
{
    bool Started=false, Progressed=false, Complete=false, Invalid=false;
    double Last=0.;
    void Reset(){*this=FTracker{};}
    bool Sample(double Length,double Current,double Previous,double Delta,double Remaining,bool PreviousValid)
    {
        constexpr double StartEpsilon=1.e-5, ProgressEpsilon=1.e-8;
        if(Invalid||Complete)return Complete;
        if(!std::isfinite(Length)||!std::isfinite(Current)||!std::isfinite(Previous)||
           !std::isfinite(Delta)||!std::isfinite(Remaining)||Length<=StartEpsilon||
           Current<0.||Current>Length+StartEpsilon||Previous<0.||Delta<0.)
        {Invalid=true;return false;}
        if(!PreviousValid)return false;
        if(!Started)
        {
            // First observed asset tick must start at zero, including a one-frame jump.
            if(Previous>StartEpsilon)return false;
            Started=true;Last=Previous;
        }
        if(Current+StartEpsilon<Last||Current+StartEpsilon<Previous)
        {Invalid=true;return false;}
        if(Current>Last+ProgressEpsilon&&Delta>0.)Progressed=true;
        Last=Current;
        if(Progressed&&Current>=Length&&Remaining<=0.)Complete=true;
        return Complete;
    }
};
}
