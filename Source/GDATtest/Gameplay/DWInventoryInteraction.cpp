#include "DWInventoryInteraction.h"
#include "DWGameplayWidget.h"
#include "DWGameplayConfig.h"
#include "DWUIEntryWidgets.h"
#include "DWLocalizationLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/ButtonSlot.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Framework/Application/SlateApplication.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"

namespace
{
    bool DWChordMatches(const FInputChord& Chord,const FKey& Key,const FInputEvent& Event)
    {
        return Chord.Key.IsValid()&&Chord.Key==Key&&Chord.bShift==Event.IsShiftDown()&&
            Chord.bCtrl==Event.IsControlDown()&&Chord.bAlt==Event.IsAltDown()&&Chord.bCmd==Event.IsCommandDown();
    }
    FString DWInventoryKeyLabel(const UDWGameplayWidget* Screen,const FKey& Key)
    {
        if(Key==EKeys::LeftMouseButton)return DWText(Screen,TEXT("鼠标左键"),TEXT("Left mouse")).ToString();
        if(Key==EKeys::RightMouseButton)return DWText(Screen,TEXT("鼠标右键"),TEXT("Right mouse")).ToString();
        if(Key==EKeys::MiddleMouseButton)return DWText(Screen,TEXT("鼠标中键"),TEXT("Middle mouse")).ToString();
        return Key.GetDisplayName().ToString();
    }
    FString DWInventoryChordLabel(const UDWGameplayWidget* Screen,const FInputChord& Chord)
    {
        FString Label;
        if(Chord.bCtrl)Label+=TEXT("Ctrl+");if(Chord.bShift)Label+=TEXT("Shift+");
        if(Chord.bAlt)Label+=TEXT("Alt+");if(Chord.bCmd)Label+=TEXT("Cmd+");
        return Label+DWInventoryKeyLabel(Screen,Chord.Key);
    }
    void DWInventoryTextStyle(UTextBlock* Text,int32 Size,bool bHeading=false)
    {
        if(!Text)return;
        Text->SetFont(FCoreStyle::GetDefaultFontStyle(bHeading?TEXT("Bold"):TEXT("Regular"),Size));
        Text->SetColorAndOpacity(FSlateColor(FLinearColor(.95f,.91f,.82f)));
        Text->SetAutoWrapText(true);
    }
}

bool UDWGameplayWidget::IsInventoryInteractionPage() const
{
    return CurrentPage==EDWMenuPage::Inventory||CurrentPage==EDWMenuPage::Crafting;
}

FText UDWGameplayWidget::GetInventoryControlHelp() const
{
    const FDWInventoryControls& C=InventoryControls;
    FFormatNamedArguments Args;
    Args.Add(TEXT("Drag"),FText::FromString(DWInventoryKeyLabel(this,C.DragButton)));
    Args.Add(TEXT("Discard"),FText::FromString(DWInventoryChordLabel(this,C.DiscardKey)));
    Args.Add(TEXT("DiscardClick"),FText::FromString(DWInventoryChordLabel(this,C.DiscardClick)));
    Args.Add(TEXT("Stack"),FText::FromString(DWInventoryChordLabel(this,C.QuickStackClick)));
    Args.Add(TEXT("One"),FText::FromString(DWInventoryChordLabel(this,C.SplitOneClick)));
    Args.Add(TEXT("Half"),FText::FromString(DWInventoryChordLabel(this,C.SplitHalfClick)));
    return FText::Format(DWText(this,
        TEXT("{Drag} 选择 / 拖动整组 · {One} 拆出1个 · {Half} 拆半\n{Stack} 合并背包内同类 · {Discard} 或 {DiscardClick} 请求丢弃整组\n拖到背包面板外的游戏区域也可丢弃，均需确认。"),
        TEXT("{Drag}: select / drag stack  |  {One}: split one  |  {Half}: split half\n{Stack}: combine matching bag stacks  |  {Discard} or {DiscardClick}: discard stack\nDrop outside the bag panel within the game to request discard. Confirmation is required.")),Args);
}

