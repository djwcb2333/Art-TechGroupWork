#include "DWLanguageUIScreen.h"
#include "DWLanguageUICommon.h"
#include "DWLanguageJournalPanel.h"
#include "DWLanguagePuzzlePanel.h"
#include "DWLanguageExchangePanel.h"
#include "DWTalentPanel.h"
#include "DWUIBounceComponent.h"
#include "DWUIOffscreenComponent.h"
#include "Engine/Font.h"
#include "Framework/Application/SlateApplication.h"
#include "InputCoreTypes.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SOverlay.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

UDWLanguageUIScreen::UDWLanguageUIScreen(const FObjectInitializer& Init) : Super(Init)
{
    SetIsFocusable(true);
    SetVisibility(ESlateVisibility::Collapsed);
    JournalToggleKey = EKeys::Tilde; // Executor replaces with the actual resolved rebound key.
}

TSharedRef<SWidget> UDWLanguageUIScreen::RebuildWidget()
{
    if (!Context.IsValid()) Context = MakeShared<FDWLanguageUIContext>();
    TWeakObjectPtr<UDWLanguageUIScreen> WeakThis(this);
    Context->FontProvider = [WeakThis](int32 Size)
    {
        return WeakThis.IsValid() && WeakThis->ExistingGameFont
            ? FSlateFontInfo(WeakThis->ExistingGameFont, Size)
            : FCoreStyle::GetDefaultFontStyle(TEXT("Regular"), Size);
    };
    Context->OnOpenJournal = [WeakThis](FName Id) { if (WeakThis.IsValid()) WeakThis->OpenJournal(Id); };
    Context->OnCloseLayer = [WeakThis] { if (WeakThis.IsValid()) WeakThis->CloseTopLayer(); };
    Context->OnNotice = [WeakThis](const FText& Message)
    { if (WeakThis.IsValid() && WeakThis->NoticeLabel.IsValid()) WeakThis->NoticeLabel->SetText(Message); };
    Context->OnReply = [WeakThis](const FDWLanguageUIRequest& R, const FDWLanguageUIReply& Reply)
    { if (WeakThis.IsValid()) WeakThis->HandleReply(R, Reply); };

    SAssignNew(Switcher, SWidgetSwitcher);
    Switcher->SetEnabled(false);
    BuildPanels();
    TSharedRef<SWidget> Content = SNew(SBox).WidthOverride(DesignSize.X).HeightOverride(DesignSize.Y)
        [SNew(SBorder).BorderImage(DWLanguageUI::PaperBrush()).Padding(FMargin(28))
            [SNew(SVerticalBox)
                + SVerticalBox::Slot().FillHeight(1)[Switcher.ToSharedRef()]
                + SVerticalBox::Slot().AutoHeight()
                [SNew(SBox)
                    .HeightOverride_Lambda([WeakThis]() -> FOptionalSize
                    {
                        return FOptionalSize(WeakThis.IsValid() && WeakThis->Context.IsValid() && !WeakThis->Context->NoticeText.IsEmpty() ? 32.f : 0.f);
                    })
                    .Visibility_Lambda([WeakThis]()
                    {
                        return WeakThis.IsValid() && WeakThis->Context.IsValid() && !WeakThis->Context->NoticeText.IsEmpty() ? EVisibility::Visible : EVisibility::Collapsed;
                    })
                    .Clipping(EWidgetClipping::ClipToBounds)
                    [SAssignNew(NoticeLabel,STextBlock).Font(Context->Font(18)).ColorAndOpacity(DWLanguageUI::Ink())
                        .AutoWrapText(false).OverflowPolicy(ETextOverflowPolicy::Ellipsis).Text(Context->NoticeText)
                        .ToolTipText_Lambda([WeakThis]()
                        {
                            return WeakThis.IsValid() && WeakThis->Context.IsValid() ? WeakThis->Context->NoticeText : FText::GetEmpty();
                        })]]]];
    if (!Bounce) { Bounce = NewObject<UDWUIBounceComponent>(this); Bounce->Initialize(this); }
    Bounce->bPlayOnConstruct = false; Bounce->bAutoBindButtons = false; Bounce->bAnimateNonButtonHover = false;
    Bounce->PulseScaleOffset = .015f; Bounce->PulseOffset = FVector2D(0,-3); Bounce->PulseAngle = 0.f;
    Bounce->PreConstruct(IsDesignTime()); Content = Bounce->RebuildWidgetWithContent(Content); Bounce->Construct();
    if (!Offscreen) { Offscreen = NewObject<UDWUIOffscreenComponent>(this); Offscreen->Initialize(this); }
    Offscreen->bFollowCinematics = false; Offscreen->ExitEdge = EDWUIExitEdge::Bottom;
    Offscreen->AnticipationSeconds = 0; Offscreen->ReturnSeconds = FMath::Max(.01f, EnterSeconds);
    Offscreen->ExitSeconds = FMath::Max(.01f,ExitSeconds); Offscreen->bAutoExitDistance = true;
    Offscreen->PreConstruct(IsDesignTime()); Content = Offscreen->RebuildWidgetWithContent(Content); Offscreen->Construct();
    return SNew(SBox).Padding(FMargin(36))
        [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)[Content]];
}

