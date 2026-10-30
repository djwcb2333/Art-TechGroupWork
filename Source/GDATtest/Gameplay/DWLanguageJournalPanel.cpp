#include "DWLanguageJournalPanel.h"
#include "DWLanguageUICommon.h"
#include "DWLanguageMotionButton.h"
#include "Brushes/SlateNoResource.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
    int32 EntryStateRank(const FDWLanguageUIEntryView& Entry)
    {
        if (!Entry.bDiscovered) return 1;
        return Entry.SavedGuess.TrimStartAndEnd().IsEmpty() ? 0 : 2;
    }
}

void SDWLanguageJournalPanel::Construct(const FArguments& InArgs)
{
    Context = InArgs._Context;
    SelectedEntryId = InArgs._InitialEntryId;
    bShowCloseAction = InArgs._ShowCloseAction;
    SessionId = Context->GetSnapshot().SessionId;
    ChildSlot[SAssignNew(Root, SBox)];
    Refresh();
}

bool SDWLanguageJournalPanel::MatchesFilter(const FDWLanguageUIEntryView& Entry) const
{
    return Filter == EDWJournalFilter::All ||
        (Filter == EDWJournalFilter::Single && Entry.Kind == EDWLanguageEntryKind::Single) ||
        (Filter == EDWJournalFilter::Compound && Entry.Kind == EDWLanguageEntryKind::Compound);
}

void SDWLanguageJournalPanel::UpdateVisibleEntries()
{
    const FDWLanguageUISnapshot& Snapshot = Context->GetSnapshot();
    const FName PreviousSelection = SelectedEntryId;
    VisibleIds.Reset();
    for (const FDWLanguageUIEntryView& Entry : Snapshot.Entries)
        if (MatchesFilter(Entry)) VisibleIds.Add(Entry.Id);
    VisibleIds.Sort([this, &Snapshot](const FName& AId, const FName& BId)
    {
        const FDWLanguageUIEntryView& A = *Snapshot.FindEntry(AId);
        const FDWLanguageUIEntryView& B = *Snapshot.FindEntry(BId);
        if (Sort == EDWJournalSort::UnrecordedFirst)
        {
            const int32 ARank = EntryStateRank(A), BRank = EntryStateRank(B);
            if (ARank != BRank) return ARank < BRank;
        }
        else if (A.bDiscovered != B.bDiscovered) return A.bDiscovered;
        if (A.bDiscovered && B.bDiscovered && A.DiscoveryOrder != B.DiscoveryOrder)
        {
            return Sort == EDWJournalSort::LatestDiscovery ? A.DiscoveryOrder > B.DiscoveryOrder : A.DiscoveryOrder < B.DiscoveryOrder;
        }
        return A.Id.ToString().Compare(B.Id.ToString(), ESearchCase::CaseSensitive) < 0;
    });
    if (!VisibleIds.Contains(SelectedEntryId))
    {
        SelectedEntryId = NAME_None;
        for (const FName Id : VisibleIds)
        {
            if (Snapshot.FindEntry(Id)->bDiscovered) { SelectedEntryId = Id; break; }
        }
    }
    if (SelectedEntryId != PreviousSelection) EntryViews.Remove(SelectedEntryId);
}

void SDWLanguageJournalPanel::RememberScroll()
{
    if (EntryScroll) MainListOffset = EntryScroll->GetScrollOffset();
    if (SelectedEntryId.IsNone()) return;
    FEntryViewState& State = EntryViews.FindOrAdd(SelectedEntryId);
    if (DetailScroll) State.DetailOffset = DetailScroll->GetScrollOffset();
    if (ClueListScroll) State.ClueListOffset = ClueListScroll->GetScrollOffset();
    if (ClueDetailScroll) State.ClueDetailOffset = ClueDetailScroll->GetScrollOffset();
    if (RelatedListScroll) State.RelatedListOffset = RelatedListScroll->GetScrollOffset();
    if (RelatedDetailScroll) State.RelatedDetailOffset = RelatedDetailScroll->GetScrollOffset();
    if (PartsScroll) State.PartsOffset = PartsScroll->GetScrollOffset();
}

