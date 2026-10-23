#pragma once
#include "CoreMinimal.h"
#include "DWCombatGroundAreas.h"
class FDWCombatRules;
enum class EDWBowForm:uint8{Normal,MoldMobile,Yeast,MoldRooted};
enum class EDWBowPhase:uint8{None,Windup,Release,Recovery,Gap,Complete};
struct FDWBowShotSnapshot
{
    double Damage=0.,RangeCm=0.,SpeedCmPerSecond=0.,RadiusCm=0.,KnockbackCm=0.,KnockbackSeconds=0.;
    FDWBowSlowAreaSnapshot Area;
    bool IsValid()const;
};
struct FDWBowConfig
{
    FDWBowShotSnapshot Base;
    double IntervalSeconds=0.,WindupSeconds=0.,RecoverySeconds=0.;int32 AmmoPerShot=0;
    double NormalDamageMultiplier=0.,NormalRateMultiplier=0.,YeastDamageMultiplier=0.,YeastRateMultiplier=0.,YeastRangeCm=0.,RootDamageMultiplier=0.,RootRateMultiplier=0.;
    static bool Build(const FDWCombatRules&,FDWBowConfig&,FString&);
    bool IsValid()const;
    double Rate(EDWBowForm)const;
    FDWBowShotSnapshot Shot(EDWBowForm)const;
};
struct FDWBowPlan
{
    FDWBowConfig Config;EDWBowPhase Phase=EDWBowPhase::None;double Elapsed=0.,PhaseRate=1.;
    bool Start(const FDWBowConfig&,EDWBowForm);
    bool Advance(double Seconds,double& Consumed);
    bool EnterNext(EDWBowForm);
    double Duration()const;
    double Remaining()const;
    double GetSourceSeconds()const;
    double EstimateRemaining(EDWBowForm)const;
    bool AtBoundary()const{return IsRunning()&&Elapsed>=Duration();}
    bool IsRunning()const{return Phase!=EDWBowPhase::None&&Phase!=EDWBowPhase::Complete;}
    void Cancel(){Phase=EDWBowPhase::None;Elapsed=0.;}
};
