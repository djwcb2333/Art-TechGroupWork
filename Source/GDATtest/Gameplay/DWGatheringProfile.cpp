#include "DWGatheringProfile.h"

bool UDWGatheringProfile::IsInventoryItem(FName ItemId) const
{return !ItemId.IsNone()&&GetItemDefinition(ItemId)&&!NonInventoryItems.Contains(ItemId);}

int32 UDWGatheringProfile::GetConfiguredStackLimit(FName ItemId) const
{
    if(!IsInventoryItem(ItemId))return 0;
    const int32* Limit=StackLimits.Find(ItemId);
    return Limit?*Limit:MaxStackSize;
}

FName UDWGatheringProfile::GetRequiredStation(FName RecipeId) const
{const FName* Value=RecipeStations.Find(RecipeId);return Value?*Value:NAME_None;}

FName UDWGatheringProfile::GetBuildingOutput(FName RecipeId) const
{const FName* Value=RecipeBuildings.Find(RecipeId);return Value?*Value:NAME_None;}

bool UDWGatheringProfile::ValidateGatheringDefinitions(FString& Error) const
{
    Error.Reset();
    const auto Fail=[&](const FString& Message){Error=Message;return false;};
    if(MaxInventorySlots<1||MaxStackSize<1)return Fail(TEXT("Invalid gathering inventory capacity."));
    TSet<FName> ItemIds,RecipeIds;
    for(const auto& Item:Items)
    {
        if(Item.ItemId.IsNone()||ItemIds.Contains(Item.ItemId))return Fail(TEXT("Empty or duplicate gathering item identity."));
        ItemIds.Add(Item.ItemId);
    }
    for(FName Id:NonInventoryItems)
        if(Id.IsNone()||!ItemIds.Contains(Id))return Fail(TEXT("Unknown non-inventory building identity."));
    for(const auto& Pair:StackLimits)
        if(!IsInventoryItem(Pair.Key)||Pair.Value<1)return Fail(TEXT("Invalid gathering per-item stack limit."));
    for(const auto& Stack:StarterItems)
        if(!IsInventoryItem(Stack.ItemId)||Stack.Quantity<1)return Fail(TEXT("Invalid gathering starter item."));
    for(const auto& Pair:Tools)
    {
        const auto& Tool=Pair.Value;
        const bool bKnownType=Tool.ToolType==TEXT("Axe")||Tool.ToolType==TEXT("Pickaxe")||Tool.ToolType==TEXT("Hammer")||Tool.ToolType==TEXT("Spear");
        if(!IsInventoryItem(Pair.Key)||!bKnownType||!FMath::IsFinite(Tool.Sharpness)||Tool.Sharpness<=0.f||
            !FMath::IsFinite(Tool.UseIntervalSeconds)||Tool.UseIntervalSeconds<.05f||!FMath::IsFinite(Tool.ReachCm)||Tool.ReachCm<=0.f)
            return Fail(TEXT("Invalid gathering tool definition."));
    }
    for(const auto& Recipe:Recipes)
    {
        if(Recipe.RecipeId.IsNone()||RecipeIds.Contains(Recipe.RecipeId)||Recipe.Inputs.IsEmpty()||Recipe.Outputs.IsEmpty())
            return Fail(TEXT("Invalid gathering recipe identity or empty recipe."));
        RecipeIds.Add(Recipe.RecipeId);
        for(const auto& Input:Recipe.Inputs)
            if(!IsInventoryItem(Input.ItemId)||Input.Quantity<1)return Fail(TEXT("Invalid gathering recipe input."));
        const FName Building=GetBuildingOutput(Recipe.RecipeId);
        if(!Building.IsNone())
        {
            if(!NonInventoryItems.Contains(Building)||Recipe.Outputs.Num()!=1||Recipe.Outputs[0].ItemId!=Building||Recipe.Outputs[0].Quantity!=1)
                return Fail(TEXT("A building recipe must produce exactly one registered non-inventory building."));
        }
        else for(const auto& Output:Recipe.Outputs)
            if(!IsInventoryItem(Output.ItemId)||Output.Quantity<1)return Fail(TEXT("Invalid gathering recipe output."));
    }
    for(const auto& Pair:RecipeStations)
        if(!RecipeIds.Contains(Pair.Key)||Pair.Value.IsNone())return Fail(TEXT("Unknown recipe or empty station requirement."));
    for(const auto& Pair:RecipeBuildings)
        if(!RecipeIds.Contains(Pair.Key)||Pair.Value.IsNone())return Fail(TEXT("Unknown recipe or empty building output."));
    return true;
}
