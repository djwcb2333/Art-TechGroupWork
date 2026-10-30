#pragma once
// Portable single-flight capture scheduler. It does not create RTs or prove GPU completion.
#include <cmath>
#include <cstdint>

namespace DWMapMotionSchedule
{
struct Request{std::uint64_t Token=0;double Alpha=0.;};
class Scheduler
{
public:
    explicit Scheduler(double Hz=30.) : Interval(std::isfinite(Hz)&&Hz>0.?1./Hz:1./30.) {}
    bool Begin(double InitialAlpha,double Now)
    {
        if(Pending||!std::isfinite(InitialAlpha)||InitialAlpha<0.||InitialAlpha>1.||!std::isfinite(Now))return false;
        ++Generation;HasCommit=false;LastRequest=Now-Interval;
        CommittedAlpha=InitialAlpha;Direction=0;return true;
    }
    // A change in direction rejects any in-flight frame from the old animation.
    void SetDirection(int NewDirection)
    {
        NewDirection=NewDirection<0?-1:(NewDirection>0?1:0);
        if(NewDirection!=Direction){Direction=NewDirection;++Generation;}
    }
    bool TryRequest(double DesiredAlpha,double Now,bool Force,Request& Out)
    {
        if(Pending||!std::isfinite(DesiredAlpha)||DesiredAlpha<0.||DesiredAlpha>1.||!std::isfinite(Now)||
           !std::isfinite(LastRequest)||Now<LastRequest||(!Force&&Now-LastRequest+1e-9<Interval))return false;
        if(HasCommit&&std::abs(DesiredAlpha-CommittedAlpha)<1e-12)return false;
        Pending=true;LastRequest=Now;PendingToken=++NextToken;PendingGeneration=Generation;
        PendingAlpha=DesiredAlpha;Out={PendingToken,PendingAlpha};return true;
    }
    // Call only after the renderer retired this exact request. Succeeded=false
    // means retired with failure; it must not mean "still in flight".
    bool Complete(std::uint64_t Token,bool Succeeded,double& OutAlpha)
    {
        if(!Pending||Token!=PendingToken)return false;
        Pending=false;if(PendingGeneration!=Generation||!Succeeded)return false;
        CommittedAlpha=PendingAlpha;HasCommit=true;OutAlpha=CommittedAlpha;return true;
    }
    // Keep pending until the renderer retires the actual request; this prevents
    // reusing the same capture/target while a canceled render is still in flight.
    void Cancel(){++Generation;Direction=0;}
    bool IsPending() const{return Pending;}
    // A pending physical operation may already be obsolete after reversal/cancel.
    // The caller can retire it only after its preceding GPU/resource fence completes.
    bool IsPendingCurrent() const{return Pending&&PendingGeneration==Generation;}
    double GetCommittedAlpha() const{return CommittedAlpha;}
private:
    double Interval,LastRequest=0.,CommittedAlpha=0.,PendingAlpha=0.;
    std::uint64_t Generation=0,NextToken=0,PendingToken=0,PendingGeneration=0;
    int Direction=0;bool Pending=false,HasCommit=false;
};
}
