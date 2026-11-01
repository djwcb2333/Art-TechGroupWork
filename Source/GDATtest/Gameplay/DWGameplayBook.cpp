#include "DWGameplayWidget.h"
#include "DWCharacterPreview.h"
#include "Animation/AnimSequenceBase.h"
#include "DWLanguageUIBridgeComponent.h"
#include "DWLanguageItemTooltipFactory.h"
#include "DWLanguageSubsystem.h"
#include "DWCharacterA01Component.h"
#include "DWHandbookButton.h"
#include "DWUIBounceComponent.h"
#include "DWC01UIStyle.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWWorldEvent.h"
#include "DWPlayerController.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayConfig.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWInventoryComponent.h"
#include "DWUIEntryWidgets.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/ScrollBox.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/WidgetSwitcher.h"
#include "Components/NativeWidgetHost.h"
#include "Widgets/SNullWidget.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Image.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Widgets/SCompoundWidget.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"

void UDWHandbookButton::Configure(UDWGameplayWidget* InScreen,int32 InAction,bool bItemSlot)
{
 bC01ItemSlot=InAction>=1000||bItemSlot;
 if(bC01ItemSlot)DWC01UI::DisableSlotMotion(this);else DWC01UI::AttachMotion(this,false);
 Screen=InScreen;Action=InAction;OnClicked.AddUniqueDynamic(this,&UDWHandbookButton::Dispatch);
}
void UDWHandbookButton::Dispatch(){if(Screen)Screen->HandleHandbookAction(Action);}

TSharedRef<SWidget> UDWHandbookFrame::RebuildWidget()
{
 if(!Bounce){Bounce=NewObject<UDWUIBounceComponent>(this);Bounce->bPlayOnConstruct=false;Bounce->bAutoBindButtons=false;Bounce->bAnimateNonButtonHover=false;Bounce->PulseScaleOffset=.015f;Bounce->PulseOffset=FVector2D(0,-3);Bounce->PulseAngle=1.2f;Bounce->Initialize(this);}
 Bounce->PreConstruct(IsDesignTime());
 auto Wrapped=Bounce->RebuildWidgetWithContent(Super::RebuildWidget());Bounce->Construct();return Wrapped;
}
void UDWHandbookFrame::ReleaseSlateResources(bool bReleaseChildren){if(Bounce)Bounce->Destruct();Super::ReleaseSlateResources(bReleaseChildren);}
void UDWHandbookFrame::Pulse(){if(Bounce)Bounce->Pulse();}

namespace DWHandbook
{
const FLinearColor Paper=DWC01UI::Paper(),Ink=DWC01UI::Ink(),Leather=DWC01UI::Muted(),SlotColor=DWC01UI::Muted(),Gold=DWC01UI::Gold(),Red=DWC01UI::Danger();
const EDWAnimatedWeapon Weapons[]={EDWAnimatedWeapon::Spear,EDWAnimatedWeapon::Greatsword,EDWAnimatedWeapon::ShieldAxe,EDWAnimatedWeapon::DualDaggers,EDWAnimatedWeapon::SporeGun,EDWAnimatedWeapon::Shotgun,EDWAnimatedWeapon::MoldBow,EDWAnimatedWeapon::Alcohol,EDWAnimatedWeapon::Unarmed};
constexpr int32 EquipmentColumns=2;
const EDWMenuPage Pages[]={EDWMenuPage::Character,EDWMenuPage::Inventory,EDWMenuPage::Crafting,EDWMenuPage::Building,EDWMenuPage::LanguageJournal};
const int32 VisiblePageIndices[]={0,1,2,4}; // Preserve PageSwitcher indices.
const TCHAR* CN[]={TEXT("角色"),TEXT("背包"),TEXT("制作"),TEXT("建造"),TEXT("语言")};
const TCHAR* EN[]={TEXT("Character"),TEXT("Inventory"),TEXT("Crafting"),TEXT("Building"),TEXT("Language")};
struct Builder
{
 UDWGameplayWidget* Screen;UWidgetTree* Tree;
 template<class T>T* Make(const TCHAR* Name=nullptr){return Tree->ConstructWidget<T>(T::StaticClass(),Name?FName(Name):NAME_None);}
 UTextBlock* Text(FText Value,int32 Size=20){auto* T=Make<UTextBlock>();T->SetText(Value);T->SetColorAndOpacity(Ink);T->SetAutoWrapText(true);FSlateFontInfo F=T->GetFont();F.Size=Size;if(Screen->GetDWHUD()&&Screen->GetDWHUD()->GameUIFont)F.FontObject=Screen->GetDWHUD()->GameUIFont;T->SetFont(F);return T;}
 UTextBlock* LocalText(const TCHAR* Chinese,const TCHAR* English,int32 Size=20){auto* T=Text(DWText(Screen,Chinese,English),Size);Screen->RegisterHandbookLabel(T,Chinese,English);return T;}
 UDWHandbookButton* LocalButton(const TCHAR* Chinese,const TCHAR* English,int32 Action,FLinearColor Color=Paper,bool bItemSlot=false,const TCHAR* WidgetName=nullptr){auto* B=Button(DWText(Screen,Chinese,English),Action,Color,bItemSlot,WidgetName);Screen->RegisterHandbookLabel(Cast<UTextBlock>(B->GetContent()),Chinese,English);return B;}
 USizeBox* Size(UWidget* W,float X=0,float Y=0){auto* B=Make<USizeBox>();if(X>0)B->SetWidthOverride(X);if(Y>0)B->SetHeightOverride(Y);B->SetContent(W);return B;}
 UWidget* FitImage(UWidget* Image,float X,float Y){auto* Fit=Make<UScaleBox>();Fit->SetStretch(EStretch::ScaleToFit);Fit->SetContent(Image);return Size(Fit,X,Y);}
 UVerticalBoxSlot* V(UVerticalBox* P,UWidget* W,float Pad=6,bool Fill=false){auto* S=P->AddChildToVerticalBox(W);S->SetPadding(FMargin(0,Pad));if(Fill)S->SetSize(FSlateChildSize(ESlateSizeRule::Fill));return S;}
 UHorizontalBoxSlot* H(UHorizontalBox* P,UWidget* W,float Weight=0,float Pad=8){auto* S=P->AddChildToHorizontalBox(W);S->SetPadding(FMargin(Pad,0));if(Weight>0){FSlateChildSize Z(ESlateSizeRule::Fill);Z.Value=Weight;S->SetSize(Z);}return S;}
 UDWHandbookButton* Button(FText Label,int32 Action,FLinearColor Color=Paper,bool bItemSlot=false,const TCHAR* WidgetName=nullptr){auto* B=Make<UDWHandbookButton>(WidgetName);B->Configure(Screen,Action,bItemSlot);B->C01BaseColor=Color;Screen->StyleC01Button(B,Color,false,false,bItemSlot||Action>=1000);auto* T=Text(Label,18);T->SetColorAndOpacity(Color.Equals(Red,.0001f)?FLinearColor::White:Ink);T->SetJustification(ETextJustify::Center);T->SetAutoWrapText(false);T->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);B->SetContent(T);return B;}
 UBorder* Card(UWidget* W,FLinearColor C=SlotColor,float Padding=20){auto* B=Make<UBorder>();B->SetBrush(DWC01UI::Surface(C,FVector2D(400,200)));B->SetBrushColor(FLinearColor::White);B->SetPadding(FMargin(Padding));B->SetContent(W);return B;}
 UCanvasPanelSlot* At(UCanvasPanel* P,UWidget* W,float X,float Y,float Width,float Height){auto* S=P->AddChildToCanvas(W);S->SetPosition(FVector2D(X,Y));S->SetSize(FVector2D(Width,Height));return S;}
 UImage* Art(const TCHAR* AssetName,FVector2D Size){auto* I=Make<UImage>();I->SetBrush(DWC01UI::Texture(AssetName,Size));I->SetVisibility(ESlateVisibility::HitTestInvisible);return I;}

};
}


