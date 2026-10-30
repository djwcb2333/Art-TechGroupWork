#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// Pure presentation clock; no damage, animation, actor mutation, timers or save writes.
namespace DWSourDeathPresentation
{
struct FIdentity
{
    std::uint64_t World=0,Entity=0,Life=0;
    bool Valid()const{return World&&Entity&&Life;}
    bool operator==(const FIdentity& Other)const{return World==Other.World&&Entity==Other.Entity&&Life==Other.Life;}
};
inline bool AcceptToken(FIdentity Expected,FIdentity Current,std::uint64_t ExpectedGeneration,
                        std::uint64_t SuppliedGeneration,bool bOwned,bool bPaused)
{
    return bOwned&&!bPaused&&Expected.Valid()&&Expected==Current&&ExpectedGeneration!=0&&
        ExpectedGeneration==SuppliedGeneration;
}
inline double SafeSignalTimeout(double Configured,double ClipSeconds,double AbsoluteRate)
{
    const double Timeout=std::isfinite(Configured)?std::clamp(Configured,1.,120.):8.;
    if(!std::isfinite(ClipSeconds)||ClipSeconds<=0.||!std::isfinite(AbsoluteRate)||AbsoluteRate<=0.)return Timeout;
    const double Expected=ClipSeconds/AbsoluteRate;
    return std::max(Timeout,std::isfinite(Expected)?std::clamp(Expected+1.,1.,120.):120.);
}
inline bool SignalTimedOut(double Start,double Now,double Timeout,bool bPaused)
{
    return !bPaused&&std::isfinite(Start)&&std::isfinite(Now)&&std::isfinite(Timeout)&&
        Start>=0.&&Now>=Start&&Timeout>0.&&Now-Start>=Timeout;
}
enum class EPhase : std::uint8_t { Inactive, Animation, Hold, Dissolve, Complete, Canceled };
struct FSample { EPhase Phase=EPhase::Inactive; double Dissolve=0.; };
struct FParameters
{
    double DissolveSeconds=1.15;
    double AfterAnimationHoldSeconds=0.;
    bool Valid()const
    {
        return std::isfinite(DissolveSeconds)&&DissolveSeconds>=.05&&DissolveSeconds<=3600.&&
          std::isfinite(AfterAnimationHoldSeconds)&&AfterAnimationHoldSeconds>=0.&&AfterAnimationHoldSeconds<=60.;
    }
};
class FClock
{
public:
    bool Begin(FIdentity Id,double GameSeconds,FParameters Params)
    {
        if(Active||!Id.Valid()||!Params.Valid()||!std::isfinite(GameSeconds)||GameSeconds<0.||Id==LastStarted)return false;
        Identity=LastStarted=Id;Config=Params;Last=GameSeconds;DissolveStart=-1.;Active=true;return true;
    }
    // Only a verified completed, nonlooping Death state may call this.
    bool AnimationCompleted(FIdentity Current,double GameSeconds,bool Paused)
    {
        if(!Active||DissolveStart>=0.||Paused||!(Current==Identity)||
            !std::isfinite(GameSeconds)||GameSeconds<Last)return false;
        Last=GameSeconds;DissolveStart=GameSeconds;return true;
    }
    FSample Sample(FIdentity Current,double GameSeconds,bool Paused)
    {
        if(!Active)return {};
        if(!(Current==Identity)||!std::isfinite(GameSeconds)||GameSeconds<Last)
        {Active=false;return {EPhase::Canceled,0.};}
        if(!Paused)Last=GameSeconds;
        if(DissolveStart<0.)return {EPhase::Animation,0.};
        const double Elapsed=Last-DissolveStart;
        if(Elapsed<Config.AfterAnimationHoldSeconds)return {EPhase::Hold,0.};
        const double DissolveElapsed=Elapsed-Config.AfterAnimationHoldSeconds;
        if(DissolveElapsed>=Config.DissolveSeconds){Active=false;return {EPhase::Complete,1.};}
        return {EPhase::Dissolve,std::clamp(DissolveElapsed/Config.DissolveSeconds,0.,1.)};
    }
    void Cancel(){Active=false;}
    bool IsActive()const{return Active;}
private:
    FIdentity Identity{},LastStarted{};
    FParameters Config{};
    double DissolveStart=-1.,Last=0.;
    bool Active=false;
};
}
