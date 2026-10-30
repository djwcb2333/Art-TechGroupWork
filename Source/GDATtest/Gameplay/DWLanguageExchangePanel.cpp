#include "DWLanguageExchangePanel.h"
#include "DWLanguageUICommon.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "InputCoreTypes.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
class FDWExchangeItemDrag : public FDragDropOperation
{
public:
    DRAG_DROP_OPERATOR_TYPE(FDWExchangeItemDrag, FDragDropOperation)
    FGuid OwnerId;
    FString SessionId;
    FName StackId;
    bool bFromStaging = false;
    FText Label;
    FSlateFontInfo Font;

    void Initialize()
    {
        bCreateNewWindow = false; // Game viewport decorator, not a desktop tool window.
        Construct();
    }

    virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
    {
        return DWLanguageUI::Card(SNew(STextBlock).Text(Label).Font(Font)
            .ColorAndOpacity(DWLanguageUI::Ink()), FMargin(16, 10));
    }
};

/** A click adds/removes exactly one. Drag detection suppresses the click path. */
class SDWExchangeItemTile : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWExchangeItemTile) {}
        SLATE_ARGUMENT(TSharedPtr<FDWLanguageUIContext>, Context)
        SLATE_ARGUMENT(FDWLanguageInventoryView, Item)
        SLATE_ARGUMENT(int32, DisplayCount)
        SLATE_ARGUMENT(FGuid, OwnerId)
        SLATE_ARGUMENT(FString, SessionId)
        SLATE_ARGUMENT(bool, FromStaging)
        SLATE_ARGUMENT(bool, Enabled)
        SLATE_ARGUMENT(TFunction<void()>, OnActivate)
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs)
    {
        Context = InArgs._Context;
        Item = InArgs._Item;
        OwnerId = InArgs._OwnerId;
        SessionId = InArgs._SessionId;
        bFromStaging = InArgs._FromStaging;
        bEnabled = InArgs._Enabled;
        OnActivate = InArgs._OnActivate;
        SetToolTipText(Item.BlockedReason.IsEmpty() ? Item.Name : Item.BlockedReason);
        SetEnabled(bEnabled);
        ChildSlot
        [
            SNew(SBox).WidthOverride(88).HeightOverride(100)
            [
                SNew(SBorder).BorderImage(DWLanguageUI::InsetBrush()).Padding(FMargin(8))
                .BorderBackgroundColor_Lambda([this]()
                {
                    return FLinearColor::LerpUsingHSV(FLinearColor::White, FLinearColor(1.18f, 1.18f, 1.18f, 1.f), HoverAlpha);
                })
                [
                    SNew(SVerticalBox)
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [Context->Glyph(Item.Icon, FVector2D(42, 42))]
                    + SVerticalBox::Slot().AutoHeight().Padding(0, 3).HAlign(HAlign_Center)
                    [SNew(STextBlock).Text(Item.Name).Font(Context->Font(13)).ColorAndOpacity(DWLanguageUI::Ink())
                        .OverflowPolicy(ETextOverflowPolicy::Ellipsis).AutoWrapText(false)]
                    + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
                    [DWLanguageUI::Text(Context.ToSharedRef(), FText::AsNumber(InArgs._DisplayCount), 15, DWLanguageUI::Ink())]
                ]
            ]
        ];
    }

    virtual bool SupportsKeyboardFocus() const override { return true; }
    virtual void Tick(const FGeometry& Geometry, double Time, float DeltaTime) override
    {
        SCompoundWidget::Tick(Geometry, Time, DeltaTime);
        // Items retain the approved linear brightness hover; no lift, scale, or shadow.
        HoverAlpha = FMath::FInterpConstantTo(HoverAlpha, IsHovered() ? 1.f : 0.f, DeltaTime, 1.f / .16f);
    }
    virtual FReply OnMouseButtonDown(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (bEnabled && Event.GetEffectingButton() == EKeys::LeftMouseButton)
            return FReply::Handled().CaptureMouse(SharedThis(this)).SetUserFocus(SharedThis(this))
                .DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
        return FReply::Unhandled();
    }
    virtual FReply OnMouseButtonUp(const FGeometry& Geometry, const FPointerEvent& Event) override
    {
        if (Event.GetEffectingButton() == EKeys::LeftMouseButton && HasMouseCapture())
        {
            if (bEnabled && Geometry.IsUnderLocation(Event.GetScreenSpacePosition()) && OnActivate) OnActivate();
            return FReply::Handled().ReleaseMouseCapture();
        }
        return FReply::Unhandled();
    }
    virtual FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
    {
        if (!bEnabled) return FReply::Unhandled();
        TSharedRef<FDWExchangeItemDrag> Operation = MakeShared<FDWExchangeItemDrag>();
        Operation->OwnerId = OwnerId;
        Operation->SessionId = SessionId;
        Operation->StackId = Item.StackId;
        Operation->bFromStaging = bFromStaging;
        Operation->Label = Item.Name;
        Operation->Font = Context->Font(18);
        Operation->Initialize();
        return FReply::Handled().BeginDragDrop(Operation);
    }
    virtual FReply OnKeyDown(const FGeometry&, const FKeyEvent& Event) override
    {
        if (bEnabled && !Event.IsRepeat() && (Event.GetKey() == EKeys::Enter || Event.GetKey() == EKeys::SpaceBar))
        {
            if (OnActivate) OnActivate();
            return FReply::Handled();
        }
        return FReply::Unhandled();
    }
