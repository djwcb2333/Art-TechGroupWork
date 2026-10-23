#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
class UDWWeaponAnimationComponent;
class UDWInventoryComponent;
class ADWPlayerCharacter;
class ADWCombatBowProjectile;
class UDWCombatAuthoritySubsystem;
/** Exactly one bow ammo debit, receipt and already-staged native projectile release. */
class FDWCombatBowShotTransaction final
{
    friend class UDWWeaponAnimationComponent;
    friend class UDWCombatAuthoritySubsystem;
    static bool Commit(UDWWeaponAnimationComponent& Weapon,double SliceOffset);
};
