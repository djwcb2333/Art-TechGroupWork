#pragma once

#include "DWLanguageUIContract.h"
#include "Widgets/SCompoundWidget.h"
class SScrollBox;

/** Presentation-only offering / NPC trade panel. Each selection is acknowledged by authority;
 * the local Staged map is only its display mirror and never modifies inventory. */
class SDWLanguageExchangePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWLanguageExchangePanel) {}
        SLATE_ARGUMENT(TSharedPtr<FDWLanguageUIContext>, Context)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh();

private:
    TSharedPtr<FDWLanguageUIContext> Context;
    TMap<FName, int32> Staged;
    TMap<FName, TWeakPtr<SWidget>> InventoryTiles;
    TMap<FName, TWeakPtr<SWidget>> StagedTiles;
    TSharedPtr<SScrollBox> InventoryScroll;
    TSharedPtr<SScrollBox> StagedScroll;
    float InventoryOffset = 0.f;
    float StagedOffset = 0.f;
    FString SessionId;
    FName TargetId;
    EDWLanguageExchangeMode Mode = EDWLanguageExchangeMode::Offering;
    FGuid DragOwnerId;
    bool bSubmitting = false;
    bool bTradeSuccessHold = false;

    void ReconcileStaging(bool bAnnounceInvalidation = true);
    int64 GetStagedUnits() const;
    void ChangeOne(FName StackId, bool bRemove);
    FReply Submit();
    TSharedRef<SWidget> MakeInscription(const TArray<FName>& EntryIds);
    TSharedRef<SWidget> MakeInventory(bool bStagingArea);
    bool AcceptDrop(const FDragDropEvent& Event, bool bToStaging);
    bool CanEdit() const;
};