private:
    TSharedPtr<FDWLanguageUIContext> Context;
    FDWLanguageInventoryView Item;
    FGuid OwnerId;
    FString SessionId;
    TFunction<void()> OnActivate;
    bool bFromStaging = false;
    bool bEnabled = true;
    float HoverAlpha = 0.f;
};

class SDWExchangeDropArea : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SDWExchangeDropArea) {}
        SLATE_DEFAULT_SLOT(FArguments, Content)
        SLATE_ARGUMENT(TFunction<bool(const FDragDropEvent&)>, OnItemDrop)
    SLATE_END_ARGS()
    void Construct(const FArguments& InArgs)
    {
        OnItemDrop = InArgs._OnItemDrop;
        ChildSlot[InArgs._Content.Widget];
    }
    virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& Event) override
    {
        return OnItemDrop && OnItemDrop(Event) ? FReply::Handled() : FReply::Unhandled();
    }
private:
    TFunction<bool(const FDragDropEvent&)> OnItemDrop;
};
}

void SDWLanguageExchangePanel::Construct(const FArguments& InArgs)
{
    check(InArgs._Context.IsValid());
    Context = InArgs._Context;
    DragOwnerId = FGuid::NewGuid();
    Refresh();
}

bool SDWLanguageExchangePanel::CanEdit() const
{
    return Context.IsValid() && Context->GetSnapshot().bSessionValid && Context->GetSnapshot().Exchange.bAvailable &&
        !Context->GetSnapshot().Exchange.TargetId.IsNone() && !bSubmitting && !bTradeSuccessHold;
}

int64 SDWLanguageExchangePanel::GetStagedUnits() const
{
    int64 Total = 0;
    for (const auto& Pair : Staged) Total += Pair.Value;
    return Total;
}