void UDWGameplayWidget::EnsureInventoryInteractionUI()
{
    if(IsDesignTime()||!WidgetTree||bInventoryUIInitialized)return;
    // Append to the existing authored card. Nothing is rebuilt, renamed or moved.
    for(const TPair<UBorder*,FName>& Entry:{TPair<UBorder*,FName>(InventoryCard,TEXT("InventoryControlsHelpText")),TPair<UBorder*,FName>(CraftingCard,TEXT("CraftingControlsHelpText"))})
    {
        if(!Entry.Key||WidgetTree->FindWidget(Entry.Value))continue;
        UVerticalBox* Column=Cast<UVerticalBox>(Entry.Key->GetContent());
        if(!Column)continue;
        UTextBlock* Help=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),Entry.Value);
        DWInventoryTextStyle(Help,13);Help->SetText(GetInventoryControlHelp());Help->SetVisibility(ESlateVisibility::HitTestInvisible);
        UVerticalBoxSlot* HelpSlot=Column->AddChildToVerticalBox(Help);HelpSlot->SetPadding(FMargin(0,10,0,0));
    }
    // Prefer Designer-authored additions when present. Bind only the existing named controls.
    if(UBorder* AuthoredModal=Cast<UBorder>(WidgetTree->FindWidget(TEXT("InventoryDiscardModal"))))
    {
        UTextBlock* Message=Cast<UTextBlock>(WidgetTree->FindWidget(TEXT("InventoryDiscardMessage")));
        UButton* Confirm=Cast<UButton>(WidgetTree->FindWidget(TEXT("InventoryDiscardConfirmButton")));
        UButton* Cancel=Cast<UButton>(WidgetTree->FindWidget(TEXT("InventoryDiscardCancelButton")));
        if(!Confirm||!Cancel)return;
        InventoryDiscardModal=AuthoredModal;InventoryDiscardMessage=Message;
        Confirm->OnClicked.RemoveDynamic(this,&UDWGameplayWidget::ConfirmInventoryDiscard);Confirm->OnClicked.AddDynamic(this,&UDWGameplayWidget::ConfirmInventoryDiscard);
        Cancel->OnClicked.RemoveDynamic(this,&UDWGameplayWidget::CancelInventoryDiscard);Cancel->OnClicked.AddDynamic(this,&UDWGameplayWidget::CancelInventoryDiscard);
        ConfigureInventoryDiscardPopup();
        InventoryDiscardModal->SetVisibility(ESlateVisibility::Collapsed);
        bInventoryUIInitialized=true;
        ApplyWidgetPresentation(this);return;
    }
    UCanvasPanel* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(!Root)return;
    InventoryDiscardModal=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("InventoryDiscardModal"));
    InventoryDiscardModal->SetHorizontalAlignment(HAlign_Fill);InventoryDiscardModal->SetVerticalAlignment(VAlign_Fill);
    UCanvasPanelSlot* ModalSlot=Root->AddChildToCanvas(InventoryDiscardModal);
    ModalSlot->SetAnchors(FAnchors(0));ModalSlot->SetAlignment(FVector2D::ZeroVector);ModalSlot->SetAutoSize(false);ModalSlot->SetSize(FVector2D(260,128));ModalSlot->SetZOrder(10000);
    UVerticalBox* Column=WidgetTree->ConstructWidget<UVerticalBox>();InventoryDiscardModal->SetContent(Column);
    UTextBlock* Title=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("InventoryDiscardTitle"));
    DWInventoryTextStyle(Title,22,true);Column->AddChildToVerticalBox(Title)->SetPadding(FMargin(0,0,0,10));
    InventoryDiscardMessage=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("InventoryDiscardMessage"));DWInventoryTextStyle(InventoryDiscardMessage,18);
    Column->AddChildToVerticalBox(InventoryDiscardMessage);InventoryDiscardMessage->SetVisibility(ESlateVisibility::Collapsed);
    UHorizontalBox* Actions=WidgetTree->ConstructWidget<UHorizontalBox>();Column->AddChildToVerticalBox(Actions);
    UButton* Confirm=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("InventoryDiscardConfirmButton"));UButton* Cancel=WidgetTree->ConstructWidget<UButton>(UButton::StaticClass(),TEXT("InventoryDiscardCancelButton"));
    for(UButton* Button:{Confirm,Cancel})
    {
        Button->SetBackgroundColor(FLinearColor(.35f,.25f,.14f));
        UHorizontalBoxSlot* S=Actions->AddChildToHorizontalBox(Button);S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));S->SetPadding(FMargin(5));
    }
    UTextBlock* ConfirmLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("InventoryDiscardConfirmLabel"));DWInventoryTextStyle(ConfirmLabel,18);ConfirmLabel->SetAutoWrapText(false);Confirm->SetContent(ConfirmLabel);
    UTextBlock* CancelLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("InventoryDiscardCancelLabel"));DWInventoryTextStyle(CancelLabel,18);CancelLabel->SetAutoWrapText(false);Cancel->SetContent(CancelLabel);
    Confirm->OnClicked.AddDynamic(this,&UDWGameplayWidget::ConfirmInventoryDiscard);
    Cancel->OnClicked.AddDynamic(this,&UDWGameplayWidget::CancelInventoryDiscard);
    ConfigureInventoryDiscardPopup();
    InventoryDiscardModal->SetVisibility(ESlateVisibility::Collapsed);
    bInventoryUIInitialized=true;
    ApplyWidgetPresentation(this);
}

