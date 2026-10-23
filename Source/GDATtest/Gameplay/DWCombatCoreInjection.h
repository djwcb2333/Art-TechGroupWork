#pragma once
#include "CoreMinimal.h"
#include "DWCombatPlayerVitals.h"

class FDWCombatRules;
struct FDWCoreInjectionConfig
{
    double DurationSeconds=0.;
    int32 CoreCost=0;
    FDWCombatPlayerVitalsConfig Vitals;
    bool IsValid() const;
    static bool Build(const FDWCombatRules& Rules,FDWCoreInjectionConfig& Out,FString& Error);
};
enum class EDWCoreInjectionPhase : uint8 { Idle, Injecting, Ready, Committed, Cancelled };

// Clock only: no inventory, UObject calls or form grant. Ready remains cancellable until typed commit.
class GDATTEST_API FDWCoreInjectionPlan
{
public:
    bool Start(double DurationSeconds);
    bool Advance(double Seconds);
    bool Commit();
    void Cancel();
    bool IsPending() const {return Phase==EDWCoreInjectionPhase::Injecting||Phase==EDWCoreInjectionPhase::Ready;}
    bool IsReady() const {return Phase==EDWCoreInjectionPhase::Ready;}
    EDWCoreInjectionPhase GetPhase() const {return Phase;}
    uint64 GetSerial() const {return Serial;}
    double GetDurationSeconds() const {return Duration;}
    double GetRemainingSeconds() const;
    double GetNextBoundarySeconds() const;
private:
    EDWCoreInjectionPhase Phase=EDWCoreInjectionPhase::Idle;
    uint64 Serial=0;
    double Duration=0.,Remaining=0.,Carry=0.;
};
