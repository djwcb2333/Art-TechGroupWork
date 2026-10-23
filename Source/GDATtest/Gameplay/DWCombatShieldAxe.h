#pragma once
#include "DWCombatSpear.h"

/** Classification is provided by a typed damage adapter, never guessed from a mesh or damage value. */
enum class EDWShieldIncomingKind:uint8 { Unblockable, Melee, PhysicalProjectile };
struct FDWShieldAxeTuning
{
    FDWSpearTuning Attack;
    double BlockReduction=0.,BlockFullArcDegrees=0.,ParryBeforeSeconds=0.,ParryAfterSeconds=0.;
    double ReopenCooldownSeconds=0.,CounterMultiplier=0.,RaiseVisualSeconds=0.,LowerVisualSeconds=0.;
    int32 CounterHits=0;
    static bool Build(const FDWCombatRules&,FDWShieldAxeTuning&,FString&);
    bool IsValid()const;
};
/** Relative-time contract for D22. No disk caller is implied by this native DTO. */
struct FDWShieldAxeDefenseSnapshot
{
    int32 Schema=1,CounterCharges=0;
    double PerfectRemainingSeconds=0.,ReopenRemainingSeconds=0.;
    bool bPerfectEligible=false;
    uint8 BaseForm=0;
};
struct FDWShieldImpactPolicy
{
    bool bEligible=false,bPerfectNow=false,bOrdinaryBlock=false;
    double OrdinaryDamageMultiplier=1.,AfterSeconds=0.;
};
/** Callback-free facts. World game-time timestamps freeze naturally with the paused world. */
class FDWShieldAxeDefense
{
public:
    bool Configure(const FDWShieldAxeTuning&);
    bool Raise(double Now,uint8 BaseForm);
    void Release(){bHeld=false;bEligible=false;}
    bool IsHeld()const{return bHeld;}
    bool CanPerfectAt(double Now)const;
    FDWShieldImpactPolicy PrepareImpact(double Now,EDWShieldIncomingKind,FVector Facing,FVector Incoming)const;
    void RecordParry(){CounterCharges=Tuning.CounterHits;}
    double ConsumeCounterAtSwing();
    void ClearCounter(){CounterCharges=0;}
    void NotifyBaseForm(uint8 Form){if(Form!=CounterBaseForm)CounterCharges=0;CounterBaseForm=Form;}
    int32 GetCounterCharges()const{return CounterCharges;}
    FDWShieldAxeDefenseSnapshot Capture(double Now)const;
    bool Restore(const FDWShieldAxeDefenseSnapshot&,double Now);
    const FDWShieldAxeTuning& GetTuning()const{return Tuning;}
private:
    FDWShieldAxeTuning Tuning;
    bool bConfigured=false,bHeld=false,bEligible=false;
    double PerfectUntil=0.,ReopenAt=0.;
    int32 CounterCharges=0;uint8 CounterBaseForm=0;
};
