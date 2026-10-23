#pragma once
#include "CoreMinimal.h"
class FDWCombatRules;
enum class EDWSporeForm : uint8 { Normal,MoldMobile,Yeast,MoldRooted };
enum class EDWSporePhase : uint8 { None,Windup,Release,Recovery,Gap,Complete };
struct FDWSporeShotSnapshot
{
    double Damage=0.,RangeCm=0.,SpeedCmPerSecond=0.,TurnDegreesPerSecond=0.,RadiusCm=0.;
    double KnockbackCm=0.,KnockbackSeconds=0.;
    bool IsValid() const;
};
struct GDATTEST_API FDWSporeGunConfig
{
    double Damage=0.,RangeCm=0.,IntervalSeconds=0.,WindupSeconds=0.,RecoverySeconds=0.;
    double TargetArcDegrees=0.,SpeedCmPerSecond=0.,TurnDegreesPerSecond=0.,RadiusCm=0.;
    double KnockbackCm=0.,KnockbackSeconds=0.;
    double NormalDamageMultiplier=0.,NormalRateMultiplier=0.;
    double YeastDamageMultiplier=0.,YeastRateMultiplier=0.,YeastRangeCm=0.;
    double RootDamageMultiplier=0.,RootRateMultiplier=0.;
    int32 AmmoPerShot=0;
    static bool Build(const FDWCombatRules& Rules,FDWSporeGunConfig& Out,FString& Error);
    bool IsValid() const;
    double Rate(EDWSporeForm Form) const;
    FDWSporeShotSnapshot Shot(EDWSporeForm Form) const;
};
/** Each phase samples its own rate; the release is a zero boundary owned by one fixed transaction. */
class GDATTEST_API FDWSporeShotPlan
{
public:
    bool Start(const FDWSporeGunConfig& Config,EDWSporeForm Form);
    bool Advance(double Seconds,double& Consumed);
    bool EnterNext(EDWSporeForm Form);
    void Cancel(){Phase=EDWSporePhase::None;Elapsed=0.;}
    EDWSporePhase GetPhase()const{return Phase;}
    bool IsRunning()const{return Phase!=EDWSporePhase::None&&Phase!=EDWSporePhase::Complete;}
    bool AtBoundary()const{return IsRunning()&&Elapsed>=Duration();}
    double Remaining()const;
    double GetSourceSeconds()const;
    double GetRate()const{return PhaseRate;}
    double EstimateRemaining(EDWSporeForm Form)const;
private:
    FDWSporeGunConfig Config;
    EDWSporePhase Phase=EDWSporePhase::None;
    double Elapsed=0.,PhaseRate=1.;
    double Duration()const;
};
namespace DWSporeGun
{
    // Direction-only mathematical step, no targeting, movement, collision or damage side effects.
    FVector TurnTowards(FVector Current,FVector Desired,double MaximumDegrees);
}
