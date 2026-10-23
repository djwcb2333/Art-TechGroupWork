#include "DWUIBrushFillComponent.h"
#include "DWC01UIStyle.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Components/WidgetSwitcher.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Widgets/SCompoundWidget.h"
#include "Rendering/DrawElements.h"

namespace
{
class SDWBrushFill final:public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SDWBrushFill){} SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
 void Construct(const FArguments& Args,UDWUIBrushFillComponent* InEffect)
 {
  Effect=InEffect;Brush=DWC01UI::Surface(FLinearColor::White);
  ChildSlot.HAlign(HAlign_Fill).VAlign(VAlign_Fill)[Args._Content.Widget];
 }
 virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Cull,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled)const override
 {
  const auto* C=Effect.Get();const FVector2D Size=G.GetLocalSize();
  if(C&&C->GetFillProgress()>0.f&&Size.X>0&&Size.Y>0)
  {
   const int32 N=FMath::Clamp(C->StrokeCount,1,12),Bristles=6;
   const float P=C->GetFillProgress(),Stagger=FMath::Clamp(C->StrokeStagger,0.f,1.f);
   const FLinearColor Tint=C->FillColor*Brush.GetTint(Style)*Style.GetColorAndOpacityTint();
   if(P>=1.f)FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),&Brush,ESlateDrawEffect::None,Tint);
   else for(int32 Row=0;Row<N;++Row)
   {
    const float Lane=FMath::Clamp(P*(1.f+Stagger)-Stagger*Row/FMath::Max(1,N-1),0.f,1.f);
    if(Lane<=0.f)continue;
    for(int32 Hair=0;Hair<Bristles;++Hair)
    {
     const float Y=Size.Y*(Row*Bristles+Hair)/(N*Bristles),H=Size.Y/(N*Bristles);
     const float Ragged=FMath::Sin(float(Row*17+Hair*7))*FMath::Clamp(C->BristleEdgePixels,0.f,12.f);
     const float Width=Lane>=1.f?Size.X:FMath::Clamp(Lane*Size.X+Ragged,0.f,float(Size.X));
     if(Width<=0)continue;
     const FGeometry Clip=G.MakeChild(FVector2D(Width,H),FSlateLayoutTransform(FVector2D(0,Y)));
     Out.PushClip(FSlateClippingZone(Clip));
     FSlateDrawElement::MakeBox(Out,Layer,G.ToPaintGeometry(),&Brush,ESlateDrawEffect::None,Tint);
     Out.PopClip();
    }
   }
  }
  return SCompoundWidget::OnPaint(Args,G,Cull,Out,Layer+1,Style,ParentEnabled);
 }
