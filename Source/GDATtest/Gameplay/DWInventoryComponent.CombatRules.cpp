#include "DWInventoryComponent.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatEquipment.h"
#include "DWGameplayConfig.h"

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

bool UDWInventoryComponent::GetCraftPermanentWeaponOutput(FName RecipeId,FName& OutItemId) const
{
    OutItemId=NAME_None;
    const auto* Recipe=GetConfig()->GetRecipeDefinition(RecipeId);
    if(!Recipe)return false;
    for(const auto& Output:Recipe->Outputs)
        if(FDWCombatItemRegistry::GetItemRole(Output.ItemId)==EDWCombatItemRole::PermanentWeapon)
        {
            // E1 permanent acquisition is one identity and one physical item in the same transaction.
            if(Recipe->Outputs.Num()!=1||Output.Quantity!=1)return false;
            OutItemId=Output.ItemId;
        }
    return true;
}

bool UDWInventoryComponent::CanCraftPermanentWeapon(FName ItemId) const
{
    return FDWCombatEquipmentTransaction::CanCraft(*this,ItemId);
}
