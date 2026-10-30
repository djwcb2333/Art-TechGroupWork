#pragma once
#include "CoreMinimal.h"
#include "DWGameplayTypes.h"

// Narrow policy: names not in the explicit retirement list remain subject to existing validation.
// No live data assets or disk saves are modified by these methods.
namespace DWRetiredItemPolicy
{
inline bool IsRetired(FName Id)
{
    static const TSet<FName> Retired={
        FName(TEXT("Jelly")),FName(TEXT("GummyCandy")),FName(TEXT("Waffle")),FName(TEXT("CandyCane")),FName(TEXT("Marshmallow")),
        FName(TEXT("LicoriceCandy")),FName(TEXT("GingerbreadMan")),FName(TEXT("Tofu")),FName(TEXT("Cocoa")),FName(TEXT("Beeswax")),
        FName(TEXT("Salt")),FName(TEXT("Acid")),FName(TEXT("ElementC")),FName(TEXT("ElementH")),FName(TEXT("ElementO")),
        FName(TEXT("ElementN")),FName(TEXT("ElementP")),FName(TEXT("ElementK")),FName(TEXT("ElementS"))};
    return Retired.Contains(Id);
}
inline void NormalizeLegacyInventory(TArray<FDWItemStack>& Slots)
{Slots.RemoveAll([](const FDWItemStack& Item){return IsRetired(Item.ItemId);});}
inline bool RecipeUsesRetired(const FDWRecipeDefinition& Recipe)
{
    for(const auto& Item:Recipe.Inputs)if(IsRetired(Item.ItemId))return true;
    for(const auto& Item:Recipe.Outputs)if(IsRetired(Item.ItemId))return true;
    return false;
}
}