TSharedRef<SWidget> UDWHandbookButton::RebuildWidget(){return Super::RebuildWidget();}
void UDWHandbookButton::UpdateSelection(bool bSelected)
{
 const double Now=FPlatformTime::Seconds();const float Target=bSelected?1.f:0.f;
 if(LastSelectionTick<=0)Selection=Target;
 else Selection=FMath::FInterpConstantTo(Selection,Target,FMath::Min(float(Now-LastSelectionTick),.1f),1.f/FMath::Max(.001f,Screen?Screen->C01FocusSeconds:.18f));
 LastSelectionTick=Now;InvalidateLayoutAndVolatility();
 if(Screen)Screen->StyleC01Button(this,C01BaseColor,bSelected,bC01TransparentRest);
 if(auto* T=Cast<UTextBlock>(GetContent()))T->SetColorAndOpacity((GetFName()==TEXT("HandbookTalentEntry")||(!bSelected&&C01BaseColor.Equals(DWC01UI::Danger(),.0001f)))?FLinearColor::White:DWHandbook::Ink);
}

void UDWGameplayWidget::RegisterHandbookLabel(UTextBlock* Label,const TCHAR* Chinese,const TCHAR* English)
{if(Label)HandbookLabels.Add(Label,TPair<FString,FString>(Chinese,English));}

