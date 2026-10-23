#include "DWUIButtonMotionComponent.h"
#include "Components/Button.h"
#include "Components/PanelWidget.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "Rendering/DrawElements.h"
#include "Slate/WidgetTransform.h"
#include "Widgets/Layout/SBox.h"
#include "Layout/Children.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Extensions/UIComponentUserWidgetExtension.h"

namespace
{
template<class T> T* FindButtonMotion(UWidget* Widget)
{
    if(!IsValid(Widget))return nullptr;
    // Match UWidget::TakeWidget_Private exactly: a component belongs to this target's tree.
    const auto* Tree=Cast<UWidgetTree>(Widget->GetOuter());
    const auto* Owner=Tree?Cast<UUserWidget>(Tree->GetOuter()):nullptr;
    auto* Extension=Owner?Owner->GetExtension<UUIComponentUserWidgetExtension>():nullptr;
    if(!Extension||!Extension->IsContainerInitialized())return nullptr;
    auto* Component=Cast<T>(Extension->GetComponent(T::StaticClass(),Widget->GetFName()));
    return Component&&Component->GetOwner().Get()==Widget?Component:nullptr;
}
}

UDWUIButtonLiftComponent* UDWUIButtonMotionLibrary::GetLiftComponent(UWidget* Widget)
{return FindButtonMotion<UDWUIButtonLiftComponent>(Widget);}
UDWUIButtonPressComponent* UDWUIButtonMotionLibrary::GetPressComponent(UWidget* Widget)
{return FindButtonMotion<UDWUIButtonPressComponent>(Widget);}

class SDWUIButtonMotionBox : public SBox
{
public:
    SLATE_BEGIN_ARGS(SDWUIButtonMotionBox){} SLATE_DEFAULT_SLOT(FArguments,Content) SLATE_END_ARGS()
    TWeakObjectPtr<UDWUIButtonMotionComponent> Component;
    TWeakPtr<SWidget> SourceContent;
    void Construct(const FArguments& Args,UDWUIButtonMotionComponent* InComponent)
    {
        Component=InComponent;
        SourceContent=Args._Content.Widget;
        SBox::Construct(SBox::FArguments().HAlign(HAlign_Fill).VAlign(VAlign_Fill)[Args._Content.Widget]);
    }
    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Cull,
        FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        if(const auto* C=Component.Get())
        {
            float Scale,Blur,Radius;FVector2D Offset,ShadowOffset;FLinearColor Color;int32 Count;
            C->GetPresentation(C->GetCurrentProgress(),Scale,Offset,ShadowOffset,Blur,Radius,Color,Count);
            Color.A=FMath::Clamp(Color.A,0.f,1.f)*Style.GetColorAndOpacityTint().A;
            // The child SButton applies its own disabled effect, while this outer shadow
            // is separately painted. Keep a disabled control's shadow equally subdued.
            const UWidget* OwnerWidget=C->GetOwner().Get();
            if(!OwnerWidget)Color.A=0.f;
            else if(!bParentEnabled||!OwnerWidget->GetIsEnabled())Color.A*=.25f;
            FGeometry ShadowGeometry=G;
            // Existing DW Bounce/Offscreen wrappers have one fill child and no layout padding.
            // Read their transforms and the original button transform/opacity so the shadow
            // follows an authored entrance/fade too. Never descend into the button's label.
            TSharedPtr<SWidget> Inner=SourceContent.Pin();
            for(int32 Depth=0;Inner.IsValid()&&Depth<16;++Depth)
            {
                Color.A*=Inner->GetRenderOpacity();
                if(const auto& Transform=Inner->GetRenderTransform();Transform.IsSet())
                    ShadowGeometry=ShadowGeometry.MakeChild(Transform.GetValue(),Inner->GetRenderTransformPivot());
                if(Inner->GetTypeAsString()==TEXT("SButton"))break;
                FChildren* Children=Inner->GetChildren();
                if(!Children||Children->Num()!=1)break;
                Inner=Children->GetChildAt(0);
            }
            const FVector2D Size=G.GetLocalSize();
            const auto Draw=[&](float Spread,float Alpha)
            {
                if(Alpha<=.00001f)return;
                FSlateRoundedBoxBrush Brush(FLinearColor::White,FMath::Max(0.f,Radius+Spread));
                FLinearColor Tint=Color;Tint.A=Alpha;
                FSlateDrawElement::MakeBox(Out,Layer,ShadowGeometry.ToPaintGeometry(FVector2f(Size+FVector2D(2*Spread)),
                    FSlateLayoutTransform(FVector2f(ShadowOffset-FVector2D(Spread)))),&Brush,ESlateDrawEffect::None,Tint);
            };
            if(Blur<=KINDA_SMALL_NUMBER)Draw(0.f,Color.A);
            else
            {
                // Approximate CSS blur by nested rounded-box Gaussian shells. No offscreen RT.
                Count=FMath::Clamp(Count,4,24);
                const float Sigma=FMath::Max(.25f,Blur*.5f);
                float PreviousWeight=0.f,AccumulatedAlpha=0.f;
                for(int32 I=Count;I>=0;--I)
                {
                    const float Spread=3.f*Sigma*float(I)/Count;
                    const float Weight=FMath::Exp(-.5f*FMath::Square(Spread/Sigma));
                    const float DesiredIncrement=(Weight-PreviousWeight)*Color.A;
                    const float Alpha=FMath::Clamp(DesiredIncrement/FMath::Max(.0001f,1.f-AccumulatedAlpha),0.f,1.f);
                    Draw(Spread,Alpha);
                    AccumulatedAlpha+=DesiredIncrement;PreviousWeight=Weight;
                }
            }
        }
        return SBox::OnPaint(Args,G,Cull,Out,Layer+1,Style,bParentEnabled);
    }
};

