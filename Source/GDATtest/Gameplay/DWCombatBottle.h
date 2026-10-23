#pragma once
#include "DWCombatSpear.h"

enum class EDWBottlePhase:uint8 { None,Windup,Release,Recovery,Gap,Complete };
struct FDWBottleSnapshot
{
    double RangeCm=0.,FlightSeconds=0.,ArcHeightCm=0.,CollisionRadiusCm=0.;
    double AreaRadiusCm=0.,AreaSeconds=0.,TickSeconds=0.,TickDamage=0.,SlowFraction=0.;
    bool IsValid()const;
};
struct FDWBottleConfig
{
    FDWBottleSnapshot Base;
    double IntervalSeconds=0.,WindupSeconds=0.,RecoverySeconds=0.;
    double NormalDamageMultiplier=0.,NormalRateMultiplier=0.,YeastDamageMultiplier=0.,YeastRateMultiplier=0.,YeastRangeCm=0.;
    double RootDamageMultiplier=0.,RootRateMultiplier=0.;
    int32 StockPerThrow=0;
    static bool Build(const FDWCombatRules&,FDWBottleConfig&,FString&);
    bool IsValid()const;
    double Rate(EDWSpearForm)const;
    FDWBottleSnapshot Shot(EDWSpearForm)const;
};
/** Release is an explicit zero-time boundary, owned by the unique stock/emitter transaction. */
class FDWBottlePlan
{
public:
    bool Start(const FDWBottleConfig&,EDWSpearForm);
    bool Advance(double Seconds,double& Consumed);
    bool EnterNext(EDWSpearForm);
    void Cancel(){Phase=EDWBottlePhase::None;Elapsed=0.;}
    EDWBottlePhase GetPhase()const{return Phase;}
    bool IsRunning()const{return Phase!=EDWBottlePhase::None&&Phase!=EDWBottlePhase::Complete;}
    bool AtBoundary()const{return IsRunning()&&Elapsed>=Duration();}
    double Remaining()const;
    double SourceSeconds()const;
    double EstimateRemaining(EDWSpearForm)const;
private:
    FDWBottleConfig Config;EDWBottlePhase Phase=EDWBottlePhase::None;
    double Elapsed=0.,PhaseRate=1.;
    double Duration()const;
};
namespace DWBottle
{
    FVector SampleArc(FVector Start,FVector GroundEnd,double FlightSeconds,double ArcHeightCm,double AgeSeconds);
    FVector ClampHorizontalTarget(FVector Origin,FVector Requested,double MaximumRangeCm);
}