void SDWLanguageJournalPanel::Refresh()
{
    RememberScroll();
    Context->RefreshSnapshot();
    const FDWLanguageUISnapshot& Snapshot = Context->GetSnapshot();
    if (SessionId != Snapshot.SessionId)
    {
        SessionId = Snapshot.SessionId;
        Drafts.Reset();
        Navigation.Reset();
        EntryViews.Reset();
        MainListOffset = 0.f;
    }
    // Never retain a private draft/editor if authority revokes discovery (e.g. profile switch).
    for (auto It = Drafts.CreateIterator(); It; ++It)
    {
        const FDWLanguageUIEntryView* Entry = Snapshot.FindEntry(It.Key());
        if (!Entry || !Entry->bDiscovered) It.RemoveCurrent();
    }
    UpdateVisibleEntries();
    Rebuild();
}

FText SDWLanguageJournalPanel::StatusText(const FDWLanguageUIEntryView& Entry) const
{
    if (!Entry.bDiscovered) return DWLanguageUI::L(Context, TEXT("未发现"), TEXT("Undiscovered"));
    if (Entry.SavedGuess.TrimStartAndEnd().IsEmpty()) return DWLanguageUI::L(Context, TEXT("未记录"), TEXT("Unrecorded"));
    return DWLanguageUI::L(Context, TEXT("已记录"), TEXT("Recorded"));
}

FText SDWLanguageJournalPanel::SortText() const
{
    switch (Sort)
    {
    case EDWJournalSort::LatestDiscovery: return DWLanguageUI::L(Context, TEXT("最近发现优先"), TEXT("Recently discovered"));
    case EDWJournalSort::FirstDiscovery: return DWLanguageUI::L(Context, TEXT("按发现先后"), TEXT("First discovered"));
    default: return DWLanguageUI::L(Context, TEXT("未记录优先"), TEXT("Unrecorded first"));
    }
}

