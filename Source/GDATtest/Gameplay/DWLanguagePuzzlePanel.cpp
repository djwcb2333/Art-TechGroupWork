#include "DWLanguagePuzzlePanel.h"
#include "DWLanguageUICommon.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Input/DragAndDrop.h"
#include "Input/Reply.h"
#include "InputCoreTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Widgets/SCanvas.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

// In-game, windowless native Slate drag. IDs are proposals, never translations or item/reward state.
class FDWPuzzleWordDrag : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FDWPuzzleWordDrag, FDragDropOperation)
    FGuid Owner;
    FString Session;
    FName Puzzle;
    FName Word;
    FName SourceSlot; // None => copy from pool; otherwise swap two draft positions.
    FText Label;
    FSlateFontInfo Font;
    TFunction<void()> Ended;
    TSharedPtr<SWidget> Decorator;
    FVector2D DecoratorPosition = FVector2D::ZeroVector;
    FSlateRoundedBoxBrush Back = FSlateRoundedBoxBrush(FLinearColor::White, 16.f);

    static TSharedRef<FDWPuzzleWordDrag> New(const TSharedRef<FDWLanguageUIContext>& C,
        FGuid InOwner, FName InPuzzle, FName InWord, FName InSource, const FText& InLabel,
        TFunction<void()> OnEnded)
    {
        TSharedRef<FDWPuzzleWordDrag> Op = MakeShared<FDWPuzzleWordDrag>();
        Op->Owner = InOwner; Op->Session = C->GetSnapshot().SessionId;
        Op->Puzzle = InPuzzle; Op->Word = InWord; Op->SourceSlot = InSource;
        Op->Label = InLabel; Op->Font = C->Font(20); Op->Ended = MoveTemp(OnEnded);
        Op->bCreateNewWindow = false;
        Op->DecoratorPosition = FSlateApplication::Get().GetCursorPos() + FVector2D(12);
        Op->Decorator = SNew(SBorder).BorderImage(&Op->Back).BorderBackgroundColor(DWLanguageUI::Gold())
            .Padding(FMargin(18, 12)).Visibility(EVisibility::HitTestInvisible)
            [SNew(STextBlock).Text(Op->Label).Font(Op->Font).ColorAndOpacity(DWLanguageUI::Ink())];
        Op->Construct();
        return Op;
    }
    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
    {
        return Decorator;
    }
    virtual FVector2D GetDecoratorPosition() const override { return DecoratorPosition; }
    virtual void OnDragged(const FDragDropEvent& Event) override
    { DecoratorPosition = Event.GetScreenSpacePosition() + FVector2D(12); }
    virtual void OnDrop(bool bHandled, const FPointerEvent& Event) override
    {
        FDragDropOperation::OnDrop(bHandled, Event);
        if (Ended) Ended();
    }
};

