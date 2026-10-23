#pragma once
class UDWWeaponAnimationComponent;
/** One fixed stock debit + committed WeaponUse receipt + independently owned bottle emitter. */
class FDWCombatBottleTransaction final
{
    friend class UDWWeaponAnimationComponent;
    static bool Commit(UDWWeaponAnimationComponent&,double SliceOffset);
};