void SDWLanguageJournalPanel::Rebuild()
{
    if (!Root) return;
    NoteEditor.Reset(); DetailScroll.Reset();
    ClueListScroll.Reset(); ClueDetailScroll.Reset();
    RelatedListScroll.Reset(); RelatedDetailScroll.Reset(); PartsScroll.Reset();

    TSharedRef<SVerticalBox> SortMenu = SNew(SVerticalBox);
    const TCHAR* CN[] = { TEXT("未记录优先"), TEXT("最近发现优先"), TEXT("按发现先后") };
    const TCHAR* EN[] = { TEXT("Unrecorded first"), TEXT("Recently discovered"), TEXT("First discovered") };
    for (int32 I = 0; I < 3; ++I)
    {
        SortMenu->AddSlot().AutoHeight().Padding(2)
        [DWLanguageUI::Button(Context, DWLanguageUI::L(Context, CN[I], EN[I]),
            FOnClicked::CreateLambda([this, I]() { SetSort(static_cast<EDWJournalSort>(I)); return FReply::Handled(); }), 226)];
    }
    TSharedRef<SHorizontalBox> Header = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
        [DWLanguageUI::Text(Context, DWLanguageUI::L(Context, TEXT("语言图鉴"), TEXT("Language journal")), 28, DWLanguageUI::Ink())];
    if (!Navigation.IsEmpty())
    {
        const bool bWord = Navigation.Last().bSourceIsCompound;
        Header->AddSlot().AutoWidth().Padding(12, 0)
        [DWLanguageUI::Button(Context, bWord ? DWLanguageUI::L(Context, TEXT("返回原词"), TEXT("Back to word"))
             : DWLanguageUI::L(Context, TEXT("返回原字"), TEXT("Back to character")),
             FOnClicked::CreateLambda([this]() { NavigateBack(); return FReply::Handled(); }), 164)];
    }
    if (bShowCloseAction) Header->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(16, 0, 0, 0)
        [DWLanguageUI::CloseButton(Context, FOnClicked::CreateLambda([Weak = TWeakPtr<FDWLanguageUIContext>(Context)]
        { if (const auto C = Weak.Pin()) C->CloseLayer(); return FReply::Handled(); }))];
    TSharedRef<SHorizontalBox> Filters = SNew(SHorizontalBox);
    const TCHAR* FilterCN[] = { TEXT("全部"), TEXT("单字"), TEXT("组合") };
    const TCHAR* FilterEN[] = { TEXT("All"), TEXT("Single"), TEXT("Compound") };
    for (int32 I = 0; I < 3; ++I)
    {
        Filters->AddSlot().AutoWidth().Padding(0, 0, 8, 0)
        [DWLanguageUI::Button(Context, DWLanguageUI::L(Context, FilterCN[I], FilterEN[I]),
            FOnClicked::CreateLambda([this, I]() { SetFilter(static_cast<EDWJournalFilter>(I)); return FReply::Handled(); }),
            I == 2 ? 142.f : 108.f, true, static_cast<int32>(Filter) == I)];
    }
    Filters->AddSlot().FillWidth(1)[SNew(SBox)];
    Filters->AddSlot().AutoWidth()
    [SNew(SComboButton).ButtonStyle(&DWLanguageUI::ButtonStyle()).ContentPadding(FMargin(18, 10))
        .ButtonContent()[DWLanguageUI::Text(Context, SortText(), 18, DWLanguageUI::Ink())]
        .MenuContent()[SortMenu]];

    Root->SetContent(SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[Header]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)[Filters]
        + SVerticalBox::Slot().FillHeight(1)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0)
            [SNew(SBox).WidthOverride(240)[BuildEntryList()]]
            + SHorizontalBox::Slot().FillWidth(1)[BuildDetail()]]);
    if (EntryScroll) EntryScroll->SetScrollOffset(MainListOffset);
}

TSharedRef<SWidget> SDWLanguageJournalPanel::EntryCard(FName EntryId, bool bSelected, FOnClicked OnClicked)
{
    const FDWLanguageUIEntryView* Entry = Context->GetSnapshot().FindEntry(EntryId);
    if (!Entry) return SNew(SBox);
    const FText Guess = Entry->bDiscovered && !Entry->SavedGuess.TrimStartAndEnd().IsEmpty()
        ? FText::FromString(Entry->SavedGuess) : StatusText(*Entry);
    return SNew(SButton).ButtonStyle(&DWLanguageUI::ButtonStyle()).ContentPadding(FMargin(12))
        .ButtonColorAndOpacity(bSelected ? DWLanguageUI::Gold() / DWLanguageUI::Paper() : FLinearColor::White)
        .OnClicked(OnClicked)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
            [Context->EntryGlyph(EntryId, FVector2D(44, 44))]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[DWLanguageUI::Text(Context, Guess, 18, DWLanguageUI::Ink(), true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 5, 0, 0)
                [DWLanguageUI::Text(Context, StatusText(*Entry), 13, DWLanguageUI::Ink())]]];
}

TSharedRef<SWidget> SDWLanguageJournalPanel::BuildEntryList()
{
    SAssignNew(EntryScroll, SScrollBox).Orientation(Orient_Vertical);
    for (const FName Id : VisibleIds)
    {
        EntryScroll->AddSlot().Padding(0, 0, 8, 8)
        [EntryCard(Id, Id == SelectedEntryId,
            FOnClicked::CreateLambda([this, Id]() { SelectFromList(Id); return FReply::Handled(); }))];
    }
    if (VisibleIds.IsEmpty())
        EntryScroll->AddSlot()[DWLanguageUI::Text(Context, DWLanguageUI::L(Context, TEXT("暂无条目"), TEXT("No entries")), 20, DWLanguageUI::Ink())];
    return EntryScroll.ToSharedRef();
}

