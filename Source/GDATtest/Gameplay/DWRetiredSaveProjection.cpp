#include "DWRetiredSaveProjection.h"
#include "DWRetiredItemPolicy.h"
#include "DWSaveGame.h"
#include "DWGatheringSave.h"
#include "DWGatheringWorldPersistence.h"

bool DWRetiredSaveProjection::ApplyToDuplicate(UDWSaveGame& Copy,FString& Error,int32 MaxInventorySlots)
{
    Error.Reset();
    bool bChangesWorld=false;
    for(const auto& Row:Copy.CombatWorld.Gathering.Actors)
    {
        if(Row.Kind==EDWGatheringSavedKind::Pickup&&DWRetiredItemPolicy::IsRetired(Row.ItemId))bChangesWorld=true;
        for(const auto& Drop:Row.Drops)if(DWRetiredItemPolicy::IsRetired(Drop.ItemId))bChangesWorld=true;
        for(const auto& Cost:Row.DemolitionCostInputs)if(DWRetiredItemPolicy::IsRetired(Cost.ItemId))bChangesWorld=true;
    }
    // Preserve strict identity/schema checks BEFORE filtering affected world rows;
    // otherwise a duplicate retired actor could disappear and evade validation. Do not
    // impose a new world-schema restriction on legacy inventory-only restore paths.
    if(bChangesWorld&&!FDWGatheringWorldPersistence::Validate(Copy.CombatWorld.Gathering,Copy.MapPackage,Copy.Inventory,
        Copy.CombatWorld.bNewAdventure,Error))return false;
    bool bHasRetiredInventory=false;
    for(const auto& Stack:Copy.Inventory)
        if(DWRetiredItemPolicy::IsRetired(Stack.ItemId))
        {
            bHasRetiredInventory=true;
            // Both audited pre-retirement gathering profiles had stack limit 10 for every retired item.
            // Unknown historic variants stay protected and require a separate migration audit.
            if(Stack.Quantity<=0||Stack.Quantity>10)
        {Error=TEXT("Malformed retired inventory stack; original save was preserved.");return false;}
        }
    if(bHasRetiredInventory&&(MaxInventorySlots<1||Copy.Inventory.Num()>MaxInventorySlots))
    {Error=TEXT("Saved inventory exceeds its configured slot capacity; original save was preserved.");return false;}
    auto& Rows=Copy.CombatWorld.Gathering.Actors;
    for(const auto& Row:Rows)
    {
        if(Row.Kind==EDWGatheringSavedKind::Pickup&&DWRetiredItemPolicy::IsRetired(Row.ItemId)&&Row.Quantity<0)
        {Error=TEXT("Malformed retired pickup; original save was preserved.");return false;}
        if(Row.Kind==EDWGatheringSavedKind::Resource)
            for(const auto& Drop:Row.Drops)if(DWRetiredItemPolicy::IsRetired(Drop.ItemId)&&Drop.Quantity<=0)
            {Error=TEXT("Malformed retired resource reward; original save was preserved.");return false;}
        if(Row.Kind==EDWGatheringSavedKind::Station)
            for(const auto& Stack:Row.DemolitionCostInputs)if(DWRetiredItemPolicy::IsRetired(Stack.ItemId)&&Stack.Quantity<=0)
            {Error=TEXT("Malformed retired building cost; original save was preserved.");return false;}
    }
    // Do not touch historical language receipts: their exact IDs remain known for validation.
    DWRetiredItemPolicy::NormalizeLegacyInventory(Copy.Inventory);
    Rows.RemoveAll([](const FDWGatheringActorSave& Row){return Row.Kind==EDWGatheringSavedKind::Pickup&&
        Row.bRuntimeSpawned&&DWRetiredItemPolicy::IsRetired(Row.ItemId);});
    for(auto& Row:Rows)
    {
        if(Row.Kind==EDWGatheringSavedKind::Pickup&&DWRetiredItemPolicy::IsRetired(Row.ItemId))
        {
            // Authored identities must remain as inert tombstones or restore rejects the map.
            Row.Quantity=0;Row.PickupMesh=FSoftObjectPath();Row.PickupMaterials.Reset();
        }
        if(Row.Kind==EDWGatheringSavedKind::Resource)
            Row.Drops.RemoveAll([](const FDWItemStack& Stack){return DWRetiredItemPolicy::IsRetired(Stack.ItemId);});
        if(Row.Kind==EDWGatheringSavedKind::Station)
        {
            Row.DemolitionCostInputs.RemoveAll([](const FDWItemStack& Stack){return DWRetiredItemPolicy::IsRetired(Stack.ItemId);});
            if((Row.DemolitionCostOrigin==1||Row.DemolitionCostOrigin==3||Row.DemolitionCostOrigin==4)&&Row.DemolitionCostInputs.IsEmpty())
            {Row.DemolitionCostOrigin=2;Row.DemolitionCostRecipe=NAME_None;}
        }
    }
    return true;
}