// A centered, focusable paper token that supports the same click and drag semantics.
// It does not use an SButton child, so drag detection cannot be swallowed by a child button.
class SDWPuzzleToken : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWPuzzleToken) : _Circle(false) {}
        SLATE_ARGUMENT(TSharedPtr<FDWLanguageUIContext>, Context)
        SLATE_ATTRIBUTE(FText, Label)
        SLATE_ATTRIBUTE(bool, Selected)
        SLATE_ARGUMENT(bool, Circle)
        SLATE_ARGUMENT(TFunction<void()>, Clicked)
        SLATE_ARGUMENT(TFunction<void()>, Cleared)
        SLATE_ARGUMENT(TFunction<TSharedPtr<FDragDropOperation>()>, StartDrag)
        SLATE_ARGUMENT(TFunction<bool(const FDWPuzzleWordDrag&)>, Dropped)
    SLATE_END_ARGS()

    void Construct(const FArguments& A)
    {
        C = A._Context; Label = A._Label; Selected = A._Selected;
        Clicked = A._Clicked; Cleared = A._Cleared; StartDrag = A._StartDrag; Dropped = A._Dropped;
        Shape = FSlateRoundedBoxBrush(FLinearColor::White, A._Circle ? 1000.f : 16.f);
        SetCursor(EMouseCursor::Hand);
        SetToolTipText(Label);
        ChildSlot
        [SNew(SBorder).BorderImage(&Shape).Padding(FMargin(A._Circle ? 10.f : 18.f, 10.f))
            .BorderBackgroundColor_Lambda([this]
            {
                const FLinearColor Base = (bDropOver || Selected.Get(false)) ? DWLanguageUI::Gold() : DWLanguageUI::Paper() * .91f;
                return FSlateColor((IsHovered() || HasKeyboardFocus()) ? FMath::Lerp(Base, FLinearColor::White, .14f) : Base);
            })
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                .HAlign(HAlign_Center).VAlign(VAlign_Center)
                [SNew(STextBlock).Text(Label).Font(C->Font(19)).ColorAndOpacity(DWLanguageUI::Ink())
                    .Justification(ETextJustify::Center).AutoWrapText(false)]]];
    }
    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton || !IsEnabled()) return FReply::Unhandled();
        bPressed = true;
        FReply R = FReply::Handled().CaptureMouse(AsShared()).SetUserFocus(AsShared(), EFocusCause::Mouse);
        return StartDrag ? R.DetectDrag(AsShared(), EKeys::LeftMouseButton) : R;
    }
    virtual FReply OnMouseButtonUp(const FGeometry& G, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() != EKeys::LeftMouseButton || !HasMouseCapture()) return FReply::Unhandled();
        const bool bActivate = bPressed && G.IsUnderLocation(E.GetScreenSpacePosition()) && IsEnabled();
        bPressed = false;
        if (bActivate && Clicked) Clicked();
        return FReply::Handled().ReleaseMouseCapture();
    }
    virtual FReply OnMouseButtonDoubleClick(const FGeometry&, const FPointerEvent& E) override
    {
        if (E.GetEffectingButton() == EKeys::LeftMouseButton && IsEnabled() && Cleared)
        {
            bPressed = false; Cleared(); return FReply::Handled().ReleaseMouseCapture();
        }
        return FReply::Unhandled();
    }
    virtual void OnMouseCaptureLost(const FCaptureLostEvent&) override { bPressed = false; }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& E) override
    {
        if (!IsEnabled()) return FReply::Unhandled();
        if ((E.GetKey() == EKeys::Enter || E.GetKey() == EKeys::SpaceBar) && Clicked)
        { Clicked(); return FReply::Handled(); }
        if ((E.GetKey() == EKeys::Delete || E.GetKey() == EKeys::BackSpace) && Cleared)
        { Cleared(); return FReply::Handled(); }
        return FReply::Unhandled();
    }
    virtual FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
    {
        bPressed = false;
        const TSharedPtr<FDragDropOperation> Op = IsEnabled() && StartDrag ? StartDrag() : nullptr;
        return Op.IsValid() ? FReply::Handled().ReleaseMouseCapture().BeginDragDrop(Op.ToSharedRef()) : FReply::Unhandled();
    }
    virtual void OnDragEnter(const FGeometry&, const FDragDropEvent& E) override
    { bDropOver = Dropped && E.GetOperationAs<FDWPuzzleWordDrag>().IsValid() && IsEnabled(); }
    virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent& E) override
    {
        bDropOver = Dropped && E.GetOperationAs<FDWPuzzleWordDrag>().IsValid() && IsEnabled();
        return bDropOver ? FReply::Handled() : FReply::Unhandled();
    }
    virtual void OnDragLeave(const FDragDropEvent&) override { bDropOver = false; }
    virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& E) override
    {
        bDropOver = false;
        TSharedPtr<FDWPuzzleWordDrag> Op = E.GetOperationAs<FDWPuzzleWordDrag>();
        return IsEnabled() && Dropped && Op.IsValid() && Dropped(*Op) ? FReply::Handled() : FReply::Unhandled();
    }
private:
    TSharedPtr<FDWLanguageUIContext> C;
    TAttribute<FText> Label;
    TAttribute<bool> Selected;
    FSlateRoundedBoxBrush Shape = FSlateRoundedBoxBrush(FLinearColor::White, 16.f);
    TFunction<void()> Clicked, Cleared;
    TFunction<TSharedPtr<FDragDropOperation>()> StartDrag;
    TFunction<bool(const FDWPuzzleWordDrag&)> Dropped;
    bool bPressed = false, bDropOver = false;
};

void SDWLanguagePuzzlePanel::Construct(const FArguments& InArgs)
{
    check(InArgs._Context.IsValid());
    Context = InArgs._Context;
    SetCanTick(true);
    Refresh();
}

void SDWLanguagePuzzlePanel::SetSuspended(bool bInSuspended)
{
    const bool bStartingSuspension = bInSuspended && !bSuspended;
    bSuspended = bInSuspended;
    SetEnabled(!bSuspended);
    if (bStartingSuspension) ResetDragEpoch();
}

