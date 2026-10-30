#pragma once

#include <cstdint>
#include <string>

enum class EDWA01AbilityPhase : std::uint8_t { Idle, Pending, Sprint, DashAcceleration, DashHold, DashDeceleration };
enum class EDWA01AbilityStart : std::uint8_t { None, Dash, Sprint };

struct FDWA01AbilityConfig
{
    // Editable LANG-01 trial defaults; executor populates these from Design/TrialDefaults.json.
    double HoldThresholdSeconds=.18, SprintSpeedMultiplier=1.2, DashSpeedMultiplier=2.;
    double DashAccelerationSeconds=.1, DashHoldSeconds=.25, DashDecelerationSeconds=.1;
    double DashCooldownSeconds=.3;
    bool IsValid(std::string* Error=nullptr) const;
    double DashDurationSeconds() const;
    double DashDistanceCm(double FinalWalkSpeedCmPerSecond) const;
};

struct FDWA01AbilityState
{
    EDWA01AbilityPhase Phase=EDWA01AbilityPhase::Idle;
    EDWA01AbilityStart StartRequest=EDWA01AbilityStart::None;
    double PhaseElapsedSeconds=0., CooldownRemainingSeconds=0.;
    double DashDirectionX=0., DashDirectionY=0., DashDistanceCm=0.;
    bool bPhysicalDown=false, bReleaseRequired=false;
};

struct FDWA01AbilityStep
{
    double AdvancedSeconds=0., DistanceCm=0., DirectionX=0., DirectionY=0.;
    EDWA01AbilityPhase PhaseBefore=EDWA01AbilityPhase::Idle, PhaseAfter=EDWA01AbilityPhase::Idle;
    EDWA01AbilityStart StartRequest=EDWA01AbilityStart::None;
    bool bPhaseEnded=false, bDashEnded=false, bSprintEnded=false;
    // A step never crosses a phase boundary: this flag describes its entire desired swept segment.
    bool bInvulnerableDuringStep=false, bInvulnerableAfterStep=false;
    bool bInputRejected=false;
};

class FDWA01AbilityPlan
{
public:
    bool Initialize(const FDWA01AbilityConfig&, std::string* Error=nullptr);
    const FDWA01AbilityConfig& GetConfig() const { return Config; }
    const FDWA01AbilityState& GetState() const { return State; }
    bool IsReady() const { return bReady; }
    bool IsDashing() const;
    bool IsSprinting() const { return State.Phase==EDWA01AbilityPhase::Sprint; }
    bool IsInvulnerable() const { return State.Phase==EDWA01AbilityPhase::DashHold; }
    bool IsMenuBlocked() const { return IsDashing()||IsSprinting(); }
    // Physical edges only. Press starts Pending; Release before threshold requests Dash.
    bool Press();
    bool Release(double FacingX,double FacingY,FDWA01AbilityStep&);
    // Pending/phase/cooldown boundary, caller may further clip MaximumSeconds at resource/form events.
    // Sprint direction is supplied every slice; dash direction stays locked to the tap-release facing.
    bool AdvanceNext(double MaximumSeconds,double FinalWalkSpeedCmPerSecond,double MoveX,double MoveY,FDWA01AbilityStep&);
    // Plan never debits gas. Executor validates, pays a candidate resource state, then calls exactly one Accept.
    bool AcceptDash();
    bool AcceptSprint();
    bool RejectStart();
    bool EndOnHit();
    bool EndOnWall();
    void Cancel(bool bForceRelease=false);
    // Use when entering a form while Space is already held, or when an input context is replaced.
    void RequireRelease();
    // Loading never resumes a movement action or press; only the validated remaining cooldown is restored.
    bool RestoreIdleCooldown(double RemainingSeconds,bool bPhysicalKeyHeld);
private:
    FDWA01AbilityConfig Config;
    FDWA01AbilityState State;
    bool bReady=false;
    void EndDashFacts();
    void EndSprintFacts();
    bool RequestSprint(FDWA01AbilityStep&);
};
