#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipment.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "Engine/World.h"

bool UDWWeaponAnimationComponent::HasCurrentEquipmentIdentity()const
{
    EDWAnimatedWeapon Expected;FString Error;
    return CombatWeaponResources.bRulesReady&&DWCombatEquipment::ValidateIdentity(CombatWeaponResources.Snapshot,Error)&&
        DWCombatEquipment::ToPresentation(CombatWeaponResources.Snapshot.CurrentEquipmentId,Expected)&&Expected==SelectedWeapon;
}
bool UDWWeaponAnimationComponent::CanSelectCombatEquipment(EDWAnimatedWeapon Weapon)const
{
    const auto* P=Player();const auto* Inventory=P?P->GetInventory():nullptr;
    return bEnabled&&IsValid(this)&&!IsBeingDestroyed()&&P&&P->IsCombatActionAllowed()&&
        CombatWeaponResources.bRulesReady&&bEquipmentRulesReady&&CombatWeaponResources.Revision<MAX_int64&&
        DWCombatEquipment::CanSelect(CombatWeaponResources.Snapshot,Weapon,Inventory?Inventory->CountItem(TEXT("Alcohol")):0);
}
bool UDWWeaponAnimationComponent::SelectWeapon(EDWAnimatedWeapon Weapon)
{
    if(!CanSelectCombatEquipment(Weapon))return false;
    FName Id;if(!DWCombatEquipment::ToItemId(Weapon,Id))return false;
    // Selecting the existing item is a no-op, not a free action cancellation or cooldown reset.
    if(Id==CombatWeaponResources.Snapshot.CurrentEquipmentId&&SelectedWeapon==Weapon)return true;
    auto* P=Player();auto* Authority=UDWCombatAuthoritySubsystem::Get(P);if(!Authority)return false;
    const TWeakObjectPtr<UWorld> World(GetWorld());const auto Life=Authority->CaptureActorLife(P);
    const uint64 Epoch=Authority->GetWorldEpoch(),Restore=CombatWeaponResources.RestoreEpoch;
    const int64 Revision=CombatWeaponResources.Revision;const EDWAnimatedWeapon Previous=SelectedWeapon;
    const double Now=GetCombatActionWorldSeconds();if(!FMath::IsFinite(Now))return false;
    // All requests have already passed ownership validation. Cancel may publish visual callbacks;
    // a reentrant restore/equip is authoritative and must not be overwritten below.
    P->CancelCombatActions();
    Authority=UDWCombatAuthoritySubsystem::Get(P);
    if(!IsValid(this)||IsBeingDestroyed()||!IsValid(P)||P->IsActorBeingDestroyed()||Player()!=P||
        !World.IsValid()||GetWorld()!=World.Get()||World->bIsTearingDown||!Authority||Authority->GetWorldEpoch()!=Epoch||
        CombatWeaponResources.RestoreEpoch!=Restore||CombatWeaponResources.Revision!=Revision||SelectedWeapon!=Previous||
        !CanSelectCombatEquipment(Weapon))return false;
    const auto CurrentLife=Authority->CaptureActorLife(P);
    if(Life.Entity!=CurrentLife.Entity||Life.Life!=CurrentLife.Life)return false;
    ClearShieldCounterFacts();
    CombatWeaponResources.Snapshot.CurrentEquipmentId=Id;++CombatWeaponResources.Revision;
    SelectedWeapon=Weapon;AmmoInMagazine=Weapon==EDWAnimatedWeapon::Shotgun?CombatWeaponResources.Snapshot.LoadedShotgunShells:0;
    NextAttackTime=FMath::Max(NextAttackTime,Now+EquipmentSwitchLockSeconds);
    bPrimaryHeld=bSecondaryHeld=false;PrimaryInputOrigin.Reset();SecondaryInputOrigin.Reset();
    ActiveSet=nullptr;ActiveClip={};
    RefreshDaggerEquipmentFacts();
    UpdateVisuals();return true; // Native selection is complete before any presentation notification.
}
