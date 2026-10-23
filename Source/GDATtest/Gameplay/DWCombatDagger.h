#pragma once
#include "CoreMinimal.h"
#include "DWCombatSpear.h"
#include "DWCombatDagger.generated.h"
class FDWCombatRules;
struct GDATTEST_API FDWDaggerTuning
{
    FDWSpearTuning Attack; // The .30/.06/.10/.14 profile supplies phase proportions, never the live segment duration.
    double SegmentDurationSeconds[4]={};
    bool MakeSegmentTuning(int32 Segment,FDWSpearTuning& Out)const;
    double BackArcDegrees=0.,GraceSeconds=0.,RefreshRadiusCm=0.,EliteMaxHealthFraction=0.;
    static bool Build(const FDWCombatRules&,FDWDaggerTuning& Out,FString& Error);
};
/** D22 stores this beside the stable enemy identity; no world timestamp or input ticket. */
USTRUCT()
struct FDWDaggerAwarenessSave
{
    GENERATED_BODY()
    UPROPERTY() bool bInitialized=false;
    UPROPERTY() bool bInside=false;
    UPROPERTY() bool bEquipped=false;
    UPROPERTY() double GraceRemainingSeconds=0.;
};
/** Per enemy discovery grace. It never changes health, faction or combat memory. */
class GDATTEST_API FDWDaggerAwareness
{
public:
    void Observe(double Now,bool Inside,bool Equipped,bool Engaged,double FullGrace);
    void EquipmentChanged(bool Inside,bool Equipped);
    void BreakProtection(){Deadline=0.;}
    void Refresh(double Now,double FullGrace,bool Equipped,bool Engaged);
    bool IsProtected(double Now,bool Equipped,bool Engaged)const;
    FDWDaggerAwarenessSave Capture(double Now)const;
    bool Restore(const FDWDaggerAwarenessSave&,double Now);
    static bool Validate(const FDWDaggerAwarenessSave&);
private:
    bool bInitialized=false,bInside=false,bEquipped=false;
    double Deadline=0.;
};
