#include "DWUIEntryWidgets.h"
#include "DWGameplayWidget.h"
#include "DWC01UIStyle.h"
#include "Components/SizeBox.h"
#include "Components/ButtonSlot.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/WrapBox.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "HAL/PlatformTime.h"
#include "GameFramework/PlayerController.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWInventoryInteraction.h"
#include "DWPlayerCharacter.h"
#include "DWLocalizationLibrary.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Engine/Texture2D.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"

namespace
{
    FSlateFontInfo DWC01FitEnglishSlotName(const FText& Name,FSlateFontInfo Font,float AvailableWidth)
    {
        Font.Size=14;
        if(!FSlateApplication::IsInitialized())return Font;
        auto* Renderer=FSlateApplication::Get().GetRenderer();if(!Renderer)return Font;
        const auto Measure=Renderer->GetFontMeasureService();
        TArray<FString> Words;Name.ToString().ParseIntoArrayWS(Words);
        // Keep whole words such as Cinnamon and Breadcrumb. Choose the largest
        // integer size that fits every word, using the actual composite font.
        for(int32 FontSize=14;FontSize>=10;--FontSize)
        {
            Font.Size=FontSize;bool bFits=true;
            for(const FString& Word:Words)
            {
                const FVector2f WordSize(Measure->Measure(Word,Font));
                if(WordSize.X>AvailableWidth){bFits=false;break;}
            }
            if(bFits)break;
        }
        return Font;
    }
    FString DWItemName(UDWGameplayWidget* UI,FName Id)
    {const auto* Config=UI?UI->GetGameplayConfig():nullptr;const auto* D=Config?Config->GetItemDefinition(Id):nullptr;return D?UDWLocalizationLibrary::GetItemDisplayName(UI,*D).ToString():Id.ToString();}
    FString DWStackSummary(UDWGameplayWidget* UI,const TArray<FDWItemStack>& Stacks,bool bOwned)
    {
        TArray<FString> Lines;
        for(const auto& S:Stacks)
        {
            FString Line=FText::Format(DWText(UI,TEXT("{0} × {1}"),TEXT("{0} x {1}")),FText::FromString(DWItemName(UI,S.ItemId)),S.Quantity).ToString();
            if(bOwned)Line+=FText::Format(DWText(UI,TEXT("（拥有 {0}）"),TEXT(" (Owned: {0})")),UI&&UI->GetInventory()?UI->GetInventory()->CountItem(S.ItemId):0).ToString();
            Lines.Add(Line);
        }
        return FString::Join(Lines,TEXT(" + "));
    }
}

void UDWInventorySlotWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(SlotButton){SlotButton->OnClicked.RemoveDynamic(this,&UDWInventorySlotWidget::ClickSlot);SlotButton->OnClicked.AddDynamic(this,&UDWInventorySlotWidget::ClickSlot);}
}
void UDWInventorySlotWidget::ApplyItemData(UDWGameplayWidget* OwnerScreen,int32 Index,const FDWItemStack& InItem,bool bSelected)
{
    Screen=OwnerScreen;SlotIndex=Index;Item=InItem;
    if(Screen)Screen->ApplyWidgetPresentation(this);
    const auto* Config=Screen?Screen->GetGameplayConfig():nullptr;
    const auto* D=Config?Config->GetItemDefinition(Item.ItemId):nullptr;
    const bool bOccupied=!Item.ItemId.IsNone()&&Item.Quantity>0;
    const bool bC01=Screen&&Screen->HasHandbook();const bool bHasIcon=bOccupied&&D&&D->Icon;
    if(ItemNameText)
    {
        const FText DisplayName=bOccupied?(D?UDWLocalizationLibrary::GetItemDisplayName(this,*D):FText::FromName(Item.ItemId)):FText::GetEmpty();
        ItemNameText->SetText(DisplayName);
        if(bC01)
        {
            ItemNameText->SetVisibility(bOccupied&&!bHasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            ItemNameText->SetAutoWrapText(false);ItemNameText->SetWrapTextAt(76.f);
            ItemNameText->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);
            ItemNameText->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);ItemNameText->SetColorAndOpacity(DWC01UI::Ink());
            FSlateFontInfo Font=ItemNameText->GetFont();Font.Size=14;
            if(bOccupied&&!bHasIcon&&UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English)
                Font=DWC01FitEnglishSlotName(DisplayName,Font,76.f);
            ItemNameText->SetFont(Font);
        }
    }
    if(QuantityText){QuantityText->SetText(FText::FromString(bOccupied?FString::FromInt(Item.Quantity):TEXT("")));if(bC01){QuantityText->SetAutoWrapText(false);QuantityText->SetColorAndOpacity(DWC01UI::Ink());FSlateFontInfo Font=QuantityText->GetFont();Font.Size=12;QuantityText->SetFont(Font);}}
    if(ItemIcon)
    {
        if(bHasIcon){ItemIcon->SetBrushFromTexture(D->Icon,false);ItemIcon->SetColorAndOpacity(FLinearColor::White);ItemIcon->SetVisibility(ESlateVisibility::HitTestInvisible);}
        else if(bC01){ItemIcon->SetBrushFromTexture(nullptr);ItemIcon->SetVisibility(ESlateVisibility::Collapsed);}
        else{FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Box;Brush.TintColor=FSlateColor(FLinearColor::White);Brush.ImageSize=FVector2D(32,32);ItemIcon->SetBrush(Brush);ItemIcon->SetColorAndOpacity(bOccupied?(D?D->IconColor:FLinearColor(.85f,.6f,.3f)):FLinearColor(.17f,.14f,.12f));}
    }
    if(bC01)
    {
        if(auto* Dimensions=Cast<USizeBox>(GetWidgetFromName(TEXT("SlotDimensions")))){Dimensions->SetWidthOverride(88);Dimensions->SetHeightOverride(88);}
        if(auto* IconBox=Cast<USizeBox>(GetWidgetFromName(TEXT("ItemIconSize")))){IconBox->SetWidthOverride(36);IconBox->SetHeightOverride(36);IconBox->SetVisibility(bHasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
        if(SlotButton)
        {
            // Keep the authored widgets and their events. Only the runtime arrangement changes:
            // Darumadrop's two name lines must not compete with a third quantity line in 88px.
            if(WidgetTree&&ItemNameText&&QuantityText&&!GetWidgetFromName(TEXT("C03SlotOverlay")))
            {
                UWidget* ExistingContent=SlotButton->GetContent();
                ItemNameText->RemoveFromParent();QuantityText->RemoveFromParent();
                SlotButton->SetContent(nullptr);
                auto* Overlay=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("C03SlotOverlay"));
                if(ExistingContent)
                {
                    auto* Original=Overlay->AddChildToOverlay(ExistingContent);
                    Original->SetHorizontalAlignment(HAlign_Fill);Original->SetVerticalAlignment(VAlign_Center);
                    Original->SetPadding(FMargin(6,0));
                }
                auto* NameRegion=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("C03SlotNameRegion"));
                NameRegion->SetHeightOverride(58);NameRegion->SetClipping(EWidgetClipping::ClipToBounds);
                NameRegion->SetContent(ItemNameText);
                auto* NameSlot=Overlay->AddChildToOverlay(NameRegion);
                NameSlot->SetHorizontalAlignment(HAlign_Fill);NameSlot->SetVerticalAlignment(VAlign_Top);
                NameSlot->SetPadding(FMargin(6,3,6,0));
                auto* CountSlot=Overlay->AddChildToOverlay(QuantityText);
                CountSlot->SetHorizontalAlignment(HAlign_Right);CountSlot->SetVerticalAlignment(VAlign_Bottom);
                CountSlot->SetPadding(FMargin(4,0,6,3));
                SlotButton->SetContent(Overlay);
            }
            if(auto* NameRegion=GetWidgetFromName(TEXT("C03SlotNameRegion")))NameRegion->SetVisibility(bOccupied&&!bHasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
            const bool bQuickSlot=SlotIndex>=0&&SlotIndex<3;
            auto* QuickNumber=Cast<UTextBlock>(GetWidgetFromName(TEXT("C04QuickSlotNumber")));
            if(bQuickSlot&&!QuickNumber&&WidgetTree)
            {
                if(auto* Overlay=Cast<UOverlay>(GetWidgetFromName(TEXT("C03SlotOverlay"))))
                {
                    QuickNumber=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("C04QuickSlotNumber"));
                    QuickNumber->SetAutoWrapText(false);QuickNumber->SetJustification(ETextJustify::Left);
                    auto* NumberSlot=Overlay->AddChildToOverlay(QuickNumber);
                    NumberSlot->SetHorizontalAlignment(HAlign_Left);NumberSlot->SetVerticalAlignment(VAlign_Bottom);
                    NumberSlot->SetPadding(FMargin(8,0,0,3));
                }
            }
            if(QuickNumber)
            {
                QuickNumber->SetText(FText::AsNumber(SlotIndex+1));
                QuickNumber->SetVisibility(bQuickSlot?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
                QuickNumber->SetColorAndOpacity(FLinearColor(1.f,1.f,1.f,.55f));
                FSlateFontInfo NumberFont=QuantityText?QuantityText->GetFont():QuickNumber->GetFont();
                NumberFont.Size=14;NumberFont.OutlineSettings.OutlineSize=0;QuickNumber->SetFont(NumberFont);
            }
            // One neutral brush lets the shared C01 tween own selection/hover colors.
            Screen->StyleC01Button(SlotButton,DWC01UI::Paper(),bSelected,false,true);
            // The first three cells and their separately painted white rims must use
            // identical rounded geometry, rather than a texture with transparent margins.
            FButtonStyle Style=SlotButton->GetStyle();
            Style.Normal=Style.Hovered=Style.Pressed=Style.Disabled=bQuickSlot?
                DWC01UI::QuickSlotFill(FVector2D(88,88)):
                DWC01UI::Surface(FLinearColor::White,FVector2D(88,88));
            Style.NormalPadding=Style.PressedPadding=FMargin(0);SlotButton->SetStyle(Style);
            if(auto* ButtonSlot=Cast<UButtonSlot>(SlotButton->GetContent()?SlotButton->GetContent()->Slot:nullptr))
            {ButtonSlot->SetPadding(FMargin(0));ButtonSlot->SetHorizontalAlignment(HAlign_Fill);ButtonSlot->SetVerticalAlignment(VAlign_Fill);}
        }
    }
    else if(SlotButton)SlotButton->SetBackgroundColor(bSelected?FLinearColor(.9f,.63f,.25f):FLinearColor(.34f,.27f,.21f));
    OnItemPresentationUpdated(bSelected);
}
int32 UDWInventorySlotWidget::NativePaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Cull,
    FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled)const
{
    const int32 Base=Super::NativePaint(Args,G,Cull,Elements,Layer,Style,bParentEnabled);
    if(!Screen||!Screen->HasHandbook()||SlotIndex<0||SlotIndex>=3||!SlotButton||Screen->C01QuickSlotOutlineThickness<=0.f)return Base;
    const FGeometry Cell=SlotButton->GetTickSpaceGeometry();
    const FVector2D Origin=G.AbsoluteToLocal(Cell.LocalToAbsolute(FVector2D::ZeroVector));
    const FVector2D End=G.AbsoluteToLocal(Cell.LocalToAbsolute(Cell.GetLocalSize()));
    const FVector2D Size=End-Origin;if(Size.X<=0.f||Size.Y<=0.f)return Base;
    FSlateBrush Outline=DWC01UI::QuickSlotOutline(Size,Screen->C01QuickSlotOutlineThickness);
    Outline.OutlineSettings.Color=FLinearColor(1,1,1,Style.GetColorAndOpacityTint().A);
    // Separate overlay paint keeps the approved white rim white throughout the selection tween.
    FSlateDrawElement::MakeBox(Elements,Base+1,G.ToPaintGeometry(FVector2f(Size),FSlateLayoutTransform(FVector2f(Origin))),
        &Outline,ESlateDrawEffect::None,FLinearColor::Transparent);
    return Base+1;
}
void UDWInventorySlotWidget::ClickSlot(){if(Screen)Screen->SelectInventorySlot(SlotIndex);}

