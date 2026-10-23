#pragma once

#include "CoreMinimal.h"
#include "DWGameplayConfig.h"
#include "DWGatheringProfile.generated.h"

USTRUCT(BlueprintType)
struct GDATTEST_API FDWGatheringToolDefinition
{
    GENERATED_BODY()

    /** Logical gathering kind, e.g. Axe, Pickaxe, Hammer or Spear; not a combat equipment alias. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tool") FName ToolType;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tool", meta=(ClampMin="0.01")) float Sharpness=25.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tool", meta=(ClampMin="0.05",Units="s")) float UseIntervalSeconds=1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Tool", meta=(ClampMin="1",Units="cm")) float ReachCm=250.f;
};

/** An opt-in map profile. Existing maps keep their original UDWGameplayConfig and item rules. */
UCLASS(BlueprintType)
class GDATTEST_API UDWGatheringProfile : public UDWGameplayConfig
{
    GENERATED_BODY()
public:
    /** Items and Recipes are inherited; this profile owns those arrays when their tables are None. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Inventory") TMap<FName,int32> StackLimits;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Inventory") TSet<FName> NonInventoryItems;
    /** New isolated test-session grants only; never applied by this data asset or during save restoration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Testing") TArray<FDWItemStack> StarterItems;
    /** Recipe -> required nearby station kind. None/absent means hand crafting. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Crafting") TMap<FName,FName> RecipeStations;
    /** Recipe -> building item identity. Building output is placed, never added to the bag. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Crafting") TMap<FName,FName> RecipeBuildings;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Tools") TMap<FName,FDWGatheringToolDefinition> Tools;
    /** Human-readable provenance for provisional values; not consumed by gameplay or UI. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Provenance") TMap<FName,FString> TestValueNotes;

    bool ValidateGatheringDefinitions(FString& OutError) const;
    bool IsInventoryItem(FName ItemId) const;
    int32 GetConfiguredStackLimit(FName ItemId) const;
    FName GetRequiredStation(FName RecipeId) const;
    FName GetBuildingOutput(FName RecipeId) const;
};
