#pragma once
#include "CoreMinimal.h"
class FDWCombatRules;

struct FDWYeastDashConfig
{
    double StartCost=0.,SpendPerSecond=0.,GainPerSecond=0.,SpecialFloor=0.,SpecialMax=0.;
    double FormDuration=0.,MinimumSeconds=0.,SpeedCmPerSecond=0.,CooldownSeconds=0.;
    double Damage=0.,KnockbackCm=0.,KnockbackSeconds=0.,QueryRadiusCm=0.;
    int32 HitsPerTarget=0;
    double NetSpend()const{return SpendPerSecond-GainPerSecond;}
    double MinimumBudget()const{return StartCost+NetSpend()*MinimumSeconds;}
    bool IsValid()const;
    static bool Build(const FDWCombatRules&,FDWYeastDashConfig&,FString& Error);
};
enum class EDWYeastDashEnd:uint8 {None,Released,SpecialFloor,FormExpired,Wall,Control,Death,Cancelled};
struct FDWYeastDashStep
{
    double Seconds=0.,DistanceCm=0.;
    EDWYeastDashEnd End=EDWYeastDashEnd::None;
};
/** Pure plan: no UObject, delegate, movement, damage or second resource balance.
 * Runtime prepares a copy and adopts it with the single vitals owner before publishing effects.
 */
class GDATTEST_API FDWYeastDashPlan
{
public:
    bool CanStart(const FDWYeastDashConfig&,double Special,double FormRemaining)const;
    bool Start(const FDWYeastDashConfig&,double Special,double FormRemaining,FVector2D Move,FVector2D Aim,uint64 PressSequence);
    bool Release(uint64 OriginalPress);
    double NextBoundary(double Special,double FormRemaining)const;
    bool Advance(double Seconds,double Special,double FormRemaining,FDWYeastDashStep& Out);
    // Runtime has already bounded/paid the exact slice in the single compensated vitals owner.
    bool AdvanceAtResourceBoundary(double Seconds,bool bFormExpired,bool bSpecialFloor,FDWYeastDashStep& Out);
    bool End(EDWYeastDashEnd Reason);
    bool AdvanceCooldown(double Seconds);
    bool RestoreCooldown(double Remaining); // D22 seam only, no file caller in this batch.
    // Candidate-only D22 restore: no action, press, movement or resource payment is resumed.
    bool RestoreCooldown(const FDWYeastDashConfig&,double Remaining);
    bool IsActive()const{return bActive;}
    bool IsHeld()const{return bHeld;}
    double ElapsedSeconds()const{return Elapsed-Carry;}
    double CooldownSeconds()const{return Cooldown-CooldownCarry;}
    FVector2D Direction()const{return LockedDirection;}
    uint64 Serial()const{return ActionSerial;}
    uint64 OriginPress()const{return ActivePress;}
    EDWYeastDashEnd LastEnd()const{return EndReason;}
    const FDWYeastDashConfig& GetConfig()const{return Config;}
private:
    FDWYeastDashConfig Config;
    bool bActive=false,bHeld=false;
    double Elapsed=0.,Carry=0.,Cooldown=0.,CooldownCarry=0.;
    uint64 ActionSerial=0,ActivePress=0,LastAttempt=0;
    FVector2D LockedDirection=FVector2D::ZeroVector;
    EDWYeastDashEnd EndReason=EDWYeastDashEnd::None;
};
