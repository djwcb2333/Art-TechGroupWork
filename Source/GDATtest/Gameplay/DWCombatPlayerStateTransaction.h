#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"

class ADWPlayerCharacter;
class UDWInventoryComponent;
class UDWWeaponAnimationComponent;
class UDWCombatAuthoritySubsystem;
class UDWSaveGame;
class FDWCombatRules;
class FDWCharacterA01Runtime;
class FDWPreparedInventoryChange;

struct FDWCombatPlayerStateNotice
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    TWeakObjectPtr<UDWWeaponAnimationComponent> Weapon;
    DWCombatCommit::FEntityLife Life;
    uint64 RestoreEpoch=0;
    int64 WeaponRevision=0;
    int32 LoadedShells=0;
    bool bRestore=false;
    FName UsedItem;
    float RestoredHealth=0.f;
};

/** Two fixed native transactions. No public suppress-notifications window or arbitrary callback. */
class FDWCombatPlayerStateTransaction final
{
    friend class ADWPlayerCharacter;
    friend class UDWCombatAuthoritySubsystem;
    static bool Restore(ADWPlayerCharacter& Player,const UDWSaveGame& Save,const FDWCombatRules& Rules,
        FDWCombatPlayerStateNotice& OutNotice,FString& Error);
    static bool UseLegacyHealingItem(ADWPlayerCharacter& Player,int32 SlotIndex);
    static bool UseA01Consumable(ADWPlayerCharacter&,FDWPreparedInventoryChange&&,FDWCharacterA01Runtime&&,
        uint64 ExpectedRevision,double ExpectedHealth,double HealthAmount,FName UsedItem);
    static void Publish(const FDWCombatPlayerStateNotice& Notice);
};
