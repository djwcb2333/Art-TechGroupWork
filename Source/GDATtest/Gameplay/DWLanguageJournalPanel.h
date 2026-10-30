#pragma once

#include "DWLanguageUIContract.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"

class SBox;
class SScrollBox;
class SEditableTextBox;

enum class EDWJournalFilter : uint8 { All, Single, Compound };
enum class EDWJournalSort : uint8 { UnrecordedFirst, LatestDiscovery, FirstDiscovery };

/** Personal notes only. All observed entries/clues are supplied by the authoritative adapter. */
class SDWLanguageJournalPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWLanguageJournalPanel)
        : _Context(MakeShared<FDWLanguageUIContext>()), _InitialEntryId(NAME_None), _ShowCloseAction(true) {}
        SLATE_ARGUMENT(TSharedRef<FDWLanguageUIContext>, Context)
        SLATE_ARGUMENT(FName, InitialEntryId)
        SLATE_ARGUMENT(bool, ShowCloseAction)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs);
    void Refresh();
    void SelectEntry(FName EntryId); // Host entry point; clears relationship-only navigation history.
    FName GetSelectedEntryId() const { return SelectedEntryId; }

private:
    struct FDraft
    {
        FString Text;
        FString BaseSavedGuess;
        bool bDirty = false;
    };
    struct FEntryViewState
    {
        FName ClueId;
        FName RelatedId;
        float DetailOffset = 0.f;
        float ClueListOffset = 0.f;
        float ClueDetailOffset = 0.f;
        float RelatedListOffset = 0.f;
        float RelatedDetailOffset = 0.f;
        float PartsOffset = 0.f;
    };
    struct FNavigationFrame
    {
        FName EntryId;
        EDWJournalFilter Filter = EDWJournalFilter::All;
        EDWJournalSort Sort = EDWJournalSort::UnrecordedFirst;
        float ListOffset = 0.f;
        FEntryViewState ViewState;
        bool bSourceIsCompound = false;
    };

    TSharedRef<FDWLanguageUIContext> Context = MakeShared<FDWLanguageUIContext>();
    TSharedPtr<SBox> Root;
    TSharedPtr<SScrollBox> EntryScroll;
    TSharedPtr<SScrollBox> DetailScroll;
    TSharedPtr<SScrollBox> ClueListScroll;
    TSharedPtr<SScrollBox> ClueDetailScroll;
    TSharedPtr<SScrollBox> RelatedListScroll;
    TSharedPtr<SScrollBox> RelatedDetailScroll;
    TSharedPtr<SScrollBox> PartsScroll;
    TSharedPtr<SEditableTextBox> NoteEditor;
    TArray<FName> VisibleIds;
    TArray<FNavigationFrame> Navigation;
    TMap<FName, FDraft> Drafts;
    TMap<FName, FEntryViewState> EntryViews;
    EDWJournalFilter Filter = EDWJournalFilter::All;
    EDWJournalSort Sort = EDWJournalSort::UnrecordedFirst;
    FName SelectedEntryId;
    FString SessionId;
    float MainListOffset = 0.f;
    bool bRestoringNote = false;
    bool bShowCloseAction = true;

    bool MatchesFilter(const FDWLanguageUIEntryView& Entry) const;
    void UpdateVisibleEntries();
    void RememberScroll();
    void Rebuild();
    TSharedRef<SWidget> BuildEntryList();
    TSharedRef<SWidget> BuildDetail();
    TSharedRef<SWidget> BuildClues(const FDWLanguageUIEntryView& Entry);
    TSharedRef<SWidget> BuildRelations(const FDWLanguageUIEntryView& Entry);
    TSharedRef<SWidget> BuildNoteEditor(const FDWLanguageUIEntryView& Entry);
    TSharedRef<SWidget> EntryCard(FName EntryId, bool bSelected, FOnClicked OnClicked);
    FText StatusText(const FDWLanguageUIEntryView& Entry) const;
    FText SortText() const;
    void SelectFromList(FName EntryId);
    void NavigateRelationship(FName EntryId);
    void NavigateBack();
    void SetFilter(EDWJournalFilter NewFilter);
    void SetSort(EDWJournalSort NewSort);
    void NoteChanged(const FText& Value, FName EntryId);
    void SaveNote(FName EntryId);
};
