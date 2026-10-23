#pragma once
#include "CoreMinimal.h"
enum class EDWAreaAdoption:uint8{Invalid,Created,Existing};
struct FDWBowSlowAreaSnapshot
{
    double RadiusCm=0.,DurationSeconds=0.,SlowFraction=0.;
    bool IsValid()const;
};
struct FDWAlcoholAreaSnapshot
{
    double Damage=0.,RadiusCm=0.,DurationSeconds=0.,TickIntervalSeconds=0.,SlowFraction=0.;
    bool IsValid()const;
};
/** Target cadence persists outside a pool. No entering/switching operation produces an immediate tick. */
struct FDWCombatAreaTargetClock
{
    double AnchorSeconds=0.,PeriodSeconds=0.;
    uint64 NextOrdinal=0;
    bool Start(double FirstCoveredSeconds,double Period);
    double NextSeconds()const;
    bool ConsumeNext(double ThroughSeconds,double& DueSeconds,uint64& Ordinal);
};
