#include "DWGameplayWidget.h"
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
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
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
 if(auto* T=Cast<UTextBlock>(GetContent()))T->SetColorAndOpacity(!bSelected&&C01BaseColor.Equals(DWC01UI::Danger(),.0001f)?FLinearColor::White:DWHandbook::Ink);
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
 for(int32 I=0;I<5;++I){auto* B=D.Button(DWText(this,CN[I],EN[I]),I);B->bBookTab=true;B->bC01TransparentRest=true;D.H(Tabs,D.Size(B,152,60),0,0)->SetPadding(FMargin(0,0,12,0));HandbookTabButtons.Add(B);}
 auto* Close=D.Make<UDWHandbookButton>(TEXT("HandbookClose"));Close->Configure(this,10);Close->bC01TransparentRest=true;Close->C01BaseColor=FLinearColor::Transparent;StyleC01Button(Close,FLinearColor::Transparent,false,true);
 auto* Cross=D.Art(TEXT("T_DW_C04_CloseBrush"),FVector2D(64,64));Cross->SetColorAndOpacity(Ink);Close->SetContent(D.FitImage(Cross,64,64));Close->SetToolTipText(DWText(this,TEXT("关闭"),TEXT("Close")));D.At(Whole,Close,1104,121,72,64);
 HandbookPages=D.Make<UWidgetSwitcher>(TEXT("HandbookPages"));D.At(Whole,HandbookPages,108,207,1064,470);

 auto* Character=D.Make<UCanvasPanel>();HandbookPages->AddChild(Character);
 // Wider single-line equipment buttons keep long English names at the shared font size.
 auto* OwnedScroll=D.Make<UScrollBox>();HandbookEquipmentList=D.Make<UVerticalBox>(TEXT("HandbookEquipmentList"));OwnedScroll->AddChild(HandbookEquipmentList);D.At(Character,OwnedScroll,0,0,570,366);
 for(int32 R=0;R<(UE_ARRAY_COUNT(Weapons)+EquipmentColumns-1)/EquipmentColumns;++R){auto* Row=D.Make<UHorizontalBox>();D.V(HandbookEquipmentList,Row,4);for(int32 Col=0;Col<EquipmentColumns&&R*EquipmentColumns+Col<UE_ARRAY_COUNT(Weapons);++Col)D.H(Row,D.Size(D.Button(FText::GetEmpty(),200+R*EquipmentColumns+Col),270,64),0,6);}
 HandbookStats=D.Text(FText::GetEmpty(),18);auto* StatsScroll=D.Make<UScrollBox>();StatsScroll->AddChild(HandbookStats);D.At(Character,StatsScroll,594,0,232,420);
 D.At(Character,D.Art(TEXT("T_DW_C01_CharacterPreview_Artwork_reservation_e9924678"),FVector2D(190,270)),866,6,190,270);D.At(Character,D.LocalText(TEXT("角色展示"),TEXT("Character preview"),16),866,290,190,40);
 D.At(Character,D.LocalText(TEXT("选择已拥有的装备"),TEXT("Select owned equipment"),16),0,376,570,42);

 auto* Inventory=D.Make<UCanvasPanel>();HandbookPages->AddChild(Inventory);
 InventoryGrid=D.Make<UUniformGridPanel>(TEXT("HandbookInventoryGrid"));InventoryGrid->SetSlotPadding(FMargin(4));InventoryGrid->SetMinDesiredSlotWidth(96);InventoryGrid->SetMinDesiredSlotHeight(96);InventoryColumns=6;
 auto* InvScroll=D.Make<UScrollBox>();InvScroll->SetAnimateWheelScrolling(true);InvScroll->AddChild(InventoryGrid);D.At(Inventory,InvScroll,-4,-4,628,400);
 InventoryCapacityText=D.Text(FText::GetEmpty(),14);D.At(Inventory,InventoryCapacityText,0,410,420,30);
 D.At(Inventory,D.LocalButton(TEXT("拆分"),TEXT("Split"),22),432,406,88,44);D.At(Inventory,D.LocalButton(TEXT("合并"),TEXT("Merge"),24),532,406,88,44);
 HandbookItemImage=D.Make<UImage>();D.At(Inventory,D.Card(D.FitImage(HandbookItemImage,160,80),SlotColor,12),656,0,408,104);
 HandbookSelection=D.Text(FText::GetEmpty(),24);D.At(Inventory,HandbookSelection,656,118,408,56);
 InventoryDetailsText=D.Text(FText::GetEmpty(),17);auto* DescriptionScroll=D.Make<UScrollBox>();DescriptionScroll->AddChild(InventoryDetailsText);D.At(Inventory,DescriptionScroll,656,182,408,64);
 HandbookRelatedRecipeButton=D.LocalButton(TEXT("相关配方"),TEXT("Related recipe"),25);D.At(Inventory,HandbookRelatedRecipeButton,656,262,408,56);
 InventoryUseButton=D.LocalButton(TEXT("使用"),TEXT("Use"),21);D.At(Inventory,InventoryUseButton,656,334,196,48);D.At(Inventory,D.LocalButton(TEXT("丢弃"),TEXT("Discard"),23,Red),868,334,196,48);
 auto* Equip=D.LocalButton(TEXT("装备"),TEXT("Equip"),27,Paper,false,TEXT("HandbookInventoryEquip"));Equip->SetIsEnabled(false);D.At(Inventory,Equip,656,398,196,48);
 HandbookCraftMoreButton=D.LocalButton(TEXT("制作更多"),TEXT("Craft more"),26,Gold);D.At(Inventory,HandbookCraftMoreButton,868,398,196,48);
 D.At(Inventory,D.LocalText(TEXT("前三格为快捷栏 · 拖拽可调整"),TEXT("First three slots are shortcuts · drag to arrange"),13),0,444,624,26);

 auto* Craft=D.Make<UCanvasPanel>();HandbookPages->AddChild(Craft);auto* Categories=D.Make<UHorizontalBox>();D.At(Craft,Categories,0,0,558,44);
 const TCHAR* CatCN[]={TEXT("全部"),TEXT("武器"),TEXT("护甲"),TEXT("消耗品")};const TCHAR* CatEN[]={TEXT("All"),TEXT("Weapons"),TEXT("Armour"),TEXT("Consumables")};
 // Preserve the first three hit areas; the longer English category needs its own width.
 for(int32 I=0;I<4;++I){auto* B=D.LocalButton(CatCN[I],CatEN[I],-1);B->SetIsEnabled(I==0);D.H(Categories,D.Size(B,I==3?164.f:118.f,42),0,5);}
 auto* RecipeScroll=D.Make<UScrollBox>();HandbookRecipeList=D.Make<UVerticalBox>(TEXT("HandbookRecipeList"));RecipeScroll->AddChild(HandbookRecipeList);D.At(Craft,RecipeScroll,0,68,520,362);
 HandbookRecipeImage=D.Make<UImage>();D.At(Craft,D.Card(D.FitImage(HandbookRecipeImage,180,102),SlotColor,12),568,0,496,126);
 HandbookRecipeDetails=D.Text(FText::GetEmpty(),18);auto* MaterialScroll=D.Make<UScrollBox>();MaterialScroll->AddChild(HandbookRecipeDetails);D.At(Craft,MaterialScroll,580,140,472,256);
 auto* Quantity=D.LocalText(TEXT("1 份"),TEXT("1 craft"),18);Quantity->SetJustification(ETextJustify::Center);Quantity->SetAutoWrapText(false);auto* QuantityCard=D.Card(Quantity,SlotColor,0);QuantityCard->SetPadding(FMargin(8,0));QuantityCard->SetVerticalAlignment(VAlign_Center);D.At(Craft,QuantityCard,568,422,258,48);
 HandbookCraftButton=D.LocalButton(TEXT("制作"),TEXT("Craft"),30,Gold);D.At(Craft,HandbookCraftButton,880,422,184,48);

 auto* Building=D.Make<UHorizontalBox>();HandbookPages->AddChild(Building);auto* Facilities=D.Make<UVerticalBox>();
 const TCHAR* BuildCN[]={TEXT("篝火"),TEXT("工作台"),TEXT("围栏")};const TCHAR* BuildEN[]={TEXT("Campfire"),TEXT("Workbench"),TEXT("Fence")};
 for(int32 I=0;I<3;++I){auto* B=D.LocalButton(BuildCN[I],BuildEN[I],-1,SlotColor,true);B->SetIsEnabled(false);D.V(Facilities,D.Size(B,0,90),10);}D.H(Building,Facilities,1);
 auto* BuildInfo=D.Make<UVerticalBox>();D.V(BuildInfo,D.LocalText(TEXT("建造 / 放置"),TEXT("Building / placement"),30));D.V(BuildInfo,D.LocalText(TEXT("设施与材料预览区域\n\n局内建造将在后续版本接入。"),TEXT("Facility and material preview\n\nWorld placement will be added later."),22),20,true);auto* Place=D.LocalButton(TEXT("选择位置 · 待接入"),TEXT("Place · Coming later"),-1);Place->SetIsEnabled(false);D.V(BuildInfo,Place);D.H(Building,BuildInfo,2);

 auto* Language=D.Make<UHorizontalBox>();HandbookPages->AddChild(Language);auto* Words=D.Make<UVerticalBox>();D.V(Words,D.LocalText(TEXT("收录条目"),TEXT("Journal entries"),22));for(int32 I=0;I<5;++I)D.V(Words,D.Card(D.LocalText(TEXT("?   未发现"),TEXT("?   Undiscovered"),20),SlotColor,14),6);D.H(Language,Words,1);
 auto* Notes=D.Make<UVerticalBox>();D.V(Notes,D.LocalText(TEXT("语言图鉴"),TEXT("Language journal"),30));D.V(Notes,D.Card(D.LocalText(TEXT("?   字形与相关组合"),TEXT("?   Glyphs and related words"),25)),20);
 D.V(Notes,D.LocalText(TEXT("猜想与现场线索将在后续版本接入。\n当前页面仅作界面占位。"),TEXT("Guesses and encountered clues will be added later.\nThis page is a UI placeholder."),22),12,true);D.H(Language,Notes,1.6f);
 D.H(Language,D.Card(D.LocalText(TEXT("现场图片"),TEXT("Scene image"),24)),1);
 HandbookPageIndex=PageSwitcher->GetChildrenCount();PageSwitcher->AddChild(Root);
 RefreshInventory();RefreshHandbookRecipes();
}