void UDWGameplayWidget::ConfigureInventoryDiscardPopup()
{
    if(!WidgetTree||!InventoryDiscardModal)return;
    // The former full-screen modal becomes one compact card; the input blocker below has no paint.
    InventoryDiscardModal->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.95f,.86f,.69f),12.f,FLinearColor(.45f,.24f,.10f),1.5f));
    InventoryDiscardModal->SetBrushColor(FLinearColor::White);
    InventoryDiscardModal->SetPadding(FMargin(16));
    InventoryDiscardModal->SetHorizontalAlignment(HAlign_Fill);InventoryDiscardModal->SetVerticalAlignment(VAlign_Fill);
    if(UCanvasPanelSlot* PopupLayoutSlot=Cast<UCanvasPanelSlot>(InventoryDiscardModal->Slot))
    {PopupLayoutSlot->SetAnchors(FAnchors(0));PopupLayoutSlot->SetAlignment(FVector2D::ZeroVector);PopupLayoutSlot->SetAutoSize(false);PopupLayoutSlot->SetSize(FVector2D(260,128));PopupLayoutSlot->SetZOrder(10000);}
    if(InventoryDiscardMessage){InventoryDiscardMessage->SetText(FText::GetEmpty());InventoryDiscardMessage->SetVisibility(ESlateVisibility::Collapsed);}
    // Retain existing authored wrappers, but remove their old 460-wide centered dialog constraints.
    if(USizeBox* Width=Cast<USizeBox>(GetWidgetFromName(TEXT("InventoryDiscardWidth")))){Width->ClearWidthOverride();Width->ClearHeightOverride();}
    if(UBorder* Card=Cast<UBorder>(GetWidgetFromName(TEXT("InventoryDiscardCard")))){Card->SetBrushColor(FLinearColor::Transparent);Card->SetPadding(FMargin(0));}
    if(UWidget* Actions=GetWidgetFromName(TEXT("InventoryDiscardActions")))
        if(UVerticalBoxSlot* PopupLayoutSlot=Cast<UVerticalBoxSlot>(Actions->Slot))PopupLayoutSlot->SetPadding(FMargin(0));
    if(UTextBlock* Title=Cast<UTextBlock>(GetWidgetFromName(TEXT("InventoryDiscardTitle"))))
    {
        Title->SetJustification(ETextJustify::Center);Title->SetAutoWrapText(false);
        FSlateFontInfo Font=Title->GetFont();Font.Size=22;Title->SetFont(Font);Title->SetColorAndOpacity(FSlateColor(FLinearColor(.22f,.10f,.045f)));
        if(UVerticalBoxSlot* PopupLayoutSlot=Cast<UVerticalBoxSlot>(Title->Slot))PopupLayoutSlot->SetPadding(FMargin(0,0,0,10));
    }
    for(const FName Name:{FName(TEXT("InventoryDiscardConfirmLabel")),FName(TEXT("InventoryDiscardCancelLabel"))})
        if(UTextBlock* Label=Cast<UTextBlock>(GetWidgetFromName(Name)))
        {FSlateFontInfo Font=Label->GetFont();Font.Size=18;Label->SetFont(Font);Label->SetAutoWrapText(false);Label->SetColorAndOpacity(FSlateColor(FLinearColor(.22f,.10f,.045f)));}
    for(const FName Name:{FName(TEXT("InventoryDiscardConfirmButton")),FName(TEXT("InventoryDiscardCancelButton"))})
        if(UButton* Button=Cast<UButton>(GetWidgetFromName(Name)))
        {
            FButtonStyle Style=Button->GetStyle();
            Style.Normal=FSlateRoundedBoxBrush(FLinearColor(.77f,.57f,.32f),7.f);
            Style.Hovered=FSlateRoundedBoxBrush(FLinearColor(.86f,.67f,.42f),7.f);
            Style.Pressed=FSlateRoundedBoxBrush(FLinearColor(.64f,.43f,.22f),7.f);
            Style.NormalPadding=FMargin(8,6);Style.PressedPadding=FMargin(8,7,8,5);
            Button->SetStyle(Style);Button->SetBackgroundColor(FLinearColor::White);
            if(UWidget* Label=Button->GetContent())if(UButtonSlot* PopupLayoutSlot=Cast<UButtonSlot>(Label->Slot))PopupLayoutSlot->SetPadding(FMargin(8,5));
        }
    UCanvasPanel* Root=Cast<UCanvasPanel>(WidgetTree->RootWidget);
    if(Root&&!GetWidgetFromName(TEXT("InventoryDiscardHitBlocker")))
    {
        UBorder* Blocker=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("InventoryDiscardHitBlocker"));
        FSlateBrush NoPaint;NoPaint.DrawAs=ESlateBrushDrawType::NoDrawType;Blocker->SetBrush(NoPaint);Blocker->SetPadding(FMargin(0));
        UCanvasPanelSlot* PopupLayoutSlot=Root->AddChildToCanvas(Blocker);PopupLayoutSlot->SetAnchors(FAnchors(0,0,1,1));PopupLayoutSlot->SetOffsets(FMargin(0));PopupLayoutSlot->SetZOrder(9999);
        Blocker->SetVisibility(ESlateVisibility::Collapsed);
    }
}

