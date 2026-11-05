#include "DWCraftingPanel.h"
#include "DWGameplayWidget.h"
#include "DWGameplayHUD.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWPlayerCharacter.h"
#include "DWCombatItemRegistry.h"
#include "DWLanguageSubsystem.h"
#include "DWLanguageItemTooltipFactory.h"
#include "DWLocalizationLibrary.h"
#include "DWAudioLibrary.h"
#include "DWC01UIStyle.h"
#include "Blueprint/WidgetTree.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/UniformGridPanel.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/UniformGridSlot.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/ButtonSlot.h"
#include "Components/Border.h"
#include "Components/BorderSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/ScaleBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/WrapBox.h"
#include "Components/WrapBoxSlot.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Font.h"
#include "Engine/GameInstance.h"
#include "HAL/PlatformTime.h"

void UDWCraftingControl::Bind(UDWCraftingPanel* P,int32 A,FName R)
{OwnerPanel=P;Code=A;Id=R;OnClicked.AddUniqueDynamic(this,&UDWCraftingControl::Dispatch);}
void UDWCraftingControl::Dispatch(){if(OwnerPanel.IsValid())OwnerPanel->Act(Code,Id);}

namespace
{
UTextBlock* ButtonLabel(UButton* Button)
{
 if(!Button)return nullptr;
 if(auto* Label=Cast<UTextBlock>(Button->GetContent()))return Label;
 if(auto* Fit=Cast<UScaleBox>(Button->GetContent()))return Cast<UTextBlock>(Fit->GetContent());
 return nullptr;
}
struct FBuild
{
 UWidgetTree* T;UDWGameplayWidget* S;UDWCraftingPanel* P;
 template<class W>W* Make(){return T->ConstructWidget<W>();}
 UTextBlock* Text(FText Value,int32 Size=16)
 {
  auto* W=Make<UTextBlock>();W->SetText(Value);auto F=W->GetFont();F.Size=Size;F.OutlineSettings.OutlineSize=0;
  if(S->GetDWHUD()&&S->GetDWHUD()->GameUIFont)F.FontObject=S->GetDWHUD()->GameUIFont;
  W->SetFont(F);W->SetColorAndOpacity(DWC01UI::Ink());W->SetAutoWrapText(false);return W;
 }
 USizeBox* Size(UWidget* C,float X,float Y){auto* W=Make<USizeBox>();W->SetWidthOverride(X);W->SetHeightOverride(Y);W->SetContent(C);return W;}
 UWidget* Fit(UWidget* C,float X,float Y){auto* W=Make<UScaleBox>();W->SetStretch(EStretch::ScaleToFit);W->SetContent(C);return Size(W,X,Y);}
 // Real text desired size includes the active Chinese/English font metrics. DownOnly
 // scales both axes inside the allocated inner rectangle instead of clipping glyphs.
 UScaleBox* TextFit(UTextBlock* Label,EHorizontalAlignment Align=HAlign_Center)
 {
  auto* W=Make<UScaleBox>();W->SetStretch(EStretch::ScaleToFit);W->SetStretchDirection(EStretchDirection::DownOnly);W->SetContent(Label);
  if(auto* TextCell=Cast<UScaleBoxSlot>(Label->Slot)){TextCell->SetHorizontalAlignment(Align);TextCell->SetVerticalAlignment(VAlign_Center);}
  return W;
 }
 void At(UCanvasPanel* C,UWidget* W,float X,float Y,float Width,float Height)
 {auto* Cell=C->AddChildToCanvas(W);Cell->SetPosition(FVector2D(X,Y));Cell->SetSize(FVector2D(Width,Height));}
 UBorder* Card(UWidget* C,FLinearColor Color,float Pad=8)
 {auto* W=Make<UBorder>();W->SetBrush(FSlateRoundedBoxBrush(Color,16.f));W->SetBrushColor(FLinearColor::White);W->SetPadding(FMargin(Pad));W->SetContent(C);return W;}
 UDWCraftingControl* Button(FText Label,int32 A,bool Slot=false,FName Id=NAME_None)
 {
  auto* W=Make<UDWCraftingControl>();W->Bind(P,A,Id);auto* L=Text(Label,18);L->SetJustification(ETextJustify::Center);L->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);W->SetContent(TextFit(L));W->SetToolTipText(Label);
  S->StyleC01Button(W,DWC01UI::Paper(),false,false,Slot);
  auto Design=W->GetStyle();
  Design.Normal=Design.Hovered=Design.Pressed=Design.Disabled=Slot?DWC01UI::QuickSlotFill(FVector2D(DWCrafting::BackpackSlotExtent,DWCrafting::BackpackSlotExtent)):FSlateRoundedBoxBrush(FLinearColor::White,12.f);
  Design.NormalPadding=Design.PressedPadding=FMargin(0);W->SetStyle(Design);
  if(auto* Cell=Cast<UButtonSlot>(W->GetContentSlot())){Cell->SetPadding(Slot?FMargin(0):FMargin(12,6));Cell->SetHorizontalAlignment(HAlign_Fill);Cell->SetVerticalAlignment(VAlign_Fill);}
  return W;
 }
};
const EDWCraftingCategory Tabs[]={EDWCraftingCategory::All,EDWCraftingCategory::Weapons,EDWCraftingCategory::Armour,EDWCraftingCategory::Tools,EDWCraftingCategory::Consumables,EDWCraftingCategory::Materials,EDWCraftingCategory::Buildings,EDWCraftingCategory::Other};
const TCHAR* CN[]={TEXT("全部"),TEXT("武器"),TEXT("护甲"),TEXT("工具"),TEXT("消耗品"),TEXT("材料"),TEXT("设施"),TEXT("其他")};
const TCHAR* EN[]={TEXT("All"),TEXT("Weapons"),TEXT("Armour"),TEXT("Tools"),TEXT("Consumables"),TEXT("Materials"),TEXT("Facilities"),TEXT("Other")};
const float Widths[]={58,92,84,72,132,102,102,76};
FLinearColor EnoughColor(){return FLinearColor::FromSRGBColor(FColor(185,202,153));}
}