void SDWLanguagePuzzlePanel::ResetDragEpoch()
{
    const FGuid PreviousOwner = DragOwner;
    DragOwner = FGuid::NewGuid();
    DraggedWord = NAME_None;
    if (!FSlateApplication::IsInitialized()) return;
    const TSharedPtr<FDragDropOperation> Current = FSlateApplication::Get().GetDragDroppingContent();
    if (Current.IsValid() && Current->IsOfType<FDWPuzzleWordDrag>())
    {
        const TSharedPtr<FDWPuzzleWordDrag> WordDrag = StaticCastSharedPtr<FDWPuzzleWordDrag>(Current);
        if (WordDrag->Owner == PreviousOwner) FSlateApplication::Get().CancelDragDrop();
    }
}

void SDWLanguagePuzzlePanel::Refresh()
{
    View = Context->GetSnapshot().Puzzle;
    ReconcileDraft();
    Build();
}

void SDWLanguagePuzzlePanel::ReconcileDraft()
{
    const FString& Session = Context->GetSnapshot().SessionId;
    if (DraftSession != Session || DraftPuzzle != View.Id)
    {
        Answers.Reset(); ActiveSlot = 0; DraftSession = Session; DraftPuzzle = View.Id;
        ResetDragEpoch();
    }
    bDefinitionValid = !View.Id.IsNone() && !Session.IsEmpty();
    DefinitionIssue = FText::GetEmpty();
    TSet<FName> SlotIds, WordIds;
    for (const FDWLanguageWordView& Word : View.WordPool)
    {
        if (Word.Id.IsNone() || WordIds.Contains(Word.Id)) bDefinitionValid = false;
        WordIds.Add(Word.Id);
    }
    for (const FDWLanguagePuzzleSlotView& Slot : View.Slots)
    {
        if (Slot.SlotId.IsNone() || SlotIds.Contains(Slot.SlotId)) bDefinitionValid = false;
        SlotIds.Add(Slot.SlotId);
    }
    for (auto It = Answers.CreateIterator(); It; ++It)
        if (!SlotIds.Contains(It.Key()) || !WordIds.Contains(It.Value())) It.RemoveCurrent();
    if (!View.bSolved && (View.Slots.IsEmpty() || View.WordPool.IsEmpty())) bDefinitionValid = false;
    if (!bDefinitionValid)
        DefinitionIssue = DWLanguageUI::L(Context.ToSharedRef(), TEXT("当前谜题数据不可用"), TEXT("This puzzle is unavailable"));
    ActiveSlot = FMath::Clamp(ActiveSlot, 0, FMath::Max(0, View.Slots.Num() - 1));
}

bool SDWLanguagePuzzlePanel::CanEdit() const
{
    const FDWLanguageUISnapshot& Current = Context->GetSnapshot();
    return Current.bSessionValid && Current.SessionId == DraftSession && Current.Puzzle.Id == View.Id
        && Current.Puzzle.bAvailable && !Current.Puzzle.bSolved
        && View.bAvailable && !View.bSolved && bDefinitionValid && !bPending && !bSuspended && IsEnabled();
}

bool SDWLanguagePuzzlePanel::IsComplete() const
{
    if (!bDefinitionValid || View.Slots.IsEmpty()) return false;
    for (const FDWLanguagePuzzleSlotView& Slot : View.Slots)
        if (!Answers.Contains(Slot.SlotId) || Answers.FindChecked(Slot.SlotId).IsNone()) return false;
    return true;
}

FText SDWLanguagePuzzlePanel::NoteFor(FName EntryId) const
{
    const FDWLanguageUIEntryView* Entry = Context->GetSnapshot().FindEntry(EntryId);
    if (Entry && Entry->bDiscovered && !Entry->SavedGuess.TrimStartAndEnd().IsEmpty()) return FText::FromString(Entry->SavedGuess);
    return DWLanguageUI::L(Context.ToSharedRef(), TEXT("未记录含义"), TEXT("No meaning recorded"));
}

FText SDWLanguagePuzzlePanel::LabelFor(FName WordId) const
{
    const FDWLanguageWordView* Word = View.WordPool.FindByPredicate([WordId](const FDWLanguageWordView& W) { return W.Id == WordId; });
    return Word ? Word->Label : FText::GetEmpty();
}