TSharedRef<SWidget> SDWLanguageJournalPanel::BuildDetail()
{
    const FDWLanguageUIEntryView* Entry = Context->GetSnapshot().FindEntry(SelectedEntryId);
    if (!Entry || !Entry->bDiscovered)
    {
        // Hard early return: no glyph asset, clue, relationship, guessed text, or editor is built here.
        return SNew(SBox).HAlign(HAlign_Fill).VAlign(VAlign_Center)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                [DWLanguageUI::Text(Context, FText::FromString(TEXT("?")), 64, DWLanguageUI::Ink())]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 16)
                [SNew(STextBlock).Text(Entry ? DWLanguageUI::L(Context, TEXT("未发现"), TEXT("Undiscovered"))
                    : DWLanguageUI::L(Context, TEXT("发现文字后，可在这里记录猜测。"), TEXT("Discover an inscription to begin your notes.")))
                    .Font(Context->Font(20)).ColorAndOpacity(DWLanguageUI::Ink())
                    .AutoWrapText(true).Justification(ETextJustify::Center)]];
    }
    const FText Guess = Entry->SavedGuess.TrimStartAndEnd().IsEmpty()
        ? DWLanguageUI::L(Context, TEXT("未记录含义"), TEXT("Meaning unrecorded")) : FText::FromString(Entry->SavedGuess);
    // Clue/relation scroll boxes otherwise receive only the height left after the
    // fixed header/editor and may collapse to their headings on the 650-unit page.
    // Keep the existing two-column sections and note editor; scroll only the detail body.
    SAssignNew(DetailScroll, SScrollBox).Orientation(Orient_Vertical);
    DetailScroll->AddSlot()
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
        [DWLanguageUI::Card(SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 18, 0)[Context->EntryGlyph(Entry->Id, FVector2D(72, 72))]
            + SHorizontalBox::Slot().FillWidth(1).VAlign(VAlign_Center)
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().AutoHeight()[DWLanguageUI::Text(Context, Guess, 24, DWLanguageUI::Ink(), true)]
                + SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[DWLanguageUI::Text(Context, StatusText(*Entry), 15, DWLanguageUI::Ink())]], FMargin(18))]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
            [SNew(SBox).HeightOverride(190.f)[BuildClues(*Entry)]]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 14)
            [SNew(SBox).HeightOverride(170.f)[BuildRelations(*Entry)]]];
    DetailScroll->SetScrollOffset(EntryViews.FindOrAdd(Entry->Id).DetailOffset);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1).Padding(0, 0, 0, 14)[DetailScroll.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight()[BuildNoteEditor(*Entry)];
}