void SDWLanguageExchangePanel::ReconcileStaging(bool bAnnounceInvalidation)
{
    const FDWLanguageUISnapshot& Snapshot = Context->GetSnapshot();
    const FDWLanguageExchangeView& View = Snapshot.Exchange;
    if (SessionId != Snapshot.SessionId || TargetId != View.TargetId || Mode != View.Mode)
    {
        Staged.Reset();
        InventoryOffset=StagedOffset=0.f;InventoryScroll.Reset();StagedScroll.Reset();
        bTradeSuccessHold = false;
        DragOwnerId = FGuid::NewGuid(); // Invalidate a drag begun on the previous target.
        SessionId = Snapshot.SessionId;
        TargetId = View.TargetId;
        Mode = View.Mode;
    }
    bool bInventoryDrifted = false;
    for (const auto& Pair : Staged)
    {
        const FDWLanguageInventoryView* Item = View.Inventory.FindByPredicate(
            [&Pair](const FDWLanguageInventoryView& I) { return I.StackId == Pair.Key; });
        if (!Item || Item->StackId.IsNone() || !Item->bCanSelect || Pair.Value <= 0 || Item->Quantity < Pair.Value)
        {
            bInventoryDrifted = true;
            break;
        }
    }
    if (bInventoryDrifted)
    {
        // v2.1 tokens contain InventoryRevision. Any drift invalidates the entire backend selection;
        // do not rebind the old choice to a current slot or silently clamp only one of its quantities.
        Staged.Reset();
        DragOwnerId = FGuid::NewGuid();
        if (bAnnounceInvalidation)
            Context->Notice(DWLanguageUI::L(Context.ToSharedRef(), TEXT("背包已变化，请重新选择物品"),
                TEXT("Inventory changed. Select your items again.")));
    }
}

void SDWLanguageExchangePanel::ChangeOne(FName StackId, bool bRemove)
{
    if (!CanEdit()) return;
    ReconcileStaging();
    const FDWLanguageInventoryView* Item = Context->GetSnapshot().Exchange.Inventory.FindByPredicate(
        [StackId](const FDWLanguageInventoryView& I) { return I.StackId == StackId; });
    if (!Item || StackId.IsNone()) return;
    const int32 Before = Staged.FindRef(StackId);
    const FDWLanguageExchangeView& View = Context->GetSnapshot().Exchange;
    if (bRemove && Before <= 0) return;
    if (!bRemove && (!Item->bCanSelect || Before >= Item->Quantity)) return;
    if (!bRemove && View.Mode == EDWLanguageExchangeMode::Offering &&
        GetStagedUnits() >= FMath::Max(1, View.UnitsPerSubmission))
    {
        Context->Notice(FText::Format(DWLanguageUI::L(Context.ToSharedRef(), TEXT("本次最多投入 {0} 件"),
            TEXT("Select up to {0} item(s) for this offering.")), FText::AsNumber(FMath::Max(1, View.UnitsPerSubmission))));
        Refresh();
        return;
    }

    FDWLanguageUIRequest Request;
    Request.Kind = bRemove ? EDWLanguageUIRequestKind::UnstageItem : EDWLanguageUIRequestKind::StageItem;
    Request.TargetId = View.TargetId;
    Request.Items.Add({StackId, 1}); // One actual click/drop is one backend selection operation.
    const FString RequestedSession = Context->GetSnapshot().SessionId;
    const FName RequestedTarget = View.TargetId;
    const TSharedRef<SDWLanguageExchangePanel> KeepAlive = SharedThis(this);
    bSubmitting = true;
    const FDWLanguageUIReply Reply = Context->Request(MoveTemp(Request));
    bSubmitting = false;
    if (Reply.bResetSelection) Staged.Reset();
    ReconcileStaging(false); // Preserve the real failure receipt instead of replacing it with a local notice.
    const FDWLanguageUISnapshot& Current = Context->GetSnapshot();
    const FDWLanguageInventoryView* CurrentItem = Current.Exchange.Inventory.FindByPredicate(
        [StackId](const FDWLanguageInventoryView& I) { return I.StackId == StackId; });
    if (Reply.bAccepted && !Reply.bResetSelection && Current.bSessionValid && Current.SessionId == RequestedSession &&
        Current.Exchange.TargetId == RequestedTarget && CurrentItem)
    {
        const int32 AcceptedCount = Staged.FindRef(StackId);
        if (bRemove)
        {
            if (AcceptedCount <= 1) Staged.Remove(StackId);
            else Staged.Add(StackId, AcceptedCount - 1);
        }
        else if (AcceptedCount < CurrentItem->Quantity)
        {
            Staged.Add(StackId, AcceptedCount + 1);
        }
    }
    Refresh();
    const auto& FocusMap = bRemove && Staged.Contains(StackId) ? StagedTiles : InventoryTiles;
    if (const TWeakPtr<SWidget>* Weak = FocusMap.Find(StackId))
        if (const auto Tile = Weak->Pin(); Tile && Tile->IsEnabled() && FSlateApplication::IsInitialized())
            FSlateApplication::Get().SetKeyboardFocus(Tile, EFocusCause::SetDirectly);
}

