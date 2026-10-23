#pragma once
#if WITH_DEV_AUTOMATION_TESTS
#include "DWWeaponAnimationComponent.h"
#include "DWCombatItemRegistry.h"

namespace DWCombatEquipmentTestFixture
{
// Explicit initial test/save state, never a live quick-selection shortcut. Preserve other
// owned items/shells and any existing attack lock; do not advance independent resource clocks.
inline bool RestoreOwnedCurrent(UDWWeaponAnimationComponent& Weapon,const FDWCombatRules& Rules,
    EDWAnimatedWeapon Selection,FString& Error)
{
    const auto* Binding=FDWCombatItemRegistry::FindPresentation(Selection);
    if(!Binding||FDWCombatItemRegistry::GetItemRole(Binding->ItemId)!=EDWCombatItemRole::PermanentWeapon)
    {Error=TEXT("Fixture requires a registered permanent weapon.");return false;}
    auto Snapshot=Weapon.GetCombatWeaponStateSnapshot();
    Snapshot.OwnedPermanentWeaponIds.Add(Binding->ItemId);Snapshot.CurrentEquipmentId=Binding->ItemId;
    return Weapon.RestoreCombatWeaponState(Snapshot,Rules,Error)&&Weapon.GetCurrentEquipmentId()==Binding->ItemId&&
        Weapon.SelectedWeapon==Selection;
}
}
#endif