float UDWUIButtonMotionLibrary::EvaluateCSSEase(float Time01,FVector2D C1,FVector2D C2)
{
    const double X=FMath::IsFinite(Time01)?FMath::Clamp(double(Time01),0.,1.):1.;
    if(X<=0.||X>=1.)return float(X);
    C1.X=FMath::IsFinite(C1.X)?FMath::Clamp(C1.X,0.,1.):.25;
    C2.X=FMath::IsFinite(C2.X)?FMath::Clamp(C2.X,0.,1.):.25;
    C1.Y=FMath::IsFinite(C1.Y)?C1.Y:.1;C2.Y=FMath::IsFinite(C2.Y)?C2.Y:1.;
    const auto B=[](double T,double A,double B){const double V=1.-T;return 3.*V*V*T*A+3.*V*T*T*B+T*T*T;};
    // Bisection is robust when the derivative is zero; X control coordinates obey CSS [0,1].
    double Low=0.,High=1.;
    for(int32 I=0;I<25;++I){const double T=(Low+High)*.5;if(B(T,C1.X,C2.X)<X)Low=T;else High=T;}
    return float(B((Low+High)*.5,C1.Y,C2.Y));
}

TSharedRef<SWidget> UDWUIButtonMotionComponent::RebuildWidgetWithContent(TSharedRef<SWidget> Content)
{
    const TWeakPtr<SWidget> WeakChild(Content);
    auto Box=SNew(SDWUIButtonMotionBox,this)[Content];
    Wrapper=Box;
    Box->SetVisibility(TAttribute<EVisibility>::CreateLambda([WeakChild]()
    {const auto Child=WeakChild.Pin();return Child?Child->GetVisibility():EVisibility::Collapsed;}));
    Box->SetOnMouseEnter(FNoReplyPointerEventHandler::CreateWeakLambda(this,[this](const FGeometry&,const FPointerEvent&)
    {if(bAnimateNonButtonHover&&!Cast<UButton>(GetOwner().Get()))SetHovered(true);}));
    Box->SetOnMouseLeave(FSimpleNoReplyPointerEventHandler::CreateWeakLambda(this,[this](const FPointerEvent&)
    {if(bAnimateNonButtonHover&&!Cast<UButton>(GetOwner().Get())){SetHovered(false);SetPressed(false);}}));
    Apply();
    if(bConstructed&&(bTransitioning||bHoveredState||bPressedState))EnsureTicker();
    return Box;
}