bool SDWLanguageExchangePanel::AcceptDrop(const FDragDropEvent& Event, bool bToStaging)
{
    const TSharedPtr<FDWExchangeItemDrag> Operation = Event.GetOperationAs<FDWExchangeItemDrag>();
    if (!Operation || !CanEdit() || TargetId != Context->GetSnapshot().Exchange.TargetId ||
        Mode != Context->GetSnapshot().Exchange.Mode || Operation->OwnerId != DragOwnerId ||
        Operation->SessionId != Context->GetSnapshot().SessionId || Operation->bFromStaging == bToStaging) return false;
    ChangeOne(Operation->StackId, !bToStaging);
    return true;
}

TSharedRef<SWidget> SDWLanguageExchangePanel::MakeInscription(const TArray<FName>& EntryIds)
{
    TSharedRef<SWrapBox> Words = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8, 6));
    for (FName Id : EntryIds)
    {
        const FDWLanguageUIEntryView* Entry = Context->GetSnapshot().FindEntry(Id);
        const auto* VisibleGlyph = Context->GetSnapshot().Exchange.VisibleInscriptionGlyphs.Find(Id);
        TSharedRef<SVerticalBox> Word = SNew(SVerticalBox)
            + SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
            [VisibleGlyph ? Context->Glyph(*VisibleGlyph, FVector2D(48,48), DWLanguageUI::Ink())
                : Context->EntryGlyph(Id, FVector2D(48, 48))];
        if (Entry && Entry->bDiscovered && !Entry->SavedGuess.IsEmpty())
        {
            // Preview only: the saved note remains complete in its tooltip and journal.
            FString Preview = Entry->SavedGuess;
            Preview.ReplaceInline(TEXT("\r"), TEXT(" "));
            Preview.ReplaceInline(TEXT("\n"), TEXT(" "));
            Word->AddSlot().AutoHeight().HAlign(HAlign_Fill)
            [SNew(STextBlock).Text(FText::FromString(FString::Printf(TEXT("(%s)"), *Preview)))
                .ToolTipText(FText::FromString(Entry->SavedGuess)).Font(Context->Font(13))
                .ColorAndOpacity(DWLanguageUI::Muted()).AutoWrapText(false)
                .OverflowPolicy(ETextOverflowPolicy::Ellipsis)];
        }
        Words->AddSlot()
        [SNew(SButton).ButtonStyle(&DWLanguageUI::ButtonStyle()).ContentPadding(FMargin(8))
            .ToolTipText(DWLanguageUI::L(Context.ToSharedRef(), TEXT("打开图鉴中的记录"), TEXT("Open journal entry")))
            .OnClicked_Lambda([WeakContext = TWeakPtr<FDWLanguageUIContext>(Context), Id]()
            {
                if (const auto C = WeakContext.Pin()) C->OpenJournal(Id);
                return FReply::Handled();
            })[SNew(SBox).WidthOverride(100)[Word]]];
    }
    if (EntryIds.IsEmpty())
        Words->AddSlot()[DWLanguageUI::Text(Context.ToSharedRef(), DWLanguageUI::L(Context.ToSharedRef(), TEXT("未观察到铭文"), TEXT("No inscription observed")),
            16, DWLanguageUI::Muted())];
    return Words;
}

