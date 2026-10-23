#include "DWC01UIStyle.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "DWUIButtonMotionComponent.h"
#include "DWUIBounceComponent.h"
#include "DWUIBrushFillComponent.h"
#include "DWFrontendWidget.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/ButtonSlot.h"
#include "Components/TextBlock.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Blueprint/WidgetTree.h"
#include "Blueprint/UserWidget.h"
#include "Extensions/UIComponentContainer.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "UObject/UnrealType.h"
namespace DWC01UI
{
struct FButtonLabelSize { float Nominal=0.f; float Applied=0.f; };
FLinearColor Paper(){return FLinearColor::FromSRGBColor(FColor(214,203,169));}
FLinearColor Ink(){return FLinearColor::FromSRGBColor(FColor(90,81,64));}
FLinearColor Muted(){return FLinearColor::FromSRGBColor(FColor(155,144,113));}
FLinearColor Gold(){return FLinearColor::FromSRGBColor(FColor(196,161,96));}
FLinearColor Danger(){return FLinearColor::FromSRGBColor(FColor(185,99,69));}
void CenterButtonLabel(UButton* Button,float LatinOpticalOffsetEm)
{
 if(!Button)return;
 auto* Label=Cast<UTextBlock>(Button->GetContent());if(!Label)return;
 // SButton adds its style padding to UButtonSlot padding. Keep a single symmetric inset.
 auto Style=Button->GetStyle();
 if(Style.NormalPadding!=FMargin(0)||Style.PressedPadding!=FMargin(0))
 {Style.NormalPadding=Style.PressedPadding=FMargin(0);Button->SetStyle(Style);}
 auto* Slot=Cast<UButtonSlot>(Label->Slot);
 if(Slot)
 {
  if(Slot->GetPadding()!=FMargin(12,4))Slot->SetPadding(FMargin(12,4));
  if(Slot->GetHorizontalAlignment()!=HAlign_Fill)Slot->SetHorizontalAlignment(HAlign_Fill);
  if(Slot->GetVerticalAlignment()!=VAlign_Center)Slot->SetVerticalAlignment(VAlign_Center);
 }
 Label->SetJustification(ETextJustify::Center);Label->SetMargin(FMargin(0));
 if(Label->GetAutoWrapText())Label->SetAutoWrapText(false);
 if(Label->GetWrapTextAt()!=0)Label->SetWrapTextAt(0);
 Label->SetTextOverflowPolicy(ETextOverflowPolicy::Clip);
 // Latin Darumadrop has extra descent space. Align its visible ink, while preserving CJK metrics.
 const FString Value=Label->GetText().ToString();bool Latin=false,CJK=false;
 for(TCHAR C:Value){Latin|=(C>=TEXT('A')&&C<=TEXT('Z'))||(C>=TEXT('a')&&C<=TEXT('z'));CJK|=C>=0x2e80;}
 // Remember the user's nominal size: shrinking is runtime-only and reversible on wider layouts.
 static TMap<TWeakObjectPtr<UTextBlock>,FButtonLabelSize> Sizes;
 if(Sizes.Num()>512)for(auto It=Sizes.CreateIterator();It;++It)if(!It.Key().IsValid())It.RemoveCurrent();
 auto& Size=Sizes.FindOrAdd(Label);FSlateFontInfo Font=Label->GetFont();
 if(Size.Nominal==0||Font.Size!=Size.Applied)Size.Nominal=Font.Size;
 Font.Size=Size.Nominal;
 float Fit=1.f;
 const FVector2D Available=Button->GetTickSpaceGeometry().GetLocalSize();
 if(Available.X>24&&Available.Y>4&&FSlateApplication::IsInitialized())
 {
  const FVector2D Measured=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Label->GetText(),Font);
  // Fit long English/rebound key labels without changing their stored font or wrapping outside the art.
  if(Measured.X>0)Fit=FMath::Min(Fit,float((Available.X-24)/Measured.X));
  if(Measured.Y>0)Fit=FMath::Min(Fit,float((Available.Y-8)/Measured.Y));
  Fit=FMath::Clamp(Fit,.25f,1.f);
 }
 Size.Applied=FMath::Max(8.f,Size.Nominal*Fit);Font.Size=Size.Applied;
 if(Label->GetFont().Size!=Size.Applied)Label->SetFont(Font);
 const float Offset=Latin&&!CJK?-Size.Applied*FMath::Clamp(LatinOpticalOffsetEm,0.f,.3f):0.f;
 Label->SetRenderTransformPivot(FVector2D(.5,.5));
 FWidgetTransform Transform=Label->GetRenderTransform();
 const FVector2D Scale(1,1),Translation(0,Offset);
 if(!Transform.Scale.Equals(Scale,.001)||!Transform.Translation.Equals(Translation,.01))
 {Transform.Scale=Scale;Transform.Translation=Translation;Label->SetRenderTransform(Transform);}
}
FSlateBrush Texture(const TCHAR* Name,FVector2D Size)
{
 // Native brush resources also need GC-safe ownership outside reflected UMG properties.
 // The bounded process cache retains only explicitly requested C01 textures.
 static TMap<FString,TStrongObjectPtr<UTexture2D>> Cache;
 const FString Key(Name);auto* Found=Cache.Find(Key);
 if(!Found)
 {
  const FString Path=FString(TEXT("/Game/DoughWorld/UI/Image/"))+Key+TEXT(".")+Key;
  UTexture2D* Art=LoadObject<UTexture2D>(nullptr,*Path);
  Found=&Cache.Add(Key,TStrongObjectPtr<UTexture2D>(Art));
 }
 UTexture2D* Art=Found->Get();
 if(!Art)return FSlateRoundedBoxBrush(Paper(),12.f);
 FSlateBrush B;B.SetResourceObject(Art);B.DrawAs=ESlateBrushDrawType::Image;
 B.ImageSize=Size.IsNearlyZero()?FVector2D(Art->GetSizeX()*.5,Art->GetSizeY()*.5):Size;
 B.TintColor=FLinearColor::White;return B;
}
FSlateBrush Surface(FLinearColor Color,FVector2D Size)
{
 FSlateBrush B=Texture(Size.X/ FMath::Max(1.,Size.Y)<1.4?TEXT("T_DW_C01_ItemSlot_Slot_fill_40a49d6d"):TEXT("T_DW_C01_Button_Paper_fine_edge_e73e9820"),Size);
 const auto Source=Paper();
 B.TintColor=FLinearColor(Color.R/FMath::Max(.001f,Source.R),Color.G/FMath::Max(.001f,Source.G),Color.B/FMath::Max(.001f,Source.B),Color.A);
 return B;
}
float Ease(float Alpha,FVector2D P1,FVector2D P2)
{
 const float X=FMath::Clamp(Alpha,0.f,1.f);
 if(X<=0.f||X>=1.f)return X;
 const auto Bezier=[](float T,float A,float B){const float U=1.f-T;return 3.f*U*U*T*A+3.f*U*T*T*B+T*T*T;};
 float Lo=0.f,Hi=1.f;
 for(int32 I=0;I<18;++I){const float Mid=(Lo+Hi)*.5f;if(Bezier(Mid,float(P1.X),float(P2.X))<X)Lo=Mid;else Hi=Mid;}
 return Bezier((Lo+Hi)*.5f,float(P1.Y),float(P2.Y));
}
void AttachMotion(UButton* Button,bool bItemSlot,bool bDuringOwnerInitialize)
{
 if(!Button||Button->IsDesignTime())return;
 // The old true/Lift call site now means an item choice: color feedback only.
 if(bItemSlot){DisableSlotMotion(Button);return;}
 auto* Owner=Button->GetTypedOuter<UUserWidget>();if(!Owner)return;
 auto* Extension=Owner->GetExtension<UUIComponentUserWidgetExtension>();
 const bool NewExtension=Extension==nullptr;
 if(!Extension)Extension=Owner->AddExtension<UUIComponentUserWidgetExtension>();
 if(!Extension)return;
 UUIComponentContainer* Container=nullptr;
 if(const auto* Property=FindFProperty<FObjectPropertyBase>(Extension->GetClass(),TEXT("ComponentContainer")))
  Container=Cast<UUIComponentContainer>(Property->GetObjectPropertyValue_InContainer(Extension));
 if(!Container)
 {Container=NewObject<UUIComponentContainer>(Extension);Extension->InitializeContainer(Container);}
 UDWUIBounceComponent* ExistingBounce=nullptr;
 // Remove only the retired runtime Press/Lift effects, including Designer-authored instances.
 // Zero their shadow before resetting so a previously built Slate wrapper stays invisible.
 const auto Components=Extension->GetComponentsFor(Button);
 for(UUIComponent* C:Components)
 {
  if(auto* Motion=Cast<UDWUIButtonMotionComponent>(C))
  {
   Motion->bAutoBindButtons=false;Motion->bAnimateNonButtonHover=false;
   if(auto* Lift=Cast<UDWUIButtonLiftComponent>(Motion))Lift->RestShadowOpacity=Lift->HoverShadowOpacity=0.f;
   if(auto* Press=Cast<UDWUIButtonPressComponent>(Motion))Press->ShadowColor.A=0.f;
   Motion->RestoreImmediately();Motion->Destruct();Container->RemoveComponent(Button->GetFName(),C);
  }
  else if(auto* Bounce=Cast<UDWUIBounceComponent>(C))ExistingBounce=Bounce;
 }
 if(NewExtension&&bDuringOwnerInitialize)Extension->Initialize();
 const FName Name=Button->GetFName();
 if(Owner->IsA<UDWFrontendWidget>()&&(Name==TEXT("StartButton")||Name==TEXT("TitleSettingsButton")||Name==TEXT("QuitButton")))
 {
  if(!UDWUIBrushFillLibrary::GetBrushFillComponent(Button)&&!Button->GetCachedWidget().IsValid())
  {
   auto* Fill=NewObject<UDWUIBrushFillComponent>(Container);
   Container->AddComponent(Name,Fill);Fill->Initialize(Button);Button->bWrappedByComponent=true;
   // Components wrap in array order: paint first, then the preserved spring outside both paint and text.
   if(ExistingBounce)Container->MoveComponent(Name,UDWUIBrushFillComponent::StaticClass(),UDWUIBounceComponent::StaticClass(),false);
   if(Owner->IsConstructed()){Fill->PreConstruct(false);Fill->Construct();}
  }
 }
 // Preserve every existing Designer-authored spring, hover, press and entrance parameter.
 if(ExistingBounce)return;
 if(Button->GetCachedWidget().IsValid())
 {UE_LOG(LogTemp,Warning,TEXT("C03 bounce skipped late mount: %s"),*Button->GetPathName());return;}
 auto* Bounce=NewObject<UDWUIBounceComponent>(Container);
 // Existing page/title entrance owns the reveal; buttons use the original spring interaction.
 Bounce->bPlayOnConstruct=false;
 Container->AddComponent(Button->GetFName(),Bounce);Bounce->Initialize(Button);
 Button->bWrappedByComponent=true;
 if(Owner->IsConstructed()){Bounce->PreConstruct(false);Bounce->Construct();}
}
void DisableSlotMotion(UButton* Button)
{
 if(!Button||Button->IsDesignTime())return;
 auto* Owner=Button->GetTypedOuter<UUserWidget>();if(!Owner)return;
 auto* Extension=Owner->GetExtension<UUIComponentUserWidgetExtension>();if(!Extension)return;
 auto* Property=FindFProperty<FObjectPropertyBase>(Extension->GetClass(),TEXT("ComponentContainer"));
 auto* Container=Property?Cast<UUIComponentContainer>(Property->GetObjectPropertyValue_InContainer(Extension)):nullptr;
 if(!Container)return;
 // Slot hover is brightness only. Never leave an entrance, held press or bound spring behind.
 const auto Components=Extension->GetComponentsFor(Button);
 for(UUIComponent* C:Components)
 {
  if(auto* Motion=Cast<UDWUIButtonMotionComponent>(C))
  {
   Motion->bAutoBindButtons=false;Motion->bAnimateNonButtonHover=false;
   if(auto* Lift=Cast<UDWUIButtonLiftComponent>(Motion))Lift->RestShadowOpacity=Lift->HoverShadowOpacity=0.f;
   if(auto* Press=Cast<UDWUIButtonPressComponent>(Motion))Press->ShadowColor.A=0.f;
   Motion->RestoreImmediately();Motion->Destruct();Container->RemoveComponent(Button->GetFName(),C);
  }
  else if(auto* Bounce=Cast<UDWUIBounceComponent>(C))
  {
   Bounce->bPlayOnConstruct=false;Bounce->bAutoBindButtons=false;Bounce->bAnimateNonButtonHover=false;
   Bounce->ResetBounce();Bounce->Destruct();Container->RemoveComponent(Button->GetFName(),C);
  }
 }
}
FSlateBrush QuickSlotOutline(FVector2D Size,float Thickness)
{
 const float Width=FMath::IsFinite(Thickness)?FMath::Clamp(Thickness,0.f,12.f):3.f;
 FSlateRoundedBoxBrush Brush(FLinearColor::Transparent,float(FMath::Min(Size.X,Size.Y))*.25f,FLinearColor::White,Width,Size);
 Brush.OutlineSettings.bUseBrushTransparency=false;return Brush;
}
FSlateBrush QuickSlotFill(FVector2D Size)
{return FSlateRoundedBoxBrush(FLinearColor::White,float(FMath::Min(Size.X,Size.Y))*.25f,Size);}
void ReleaseMotionTree(UWidget* Widget)
{
 if(!Widget)return;
 if(auto* User=Cast<UUserWidget>(Widget))
 {if(User->WidgetTree)User->WidgetTree->ForEachWidget([](UWidget* Child){ReleaseMotionTree(Child);});}
 if(auto* Panel=Cast<UPanelWidget>(Widget))for(UWidget* Child:Panel->GetAllChildren())ReleaseMotionTree(Child);
 auto* Owner=Widget->GetTypedOuter<UUserWidget>();if(!Owner)return;
 auto* Extension=Owner->GetExtension<UUIComponentUserWidgetExtension>();if(!Extension)return;
 auto* Property=FindFProperty<FObjectPropertyBase>(Extension->GetClass(),TEXT("ComponentContainer"));
 auto* Container=Property?Cast<UUIComponentContainer>(Property->GetObjectPropertyValue_InContainer(Extension)):nullptr;
 if(!Container)return;
 for(UUIComponent* C:Extension->GetComponentsFor(Widget))
  if(Cast<UDWUIButtonMotionComponent>(C)||Cast<UDWUIBounceComponent>(C)||Cast<UDWUIBrushFillComponent>(C))
  {C->Destruct();Container->RemoveComponent(Widget->GetFName(),C);}
}
}