TSharedRef<SWidget> SDWLanguageJournalPanel::BuildClues(const FDWLanguageUIEntryView& Entry)
{
    FEntryViewState& State = EntryViews.FindOrAdd(Entry.Id);
    const FDWLanguageUIClueView* SelectedClue = Entry.Clues.FindByPredicate([&State](const FDWLanguageUIClueView& C) { return C.Id == State.ClueId; });
    if (!SelectedClue && !Entry.Clues.IsEmpty()) { SelectedClue = &Entry.Clues[0]; State.ClueId = SelectedClue->Id; }
    SAssignNew(ClueListScroll, SScrollBox).Orientation(Orient_Vertical);
    for (const FDWLanguageUIClueView& Clue : Entry.Clues)
    {
        const FName EntryId = Entry.Id, ClueId = Clue.Id;
        ClueListScroll->AddSlot().Padding(0, 0, 8, 7)
        [SNew(SButton).ButtonStyle(&DWLanguageUI::ButtonStyle()).ContentPadding(FMargin(12))
            .ButtonColorAndOpacity(State.ClueId == Clue.Id ? DWLanguageUI::Gold() / DWLanguageUI::Paper() : FLinearColor::White)
            .OnClicked(FOnClicked::CreateLambda([this, EntryId, ClueId]()
            {
                RememberScroll();
                FEntryViewState& View = EntryViews.FindOrAdd(EntryId);
                View.ClueId = ClueId; View.ClueDetailOffset = 0.f;
                Rebuild(); return FReply::Handled();
            }))
            [DWLanguageUI::Text(Context, Clue.Place, 18, DWLanguageUI::Ink(), true)]];
    }
    SAssignNew(ClueDetailScroll, SScrollBox).Orientation(Orient_Vertical);
    if (SelectedClue)
    {
        ClueDetailScroll->AddSlot().Padding(12, 0, 10, 8)
            [DWLanguageUI::Text(Context, SelectedClue->Place, 20, DWLanguageUI::Ink(), true)];
        ClueDetailScroll->AddSlot().Padding(12, 0, 10, 10)
            [DWLanguageUI::Text(Context, SelectedClue->Carrier, 16, DWLanguageUI::Ink(), true)];
        if (SelectedClue->bFromCompound)
            ClueDetailScroll->AddSlot().Padding(12, 0, 10, 10)
                [DWLanguageUI::Text(Context, DWLanguageUI::L(Context, TEXT("出现在该组合词中"), TEXT("Appears in this compound")), 16, DWLanguageUI::Ink(), true)];
        ClueDetailScroll->AddSlot().Padding(12, 0, 10, 12)
            [DWLanguageUI::Text(Context, SelectedClue->Description, 18, DWLanguageUI::Ink(), true)];
        if (!SelectedClue->ObservedEntryId.IsNone())
            ClueDetailScroll->AddSlot().Padding(12, 0, 10, 0)
                [Context->EntryGlyph(SelectedClue->ObservedEntryId, FVector2D(56, 56))];
    }
    else ClueDetailScroll->AddSlot().Padding(12)
        [DWLanguageUI::Text(Context, DWLanguageUI::L(Context, TEXT("尚无现场线索"), TEXT("No observed clues yet")), 18, DWLanguageUI::Ink(), true)];
    ClueListScroll->SetScrollOffset(State.ClueListOffset);
    ClueDetailScroll->SetScrollOffset(State.ClueDetailOffset);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [DWLanguageUI::Text(Context, DWLanguageUI::L(Context, TEXT("现场线索"), TEXT("Observed clues")), 20, DWLanguageUI::Ink())]
        + SVerticalBox::Slot().FillHeight(1)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(184)[ClueListScroll.ToSharedRef()]]
            + SHorizontalBox::Slot().FillWidth(1)[ClueDetailScroll.ToSharedRef()]];
}

