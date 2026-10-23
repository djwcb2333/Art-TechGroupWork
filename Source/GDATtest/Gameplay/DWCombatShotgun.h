#pragma once
#include "CoreMinimal.h"
#include "DWCombatSpear.h"
class FDWCombatRules;
struct GDATTEST_API FDWShotgunConfig
{
    int32 Pellets=0;
    double Damage=0.,Range=0.,FullRange=0.,FarFraction=0.,SpreadDegrees=0.;
    double NearKnockback=0.,FarKnockback=0.,KnockbackSeconds=0.;
    double ReloadMoveMultiplier=0.; // Required formal rule; applied after the normal form/stance movement result.
    double Windup=0.,Recovery=0.,Interval=0.,YeastDamage=0.,YeastRate=0.,YeastRange=0.,RootDamage=0.,RootRate=0.;
    static bool Build(const FDWCombatRules&,FDWShotgunConfig&,FString&);
    bool IsValid()const;
    double Rate(EDWSpearForm F)const;
    double DamageScale(EDWSpearForm F)const;
    double RangeFor(EDWSpearForm F)const;
    double Angle(int32 Index)const;
    double DamageAt(double Distance,double ActualRange)const;
    double KnockbackAt(double Distance,double ActualRange)const;
};
enum class EDWShotgunPhase:uint8 {None,Windup,Recovery,Complete};
struct GDATTEST_API FDWShotgunPlan
{
    FDWShotgunConfig Config;EDWShotgunPhase Phase=EDWShotgunPhase::None;
    double Remaining=0.,PhaseDuration=0.;
    double GetPhaseProgress()const{return PhaseDuration>0.?FMath::Clamp(1.-Remaining/PhaseDuration,0.,1.):1.;}
    bool Start(const FDWShotgunConfig&,EDWSpearForm);
    double Advance(double Seconds);
    void Fired(EDWSpearForm);
    void Cancel(){Phase=EDWShotgunPhase::None;Remaining=0.;}
};
struct FDWShotgunTarget
{
    TWeakObjectPtr<AActor> Target;double Damage=0.,Knockback=0.;int32 Pellets=0;
};
// Independent gameplay rays versus explicit hurtboxes and world obstruction; no animated mesh sampling.
struct GDATTEST_API FDWShotgunQuery
{
    static void Collect(UWorld*,AActor* Source,FVector Origin,FVector Forward,const FDWShotgunConfig&,
        EDWSpearForm,TArray<FDWShotgunTarget>& Out);
};
