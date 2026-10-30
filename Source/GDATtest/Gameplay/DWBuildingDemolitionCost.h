#pragma once
#include "CoreMinimal.h"
#include "DWGameplayTypes.h"
class UDWGameplayConfig;

// Recipe-at-placement is a definition snapshot, NOT a record of historical payment.
struct FDWBuildingDemolitionCost
{
    FName RecipeId;
    TArray<FDWItemStack> Inputs;
    static bool ResolveUnique(const UDWGameplayConfig& Config,FName BuildingItem,FDWBuildingDemolitionCost& Out,FString& Error);
    static bool Validate(const UDWGameplayConfig& Config,FName BuildingItem,const FDWBuildingDemolitionCost& Cost,FString& Error);
    bool Matches(const FDWBuildingDemolitionCost& Other) const;
};
