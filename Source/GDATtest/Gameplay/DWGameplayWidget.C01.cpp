#include "DWGameplayWidget.h"
#include "Engine/Font.h"
#include "DWC01UIStyle.h"
#include "DWC01HUDAnchor.h"
#include "DWUIOffscreenComponent.h"
#include "DWHandbookButton.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWInventoryComponent.h"
#include "DWPlayerController.h"
#include "DWGameplayConfig.h"
#include "DWLocalizationLibrary.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/SizeBox.h"
#include "Components/ScaleBox.h"
#include "Components/Border.h"
#include "Components/ButtonSlot.h"
#include "DWWorldEvent.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Brushes/SlateNoResource.h"
#include "HAL/PlatformTime.h"
#include "Components/PanelWidget.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/Image.h"
#include "DWUIBounceComponent.h"
#include "DWUIBrushFillComponent.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "Components/CapsuleComponent.h"
#include "Blueprint/WidgetLayoutLibrary.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Extensions/UIComponentContainer.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "UObject/UnrealType.h"
#include "Framework/Application/SlateApplication.h"

namespace
{
// This is the same existing designer component, mounted before the local widget builds Slate.
// Its wrapper composes with the HUD's cinematic translation; no gameplay clock/state is written.
UDWUIBounceComponent* C01MountPresentationBounce(UUserWidget* Owner,UWidget* Target)
{
 if(!Owner||!Target||Target->GetCachedWidget().IsValid())return nullptr;
 auto* Extension=Owner->GetExtension<UUIComponentUserWidgetExtension>();
 if(!Extension)Extension=Owner->AddExtension<UUIComponentUserWidgetExtension>();
 if(!Extension)return nullptr;
 auto* Property=FindFProperty<FObjectPropertyBase>(Extension->GetClass(),TEXT("ComponentContainer"));
 auto* Container=Property?Cast<UUIComponentContainer>(Property->GetObjectPropertyValue_InContainer(Extension)):nullptr;
 if(!Container){Container=NewObject<UUIComponentContainer>(Extension);Extension->InitializeContainer(Container);}
 auto* Bounce=NewObject<UDWUIBounceComponent>(Container);
 Bounce->bAutoBindButtons=false;Bounce->bAnimateNonButtonHover=false;Bounce->bPlayOnConstruct=false;
 Bounce->EntryOffset=FVector2D::ZeroVector;Bounce->EntryAngle=0.f;
 Container->AddComponent(Target->GetFName(),Bounce);Bounce->Initialize(Target);Target->bWrappedByComponent=true;
 if(Owner->IsConstructed()){Bounce->PreConstruct(false);Bounce->Construct();}
 return Bounce;
}
float C01Safe(float Value,float Fallback,float Min,float Max)
{return FMath::IsFinite(Value)?FMath::Clamp(Value,Min,Max):Fallback;}
bool C01IsEnabledInHierarchy(const UWidget* Widget)
{
 const UWidget* Current=Widget;
 for(int32 Depth=0;Current&&Depth<64;++Depth)
 {
  if(!Current->GetIsEnabled())return false;
  if(const auto* Parent=Current->GetParent())Current=Parent;
  else
  {
   const auto* Tree=Cast<UWidgetTree>(Current->GetOuter());
   Current=Tree?Cast<UUserWidget>(Tree->GetOuter()):nullptr;
  }
 }
 return Current==nullptr;
}
}

void UDWGameplayWidget::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 if(bUseC01HUD)PrepareSavePageLayout();
 if(bUseC01HUD&&WidgetTree)WidgetTree->ForEachWidget([](UWidget* W)
 {if(auto* B=Cast<UButton>(W))DWC01UI::AttachMotion(B,false,true);});
}

