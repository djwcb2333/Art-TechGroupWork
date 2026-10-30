#pragma once
#include "CoreMinimal.h"
#include "DWLanguageUICommon.h"
#include "Widgets/SToolTip.h"

struct FDWCraftingConditionView
{
    FName ConditionId;
    TSoftObjectPtr<UTexture2D> Icon;
    FText Name;
    FText Description;
    FText MissingReason;
    bool bSatisfied = false;
};
struct FDWItemTooltipView
{
    FName ItemId;
    bool bOccupied = false;
    FText Name;
    FText Use;
    int32 Quantity = 0;
    int32 StackLimit = 0;
    TSoftObjectPtr<UTexture2D> Icon;
    TArray<FText> Attributes;
    FText Product;
    TArray<FText> Materials;
    TArray<FDWCraftingConditionView> Conditions;
    FText BlockedReason;
};
namespace DWItemTooltip
{
    // Native Slate tooltip placement keeps the popup within the current monitor/work area.
    // Attach to the actual slot, never a second hit-test layer. Empty slots return nullptr.
    TSharedPtr<SToolTip> Make(const TSharedRef<FDWLanguageUIContext>& Context,const FDWItemTooltipView& View);
    void Update(const TSharedRef<SToolTip>& Tooltip,const TSharedRef<FDWLanguageUIContext>& Context,const FDWItemTooltipView& View);
    TSharedRef<SWidget> Needs(const TSharedRef<FDWLanguageUIContext>& Context,const TArray<FDWCraftingConditionView>& Conditions);
}