void UDWGameplayWidget::RefreshHandbookRecipes()
{
 if(!HandbookRecipeList)return;using namespace DWHandbook;Builder D{this,WidgetTree};const auto* C=GetGameplayConfig();const int32 Count=C?C->Recipes.Num():0,Rows=(Count+3)/4;
 if(HandbookRecipeList->GetChildrenCount()!=Rows){DWC01UI::ReleaseMotionTree(HandbookRecipeList);HandbookRecipeList->ClearChildren();for(int32 R=0;R<Rows;++R){auto* Row=D.Make<UHorizontalBox>();D.V(HandbookRecipeList,Row,4);for(int32 Col=0;Col<4&&R*4+Col<Count;++Col)D.H(Row,D.Size(D.Button(FText::GetEmpty(),1000+R*4+Col),104,104),0,10);}}
 HandbookRecipeIndex=FMath::Clamp(HandbookRecipeIndex,0,FMath::Max(Count-1,0));
 for(int32 I=0;I<Count;++I)if(auto* Row=Cast<UHorizontalBox>(HandbookRecipeList->GetChildAt(I/4)))if(auto* Box=Cast<USizeBox>(Row->GetChildAt(I%4)))if(auto* B=Cast<UDWHandbookButton>(Box->GetContent())){if(auto* T=Cast<UTextBlock>(B->GetContent())){T->SetText(UDWLocalizationLibrary::GetRecipeDisplayName(this,C->Recipes[I]));T->SetAutoWrapText(true);}B->UpdateSelection(I==HandbookRecipeIndex);}
 if(!Count){HandbookRecipeDetails->SetText(DWText(this,TEXT("暂无配方"),TEXT("No recipes")));HandbookCraftButton->SetIsEnabled(false);HandbookRecipeImage->SetVisibility(ESlateVisibility::Hidden);return;}
 const auto& R=C->Recipes[HandbookRecipeIndex];const auto* Bag=GetInventory();const auto* Player=GetPlayer();FString Text=UDWLocalizationLibrary::GetRecipeDisplayName(this,R).ToString()+TEXT("\n\n");
 for(const auto& Item:R.Inputs){const auto* Def=C->GetItemDefinition(Item.ItemId);Text+=FString::Printf(TEXT("%s    %d / %d\n"),*(Def?UDWLocalizationLibrary::GetItemDisplayName(this,*Def).ToString():Item.ItemId.ToString()),Bag?Bag->CountItem(Item.ItemId):0,Item.Quantity);}
 Text+=DWText(this,TEXT("\n产物\n"),TEXT("\nOutput\n")).ToString();for(const auto& Item:R.Outputs){const auto* Def=C->GetItemDefinition(Item.ItemId);Text+=FString::Printf(TEXT("%s x %d\n"),*(Def?UDWLocalizationLibrary::GetItemDisplayName(this,*Def).ToString():Item.ItemId.ToString()),Item.Quantity);}
 const bool Can=Bag&&Player&&Bag->CanCraft(R.RecipeId,Player->IsYeastForm());if(!Can)Text+=DWText(this,TEXT("\n检查材料、空间和形态。"),TEXT("\nCheck ingredients, space and form.")).ToString();HandbookRecipeDetails->SetText(FText::FromString(Text));HandbookCraftButton->SetIsEnabled(Can);
 const auto* Out=R.Outputs.Num()?C->GetItemDefinition(R.Outputs[0].ItemId):nullptr;if(Out&&Out->Icon){HandbookRecipeImage->SetBrushFromTexture(Out->Icon,true);HandbookRecipeImage->SetColorAndOpacity(FLinearColor::White);HandbookRecipeImage->SetVisibility(ESlateVisibility::HitTestInvisible);}else HandbookRecipeImage->SetVisibility(ESlateVisibility::Hidden);
}

