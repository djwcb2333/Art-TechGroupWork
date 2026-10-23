#pragma once
#include "CoreMinimal.h"
class UDWInventoryComponent;
struct FDWInventorySlotSnapshot;
class FDWGatheringInventoryTransaction final
{
public:
    static bool TryCraftBuilding(UDWInventoryComponent& Inventory,FName RecipeId,bool bYeast);
    static bool TryDiscardToWorld(UDWInventoryComponent& Inventory,const FDWInventorySlotSnapshot& Snapshot);
};