TSharedRef<SWidget> SDWLanguagePuzzlePanel::MakeGlyphButton(const FDWLanguagePuzzleSlotView& Slot, FVector2D Size)
{
    const FName EntryId = Slot.EntryId;
    return SNew(SButton).ButtonStyle(&DWLanguageUI::ButtonStyle()).ContentPadding(FMargin(6))
        .HAlign(HAlign_Center).VAlign(VAlign_Center).ToolTipText(NoteFor(EntryId))
        .OnClicked_Lambda([this, EntryId]
        {
            if (!bPending && !bSuspended) Context->OpenJournal(EntryId);
            return FReply::Handled();
        })
        [Context->Glyph(Slot.Glyph, Size)]; // Physical inscription is visible even before journal discovery.
}

TSharedRef<SWidget> SDWLanguagePuzzlePanel::MakeInscription()
{
    TSharedRef<SHorizontalBox> Symbols = SNew(SHorizontalBox);
    for (const FDWLanguagePuzzleSlotView& Slot : View.Slots)
        Symbols->AddSlot().AutoWidth().Padding(4, 2)[MakeGlyphButton(Slot, FVector2D(48, 58))];
    return SNew(SBox).HeightOverride(144.f)
        [DWLanguageUI::Card(
            SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                [DWLanguageUI::Text(Context.ToSharedRef(), DWLanguageUI::L(Context.ToSharedRef(), TEXT("残骸铭文"), TEXT("Wreck inscription")), 20, DWLanguageUI::Ink())]
            + SVerticalBox::Slot().FillHeight(1)
                [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                    .HAlign(HAlign_Left).VAlign(VAlign_Center)[Symbols]], FMargin(18))];
}

TSharedRef<SWidget> SDWLanguagePuzzlePanel::MakePool()
{
    Bodies.Reset(); PoolSize = FVector2D::ZeroVector;
    SAssignNew(PoolCanvas, SCanvas);
    Bodies.SetNum(View.WordPool.Num());
    for (int32 I = 0; I < View.WordPool.Num(); ++I)
    {
        const FDWLanguageWordView Word = View.WordPool[I];
        TSharedPtr<SDWPuzzleToken> Token;
        SAssignNew(Token, SDWPuzzleToken).Context(Context).Label(Word.Label).Circle(true)
            .Selected(false).IsEnabled_Lambda([this] { return CanEdit(); })
            .Clicked([this, Id = Word.Id] { FillSlot(ActiveSlot, Id); })
            .StartDrag([this, Id = Word.Id] { return BeginWordDrag(Id, NAME_None); });
        Bodies[I].Widget = Token;
        PoolCanvas->AddSlot()
            .Position_Lambda([this, I] { return Bodies.IsValidIndex(I) ? Bodies[I].Position - FVector2D(Bodies[I].Radius) : FVector2D::ZeroVector; })
            .Size_Lambda([this, I] { return Bodies.IsValidIndex(I) ? FVector2D(Bodies[I].Radius * 2.f) : FVector2D(1); })
            [Token.ToSharedRef()];
    }
    LayoutPool(FVector2D(502, 290));
    // This canvas receives the actual available pool size. No virtual content or scroll viewport.
    return SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)[PoolCanvas.ToSharedRef()];
}

TSharedRef<SWidget> SDWLanguagePuzzlePanel::MakeAnswers()
{
    TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
    for (int32 I = 0; I < View.Slots.Num(); ++I)
    {
        const FDWLanguagePuzzleSlotView Slot = View.Slots[I];
        Rows->AddSlot().FillHeight(1).VAlign(VAlign_Center).Padding(0, 0, 0, I + 1 < View.Slots.Num() ? 10.f : 0.f)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [MakeGlyphButton(Slot, FVector2D(42, 54))]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
                [SNew(SBox).HeightOverride(66)
                    [SNew(SDWPuzzleToken).Context(Context).Circle(false)
                        .Label_Lambda([this, Id = Slot.SlotId]
                        {
                            const FName* Word = Answers.Find(Id);
                            return Word ? LabelFor(*Word) : DWLanguageUI::L(Context.ToSharedRef(), TEXT("选择含义"), TEXT("Choose meaning"));
                        })
                        .Selected_Lambda([this, I] { return ActiveSlot == I; })
                        .IsEnabled_Lambda([this] { return CanEdit(); })
                        .Clicked([this, I] { SelectSlot(I); })
                        .Cleared([this, I] { ClearSlot(I); })
                        .StartDrag([this, Id = Slot.SlotId]() -> TSharedPtr<FDragDropOperation>
                        {
                            const FName* Word = Answers.Find(Id);
                            return Word ? BeginWordDrag(*Word, Id) : nullptr;
                        })
                        .Dropped([this, I](const FDWPuzzleWordDrag& Drag) { return AcceptDrop(I, Drag); })]]];
    }
    return Rows;
}

