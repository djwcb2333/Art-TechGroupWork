#pragma once
#include "CoreMinimal.h"
#include "DWCombatSpear.h"
#include <limits>
class FDWCombatRules;
enum class EDWSwordPhase : uint8 { None, Charge, Slam, Gap, Sweep, Recovery, Complete, Cancelled };
struct GDATTEST_API FDWSwordTuning
{
    double SlamDamage=0.,SweepDamage=0.,RangeCm=0.,ChargeSeconds=0.,GapSeconds=0.,RecoverySeconds=0.;
    double SlamSeconds=0.,SweepSeconds=0.,SlamArc=0.,SweepArc=0.,KnockbackCm=0.,KnockbackSeconds=0.;
    double YeastDamageMultiplier=0.,YeastRecoveryRate=0.,RootDamageMultiplier=0.;
    bool IsValid()const;
    static bool Build(const FDWCombatRules&,FDWSwordTuning& Out,FString& Error);
    double DamageMultiplier(EDWSpearForm)const;
};
/** Gameplay seconds only. Does not read animation, sockets, visual markers or weapon meshes. */
class GDATTEST_API FDWSwordAttackPlan
{
public:
    bool Start(const FDWSwordTuning&);
    bool AdvanceCurrentPhase(double Seconds,double& Consumed,double& Remaining);
    bool EnterNextPhase(EDWSpearForm CurrentForm);
    bool ReleaseCharge(EDWSpearForm CurrentForm);
    bool IsChargeReady()const{return Phase==EDWSwordPhase::Charge&&Elapsed>=Duration;}
    void Cancel(){if(IsRunning())Phase=EDWSwordPhase::Cancelled;}
    EDWSwordPhase GetPhase()const{return Phase;}
    bool IsRunning()const{return Phase>=EDWSwordPhase::Charge&&Phase<=EDWSwordPhase::Recovery;}
    bool IsAtBoundary()const{return IsRunning()&&Elapsed>=Duration;}
    double GetRemainingSeconds()const{return IsChargeReady()?std::numeric_limits<double>::infinity():IsRunning()?FMath::Max(0.,Duration-Elapsed):0.;}
    double EstimateRemainingSeconds(EDWSpearForm Form)const;
    double GetPhaseProgress()const{return Duration>0.?FMath::Clamp(Elapsed/Duration,0.,1.):1.;}
    double GetPhaseDuration()const{return Duration;}
    double GetDamage()const{return Phase==EDWSwordPhase::Slam?Tuning.SlamDamage*DamageScale:Phase==EDWSwordPhase::Sweep?Tuning.SweepDamage*DamageScale:0.;}
    double GetArc()const{return Phase==EDWSwordPhase::Slam?Tuning.SlamArc:Tuning.SweepArc;}
    uint64 GetDamagePhase()const{return Phase==EDWSwordPhase::Slam?1:Phase==EDWSwordPhase::Sweep?2:0;}
    const FDWSwordTuning& GetTuning()const{return Tuning;}
private:
    FDWSwordTuning Tuning;
    EDWSwordPhase Phase=EDWSwordPhase::None;
    double Elapsed=0.,Duration=0.,DamageScale=0.;
};