void UDWGameplayWidget::InitializeHandbook()
{
 if(!WidgetTree||!PageSwitcher||HandbookPageIndex!=INDEX_NONE)return;
 using namespace DWHandbook;Builder D{this,WidgetTree};
 auto* Root=D.Make<UCanvasPanel>(TEXT("HandbookRuntimeRoot"));auto* Scale=D.Make<UScaleBox>();Scale->SetStretch(EStretch::ScaleToFit);
 auto* CS=Root->AddChildToCanvas(Scale);const float RelativeSize=FMath::Clamp(HandbookScreenFraction/.86f,.5f,1.f),Inset=(1.f-RelativeSize)*.5f;CS->SetAnchors(FAnchors(Inset,Inset,1.f-Inset,1.f-Inset));CS->SetOffsets(FMargin(0));
 auto* Whole=D.Make<UCanvasPanel>();HandbookFrame=D.Make<UDWHandbookFrame>(TEXT("HandbookFrame"));HandbookFrame->SetPadding(FMargin(0));HandbookFrame->SetBrushColor(FLinearColor::Transparent);HandbookFrame->SetContent(Whole);Scale->SetContent(D.Size(HandbookFrame,1280,800));
 D.At(Whole,D.Art(TEXT("T_DW_C01_Menu_Paper_1160x650"),FVector2D(1160,650)),60,75,1160,650);
 auto* Tabs=D.Make<UHorizontalBox>();D.At(Whole,Tabs,108,123,900,60);HandbookTabButtons.Reset();
 for(const int32 PageIndex:VisiblePageIndices){auto* B=D.Button(DWText(this,CN[PageIndex],EN[PageIndex]),PageIndex);B->bBookTab=true;B->bC01TransparentRest=true;D.H(Tabs,D.Size(B,152,60),0,0)->SetPadding(FMargin(0,0,12,0));HandbookTabButtons.Add(B);}
 auto* Close=D.Make<UDWHandbookButton>(TEXT("HandbookClose"));Close->Configure(this,10);Close->bC01TransparentRest=true;Close->C01BaseColor=FLinearColor::Transparent;StyleC01Button(Close,FLinearColor::Transparent,false,true);
 auto* Cross=D.Art(TEXT("T_DW_C04_CloseBrush"),FVector2D(64,64));Cross->SetColorAndOpacity(Ink);Close->SetContent(D.FitImage(Cross,64,64));Close->SetToolTipText(DWText(this,TEXT("关闭"),TEXT("Close")));D.At(Whole,Close,1104,121,72,64);
 HandbookPages=D.Make<UWidgetSwitcher>(TEXT("HandbookPages"));D.At(Whole,HandbookPages,108,207,1064,470);

 auto* Character=D.Make<UCanvasPanel>();HandbookPages->AddChild(Character);
 const bool LanguageTrial=DWLanguageItemTooltip::IsEnabled(this);
 // Wider single-line equipment buttons keep long English names at the shared font size.
 auto* OwnedScroll=D.Make<UScrollBox>();HandbookEquipmentList=D.Make<UVerticalBox>(TEXT("HandbookEquipmentList"));OwnedScroll->AddChild(HandbookEquipmentList);
 for(int32 R=0;R<(UE_ARRAY_COUNT(Weapons)+EquipmentColumns-1)/EquipmentColumns;++R){auto* Row=D.Make<UHorizontalBox>();D.V(HandbookEquipmentList,Row,4);for(int32 Col=0;Col<EquipmentColumns&&R*EquipmentColumns+Col<UE_ARRAY_COUNT(Weapons);++Col)D.H(Row,D.Size(D.Button(FText::GetEmpty(),200+R*EquipmentColumns+Col),270,64),0,6);}
  // Display only: preserve all gameplay owners and the existing resource snapshot.
  HandbookResourceBars.Reset();HandbookResourceLabels.Reset();
  {
   const FLinearColor Fill[]={FLinearColor::FromSRGBColor(FColor(185,99,69)),FLinearColor::FromSRGBColor(FColor(143,156,141)),FLinearColor::FromSRGBColor(FColor(146,150,104)),FLinearColor::FromSRGBColor(FColor(196,161,96))};
   const TSoftObjectPtr<UTexture2D>* Icons[]={&C01StatusIconHealth,&C01StatusIconWater,&C01StatusIconFood,&C01StatusIconSpecial};
   UMaterialInterface* WhiteIconMaterial=CharacterStatusWhiteMaterial.LoadSynchronous();
   for(int32 I=0;I<4;++I)
   {
    auto* Layers=D.Make<UOverlay>();auto* Bar=D.Make<UProgressBar>();FProgressBarStyle Style=Bar->GetWidgetStyle();
    Style.BackgroundImage=FSlateRoundedBoxBrush(DWC01UI::Muted(),18.f,FVector2D(390,82));Style.FillImage=FSlateRoundedBoxBrush(FLinearColor::White,18.f,FVector2D(390,82));
    Bar->SetWidgetStyle(Style);Bar->SetFillColorAndOpacity(Fill[I]);
    auto* Background=Layers->AddChildToOverlay(Bar);Background->SetHorizontalAlignment(HAlign_Fill);Background->SetVerticalAlignment(VAlign_Fill);
    auto* Row=D.Make<UHorizontalBox>();auto* Icon=D.Make<UImage>();auto* SourceIcon=Icons[I]->LoadSynchronous();Icon->SetBrushFromTexture(SourceIcon,false);
    if(SourceIcon&&WhiteIconMaterial)if(auto* WhiteIcon=UMaterialInstanceDynamic::Create(WhiteIconMaterial,Icon))
    {WhiteIcon->SetTextureParameterValue(TEXT("SourceIcon"),SourceIcon);Icon->SetBrushFromMaterial(WhiteIcon);}
    Icon->SetColorAndOpacity(FLinearColor::White);Icon->SetVisibility(ESlateVisibility::HitTestInvisible);
    D.H(Row,D.FitImage(Icon,42,42),0,12)->SetVerticalAlignment(VAlign_Center);
    auto* Label=D.Text(FText::GetEmpty(),17);Label->SetColorAndOpacity(FLinearColor::White);Label->SetAutoWrapText(false);D.H(Row,Label,1,4)->SetVerticalAlignment(VAlign_Center);
    auto* Foreground=Layers->AddChildToOverlay(Row);Foreground->SetHorizontalAlignment(HAlign_Fill);Foreground->SetVerticalAlignment(VAlign_Center);
    D.At(Character,Layers,(I%2)*406.f,90.f+(I/2)*130.f,390.f,114.f);
    HandbookResourceLabels.Add(Label);HandbookResourceBars.Add(Bar);
   }
  }
  // Independent frames remain visible while the dynamic preview replaces its placeholder.
  constexpr float EquipmentY=350.f, EquipmentSize=114.f, PreviewX=814.f, PreviewY=90.f, PreviewW=250.f, PreviewH=374.f;
  auto* WeaponFrame=D.Make<UBorder>(TEXT("HandbookWeaponFrame"));WeaponFrame->SetPadding(FMargin(0));WeaponFrame->SetBrush(FSlateRoundedBoxBrush(SlotColor,16.f));
  D.At(Character,WeaponFrame,0,EquipmentY,EquipmentSize,EquipmentSize);
  // Form is a state, not an equipped core; the stat grid already presents it.
  HandbookFormIcon=nullptr;HandbookFormPlaceholder=nullptr;
  HandbookEquippedIcon=D.Make<UImage>();HandbookEquippedIcon->SetVisibility(ESlateVisibility::Hidden);
  auto* EquippedImageBox=D.FitImage(HandbookEquippedIcon,86,86);EquippedImageBox->SetVisibility(ESlateVisibility::HitTestInvisible);
  D.At(Character,EquippedImageBox,14,EquipmentY+14,86,86);
  auto* PreviewFrame=D.Card(nullptr,SlotColor,0);PreviewFrame->SetBrush(FSlateRoundedBoxBrush(SlotColor,18.f));
  PreviewFrame->SetVisibility(ESlateVisibility::HitTestInvisible);D.At(Character,PreviewFrame,PreviewX,PreviewY,PreviewW,PreviewH);
  // Preserve the 512 x 768 render target aspect ratio.
  constexpr float PreviewImageW=PreviewH*2.f/3.f, PreviewImageX=PreviewX+(PreviewW-PreviewImageW)*.5f;
  HandbookPreviewPlaceholder=D.Art(TEXT("T_DW_C01_CharacterPreview_Artwork_reservation_e9924678"),FVector2D(250,375));
  D.At(Character,HandbookPreviewPlaceholder,PreviewImageX,PreviewY,PreviewImageW,PreviewH);
  HandbookPreviewImage=D.Make<UImage>();HandbookPreviewImage->SetVisibility(ESlateVisibility::Hidden);
  D.At(Character,HandbookPreviewImage,PreviewImageX,PreviewY,PreviewImageW,PreviewH);
  D.At(Character,D.LocalText(TEXT("角色属性"),TEXT("Character attributes"),24),0,0,570,64);
  auto* TalentEntry=D.LocalButton(TEXT("天赋"),TEXT("Talents"),40,SlotColor,false,TEXT("HandbookTalentEntry"));
  if(auto* Label=Cast<UTextBlock>(TalentEntry->GetContent()))Label->SetColorAndOpacity(FLinearColor::White);
  D.At(Character,TalentEntry,406,EquipmentY,390,EquipmentSize);

 auto* Inventory=D.Make<UCanvasPanel>();HandbookPages->AddChild(Inventory);
 InventoryGrid=D.Make<UUniformGridPanel>(TEXT("HandbookInventoryGrid"));InventoryGrid->SetSlotPadding(FMargin(4));InventoryGrid->SetMinDesiredSlotWidth(96);InventoryGrid->SetMinDesiredSlotHeight(96);InventoryColumns=6;
 // The bag has a single inventory grid; weapon equipment continues through the item actions.
 auto* InvScroll=D.Make<UScrollBox>();InvScroll->SetAnimateWheelScrolling(true);InvScroll->AddChild(InventoryGrid);D.At(Inventory,InvScroll,0,0,628,396);
 LanguageBagItemsArea=InvScroll;LanguageBagEquipmentArea=OwnedScroll;
 OwnedScroll->SetVisibility(ESlateVisibility::Collapsed);
 D.At(Inventory,D.LocalButton(TEXT("拆分"),TEXT("Split"),22),432,406,88,44);D.At(Inventory,D.LocalButton(TEXT("合并"),TEXT("Merge"),24),532,406,88,44);
 HandbookItemImage=D.Make<UImage>();auto* ItemCard=D.Card(D.FitImage(HandbookItemImage,120,120),SlotColor,8);
 ItemCard->SetBrush(FSlateRoundedBoxBrush(SlotColor,16.f));D.At(Inventory,ItemCard,656,0,136,136);
 HandbookSelection=D.Text(FText::GetEmpty(),24);HandbookSelection->SetAutoWrapText(false);
 auto* SelectionFit=D.Make<UScaleBox>();SelectionFit->SetStretch(EStretch::ScaleToFit);SelectionFit->SetStretchDirection(EStretchDirection::DownOnly);SelectionFit->SetContent(HandbookSelection);
 if(auto* SelectionSlot=Cast<UScaleBoxSlot>(HandbookSelection->Slot)){SelectionSlot->SetHorizontalAlignment(HAlign_Left);SelectionSlot->SetVerticalAlignment(VAlign_Center);}
 D.At(Inventory,SelectionFit,812,0,252,56);
 InventoryDetailsText=D.Text(FText::GetEmpty(),17);auto* DescriptionScroll=D.Make<UScrollBox>();DescriptionScroll->AddChild(InventoryDetailsText);D.At(Inventory,DescriptionScroll,812,62,252,106);
 HandbookRelatedRecipeButton=D.LocalButton(TEXT("相关配方"),TEXT("Related recipe"),25);D.At(Inventory,HandbookRelatedRecipeButton,656,262,408,56);
 InventoryUseButton=D.LocalButton(TEXT("使用"),TEXT("Use"),21);D.At(Inventory,InventoryUseButton,656,334,196,48);D.At(Inventory,D.LocalButton(TEXT("丢弃"),TEXT("Discard"),23,Red),868,334,196,48);
 auto* Equip=D.LocalButton(TEXT("装备"),TEXT("Equip"),27,Paper,false,TEXT("HandbookInventoryEquip"));Equip->SetIsEnabled(false);D.At(Inventory,Equip,656,398,196,48);
 HandbookCraftMoreButton=D.LocalButton(TEXT("制作更多"),TEXT("Craft more"),26,Gold);D.At(Inventory,HandbookCraftMoreButton,868,398,196,48);

 auto* Craft=D.Make<UCanvasPanel>();HandbookPages->AddChild(Craft);auto* Categories=D.Make<UHorizontalBox>();D.At(Craft,Categories,0,0,558,44);
 const TCHAR* CatCN[]={TEXT("全部"),TEXT("武器"),TEXT("护甲"),TEXT("消耗品")};const TCHAR* CatEN[]={TEXT("All"),TEXT("Weapons"),TEXT("Armour"),TEXT("Consumables")};
 // Preserve the first three hit areas; the longer English category needs its own width.
 for(int32 I=0;I<4;++I){auto* B=D.LocalButton(CatCN[I],CatEN[I],-1);B->SetIsEnabled(I==0);D.H(Categories,D.Size(B,I==3?164.f:118.f,42),0,5);}
 auto* RecipeScroll=D.Make<UScrollBox>();HandbookRecipeList=D.Make<UVerticalBox>(TEXT("HandbookRecipeList"));RecipeScroll->AddChild(HandbookRecipeList);D.At(Craft,RecipeScroll,0,68,520,362);
 HandbookRecipeImage=D.Make<UImage>();auto* RecipeCard=D.Card(D.FitImage(HandbookRecipeImage,120,120),SlotColor,8);
 RecipeCard->SetBrush(FSlateRoundedBoxBrush(SlotColor,16.f));D.At(Craft,RecipeCard,568,0,136,136);
 HandbookRecipeDetails=D.Text(FText::GetEmpty(),20);D.At(Craft,HandbookRecipeDetails,724,0,340,136);
 auto* MaterialScroll=D.Make<UScrollBox>();auto* MaterialList=D.Make<UVerticalBox>(TEXT("HandbookRecipeItems"));MaterialScroll->AddChild(MaterialList);D.At(Craft,MaterialScroll,568,148,496,170);
 LanguageRecipeNeeds=D.Make<UDWLanguageRecipeNeedsWidget>(TEXT("LanguageRecipeNeeds"));D.At(Craft,LanguageRecipeNeeds,568,326,496,82);
 auto* Quantity=D.LocalText(TEXT("1 份"),TEXT("1 craft"),18);Quantity->SetJustification(ETextJustify::Center);Quantity->SetAutoWrapText(false);auto* QuantityCard=D.Card(Quantity,SlotColor,0);QuantityCard->SetPadding(FMargin(8,0));QuantityCard->SetVerticalAlignment(VAlign_Center);D.At(Craft,QuantityCard,568,422,258,48);
 HandbookCraftButton=D.LocalButton(TEXT("制作"),TEXT("Craft"),30,Gold);D.At(Craft,HandbookCraftButton,880,422,184,48);

 auto* Building=D.Make<UHorizontalBox>();HandbookPages->AddChild(Building);auto* Facilities=D.Make<UVerticalBox>();
 if(LanguageTrial)
 {
  const FName Ids[]={TEXT("CraftingTable"),TEXT("Furnace"),TEXT("FermentationVat")};const auto* Config=GetGameplayConfig();
  for(int32 I=0;I<3;++I)
  {
   const auto* Item=Config?Config->GetItemDefinition(Ids[I]):nullptr;auto* B=D.Button(Item?UDWLocalizationLibrary::GetItemDisplayName(this,*Item):FText::FromName(Ids[I]),50+I,SlotColor,true);B->SetIsEnabled(Item!=nullptr);
   if(Item){auto* Row=D.Make<UHorizontalBox>();auto* Image=D.Make<UImage>();Image->SetBrushFromTexture(Item->Icon.Get());D.H(Row,D.FitImage(Image,64,64));D.H(Row,D.Text(UDWLocalizationLibrary::GetItemDisplayName(this,*Item),21),1);B->SetContent(Row);DWLanguageItemTooltip::AttachItem(B,this,FDWItemStack(Ids[I],1));}
   D.V(Facilities,D.Size(B,0,90),10);
  }
  D.H(Building,Facilities,1);auto* Info=D.Make<UVerticalBox>();D.V(Info,D.LocalText(TEXT("设施制作与放置"),TEXT("Crafting and placing facilities"),30));
  D.V(Info,D.LocalText(TEXT("点击设施：已在背包中则选中该物品，否则查看其现有配方。\n\n在背包选择设施并使用，随后用鼠标选择有效位置；左键确认，Q/E旋转，Esc取消。\n\n制作条件与材料以制作页当前显示为准。"),TEXT("Select a facility to find it in your inventory, or view its existing recipe.\n\nUse the facility from your inventory, then choose a valid location. LMB confirms, Q/E rotates, Esc cancels.\n\nThe crafting page shows current materials and requirements."),22),20,true);
  D.V(Info,D.LocalButton(TEXT("打开背包"),TEXT("Open inventory"),1));D.H(Building,Info,2);
 }
 else
 {
 const TCHAR* BuildCN[]={TEXT("篝火"),TEXT("工作台"),TEXT("围栏")};const TCHAR* BuildEN[]={TEXT("Campfire"),TEXT("Workbench"),TEXT("Fence")};
 for(int32 I=0;I<3;++I){auto* B=D.LocalButton(BuildCN[I],BuildEN[I],-1,SlotColor,true);B->SetIsEnabled(false);D.V(Facilities,D.Size(B,0,90),10);}D.H(Building,Facilities,1);
 auto* BuildInfo=D.Make<UVerticalBox>();D.V(BuildInfo,D.LocalText(TEXT("建造 / 放置"),TEXT("Building / placement"),30));D.V(BuildInfo,D.LocalText(TEXT("设施与材料预览区域\n\n局内建造将在后续版本接入。"),TEXT("Facility and material preview\n\nWorld placement will be added later."),22),20,true);auto* Place=D.LocalButton(TEXT("选择位置 · 待接入"),TEXT("Place · Coming later"),-1);Place->SetIsEnabled(false);D.V(BuildInfo,Place);D.H(Building,BuildInfo,2);
 }

 auto* LanguageLayer=D.Make<UOverlay>();HandbookPages->AddChild(LanguageLayer);
 auto* Language=D.Make<UHorizontalBox>();auto* PlaceholderSlot=LanguageLayer->AddChildToOverlay(Language);
 PlaceholderSlot->SetHorizontalAlignment(HAlign_Fill);PlaceholderSlot->SetVerticalAlignment(VAlign_Fill);LanguagePagePlaceholder=Language;
 LanguageJournalHost=D.Make<UNativeWidgetHost>(TEXT("LanguageJournalHost"));LanguageJournalHost->SetVisibility(ESlateVisibility::Collapsed);
 auto* JournalSlot=LanguageLayer->AddChildToOverlay(LanguageJournalHost);JournalSlot->SetHorizontalAlignment(HAlign_Fill);JournalSlot->SetVerticalAlignment(VAlign_Fill);
 // This is a presentation-only unavailable state, never a fabricated journal session.
 auto* Words=D.Make<UVerticalBox>();D.V(Words,D.LocalText(TEXT("收录条目"),TEXT("Journal entries"),22));
 D.V(Words,D.Card(D.LocalText(TEXT("图鉴暂不可用"),TEXT("Journal unavailable"),18),SlotColor,14),8);D.H(Language,Words,1);
 auto* Notes=D.Make<UVerticalBox>();D.V(Notes,D.LocalText(TEXT("语言图鉴"),TEXT("Language journal"),28));
 D.V(Notes,D.Card(D.LocalText(TEXT("当前语言会话尚未就绪，无法显示或编辑记录。"),TEXT("The language session is unavailable. Entries and notes cannot be shown or edited."),20)),12);
 D.V(Notes,D.LocalText(TEXT("可继续使用其他页面，也可重新检查图鉴状态。"),TEXT("You can use the other pages or check the journal again."),18),8);
 D.V(Notes,D.Size(D.LocalButton(TEXT("重试打开图鉴"),TEXT("Retry journal"),70,Paper),240,48),8);D.H(Language,Notes,2);
 HandbookPageIndex=PageSwitcher->GetChildrenCount();PageSwitcher->AddChild(Root);
 RefreshInventory();RefreshHandbookRecipes();
}