FReply UDWInventorySlotWidget::NativeOnPreviewMouseButtonDown(const FGeometry& Geometry,const FPointerEvent& Event)
{
    if(Screen)
    {
        bool bDetectDrag=false;
        if(Screen->HandleInventorySlotPointer(SlotIndex,Event,bDetectDrag))
        {
            if(bDetectDrag&&Screen->GetInventory()&&Screen->GetInventory()->CaptureSlotSnapshot(SlotIndex,DragSnapshot))
                return FReply::Handled().DetectDrag(TakeWidget(),Screen->InventoryControls.DragButton);
            return FReply::Handled();
        }
    }
    return Super::NativeOnPreviewMouseButtonDown(Geometry,Event);
}

void UDWInventorySlotWidget::NativeOnDragDetected(const FGeometry& Geometry,const FPointerEvent& Event,UDragDropOperation*& OutOperation)
{
    if(Screen)OutOperation=Screen->CreateInventoryDrag(DragSnapshot,this);
    if(!OutOperation)Super::NativeOnDragDetected(Geometry,Event,OutOperation);
}

bool UDWInventorySlotWidget::NativeOnDrop(const FGeometry& Geometry,const FDragDropEvent& Event,UDragDropOperation* Operation)
{
    if(Screen&&Screen->HandleInventorySlotDrop(SlotIndex,Operation))return true;
    return Super::NativeOnDrop(Geometry,Event,Operation);
}

void UDWRecipeEntryWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(CraftButton){CraftButton->OnClicked.RemoveDynamic(this,&UDWRecipeEntryWidget::ClickCraft);CraftButton->OnClicked.AddDynamic(this,&UDWRecipeEntryWidget::ClickCraft);}
}
void UDWRecipeEntryWidget::ApplyRecipeData(UDWGameplayWidget* OwnerScreen,const FDWRecipeDefinition& R)
{
    Screen=OwnerScreen;RecipeId=R.RecipeId;
    if(Screen)Screen->ApplyWidgetPresentation(this);
    const bool bCan=Screen&&Screen->GetInventory()&&Screen->GetPlayer()&&Screen->GetInventory()->CanCraft(RecipeId,Screen->GetPlayer()->IsYeastForm());
    if(RecipeNameText)RecipeNameText->SetText(UDWLocalizationLibrary::GetRecipeDisplayName(this,R));
    if(IngredientsText)IngredientsText->SetText(FText::FromString(DWText(this,TEXT("需要："),TEXT("Needs: ")).ToString()+DWStackSummary(Screen,R.Inputs,true)));
    if(OutputsText)OutputsText->SetText(FText::FromString(DWText(this,TEXT("获得："),TEXT("Makes: ")).ToString()+DWStackSummary(Screen,R.Outputs,false)));
    const bool bNeedsForm=Screen&&Screen->GetGameplayConfig()&&Screen->GetGameplayConfig()->bRequireTransformationForCrafting&&R.bRequiresYeast;
    if(RequirementText)RequirementText->SetText(bNeedsForm?DWText(this,TEXT("仅酵母形态可制作"),TEXT("Requires Yeast form")):DWText(this,TEXT("任意形态可制作"),TEXT("Any form can craft")));
    if(CraftButton)CraftButton->SetIsEnabled(bCan);
    if(CraftButtonText)CraftButtonText->SetText(bCan?DWText(this,TEXT("制作一份"),TEXT("Craft one")):(bNeedsForm?DWText(this,TEXT("检查材料、形态与背包空间"),TEXT("Check materials, form and bag space")):DWText(this,TEXT("检查材料与背包空间"),TEXT("Check materials and bag space"))));
    OnRecipePresentationUpdated(bCan);
}
void UDWRecipeEntryWidget::ClickCraft(){if(Screen){Screen->PlayClick();Screen->CraftRecipe(RecipeId);}}

