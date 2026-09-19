#include "DWInteractionPromptWidget.h"
#include "DWInteractionPromptStyle.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Engine/Font.h"

void UDWInteractionPromptWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetVisibility(ESlateVisibility::HitTestInvisible);
    if(PromptVisual)PromptVisual->SetRenderTransformPivot(FVector2D(.5f,1.f));
    ApplyTypography();ApplyTransform(CurrentScale,CurrentOffset,0,CurrentOpacity);
}
void UDWInteractionPromptWidget::InitializePrompt(UDWInteractionPromptStyle* InStyle)
{
    const bool bFirstStyle = !Style;
    Style=InStyle;if(!Style)Style=NewObject<UDWInteractionPromptStyle>(this);
    ApplyTypography();
    if(bFirstStyle){CurrentScale=Style->InitialScale;CurrentOffset=Style->FloatDistance;SetPromptVisible(false,true);}
}
void UDWInteractionPromptWidget::ApplyTypography()
{
    if(!Style)return;
    UFont* Fallback=LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    auto Apply=[&](UTextBlock* Text,UFont* Font,int32 Size,bool bKey)
    {
        if(!Text)return;
        FSlateFontInfo Info(Font?Font:Fallback,FMath::Clamp(Size,8,96));
        Info.OutlineSettings.OutlineSize=bKey?0:FMath::Clamp(Style->OutlineSize,0,8);
        Info.OutlineSettings.OutlineColor=Style->OutlineColor;
        Text->SetFont(Info);Text->SetColorAndOpacity(FSlateColor(bKey?Style->OutlineColor:Style->TextColor));
        Text->SetShadowColorAndOpacity(bKey?FLinearColor::Transparent:FLinearColor(0,0,0,.7f));
        Text->SetShadowOffset(FVector2D(0,bKey?0:2));
    };
    Apply(PromptTitle,Style->TitleFont,Style->TitleFontSize,false);
    Apply(ActionText,Style->TitleFont,Style->ActionFontSize,false);
    Apply(KeyText,Style->KeyFont,Style->KeyFontSize,true);
    if(Keycap)Keycap->SetBrushColor(Style->KeycapColor);
    if(HarvestProgress)HarvestProgress->SetFillColorAndOpacity(Style->ProgressColor);
}
void UDWInteractionPromptWidget::SetPromptText(FText Title,FText Action,FText Key)
{
    if(PromptTitle&&!PromptTitle->GetText().EqualTo(Title))PromptTitle->SetText(Title);
    if(ActionText&&!ActionText->GetText().EqualTo(Action))ActionText->SetText(Action);
    if(KeyText&&!KeyText->GetText().EqualTo(Key))KeyText->SetText(Key);
}
void UDWInteractionPromptWidget::SetProgress(float Progress,bool bShowProgress)
{
    Progress=FMath::Clamp(Progress,0.f,1.f);
    if(HarvestProgress){HarvestProgress->SetPercent(Progress);HarvestProgress->SetVisibility(bShowProgress?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);}
    if(!FMath::IsNearlyEqual(Progress,LastProgress,.001f)){LastProgress=Progress;OnProgressChanged(Progress);}
}
void UDWInteractionPromptWidget::SetPromptVisible(bool bVisible,bool bImmediate)
{
    if(!bImmediate&&bDesiredVisible==bVisible)return;
    const bool bChanged=bDesiredVisible!=bVisible;bDesiredVisible=bVisible;Clock=0;
    StartOpacity=CurrentOpacity;StartScale=CurrentScale;StartOffset=CurrentOffset;
    if(bImmediate)
    {
        const float Initial=Style?Style->InitialScale:.55f,Distance=Style?Style->FloatDistance:24.f;
        ApplyTransform(bVisible?1.f:Initial,bVisible?0.f:Distance,0,bVisible?1.f:0.f);
        Clock=FMath::Max(.05f,Style?(bVisible?Style->AppearDuration:Style->DisappearDuration):1.f);
    }
    if(bChanged){if(bVisible)OnPromptAppeared();else OnPromptDisappeared();}
}
void UDWInteractionPromptWidget::AdvancePresentation(float Dt)
{
    if(!Style)return;
    Clock+=FMath::Max(0.f,Dt);
    if(!Style->bUseNativeMotion){SetRenderOpacity(bDesiredVisible?1.f:0.f);CurrentOpacity=bDesiredVisible?1.f:0.f;return;}
    const float Duration=FMath::Max(.05f,bDesiredVisible?Style->AppearDuration:Style->DisappearDuration);
    const float T=FMath::Clamp(Clock/Duration,0.f,1.f);
    if(T>=1.f){ApplyTransform(bDesiredVisible?1.f:Style->InitialScale,bDesiredVisible?0.f:Style->FloatDistance,0,bDesiredVisible?1.f:0.f);return;}
    if(bDesiredVisible)
    {
        // Reach full size early, overshoot, then settle; changing progress never restarts this clock.
        const float Ease=1.f-FMath::Pow(1.f-FMath::Min(T/.52f,1.f),3.f);
        const float Spring=T<.36f?0.f:FMath::Sin((T-.36f)/.64f*PI*2.f)*FMath::Pow(1.f-(T-.36f)/.64f,1.5f);
        const float Scale=FMath::Lerp(StartScale,1.f,Ease)+Style->OvershootAmount*Spring;
        const float Offset=FMath::Lerp(StartOffset,0.f,1.f-FMath::Pow(1.f-T,3.f));
        const float Angle=-Style->WobbleDegrees*FMath::Sin(T*PI*3.f)*FMath::Pow(1.f-T,2.f);
        ApplyTransform(Scale,Offset,Angle,FMath::Lerp(StartOpacity,1.f,FMath::Min(T/.35f,1.f)));
    }
    else
    {
        const float Ease=T*T*(3.f-2.f*T);
        ApplyTransform(FMath::Lerp(StartScale,.86f,Ease),FMath::Lerp(StartOffset,-Style->FloatDistance*.35f,Ease),0,FMath::Lerp(StartOpacity,0.f,Ease));
    }
}
void UDWInteractionPromptWidget::ApplyTransform(float Scale,float Offset,float Angle,float Opacity)
{
    CurrentScale=Scale;CurrentOffset=Offset;CurrentOpacity=Opacity;
    if(PromptVisual){PromptVisual->SetRenderScale(FVector2D(Scale,Scale));PromptVisual->SetRenderTranslation(FVector2D(0,Offset));PromptVisual->SetRenderTransformAngle(Angle);}
    SetRenderOpacity(Opacity);
}