bool UDWGameplayWidget::MountLanguageJournal(TSharedRef<SWidget> Content)
{
 if(!LanguageJournalHost||!LanguagePagePlaceholder)return false;
 LanguageJournalHost->SetContent(Content);LanguagePagePlaceholder->SetVisibility(ESlateVisibility::Collapsed);LanguageJournalHost->SetVisibility(ESlateVisibility::Visible);return true;
}
void UDWGameplayWidget::UnmountLanguageJournal()
{
 if(LanguageJournalHost){LanguageJournalHost->SetContent(SNullWidget::NullWidget);LanguageJournalHost->SetVisibility(ESlateVisibility::Collapsed);}
 if(LanguagePagePlaceholder)LanguagePagePlaceholder->SetVisibility(ESlateVisibility::Visible);
}

void UDWGameplayWidget::RefreshHandbookRecipes()
{
 if(!HandbookRecipeList)return;using namespace DWHandbook;Builder D{this,WidgetTree};const auto* C=GetGameplayConfig();const int32 Count=C?C->Recipes.Num():0,Rows=(Count+3)/4;
 bool RebuildRecipes=HandbookRecipeList->GetChildrenCount()!=Rows;
 if(!RebuildRecipes)for(int32 RowIndex=0;RowIndex<Rows;++RowIndex){const auto* Row=Cast<UHorizontalBox>(HandbookRecipeList->GetChildAt(RowIndex));if(!Row||Row->GetChildrenCount()!=FMath::Min(4,Count-RowIndex*4)){RebuildRecipes=true;break;}}
 HandbookRecipeIndex=FMath::Clamp(HandbookRecipeIndex,0,FMath::Max(Count-1,0));
 const double Now=FPlatformTime::Seconds();auto* RecipeInventory=GetInventory();auto* RecipePlayer=GetPlayer();const int64 Revision=RecipeInventory?RecipeInventory->GetInventoryRevision():-1;const int32 Locale=UDWLocalizationLibrary::GetRevision(this);
 if(!RebuildRecipes&&HandbookRecipeLastIndex==HandbookRecipeIndex&&HandbookRecipeLastRevision==Revision&&HandbookRecipeLastLocale==Locale&&HandbookRecipeLastConfig.Get()==C&&HandbookRecipeLastInventory.Get()==RecipeInventory&&HandbookRecipeLastPlayer.Get()==RecipePlayer&&HandbookRecipeLastRefresh>=0.0&&Now>=HandbookRecipeLastRefresh&&Now-HandbookRecipeLastRefresh<.25)return;
 HandbookRecipeLastIndex=HandbookRecipeIndex;HandbookRecipeLastRevision=Revision;HandbookRecipeLastLocale=Locale;HandbookRecipeLastConfig=GetGameplayConfig();HandbookRecipeLastInventory=RecipeInventory;HandbookRecipeLastPlayer=RecipePlayer;HandbookRecipeLastRefresh=Now;
 if(RebuildRecipes){DWC01UI::ReleaseMotionTree(HandbookRecipeList);HandbookRecipeList->ClearChildren();for(int32 RowIndex=0;RowIndex<Rows;++RowIndex){auto* Row=D.Make<UHorizontalBox>();D.V(HandbookRecipeList,Row,4);for(int32 Col=0;Col<4&&RowIndex*4+Col<Count;++Col){
  auto* B=D.Button(FText::GetEmpty(),1000+RowIndex*4+Col,Paper,true);
  FButtonStyle Style=B->GetStyle();Style.Normal=Style.Hovered=Style.Pressed=Style.Disabled=FSlateRoundedBoxBrush(FLinearColor::White,14.f);Style.NormalPadding=Style.PressedPadding=FMargin(0);B->SetStyle(Style);
  auto* Content=D.Make<UVerticalBox>();auto* Icon=D.Make<UImage>();D.V(Content,D.FitImage(Icon,72,72),0);auto* Fallback=D.Text(FText::GetEmpty(),14);Fallback->SetJustification(ETextJustify::Center);D.V(Content,Fallback,0);B->SetContent(Content);D.H(Row,D.Size(B,104,104),0,10);
 }}}
 HandbookRecipeIndex=FMath::Clamp(HandbookRecipeIndex,0,FMath::Max(Count-1,0));
 for(int32 I=0;I<Count;++I)if(auto* Row=Cast<UHorizontalBox>(HandbookRecipeList->GetChildAt(I/4)))if(auto* Box=Cast<USizeBox>(Row->GetChildAt(I%4)))if(auto* B=Cast<UDWHandbookButton>(Box->GetContent())){
  const auto& Recipe=C->Recipes[I];const auto* Def=Recipe.Outputs.Num()?C->GetItemDefinition(Recipe.Outputs[0].ItemId):nullptr;const bool HasIcon=Def&&Def->Icon;
  if(auto* Content=Cast<UVerticalBox>(B->GetContent())){
   if(auto* IconBox=Cast<USizeBox>(Content->GetChildAt(0)))if(auto* Fit=Cast<UScaleBox>(IconBox->GetContent()))if(auto* Icon=Cast<UImage>(Fit->GetContent())){Icon->SetBrushFromTexture(HasIcon?Def->Icon.Get():nullptr,false);IconBox->SetVisibility(HasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
   if(auto* T=Cast<UTextBlock>(Content->GetChildAt(1))){T->SetText(UDWLocalizationLibrary::GetRecipeDisplayName(this,Recipe));T->SetAutoWrapText(true);T->SetVisibility(HasIcon?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);}
  }B->UpdateSelection(I==HandbookRecipeIndex);DWLanguageItemTooltip::AttachRecipe(B,this,Recipe.RecipeId);
 }
 auto* MaterialList=Cast<UVerticalBox>(GetWidgetFromName(TEXT("HandbookRecipeItems")));
 if(!Count){if(MaterialList)MaterialList->ClearChildren();if(LanguageRecipeNeeds)LanguageRecipeNeeds->SetRecipeContext(this,NAME_None);DWLanguageItemTooltip::AttachRecipe(HandbookCraftButton,this,NAME_None);HandbookRecipeDetails->SetText(DWText(this,TEXT("暂无配方"),TEXT("No recipes")));HandbookCraftButton->SetIsEnabled(false);HandbookRecipeImage->SetVisibility(ESlateVisibility::Hidden);return;}
 const auto& R=C->Recipes[HandbookRecipeIndex];if(LanguageRecipeNeeds)LanguageRecipeNeeds->SetRecipeContext(this,R.RecipeId);DWLanguageItemTooltip::AttachRecipe(HandbookCraftButton,this,R.RecipeId);const auto* Bag=GetInventory();const auto* Player=GetPlayer();
 HandbookRecipeDetails->SetText(UDWLocalizationLibrary::GetRecipeDisplayName(this,R));
 if(MaterialList){
  // Presentation only: preserve first-seen order and sum repeated input IDs.
  // A wide total cannot overflow when combining int32 recipe quantities.
  struct FInputTotal { FName Id; int64 Quantity=0; bool bValid=true; };
  TArray<FInputTotal> Inputs;
  for(const FDWItemStack& Input:R.Inputs){
   FInputTotal* Existing=Inputs.FindByPredicate([&Input](const FInputTotal& Total){return Total.Id==Input.ItemId;});
   if(!Existing){Inputs.AddDefaulted();Existing=&Inputs.Last();Existing->Id=Input.ItemId;}
   Existing->bValid=Existing->bValid&&!Input.ItemId.IsNone()&&Input.Quantity>0;
   if(Input.Quantity>0)Existing->Quantity+=int64(Input.Quantity);
  }
  const int32 ItemCount=Inputs.Num()+R.Outputs.Num();
  if(MaterialList->GetChildrenCount()!=ItemCount){MaterialList->ClearChildren();for(int32 I=0;I<ItemCount;++I){auto* Row=D.Make<UHorizontalBox>();auto* Icon=D.Make<UImage>();D.H(Row,D.FitImage(Icon,26,26),0,4);auto* Label=D.Text(FText::GetEmpty(),16);Label->SetAutoWrapText(true);D.H(Row,Label,1,4);D.V(MaterialList,Row,1);}}
  for(int32 I=0;I<ItemCount;++I)if(auto* Row=Cast<UHorizontalBox>(MaterialList->GetChildAt(I))){
   const bool IsInput=I<Inputs.Num();const FName Id=IsInput?Inputs[I].Id:R.Outputs[I-Inputs.Num()].ItemId;
   const int64 Amount=IsInput?Inputs[I].Quantity:int64(R.Outputs[I-Inputs.Num()].Quantity);
   const auto* Def=C->GetItemDefinition(Id);const FText Name=Def?UDWLocalizationLibrary::GetItemDisplayName(this,*Def):FText::FromName(Id);
   if(auto* IconBox=Cast<USizeBox>(Row->GetChildAt(0)))if(auto* Fit=Cast<UScaleBox>(IconBox->GetContent()))if(auto* Icon=Cast<UImage>(Fit->GetContent())){Icon->SetBrushFromTexture(Def?Def->Icon.Get():nullptr,false);Icon->SetVisibility(Def&&Def->Icon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
   if(auto* Text=Cast<UTextBlock>(Row->GetChildAt(1))){
    if(IsInput&&!Inputs[I].bValid)Text->SetText(FText::Format(DWText(this,TEXT("材料 {0} · 配置无效"),TEXT("Ingredient {0} · Invalid configuration")),Name));
    else Text->SetText(IsInput?FText::Format(DWText(this,TEXT("需要 {0}  {1} / {2}"),TEXT("Needs {0}  {1} / {2}")),Name,FText::AsNumber(Bag?Bag->CountItem(Id):0),FText::AsNumber(Amount)):FText::Format(DWText(this,TEXT("产物 {0} × {1}"),TEXT("Makes {0} x {1}")),Name,FText::AsNumber(Amount)));
   }
  }}
 const bool Can=Bag&&Player&&Bag->CanCraft(R.RecipeId,Player->IsYeastForm());HandbookCraftButton->SetIsEnabled(Can);
 if(!Can)HandbookRecipeDetails->SetText(FText::Format(DWText(this,TEXT("{0}\n\n检查材料、空间和制作条件。"),TEXT("{0}\n\nCheck materials, space and crafting requirements.")),UDWLocalizationLibrary::GetRecipeDisplayName(this,R)));
 const auto* Out=R.Outputs.Num()?C->GetItemDefinition(R.Outputs[0].ItemId):nullptr;if(Out&&Out->Icon){HandbookRecipeImage->SetBrushFromTexture(Out->Icon,true);HandbookRecipeImage->SetColorAndOpacity(FLinearColor::White);HandbookRecipeImage->SetVisibility(ESlateVisibility::HitTestInvisible);}else HandbookRecipeImage->SetVisibility(ESlateVisibility::Hidden);
}

void UDWGameplayWidget::RefreshHandbook()
{
 if(!HandbookPages||!ADWGameplayHUD::IsHandbookPage(CurrentPage))return;using namespace DWHandbook;
 int32 Page=0;for(int32 I=0;I<5;++I)if(Pages[I]==CurrentPage)Page=I;HandbookPages->SetActiveWidgetIndex(Page);
 for(int32 I=0;I<HandbookTabButtons.Num()&&I<UE_ARRAY_COUNT(VisiblePageIndices);++I)if(auto* B=HandbookTabButtons[I].Get()){const int32 PageIndex=VisiblePageIndices[I];if(auto* Tab=Cast<UDWHandbookButton>(B))Tab->UpdateSelection(PageIndex==Page);if(auto* T=Cast<UTextBlock>(B->GetContent())){T->SetText(DWText(this,CN[PageIndex],EN[PageIndex]));}}
 auto* C=GetGameplayConfig();auto* P=GetPlayer();auto* Bag=GetInventory();
 {
  const FDWCombatHUDSnapshot S=GetCombatHUDSnapshot();
  const FDWCombatHUDResource* Resources[]={&S.Health,&S.Water,&S.Satiety,&S.Special};
  const FText SpecialTitle=!S.bUsesA01Resources?DWText(this,TEXT("特殊"),TEXT("Special")):
      S.Form==EDWCombatBaseForm::Yeast?DWText(this,TEXT("储气"),TEXT("Gas")):
      S.Form==EDWCombatBaseForm::Mold?DWText(this,TEXT("腐化"),TEXT("Corruption")):DWText(this,TEXT("特殊（普通）"),TEXT("Special (Normal)"));
  const FText Titles[]={DWText(this,TEXT("生命"),TEXT("Health")),DWText(this,TEXT("水分"),TEXT("Water")),
      DWText(this,TEXT("饱腹"),TEXT("Food")),SpecialTitle};
  for(int32 I=0;I<4&&HandbookResourceBars.IsValidIndex(I)&&HandbookResourceLabels.IsValidIndex(I);++I)
  {
   const bool bInactiveSpecial=I==3&&S.bUsesA01Resources&&S.Form==EDWCombatBaseForm::Normal;
   if(HandbookResourceBars[I])
   {
    HandbookResourceBars[I]->SetPercent(S.bReady&&!bInactiveSpecial?float(Resources[I]->Fraction):0.f);
    HandbookResourceBars[I]->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
   }
   if(HandbookResourceLabels[I])
   {
    const FText Value=!S.bReady?DWText(this,TEXT("尚未就绪"),TEXT("Unavailable")):
        bInactiveSpecial?DWText(this,TEXT("未激活"),TEXT("Inactive")):
        FText::FromString(FString::Printf(TEXT("%.1f / %.0f"),Resources[I]->Value,Resources[I]->Maximum));
    const FText Text=FText::Format(FText::FromString(TEXT("{0}\n{1}")),Titles[I],Value);
    HandbookResourceLabels[I]->SetText(Text);HandbookResourceLabels[I]->SetToolTipText(Text);
   }
  }
   if(HandbookFormIcon&&HandbookFormPlaceholder)
   {
    const FName CoreId=S.Form==EDWCombatBaseForm::Yeast?FDWCombatItemRegistry::YeastCoreId():S.Form==EDWCombatBaseForm::Mold?FDWCombatItemRegistry::MoldCoreId():NAME_None;
    const auto* Core=C&&!CoreId.IsNone()?C->GetItemDefinition(CoreId):nullptr;
    const bool bHasIcon=S.bReady&&Core&&Core->Icon;
    if(bHasIcon)HandbookFormIcon->SetBrushFromTexture(Core->Icon.Get(),true);else HandbookFormIcon->SetBrushFromTexture(nullptr);
    HandbookFormIcon->SetVisibility(bHasIcon?ESlateVisibility::Visible:ESlateVisibility::Hidden);
    const bool bMissingFormArt=S.bReady&&S.Form!=EDWCombatBaseForm::Normal&&!bHasIcon;
    HandbookFormPlaceholder->SetVisibility(bMissingFormArt?ESlateVisibility::Visible:ESlateVisibility::Hidden);
    const FText FormTip=S.bReady?DWCombatHUD::FormLabel(this,S.Form):DWText(this,TEXT("状态未就绪"),TEXT("Status unavailable"));
    HandbookFormIcon->SetToolTipText(FormTip);HandbookFormPlaceholder->SetToolTipText(FormTip);
   }
   const FText EquipmentTip=S.bReady?S.EquipmentName:DWText(this,TEXT("状态未就绪"),TEXT("Status unavailable"));
   if(auto* Frame=Cast<UBorder>(WidgetTree->FindWidget(TEXT("HandbookWeaponFrame"))))Frame->SetToolTipText(EquipmentTip);
   if(HandbookEquippedIcon)
   {
    const auto* Equipped=S.bReady&&C?C->GetItemDefinition(FDWCombatItemRegistry::PreferredInventoryItemId(S.EquipmentId)):nullptr;
    if(!Equipped&&S.bReady&&C)Equipped=C->GetItemDefinition(S.EquipmentId);
    if(Equipped&&Equipped->Icon){HandbookEquippedIcon->SetBrushFromTexture(Equipped->Icon.Get(),true);HandbookEquippedIcon->SetVisibility(ESlateVisibility::Visible);}
    else {HandbookEquippedIcon->SetBrushFromTexture(nullptr);HandbookEquippedIcon->SetVisibility(ESlateVisibility::Hidden);}
    HandbookEquippedIcon->SetToolTipText(EquipmentTip);
   }
 }
 auto* Weapon=P?P->WeaponAnimationComponent.Get():nullptr;
 if(HandbookEquipmentList)for(int32 I=0;I<UE_ARRAY_COUNT(Weapons);++I)if(auto* Row=Cast<UHorizontalBox>(HandbookEquipmentList->GetChildAt(I/EquipmentColumns)))if(auto* Box=Cast<USizeBox>(Row->GetChildAt(I%EquipmentColumns)))if(auto* B=Cast<UDWHandbookButton>(Box->GetContent()))
 {
  const auto* Binding=FDWCombatItemRegistry::FindPresentation(Weapons[I]);const auto* Definition=C&&Binding?C->GetItemDefinition(FDWCombatItemRegistry::PreferredInventoryItemId(Binding->ItemId)):nullptr;
  if(!Definition&&C&&Binding)Definition=C->GetItemDefinition(Binding->ItemId);
  if(auto* T=Cast<UTextBlock>(B->GetContent())){T->SetText(Weapons[I]==EDWAnimatedWeapon::Unarmed?DWText(this,TEXT("空手"),TEXT("Unarmed")):(Definition?UDWLocalizationLibrary::GetItemDisplayName(this,*Definition):FText::FromName(Binding?Binding->ItemId:NAME_None)));T->SetAutoWrapText(false);}
  const bool Can=Weapon&&P&&!P->IsDead()&&P->HasCombatVitalsReady()&&DWCombatEquipment::CanSelect(Weapon->GetCombatWeaponStateSnapshot(),Weapons[I],Bag?Bag->CountItem(TEXT("Alcohol")):0);B->SetIsEnabled(Can);B->UpdateSelection(Weapon&&Weapon->SelectedWeapon==Weapons[I]);
 }

 const FDWItemStack Item=Bag&&Bag->GetSlots().IsValidIndex(SelectedSlot)?Bag->GetSlots()[SelectedSlot]:FDWItemStack();const auto* Def=C?C->GetItemDefinition(Item.ItemId):nullptr;
 HandbookSelection->SetText(Def?FText::Format(FText::FromString(TEXT("{0} x {1}")),UDWLocalizationLibrary::GetItemDisplayName(this,*Def),Item.Quantity):DWText(this,TEXT("选择一个物品"),TEXT("Select an item")));
 if(Def&&Def->Icon){HandbookItemImage->SetBrushFromTexture(Def->Icon,true);HandbookItemImage->SetColorAndOpacity(FLinearColor::White);HandbookItemImage->SetVisibility(ESlateVisibility::HitTestInvisible);}else{HandbookItemImage->SetBrushFromTexture(nullptr);HandbookItemImage->SetVisibility(ESlateVisibility::Hidden);}
 const int64 Rev=Bag?Bag->GetInventoryRevision():0;const int32 Lang=UDWLocalizationLibrary::GetRevision(this);
 if(Lang!=HandbookLanguageRevision)for(const auto& Label:HandbookLabels)if(Label.Key.IsValid())Label.Key->SetText(DWText(this,*Label.Value.Key,*Label.Value.Value));
 const auto* BookLanguage=GetGameInstance()?GetGameInstance()->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 const int64 Progress=BookLanguage&&BookLanguage->IsReady()?BookLanguage->GetRevision():-1;
 if(Rev!=HandbookInventoryRevision||Lang!=HandbookLanguageRevision||Progress!=LanguageBookProgressRevision)
 {HandbookInventoryRevision=Rev;HandbookLanguageRevision=Lang;LanguageBookProgressRevision=Progress;RefreshInventory();}
 bool Related=false,More=false;if(C&&!Item.ItemId.IsNone())for(const auto& R:C->Recipes){for(const auto& Input:R.Inputs)if(Input.ItemId==Item.ItemId)Related=true;for(const auto& Output:R.Outputs)if(Output.ItemId==Item.ItemId){Related=true;More=true;}}
 if(HandbookRelatedRecipeButton)HandbookRelatedRecipeButton->SetIsEnabled(Related);if(HandbookCraftMoreButton)HandbookCraftMoreButton->SetIsEnabled(More);
 if(CurrentPage==EDWMenuPage::Crafting)RefreshHandbookRecipes();
}

void UDWGameplayWidget::HandleHandbookAction(int32 Action)
{
 if(Action>=300&&Action<304){ActivateQuickInventorySlot(Action-300);return;}
 if(Action==27){if(CurrentPage==EDWMenuPage::Inventory&&!IsInventoryDiscardPending()&&!EquipInventorySlot(SelectedSlot))SetToast(DWText(this,TEXT("当前无法装备该物品"),TEXT("This item cannot be equipped now")).ToString());return;}
 const bool QuickWeapon=false; // Inventory shortcuts use exact slot actions 300..303; 200+ remains owned-equipment selection.
 if(!HUD||(!ADWGameplayHUD::IsHandbookPage(CurrentPage)&&!QuickWeapon)||IsInventoryDiscardPending())return;
 if(Action==70)
 {
  if(CurrentPage!=EDWMenuPage::LanguageJournal||!HUD||HUD->GetGameplayWidget()!=this)return;
  auto* L=GetGameInstance()?GetGameInstance()->GetSubsystem<UDWLanguageSubsystem>():nullptr;
  auto* Bridge=UDWLanguageUIBridgeComponent::Find(GetOwningPlayer());
  const bool bReady=L&&L->IsReady(),bBridgeAvailable=IsValid(Bridge);
  const TWeakObjectPtr<UDWGameplayWidget> Self(this);
  if(bReady&&bBridgeAvailable)Bridge->RequestJournal();
  if(!Self.IsValid())return;
  if(Self->CurrentPage==EDWMenuPage::LanguageJournal&&Self->LanguageJournalHost&&Self->LanguageJournalHost->GetVisibility()!=ESlateVisibility::Visible)
  {UE_LOG(LogTemp,Warning,TEXT("Journal retry did not mount: ready=%d bridge=%d"),bReady,bBridgeAvailable);
   Self->SetToast(DWText(Self.Get(),TEXT("语言图鉴暂不可用，请稍后重试"),TEXT("Journal unavailable. Please retry later.")).ToString());}
  Self->PlayClick();return;
 }
 if(Action==40)
 {
  const TWeakObjectPtr<UDWGameplayWidget> Self(this);
  auto* Bridge=UDWLanguageUIBridgeComponent::Find(GetOwningPlayer());
  const bool bOpened=Bridge&&Bridge->RequestTalents();
  if(!Self.IsValid())return;
  if(!bOpened)SetToast(DWText(this,TEXT("天赋暂不可用，请稍后重试。"),TEXT("Talents unavailable. Please retry later.")).ToString());
  PlayClick();return;
 }
 if(Action>=50&&Action<53&&DWLanguageItemTooltip::IsEnabled(this))
 {
  const FName Ids[]={TEXT("CraftingTable"),TEXT("Furnace"),TEXT("FermentationVat")};const FName Wanted=Ids[Action-50];const auto* Bag=GetInventory();const auto* Config=GetGameplayConfig();
  if(Bag)for(int32 I=0;I<Bag->GetSlots().Num();++I)if(Bag->GetSlots()[I].ItemId==Wanted){SelectedSlot=I;HUD->ShowMenu(EDWMenuPage::Inventory);RefreshFromHUD(true);PlayClick();return;}
  if(Config)for(int32 I=0;I<Config->Recipes.Num();++I)if(Config->Recipes[I].Outputs.ContainsByPredicate([Wanted](const FDWItemStack& S){return S.ItemId==Wanted;})){HandbookRecipeIndex=I;HUD->ShowMenu(EDWMenuPage::Crafting);RefreshHandbookRecipes();PlayClick();return;}
  SetToast(DWText(this,TEXT("当前配置没有该设施配方；试玩场景中可使用已放置的工位。"),TEXT("No recipe is configured for this facility; use an existing station in the trial scene.")).ToString());return;
 }
 if(Action==41||Action==42)
 {if(CurrentPage==EDWMenuPage::Inventory&&LanguageBagItemsArea&&LanguageBagEquipmentArea)
  {LanguageBagItemsArea->SetVisibility(Action==41?ESlateVisibility::Visible:ESlateVisibility::Collapsed);LanguageBagEquipmentArea->SetVisibility(Action==42?ESlateVisibility::Visible:ESlateVisibility::Collapsed);PlayClick();}return;}
 if(Action>=0&&Action<5){HUD->ShowMenu(DWHandbook::Pages[Action]);PlayClick();return;}
 if(Action==10){ClickClose();return;}if(Action==11||Action==12){HUD->CycleHandbook(Action==11?-1:1);PlayClick();return;}
 if(Action==21){ClickUse();return;}
 if(Action==22){auto* Bag=GetInventory();if(Bag&&Bag->GetSlots().IsValidIndex(SelectedSlot)){int32 NewSlot;const int32 Amount=Bag->GetSlots()[SelectedSlot].Quantity/2;if(Amount>0&&Bag->TrySplitSlot(SelectedSlot,Amount,NewSlot)){SelectInventorySlot(NewSlot);RefreshFromHUD(true);}else SetToast(DWText(this,TEXT("空间或数量不足"),TEXT("Not enough space or items")).ToString());}return;}
 if(Action==23){RequestDiscardSelectedInventory();return;}
 if(Action==24){if(auto* Bag=GetInventory()){Bag->TryQuickStack(NAME_None);RefreshFromHUD(true);}return;}
 if(Action==30){const auto* C=GetGameplayConfig();if(C&&C->Recipes.IsValidIndex(HandbookRecipeIndex))CraftRecipe(C->Recipes[HandbookRecipeIndex].RecipeId);return;}
 if(Action==25||Action==26)
 {
  const auto* C=GetGameplayConfig();const auto* Bag=GetInventory();if(!C||!Bag||!Bag->GetSlots().IsValidIndex(SelectedSlot))return;const FName Id=Bag->GetSlots()[SelectedSlot].ItemId;int32 Found=INDEX_NONE;
  for(int32 I=0;I<C->Recipes.Num()&&Found==INDEX_NONE;++I){for(const auto& Out:C->Recipes[I].Outputs)if(Out.ItemId==Id)Found=I;if(Action==25&&Found==INDEX_NONE)for(const auto& Input:C->Recipes[I].Inputs)if(Input.ItemId==Id)Found=I;}
  if(Found!=INDEX_NONE){HandbookRecipeIndex=Found;HUD->ShowMenu(EDWMenuPage::Crafting);PlayClick();}return;
 }
 if(Action>=200&&Action<200+UE_ARRAY_COUNT(DWHandbook::Weapons))
 {
  const EDWAnimatedWeapon Requested=DWHandbook::Weapons[Action-200];const TWeakObjectPtr<ADWPlayerCharacter> P(GetPlayer());const TWeakObjectPtr<UDWWeaponAnimationComponent> W(P.IsValid()?P->WeaponAnimationComponent.Get():nullptr);auto* A=UDWCombatAuthoritySubsystem::Get(P.Get());
  if(!A||!P.IsValid()||!W.IsValid())return;const auto Life=A->CaptureActorLife(P.Get());const uint64 Epoch=A->GetWorldEpoch(),Restore=W->GetCombatWeaponRestoreEpoch();const int64 Revision=W->GetCombatWeaponResourceRevision();
  const TWeakObjectPtr<UDWInventoryComponent> OriginalInventory(P->GetInventory());if(!DWCombatEquipment::CanSelect(W->GetCombatWeaponStateSnapshot(),Requested,OriginalInventory.IsValid()?OriginalInventory->CountItem(TEXT("Alcohol")):0))return;
  // Menus pause authoritative action admission. Close once, then revalidate through SelectWeapon.
  if(HUD->GetMenuPage()!=EDWMenuPage::None)HUD->ClosePanels();A=UDWCombatAuthoritySubsystem::Get(P.Get());bool Done=false;
  if(A&&P.IsValid()&&W.IsValid()&&GetPlayer()==P.Get()&&P->WeaponAnimationComponent.Get()==W.Get()&&P->GetInventory()==OriginalInventory.Get()&&A->GetWorldEpoch()==Epoch&&W->GetCombatWeaponRestoreEpoch()==Restore&&W->GetCombatWeaponResourceRevision()==Revision){const auto Current=A->CaptureActorLife(P.Get());if(Current.Entity==Life.Entity&&Current.Life==Life.Life)Done=W->SelectWeapon(Requested);}
  if(!Done)SetToast(DWText(this,TEXT("当前无法换装"),TEXT("Equipment cannot change now")).ToString());return;
 }

 if(Action>=1000){const auto* C=GetGameplayConfig();if(C&&C->Recipes.IsValidIndex(Action-1000)){HandbookRecipeIndex=Action-1000;RefreshHandbookRecipes();PlayClick();}}
}


bool UDWGameplayWidget::CanEquipInventorySlot(int32 Index)const
{
 const auto* GI=GetDWGameInstance();if(!GI||GI->IsPlayerRestorePending())return false;
 if(const auto* Transition=GI->GetSubsystem<UDWLoadingTransitionSubsystem>();Transition&&Transition->IsTransitionActive())return false;
 const auto* P=GetPlayer();const auto* Bag=GetInventory();const auto* W=P?P->WeaponAnimationComponent.Get():nullptr;
 if(!P||P->IsDead()||!P->HasCombatVitalsReady()||!Bag||!Bag->GetSlots().IsValidIndex(Index)||!W)return false;
 const FName Id=Bag->GetSlots()[Index].ItemId;EDWAnimatedWeapon Kind;
 if(!DWCombatEquipment::ToPresentation(Id,Kind)||Kind==EDWAnimatedWeapon::Unarmed)return false;
 if(HUD&&HUD->GetMenuPage()==EDWMenuPage::Inventory)
 {FDWInventorySlotSnapshot CapturedItemSlot;return Bag->CaptureSlotSnapshot(Index,CapturedItemSlot)&&W->CanEquipInventoryItem(CapturedItemSlot);}
 return DWCombatEquipment::CanSelect(W->GetCombatWeaponStateSnapshot(),Kind,Bag->CountItem(TEXT("Alcohol")));
}
bool UDWGameplayWidget::EquipInventorySlot(int32 Index)
{
 if(!HUD||!HUD->IsSessionStarted()||(HUD->GetMenuPage()!=EDWMenuPage::Inventory&&HUD->GetMenuPage()!=EDWMenuPage::None)||
    IsInventoryDiscardPending()||UDWWorldEventSubsystem::IsPlaying(this)||(HUD->GetMenuPage()!=EDWMenuPage::Inventory&&!GetC01CinematicOffset().IsNearlyZero(.5))||!CanEquipInventorySlot(Index))return false;
 const TWeakObjectPtr<ADWPlayerCharacter> P(GetPlayer());
 const TWeakObjectPtr<UDWWeaponAnimationComponent> W(P.IsValid()?P->WeaponAnimationComponent.Get():nullptr);
 const TWeakObjectPtr<UDWInventoryComponent> Bag(GetInventory());FDWInventorySlotSnapshot CapturedSlot;
 auto* A=UDWCombatAuthoritySubsystem::Get(P.Get());if(!A||!P.IsValid()||!W.IsValid()||!Bag.IsValid()||!Bag->CaptureSlotSnapshot(Index,CapturedSlot))return false;
 EDWAnimatedWeapon Kind;if(!DWCombatEquipment::ToPresentation(CapturedSlot.ItemId,Kind))return false;
 if(HUD->GetMenuPage()==EDWMenuPage::Inventory)return W->EquipInventoryItem(CapturedSlot);
 const auto Life=A->CaptureActorLife(P.Get());const uint64 Epoch=A->GetWorldEpoch(),Restore=W->GetCombatWeaponRestoreEpoch();const int64 Revision=W->GetCombatWeaponResourceRevision();
 if(HUD->GetMenuPage()!=EDWMenuPage::None)HUD->ClosePanels();
 A=UDWCombatAuthoritySubsystem::Get(P.Get());
 if(!A||!P.IsValid()||!W.IsValid()||!Bag.IsValid()||GetPlayer()!=P.Get()||P->GetInventory()!=Bag.Get()||P->WeaponAnimationComponent.Get()!=W.Get()||
    A->GetWorldEpoch()!=Epoch||W->GetCombatWeaponRestoreEpoch()!=Restore||W->GetCombatWeaponResourceRevision()!=Revision||!Bag->IsSlotSnapshotCurrent(CapturedSlot))return false;
 const auto Current=A->CaptureActorLife(P.Get());
 return Current.Entity==Life.Entity&&Current.Life==Life.Life&&W->SelectWeapon(Kind);
}
int32 UDWGameplayWidget::GetUnlockedQuickInventorySlotCount()const
{
 const auto* P=GetPlayer();const auto* A=P?P->FindComponentByClass<UDWCharacterA01Component>():nullptr;
 const auto* GI=GetGameInstance();const auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 return A&&A->IsEnabled()&&L&&L->IsReady()&&L->HasTalent(TEXT("a1"))?4:3;
}
bool UDWGameplayWidget::CanActivateQuickInventorySlot(int32 Index)const
{
 const auto* GI=GetDWGameInstance();if(!GI||GI->IsPlayerRestorePending())return false;
 const auto* P=GetPlayer();const auto* Bag=GetInventory();
 return Index>=0&&Index<GetUnlockedQuickInventorySlotCount()&&P&&P->CanUseMainHandShortcut(Index)&&bUseC01HUD&&HUD&&HUD->IsSessionStarted()&&HUD->GetMenuPage()==EDWMenuPage::None&&
  !HUD->IsBlockingGameplay()&&!IsInventoryDiscardPending()&&!UDWWorldEventSubsystem::IsPlaying(this)&&GetC01CinematicOffset().IsNearlyZero(.5)&&
  P&&!P->IsDead()&&P->HasCombatVitalsReady()&&Bag&&Bag->GetSlots().IsValidIndex(Index)&&Bag->GetSlots()[Index].Quantity>0;
}
void UDWGameplayWidget::ActivateQuickInventorySlot(int32 Index)
{
 if(!CanActivateQuickInventorySlot(Index))return;
 const TWeakObjectPtr<ADWPlayerCharacter> P(GetPlayer());const TWeakObjectPtr<UDWInventoryComponent> Bag(GetInventory());
 FDWInventorySlotSnapshot CapturedSlot;if(!P.IsValid()||!Bag.IsValid()||!Bag->CaptureSlotSnapshot(Index,CapturedSlot))return;
 const auto Role=FDWCombatItemRegistry::GetItemRole(CapturedSlot.ItemId);
 if(Role==EDWCombatItemRole::PermanentWeapon||Role==EDWCombatItemRole::ThrowableStock)
 {const bool Done=EquipInventorySlot(Index);if(Done){if(auto* A=P->FindComponentByClass<UDWCharacterA01Component>())A->NotifyShortcutUse(Index);}
  else SetToast(DWText(this,TEXT("当前无法装备该物品"),TEXT("This item cannot be equipped now")).ToString());return;}
 const auto* Config=GetGameplayConfig();const auto* Def=Config?Config->GetItemDefinition(CapturedSlot.ItemId):nullptr;
 const bool Core=Role==EDWCombatItemRole::MoldCore||Role==EDWCombatItemRole::YeastCore;
 if(Core||(Def&&Def->bUsable))
 {
  if(!Bag->IsSlotSnapshotCurrent(CapturedSlot)||GetPlayer()!=P.Get()||P->GetInventory()!=Bag.Get())return;
  const bool Done=Core?P->RequestCoreInjection(CapturedSlot):P->UseInventoryItem(Index);
  if(Done&&P.IsValid()){if(auto* A=P->FindComponentByClass<UDWCharacterA01Component>())A->NotifyShortcutUse(Index);}
  if(!Done)SetToast(DWText(this,TEXT("当前无法使用该物品"),TEXT("This item cannot be used now")).ToString());
  RefreshFromHUD(true);OnItemUseResult(CapturedSlot.ItemId,Done);return;
 }
 // A crafting material has no invented use effect: open its actual bag slot instead.
 HUD->ShowMenu(EDWMenuPage::Inventory);
 if(P.IsValid()&&Bag.IsValid()&&GetPlayer()==P.Get()&&P->GetInventory()==Bag.Get()&&Bag->IsSlotSnapshotCurrent(CapturedSlot))SelectInventorySlot(Index);
}
