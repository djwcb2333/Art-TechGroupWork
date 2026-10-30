#include "DWA01AbilityPlan.h"
#include <algorithm>
#include <cmath>

namespace
{
bool A01AbilityPositive(double V){return std::isfinite(V)&&V>0.;}
bool A01AbilityNonnegative(double V){return std::isfinite(V)&&V>=0.;}
bool A01AbilityNormalize(double X,double Y,double& OutX,double& OutY)
{
    const double Length=std::hypot(X,Y);
    if(!std::isfinite(Length)||Length<=0.)return false;
    OutX=X/Length;OutY=Y/Length;return true;
}
}
double FDWA01AbilityConfig::DashDurationSeconds() const
{return DashAccelerationSeconds+DashHoldSeconds+DashDecelerationSeconds;}
double FDWA01AbilityConfig::DashDistanceCm(double Walk) const
{return Walk*DashSpeedMultiplier*(DashHoldSeconds+.5*DashAccelerationSeconds+.5*DashDecelerationSeconds);}
bool FDWA01AbilityConfig::IsValid(std::string* Error) const
{
    for(double V:{HoldThresholdSeconds,SprintSpeedMultiplier,DashSpeedMultiplier,DashAccelerationSeconds,DashHoldSeconds,DashDecelerationSeconds})
        if(!A01AbilityPositive(V)){if(Error)*Error="Ability thresholds, speeds and all three phase durations must be positive and finite.";return false;}
    if(!A01AbilityNonnegative(DashCooldownSeconds)||!A01AbilityPositive(DashDurationSeconds())||!A01AbilityPositive(DashDistanceCm(1.)))
    {if(Error)*Error="Invalid ability cooldown or derived duration/distance.";return false;}
    if(Error)Error->clear();return true;
}
bool FDWA01AbilityPlan::Initialize(const FDWA01AbilityConfig& C,std::string* Error)
{if(!C.IsValid(Error))return false;Config=C;State={};bReady=true;return true;}
bool FDWA01AbilityPlan::IsDashing() const
{return State.Phase==EDWA01AbilityPhase::DashAcceleration||State.Phase==EDWA01AbilityPhase::DashHold||State.Phase==EDWA01AbilityPhase::DashDeceleration;}
bool FDWA01AbilityPlan::Press()
{
    if(!bReady||State.bPhysicalDown)return false;
    State.bPhysicalDown=true;
    if(State.bReleaseRequired||State.Phase!=EDWA01AbilityPhase::Idle)
    {State.bReleaseRequired=true;return false;}
    State.Phase=EDWA01AbilityPhase::Pending;State.PhaseElapsedSeconds=0.;State.StartRequest=EDWA01AbilityStart::None;return true;
}
bool FDWA01AbilityPlan::Release(double FaceX,double FaceY,FDWA01AbilityStep& Out)
{
    Out={};Out.PhaseBefore=State.Phase;
    if(!bReady)return false;
    const bool bWasDown=State.bPhysicalDown;
    State.bPhysicalDown=false;State.bReleaseRequired=false;
    if(State.StartRequest==EDWA01AbilityStart::Sprint)
    {
        // A hold already consumed this press. Releasing before admission must never become a tap.
        RejectStart();Out.bInputRejected=true;
    }
    else if(State.Phase==EDWA01AbilityPhase::Pending&&State.StartRequest==EDWA01AbilityStart::None&&bWasDown)
    {
        if(State.PhaseElapsedSeconds<Config.HoldThresholdSeconds&&A01AbilityNormalize(FaceX,FaceY,State.DashDirectionX,State.DashDirectionY))
        {State.StartRequest=EDWA01AbilityStart::Dash;Out.StartRequest=EDWA01AbilityStart::Dash;}
        else {State.Phase=EDWA01AbilityPhase::Idle;State.PhaseElapsedSeconds=0.;Out.bInputRejected=true;}
    }
    else if(IsSprinting()){EndSprintFacts();Out.bSprintEnded=true;}
    Out.PhaseAfter=State.Phase;Out.bInvulnerableAfterStep=IsInvulnerable();return bWasDown;
}
bool FDWA01AbilityPlan::RequestSprint(FDWA01AbilityStep& Out)
{
    if(State.Phase!=EDWA01AbilityPhase::Pending||!State.bPhysicalDown||State.bReleaseRequired||State.StartRequest!=EDWA01AbilityStart::None)return false;
    State.StartRequest=EDWA01AbilityStart::Sprint;Out.StartRequest=EDWA01AbilityStart::Sprint;return true;
}
bool FDWA01AbilityPlan::AcceptDash()
{
    if(!bReady||State.Phase!=EDWA01AbilityPhase::Pending||State.StartRequest!=EDWA01AbilityStart::Dash||State.bReleaseRequired||State.CooldownRemainingSeconds>0.)return false;
    State.StartRequest=EDWA01AbilityStart::None;State.Phase=EDWA01AbilityPhase::DashAcceleration;State.PhaseElapsedSeconds=0.;State.DashDistanceCm=0.;return true;
}
bool FDWA01AbilityPlan::AcceptSprint()
{
    if(!bReady||State.Phase!=EDWA01AbilityPhase::Pending||State.StartRequest!=EDWA01AbilityStart::Sprint||!State.bPhysicalDown||State.bReleaseRequired)return false;
    State.StartRequest=EDWA01AbilityStart::None;State.Phase=EDWA01AbilityPhase::Sprint;State.PhaseElapsedSeconds=0.;return true;
}
bool FDWA01AbilityPlan::RejectStart()
{
    if(!bReady||State.StartRequest==EDWA01AbilityStart::None)return false;
    State.StartRequest=EDWA01AbilityStart::None;State.Phase=EDWA01AbilityPhase::Idle;State.PhaseElapsedSeconds=0.;
    State.bReleaseRequired=State.bPhysicalDown;State.DashDirectionX=State.DashDirectionY=0.;return true;
}
void FDWA01AbilityPlan::EndDashFacts()
{
    State.Phase=EDWA01AbilityPhase::Idle;State.PhaseElapsedSeconds=0.;State.StartRequest=EDWA01AbilityStart::None;
    State.CooldownRemainingSeconds=Config.DashCooldownSeconds;State.bReleaseRequired=State.bPhysicalDown;
}
void FDWA01AbilityPlan::EndSprintFacts()
{
    State.Phase=EDWA01AbilityPhase::Idle;State.PhaseElapsedSeconds=0.;State.StartRequest=EDWA01AbilityStart::None;
    State.bReleaseRequired=State.bPhysicalDown;
}
bool FDWA01AbilityPlan::EndOnHit(){if(!IsDashing())return false;EndDashFacts();return true;}
bool FDWA01AbilityPlan::EndOnWall(){if(!IsDashing())return false;EndDashFacts();return true;}
void FDWA01AbilityPlan::Cancel(bool ForceRelease)
{
    if(IsDashing())EndDashFacts();else EndSprintFacts();
    State.bReleaseRequired=State.bReleaseRequired||ForceRelease;
}
void FDWA01AbilityPlan::RequireRelease(){Cancel(true);}
bool FDWA01AbilityPlan::RestoreIdleCooldown(double Remaining,bool Held)
{
    if(!bReady||!A01AbilityNonnegative(Remaining)||Remaining>Config.DashCooldownSeconds)return false;
    State={};State.CooldownRemainingSeconds=Remaining;State.bPhysicalDown=Held;State.bReleaseRequired=Held;return true;
}
bool FDWA01AbilityPlan::AdvanceNext(double Maximum,double Walk,double MoveX,double MoveY,FDWA01AbilityStep& Out)
{
    Out={};Out.PhaseBefore=Out.PhaseAfter=State.Phase;
    if(!bReady||!A01AbilityPositive(Maximum)||!A01AbilityNonnegative(Walk)||!std::isfinite(MoveX)||!std::isfinite(MoveY)||State.StartRequest!=EDWA01AbilityStart::None)return false;
    double Seconds=Maximum;
    if(State.Phase==EDWA01AbilityPhase::Idle)
    {
        if(State.CooldownRemainingSeconds>0.)Seconds=std::min(Seconds,State.CooldownRemainingSeconds);
        State.CooldownRemainingSeconds=std::max(0.,State.CooldownRemainingSeconds-Seconds);Out.AdvancedSeconds=Seconds;return true;
    }
    if(State.Phase==EDWA01AbilityPhase::Pending)
    {
        const double Until=Config.HoldThresholdSeconds-State.PhaseElapsedSeconds;
        Seconds=std::min(Seconds,Until);State.PhaseElapsedSeconds+=Seconds;Out.AdvancedSeconds=Seconds;
        State.CooldownRemainingSeconds=std::max(0.,State.CooldownRemainingSeconds-Seconds);
        if(Seconds>=Until){State.PhaseElapsedSeconds=Config.HoldThresholdSeconds;RequestSprint(Out);Out.bPhaseEnded=true;}
        return true;
    }
    if(IsSprinting())
    {
        if(!A01AbilityNormalize(MoveX,MoveY,Out.DirectionX,Out.DirectionY)||Walk<=0.)
        {EndSprintFacts();Out.bSprintEnded=true;Out.PhaseAfter=State.Phase;return true;}
        const double Distance=Walk*Config.SprintSpeedMultiplier*Seconds;
        if(!A01AbilityNonnegative(Distance))return false;
        State.CooldownRemainingSeconds=std::max(0.,State.CooldownRemainingSeconds-Seconds);
        Out.AdvancedSeconds=Seconds;Out.DistanceCm=Distance;return true;
    }
    double Duration=State.Phase==EDWA01AbilityPhase::DashAcceleration?Config.DashAccelerationSeconds:
        State.Phase==EDWA01AbilityPhase::DashHold?Config.DashHoldSeconds:Config.DashDecelerationSeconds;
    const double Until=Duration-State.PhaseElapsedSeconds;Seconds=std::min(Seconds,Until);
    const double Peak=Walk*Config.DashSpeedMultiplier;
    const double Before=State.PhaseElapsedSeconds,After=Before+Seconds;
    double Distance=Peak*Seconds;
    if(State.Phase==EDWA01AbilityPhase::DashAcceleration)Distance=Peak*(After*After-Before*Before)/(2.*Duration);
    else if(State.Phase==EDWA01AbilityPhase::DashDeceleration)Distance=Peak*(Seconds-(After*After-Before*Before)/(2.*Duration));
    if(!A01AbilityNonnegative(Distance)||!A01AbilityNonnegative(State.DashDistanceCm+Distance))return false;
    Out.AdvancedSeconds=Seconds;Out.DistanceCm=Distance;Out.DirectionX=State.DashDirectionX;Out.DirectionY=State.DashDirectionY;
    Out.bInvulnerableDuringStep=IsInvulnerable();State.DashDistanceCm+=Distance;
    if(Seconds>=Until)
    {
        Out.bPhaseEnded=true;State.PhaseElapsedSeconds=0.;
        if(State.Phase==EDWA01AbilityPhase::DashAcceleration)State.Phase=EDWA01AbilityPhase::DashHold;
        else if(State.Phase==EDWA01AbilityPhase::DashHold)State.Phase=EDWA01AbilityPhase::DashDeceleration;
        else {EndDashFacts();Out.bDashEnded=true;}
    }
    else State.PhaseElapsedSeconds=After;
    Out.PhaseAfter=State.Phase;Out.bInvulnerableAfterStep=IsInvulnerable();return true;
}