void UDWLanguageUIScreen::BuildPanels()
{
    if (!Switcher.IsValid() || !Context.IsValid()) return;
    // Keep each instance alive while the journal overlays a mechanism: its draft belongs to that session.
    SAssignNew(Journal, SDWLanguageJournalPanel).Context(Context.ToSharedRef()).ShowCloseAction(false);
    const TWeakPtr<FDWLanguageUIContext> WeakContext = Context;
    // The book body is 1064 x 470 design units; preserve the single journal instance.
    BookJournalContent = SNew(SBox).WidthOverride(1064.f).HeightOverride(470.f)
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().FillHeight(1)[Journal.ToSharedRef()]
        + SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
        [SNew(STextBlock).Font(Context->Font(16)).ColorAndOpacity(DWLanguageUI::Ink()).AutoWrapText(true)
            .Visibility_Lambda([WeakContext] { const auto C=WeakContext.Pin(); return C&&!C->NoticeText.IsEmpty()?EVisibility::Visible:EVisibility::Collapsed; })
            .Text_Lambda([WeakContext] { const auto C=WeakContext.Pin(); return C?C->NoticeText:FText::GetEmpty(); })]];
    Switcher->AddSlot()[SNullWidget::NullWidget]; // Journal has exactly one parent: BookJournalContent.
    Switcher->AddSlot()[SAssignNew(Puzzle, SDWLanguagePuzzlePanel).Context(Context.ToSharedRef())];
    Switcher->AddSlot()[SAssignNew(Exchange, SDWLanguageExchangePanel).Context(Context.ToSharedRef())];
    Switcher->AddSlot()[SAssignNew(Talents, SDWTalentPanel).Context(Context.ToSharedRef())];
}
bool UDWLanguageUIScreen::Activate(TSharedRef<IDWLanguageUIAdapter> Adapter, FDWLanguageUIHostServices Services,
    EDWLanguageUIScreenPage Page, FName Entry)
{
    if (ModalLease.IsValid()) return false; // Outer owner must close the previous session first.
    SetVisibility(ESlateVisibility::Collapsed);
    if (Context.IsValid()) { Context->bAcceptRequests = false; Context->Adapter.Reset(); Context->Snapshot = FDWLanguageUISnapshot(); Context->Notice(FText::GetEmpty()); }
    if (Switcher.IsValid()) Switcher->SetEnabled(false);
    Host = MoveTemp(Services);
    if (!Host.AcquireModal || !Host.ReleaseModal) return false; // Fail closed; never leave gameplay input live.
    TakeWidget();
    Context->Adapter = Adapter; Context->RefreshSnapshot();
    if (!Context->Snapshot.bSessionValid || Context->Snapshot.SessionId.IsEmpty()) return false;
    BasePage = Page; bNestedJournal = bJournalInBook = false; bClosing = false; CloseAt = RemoveAt = 0.;
    ModalLease = Host.AcquireModal(TakeWidget());
    if (!ModalLease.IsValid()) return false;
    Context->bAcceptRequests = true;
    Switcher->SetEnabled(true);
    Switcher->SetActiveWidgetIndex(static_cast<int32>(BasePage));
    Puzzle->SetSuspended(BasePage != EDWLanguageUIScreenPage::Puzzle);
    RefreshPanels();
    if (Page == EDWLanguageUIScreenPage::Journal)
    {
        OpenJournal(Entry);
        if (!bJournalInBook) { CloseImmediately(); return false; }
        StartTick();
        return true;
    }
    SetVisibility(ESlateVisibility::Visible);
    SetKeyboardFocus();
    if (Offscreen) Offscreen->ReturnToScreen();
    if (Bounce) Bounce->PlayEntrance();
    StartTick();
    return true;
}