bool UDWGameplayWidget::PositionInventoryDiscardPopup(const FVector2D& ScreenPosition)
{
    UCanvasPanel* Root=WidgetTree?Cast<UCanvasPanel>(WidgetTree->RootWidget):nullptr;
    UCanvasPanelSlot* PopupLayoutSlot=InventoryDiscardModal?Cast<UCanvasPanelSlot>(InventoryDiscardModal->Slot):nullptr;
    if(!Root||!PopupLayoutSlot)return false;
    const FGeometry& Geometry=Root->GetTickSpaceGeometry();const FVector2D RootSize=Geometry.GetLocalSize();
    if(RootSize.GetMin()<32.f||!FMath::IsFinite(ScreenPosition.X)||!FMath::IsFinite(ScreenPosition.Y))return false;
    // AbsoluteToLocal already removes window offset and DPI scaling. Never divide by DPI again.
    const FVector2D Requested=Geometry.AbsoluteToLocal(ScreenPosition)+FVector2D(8,8);
    const FVector2D Size(FMath::Min(260.,RootSize.X-16.),FMath::Min(128.,RootSize.Y-16.));
    const FVector2D Position(FMath::Clamp(Requested.X,8.,FMath::Max(8.,RootSize.X-Size.X-8.)),FMath::Clamp(Requested.Y,8.,FMath::Max(8.,RootSize.Y-Size.Y-8.)));
    PopupLayoutSlot->SetPosition(Position);PopupLayoutSlot->SetSize(Size);
    PopupLayoutSlot->SetAnchors(FAnchors(0));PopupLayoutSlot->SetAlignment(FVector2D::ZeroVector);PopupLayoutSlot->SetAutoSize(false);
    return true;
}