TSharedRef<SWidget> SDWLanguageExchangePanel::MakeInventory(bool bStagingArea)
{
    TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(5));
    int32 DisplayIndex = 0;
    const FDWLanguageExchangeView& ExchangeView = Context->GetSnapshot().Exchange;
    const bool bOfferingHasRoom = ExchangeView.Mode != EDWLanguageExchangeMode::Offering ||
        GetStagedUnits() < FMath::Max(1, ExchangeView.UnitsPerSubmission);
    TSet<FName> DisplayedStackIds;
    for (const FDWLanguageInventoryView& Item : Context->GetSnapshot().Exchange.Inventory)
    {
        if (Item.StackId.IsNone() || DisplayedStackIds.Contains(Item.StackId)) continue;
        DisplayedStackIds.Add(Item.StackId);
        const int32 Selected = Staged.FindRef(Item.StackId);
        if (bStagingArea && Selected <= 0) continue;
        const bool bEnabled = CanEdit() && !Item.StackId.IsNone() && Item.bCanSelect &&
            (bStagingArea ? Selected > 0 : Selected < Item.Quantity && bOfferingHasRoom);
        TSharedRef<SDWExchangeItemTile> Tile = SNew(SDWExchangeItemTile).Context(Context).Item(Item)
            .DisplayCount(bStagingArea ? Selected : Item.Quantity)
            .OwnerId(DragOwnerId).SessionId(SessionId).FromStaging(bStagingArea).Enabled(bEnabled)
            .OnActivate([WeakThis = TWeakPtr<SDWLanguageExchangePanel>(SharedThis(this)), StackId = Item.StackId, bStagingArea]()
            {
                if (const auto P = WeakThis.Pin()) P->ChangeOne(StackId, bStagingArea);
            });
        (bStagingArea ? StagedTiles : InventoryTiles).Add(Item.StackId, Tile);
        Grid->AddSlot(DisplayIndex % 4, DisplayIndex / 4)[Tile];
        ++DisplayIndex;
    }
    TSharedRef<SScrollBox> Scroll=SNew(SScrollBox);
    Scroll->AddSlot()[Grid];Scroll->SetScrollOffset(bStagingArea?StagedOffset:InventoryOffset);
    if(bStagingArea)StagedScroll=Scroll;else InventoryScroll=Scroll;
    TSharedRef<SVerticalBox> Body = SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(2, 0, 2, 10)
        [DWLanguageUI::Text(Context.ToSharedRef(), bStagingArea
            ? DWLanguageUI::L(Context.ToSharedRef(), TEXT("本次投入"), TEXT("Your selection"))
            : DWLanguageUI::L(Context.ToSharedRef(), TEXT("背包"), TEXT("Inventory")), 23, DWLanguageUI::Ink())]
        + SVerticalBox::Slot().FillHeight(1)
        [Scroll];
    if (DisplayIndex == 0)
        Body->AddSlot().AutoHeight().Padding(8, 16)
        [DWLanguageUI::Text(Context.ToSharedRef(), bStagingArea
            ? DWLanguageUI::L(Context.ToSharedRef(), TEXT("点击或拖入物品，每次一件"), TEXT("Click or drag an item to add one"))
            : DWLanguageUI::L(Context.ToSharedRef(), TEXT("背包中没有可显示的物品"), TEXT("No inventory items")), 17, DWLanguageUI::Muted(), true)];
    if (bStagingArea && ExchangeView.Mode == EDWLanguageExchangeMode::Offering)
        Body->AddSlot().AutoHeight().Padding(2, 8)
        [DWLanguageUI::Text(Context.ToSharedRef(), FText::Format(
            DWLanguageUI::L(Context.ToSharedRef(), TEXT("本次最多 {0} 件"), TEXT("Up to {0} item(s) per offering")),
            FText::AsNumber(FMath::Max(1, ExchangeView.UnitsPerSubmission))), 14, DWLanguageUI::Muted(), true)];
    Body->AddSlot().AutoHeight().Padding(2, 10, 2, 0)
    [DWLanguageUI::Text(Context.ToSharedRef(), bStagingArea
        ? DWLanguageUI::L(Context.ToSharedRef(), TEXT("点击或移回背包，每次撤回一件"), TEXT("Click or drag back to remove one"))
        : DWLanguageUI::L(Context.ToSharedRef(), TEXT("选择时不会扣除背包物品"), TEXT("Selecting does not spend inventory items")),
        14, DWLanguageUI::Muted(), true)];
    return SNew(SDWExchangeDropArea).OnItemDrop(
        [WeakThis = TWeakPtr<SDWLanguageExchangePanel>(SharedThis(this)), bStagingArea](const FDragDropEvent& Event)
        {
            const auto P = WeakThis.Pin();
            return P && P->AcceptDrop(Event, bStagingArea);
        })[DWLanguageUI::Card(Body, FMargin(12))];
}