TSharedRef<SWidget> SDWLanguageJournalPanel::BuildRelations(const FDWLanguageUIEntryView& Entry)
{
    FEntryViewState& State = EntryViews.FindOrAdd(Entry.Id);
    if (Entry.Kind == EDWLanguageEntryKind::Compound)
    {
        SAssignNew(PartsScroll, SScrollBox).Orientation(Orient_Vertical);
        for (int32 I = 0; I < Entry.PartIds.Num(); ++I)
        {
            const FName PartId = Entry.PartIds[I];
            // Do not turn PartIds into a set: water-water must render two separate ordered positions.
            PartsScroll->AddSlot().Padding(0, 0, 8, 8)
            [SNew(SHorizontalBox)
                + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 12, 0)
                [DWLanguageUI::Text(Context, FText::AsNumber(I + 1), 18, DWLanguageUI::Ink())]
                + SHorizontalBox::Slot().FillWidth(1)
                [EntryCard(PartId, false, FOnClicked::CreateLambda([this, PartId]()
                    { NavigateRelationship(PartId); return FReply::Handled(); }))]];
        }
        PartsScroll->SetScrollOffset(State.PartsOffset);
        return SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
            [DWLanguageUI::Text(Context, DWLanguageUI::L(Context, TEXT("组成字 · 按原顺序"), TEXT("Characters · original order")), 20, DWLanguageUI::Ink())]
            + SVerticalBox::Slot().FillHeight(1)[PartsScroll.ToSharedRef()];
    }

    TArray<FName> Related;
    for (const FName Id : Entry.RelatedIds)
        if (Context->GetSnapshot().FindEntry(Id)) Related.Add(Id);
    if (!Related.Contains(State.RelatedId)) State.RelatedId = Related.IsEmpty() ? NAME_None : Related[0];
    SAssignNew(RelatedListScroll, SScrollBox).Orientation(Orient_Vertical);
    for (const FName Id : Related)
    {
        const FName EntryId = Entry.Id;
        RelatedListScroll->AddSlot().Padding(0, 0, 8, 8)
        [EntryCard(Id, Id == State.RelatedId, FOnClicked::CreateLambda([this, EntryId, Id]()
            {
                RememberScroll();
                FEntryViewState& View = EntryViews.FindOrAdd(EntryId);
                View.RelatedId = Id; View.RelatedDetailOffset = 0.f;
                Rebuild(); return FReply::Handled();
            }))];
    }
    SAssignNew(RelatedDetailScroll, SScrollBox).Orientation(Orient_Vertical);
    const FDWLanguageUIEntryView* Word = Context->GetSnapshot().FindEntry(State.RelatedId);
    if (Word && Word->bDiscovered)
    {
        RelatedDetailScroll->AddSlot().Padding(12, 0, 12, 8)[Context->EntryGlyph(Word->Id, FVector2D(60, 60))];
        RelatedDetailScroll->AddSlot().Padding(12, 0, 12, 8)
        [DWLanguageUI::Text(Context, Word->SavedGuess.TrimStartAndEnd().IsEmpty()
            ? DWLanguageUI::L(Context, TEXT("未记录含义"), TEXT("Meaning unrecorded")) : FText::FromString(Word->SavedGuess), 18, DWLanguageUI::Ink(), true)];
        const FName WordId = Word->Id;
        RelatedDetailScroll->AddSlot().Padding(12, 0, 12, 8)
        [DWLanguageUI::Button(Context, DWLanguageUI::L(Context, TEXT("查看组合词"), TEXT("Open compound")),
            FOnClicked::CreateLambda([this, WordId]() { NavigateRelationship(WordId); return FReply::Handled(); }), 180)];
    }
    else RelatedDetailScroll->AddSlot().Padding(12)
        [DWLanguageUI::Text(Context, Word ? DWLanguageUI::L(Context, TEXT("未发现"), TEXT("Undiscovered"))
             : DWLanguageUI::L(Context, TEXT("尚无相关词"), TEXT("No related words yet")), 18, DWLanguageUI::Ink(), true)];
    RelatedListScroll->SetScrollOffset(State.RelatedListOffset);
    RelatedDetailScroll->SetScrollOffset(State.RelatedDetailOffset);
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [DWLanguageUI::Text(Context, DWLanguageUI::L(Context, TEXT("包含该字的词"), TEXT("Words containing this character")), 20, DWLanguageUI::Ink())]
        + SVerticalBox::Slot().FillHeight(1)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()[SNew(SBox).WidthOverride(240)[RelatedListScroll.ToSharedRef()]]
            + SHorizontalBox::Slot().FillWidth(1)[RelatedDetailScroll.ToSharedRef()]];
}