TSharedRef<SWidget> SDWLanguagePuzzlePanel::MakeSolved()
{
    // This function is only reached from authoritative bSolved. Never render preview answers/stories.
    TSharedRef<SVerticalBox> Stories = SNew(SVerticalBox);
    if (!View.StoryChinese.IsEmpty())
        Stories->AddSlot().AutoHeight().Padding(0,0,0,18)
            [DWLanguageUI::Text(Context.ToSharedRef(), View.StoryChinese, 22, DWLanguageUI::Ink(), true)];
    if (!View.StoryEnglish.IsEmpty())
        Stories->AddSlot().AutoHeight()
            [DWLanguageUI::Text(Context.ToSharedRef(), View.StoryEnglish, 22, DWLanguageUI::Ink(), true)];
    if (View.StoryChinese.IsEmpty() && View.StoryEnglish.IsEmpty())
        Stories->AddSlot().AutoHeight()[DWLanguageUI::Text(Context.ToSharedRef(),
            DWLanguageUI::L(Context.ToSharedRef(), TEXT("记录暂不可用"), TEXT("The record is unavailable")),22,DWLanguageUI::Ink(),true)];
    TSharedRef<SVerticalBox> Content = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 18)
            [DWLanguageUI::Text(Context.ToSharedRef(), DWLanguageUI::L(Context.ToSharedRef(), TEXT("残骸已破译"), TEXT("Wreck deciphered")), 28, DWLanguageUI::Ink())]
        + SVerticalBox::Slot().FillHeight(1)
            [SNew(SScrollBox) + SScrollBox::Slot()[Stories]];
    if (View.bRewardGranted && !View.RewardDescription.IsEmpty())
        Content->AddSlot().AutoHeight().Padding(0, 20, 0, 0)
            [DWLanguageUI::Text(Context.ToSharedRef(), View.RewardDescription, 20, DWLanguageUI::Muted(), true)];
    return DWLanguageUI::Card(Content, FMargin(28));
}