void UDWGameplayWidget::StyleC01Button(UButton* B,FLinearColor BaseColor,bool Selected,bool Transparent,bool ItemSlot)
{
 if(!bUseC01HUD||!B)return;
 if(const auto* Choice=Cast<UDWHandbookButton>(B))ItemSlot|=Choice->bC01ItemSlot;
 if(ItemSlot)DWC01UI::DisableSlotMotion(B);else DWC01UI::AttachMotion(B,false);
 const bool First=!C01Buttons.Contains(B);
 auto& V=C01Buttons.FindOrAdd(B);V.Base=BaseColor;V.bSelected=Selected;V.bTransparentRest=Transparent;V.bItemSlot=ItemSlot;
 V.bBrushFill=UDWUIBrushFillLibrary::GetBrushFillComponent(B)!=nullptr;
 if(First)
 {
  FButtonStyle S=B->GetStyle();S.Normal=S.Hovered=S.Pressed=S.Disabled=DWC01UI::Surface(FLinearColor::White);
  if(V.bBrushFill||B->GetFName()==TEXT("HandbookClose"))S.Normal=S.Hovered=S.Pressed=S.Disabled=FSlateNoResource();
  // Spring movement belongs to the existing Bounce wrapper, never to layout padding.
  S.PressedPadding=S.NormalPadding;B->SetStyle(S);
  V.Current=V.Start=V.Target=BaseColor;V.Started=FPlatformTime::Seconds();V.bInitialized=true;
 }
 if(auto* Label=Cast<UTextBlock>(B->GetContent()))
  Label->SetColorAndOpacity(V.bBrushFill||(!Selected&&BaseColor.Equals(DWC01UI::Danger(),.0001f))?FLinearColor::White:DWC01UI::Ink());
 if(V.bBrushFill)B->SetBackgroundColor(FLinearColor::Transparent);
 DWC01UI::CenterButtonLabel(B,C01ButtonLatinOpticalOffsetEm);
}

void UDWGameplayWidget::TickC01Buttons()
{
 const double Now=FPlatformTime::Seconds();
 for(auto It=C01Buttons.CreateIterator();It;++It)
 {
  UButton* B=It.Key().Get();if(!IsValid(B)){It.RemoveCurrent();continue;}
  auto& V=It.Value();
  // Refresh after runtime translation/font/layout changes; the spring still owns the outer button.
  DWC01UI::CenterButtonLabel(B,C01ButtonLatinOpticalOffsetEm);
  if(B->GetFName()==TEXT("HandbookClose")){B->SetBackgroundColor(FLinearColor::Transparent);continue;}
  if(V.bBrushFill){B->SetBackgroundColor(FLinearColor::Transparent);if(auto* Label=Cast<UTextBlock>(B->GetContent()))Label->SetColorAndOpacity(FLinearColor::White);continue;}
  const bool Enabled=C01IsEnabledInHierarchy(B)&&(!FSlateApplication::IsInitialized()||FSlateApplication::Get().IsActive());
  const bool bDanger=!V.bSelected&&V.Base.Equals(DWC01UI::Danger(),.0001f);
  FLinearColor Target=V.bSelected?DWC01UI::Gold():V.Base;
  float Seconds=V.bSelected?C01FocusSeconds:C01HoverSeconds;
  // Keep danger readable: the old .13 linear white lift washed out both light and dark labels.
  if(Enabled&&B->IsHovered())Target=FMath::Lerp(Target,FLinearColor::White,V.bItemSlot?FMath::Clamp(C01SlotHoverLighten,0.f,1.f):(bDanger?.04f:.13f));
  if(!V.bItemSlot&&Enabled&&B->IsPressed()){Target=FMath::Lerp(Target,DWC01UI::Ink(),.16f);Seconds=C01PressSeconds;}
  if(V.bTransparentRest&&!V.bSelected&&(!Enabled||!B->IsHovered()))Target.A=0.f;
  if(!Target.Equals(V.Target,.0001f))
  {V.Start=V.Current;V.Target=Target;V.Started=Now;V.Duration=FMath::Max(0.f,Seconds);}
  const float A=V.Duration>SMALL_NUMBER?FMath::Clamp(float((Now-V.Started)/V.Duration),0.f,1.f):1.f;
  // LayoutSlot hover is explicitly linear, independent of the execution-button color curve.
  V.Current=FMath::Lerp(V.Start,V.Target,V.bItemSlot?A:DWC01UI::Ease(A));B->SetBackgroundColor(V.Current);
  if(auto* Label=Cast<UTextBlock>(B->GetContent()))Label->SetColorAndOpacity(bDanger?FLinearColor::White:DWC01UI::Ink());
 }
}