TSharedRef<SWidget> SDWLanguageJournalPanel::BuildNoteEditor(const FDWLanguageUIEntryView& Entry)
{
    if (!Entry.bCanEdit)
        return DWLanguageUI::Text(Context, Entry.EditBlockedReason.IsEmpty()
            ? DWLanguageUI::L(Context, TEXT("当前不可编辑"), TEXT("Editing unavailable")) : Entry.EditBlockedReason, 18, DWLanguageUI::Ink(), true);
    FDraft& Draft = Drafts.FindOrAdd(Entry.Id);
    if (!Draft.bDirty)
    {
        Draft.Text = Entry.SavedGuess;
        Draft.BaseSavedGuess = Entry.SavedGuess;
    }
    const FName EntryId = Entry.Id;
    return SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 14, 0)
            [SAssignNew(NoteEditor, SEditableTextBox).Style(&DWLanguageUI::NoteStyle()).Text(FText::FromString(Draft.Text))
                .HintText(DWLanguageUI::L(Context, TEXT("输入自己的猜测"), TEXT("Write your own guess")))
                .Font(Context->Font(20)).Padding(FMargin(14, 10)).MinDesiredWidth(220.f)
                .SelectAllTextWhenFocused(false).ClearKeyboardFocusOnCommit(false)
                .OnTextChanged_Lambda([this, EntryId](const FText& Value) { NoteChanged(Value, EntryId); })
                .OnTextCommitted_Lambda([this, EntryId](const FText&, ETextCommit::Type CommitType)
                    { if (CommitType == ETextCommit::OnEnter) SaveNote(EntryId); })]
            + SHorizontalBox::Slot().AutoWidth()
            [DWLanguageUI::Button(Context, DWLanguageUI::L(Context, TEXT("保存猜测"), TEXT("Save note")),
                FOnClicked::CreateLambda([this, EntryId]() { SaveNote(EntryId); return FReply::Handled(); }),
                140, Context->GetSnapshot().bSessionValid)]]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(STextBlock).Font(Context->Font(14)).ColorAndOpacity(DWLanguageUI::Ink())
            .Text_Lambda([this, EntryId]()
            {
                const FDraft* Current = Drafts.Find(EntryId);
                const int32 Count = Current ? DWLanguageUI::CountCharacters(Current->Text) : 0;
                const int32 Limit = FMath::Max(1, Context->GetSnapshot().MaxGuessCharacters);
                return FText::FromString(FString::Printf(TEXT("%d / %d"), Count, Limit));
            })];
}

void SDWLanguageJournalPanel::SelectFromList(FName EntryId)
{
    RememberScroll();
    Navigation.Reset(); // Ordinary list browsing never creates a relationship back stack.
    if (EntryId != SelectedEntryId) EntryViews.Remove(EntryId);
    SelectedEntryId = EntryId;
    Rebuild();
}

void SDWLanguageJournalPanel::SelectEntry(FName EntryId)
{
    Refresh(); // Also handles session/profile changes before opening the requested entry.
    Navigation.Reset();
    EntryViews.Remove(EntryId);
    SelectedEntryId = EntryId;
    const FDWLanguageUIEntryView* Entry = Context->GetSnapshot().FindEntry(EntryId);
    if (Entry && !MatchesFilter(*Entry)) Filter = EDWJournalFilter::All;
    UpdateVisibleEntries();
    Rebuild();
}

void SDWLanguageJournalPanel::NavigateRelationship(FName EntryId)
{
    const FDWLanguageUIEntryView* Target = Context->GetSnapshot().FindEntry(EntryId);
    const FDWLanguageUIEntryView* Source = Context->GetSnapshot().FindEntry(SelectedEntryId);
    if (!Target || !Source || !Source->bDiscovered) return;
    RememberScroll();
    FNavigationFrame Frame;
    Frame.EntryId = SelectedEntryId; Frame.Filter = Filter; Frame.Sort = Sort;
    Frame.ListOffset = MainListOffset; Frame.ViewState = EntryViews.FindOrAdd(SelectedEntryId);
    Frame.bSourceIsCompound = Source->Kind == EDWLanguageEntryKind::Compound;
    Navigation.Add(Frame);
    EntryViews.Remove(EntryId); // A fresh related entry starts at its own first clue, not another entry's index.
    SelectedEntryId = EntryId;
    if (!MatchesFilter(*Target)) Filter = EDWJournalFilter::All;
    UpdateVisibleEntries();
    Rebuild();
}