void UDWSaveSlotWidget::NativeConstruct()
{
    Super::NativeConstruct();
    if(LoadButton){LoadButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickLoad);LoadButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickLoad);}
    if(NewButton){NewButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickNew);NewButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickNew);}
    if(DeleteButton){DeleteButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickDelete);DeleteButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickDelete);}
    if(CancelDeleteButton){CancelDeleteButton->OnClicked.RemoveDynamic(this,&UDWSaveSlotWidget::ClickCancelDelete);CancelDeleteButton->OnClicked.AddDynamic(this,&UDWSaveSlotWidget::ClickCancelDelete);}
}
void UDWSaveSlotWidget::ApplySaveData(UDWGameplayWidget* OwnerScreen,const FDWSlotSummary& InSummary)
{
    const bool bSameSlot=Screen==OwnerScreen&&Summary.SlotIndex==InSummary.SlotIndex&&Summary.bExists==InSummary.bExists;
    Screen=OwnerScreen;Summary=InSummary;if(!bSameSlot)bConfirmDelete=false;
    if(Screen)Screen->ApplyWidgetPresentation(this);
    FText DisplayName=FText::FromString(Summary.DisplayName);
    // Only translate the exact automatically generated name; preserve user names verbatim.
    const FString DefaultChinese=FString::Printf(TEXT("存档 %d"),Summary.SlotIndex+1);
    const FString DefaultEnglish=FString::Printf(TEXT("Save %d"),Summary.SlotIndex+1);
    if(Summary.DisplayName==DefaultChinese||Summary.DisplayName==DefaultEnglish)
        DisplayName=FText::Format(DWText(this,TEXT("存档 {0}"),TEXT("Save {0}")),Summary.SlotIndex+1);
    if(SlotNameText)SlotNameText->SetText(Summary.bExists?DisplayName:FText::Format(DWText(this,TEXT("存档 {0} · 空槽位"),TEXT("Save {0} - Empty")),Summary.SlotIndex+1));
    if(SlotDateText)SlotDateText->SetText(Summary.bExists?FText::FromString(Summary.Timestamp.ToString(TEXT("%Y-%m-%d  %H:%M"))):DWText(this,TEXT("开始一段新的冒险"),TEXT("Begin a new adventure")));
    if(LoadButton)LoadButton->SetVisibility(Summary.bExists?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(DeleteButton)DeleteButton->SetVisibility(Summary.bExists?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    if(NewButton)NewButton->SetVisibility(Summary.bExists?ESlateVisibility::Collapsed:ESlateVisibility::Visible);
    for(UButton* B:{LoadButton.Get(),NewButton.Get(),DeleteButton.Get(),CancelDeleteButton.Get()})
        if(B)if(auto* T=Cast<UTextBlock>(B->GetContent()))T->SetAutoWrapText(false);
    ApplyCardLayout();
    RefreshDeleteState();
}
void UDWSaveSlotWidget::RefreshDeleteState()
{
    if(DeleteButtonText)DeleteButtonText->SetAutoWrapText(false);
    if(DeleteButtonText)DeleteButtonText->SetText(bConfirmDelete?DWText(this,TEXT("确认永久删除"),TEXT("Confirm delete")):DWText(this,TEXT("删除"),TEXT("Delete")));
    if(CancelDeleteButton)CancelDeleteButton->SetVisibility(bConfirmDelete?ESlateVisibility::Visible:ESlateVisibility::Collapsed);
    OnSavePresentationUpdated(bConfirmDelete);
    ApplyCardLayout();
}
void UDWSaveSlotWidget::ClickLoad(){if(Screen){Screen->PlayClick();Screen->LoadSlot(Summary.SlotIndex);}}
void UDWSaveSlotWidget::ClickNew(){if(Screen){Screen->PlayClick();Screen->NewSlot(Summary.SlotIndex);}}
void UDWSaveSlotWidget::ClickDelete(){if(Screen)Screen->PlayClick();if(!bConfirmDelete){bConfirmDelete=true;RefreshDeleteState();return;}if(Screen)Screen->DeleteSlot(Summary.SlotIndex);}
void UDWSaveSlotWidget::ClickCancelDelete(){if(Screen)Screen->PlayClick();bConfirmDelete=false;RefreshDeleteState();}

void UDWInventorySlotWidget::NativeOnInitialized(){Super::NativeOnInitialized();if(SlotButton)DWC01UI::DisableSlotMotion(SlotButton);}
void UDWRecipeEntryWidget::NativeOnInitialized(){Super::NativeOnInitialized();if(CraftButton)DWC01UI::AttachMotion(CraftButton,false,true);}
void UDWSaveSlotWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();ApplyCardLayout();
    for(auto* B:{LoadButton.Get(),NewButton.Get(),DeleteButton.Get(),CancelDeleteButton.Get()})if(B)DWC01UI::AttachMotion(B,false,true);
}
void UDWSaveSlotWidget::ApplyCardLayout()
{
    if(!WidgetTree)return;
    SetVisibility(ESlateVisibility::Visible);
    if(auto* Card=Cast<UBorder>(GetWidgetFromName(TEXT("SaveSlotCard"))))
    {
        Card->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,FMath::Max(0.f,CardCornerRadius)));
        Card->SetBrushColor(FMath::Lerp(DWC01UI::Paper(),FLinearColor::White,CardHoverAlpha*FMath::Clamp(CardHoverBrightness,0.f,1.f)));
        Card->SetPadding(CardContentPadding);Card->SetVisibility(ESlateVisibility::Visible);
        if(Card->GetContent())if(auto* LayoutSlot=Cast<UBorderSlot>(Card->GetContent()->Slot))LayoutSlot->SetPadding(CardContentPadding);
    }
    for(auto* Label:{SlotNameText.Get(),SlotDateText.Get()})if(Label)
    {
        auto Font=Label->GetFont();Font.Size=Label==SlotNameText?22:16;Label->SetFont(Font);
        Label->SetAutoWrapText(false);Label->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
        if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Label->Slot))LayoutSlot->SetPadding(FMargin(0,0,0,Label==SlotNameText?4.f:0.f));
    }
    if(auto* Labels=GetWidgetFromName(TEXT("SaveLabels")))if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Labels->Slot))LayoutSlot->SetPadding(FMargin(0));
    if(auto* Wrap=Cast<UWrapBox>(GetWidgetFromName(TEXT("SaveActionsWrap"))))
    {
        Wrap->SetInnerSlotPadding(FVector2D(16,10));
        if(auto* LayoutSlot=Cast<UVerticalBoxSlot>(Wrap->Slot))LayoutSlot->SetPadding(FMargin(0,10,0,0));
    }
    for(auto* Button:{LoadButton.Get(),NewButton.Get(),DeleteButton.Get(),CancelDeleteButton.Get()})if(Button)
    {
        // Named SizeBoxes survive Bounce's wrapper. Hide the whole box for inactive actions.
        const FName BoxName(*(Button->GetName()+TEXT("MinimumWidth")));
        if(auto* Box=Cast<USizeBox>(GetWidgetFromName(BoxName)))
        {
            Box->SetMinDesiredWidth(Button==DeleteButton&&bConfirmDelete?220.f:160.f);Box->SetMinDesiredHeight(44);
            Box->SetVisibility(Button->GetVisibility()==ESlateVisibility::Collapsed?ESlateVisibility::Collapsed:ESlateVisibility::SelfHitTestInvisible);
        }
        if(Screen)Screen->StyleC01Button(Button,Button==DeleteButton?DWC01UI::Danger():FMath::Lerp(DWC01UI::Paper(),FLinearColor::White,.55f));
        if(Button!=DeleteButton)
        {
            // A solid white brush keeps color values independent of the old colored paper texture.
            auto Design=Button->GetStyle();Design.Normal=Design.Hovered=Design.Pressed=Design.Disabled=FSlateRoundedBoxBrush(FLinearColor::White,10.f);Button->SetStyle(Design);
        }
        DWC01UI::CenterButtonLabel(Button,Screen?Screen->C01ButtonLatinOpticalOffsetEm:.15f);
    }
}
void UDWSaveSlotWidget::NativeTick(const FGeometry& MyGeometry,float InDeltaTime)
{
    Super::NativeTick(MyGeometry,InDeltaTime);
    const double Now=FPlatformTime::Seconds();
    const float Step=CardLastTickTime>0?float(FMath::Clamp(Now-CardLastTickTime,0.,.1)):0.f;CardLastTickTime=Now;
    const auto* OwnerHUD=GetOwningPlayer()?Cast<ADWGameplayHUD>(GetOwningPlayer()->GetHUD()):nullptr;
    const bool Active=OwnerHUD&&OwnerHUD->GetMenuPage()==EDWMenuPage::SaveSlots&&FSlateApplication::IsInitialized()&&FSlateApplication::Get().IsActive();
    const float Target=Active&&IsHovered()?1.f:0.f;
    CardHoverAlpha=Active&&CardHoverSeconds>0?FMath::FInterpConstantTo(CardHoverAlpha,Target,Step,1.f/CardHoverSeconds):Target;
    if(auto* Card=Cast<UBorder>(GetWidgetFromName(TEXT("SaveSlotCard"))))
        Card->SetBrushColor(FMath::Lerp(DWC01UI::Paper(),FLinearColor::White,CardHoverAlpha*FMath::Clamp(CardHoverBrightness,0.f,1.f)));
}
void UDWSaveSlotWidget::NativeDestruct()
{
    CardHoverAlpha=0;CardLastTickTime=0;Super::NativeDestruct();
}