void UDWGameplayWidget::RefreshHandbook()
{
 if(!HandbookPages||!ADWGameplayHUD::IsHandbookPage(CurrentPage))return;using namespace DWHandbook;
 int32 Page=0;for(int32 I=0;I<5;++I)if(Pages[I]==CurrentPage)Page=I;HandbookPages->SetActiveWidgetIndex(Page);
 for(int32 I=0;I<HandbookTabButtons.Num();++I)if(auto* B=HandbookTabButtons[I].Get()){if(auto* Tab=Cast<UDWHandbookButton>(B))Tab->UpdateSelection(I==Page);if(auto* T=Cast<UTextBlock>(B->GetContent())){T->SetText(DWText(this,CN[I],EN[I]));}}
 auto* C=GetGameplayConfig();auto* P=GetPlayer();auto* Bag=GetInventory();
 if(HandbookStats){const FDWCombatHUDSnapshot S=GetCombatHUDSnapshot();if(!S.bReady)HandbookStats->SetText(DWText(this,TEXT("状态未就绪"),TEXT("Status unavailable")));else{FString Stats=FText::Format(DWText(this,TEXT("生命  {0} / {1}\n水分  {2} / {3}\n饱腹  {4} / {5}\n特殊  {6} / {7}"),TEXT("Health  {0} / {1}\nWater  {2} / {3}\nFood  {4} / {5}\nSpecial  {6} / {7}")),FMath::RoundToInt(S.Health.Value),FMath::RoundToInt(S.Health.Maximum),FMath::RoundToInt(S.Water.Value),FMath::RoundToInt(S.Water.Maximum),FMath::RoundToInt(S.Satiety.Value),FMath::RoundToInt(S.Satiety.Maximum),FMath::RoundToInt(S.Special.Value),FMath::RoundToInt(S.Special.Maximum)).ToString();Stats+=TEXT("\n\n")+DWCombatHUD::FormLabel(this,S.Form).ToString();if(S.Form!=EDWCombatBaseForm::Normal)Stats+=FString::Printf(TEXT("  %.1fs"),S.FormRemaining);Stats+=TEXT("\n")+S.EquipmentName.ToString();HandbookStats->SetText(FText::FromString(Stats));}}
 auto* Weapon=P?P->WeaponAnimationComponent.Get():nullptr;
 if(HandbookEquipmentList)for(int32 I=0;I<UE_ARRAY_COUNT(Weapons);++I)if(auto* Row=Cast<UHorizontalBox>(HandbookEquipmentList->GetChildAt(I/EquipmentColumns)))if(auto* Box=Cast<USizeBox>(Row->GetChildAt(I%EquipmentColumns)))if(auto* B=Cast<UDWHandbookButton>(Box->GetContent()))
 {
  const auto* Binding=FDWCombatItemRegistry::FindPresentation(Weapons[I]);const auto* Definition=C&&Binding?C->GetItemDefinition(Binding->ItemId):nullptr;
  if(auto* T=Cast<UTextBlock>(B->GetContent())){T->SetText(Weapons[I]==EDWAnimatedWeapon::Unarmed?DWText(this,TEXT("空手"),TEXT("Unarmed")):(Definition?UDWLocalizationLibrary::GetItemDisplayName(this,*Definition):FText::FromName(Binding?Binding->ItemId:NAME_None)));T->SetAutoWrapText(false);}
  const bool Can=Weapon&&P&&!P->IsDead()&&P->HasCombatVitalsReady()&&DWCombatEquipment::CanSelect(Weapon->GetCombatWeaponStateSnapshot(),Weapons[I],Bag?Bag->CountItem(TEXT("Alcohol")):0);B->SetIsEnabled(Can);B->UpdateSelection(Weapon&&Weapon->SelectedWeapon==Weapons[I]);
 }

 const FDWItemStack Item=Bag&&Bag->GetSlots().IsValidIndex(SelectedSlot)?Bag->GetSlots()[SelectedSlot]:FDWItemStack();const auto* Def=C?C->GetItemDefinition(Item.ItemId):nullptr;
 HandbookSelection->SetText(Def?FText::Format(FText::FromString(TEXT("{0} x {1}")),UDWLocalizationLibrary::GetItemDisplayName(this,*Def),Item.Quantity):DWText(this,TEXT("选择一个物品"),TEXT("Select an item")));
 if(Def&&Def->Icon){HandbookItemImage->SetBrushFromTexture(Def->Icon,true);HandbookItemImage->SetColorAndOpacity(FLinearColor::White);HandbookItemImage->SetVisibility(ESlateVisibility::HitTestInvisible);}else{HandbookItemImage->SetBrushFromTexture(nullptr);HandbookItemImage->SetVisibility(ESlateVisibility::Hidden);}
 const int64 Rev=Bag?Bag->GetInventoryRevision():0;const int32 Lang=UDWLocalizationLibrary::GetRevision(this);
 if(Lang!=HandbookLanguageRevision)for(const auto& Label:HandbookLabels)if(Label.Key.IsValid())Label.Key->SetText(DWText(this,*Label.Value.Key,*Label.Value.Value));
 if(Rev!=HandbookInventoryRevision||Lang!=HandbookLanguageRevision){HandbookInventoryRevision=Rev;HandbookLanguageRevision=Lang;RefreshInventory();}
 bool Related=false,More=false;if(C&&!Item.ItemId.IsNone())for(const auto& R:C->Recipes){for(const auto& Input:R.Inputs)if(Input.ItemId==Item.ItemId)Related=true;for(const auto& Output:R.Outputs)if(Output.ItemId==Item.ItemId){Related=true;More=true;}}
 if(HandbookRelatedRecipeButton)HandbookRelatedRecipeButton->SetIsEnabled(Related);if(HandbookCraftMoreButton)HandbookCraftMoreButton->SetIsEnabled(More);
 if(CurrentPage==EDWMenuPage::Crafting)RefreshHandbookRecipes();
}

