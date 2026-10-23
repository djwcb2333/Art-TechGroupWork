#include "DWInventoryComponent.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"

bool UDWInventoryComponent::TryApplyCombatStackPolicy(const FDWCombatRules& Rules,FString& OutError)
{
    int32 CoreLimit=0;
    if(!Rules.TryGetCount(TEXT("core.stack_limit"),CoreLimit,OutError))return false;
    if(CoreLimit<1)
    {OutError=TEXT("The formal core stack limit must be a positive count.");return false;}
    if(!CanWriteInventory())
    {OutError=TEXT("The inventory owner is not writable.");return false;}
    // Preserve every unrelated item-specific policy. No repacking, truncating or deleting old stacks.
    TMap<FName,int32> Candidate=StackLimitOverrides;
    Candidate.Add(FDWCombatItemRegistry::MoldCoreId(),CoreLimit);
    Candidate.Add(FDWCombatItemRegistry::YeastCoreId(),CoreLimit);
    if(!AreSlotsValidWithPolicy(Slots,GetConfig(),Candidate)||Revision==MAX_int64)
    {OutError=TEXT("Core registration or current inventory is incompatible with the formal stack policy.");return false;}
    OutError.Reset();
    // This sole policy commit increments the inventory revision and then broadcasts.
    // Callers must recheck their own lifecycle after the external notification.
    return TrySetStackLimitOverrides(Candidate);
}
