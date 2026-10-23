#pragma once
#include "CoreMinimal.h"
class FDWCombatRules;
class UWorld;
class AActor;
enum class EDWSpearForm : uint8 { Normal, MoldMobile, Yeast, MoldRooted };
enum class EDWSpearPhase : uint8 { None, Windup, Active, Recovery, Complete, Cancelled };
/** Approved rule values, converted to UE centimetres once. */
struct GDATTEST_API FDWSpearTuning
{
    double Damage=0.,RangeCm=0.,FullArcDegrees=0.,IntervalSeconds=0.;
    double EdgeBandFraction=0.,MinimumDamageFraction=0.,KnockbackCm=0.,KnockbackSeconds=0.;
    double WindupSeconds=0.,ActiveSeconds=0.,RecoverySeconds=0.;
    double YeastDamageMultiplier=0.,YeastRateMultiplier=0.,RootedDamageMultiplier=0.,SwitchLockSeconds=0.;
    bool IsValid() const;
    static bool Build(const FDWCombatRules& Rules,FDWSpearTuning& Out,FString& Error);
    double DamageMultiplier(EDWSpearForm Form) const;
    double RateMultiplier(EDWSpearForm Form) const;
    bool DamageAtDistance(double DistanceCm,double SnapshotDamage,double& OutDamage) const;
};
/** Art measurements, not gameplay tuning or permission to retime the source. */
struct GDATTEST_API FDWSpearSourceTiming
{
    double DurationSeconds=0.,ContactBeginSeconds=0.,ContactEndSeconds=0.;
    bool Validate(const FDWSpearTuning& Tuning,FString& Error) const;
};
/** No UObject calls or callbacks. Caller samples the current form at each exact phase boundary. */
class GDATTEST_API FDWSpearAttackPlan
{
public:
    bool Start(const FDWSpearTuning& InTuning,EDWSpearForm Form);
    bool AdvanceCurrentPhase(double Seconds,double& OutConsumed,double& OutRemaining);
    bool EnterNextPhase(EDWSpearForm Form);
    void Cancel();
    EDWSpearPhase GetPhase() const {return Phase;}
    bool IsRunning() const;
    bool IsAtBoundary() const;
    double GetSourceSeconds() const;
    double GetPhaseRate() const {return PhaseRate;}
    double GetPhaseRemainingSeconds() const;
    double EstimateRemainingSeconds(EDWSpearForm CurrentForm) const;
    double GetDamageSnapshot() const {return DamageSnapshot;}
    const FDWSpearTuning& GetTuning() const {return Tuning;}
private:
    FDWSpearTuning Tuning;
    EDWSpearPhase Phase=EDWSpearPhase::None;
    double PhaseSourceElapsed=0.,PhaseRate=1.,DamageSnapshot=0.;
    double PhaseDuration() const;
};
struct FDWSpearHitCandidate
{
    TWeakObjectPtr<AActor> Target;
    FVector ClosestPoint=FVector::ZeroVector;
    double EdgeDistanceCm=0.;
};
/** Re-evaluate before submission: earlier victims may call back and move/destroy other victims. */
class GDATTEST_API FDWSpearHitQuery
{
public:
    // Shared geometry-only overload: no dependency on spear damage/rate/animation tuning.
    static void CollectSector(UWorld*,AActor*,const FVector& Origin,const FVector& Forward,double RangeCm,double FullArc,TArray<FDWSpearHitCandidate>& Out);
    static bool EvaluateSector(UWorld*,AActor*,AActor* Target,const FVector& Origin,const FVector& Forward,double RangeCm,double FullArc,FDWSpearHitCandidate& Out);
    static void Collect(UWorld* World,AActor* Source,const FVector& Origin,const FVector& Forward,
        const FDWSpearTuning& Tuning,TArray<FDWSpearHitCandidate>& Out);
    static bool Evaluate(UWorld* World,AActor* Source,AActor* Target,const FVector& Origin,
        const FVector& Forward,const FDWSpearTuning& Tuning,FDWSpearHitCandidate& Out);
};