void UDWGameplayWidget::HandleHandbookAction(int32 Action)
{
 if(Action>=300&&Action<303){ActivateQuickInventorySlot(Action-300);return;}
 if(Action==27){if(CurrentPage==EDWMenuPage::Inventory&&!IsInventoryDiscardPending()&&!EquipInventorySlot(SelectedSlot))SetToast(DWText(this,TEXT("当前无法装备该物品"),TEXT("This item cannot be equipped now")).ToString());return;}
 const bool QuickWeapon=false; // Inventory shortcuts use exact slot actions 300..302; 200+ remains Character equipment only.
 if(!HUD||(!ADWGameplayHUD::IsHandbookPage(CurrentPage)&&!QuickWeapon)||IsInventoryDiscardPending())return;
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
 return DWCombatEquipment::CanSelect(W->GetCombatWeaponStateSnapshot(),Kind,Bag->CountItem(TEXT("Alcohol")));
}
bool UDWGameplayWidget::EquipInventorySlot(int32 Index)
{
 if(!HUD||!HUD->IsSessionStarted()||(HUD->GetMenuPage()!=EDWMenuPage::Inventory&&HUD->GetMenuPage()!=EDWMenuPage::None)||
    IsInventoryDiscardPending()||UDWWorldEventSubsystem::IsPlaying(this)||!GetC01CinematicOffset().IsNearlyZero(.5)||!CanEquipInventorySlot(Index))return false;
 const TWeakObjectPtr<ADWPlayerCharacter> P(GetPlayer());
 const TWeakObjectPtr<UDWWeaponAnimationComponent> W(P.IsValid()?P->WeaponAnimationComponent.Get():nullptr);
 const TWeakObjectPtr<UDWInventoryComponent> Bag(GetInventory());FDWInventorySlotSnapshot CapturedSlot;
 auto* A=UDWCombatAuthoritySubsystem::Get(P.Get());if(!A||!P.IsValid()||!W.IsValid()||!Bag.IsValid()||!Bag->CaptureSlotSnapshot(Index,CapturedSlot))return false;
 EDWAnimatedWeapon Kind;if(!DWCombatEquipment::ToPresentation(CapturedSlot.ItemId,Kind))return false;
 const auto Life=A->CaptureActorLife(P.Get());const uint64 Epoch=A->GetWorldEpoch(),Restore=W->GetCombatWeaponRestoreEpoch();const int64 Revision=W->GetCombatWeaponResourceRevision();
 if(HUD->GetMenuPage()!=EDWMenuPage::None)HUD->ClosePanels();
 A=UDWCombatAuthoritySubsystem::Get(P.Get());
 if(!A||!P.IsValid()||!W.IsValid()||!Bag.IsValid()||GetPlayer()!=P.Get()||P->GetInventory()!=Bag.Get()||P->WeaponAnimationComponent.Get()!=W.Get()||
    A->GetWorldEpoch()!=Epoch||W->GetCombatWeaponRestoreEpoch()!=Restore||W->GetCombatWeaponResourceRevision()!=Revision||!Bag->IsSlotSnapshotCurrent(CapturedSlot))return false;
 const auto Current=A->CaptureActorLife(P.Get());
 return Current.Entity==Life.Entity&&Current.Life==Life.Life&&W->SelectWeapon(Kind);
}
bool UDWGameplayWidget::CanActivateQuickInventorySlot(int32 Index)const
{
 const auto* GI=GetDWGameInstance();if(!GI||GI->IsPlayerRestorePending())return false;
 const auto* P=GetPlayer();const auto* Bag=GetInventory();
 return Index>=0&&Index<3&&bUseC01HUD&&HUD&&HUD->IsSessionStarted()&&HUD->GetMenuPage()==EDWMenuPage::None&&
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
 {if(!EquipInventorySlot(Index))SetToast(DWText(this,TEXT("当前无法装备该物品"),TEXT("This item cannot be equipped now")).ToString());return;}
 const auto* Config=GetGameplayConfig();const auto* Def=Config?Config->GetItemDefinition(CapturedSlot.ItemId):nullptr;
 const bool Core=Role==EDWCombatItemRole::MoldCore||Role==EDWCombatItemRole::YeastCore;
 if(Core||(Def&&Def->bUsable))
 {
  if(!Bag->IsSlotSnapshotCurrent(CapturedSlot)||GetPlayer()!=P.Get()||P->GetInventory()!=Bag.Get())return;
  const bool Done=Core?P->RequestCoreInjection(CapturedSlot):P->UseInventoryItem(Index);
  if(!Done)SetToast(DWText(this,TEXT("当前无法使用该物品"),TEXT("This item cannot be used now")).ToString());
  RefreshFromHUD(true);OnItemUseResult(CapturedSlot.ItemId,Done);return;
 }
 // A crafting material has no invented use effect: open its actual bag slot instead.
 HUD->ShowMenu(EDWMenuPage::Inventory);
 if(P.IsValid()&&Bag.IsValid()&&GetPlayer()==P.Get()&&P->GetInventory()==Bag.Get()&&Bag->IsSlotSnapshotCurrent(CapturedSlot))SelectInventorySlot(Index);
}
