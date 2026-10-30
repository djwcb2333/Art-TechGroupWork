#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

// No gameplay side effects: the integration owns validation and the one drop transaction.
namespace DWDemolitionWork
{
enum class EState { Idle, Working, Ready, Committed };
struct FWork
{
    EState State=EState::Idle;
    uint64_t Target=0;
    uint64_t Tool=0;
    double Required=0.;
    double Done=0.;

    bool Begin(uint64_t TargetId, uint64_t ToolId, double RequiredWork)
    {
        if(State!=EState::Idle || !TargetId || !ToolId || !std::isfinite(RequiredWork) || RequiredWork<=0.) return false;
        Target=TargetId; Tool=ToolId; Required=RequiredWork; Done=0.; State=EState::Working; return true;
    }
    bool Advance(uint64_t TargetId,uint64_t ToolId,double Delta,double Rate,bool StillValid)
    {
        if(State!=EState::Working)return false;
        if(!StillValid || TargetId!=Target || ToolId!=Tool || !std::isfinite(Delta) || Delta<0. || !std::isfinite(Rate) || Rate<=0.)
        {Reset();return false;}
        // Saturating arithmetic avoids non-finite sums and does not expose completed work before the transaction.
        const double Remaining=Required-Done;
        if(Delta>=Remaining/Rate){Done=Required;State=EState::Ready;return true;}
        Done+=Delta*Rate;return false;
    }
    double Progress() const {return State==EState::Idle?0.:std::clamp(Done/Required,0.,1.);}
    // Caller may set Committed only after it has committed both building removal and one valid pickup.
    bool Commit()
    {if(State!=EState::Ready)return false;State=EState::Committed;return true;}
    // A failed prepare, release, move, hit, target/tool change or save/load discards partial progress.
    void Reset(){State=EState::Idle;Target=Tool=0;Required=Done=0.;}
};
}
