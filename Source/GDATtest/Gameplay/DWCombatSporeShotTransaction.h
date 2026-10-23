#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
class UDWWeaponAnimationComponent;
class UDWInventoryComponent;
class ADWPlayerCharacter;
class ADWCombatSporeProjectile;
class UDWCombatAuthoritySubsystem;
/** Exactly one spore ammo debit, receipt and already-staged native projectile release. */
class FDWCombatSporeShotTransaction final
{
    friend class UDWWeaponAnimationComponent;
    friend class UDWCombatAuthoritySubsystem;
    static bool Commit(UDWWeaponAnimationComponent& Weapon,double SliceOffset);
};
