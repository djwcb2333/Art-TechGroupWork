#include "DWUIGlowPulseComponent.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Components/PanelWidget.h"
#include "Components/WidgetSwitcher.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Widgets/SCompoundWidget.h"

namespace
{
float SafeRange(float Value,float Maximum)
{
 return FMath::IsFinite(Value)?FMath::Clamp(Value,0.f,Maximum):0.f;
}

bool IsPresented(const UWidget* Widget)
{
 for(int32 Depth=0;Widget&&Depth<64;++Depth)
 {
  if(!Widget->IsVisible()||Widget->GetRenderOpacity()<=KINDA_SMALL_NUMBER)return false;
  if(const UPanelWidget* Parent=Widget->GetParent())
  {
   if(const auto* Switcher=Cast<UWidgetSwitcher>(Parent))if(Switcher->GetActiveWidget()!=Widget)return false;
   Widget=Parent;
  }
  else
  {
   const auto* Tree=Cast<UWidgetTree>(Widget->GetOuter());
   Widget=Tree?Cast<UUserWidget>(Tree->GetOuter()):nullptr;
  }
 }
 return Widget==nullptr;
}

class SDWGlowPulse final : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SDWGlowPulse){} SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
 void Construct(const FArguments& Args,UDWUIGlowPulseComponent* InEffect)
 {
  Effect=InEffect;
  SetClipping(EWidgetClipping::Inherit);
  ChildSlot.HAlign(HAlign_Fill).VAlign(VAlign_Fill)[Args._Content.Widget];
 }
 virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool ParentEnabled) const override
 {
  const auto* EffectObject=Effect.Get();
  const FVector2D Size=Geometry.GetLocalSize();
  if(EffectObject&&Size.X>0.&&Size.Y>0.)
  {
   const FLinearColor StyleTint=Style.GetColorAndOpacityTint();
   const float Peak=FMath::Clamp(EffectObject->GetPulseOpacity()*SafeRange(EffectObject->GlowColor.A,1.f)*StyleTint.A,0.f,.98f);
   if(Peak>KINDA_SMALL_NUMBER)
   {
    // Gaussian-weighted concentric translucent fills form a soft fringe, not a solid
    // outline. Paint is BELOW the unchanged panel/text, so a white flash cannot wash out
    // the white lettering. Persistent brush storage keeps draw-element pointers valid.
    constexpr int32 LayerCount=24;
    const float Extent=SafeRange(EffectObject->HaloExtent,96.f);
    const float Radius=SafeRange(EffectObject->CornerRadius,96.f);
    float WeightSum=0.f;
    for(int32 Index=1;Index<=LayerCount;++Index)
    {const float Fraction=float(Index)/LayerCount;WeightSum+=FMath::Exp(-4.5f*Fraction*Fraction);}
    for(int32 Index=LayerCount;Index>=1;--Index)
    {
     const float Fraction=float(Index)/LayerCount,Spread=Extent*Fraction;
     const float Weight=FMath::Exp(-4.5f*Fraction*Fraction)/WeightSum;
     // Alpha compositing of all nested layers reaches exactly Peak at the inner edge.
     const float Alpha=1.f-FMath::Pow(1.f-Peak,Weight);
     FLinearColor Tint=EffectObject->GlowColor*StyleTint;Tint.A=Alpha;
     Brushes[Index-1]=FSlateRoundedBoxBrush(FLinearColor::White,Radius+Spread);
     const FGeometry GlowGeometry=Geometry.MakeChild(Size+FVector2D(Spread*2.),FSlateLayoutTransform(FVector2D(-Spread)));
     FSlateDrawElement::MakeBox(Out,Layer,GlowGeometry.ToPaintGeometry(),&Brushes[Index-1],ESlateDrawEffect::None,Tint);
    }
   }
  }
  return SCompoundWidget::OnPaint(Args,Geometry,Cull,Out,Layer+1,Style,ParentEnabled);
 }
private:
 TWeakObjectPtr<UDWUIGlowPulseComponent> Effect;
 mutable FSlateBrush Brushes[24];
};
}

