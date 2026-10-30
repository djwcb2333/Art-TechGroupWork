#pragma once

// External LANG-UI candidate. Presentation/draft only: correctness and rewards belong to the adapter.
#include "DWLanguageUIContract.h"
#include "Widgets/SCompoundWidget.h"

class SCanvas;
class FDragDropOperation;
class SDWPuzzleToken;
class FDWPuzzleWordDrag;

class SDWLanguagePuzzlePanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWLanguagePuzzlePanel) {}
        SLATE_ARGUMENT(TSharedPtr<FDWLanguageUIContext>, Context)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);
    // Keep this panel alive while the book owns the journal; Refresh preserves the same draft.
    void Refresh();
    // Suspend before detaching from the overlay; resume only after the same interaction returns.
    void SetSuspended(bool bInSuspended);
    // Invalidate/cancel only this panel's drag; answers, selected slot and sampled words survive.
    void ResetDragEpoch();
    void SetWordMotionEnabled(bool bEnabled) { bWordMotionEnabled = bEnabled; }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnKeyDown(const FGeometry& Geometry, const FKeyEvent& Event) override;
    virtual void Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime) override;

private:
    struct FWordBody
    {
        FVector2D Position = FVector2D::ZeroVector; // Circle centre, in pool-local Slate units.
        FVector2D Velocity = FVector2D::ZeroVector;
        float Radius = 34.f;
        TSharedPtr<SDWPuzzleToken> Widget;
    };

    TSharedPtr<FDWLanguageUIContext> Context;
    FDWLanguagePuzzleView View;
    FString DraftSession;
    FName DraftPuzzle;
    TMap<FName, FName> Answers; // SlotId -> WordId. Repeated values are deliberately allowed.
    int32 ActiveSlot = 0;
    bool bPending = false;
    bool bSuspended = false;
    bool bWordMotionEnabled = true;
    bool bDefinitionValid = true;
    FText DefinitionIssue;
    FGuid DragOwner = FGuid::NewGuid();
    FName DraggedWord;
    TSharedPtr<SCanvas> PoolCanvas;
    TArray<FWordBody> Bodies;
    FVector2D PoolSize = FVector2D::ZeroVector;

    void ReconcileDraft();
    void Build();
    TSharedRef<SWidget> MakeInscription();
    TSharedRef<SWidget> MakePool();
    TSharedRef<SWidget> MakeAnswers();
    TSharedRef<SWidget> MakeSolved();
    TSharedRef<SWidget> MakeGlyphButton(const FDWLanguagePuzzleSlotView& Slot, FVector2D Size);
    FText NoteFor(FName EntryId) const;
    FText LabelFor(FName WordId) const;
    bool CanEdit() const;
    bool IsComplete() const;
    void SelectSlot(int32 Index);
    void FillSlot(int32 Index, FName WordId);
    void ClearSlot(int32 Index);
    FReply Submit();
    TSharedPtr<FDragDropOperation> BeginWordDrag(FName WordId, FName SourceSlot);
    bool AcceptDrop(int32 Index, const FDWPuzzleWordDrag& Drag);
    void LayoutPool(FVector2D Size);
    void StepPool(float DeltaTime);
};