void SDWLanguagePuzzlePanel::Build()
{
    const TSharedRef<FDWLanguageUIContext> C = Context.ToSharedRef();
    ResetDragEpoch(); // Rebuilt tokens cannot accept a drag created by their prior widget tree.
    PoolCanvas.Reset(); Bodies.Reset(); PoolSize = FVector2D::ZeroVector;
    TSharedRef<SVerticalBox> Root = SNew(SVerticalBox);
    Root->AddSlot().AutoHeight().Padding(0, 0, 0, 16)
        [SNew(SBox).HeightOverride(44.f)
            [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
                .HAlign(HAlign_Left).VAlign(VAlign_Center)
                [DWLanguageUI::Text(C, View.Title.IsEmpty() ? DWLanguageUI::L(C, TEXT("残骸破译"), TEXT("Decipher wreck")) : View.Title, 30, DWLanguageUI::Ink())]]];
    if (!View.bAvailable)
    {
        Root->AddSlot().FillHeight(1)[DWLanguageUI::Text(C,
            View.BlockedReason.IsEmpty() ? DWLanguageUI::L(C, TEXT("残骸交互已结束"), TEXT("Wreck interaction ended")) : View.BlockedReason,
            22, DWLanguageUI::Ink(), true)];
    }
    else if (View.bSolved)
    {
        Root->AddSlot().FillHeight(1)[MakeSolved()];
    }
    else if (!bDefinitionValid)
    {
        Root->AddSlot().FillHeight(1)[DWLanguageUI::Text(C, DefinitionIssue, 22, DWLanguageUI::Ink(), true)];
    }
    else
    {
        Root->AddSlot().FillHeight(1)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 24, 0)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)[MakeInscription()]
                    + SVerticalBox::Slot().FillHeight(1)
                        [DWLanguageUI::Card(
                            SNew(SVerticalBox)
                            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
                                [DWLanguageUI::Text(C, DWLanguageUI::L(C, TEXT("候选词"), TEXT("Word pool")), 20, DWLanguageUI::Ink())]
                            + SVerticalBox::Slot().FillHeight(1)[MakePool()], FMargin(18))]]
            + SHorizontalBox::Slot().FillWidth(1)
                [SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
                        [DWLanguageUI::Text(C, DWLanguageUI::L(C, TEXT("逐字选择含义，词可重复使用"), TEXT("Match each glyph. Words may be reused.")), 18, DWLanguageUI::Muted(), true)]
                    + SVerticalBox::Slot().FillHeight(1)[MakeAnswers()]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 0)
                        [DWLanguageUI::Text(C, View.bCanSubmit ? FText::GetEmpty() : View.BlockedReason, 18, DWLanguageUI::Muted(), true)]]];
    }
    TSharedRef<SHorizontalBox> Footer = SNew(SHorizontalBox);
    Footer->AddSlot().AutoWidth().Padding(0, 0, 12, 0)
        [SNew(SBox).IsEnabled(!bPending)
            [DWLanguageUI::CloseButton(C, FOnClicked::CreateLambda([this]
            { if (!bPending) { ResetDragEpoch(); Context->CloseLayer(); } return FReply::Handled(); }))]];
    Footer->AddSlot().AutoWidth().Padding(0, 0, 12, 0)
        [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("语言图鉴"), TEXT("Journal")), FOnClicked::CreateLambda([this]
        {
            if (!bPending) Context->OpenJournal(View.Slots.IsValidIndex(ActiveSlot) ? View.Slots[ActiveSlot].EntryId : NAME_None);
            return FReply::Handled();
        }), 140, View.bAvailable && !bPending)];
    Footer->AddSlot().FillWidth(1)[SNew(SBox)];
    if (View.bAvailable && !View.bSolved && bDefinitionValid)
    {
        Footer->AddSlot().AutoWidth().Padding(0, 0, 12, 0)
            [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("清空当前"), TEXT("Clear slot")),
                FOnClicked::CreateLambda([this] { ClearSlot(ActiveSlot); return FReply::Handled(); }), 140, !bPending)];
        Footer->AddSlot().AutoWidth()
            [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("提交"), TEXT("Submit")), FOnClicked::CreateSP(this, &SDWLanguagePuzzlePanel::Submit),
                140, View.bCanSubmit && !bPending, true)];
    }
    Root->AddSlot().AutoHeight().Padding(0, 16, 0, 0)
        [SNew(SBox).HeightOverride(56.f).VAlign(VAlign_Center)[Footer]];
    // Fit the whole interaction as one surface on small windows. The 15 sampled words,
    // five glyphs/answers and footer share this scale, rather than clipping individual regions.
    ChildSlot
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
            .HAlign(HAlign_Center).VAlign(VAlign_Center)
            [SNew(SBox).WidthOverride(1100.f).HeightOverride(650.f)[Root]]];
}

void SDWLanguagePuzzlePanel::SelectSlot(int32 Index)
{
    if (CanEdit() && View.Slots.IsValidIndex(Index)) ActiveSlot = Index;
}

void SDWLanguagePuzzlePanel::FillSlot(int32 Index, FName WordId)
{
    if (!CanEdit() || !View.Slots.IsValidIndex(Index) || !View.WordPool.ContainsByPredicate([WordId](const FDWLanguageWordView& W) { return W.Id == WordId; })) return;
    Answers.Add(View.Slots[Index].SlotId, WordId);
    ActiveSlot = Index;
    for (int32 Next = Index + 1; Next < View.Slots.Num(); ++Next)
        if (!Answers.Contains(View.Slots[Next].SlotId)) { ActiveSlot = Next; break; }
}

void SDWLanguagePuzzlePanel::ClearSlot(int32 Index)
{
    if (!CanEdit() || !View.Slots.IsValidIndex(Index)) return;
    Answers.Remove(View.Slots[Index].SlotId); ActiveSlot = Index;
}

FReply SDWLanguagePuzzlePanel::Submit()
{
    if (!CanEdit() || !View.bCanSubmit) return FReply::Handled();
    if (!IsComplete())
    {
        Context->Notice(DWLanguageUI::L(Context.ToSharedRef(), TEXT("请先填写全部答案"), TEXT("Fill every answer slot first")));
        return FReply::Handled();
    }
    FDWLanguageUIRequest Request;
    Request.Kind = EDWLanguageUIRequestKind::SubmitPuzzle;
    Request.TargetId = View.Id;
    for (const FDWLanguagePuzzleSlotView& Slot : View.Slots)
        Request.Answers.Add({Slot.SlotId, Answers.FindChecked(Slot.SlotId)});
    // UI does not compare labels, notes, glyph IDs or preloaded answer keys to decide correctness.
    bPending = true;
    Context->Request(MoveTemp(Request));
    bPending = false;
    Refresh(); // Both rejection and success re-read the context's authoritative snapshot.
    return FReply::Handled();
}