void UDWUIButtonMotionComponent::OnPreConstruct(bool bInDesignTime)
{Super::OnPreConstruct(bInDesignTime);bDesignTime=bInDesignTime;if(bDesignTime)RestoreImmediately();}
void UDWUIButtonMotionComponent::OnConstruct()
{
    Super::OnConstruct();bDesignTime=!GetOwner().IsValid()||GetOwner()->IsDesignTime();bConstructed=!bDesignTime;
    RestoreImmediately();BindButton();
}
void UDWUIButtonMotionComponent::OnDestruct()
{bConstructed=false;UnbindButton();RestoreImmediately();Super::OnDestruct();}
void UDWUIButtonMotionComponent::BeginDestroy()
{
    bConstructed=false;StopTicker();UnbindButton();
    if(auto Box=Wrapper.Pin())Box->SetRenderTransform(TOptional<FSlateRenderTransform>());
    Wrapper.Reset();Super::BeginDestroy();
}
bool UDWUIButtonMotionComponent::CanAnimate() const
{return bConstructed&&!bDesignTime&&GetOwner().IsValid()&&!GetOwner()->IsDesignTime();}
bool UDWUIButtonMotionComponent::CanInteract() const
{
    if(!CanAnimate()||(FSlateApplication::IsInitialized()&&!FSlateApplication::Get().IsActive()))return false;
    const UWidget* W=GetOwner().Get();
    for(int32 Depth=0;W&&Depth<64;++Depth)
    {
        if(!W->GetIsEnabled()||!W->IsVisible())return false;
        if(const auto* Parent=W->GetParent())W=Parent;
        else
        {
            // Cross a nested UserWidget's root boundary as Slate does.
            const auto* Tree=Cast<UWidgetTree>(W->GetOuter());
            W=Tree?Cast<UUserWidget>(Tree->GetOuter()):nullptr;
        }
    }
    return W==nullptr;
}
void UDWUIButtonMotionComponent::BindButton()
{
    UnbindButton();auto* B=bAutoBindButtons&&CanAnimate()?Cast<UButton>(GetOwner().Get()):nullptr;
    if(!B)return;BoundButton=B;
    B->OnHovered.AddUniqueDynamic(this,&UDWUIButtonMotionComponent::HandleHovered);
    B->OnUnhovered.AddUniqueDynamic(this,&UDWUIButtonMotionComponent::HandleUnhovered);
    B->OnPressed.AddUniqueDynamic(this,&UDWUIButtonMotionComponent::HandlePressed);
    B->OnReleased.AddUniqueDynamic(this,&UDWUIButtonMotionComponent::HandleReleased);
}
void UDWUIButtonMotionComponent::UnbindButton()
{
    if(auto* B=BoundButton.Get())
    {
        B->OnHovered.RemoveDynamic(this,&UDWUIButtonMotionComponent::HandleHovered);
        B->OnUnhovered.RemoveDynamic(this,&UDWUIButtonMotionComponent::HandleUnhovered);
        B->OnPressed.RemoveDynamic(this,&UDWUIButtonMotionComponent::HandlePressed);
        B->OnReleased.RemoveDynamic(this,&UDWUIButtonMotionComponent::HandleReleased);
    }
    BoundButton.Reset();
}
void UDWUIButtonMotionComponent::SetHovered(bool bHovered)
{
    if(!CanAnimate())return;
    bHoveredState=bHovered&&CanInteract();if(!bHoveredState)bPressedState=false;
    Retarget();
}
void UDWUIButtonMotionComponent::SetPressed(bool bPressed)
{if(!CanAnimate())return;bPressedState=bPressed&&CanInteract();Retarget();}
void UDWUIButtonMotionComponent::HandleHovered(){SetHovered(true);}
void UDWUIButtonMotionComponent::HandleUnhovered(){SetHovered(false);}
void UDWUIButtonMotionComponent::HandlePressed(){SetPressed(true);}
void UDWUIButtonMotionComponent::HandleReleased(){SetPressed(false);}
void UDWUIButtonMotionComponent::RestoreImmediately()
{
    StopTicker();bHoveredState=bPressedState=bTransitioning=false;Current=Start=Target=0.f;Apply();
}
void UDWUIButtonMotionComponent::Sample(double Now)
{
    if(!bTransitioning)return;
    const float U=Duration<=KINDA_SMALL_NUMBER?1.f:FMath::Clamp(float((Now-StartedAt)/Duration),0.f,1.f);
    Current=FMath::Lerp(Start,Target,UDWUIButtonMotionLibrary::EvaluateCSSEase(U,CurveControl1,CurveControl2));
    if(U>=1.f){Current=Target;bTransitioning=false;}
}
void UDWUIButtonMotionComponent::Retarget()
{
    const double Now=FPlatformTime::Seconds();Sample(Now);
    const bool bActive=WantsActive();const float Next=bActive?1.f:0.f;
    if(Next!=Target)
    {
        Start=Current;Target=Next;StartedAt=Now;
        const float Requested=DurationFor(bActive);
        Duration=FMath::IsFinite(Requested)?FMath::Max(0.f,Requested):0.f;
        bTransitioning=Duration>KINDA_SMALL_NUMBER&&Start!=Target;
        if(!bTransitioning)Current=Target;
    }
    Apply();if(bTransitioning||bHoveredState||bPressedState)EnsureTicker();
}
void UDWUIButtonMotionComponent::EnsureTicker()
{
    if(!CanAnimate()||!Wrapper.IsValid()||Handle.IsValid())return;
    const TWeakObjectPtr<UDWUIButtonMotionComponent> Weak(this);
    Handle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak](float Dt)
    {auto* C=Weak.Get();return C?C->TickMotion(Dt):false;}));
}
void UDWUIButtonMotionComponent::StopTicker()
{if(Handle.IsValid()&&!bInsideTick)FTSTicker::RemoveTicker(Handle);Handle.Reset();}
bool UDWUIButtonMotionComponent::TickMotion(float UnusedDelta)
{
    (void)UnusedDelta;bInsideTick=true;
    if(!CanAnimate()||!Wrapper.IsValid()){Handle.Reset();bInsideTick=false;return false;}
    if(!CanInteract()&&(bHoveredState||bPressedState))
    {bHoveredState=bPressedState=false;Retarget();}
    Sample(FPlatformTime::Seconds());Apply();
    const bool bKeep=bTransitioning||bHoveredState||bPressedState;
    if(!bKeep)Handle.Reset();bInsideTick=false;return bKeep;
}
void UDWUIButtonMotionComponent::Apply()
{
    if(auto Box=Wrapper.Pin())
    {
        float Scale,Blur,Radius;FVector2D Offset,ShadowOffset;FLinearColor Color;int32 Layers;
        GetPresentation(Current,Scale,Offset,ShadowOffset,Blur,Radius,Color,Layers);
        Box->SetRenderTransformPivot(Pivot);
        if(Scale==1.f&&Offset.IsZero())Box->SetRenderTransform(TOptional<FSlateRenderTransform>());
        else Box->SetRenderTransform(TOptional<FSlateRenderTransform>(FWidgetTransform(Offset,FVector2D(Scale),FVector2D::ZeroVector,0).ToSlateRenderTransform()));
        Box->Invalidate(EInvalidateWidgetReason::Paint);
    }
}
float UDWUIButtonMotionComponent::GetCurrentScale() const
{float S,B,R;FVector2D O,SO;FLinearColor C;int32 L;GetPresentation(Current,S,O,SO,B,R,C,L);return S;}
FVector2D UDWUIButtonMotionComponent::GetCurrentOffset() const
{float S,B,R;FVector2D O,SO;FLinearColor C;int32 L;GetPresentation(Current,S,O,SO,B,R,C,L);return O;}
void UDWUIButtonMotionComponent::GetPresentation(float Alpha,float& Scale,FVector2D& Offset,FVector2D& ShadowOffset,
    float& Blur,float& Radius,FLinearColor& Color,int32& Layers) const
{Scale=1.f;Offset=ShadowOffset=FVector2D::ZeroVector;Blur=Radius=0.f;Color=FLinearColor::Transparent;Layers=1;}
void UDWUIButtonLiftComponent::GetPresentation(float A,float& Scale,FVector2D& Offset,FVector2D& ShadowOffset,
    float& Blur,float& Radius,FLinearColor& Color,int32& Layers) const
{
    A=FMath::Clamp(A,0.f,1.f);Scale=1.f;Offset=FVector2D(0,-LiftDistance*A);
    ShadowOffset=FMath::Lerp(RestShadowOffset,HoverShadowOffset,A);
    Blur=FMath::Max(0.f,FMath::Lerp(RestShadowBlur,HoverShadowBlur,A));Radius=FMath::Max(0.f,CornerRadius);
    Color=ShadowColor;Color.A*=FMath::Clamp(FMath::Lerp(RestShadowOpacity,HoverShadowOpacity,A),0.f,1.f);Layers=SoftShadowLayers;
}
void UDWUIButtonPressComponent::GetPresentation(float A,float& Scale,FVector2D& Offset,FVector2D& ShadowOffset,
    float& Blur,float& Radius,FLinearColor& Color,int32& Layers) const
{
    A=FMath::Clamp(A,0.f,1.f);Scale=FMath::Lerp(1.f,FMath::Clamp(PressScale,.05f,3.f),A);
    Offset=PressTranslate*A*Scale; // CSS scale(s) translate(t) = scaled translation.
    ShadowOffset=FMath::Lerp(RestShadowOffset,PressShadowOffset,A);
    Blur=0.f;Radius=FMath::Max(0.f,CornerRadius);Color=ShadowColor;Layers=1;
}