private:TWeakObjectPtr<UDWUIBrushFillComponent> Effect;FSlateBrush Brush;
};
bool VisibleEnabled(const UWidget* W)
{
 for(int32 Depth=0;W&&Depth<64;++Depth)
 {
  if(!W->IsVisible()||!W->GetIsEnabled())return false;
  if(W->GetParent())
  {
   if(const auto* Switcher=Cast<UWidgetSwitcher>(W->GetParent()))if(Switcher->GetActiveWidget()!=W)return false;
   W=W->GetParent();
  }
  else{const auto* Tree=Cast<UWidgetTree>(W->GetOuter());W=Tree?Cast<UUserWidget>(Tree->GetOuter()):nullptr;}
 }
 return W==nullptr;
}
}
TSharedRef<SWidget> UDWUIBrushFillComponent::RebuildWidgetWithContent(TSharedRef<SWidget> Content)
{
 const TWeakPtr<SWidget> WeakContent(Content);
 auto Paint=SNew(SDWBrushFill,this)[Content];
 Paint->SetVisibility(TAttribute<EVisibility>::CreateLambda([WeakContent](){auto W=WeakContent.Pin();return W?W->GetVisibility():EVisibility::Collapsed;}));
 Wrapper=Paint;return Paint;
}
void UDWUIBrushFillComponent::OnPreConstruct(bool Design){Super::OnPreConstruct(Design);bRuntime=!Design;if(Design)ResetFill();}
void UDWUIBrushFillComponent::OnConstruct()
{
 Super::OnConstruct();bRuntime=GetOwner().IsValid()&&!GetOwner()->IsDesignTime();ResetFill();UnbindButton();
 if(bRuntime&&bAutoBindButtons)if(auto* B=Cast<UButton>(GetOwner().Get()))
 {BoundButton=B;B->OnHovered.AddUniqueDynamic(this,&UDWUIBrushFillComponent::HandleHovered);B->OnUnhovered.AddUniqueDynamic(this,&UDWUIBrushFillComponent::HandleUnhovered);}
}
void UDWUIBrushFillComponent::UnbindButton()
{
 if(auto* B=BoundButton.Get()){B->OnHovered.RemoveDynamic(this,&UDWUIBrushFillComponent::HandleHovered);B->OnUnhovered.RemoveDynamic(this,&UDWUIBrushFillComponent::HandleUnhovered);}BoundButton.Reset();
}
void UDWUIBrushFillComponent::OnDestruct(){bRuntime=false;UnbindButton();ResetFill();Super::OnDestruct();}
void UDWUIBrushFillComponent::BeginDestroy(){bRuntime=false;UnbindButton();StopTicker();Wrapper.Reset();Super::BeginDestroy();}
void UDWUIBrushFillComponent::StopTicker(){if(TickerHandle.IsValid()){FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);TickerHandle.Reset();}}
void UDWUIBrushFillComponent::ResetFill(){StopTicker();bHoveredState=false;Progress=StartProgress=TargetProgress=0.f;if(auto W=Wrapper.Pin())W->Invalidate(EInvalidateWidgetReason::Paint);}
void UDWUIBrushFillComponent::SetHovered(bool Hovered)
{
 if(!bRuntime)return;
 if(bHoveredState==Hovered&&FMath::IsNearlyEqual(TargetProgress,Hovered?1.f:0.f))return;
 bHoveredState=Hovered;StartProgress=Progress;TargetProgress=Hovered?1.f:0.f;
 const float Seconds=Hovered?FillSeconds:ClearSeconds;Duration=FMath::IsFinite(Seconds)?FMath::Max(0.f,Seconds):0.f;StartedAt=FPlatformTime::Seconds();
 if(Duration<=SMALL_NUMBER)
 {
  Progress=TargetProgress;if(auto W=Wrapper.Pin())W->Invalidate(EInvalidateWidgetReason::Paint);
  if(!Hovered){StopTicker();return;}
 }
 if(!TickerHandle.IsValid())TickerHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UDWUIBrushFillComponent::TickFill));
}
bool UDWUIBrushFillComponent::TickFill(float)
{
 if(!bRuntime||!GetOwner().IsValid()||!VisibleEnabled(GetOwner().Get())||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().IsActive())
 {Progress=StartProgress=TargetProgress=0.f;bHoveredState=false;TickerHandle.Reset();if(auto W=Wrapper.Pin())W->Invalidate(EInvalidateWidgetReason::Paint);return false;}
 const float A=FMath::Clamp(float((FPlatformTime::Seconds()-StartedAt)/FMath::Max(Duration,.001f)),0.f,1.f);
 Progress=FMath::Lerp(StartProgress,TargetProgress,A);if(auto W=Wrapper.Pin())W->Invalidate(EInvalidateWidgetReason::Paint);
 // Keep watching while hovered, so hiding the page or switching application cannot retain stale paint.
 if(A>=1.f&&!bHoveredState){TickerHandle.Reset();return false;}return true;
}
UDWUIBrushFillComponent* UDWUIBrushFillLibrary::GetBrushFillComponent(UWidget* Widget)
{
 if(!Widget)return nullptr;auto* Owner=Widget->GetTypedOuter<UUserWidget>();auto* Extension=Owner?Owner->GetExtension<UUIComponentUserWidgetExtension>():nullptr;
 if(Extension)for(auto* C:Extension->GetComponentsFor(Widget))if(auto* Fill=Cast<UDWUIBrushFillComponent>(C))return Fill;
 return nullptr;
}