void UDWLanguageUIScreen::OpenJournal(FName Entry)
{
    if (!ModalLease.IsValid() || bClosing || CloseAt > 0. || !Context || !Journal || !BookJournalContent || !Host.PresentJournalInBook) return;
    Context->RefreshSnapshot();
    if (!Context->Snapshot.bSessionValid) { CloseImmediately(); return; }
    // Keep dirty notes/navigation on a plain tab revisit. Only an explicit glyph changes selection.
    Journal->Refresh();
    if (!Entry.IsNone()) Journal->SelectEntry(Entry);
    Puzzle->SetSuspended(true); // Retires only this puzzle's drag epoch; answers stay in the same instance.
    if (!Host.PresentJournalInBook(BookJournalContent.ToSharedRef()))
    { Puzzle->SetSuspended(BasePage != EDWLanguageUIScreenPage::Puzzle); return; }
    bNestedJournal = BasePage != EDWLanguageUIScreenPage::Journal;
    bBookAuxiliary = false;
    bJournalInBook = true;
    SetVisibility(ESlateVisibility::Collapsed);
}

bool UDWLanguageUIScreen::OpenTalentsFromBook()
{
    if (!bJournalInBook || bClosing || CloseAt > 0. || !Context || !Context->bAcceptRequests || !Host.SetBookAuxiliaryVisible) return false;
    Context->RefreshSnapshot();
    if (!Context->Snapshot.bSessionValid) { CloseImmediately(); return false; }
    Talents->Refresh();
    if (!Host.SetBookAuxiliaryVisible(true)) { CloseImmediately(); return false; }
    bBookAuxiliary = true;
    Switcher->SetActiveWidgetIndex(static_cast<int32>(EDWLanguageUIScreenPage::Talents));
    const FGuid ExpectedLease = ModalLease;
    if (!IsInViewport()) AddToViewport(200);
    if (!ModalLease.IsValid() || ModalLease != ExpectedLease || !bJournalInBook) { CloseImmediately(); return false; }
    SetVisibility(ESlateVisibility::Visible); SetKeyboardFocus();
    return true;
}