void UDWCraftingPanel::Setup(UDWGameplayWidget* S)
{
 if(!S||!WidgetTree||RecipeGrid)return;Screen=S;FBuild B{WidgetTree,S,this};
 auto* Root=B.Make<UCanvasPanel>();WidgetTree->RootWidget=Root;
 auto* CategoryWrap=B.Make<UWrapBox>();CategoryWrap->SetExplicitWrapSize(true);CategoryWrap->SetWrapSize(520);CategoryWrap->SetInnerSlotPadding(FVector2D(8,6));B.At(Root,CategoryWrap,0,0,520,92);
 for(int32 I=0;I<UE_ARRAY_COUNT(Tabs);++I)
 {
  auto* Button=B.Button(DWText(S,CN[I],EN[I]),10+I);Categories.Add(Button);CategoryLabels.Add(ButtonLabel(Button));
  if(auto* Label=ButtonLabel(Button)){auto F=Label->GetFont();F.Size=16;Label->SetFont(F);}
  CategoryWrap->AddChildToWrapBox(B.Size(Button,Widths[I],42));
 }
 auto* Scroll=B.Make<UScrollBox>();Scroll->SetScrollBarVisibility(ESlateVisibility::Visible);
 RecipeGrid=B.Make<UUniformGridPanel>();RecipeGrid->SetSlotPadding(FMargin(4));RecipeGrid->SetMinDesiredSlotWidth(DWCrafting::BackpackCellExtent);RecipeGrid->SetMinDesiredSlotHeight(DWCrafting::BackpackCellExtent);Scroll->AddChild(RecipeGrid);B.At(Root,Scroll,0,104,520,366);
 ProductImage=B.Make<UImage>();auto* ImageBox=B.Card(B.Fit(ProductImage,60,60),DWC01UI::Paper(),8);
 ImageBox->SetHorizontalAlignment(HAlign_Center);ImageBox->SetVerticalAlignment(VAlign_Center);B.At(Root,ImageBox,568,0,496,76);
 Description=B.Text(FText::GetEmpty(),16);Description->SetJustification(ETextJustify::Left);
 B.At(Root,B.Card(B.TextFit(Description,HAlign_Left),DWC01UI::Paper(),6),568,84,496,36);
 Needs=B.Make<UDWLanguageRecipeNeedsWidget>();
 // Needs owns 40px station glyphs plus a text baseline. The old 32px Canvas
 // clipped neither desired height nor paint, leaking status labels behind cards.
 // Preserve its cached content/tooltip; bound it in a real scroll viewport instead.
 auto* ConditionsScroll=B.Make<UScrollBox>();ConditionsScroll->SetClipping(EWidgetClipping::ClipToBounds);ConditionsScroll->SetAlwaysShowScrollbar(false);ConditionsScroll->SetScrollBarVisibility(ESlateVisibility::Visible);ConditionsScroll->AddChild(Needs);B.At(Root,ConditionsScroll,568,128,496,72);
 MaterialScroll=B.Make<UScrollBox>();MaterialScroll->SetAlwaysShowScrollbar(false);MaterialScroll->SetScrollBarVisibility(ESlateVisibility::Visible);
 MaterialGrid=B.Make<UUniformGridPanel>();MaterialGrid->SetSlotPadding(FMargin(0,0,12,8));MaterialScroll->AddChild(MaterialGrid);B.At(Root,MaterialScroll,568,208,496,DWCrafting::MaterialViewportHeight);
 Minus=B.Button(FText::FromString(TEXT("-")),1);B.At(Root,Minus,568,414,48,56);
 CountLabel=B.Text(FText::GetEmpty(),18);CountLabel->SetJustification(ETextJustify::Center);auto* Counter=B.Card(B.TextFit(CountLabel),DWC01UI::Paper(),12);Counter->SetVerticalAlignment(VAlign_Center);B.At(Root,Counter,628,414,160,56);
 Plus=B.Button(FText::FromString(TEXT("+")),2);B.At(Root,Plus,800,414,48,56);
 Craft=B.Button(DWText(S,TEXT("制作"),TEXT("Craft")),3);S->StyleC01Button(Craft,DWC01UI::Paper());B.At(Root,Craft,868,414,196,56);
}
EDWCraftingCategory UDWCraftingPanel::Classify(FName Id) const
{
 auto* S=Screen.Get();auto* C=S?S->GetGameplayConfig():nullptr;if(!C)return EDWCraftingCategory::Other;
 const auto* Def=C->GetItemDefinition(Id);
 DWCrafting::FCategoryFacts Facts;
 const auto Role=FDWCombatItemRegistry::GetItemRole(Id);
 Facts.Weapon=Role==EDWCombatItemRole::PermanentWeapon;
 Facts.Armour=Id==TEXT("SugarArmor"); // Exact deployed identity, never a name-prefix guess.
 Facts.Building=C->GetBuildingDefinition(Id)!=nullptr;
 if(const auto* Profile=UDWGameplayConfig::AsGatheringConfig(C))if(const auto* Tool=Profile->Tools.Find(Id))
  {Facts.Weapon|=Tool->ToolType==TEXT("Spear");Facts.Tool=Tool->ToolType!=TEXT("Spear");}
 auto* GI=S->GetGameInstance();const auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 Facts.Consumable=(Def&&Def->bUsable)||(L&&L->IsRuntimeConsumable(Id))||Role==EDWCombatItemRole::ThrowableStock;
 Facts.Material=Role==EDWCombatItemRole::ReserveAmmo;
 const FName Canonical=DWSugarMaterial::Canonical(Id);
 for(const auto& R:C->Recipes)if(C->GetRecipeDefinition(R.RecipeId))for(const auto& Input:R.Inputs)if(DWSugarMaterial::Canonical(Input.ItemId)==Canonical)Facts.Material=true;
 return DWCrafting::ResolveCategory(Def?Def->Category:EDWItemCategory::Auto,Facts);
}
bool UDWCraftingPanel::RecipeMatches(FName Id) const
{
 const auto* S=Screen.Get();const auto* C=S?S->GetGameplayConfig():nullptr;const auto* R=C?C->GetRecipeDefinition(Id):nullptr;if(!R)return false;
 if(Category==EDWCraftingCategory::All)return true;
 for(const auto& O:R->Outputs)if(Classify(O.ItemId)==Category)return true;return false;
}
void UDWCraftingPanel::Refresh(FName Preferred,bool Force)
{
 auto* S=Screen.Get();auto* C=S?S->GetGameplayConfig():nullptr;if(!S||!C||!RecipeGrid)return;
 FBuild B{WidgetTree,S,this};auto* Bag=S->GetInventory();auto* P=S->GetPlayer();
 if(!Preferred.IsNone()&&Preferred!=Selected){Selected=Preferred;Category=EDWCraftingCategory::All;Count=1;Force=true;MaterialScroll->ScrollToStart();}
 const int64 Revision=Bag?Bag->GetInventoryRevision():-1;const int32 Locale=UDWLocalizationLibrary::GetRevision(S);const double Now=FPlatformTime::Seconds();
 if(!Force&&Revision==LastRevision&&Locale==LastLocale&&Now<NextRefreshAt)return;
 LastRevision=Revision;LastLocale=Locale;NextRefreshAt=Now+.25;
 TArray<FName> Visible;
 for(const auto& R:C->Recipes)if(C->GetRecipeDefinition(R.RecipeId)&&RecipeMatches(R.RecipeId))Visible.AddUnique(R.RecipeId);
 if(!Visible.Contains(Selected))Selected=Visible.Num()?Visible[0]:NAME_None;
 S->SetHandbookRecipeSelection(Selected);
 const auto* R=C->GetRecipeDefinition(Selected);
 int32 Limit=DWCrafting::HardBatchLimit;FName Permanent;
 if(R&&Bag&&Bag->GetCraftPermanentWeaponOutput(Selected,Permanent)&&!Permanent.IsNone())Limit=1;
 if(R)for(const auto& O:R->Outputs)if(C->GetBuildingDefinition(O.ItemId))Limit=1;
 Count=FMath::Clamp(Count,1,Limit);
 TArray<FDWItemStack> Inputs;const bool Valid=R&&DWCrafting::Scale(R->Inputs,Count,true,Inputs);
 FString Signature=Selected.ToString()+FString::Printf(TEXT("|%d|%d|%lld|%d"),Count,int32(Category),Bag?Bag->GetInventoryRevision():-1,UDWLocalizationLibrary::GetRevision(S));
 for(FName Id:Visible)Signature+=TEXT("|")+Id.ToString();
 if(R)for(const auto& O:R->Outputs)Signature+=FString::Printf(TEXT("|out:%s:%d"),*O.ItemId.ToString(),O.Quantity);
 for(const auto& I:Inputs)Signature+=FString::Printf(TEXT("|%s:%d:%d"),*I.ItemId.ToString(),I.Quantity,Bag?Bag->CountCraftingMaterial(I.ItemId):0);
 const auto Conditions=DWLanguageItemTooltip::Conditions(S,Selected);for(const auto& V:Conditions)Signature+=V.ConditionId.ToString()+(V.bSatisfied?TEXT("+" ):TEXT("-"));
 const bool Can=Valid&&P&&P->CanCraftRecipeBatch(Selected,Count)&&!S->IsInventoryDiscardPending();Signature+=Can?TEXT("|yes"):TEXT("|no");
 if(!Force&&Signature==LastSignature)return;LastSignature=Signature;
 TSet<EDWCraftingCategory> Present;
 for(const auto& Recipe:C->Recipes)if(C->GetRecipeDefinition(Recipe.RecipeId))for(const auto& O:Recipe.Outputs)Present.Add(Classify(O.ItemId));
 for(int32 I=0;I<Categories.Num();++I){Categories[I]->GetParent()->SetVisibility(I==0||Present.Contains(Tabs[I])?ESlateVisibility::Visible:ESlateVisibility::Collapsed);S->StyleC01Button(Categories[I],DWC01UI::Paper(),Tabs[I]==Category);CategoryLabels[I]->SetText(DWText(S,CN[I],EN[I]));}
 for(auto& Cell:Recipes)if(!Visible.Contains(Cell.Id)&&Cell.Box->GetVisibility()!=ESlateVisibility::Collapsed)Cell.Box->SetVisibility(ESlateVisibility::Collapsed);
 for(int32 Index=0;Index<Visible.Num();++Index)
 {
  const FName Id=Visible[Index];auto* Cell=Recipes.FindByPredicate([Id](const auto& X){return X.Id==Id;});
  if(!Cell)
  {
   FDWCraftingRecipeCell New;New.Id=Id;New.Button=B.Button(FText::GetEmpty(),4,true,Id);
   auto* Content=B.Make<UOverlay>();New.Icon=B.Make<UImage>();
   auto* IconPlacement=Content->AddChildToOverlay(B.Fit(New.Icon,DWCrafting::BackpackIconExtent,DWCrafting::BackpackIconExtent));IconPlacement->SetHorizontalAlignment(HAlign_Center);IconPlacement->SetVerticalAlignment(VAlign_Center);
   New.Fallback=B.Text(FText::GetEmpty(),14);New.Fallback->SetJustification(ETextJustify::Center);
   auto* NamePlacement=Content->AddChildToOverlay(B.Size(B.TextFit(New.Fallback),76,64));NamePlacement->SetHorizontalAlignment(HAlign_Center);NamePlacement->SetVerticalAlignment(VAlign_Center);
   New.Button->SetContent(Content);if(auto* ContentCell=Cast<UButtonSlot>(New.Button->GetContentSlot())){ContentCell->SetPadding(FMargin(0));ContentCell->SetHorizontalAlignment(HAlign_Fill);ContentCell->SetVerticalAlignment(VAlign_Fill);}
   New.Box=B.Size(New.Button,DWCrafting::BackpackSlotExtent,DWCrafting::BackpackSlotExtent);auto* RecipeCell=RecipeGrid->AddChildToUniformGrid(New.Box,Index/DWCrafting::RecipeColumns,Index%DWCrafting::RecipeColumns);RecipeCell->SetHorizontalAlignment(HAlign_Center);RecipeCell->SetVerticalAlignment(VAlign_Center);Recipes.Add(New);Cell=&Recipes.Last();
  }
  if(auto* GridCellSlot=Cast<UUniformGridSlot>(Cell->Box->Slot)){if(GridCellSlot->GetRow()!=Index/DWCrafting::RecipeColumns)GridCellSlot->SetRow(Index/DWCrafting::RecipeColumns);if(GridCellSlot->GetColumn()!=Index%DWCrafting::RecipeColumns)GridCellSlot->SetColumn(Index%DWCrafting::RecipeColumns);}
  if(Cell->Box->GetVisibility()!=ESlateVisibility::Visible)Cell->Box->SetVisibility(ESlateVisibility::Visible);const auto* Recipe=C->GetRecipeDefinition(Id);const auto* Item=Recipe&&Recipe->Outputs.Num()?C->GetItemDefinition(Recipe->Outputs[0].ItemId):nullptr;
  if(Cell->Icon->GetBrush().GetResourceObject()!=(Item?Item->Icon.Get():nullptr))Cell->Icon->SetBrushFromTexture(Item?Item->Icon.Get():nullptr,false);
  Cell->Icon->SetVisibility(Item&&Item->Icon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  Cell->Fallback->SetText(Recipe?UDWLocalizationLibrary::GetRecipeDisplayName(S,*Recipe):FText::FromName(Id));Cell->Fallback->SetVisibility(Item&&Item->Icon?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
  S->StyleC01Button(Cell->Button,DWC01UI::Paper(),Id==Selected,false,true);DWLanguageItemTooltip::AttachRecipe(Cell->Button,S,Id);
 }
 LastVisible=Visible;
 const auto* Product=R&&R->Outputs.Num()?C->GetItemDefinition(R->Outputs[0].ItemId):nullptr;
 if(ProductImage->GetBrush().GetResourceObject()!=(Product?Product->Icon.Get():nullptr))ProductImage->SetBrushFromTexture(Product?Product->Icon.Get():nullptr,false);
 ProductImage->SetVisibility(Product&&Product->Icon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
 FText Details=R?UDWLocalizationLibrary::GetRecipeDisplayName(S,*R):DWText(S,TEXT("暂无配方"),TEXT("No recipes"));
 if(Product){const auto View=DWLanguageItemTooltip::Item(S,FDWItemStack(Product->ItemId,1));if(!View.Use.IsEmpty())Details=FText::Format(FText::FromString(TEXT("{0} · {1}")),Details,View.Use);}
 Description->SetText(Details);Description->SetToolTipText(Details);Needs->SetRecipeContext(S,Selected);
 for(int32 I=0;I<Inputs.Num();++I)
 {
  if(!Materials.IsValidIndex(I))
  {
   FDWCraftingMaterialCell New;auto* Row=B.Make<UHorizontalBox>();New.Icon=B.Make<UImage>();
   New.IconFrame=B.Card(B.Fit(New.Icon,DWCrafting::MaterialIconExtent,DWCrafting::MaterialIconExtent),DWC01UI::Paper(),6);
   auto IconShape=DWC01UI::QuickSlotFill(FVector2D(DWCrafting::MaterialSlotExtent,DWCrafting::MaterialSlotExtent));IconShape.TintColor=DWC01UI::Paper();New.IconFrame->SetBrush(IconShape);
   Row->AddChildToHorizontalBox(B.Size(New.IconFrame,DWCrafting::MaterialSlotExtent,DWCrafting::MaterialSlotExtent))->SetPadding(FMargin(0,0,10,0));
   auto* Texts=B.Make<UVerticalBox>();New.Need=B.Text(FText::GetEmpty(),14);New.Have=B.Text(FText::GetEmpty(),14);
   Texts->AddChildToVerticalBox(B.Size(B.TextFit(New.Need,HAlign_Left),162,20))->SetPadding(FMargin(0,0,0,2));
   Texts->AddChildToVerticalBox(B.Size(B.TextFit(New.Have,HAlign_Left),162,20));
   Row->AddChildToHorizontalBox(B.Size(Texts,162,42))->SetVerticalAlignment(VAlign_Center);
   New.Card=B.Card(Row,DWC01UI::Paper(),7);auto* Sized=B.Size(New.Card,DWCrafting::MaterialCardWidth,DWCrafting::MaterialCardHeight);auto* MaterialCell=MaterialGrid->AddChildToUniformGrid(Sized,I/2,I%2);MaterialCell->SetHorizontalAlignment(HAlign_Left);MaterialCell->SetVerticalAlignment(VAlign_Center);Materials.Add(New);
  }
  auto& Cell=Materials[I];const auto& Input=Inputs[I];const int32 Have=Bag?Bag->CountCraftingMaterial(Input.ItemId):0;const auto* Def=C->GetItemDefinition(Input.ItemId);
  Cell.Card->GetParent()->SetVisibility(ESlateVisibility::Visible);const FLinearColor MaterialColor=Have>=Input.Quantity?EnoughColor():DWC01UI::Paper();
  Cell.Card->SetBrush(FSlateRoundedBoxBrush(MaterialColor,12.f));
  auto IconShape=DWC01UI::QuickSlotFill(FVector2D(DWCrafting::MaterialSlotExtent,DWCrafting::MaterialSlotExtent));IconShape.TintColor=MaterialColor;Cell.IconFrame->SetBrush(IconShape);
  if(Cell.Icon->GetBrush().GetResourceObject()!=(Def?Def->Icon.Get():nullptr))Cell.Icon->SetBrushFromTexture(Def?Def->Icon.Get():nullptr,false);
  Cell.Icon->SetVisibility(Def&&Def->Icon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
  Cell.Need->SetText(FText::Format(DWText(S,TEXT("需要 {0}"),TEXT("Need {0}")),FText::AsNumber(Input.Quantity)));Cell.Have->SetText(FText::Format(DWText(S,TEXT("已有 {0}"),TEXT("Have {0}")),FText::AsNumber(Have)));
  const FText Name=Def?UDWLocalizationLibrary::GetItemDisplayName(S,*Def):FText::FromName(Input.ItemId);
  Cell.Card->SetToolTipText(FText::Format(FText::FromString(TEXT("{0}\n{1}\n{2}")),Name,Cell.Need->GetText(),Cell.Have->GetText()));
 }
 for(int32 I=Inputs.Num();I<Materials.Num();++I)Materials[I].Card->GetParent()->SetVisibility(ESlateVisibility::Collapsed);
 CountLabel->SetText(FText::Format(DWText(S,TEXT("{0} 份"),TEXT("{0} crafts")),FText::AsNumber(Count)));
 Minus->SetIsEnabled(R&&Count>1);Plus->SetIsEnabled(R&&Count<Limit);Craft->SetIsEnabled(Can);
 if(auto* L=ButtonLabel(Craft))L->SetText(DWText(S,TEXT("制作"),TEXT("Craft")));
}
void UDWCraftingPanel::Act(int32 Code,FName Recipe)
{
 auto* S=Screen.Get();if(!S||S->IsInventoryDiscardPending())return;
 if(Code!=3&&S->GetDWHUD())S->GetDWHUD()->PlayClickSound();
 if(Code>=10&&Code<10+UE_ARRAY_COUNT(Tabs)){Category=Tabs[Code-10];Refresh(NAME_None,true);return;}
 if(Code==4){Selected=Recipe;Count=1;S->SetHandbookRecipeSelection(Selected);MaterialScroll->ScrollToStart();Refresh(NAME_None,true);return;}
 if(Code==1||Code==2){Count=FMath::Clamp(Count+(Code==1?-1:1),1,DWCrafting::HardBatchLimit);Refresh(NAME_None,true);return;}
 if(Code!=3)return;auto* Player=S->GetPlayer();if(!Player)return;
 const TWeakObjectPtr<UDWCraftingPanel> Self(this);const TWeakObjectPtr<UDWGameplayWidget> Owner(S);const FName Id=Selected;
 const bool Done=Player->CraftRecipeBatch(Id,Count);if(!Self.IsValid()||!Owner.IsValid())return;
 UDWAudioLibrary::PlayEvent(Owner.Get(),Owner->GetGameplayConfig(),Done?EDWAudioEvent::CraftSuccess:EDWAudioEvent::CraftFailed);
 if(!Self.IsValid()||!Owner.IsValid())return;Self->Refresh(NAME_None,true);Owner->OnRecipeCraftResult(Id,Done);
}