TSharedPtr<FDragDropOperation> SDWLanguagePuzzlePanel::BeginWordDrag(FName WordId, FName SourceSlot)
{
    if (!CanEdit() || WordId.IsNone()) return nullptr;
    DraggedWord = WordId;
    TWeakPtr<SDWLanguagePuzzlePanel> Weak = SharedThis(this);
    return FDWPuzzleWordDrag::New(Context.ToSharedRef(), DragOwner, View.Id, WordId, SourceSlot, LabelFor(WordId),
        [Weak, Owner = DragOwner]
        {
            if (TSharedPtr<SDWLanguagePuzzlePanel> P = Weak.Pin())
                if (P->DragOwner == Owner) P->DraggedWord = NAME_None;
        });
}

bool SDWLanguagePuzzlePanel::AcceptDrop(int32 Index, const FDWPuzzleWordDrag& Drag)
{
    if (!CanEdit() || !View.Slots.IsValidIndex(Index) || Drag.Owner != DragOwner || Drag.Session != DraftSession || Drag.Puzzle != View.Id) return false;
    if (!View.WordPool.ContainsByPredicate([&Drag](const FDWLanguageWordView& W) { return W.Id == Drag.Word; })) return false;
    if (Drag.SourceSlot.IsNone()) { FillSlot(Index, Drag.Word); return true; }
    if (!View.Slots.ContainsByPredicate([&Drag](const FDWLanguagePuzzleSlotView& S) { return S.SlotId == Drag.SourceSlot; })) return false;
    const FName* Source = Answers.Find(Drag.SourceSlot);
    if (!Source || *Source != Drag.Word) return false; // Draft changed while dragging: do not overwrite it.
    const FName TargetId = View.Slots[Index].SlotId;
    if (TargetId == Drag.SourceSlot) { ActiveSlot = Index; return true; }
    const FName TargetWord = Answers.FindRef(TargetId);
    Answers.Add(TargetId, Drag.Word);
    if (TargetWord.IsNone()) Answers.Remove(Drag.SourceSlot);
    else Answers.Add(Drag.SourceSlot, TargetWord);
    ActiveSlot = Index;
    return true;
}

FReply SDWLanguagePuzzlePanel::OnKeyDown(const FGeometry&, const FKeyEvent& E)
{
    if (!CanEdit()) return FReply::Unhandled();
    if (E.GetKey() == EKeys::Delete || E.GetKey() == EKeys::BackSpace)
    { ClearSlot(ActiveSlot); return FReply::Handled(); }
    // Enter on a token is handled by the token first; only a panel-focused Enter submits.
    if (E.GetKey() == EKeys::Enter && HasKeyboardFocus()) return Submit();
    return FReply::Unhandled(); // Esc/~ belongs to the host's modal stack.
}

void SDWLanguagePuzzlePanel::LayoutPool(FVector2D Size)
{
    PoolSize = Size;
    if (Bodies.IsEmpty() || Size.X < 8 || Size.Y < 8) return;
    int32 BestCols = 1, BestRows = Bodies.Num();
    float BestDiameter = 0;
    for (int32 Cols = 1; Cols <= Bodies.Num(); ++Cols)
    {
        const int32 Rows = FMath::DivideAndRoundUp(Bodies.Num(), Cols);
        const float D = FMath::Min((Size.X - 12.f) / Cols, (Size.Y - 12.f) / Rows) - 10.f;
        if (D > BestDiameter) { BestCols = Cols; BestRows = Rows; BestDiameter = D; }
    }
    const float Diameter = FMath::Max(1.f, FMath::Min(BestDiameter, 76.f)); // Fit actual bounds; never force overflow with a minimum diameter.
    for (int32 I = 0; I < Bodies.Num(); ++I)
    {
        FWordBody& B = Bodies[I]; B.Radius = Diameter * .5f;
        B.Position = FVector2D(6.f + (I % BestCols + .5f) * (Size.X - 12.f) / BestCols,
            6.f + (I / BestCols + .5f) * (Size.Y - 12.f) / BestRows);
        const float A = I * 2.39996f + .4f, Speed = 8.f + I % 5;
        B.Velocity = FVector2D(FMath::Cos(A), FMath::Sin(A)) * Speed;
    }
}