void UDWLanguageUIScreen::CloseTopLayer()
{
    if (!ModalLease.IsValid() || bClosing) return;
    if (bBookAuxiliary)
    {
        bBookAuxiliary = false;
        if (!Host.SetBookAuxiliaryVisible || !Host.SetBookAuxiliaryVisible(false)) { CloseImmediately(); return; }
        SetVisibility(ESlateVisibility::Collapsed);
        return;
    }
    if (bJournalInBook)
    {
        const bool Resume = bNestedJournal;
        bJournalInBook = false;
        if (!Host.LeaveJournalBook || !Host.LeaveJournalBook(Resume)) { CloseImmediately(); return; }
        bNestedJournal = false;
        if (Resume)
        {
            Context->RefreshSnapshot();
            if (!Context->Snapshot.bSessionValid) { CloseImmediately(); return; }
            Switcher->SetActiveWidgetIndex(static_cast<int32>(BasePage));
            Puzzle->SetSuspended(BasePage != EDWLanguageUIScreenPage::Puzzle);
            RefreshPanels();
            SetVisibility(ESlateVisibility::Visible);
            SetKeyboardFocus();
            return; // Same adapter, draft and lease; no backend RequestJournal/CloseInteraction.
        }
        CloseImmediately(); // The book has no independent screen exit animation.
        return;
    }
    bClosing = true; CloseAt = 0.;
    Context->bAcceptRequests = false;
    Switcher->SetEnabled(false);
    if (Puzzle.IsValid()) Puzzle->SetSuspended(true);
    if (Offscreen) Offscreen->HideForCinematic();
    RemoveAt = FPlatformTime::Seconds() + FMath::Max(.01f,ExitSeconds) + .05;
    StartTick(); // Real-time ticker continues while game time is paused.
}

void UDWLanguageUIScreen::HandleReply(const FDWLanguageUIRequest& Request, const FDWLanguageUIReply& Reply)
{
    bRefreshPending = true;
    const bool bSelectionOnly = Request.Kind == EDWLanguageUIRequestKind::StageItem || Request.Kind == EDWLanguageUIRequestKind::UnstageItem;
    if (Reply.bAccepted && !bSelectionOnly && Host.RefreshWorldPrompts) Host.RefreshWorldPrompts();
    if (Reply.bAccepted && !bSelectionOnly && Bounce) Bounce->Pulse(.5f);
    if (Reply.bAccepted && Reply.bCloseAfterSuccess && Request.Kind == EDWLanguageUIRequestKind::SubmitExchange)
    {
        // Backend has already cleared a committed NPC context. Freeze this result-only
        // view until its real-time delay; no stale inventory requests can be accepted.
        Context->bAcceptRequests=false;if(Switcher)Switcher->SetEnabled(false);
        ClosingSession = Context->Snapshot.SessionId;
        CloseAt = FPlatformTime::Seconds() + FMath::Clamp(Reply.CloseDelaySeconds,0.f,10.f);
    }
}

void UDWLanguageUIScreen::RefreshPanels()
{
    if (Journal.IsValid()) Journal->Refresh();
    if (Puzzle.IsValid()) Puzzle->Refresh();
    if (Exchange.IsValid()) Exchange->Refresh();
    if (Talents.IsValid()) Talents->Refresh();
    bRefreshPending = false;
}

void UDWLanguageUIScreen::StartTick()
{
    if (!TickHandle.IsValid()) TickHandle = FTSTicker::GetCoreTicker().AddTicker(
        FTickerDelegate::CreateWeakLambda(this,[this](float Dt){return TickPresentation(Dt);}),.1f);
}
bool UDWLanguageUIScreen::TickPresentation(float)
{
    if (!ModalLease.IsValid() || !Context.IsValid()) { TickHandle.Reset(); return false; }
    const double Now = FPlatformTime::Seconds();
    if (bClosing)
    {
        if (Now >= RemoveAt) { CloseImmediately(); return false; }
        return true; // Closing animation owns the view; do not rebuild actionable children.
    }
    if(CloseAt>0.)
    {
        if(Now>=CloseAt){CloseAt=0.;bNestedJournal=false;CloseTopLayer();}
        return true; // Committed result only; bridge independently validates world/life/epoch.
    }
    const FString Session = Context->Snapshot.SessionId;
    const int64 Revision = Context->Snapshot.Revision;
    Context->RefreshSnapshot();
    if (!Context->Snapshot.bSessionValid || Context->Snapshot.SessionId != Session)
    { CloseImmediately(); return false; }
    if (bRefreshPending || Revision != Context->Snapshot.Revision) RefreshPanels();
    if (CloseAt > 0 && Now >= CloseAt && Context->Snapshot.SessionId == ClosingSession)
    {
        CloseAt = 0.; bNestedJournal = false; CloseTopLayer();
    }
    return true;
}

