#pragma once

#include "DWLanguageUIContract.h"
#include "Widgets/SCompoundWidget.h"

/** Character-page talent view only. No equipment list, local point deduction, or gameplay effects. */
class SDWTalentPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWTalentPanel) {}
        SLATE_ARGUMENT(TSharedPtr<FDWLanguageUIContext>, Context)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    void Refresh();

private:
    TSharedPtr<FDWLanguageUIContext> Context;
    FString SessionId;
    FName SelectedId;
    bool bRequestPending = false;

    void Select(FName TalentId);
    FReply RequestChange(bool bForget);
    TSharedRef<SWidget> MakeGraph();
    TSharedRef<SWidget> MakeDetails();
};
