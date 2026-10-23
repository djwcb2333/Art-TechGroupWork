#pragma once
#include "CoreMinimal.h"
class ADWPlayerCharacter;
struct FDWCombatInputPress;
class FDWYeastDashPlan;
/** Fixed, callback-free start-cost + accepted dash-plan commit; no public resource setter. */
class FDWCombatDashTransaction
{
public:
    static bool Start(ADWPlayerCharacter&,const FDWCombatInputPress&,const FDWYeastDashPlan&);
    static bool IsCurrent(const ADWPlayerCharacter&);
};