void UDWGameplayWidget::InitializeC01Display()
{
 if(!bUseC01HUD||!WidgetTree)return;
 const float DialDiameter=C01Safe(C01HUDDiameter,248.f,160.f,400.f);
 const FVector2D HUDMargin=C01HUDMargin.ContainsNaN()?FVector2D(72,30):C01HUDMargin;
 if(HUDLayer&&!C01HUDAnchor)
 {
  C01HUDAnchor=WidgetTree->ConstructWidget<UDWC01HUDAnchor>(UDWC01HUDAnchor::StaticClass(),TEXT("C01HUDCinematicAnchor"));
  C01HUDAnchor->SetBrush(FSlateNoResource());C01HUDAnchor->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* AnchorSlot=HUDLayer->AddChildToCanvas(C01HUDAnchor);AnchorSlot->SetAnchors(FAnchors(0,1));
  AnchorSlot->SetAlignment(FVector2D(0,1));AnchorSlot->SetPosition(FVector2D(HUDMargin.X,-HUDMargin.Y));AnchorSlot->SetSize(FVector2D(DialDiameter,DialDiameter));
 }
 if(HUDLayer&&!C01QuickDock)
 {
  C01QuickDock=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C01QuickDock"));C01QuickDock->SetBrush(FSlateNoResource());C01QuickDock->SetPadding(FMargin(0));
  // These are inventory positions 0..2, never the first three entries of the weapon registry.
  const float CellSize=C01Safe(C01QuickSlotSize,92.f,64.f,128.f),CellGap=C01Safe(C01QuickSlotGap,16.f,0.f,32.f);
  auto* DockCanvas=HUDLayer->AddChildToCanvas(C01QuickDock);DockCanvas->SetAnchors(FAnchors(.5f,1.f));DockCanvas->SetAlignment(FVector2D(.5f,1.f));DockCanvas->SetSize(FVector2D(3*CellSize+2*CellGap,CellSize));DockCanvas->SetPosition(FVector2D(0,-HUDMargin.Y));DockCanvas->SetZOrder(25);
  auto* Row=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("C01QuickDockRow"));C01QuickDock->SetContent(Row);
  const auto MakeText=[this](const TCHAR* Prefix,int32 Index,int32 PointSize)
  {
   auto* T=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),FName(*FString::Printf(TEXT("%s_%d"),Prefix,Index+1)));
   T->SetColorAndOpacity(DWC01UI::Ink());T->SetVisibility(ESlateVisibility::HitTestInvisible);
   FSlateFontInfo Font=T->GetFont();Font.Size=PointSize;Font.OutlineSettings.OutlineSize=0;if(HUD&&HUD->GameUIFont)Font.FontObject=HUD->GameUIFont;T->SetFont(Font);return T;
  };
  for(int32 I=0;I<3;++I)
  {
   const FName ButtonName(*FString::Printf(TEXT("C01QuickInventory_%d"),I+1));auto* Button=WidgetTree->ConstructWidget<UDWHandbookButton>(UDWHandbookButton::StaticClass(),ButtonName);
   Button->Configure(this,300+I,true);Button->C01BaseColor=DWC01UI::Paper();StyleC01Button(Button,DWC01UI::Paper(),false,false,true);
   FButtonStyle Design=Button->GetStyle();Design.Normal=Design.Hovered=Design.Pressed=Design.Disabled=DWC01UI::QuickSlotFill(FVector2D(CellSize,CellSize));Design.NormalPadding=Design.PressedPadding=FMargin(4);Button->SetStyle(Design);
   auto* Content=WidgetTree->ConstructWidget<UOverlay>();Button->SetContent(Content);if(auto* ContentSlot=Cast<UButtonSlot>(Content->Slot)){ContentSlot->SetHorizontalAlignment(HAlign_Fill);ContentSlot->SetVerticalAlignment(VAlign_Fill);}
   auto* Icon=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("C01QuickIcon_%d"),I+1)));Icon->SetVisibility(ESlateVisibility::Collapsed);
   auto* IconSize=WidgetTree->ConstructWidget<USizeBox>();IconSize->SetWidthOverride(CellSize*.55f);IconSize->SetHeightOverride(CellSize*.55f);IconSize->SetContent(Icon);
   auto* IconLayout=Content->AddChildToOverlay(IconSize);IconLayout->SetHorizontalAlignment(HAlign_Center);IconLayout->SetVerticalAlignment(VAlign_Center);
   auto* Name=MakeText(TEXT("C01QuickName"),I,12);Name->SetJustification(ETextJustify::Center);Name->SetAutoWrapText(false);Name->SetWrapTextAt(CellSize-16.f);Name->SetWrappingPolicy(ETextWrappingPolicy::DefaultWrapping);Name->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
   // Missing-icon labels remain inside the cell, with separate space for its key and quantity.
   auto* NameFit=WidgetTree->ConstructWidget<UScaleBox>();NameFit->SetStretch(EStretch::ScaleToFit);NameFit->SetStretchDirection(EStretchDirection::DownOnly);NameFit->SetContent(Name);
   auto* NameSize=WidgetTree->ConstructWidget<USizeBox>();NameSize->SetWidthOverride(CellSize-12.f);NameSize->SetHeightOverride(CellSize-38.f);NameSize->SetClipping(EWidgetClipping::ClipToBoundsAlways);NameSize->SetContent(NameFit);
   auto* NameLayout=Content->AddChildToOverlay(NameSize);NameLayout->SetHorizontalAlignment(HAlign_Center);NameLayout->SetVerticalAlignment(VAlign_Center);
   auto* Quantity=MakeText(TEXT("C01QuickQuantity"),I,15);auto* QuantityLayout=Content->AddChildToOverlay(Quantity);QuantityLayout->SetHorizontalAlignment(HAlign_Right);QuantityLayout->SetVerticalAlignment(VAlign_Bottom);
   auto* Key=MakeText(TEXT("C01QuickKey"),I,12);auto* KeyLayout=Content->AddChildToOverlay(Key);KeyLayout->SetHorizontalAlignment(HAlign_Left);KeyLayout->SetVerticalAlignment(VAlign_Top);
   // Outline is a sibling above the tinted button, so color/disabled tweens cannot color its white rim.
   auto* Frame=WidgetTree->ConstructWidget<UOverlay>();auto* ButtonLayout=Frame->AddChildToOverlay(Button);ButtonLayout->SetHorizontalAlignment(HAlign_Fill);ButtonLayout->SetVerticalAlignment(VAlign_Fill);
   auto* Outline=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),FName(*FString::Printf(TEXT("C01QuickOutline_%d"),I+1)));Outline->SetBrush(DWC01UI::QuickSlotOutline(FVector2D(CellSize,CellSize),C01QuickSlotOutlineThickness));Outline->SetVisibility(ESlateVisibility::HitTestInvisible);auto* OutlineLayout=Frame->AddChildToOverlay(Outline);OutlineLayout->SetHorizontalAlignment(HAlign_Fill);OutlineLayout->SetVerticalAlignment(VAlign_Fill);
   auto* Size=WidgetTree->ConstructWidget<USizeBox>();Size->SetWidthOverride(CellSize);Size->SetHeightOverride(CellSize);Size->SetContent(Frame);auto* Cell=Row->AddChildToHorizontalBox(Size);Cell->SetPadding(FMargin(0,0,I<2?CellGap:0,0));
   C01QuickButtons.Add(Button);C01QuickIcons.Add(Icon);C01QuickOutlines.Add(Outline);C01QuickNames.Add(Name);C01QuickQuantities.Add(Quantity);C01QuickKeys.Add(Key);
  }
  C01QuickDock->SetVisibility(ESlateVisibility::Collapsed);
 }
 if(HUDLayer&&!C01PickupPanel)
 {
  C01PickupPanel=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C01PickupPanel"));
  C01PickupPanel->SetPadding(FMargin(14,10));C01PickupPanel->SetBrush(DWC01UI::Texture(TEXT("T_DW_C01_PickupToast_Paper_fine_edge_53098739"),FVector2D(264,58)));
  C01PickupBounce=C01MountPresentationBounce(this,C01PickupPanel);
  C01PickupLabel=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("C01PickupLabel"));
  C01PickupLabel->SetColorAndOpacity(DWC01UI::Ink());C01PickupLabel->SetJustification(ETextJustify::Center);
  C01PickupLabel->SetAutoWrapText(false);C01PickupLabel->SetTextOverflowPolicy(ETextOverflowPolicy::Ellipsis);
  C01PickupPanel->SetContent(C01PickupLabel);
  auto* LayoutSlot=HUDLayer->AddChildToCanvas(C01PickupPanel);LayoutSlot->SetAnchors(FAnchors(0,1));LayoutSlot->SetAlignment(FVector2D(0,1));LayoutSlot->SetSize(FVector2D(264,58));LayoutSlot->SetZOrder(30);
  C01PickupPanel->SetVisibility(ESlateVisibility::Collapsed);
 }
 if(HUDLayer&&!C01ShellDock)
 {
  C01ShellDock=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C01ShellDock"));C01ShellDock->SetPadding(FMargin(0));C01ShellDock->SetBrush(FSlateNoResource());
  C01ShellRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("C01ShellRow"));C01ShellDock->SetContent(C01ShellRow);
  auto* LayoutSlot=HUDLayer->AddChildToCanvas(C01ShellDock);LayoutSlot->SetAlignment(FVector2D(.5,.5));LayoutSlot->SetZOrder(30);
  C01ShellDock->SetVisibility(ESlateVisibility::Collapsed);
 }
 // Saved Designer widgets remain intact. Only obsolete display groups are suppressed at runtime.
 for(const TCHAR* Name:{TEXT("StatusCard"),TEXT("HealthCard"),TEXT("BrewingCard"),TEXT("SprintCard"),TEXT("QuickSlots"),TEXT("Quickbar"),TEXT("HUDHints")})
  if(auto* W=GetWidgetFromName(Name))W->SetVisibility(ESlateVisibility::Collapsed);
 for(UTextBlock* T:{HealthValueText.Get(),TransformationValueText.Get(),FormText.Get(),RingValueText.Get(),AlcoholCountText.Get()})
  if(T)T->SetVisibility(ESlateVisibility::Collapsed);
 if(HUDPauseButton)HUDPauseButton->SetVisibility(ESlateVisibility::Collapsed);
}
FVector2D UDWGameplayWidget::GetC01CinematicOffset()const
{return C01HUDAnchor?C01HUDAnchor->GetPresentationOffset():FVector2D::ZeroVector;}