bool UDWUIButtonMotionLibrary::RunNumericalSelfTest(FString& Report)
{
    int32 Passed=0,Failed=0;Report=TEXT("Pure numerical tests; no rendering/input/gameplay acceptance.\n");
    const auto Check=[&](const TCHAR* Name,bool Okay){Report+=FString::Printf(TEXT("%s %s\n"),Okay?TEXT("PASS"):TEXT("FAIL"),Name);Okay?++Passed:++Failed;};
    const FVector2D C1(.25,.1),C2(.25,1);
    Check(TEXT("ease(0)=0"),EvaluateCSSEase(0,C1,C2)==0.f);
    Check(TEXT("ease(1)=1"),EvaluateCSSEase(1,C1,C2)==1.f);
    Check(TEXT("CSS ease(0.5) ~= 0.802403"),FMath::IsNearlyEqual(EvaluateCSSEase(.5f,C1,C2),.802403f,.00002f));
    float Previous=0.f;bool Monotone=true;
    for(int32 I=1;I<=100;++I){const float A=EvaluateCSSEase(I/100.f,C1,C2);Monotone&=A>=Previous&&A<=1.f;Previous=A;}
    Check(TEXT("ease monotone at 101 samples"),Monotone);
    const auto* Lift=GetDefault<UDWUIButtonLiftComponent>();const auto* Press=GetDefault<UDWUIButtonPressComponent>();
    float S,B,R;FVector2D O,SO;FLinearColor Color;int32 Layers;
    Lift->GetPresentation(1,S,O,SO,B,R,Color,Layers);
    Check(TEXT("Lift end y=-8, shadow16/32/.18/r12"),FMath::IsNearlyEqual(float(O.Y),-8.f)&&FMath::IsNearlyEqual(float(SO.Y),16.f)&&FMath::IsNearlyEqual(B,32.f)&&FMath::IsNearlyEqual(Color.A,.18f)&&FMath::IsNearlyEqual(R,12.f));
    Press->GetPresentation(1,S,O,SO,B,R,Color,Layers);
    Check(TEXT("Press end scale=.92, CSS finalY=3.68, localShadowY=2, radius10"),FMath::IsNearlyEqual(S,.92f)&&FMath::IsNearlyEqual(float(O.Y),3.68f)&&FMath::IsNearlyEqual(float(SO.Y),2.f)&&B==0.f&&FMath::IsNearlyEqual(R,10.f));
    Check(TEXT("Press shadow final offset relative to body=1.84"),FMath::IsNearlyEqual(float(SO.Y)*S,1.84f));
    Report+=FString::Printf(TEXT("Passed=%d Failed=%d\n"),Passed,Failed);return Failed==0;
}