void SDWLanguagePuzzlePanel::StepPool(float DeltaTime)
{
    const auto Locked = [this](int32 I)
    {
        return !Bodies[I].Widget.IsValid() || Bodies[I].Widget->IsHovered() || Bodies[I].Widget->HasKeyboardFocus()
            || (View.WordPool.IsValidIndex(I) && DraggedWord == View.WordPool[I].Id);
    };
    const auto Wall = [this](FWordBody& B)
    {
        const double XMin = B.Radius + 2., YMin = B.Radius + 2.;
        const double XMax = FMath::Max(XMin, PoolSize.X - B.Radius - 2.);
        const double YMax = FMath::Max(YMin, PoolSize.Y - B.Radius - 2.);
        B.Position.X = FMath::Clamp(B.Position.X, XMin, XMax);
        B.Position.Y = FMath::Clamp(B.Position.Y, YMin, YMax);
        if ((B.Position.X <= XMin && B.Velocity.X < 0) || (B.Position.X >= XMax && B.Velocity.X > 0)) B.Velocity.X *= -1;
        if ((B.Position.Y <= YMin && B.Velocity.Y < 0) || (B.Position.Y >= YMax && B.Velocity.Y > 0)) B.Velocity.Y *= -1;
    };
    const float Clamped = FMath::Clamp(DeltaTime, 0.f, .06f);
    const int32 Steps = FMath::Max(1, FMath::CeilToInt(Clamped / .015f));
    const float Dt = Clamped / Steps;
    for (int32 Step = 0; Step < Steps; ++Step)
    {
        for (int32 I = 0; I < Bodies.Num(); ++I)
        { if (!Locked(I)) Bodies[I].Position += Bodies[I].Velocity * Dt; Wall(Bodies[I]); }
        for (int32 Pass = 0; Pass < 2; ++Pass)
        for (int32 I = 0; I < Bodies.Num(); ++I)
        for (int32 J = I + 1; J < Bodies.Num(); ++J)
        {
            FWordBody& A = Bodies[I]; FWordBody& B = Bodies[J];
            const FVector2D Delta = B.Position - A.Position;
            const double Distance = Delta.Size(), Required = A.Radius + B.Radius + 1.;
            const float IA = Locked(I) ? 0.f : 1.f, IB = Locked(J) ? 0.f : 1.f, Mass = IA + IB;
            if (Distance >= Required || Mass == 0) continue;
            const FVector2D N = Distance > KINDA_SMALL_NUMBER ? Delta / Distance : FVector2D(1, 0);
            const double Overlap = Required - Distance;
            A.Position -= N * Overlap * IA / Mass; B.Position += N * Overlap * IB / Mass;
            const double Relative = FVector2D::DotProduct(B.Velocity * IB - A.Velocity * IA, N);
            if (Relative < 0)
            { const double Impulse = -2. * Relative / Mass; A.Velocity -= N * Impulse * IA; B.Velocity += N * Impulse * IB; }
        }
        for (FWordBody& B : Bodies) Wall(B);
    }
    for (FWordBody& B : Bodies) { const double Speed = B.Velocity.Size(); if (Speed > 16.) B.Velocity *= 16. / Speed; }
}

void SDWLanguagePuzzlePanel::Tick(const FGeometry& Geometry, double CurrentTime, float DeltaTime)
{
    SCompoundWidget::Tick(Geometry, CurrentTime, DeltaTime);
    if (!PoolCanvas.IsValid() || !CanEdit() || !FSlateApplication::IsInitialized() || !FSlateApplication::Get().IsActive()) return;
    const TSharedPtr<SWindow> OwnerWindow = FSlateApplication::Get().FindWidgetWindow(AsShared());
    if (OwnerWindow.IsValid() && !OwnerWindow->IsActive()) return;
    const FVector2D Size = PoolCanvas->GetCachedGeometry().GetLocalSize();
    if (Size.X < 8 || Size.Y < 8) return;
    if (!PoolSize.Equals(Size, .5)) LayoutPool(Size);
    if (!FSlateApplication::Get().IsDragDropping()) DraggedWord = NAME_None;
    if (bWordMotionEnabled) StepPool(DeltaTime);
}