void UDWGameplayWidget::TickC01QuickDock()
{
 if(C01HUDAnchor)C01HUDAnchor->SetMenuHidden(HUD&&HUD->GetMenuPage()!=EDWMenuPage::None);
 TickC01RuntimeMeters();
 if(!C01QuickDock)return;auto* P=GetPlayer();
 const bool Show=bUseC01HUD&&HUD&&HUD->IsSessionStarted()&&IsValid(P)&&!P->IsDead()&&CombatHUD.bReady;
 if(!Show){C01QuickDock->SetVisibility(ESlateVisibility::Collapsed);return;}
 const FVector2D Offset=GetC01CinematicOffset();const bool Transition=HUD->GetMenuPage()!=EDWMenuPage::None||UDWWorldEventSubsystem::IsPlaying(this)||!Offset.IsNearlyZero(.5);
 C01QuickDock->SetVisibility(Transition?ESlateVisibility::HitTestInvisible:ESlateVisibility::Visible);
 const FVector2D HUDMargin=C01HUDMargin.ContainsNaN()?FVector2D(72,30):C01HUDMargin;
 if(auto* DockCanvas=Cast<UCanvasPanelSlot>(C01QuickDock->Slot))DockCanvas->SetPosition(FVector2D(Offset.X,-HUDMargin.Y+Offset.Y));
 const auto* Config=GetGameplayConfig();const auto* Bag=P->GetInventory();const auto* Controller=Cast<ADWPlayerController>(GetOwningPlayer());
 for(int32 I=0;I<C01QuickButtons.Num()&&I<3;++I)if(auto* Button=C01QuickButtons[I].Get())
 {
  const FDWItemStack Item=Bag&&Bag->GetSlots().IsValidIndex(I)?Bag->GetSlots()[I]:FDWItemStack();
  const bool Occupied=!Item.ItemId.IsNone()&&Item.Quantity>0;const auto* Definition=Occupied&&Config?Config->GetItemDefinition(Item.ItemId):nullptr;
  const FText Name=Occupied?(Definition?UDWLocalizationLibrary::GetItemDisplayName(this,*Definition):FText::FromName(Item.ItemId)):FText::GetEmpty();
  const bool HasIcon=Occupied&&Definition&&Definition->Icon;const bool Selected=Occupied&&CombatHUD.EquipmentId==Item.ItemId;
  Button->SetIsEnabled(!Transition&&CanActivateQuickInventorySlot(I));Button->SetRenderOpacity(Occupied?1.f:.45f);Button->UpdateSelection(Selected);
  if(C01QuickIcons.IsValidIndex(I))if(auto* Icon=C01QuickIcons[I].Get()){Icon->SetBrushFromTexture(HasIcon?Definition->Icon.Get():nullptr,false);Icon->SetColorAndOpacity(FLinearColor::White);Icon->SetVisibility(HasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
  if(C01QuickNames.IsValidIndex(I))if(auto* Text=C01QuickNames[I].Get()){Text->SetText(Name);Text->SetVisibility(Occupied&&!HasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);}
  if(C01QuickQuantities.IsValidIndex(I))if(auto* Text=C01QuickQuantities[I].Get())Text->SetText(Occupied?FText::AsNumber(Item.Quantity):FText::GetEmpty());
  if(C01QuickKeys.IsValidIndex(I))if(auto* Text=C01QuickKeys[I].Get())Text->SetText(Controller?Controller->GetActionKeyLabel(static_cast<EDWInputAction>(int32(EDWInputAction::SelectWeapon1)+I)):FText::AsNumber(I+1));
  if(C01QuickOutlines.IsValidIndex(I))if(auto* Outline=C01QuickOutlines[I].Get())Outline->SetBrush(DWC01UI::QuickSlotOutline(FVector2D(C01Safe(C01QuickSlotSize,92.f,64.f,128.f),C01Safe(C01QuickSlotSize,92.f,64.f,128.f)),C01QuickSlotOutlineThickness));
  Button->SetToolTipText(Occupied?FText::Format(DWText(this,TEXT("{0} × {1}"),TEXT("{0} x {1}")),Name,Item.Quantity):DWText(this,TEXT("空快捷栏"),TEXT("Empty quick slot")));
 }
}
bool UDWGameplayWidget::IsC01QuickDockPointer(FVector2D ScreenPosition)const
{
 return C01QuickDock&&C01QuickDock->GetVisibility()==ESlateVisibility::Visible&&HUD&&HUD->GetMenuPage()==EDWMenuPage::None&&
  !UDWWorldEventSubsystem::IsPlaying(this)&&GetC01CinematicOffset().IsNearlyZero(.5)&&C01QuickDock->GetTickSpaceGeometry().IsUnderLocation(ScreenPosition);
}


void UDWGameplayWidget::TickC01Feedback()
{
 if(!bUseC01HUD||!HUD||!ToastContainer)return;
 const double Now=FPlatformTime::Seconds();
 const FText Text=HUD->GetToast();const double Remaining=HUD->GetToastRemaining();
 if(!bC01FeedbackBaselineCaptured)
 {
  C01FeedbackBaseTransform=ToastContainer->GetRenderTransform();
  C01FeedbackBaseOpacity=ToastContainer->GetRenderOpacity();bC01FeedbackBaselineCaptured=true;
 }
 if(Text.IsEmpty()||Remaining<=0.)
 {
  C01LastFeedbackText=FText::GetEmpty();C01FeedbackPreviousRemaining=0.;
  ToastContainer->SetRenderTransform(C01FeedbackBaseTransform);
  ToastContainer->SetRenderOpacity(C01FeedbackBaseOpacity);
  ToastContainer->SetVisibility(ESlateVisibility::Collapsed);return;
 }
 // Same text may be submitted again; a renewed expiry is a new presentation event.
 // Tiny refreshes extend lifetime without a rapid flicker/restart.
 if(!Text.EqualTo(C01LastFeedbackText)||Remaining>C01FeedbackPreviousRemaining+.05)
 {C01LastFeedbackText=Text;C01FeedbackStarted=Now;}
 C01FeedbackPreviousRemaining=Remaining;
 const double Age=FMath::Max(0.,Now-C01FeedbackStarted);
 const float Enter=FMath::IsFinite(C01FeedbackEnterSeconds)?FMath::Max(0.f,C01FeedbackEnterSeconds):0.f;
 const float Exit=FMath::IsFinite(C01FeedbackExitSeconds)?FMath::Max(0.f,C01FeedbackExitSeconds):0.f;
 const float In=Enter>SMALL_NUMBER?DWC01UI::Ease(FMath::Clamp(float(Age/Enter),0.f,1.f),C01FeedbackCurve1,C01FeedbackCurve2):1.f;
 const float Out=Exit>SMALL_NUMBER?DWC01UI::Ease(FMath::Clamp(1.f-float(Remaining/Exit),0.f,1.f),C01FeedbackCurve1,C01FeedbackCurve2):0.f;
 const float Alpha=FMath::Clamp(FMath::Min(In,1.f-Out),0.f,1.f);
 const float Distance=FMath::IsFinite(C01FeedbackFloatDistance)?C01FeedbackFloatDistance:0.f;
 FWidgetTransform Transform=C01FeedbackBaseTransform;
 Transform.Translation.Y+=Distance*(1.f-In)-Distance*.35f*Out;
 ToastContainer->SetBrush(DWC01UI::Texture(TEXT("T_DW_C01_FeedbackToast_Paper_fine_edge_f23c937a"),FVector2D(356,68)));
 ToastContainer->SetBrushColor(FLinearColor::White);ToastContainer->SetPadding(FMargin(18,10));
 ToastContainer->SetRenderTransform(Transform);ToastContainer->SetRenderOpacity(C01FeedbackBaseOpacity*Alpha);
 ToastContainer->SetVisibility(ESlateVisibility::HitTestInvisible);
 if(ToastText)
 {
  ToastText->SetColorAndOpacity(FSlateColor(FLinearColor::White));
  ToastText->SetJustification(ETextJustify::Center);ToastText->SetAutoWrapText(true);
  FSlateFontInfo Font=ToastText->GetFont();Font.OutlineSettings.OutlineSize=0;ToastText->SetFont(Font);
  ToastText->SetShadowColorAndOpacity(FLinearColor::Transparent);ToastText->SetShadowOffset(FVector2D::ZeroVector);
 }
}


void UDWGameplayWidget::TickC01RuntimeMeters()
{
 auto* Player=GetPlayer();const auto* GI=GetDWGameInstance();
 const bool Active=bUseC01HUD&&HUD&&HUD->IsSessionStarted()&&
  IsValid(Player)&&!Player->IsDead()&&CombatHUD.bReady&&GI&&!GI->IsPlayerRestorePending()&&
  (!GI->GetSubsystem<UDWLoadingTransitionSubsystem>()||!GI->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive());
 const FVector2D Offset=GetC01CinematicOffset();
 if(C01PickupPanel)
 {
  const FText Item=Active?HUD->GetPickupToast():FText::GetEmpty();
  const double Age=Active?HUD->GetPickupToastAge():0.;
  if(Item.IsEmpty())
  {
   C01PickupPanel->SetVisibility(ESlateVisibility::Collapsed);
   if(C01PickupBounce)C01PickupBounce->ResetBounce();
   C01PreviousPickupAge=-1.;C01PreviousPickupText=FText::GetEmpty();
  }
  else
  {
   const bool NewEvent=C01PreviousPickupAge<0.||Age+.001<C01PreviousPickupAge||!Item.EqualTo(C01PreviousPickupText);
   C01PreviousPickupAge=Age;C01PreviousPickupText=Item;
   C01PickupLabel->SetText(Item);FSlateFontInfo Font=C01PickupLabel->GetFont();Font.Size=20;Font.OutlineSettings.OutlineSize=0;
   if(HUD->GameUIFont)Font.FontObject=HUD->GameUIFont;C01PickupLabel->SetFont(Font);
   const FVector2D HUDMargin=C01HUDMargin.ContainsNaN()?FVector2D(72,30):C01HUDMargin;
   if(auto* LayoutSlot=Cast<UCanvasPanelSlot>(C01PickupPanel->Slot))LayoutSlot->SetPosition(FVector2D(HUDMargin.X+Offset.X,-HUDMargin.Y-C01Safe(C01HUDDiameter,248.f,160.f,400.f)-16.f+Offset.Y));
   const float InSeconds=C01Safe(HUD->C01PickupEnterSeconds,.18f,0.f,10.f),OutSeconds=C01Safe(HUD->C01PickupExitSeconds,.18f,0.f,10.f);
   const float FadeIn=InSeconds>SMALL_NUMBER?FMath::Clamp(float(Age/InSeconds),0.f,1.f):1.f;
   const float FadeOut=OutSeconds>SMALL_NUMBER?FMath::Clamp(float(HUD->GetPickupToastRemaining()/OutSeconds),0.f,1.f):1.f;
   C01PickupPanel->SetRenderOpacity(FadeIn*FadeOut);C01PickupPanel->SetVisibility(ESlateVisibility::HitTestInvisible);
   if(NewEvent&&C01PickupBounce)
   {
    C01PickupBounce->FrequencyHz=1.6f/C01Safe(HUD->C01PickupBounceSeconds,.38f,.05f,5.f);
    C01PickupBounce->DampingRatio=C01Safe(HUD->C01PickupDampingRatio,.55f,.05f,3.f);
    C01PickupBounce->EntryScale=C01Safe(HUD->C01PickupEntryScale,.82f,.05f,1.f);
    C01PickupBounce->EntryOffset=FVector2D(0,12);C01PickupBounce->PlayEntrance();
   }
  }
 }
 if(!C01ShellDock||!C01ShellRow)return;
 const auto HideShells=[this]()
 {
  C01ShellDock->SetVisibility(ESlateVisibility::Collapsed);C01PreviousLoaded=INDEX_NONE;C01ShellPlayer.Reset();
  for(const auto& Bounce:C01ShellBounces)if(Bounce)Bounce->ResetBounce();
 };
 const bool ShowShells=Active&&HUD->GetMenuPage()==EDWMenuPage::None&&bShowShotgunOverhead&&!UDWWorldEventSubsystem::IsPlaying(this)&&Offset.IsNearlyZero(.5)&&
  DWCombatHUD::ShouldShowShotgunOverhead(Player)&&CombatHUD.bUsesMagazine&&CombatHUD.MagazineCapacity>0&&CombatHUD.MagazineCapacity<=128;
 if(!ShowShells){HideShells();return;}
 const auto* Capsule=Player->GetCapsuleComponent();FVector2D At;
 if(ShotgunHeadWorldOffset.ContainsNaN()||ShotgunHeadScreenOffset.ContainsNaN()||
  !UWidgetLayoutLibrary::ProjectWorldLocationToWidgetPosition(GetOwningPlayer(),Player->GetActorLocation()+FVector(0,0,Capsule?Capsule->GetScaledCapsuleHalfHeight():90.)+ShotgunHeadWorldOffset,At,true))
 {HideShells();return;}
 At+=ShotgunHeadScreenOffset;const FVector2D View=GetCachedGeometry().GetLocalSize();
 if(At.ContainsNaN()||View.X<=16||View.Y<=16||At.X<0||At.Y<0||At.X>View.X||At.Y>View.Y)
 {HideShells();return;}
 const int32 Capacity=CombatHUD.MagazineCapacity,Loaded=FMath::Clamp(CombatHUD.Loaded,0,Capacity);
 const uint64 RestoreEpoch=Player->WeaponAnimationComponent?Player->WeaponAnimationComponent->GetCombatWeaponRestoreEpoch():0;
 const bool SameContext=C01PreviousLoaded!=INDEX_NONE&&C01ShellPlayer.Get()==Player&&C01ShellRestoreEpoch==RestoreEpoch;
 FVector2D Size(C01Safe(ShotgunAmmoSlotSize.X,11.f,3.f,64.f),C01Safe(ShotgunAmmoSlotSize.Y,17.f,3.f,64.f));
 float Gap=C01Safe(ShotgunAmmoSlotGap,4.f,0.f,32.f);float Width=Capacity*Size.X+(Capacity-1)*Gap;
 const float Fit=FMath::Min(1.f,float((View.X-16.)/Width));Size*=Fit;Gap*=Fit;Width*=Fit;
 while(C01ShellCells.Num()<Capacity)
 {
  const int32 Index=C01ShellCells.Num();
  auto* Cell=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),FName(*FString::Printf(TEXT("C01ShellCell_%d"),Index)));
  Cell->SetPadding(FMargin(0));Cell->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* Bounce=C01MountPresentationBounce(this,Cell);
  auto* Sized=WidgetTree->ConstructWidget<USizeBox>();Sized->SetContent(Cell);C01ShellRow->AddChildToHorizontalBox(Sized);
  C01ShellCells.Add(Cell);C01ShellSizes.Add(Sized);C01ShellBounces.Add(Bounce);
 }
 for(int32 I=0;I<C01ShellCells.Num();++I)
 {
  auto* Cell=C01ShellCells[I].Get();auto* Sized=C01ShellSizes[I].Get();auto* Bounce=C01ShellBounces[I].Get();
  Sized->SetVisibility(I<Capacity?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
  if(I>=Capacity){if(Bounce)Bounce->ResetBounce();continue;}
  Sized->SetWidthOverride(Size.X);Sized->SetHeightOverride(Size.Y);
  if(auto* LayoutSlot=Cast<UHorizontalBoxSlot>(Sized->Slot))LayoutSlot->SetPadding(FMargin(0,0,I<Capacity-1?Gap:0,0));
  // Solid rounded rectangles, no dark backing, outline or artificial shells.
  Cell->SetBrush(FSlateRoundedBoxBrush(FLinearColor::White,C01Safe(ShotgunShellCornerRadius,3.f,0.f,16.f)*Fit));
  Cell->SetBrushColor(I<Loaded?ShotgunLoadedColor:ShotgunEmptyColor);
  if(Bounce)
  {
   Bounce->FrequencyHz=1.6f/C01Safe(ShotgunShellBounceSeconds,.32f,.05f,5.f);
   Bounce->DampingRatio=C01Safe(ShotgunShellDampingRatio,.55f,.05f,3.f);
   Bounce->EntryScale=C01Safe(ShotgunShellEntryScale,.72f,.05f,1.f);
   if(!SameContext||I>=Loaded)Bounce->ResetBounce();
   else if(I>=C01PreviousLoaded&&I<Loaded)Bounce->PlayEntrance();
  }
 }
 C01ShellPlayer=Player;C01ShellRestoreEpoch=RestoreEpoch;C01PreviousLoaded=Loaded;
 if(auto* LayoutSlot=Cast<UCanvasPanelSlot>(C01ShellDock->Slot)){LayoutSlot->SetPosition(At);LayoutSlot->SetSize(FVector2D(Width,Size.Y));}
 C01ShellDock->SetVisibility(ESlateVisibility::HitTestInvisible);
}