bool UDWGameplayWidget::HandleInventoryDiscardPointer(const FPointerEvent& Event)
{
    if(!bInventoryDiscardPending)return false;
    // Allow the small card's Yes/No buttons through. Any backdrop click cancels and is consumed.
    if(InventoryDiscardModal&&InventoryDiscardModal->GetTickSpaceGeometry().IsUnderLocation(Event.GetScreenSpacePosition()))return false;
    CancelInventoryDiscard();return true;
}

void UDWGameplayWidget::RefreshInventoryInteractionUI()
{
    EnsureInventoryInteractionUI();
    if(UWidget* Footer=GetWidgetFromName(TEXT("InventoryControlsFooter")))Footer->SetVisibility(CurrentPage==EDWMenuPage::Inventory?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(UWidget* Footer=GetWidgetFromName(TEXT("CraftingControlsFooter")))Footer->SetVisibility(CurrentPage==EDWMenuPage::Crafting?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    for(const FName Name:{FName(TEXT("InventoryControlsHelpText")),FName(TEXT("CraftingControlsHelpText"))})
        if(UTextBlock* Help=Cast<UTextBlock>(GetWidgetFromName(Name)))Help->SetText(GetInventoryControlHelp());
    if(UTextBlock* Title=Cast<UTextBlock>(GetWidgetFromName(TEXT("InventoryDiscardTitle"))))Title->SetText(DWText(this,TEXT("是否丢弃？"),TEXT("Discard?")));
    if(UTextBlock* Label=Cast<UTextBlock>(GetWidgetFromName(TEXT("InventoryDiscardConfirmLabel"))))Label->SetText(DWText(this,TEXT("是"),TEXT("Yes")));
    if(UTextBlock* Label=Cast<UTextBlock>(GetWidgetFromName(TEXT("InventoryDiscardCancelLabel"))))Label->SetText(DWText(this,TEXT("否"),TEXT("No")));
    if(bInventoryDiscardPending&&(!IsInventoryInteractionPage()||PendingDiscardInventory.Get()!=GetInventory()||
        !PendingDiscardInventory.IsValid()||!PendingDiscardInventory->IsSlotSnapshotCurrent(PendingDiscardSnapshot)))
    {
        CancelInventoryDiscard();
        SetToast(DWText(this,TEXT("背包内容已变化，已取消丢弃。"),TEXT("Inventory changed. Discard cancelled.")).ToString());
    }
}

bool UDWGameplayWidget::HandleInventorySlotPointer(int32 SlotIndex,const FPointerEvent& Event,bool& bStartDrag)
{
    bStartDrag=false;
    if(!IsInventoryInteractionPage())return false;
    if(bInventoryDiscardPending)return true;
    UDWInventoryComponent* Bag=GetInventory();
    if(!Bag)return false;
    const FKey Key=Event.GetEffectingButton();
    const bool bDiscard=DWChordMatches(InventoryControls.DiscardClick,Key,Event);
    const bool bStack=DWChordMatches(InventoryControls.QuickStackClick,Key,Event);
    const bool bHalf=DWChordMatches(InventoryControls.SplitHalfClick,Key,Event);
    const bool bOne=DWChordMatches(InventoryControls.SplitOneClick,Key,Event);
    const bool bPlainDrag=Key==InventoryControls.DragButton&&!Event.IsControlDown()&&!Event.IsShiftDown()&&!Event.IsAltDown()&&!Event.IsCommandDown();
    if(!bDiscard&&!bStack&&!bHalf&&!bOne&&!bPlainDrag)return false;
    SelectInventorySlot(SlotIndex);
    FDWInventorySlotSnapshot Snapshot;
    if(!Bag->CaptureSlotSnapshot(SlotIndex,Snapshot))return true;
    if(bDiscard){RequestInventoryDiscardAtScreenPosition(Snapshot,Event.GetScreenSpacePosition());return true;}
    if(bStack)
    {
        const bool bDone=Bag->TryQuickStack(Snapshot.ItemId);
        SelectedSlot=INDEX_NONE;
        SetToast(DWText(this,bDone?TEXT("已合并背包内的同类物品。"):TEXT("同类物品已经堆叠完成。"),bDone?TEXT("Matching bag stacks combined."):TEXT("Matching stacks are already combined.")).ToString());
        RefreshFromHUD(true);return true;
    }
    if(bHalf||bOne)
    {
        int32 NewSlot=INDEX_NONE;const int32 Quantity=bHalf?Snapshot.Quantity/2:1;
        const bool bDone=Bag->TrySplitSlot(SlotIndex,Quantity,NewSlot);
        // Retain source selection so repeated right clicks continue to split the same source stack.
        SetToast(DWText(this,bDone?TEXT("物品已分到新的空槽。"):TEXT("无法拆分：需要至少2个物品和一个空槽。"),bDone?TEXT("Items split into a new slot."):TEXT("Split needs at least two items and one free slot.")).ToString());
        RefreshFromHUD(true);return true;
    }
    bStartDrag=bPlainDrag;return true;
}

UDragDropOperation* UDWGameplayWidget::CreateInventoryDrag(const FDWInventorySlotSnapshot& Snapshot,UDWInventorySlotWidget* Source)
{
    UDWInventoryComponent* Bag=GetInventory();
    if(!IsInventoryInteractionPage()||bInventoryDiscardPending||!Bag||!Bag->IsSlotSnapshotCurrent(Snapshot))return nullptr;
    UDWInventoryDragOperation* Drag=NewObject<UDWInventoryDragOperation>(this);
    Drag->SourceScreen=this;Drag->SourceInventory=Bag;Drag->Snapshot=Snapshot;Drag->Pivot=EDragPivot::MouseDown;
    if(Source&&InventorySlotClass)
    {
        UDWInventorySlotWidget* Visual=CreateWidget<UDWInventorySlotWidget>(GetOwningPlayer(),InventorySlotClass);
        if(Visual)
        {
            Visual->ApplyItemData(this,Snapshot.SlotIndex,FDWItemStack(Snapshot.ItemId,Snapshot.Quantity),false);
            Visual->SetVisibility(ESlateVisibility::HitTestInvisible);Visual->SetRenderOpacity(.85f);Drag->DefaultDragVisual=Visual;
        }
    }
    return Drag;
}

bool UDWGameplayWidget::HandleInventorySlotDrop(int32 SlotIndex,UDragDropOperation* Operation)
{
    UDWInventoryDragOperation* Drag=Cast<UDWInventoryDragOperation>(Operation);
    if(!Drag)return false;
    if(!IsInventoryInteractionPage()||bInventoryDiscardPending||Drag->SourceScreen.Get()!=this||Drag->SourceInventory.Get()!=GetInventory())return true;
    UDWInventoryComponent* Bag=GetInventory();
    if(!Bag||!Bag->IsSlotSnapshotCurrent(Drag->Snapshot))
    {SetToast(DWText(this,TEXT("背包内容已变化，请重新拖动。"),TEXT("Inventory changed. Please drag again.")).ToString());return true;}
    if(Bag->TryMoveOrMergeSnapshot(Drag->Snapshot,SlotIndex))
    {SelectedSlot=INDEX_NONE;RefreshFromHUD(true);}
    return true;
}

bool UDWGameplayWidget::NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation)
{
    UDWInventoryDragOperation* Drag=Cast<UDWInventoryDragOperation>(Operation);
    if(!Drag)return Super::NativeOnDrop(Geometry,Event,Operation);
    if(!IsInventoryInteractionPage()||bInventoryDiscardPending||Drag->SourceScreen.Get()!=this||Drag->SourceInventory.Get()!=GetInventory())return true;
    UBorder* ActiveCard=CurrentPage==EDWMenuPage::Inventory?InventoryCard.Get():CraftingCard.Get();
    const FVector2D Position=Event.GetScreenSpacePosition();
    // Only the in-game backdrop outside the panel counts. The OS desktop, Alt-Tab and cancelled drags do not.
    if(!ActiveCard||!Geometry.IsUnderLocation(Position))return true;
    const FName FooterName=CurrentPage==EDWMenuPage::Inventory?TEXT("InventoryControlsFooter"):TEXT("CraftingControlsFooter");
    if(UWidget* Footer=GetWidgetFromName(FooterName))if(Footer->GetTickSpaceGeometry().IsUnderLocation(Position))return true;
    const FGeometry& CardGeometry=ActiveCard->GetTickSpaceGeometry();
    if(CardGeometry.GetLocalSize().GetMin()<=1.f||CardGeometry.IsUnderLocation(Position))return true;
    RequestInventoryDiscardAtScreenPosition(Drag->Snapshot,Position);
    return true;
}

bool UDWGameplayWidget::HandleInventoryKey(const FKeyEvent& Event)
{
    if(!IsInventoryInteractionPage())return false;
    if(bInventoryDiscardPending)
    {
        if(!Event.IsRepeat())
        {
            if(Event.GetKey()==EKeys::Escape)CancelInventoryDiscard();
            else if(Event.GetKey()==EKeys::Enter)ConfirmInventoryDiscard();
        }
        return true;
    }
    if(!DWChordMatches(InventoryControls.DiscardKey,Event.GetKey(),Event))return false;
    if(!Event.IsRepeat())RequestDiscardSelectedInventory();
    return true;
}

bool UDWGameplayWidget::RequestDiscardSelectedInventory()
{
    FDWInventorySlotSnapshot Snapshot;
    return GetInventory()&&GetInventory()->CaptureSlotSnapshot(SelectedSlot,Snapshot)&&RequestInventoryDiscard(Snapshot);
}

bool UDWGameplayWidget::RequestInventoryDiscard(const FDWInventorySlotSnapshot& Snapshot)
{
    return FSlateApplication::IsInitialized()&&RequestInventoryDiscardAtScreenPosition(Snapshot,FSlateApplication::Get().GetCursorPos());
}

bool UDWGameplayWidget::RequestInventoryDiscardAtScreenPosition(const FDWInventorySlotSnapshot& Snapshot,const FVector2D& ScreenPosition)
{
    UDWInventoryComponent* Bag=GetInventory();
    if(!IsInventoryInteractionPage()||bInventoryDiscardPending||!Bag||!Bag->IsSlotSnapshotCurrent(Snapshot))return false;
    EnsureInventoryInteractionUI();
    if(!bInventoryUIInitialized||!InventoryDiscardModal||!PositionInventoryDiscardPopup(ScreenPosition))return false;
    PendingDiscardInventory=Bag;PendingDiscardSnapshot=Snapshot;bInventoryDiscardPending=true;
    if(InventoryDiscardMessage){InventoryDiscardMessage->SetText(FText::GetEmpty());InventoryDiscardMessage->SetVisibility(ESlateVisibility::Collapsed);}
    RefreshInventoryInteractionUI();
    if(UWidget* Blocker=GetWidgetFromName(TEXT("InventoryDiscardHitBlocker")))Blocker->SetVisibility(ESlateVisibility::Visible);
    InventoryDiscardModal->SetVisibility(ESlateVisibility::Visible);SetKeyboardFocus();
    return true;
}

void UDWGameplayWidget::ConfirmInventoryDiscard()
{
    if(!bInventoryDiscardPending)return;
    UDWInventoryComponent* Bag=PendingDiscardInventory.Get();
    const bool bSuccess=IsInventoryInteractionPage()&&Bag&&Bag==GetInventory()&&Bag->TryDiscardSnapshot(PendingDiscardSnapshot);
    CancelInventoryDiscard();SelectedSlot=INDEX_NONE;
    SetToast(DWText(this,bSuccess?TEXT("物品已丢弃。"):TEXT("背包内容已变化，未丢弃任何物品。"),bSuccess?TEXT("Stack discarded."):TEXT("Inventory changed. Nothing was discarded.")).ToString());
    RefreshFromHUD(true);
}

void UDWGameplayWidget::CancelInventoryDiscard()
{
    bInventoryDiscardPending=false;PendingDiscardSnapshot=FDWInventorySlotSnapshot();PendingDiscardInventory.Reset();
    if(InventoryDiscardModal)InventoryDiscardModal->SetVisibility(ESlateVisibility::Collapsed);
    if(UWidget* Blocker=GetWidgetFromName(TEXT("InventoryDiscardHitBlocker")))Blocker->SetVisibility(ESlateVisibility::Collapsed);
    if(IsInventoryInteractionPage()&&IsInViewport())SetKeyboardFocus();
}
