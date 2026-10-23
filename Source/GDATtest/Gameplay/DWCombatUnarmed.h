#pragma once
#include "DWCombatSpear.h"

/** Reuses the existing three-phase clock and sector query with independent formal unarmed tuning. */
struct FDWCombatUnarmedTuning
{
    FDWSpearTuning Attack;
    static bool Build(const FDWCombatRules&,FDWCombatUnarmedTuning&,FString& Error);
};