FReply SDWLanguageExchangePanel::Submit()
{
    if (!CanEdit()) return FReply::Handled();
    ReconcileStaging();
    const FDWLanguageExchangeView& View = Context->GetSnapshot().Exchange;
    if (!View.bCanSubmit || Staged.IsEmpty()) return FReply::Handled();
    if (View.Mode == EDWLanguageExchangeMode::Offering && GetStagedUnits() > FMath::Max(1, View.UnitsPerSubmission))
        return FReply::Handled();
    FDWLanguageUIRequest Request;
    Request.Kind = EDWLanguageUIRequestKind::SubmitExchange;
    Request.TargetId = View.TargetId;
    // Stable authored inventory order gives deterministic requests; quantity is only a proposal.
    TSet<FName> AddedStackIds;
    for (const FDWLanguageInventoryView& Item : View.Inventory)
        if (const int32 Count = Staged.FindRef(Item.StackId); Count > 0 && !AddedStackIds.Contains(Item.StackId))
        {
            Request.Items.Add({Item.StackId, Count});
            AddedStackIds.Add(Item.StackId);
        }
    const EDWLanguageExchangeMode SubmittedMode = View.Mode;
    const TSharedRef<SDWLanguageExchangePanel> KeepAlive = SharedThis(this);
    bSubmitting = true;
    const FDWLanguageUIReply Reply = Context->Request(MoveTemp(Request));
    bSubmitting = false;
    // The adapter retires a submitted backend token even when gameplay rejects it. Never display
    // a selection tied to that spent request, and never replace the real failure message.
    if (Reply.bResetSelection) Staged.Reset();
    if (Reply.bAccepted)
    {
        Staged.Reset();
        // NPC trade locks until the host closes this successful interaction. Offering may need several visits.
        bTradeSuccessHold = SubmittedMode == EDWLanguageExchangeMode::NPCTrade;
    }
    else ReconcileStaging(false); // Inventory drift clears the mirror but retains the authority's explanation.
    Refresh();
    return FReply::Handled();
}