void SDWLanguageJournalPanel::NavigateBack()
{
    if (Navigation.IsEmpty()) return;
    RememberScroll();
    const FNavigationFrame Frame = Navigation.Pop();
    SelectedEntryId = Frame.EntryId; Filter = Frame.Filter; Sort = Frame.Sort;
    MainListOffset = Frame.ListOffset;
    EntryViews.Add(Frame.EntryId, Frame.ViewState);
    UpdateVisibleEntries();
    Rebuild();
}

void SDWLanguageJournalPanel::SetFilter(EDWJournalFilter NewFilter)
{
    RememberScroll();
    Filter = NewFilter;
    Navigation.Reset();
    MainListOffset = 0.f;
    UpdateVisibleEntries();
    Rebuild();
}

void SDWLanguageJournalPanel::SetSort(EDWJournalSort NewSort)
{
    RememberScroll();
    Sort = NewSort;
    UpdateVisibleEntries(); // Sorting changes position, never a still-visible selected identity.
    Rebuild();
}

void SDWLanguageJournalPanel::NoteChanged(const FText& Value, FName EntryId)
{
    if (bRestoringNote) return;
    const FDWLanguageUIEntryView* Entry = Context->GetSnapshot().FindEntry(EntryId);
    if (!Entry || !Entry->bDiscovered || !Entry->bCanEdit) return;
    FDraft& Draft = Drafts.FindOrAdd(EntryId);
    const FString Proposed = Value.ToString();
    const int32 Limit = FMath::Max(1, Context->GetSnapshot().MaxGuessCharacters);
    const bool bShrinkingOlderLongNote = DWLanguageUI::CountCharacters(Draft.Text) > Limit
        && DWLanguageUI::CountCharacters(Proposed) < DWLanguageUI::CountCharacters(Draft.Text)
        && DWLanguageUI::IsValidNote(Proposed, MAX_int32);
    if (!DWLanguageUI::IsValidNote(Proposed, Limit) && !bShrinkingOlderLongNote)
    {
        // Roll back the entire attempted edit/paste. Never Left(N) a UTF-16 string or split a surrogate pair.
        if (NoteEditor && EntryId == SelectedEntryId)
        {
            TGuardValue<bool> Guard(bRestoringNote, true);
            NoteEditor->SetText(FText::FromString(Draft.Text));
        }
        Context->Notice(FText::Format(DWLanguageUI::L(Context, TEXT("最多输入 {0} 个字符。"), TEXT("Use at most {0} characters.")), FText::AsNumber(Limit)));
        return;
    }
    Draft.Text = Proposed;
    Draft.bDirty = Draft.Text != Draft.BaseSavedGuess;
}

void SDWLanguageJournalPanel::SaveNote(FName EntryId)
{
    const FDWLanguageUIEntryView* Entry = Context->GetSnapshot().FindEntry(EntryId);
    FDraft* Draft = Drafts.Find(EntryId);
    if (!Entry || !Entry->bDiscovered || !Entry->bCanEdit || !Draft) return;
    const FString Value = Draft->Text.TrimStartAndEnd();
    if (!DWLanguageUI::IsValidNote(Value, Context->GetSnapshot().MaxGuessCharacters)) return;
    RememberScroll();
    FDWLanguageUIRequest Request;
    Request.Kind = EDWLanguageUIRequestKind::SaveGuess;
    Request.TargetId = EntryId;
    Request.Guess = Value; // Empty is an intentional clear of this entry only.
    const FDWLanguageUIReply Reply = Context->Request(Request);
    if (Reply.bAccepted)
    {
        Drafts.Remove(EntryId);
        Context->Notice(DWLanguageUI::L(Context, TEXT("猜测已保存，可随时修改"), TEXT("Guess saved. You can edit it at any time.")));
    }
    UpdateVisibleEntries();
    Rebuild();
}
