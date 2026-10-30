#include "DWBuildingDemolitionCost.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.h"

bool FDWBuildingDemolitionCost::Matches(const FDWBuildingDemolitionCost& Other) const
{
    if(RecipeId!=Other.RecipeId||Inputs.Num()!=Other.Inputs.Num())return false;
    for(const auto& Input:Inputs)
    {
        const auto* OtherInput=Other.Inputs.FindByPredicate([&](const FDWItemStack& Item){return Item.ItemId==Input.ItemId;});
        if(!OtherInput||Input.Quantity!=OtherInput->Quantity)return false;
    }
    return true;
}

bool FDWBuildingDemolitionCost::Validate(const UDWGameplayConfig& Config,FName BuildingItem,const FDWBuildingDemolitionCost& Cost,FString& Error)
{
    Error.Reset();const auto* Profile=UDWGameplayConfig::AsGatheringConfig(&Config);
    if(!Profile||BuildingItem.IsNone()||Cost.RecipeId.IsNone()||Cost.Inputs.IsEmpty()||Cost.Inputs.Num()>256)
    {Error=TEXT("Building cost snapshot is absent or invalid.");return false;}
    TSet<FName> Seen;
    for(const auto& Item:Cost.Inputs)
    {
        if(Item.ItemId.IsNone()||Item.ItemId==BuildingItem||Item.Quantity<=0||Seen.Contains(Item.ItemId)||
           !Profile->IsInventoryItem(Item.ItemId)||!Config.GetItemDefinition(Item.ItemId))
        {Error=TEXT("Building cost snapshot contains an invalid material.");return false;}
        Seen.Add(Item.ItemId);
    }
    return true;
}

bool FDWBuildingDemolitionCost::ResolveUnique(const UDWGameplayConfig& Config,FName BuildingItem,FDWBuildingDemolitionCost& Out,FString& Error)
{
    Out={};Error.Reset();
    const auto* Profile=UDWGameplayConfig::AsGatheringConfig(&Config);
    if(!Profile||BuildingItem.IsNone()||!Config.GetBuildingDefinition(BuildingItem))
    {Error=TEXT("Building item is not registered in the current gathering profile.");return false;}
    const FDWRecipeDefinition* Match=nullptr;
    for(const auto& Recipe:Config.Recipes)
    {
        bool bMentions=false;
        for(const auto& Output:Recipe.Outputs)if(Output.ItemId==BuildingItem)bMentions=true;
        if(!bMentions)continue;
        if(Match){Error=TEXT("Building recipe is not unique.");return false;}
        Match=&Recipe;
    }
    if(!Match||Match->RecipeId.IsNone()||Match->Outputs.Num()!=1||Match->Outputs[0].ItemId!=BuildingItem||
       Match->Outputs[0].Quantity!=1||Match->Inputs.IsEmpty()||Profile->GetBuildingOutput(Match->RecipeId)!=BuildingItem)
    {Error=TEXT("A unique one-building recipe is required.");return false;}
    FDWBuildingDemolitionCost Candidate;Candidate.RecipeId=Match->RecipeId;
    for(const auto& Input:Match->Inputs)
    {
        if(Input.ItemId.IsNone()||Input.ItemId==BuildingItem||Input.Quantity<=0)
        {Error=TEXT("Building recipe has invalid inputs.");return false;}
        auto* Existing=Candidate.Inputs.FindByPredicate([&](const FDWItemStack& I){return I.ItemId==Input.ItemId;});
        if(Existing)
        {
            if(Input.Quantity>MAX_int32-Existing->Quantity){Error=TEXT("Building material quantity overflow.");return false;}
            Existing->Quantity+=Input.Quantity;
        }
        else Candidate.Inputs.Add(Input);
    }
    if(!Validate(Config,BuildingItem,Candidate,Error))return false;
    Out=MoveTemp(Candidate);return true;
}
