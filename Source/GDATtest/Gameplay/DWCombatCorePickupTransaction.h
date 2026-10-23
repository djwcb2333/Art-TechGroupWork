#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
class ADWCombatCorePickup;
class ADWPlayerCharacter;
class UDWInventoryComponent;
class UDWCombatAuthoritySubsystem;
class UDWCombatCoreDropSubsystem;
struct FDWCombatCorePickupNotice
{
    TWeakObjectPtr<ADWPlayerCharacter> Player;
    TWeakObjectPtr<UDWInventoryComponent> Inventory;
    TWeakObjectPtr<ADWCombatCorePickup> Pickup;
    DWCombatCommit::FEntityLife PlayerLife;
    DWCombatCommit::FReceiptId Receipt;
    FGuid PickupId;
    int64 InventoryRevision=0;
    FName ItemId;
    int32 Count=0;
};
/** Concrete one-item claim; no public delayed-notification or arbitrary callback facility. */
class FDWCombatCorePickupTransaction final
{
    friend class ADWCombatCorePickup;
    friend class UDWCombatAuthoritySubsystem;
    static bool Commit(ADWCombatCorePickup& Pickup,ADWPlayerCharacter& Player);
    static void Publish(const FDWCombatCorePickupNotice& Notice);
};