bool UDWLanguageUIScreen::IsTextEntryFocused() const
{
    if (!FSlateApplication::IsInitialized()) return false;
    const auto Focus = FSlateApplication::Get().GetKeyboardFocusedWidget();
    return Focus.IsValid() && Focus->GetTypeAsString().Contains(TEXT("EditableText"));
}
FReply UDWLanguageUIScreen::NativeOnPreviewKeyDown(const FGeometry& G, const FKeyEvent& E)
{
    if (ModalLease.IsValid() && E.GetKey()==EKeys::Escape) { CloseTopLayer(); return FReply::Handled(); }
    if (ModalLease.IsValid() && (E.GetKey()==JournalToggleKey||E.GetKey()==JournalAlternateToggleKey) && !IsTextEntryFocused())
    {
        if (bNestedJournal || BasePage==EDWLanguageUIScreenPage::Journal) CloseTopLayer(); else OpenJournal();
        return FReply::Handled();
    }
    return Super::NativeOnPreviewKeyDown(G,E); // Text children receive WASD/Q/F/number and IME input.
}
FReply UDWLanguageUIScreen::NativeOnKeyDown(const FGeometry& G,const FKeyEvent& E)
{ return ModalLease.IsValid()?FReply::Handled():Super::NativeOnKeyDown(G,E); }
FReply UDWLanguageUIScreen::NativeOnKeyUp(const FGeometry& G,const FKeyEvent& E)
{ return ModalLease.IsValid()?FReply::Handled():Super::NativeOnKeyUp(G,E); }
FReply UDWLanguageUIScreen::NativeOnMouseButtonDown(const FGeometry& G,const FPointerEvent& E)
{ return ModalLease.IsValid()?FReply::Handled():Super::NativeOnMouseButtonDown(G,E); }

void UDWLanguageUIScreen::ReleaseLease()
{
    const FGuid Lease = ModalLease; ModalLease.Invalidate();
    if (Lease.IsValid() && Host.ReleaseModal) Host.ReleaseModal(Lease);
    bNestedJournal = false; bClosing = false; CloseAt = RemoveAt = 0.;
}
void UDWLanguageUIScreen::CloseImmediately()
{
    if (bClosingImmediately) return;
    TGuardValue<bool> ClosingGuard(bClosingImmediately, true);
    if (TickHandle.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(TickHandle); TickHandle.Reset(); }
    if (Context.IsValid()) Context->bAcceptRequests = false;
    bBookAuxiliary = false;
    if (bJournalInBook) { bJournalInBook = false; if (Host.LeaveJournalBook) Host.LeaveJournalBook(false); }
    if (Context.IsValid()) { Context->Adapter.Reset(); Context->Snapshot = FDWLanguageUISnapshot(); }
    if (Switcher.IsValid()) Switcher->SetEnabled(false);
    ReleaseLease(); SetVisibility(ESlateVisibility::Collapsed);
    if (Puzzle.IsValid()) Puzzle->SetSuspended(true);
    if (Offscreen) Offscreen->RestoreImmediately();
    if (Bounce) Bounce->ResetBounce();
}
void UDWLanguageUIScreen::NativeDestruct() { CloseImmediately(); Super::NativeDestruct(); }
void UDWLanguageUIScreen::ReleaseSlateResources(bool Children)
{
    CloseImmediately();
    if (Bounce) Bounce->Destruct(); if (Offscreen) Offscreen->Destruct();
    BookJournalContent.Reset(); Journal.Reset(); Puzzle.Reset(); Exchange.Reset(); Talents.Reset(); Switcher.Reset(); NoticeLabel.Reset();
    Super::ReleaseSlateResources(Children);
}
