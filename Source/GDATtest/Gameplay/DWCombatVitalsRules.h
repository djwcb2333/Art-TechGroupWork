#pragma once

#include "CoreMinimal.h"
#include "DWCombatVitals.h"

class FDWCombatRules;

namespace DWCombatVitals
{
    // UE-facing adapter kept separate so the state, math, and deterministic tests stay portable.
    // On failure OutConfig remains unchanged; parameter id/unit errors come from FDWCombatRules.
    bool BuildConfig(const FDWCombatRules& Rules, FDWCombatVitalsConfig& OutConfig, FString& OutError);
}