void SDWLanguageExchangePanel::Refresh()
{
    if (!Context) return;
    if(InventoryScroll)InventoryOffset=InventoryScroll->GetScrollOffset();
    if(StagedScroll)StagedOffset=StagedScroll->GetScrollOffset();
    ReconcileStaging();
    InventoryTiles.Reset();
    StagedTiles.Reset();
    const auto C = Context.ToSharedRef();
    const FDWLanguageExchangeView& View = Context->GetSnapshot().Exchange;
    if (!View.bAvailable)
    {
        ChildSlot[DWLanguageUI::Card(DWLanguageUI::Text(C,
            View.BlockedReason.IsEmpty() ? DWLanguageUI::L(C, TEXT("该交互当前不可用"), TEXT("This interaction is unavailable")) : View.BlockedReason,
            22, DWLanguageUI::Ink(), true), FMargin(32))];
        return;
    }
    TSharedRef<SHorizontalBox> Inscription = SNew(SHorizontalBox)
        + SHorizontalBox::Slot().FillWidth(1)[MakeInscription(View.InscriptionIds)];
    if (View.Mode == EDWLanguageExchangeMode::NPCTrade)
    {
        Inscription->AddSlot().AutoWidth().Padding(20, 8).VAlign(VAlign_Center)
            [DWLanguageUI::Text(C, FText::FromString(TEXT("→")), 30, DWLanguageUI::Ink())];
        Inscription->AddSlot().FillWidth(1)[MakeInscription(View.RewardInscriptionIds)];
    }
    // Global notices have one fixed home; keep only distinct local status here.
    const FText LocalStatus = !View.BlockedReason.IsEmpty() ? View.BlockedReason : View.Receipt;
    const FText Feedback = LocalStatus.ToString() == Context->NoticeText.ToString() ? FText::GetEmpty() : LocalStatus;
    ChildSlot
    [
        SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 6)
        [DWLanguageUI::Text(C, View.Title, 26, DWLanguageUI::Ink())]
        + SVerticalBox::Slot().FillHeight(1).Padding(0, 0, 0, 12)
        [SNew(SVerticalBox)
        + SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
        [DWLanguageUI::Card(Inscription, FMargin(16, 8))]
        // Only the original item grids scroll; they receive all remaining space.
        + SVerticalBox::Slot().FillHeight(1)
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().FillWidth(1).Padding(0, 0, 12, 0)[MakeInventory(false)]
            + SHorizontalBox::Slot().FillWidth(1).Padding(12, 0, 0, 0)[MakeInventory(true)]]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 6)
        [DWLanguageUI::Text(C, View.Mode == EDWLanguageExchangeMode::Offering
            ? DWLanguageUI::L(C, TEXT("投入后无法取回，包括不符合铭文的物品。"), TEXT("Offered items cannot be recovered, even when the offering is incorrect."))
            : DWLanguageUI::L(C, TEXT("确认提交后由对方检查；失败不会在此界面扣除物品。"), TEXT("Submit your selection for the exchange.")),
            15, DWLanguageUI::Muted(), true)]
        + SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, Feedback.IsEmpty() ? 0.f : 8.f)
        [SNew(STextBlock).Text(Feedback).ToolTipText(Feedback).Font(C->Font(17))
            .Visibility(Feedback.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible)
            .ColorAndOpacity(DWLanguageUI::Ink()).AutoWrapText(false)
            .OverflowPolicy(ETextOverflowPolicy::Ellipsis)]]
        + SVerticalBox::Slot().AutoHeight()
        [SNew(SHorizontalBox)
            + SHorizontalBox::Slot().AutoWidth()
            [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("返回"), TEXT("Back")),
                FOnClicked::CreateLambda([WeakContext = TWeakPtr<FDWLanguageUIContext>(Context)]()
                { if (const auto P = WeakContext.Pin()) P->CloseLayer(); return FReply::Handled(); }), 130)]
            + SHorizontalBox::Slot().FillWidth(1)
            + SHorizontalBox::Slot().AutoWidth()
            [DWLanguageUI::Button(C, DWLanguageUI::L(C, TEXT("提交"), TEXT("Submit")),
                FOnClicked::CreateSP(this, &SDWLanguageExchangePanel::Submit), 180,
                CanEdit() && View.bCanSubmit && !Staged.IsEmpty() &&
                (View.Mode != EDWLanguageExchangeMode::Offering || GetStagedUnits() <= FMath::Max(1, View.UnitsPerSubmission)))]]
    ];
}