TSharedRef<SWidget> UDWUIGlowPulseComponent::RebuildWidgetWithContent(TSharedRef<SWidget> Content)
{
 const TWeakPtr<SWidget> WeakContent(Content);
 auto Halo=SNew(SDWGlowPulse,this)[Content];
 Halo->SetVisibility(TAttribute<EVisibility>::CreateLambda([WeakContent](){const auto Child=WeakContent.Pin();return Child?Child->GetVisibility():EVisibility::Collapsed;}));
 Wrapper=Halo;RefreshPaint();return Halo;
}
void UDWUIGlowPulseComponent::OnPreConstruct(bool DesignTime){Super::OnPreConstruct(DesignTime);bRuntime=!DesignTime;if(DesignTime)ResetPulse();}
void UDWUIGlowPulseComponent::OnConstruct()
{
 Super::OnConstruct();bRuntime=GetOwner().IsValid()&&!GetOwner()->IsDesignTime();ResetPulse();
 if(bRuntime&&bPlayOnConstruct)Pulse();
}
void UDWUIGlowPulseComponent::OnDestruct(){bRuntime=false;ResetPulse();Super::OnDestruct();}
void UDWUIGlowPulseComponent::BeginDestroy(){bRuntime=false;ResetPulse();Wrapper.Reset();Super::BeginDestroy();}
void UDWUIGlowPulseComponent::StopTicker(){if(TickerHandle.IsValid()){FTSTicker::GetCoreTicker().RemoveTicker(TickerHandle);TickerHandle.Reset();}}
void UDWUIGlowPulseComponent::RefreshPaint()
{
 if(const auto Widget=Wrapper.Pin())
 {
  Widget->SetCullingBoundsExtension(FMargin(SafeRange(HaloExtent,96.f)+2.f));
  Widget->Invalidate(EInvalidateWidgetReason::Paint);
 }
}
void UDWUIGlowPulseComponent::ResetPulse(){StopTicker();bPulsing=false;Envelope=0.f;StartedAt=0.;RefreshPaint();}
float UDWUIGlowPulseComponent::GetPulseOpacity() const{return FMath::Clamp(Envelope*SafeRange(PeakOpacity,1.f)*PulseStrength,0.f,1.f);}
float UDWUIGlowPulseComponent::GetTotalSeconds() const{return SafeRange(RiseSeconds,5.f)+SafeRange(HoldSeconds,5.f)+SafeRange(FadeSeconds,5.f);}
void UDWUIGlowPulseComponent::Pulse(float Strength)
{
 ResetPulse();
 if(!bRuntime||!GetOwner().IsValid()||!IsPresented(GetOwner().Get())||GetTotalSeconds()<=SMALL_NUMBER)return;
 PulseStrength=SafeRange(Strength,4.f);if(PulseStrength<=0.f)return;
 bPulsing=true;StartedAt=FPlatformTime::Seconds();Envelope=SafeRange(RiseSeconds,5.f)>SMALL_NUMBER?0.f:1.f;
 RefreshPaint();TickerHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UDWUIGlowPulseComponent::TickPulse));
}
bool UDWUIGlowPulseComponent::TickPulse(float)
{
 if(!bRuntime||!GetOwner().IsValid()||!IsPresented(GetOwner().Get())||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().IsActive())
 {Envelope=0.f;bPulsing=false;TickerHandle.Reset();RefreshPaint();return false;}
 const float Rise=SafeRange(RiseSeconds,5.f),Hold=SafeRange(HoldSeconds,5.f),Fade=SafeRange(FadeSeconds,5.f);
 const float Age=FMath::Max(0.f,float(FPlatformTime::Seconds()-StartedAt));
 if(Age>=Rise+Hold+Fade){Envelope=0.f;bPulsing=false;TickerHandle.Reset();RefreshPaint();return false;}
 if(Rise>SMALL_NUMBER&&Age<Rise)
 {const float Phase=FMath::Clamp(Age/Rise,0.f,1.f);Envelope=Phase*Phase*(3.f-2.f*Phase);}
 else if(Age<Rise+Hold)Envelope=1.f;
 else{const float Phase=FMath::Clamp((Age-Rise-Hold)/FMath::Max(Fade,SMALL_NUMBER),0.f,1.f);Envelope=FMath::Square(1.f-Phase);}
 RefreshPaint();return true;
}
UDWUIGlowPulseComponent* UDWUIGlowPulseLibrary::GetGlowPulseComponent(UWidget* Widget)
{
 if(!Widget)return nullptr;
 auto* Owner=Widget->GetTypedOuter<UUserWidget>();
 auto* Extension=Owner?Owner->GetExtension<UUIComponentUserWidgetExtension>():nullptr;
 if(Extension&&Extension->IsContainerInitialized())for(auto* Component:Extension->GetComponentsFor(Widget))if(auto* Glow=Cast<UDWUIGlowPulseComponent>(Component))return Glow;
 return nullptr;
}
