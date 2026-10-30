#include "DWInteractionPromptWidget.h"
#include "DWInputGlyphWidget.h"
#include "DWLocalizationLibrary.h"
#include "Components/VerticalBox.h"
#include "DWInteractionPromptStyle.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/Border.h"
#include "Components/SizeBox.h"
#include "Engine/Font.h"
#include "Blueprint/WidgetTree.h"
#include "DWC01UIStyle.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Components/Overlay.h"
#include "Components/OverlaySlot.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/Image.h"
#include "Components/ScaleBox.h"
#include "Engine/Texture2D.h"
#include "DWUIBounceComponent.h"
#include "DWUIGlowPulseComponent.h"
#include "Extensions/UIComponentContainer.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "UObject/UnrealType.h"
#include "HAL/PlatformTime.h"

void UDWInteractionPromptWidget::NativeOnInitialized()
{
    Super::NativeOnInitialized();
    // Mount the spring before Slate builds this local wrapper. The saved WBP is untouched.
    ApplyC01Artwork(true);
}

void UDWInteractionPromptWidget::NativeConstruct()
{
    Super::NativeConstruct();
    SetVisibility(ESlateVisibility::HitTestInvisible);
    if(PromptVisual)PromptVisual->SetRenderTransformPivot(FVector2D(.5f,1.f));
    ApplyTypography();ApplyTransform(CurrentScale,CurrentOffset,0,CurrentOpacity);
    RefreshGlowPaintBounds();
}
void UDWInteractionPromptWidget::NativeDestruct()
{
    bDemolitionPresentation=false;
    C04CompletedFillDeadline=0.;
    if(C04CompletionBounce)C04CompletionBounce->ResetBounce();
    if(C05CompletionGlow)C05CompletionGlow->ResetPulse();
    Super::NativeDestruct();
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
        Info.OutlineSettings.OutlineSize=bUseC01Presentation||bKey?0:FMath::Clamp(Style->OutlineSize,0,8);
        Info.OutlineSettings.OutlineColor=Style->OutlineColor;
        Text->SetFont(Info);Text->SetColorAndOpacity(FSlateColor(bUseC01Presentation?FLinearColor::White:(bKey?Style->OutlineColor:Style->TextColor)));
        Text->SetShadowColorAndOpacity(bUseC01Presentation||bKey?FLinearColor::Transparent:FLinearColor(0,0,0,.7f));
        Text->SetShadowOffset(FVector2D(0,bUseC01Presentation||bKey?0:2));
    };
    Apply(PromptTitle,Style->TitleFont,bUseC01Presentation?C04TitleFontSize:Style->TitleFontSize,false);
    Apply(ActionText,Style->TitleFont,bUseC01Presentation?C04ActionFontSize:Style->ActionFontSize,false);
    Apply(KeyText,Style->KeyFont,bUseC01Presentation?C04KeyFontSize:Style->KeyFontSize,true);
    if(Keycap)Keycap->SetBrushColor(Style->KeycapColor);
    if(HarvestProgress)HarvestProgress->SetFillColorAndOpacity(Style->ProgressColor);
    ApplyC01Artwork();
}

void UDWInteractionPromptWidget::ApplyC01Artwork(bool bDuringOwnerInitialize)
{
    if(!bUseC01Presentation)return;
    // Keep the user's title, action row, key and event bindings. Only their common runtime
    // container changes: the entire two-line block is centered over one progress surface.
    if(PromptVisual&&WidgetTree&&!C04PromptBackdrop)
    {
        UWidget* Existing=PromptVisual->GetContent();
        if(Existing)
        {
            C04PromptContent=Existing;
            C04AuthoredWidth=PromptVisual->IsWidthOverride()?PromptVisual->GetWidthOverride():0.f;
            if(const auto* CanvasSlot=Cast<UCanvasPanelSlot>(PromptVisual->Slot))
                C04AuthoredWidth=FMath::Max(C04AuthoredWidth,float(CanvasSlot->GetSize().X));
            PromptVisual->SetContent(nullptr);
            C01PromptPaper=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C01PromptPaper"));
            C01PromptPaper->SetBrush(FSlateNoResource());
            C01PromptPaper->SetPadding(FMargin(0));
            MountCompletionBounce(bDuringOwnerInitialize);
            auto* Layers=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("C04PromptLayers"));
            C04PromptBackdrop=WidgetTree->ConstructWidget<UProgressBar>(UProgressBar::StaticClass(),TEXT("C04PromptBackdrop"));
            auto* BackdropSlot=Layers->AddChildToOverlay(C04PromptBackdrop);
            BackdropSlot->SetHorizontalAlignment(HAlign_Fill);BackdropSlot->SetVerticalAlignment(VAlign_Fill);
            // Add only a runtime sibling slot. Preserve the original text/key widgets and events.
            C06PromptContentRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("C06PromptContentRow"));
            C06PromptContentRow->SetVisibility(ESlateVisibility::HitTestInvisible);
            C06PickupIconSlot=WidgetTree->ConstructWidget<USizeBox>(USizeBox::StaticClass(),TEXT("C06PickupIconSlot"));
            auto* IconRowSlot=C06PromptContentRow->AddChildToHorizontalBox(C06PickupIconSlot);
            IconRowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));IconRowSlot->SetVerticalAlignment(VAlign_Center);
            auto* IconBackdrop=WidgetTree->ConstructWidget<UBorder>(UBorder::StaticClass(),TEXT("C06PickupIconBackdrop"));
            IconBackdrop->SetBrush(FSlateRoundedBoxBrush(FLinearColor(.7f,.7f,.7f,.18f),8.f));
            IconBackdrop->SetBrushColor(FLinearColor::White);IconBackdrop->SetPadding(FMargin(4));
            auto* IconLayers=WidgetTree->ConstructWidget<UOverlay>(UOverlay::StaticClass(),TEXT("C06PickupIconLayers"));
            auto* IconScale=WidgetTree->ConstructWidget<UScaleBox>(UScaleBox::StaticClass(),TEXT("C06PickupIconScale"));
            IconScale->SetStretch(EStretch::ScaleToFit);IconScale->SetStretchDirection(EStretchDirection::Both);
            C06PickupIconImage=WidgetTree->ConstructWidget<UImage>(UImage::StaticClass(),TEXT("C06PickupIconImage"));
            C06PickupIconImage->SetColorAndOpacity(FLinearColor::White);IconScale->SetContent(C06PickupIconImage);
            auto* ImageLayer=IconLayers->AddChildToOverlay(IconScale);
            ImageLayer->SetHorizontalAlignment(HAlign_Fill);ImageLayer->SetVerticalAlignment(VAlign_Fill);
            C06PickupIconPlaceholder=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("C06PickupIconPlaceholder"));
            C06PickupIconPlaceholder->SetText(FText::FromString(TEXT("?")));C06PickupIconPlaceholder->SetJustification(ETextJustify::Center);
            auto* PlaceholderLayer=IconLayers->AddChildToOverlay(C06PickupIconPlaceholder);
            PlaceholderLayer->SetHorizontalAlignment(HAlign_Center);PlaceholderLayer->SetVerticalAlignment(VAlign_Center);
            IconBackdrop->SetContent(IconLayers);C06PickupIconSlot->SetContent(IconBackdrop);
            // One runtime-only group retains the authored title/action layout and their events.
            // Its second action is collapsed on ordinary resource/pickup/generic prompts.
            auto* TextGroup=WidgetTree->ConstructWidget<UVerticalBox>(UVerticalBox::StaticClass(),TEXT("C07PromptTextGroup"));
            auto* ExistingSlot=TextGroup->AddChildToVerticalBox(Existing);ExistingSlot->SetHorizontalAlignment(HAlign_Center);
            C07SecondaryRow=WidgetTree->ConstructWidget<UHorizontalBox>(UHorizontalBox::StaticClass(),TEXT("C07SecondaryRow"));
            auto* SecondarySlot=TextGroup->AddChildToVerticalBox(C07SecondaryRow);
            SecondarySlot->SetHorizontalAlignment(HAlign_Center);SecondarySlot->SetPadding(FMargin(0,4,0,0));
            C07SecondaryGlyph=WidgetTree->ConstructWidget<UDWInputGlyphWidget>(UDWInputGlyphWidget::StaticClass(),TEXT("C07SecondaryGlyph"));
            auto* SecondaryKeySlot=C07SecondaryRow->AddChildToHorizontalBox(C07SecondaryGlyph);
            SecondaryKeySlot->SetVerticalAlignment(VAlign_Center);SecondaryKeySlot->SetPadding(FMargin(0,0,8,0));
            C07SecondaryAction=WidgetTree->ConstructWidget<UTextBlock>(UTextBlock::StaticClass(),TEXT("C07SecondaryAction"));
            C07SecondaryAction->SetJustification(ETextJustify::Center);C07SecondaryAction->SetAutoWrapText(false);C07SecondaryAction->SetWrapTextAt(0);
            auto* SecondaryActionSlot=C07SecondaryRow->AddChildToHorizontalBox(C07SecondaryAction);SecondaryActionSlot->SetVerticalAlignment(VAlign_Center);
            C07SecondaryRow->SetVisibility(ESlateVisibility::Collapsed);
            C04PromptContent=TextGroup;
            auto* TextRowSlot=C06PromptContentRow->AddChildToHorizontalBox(TextGroup);
            TextRowSlot->SetSize(FSlateChildSize(ESlateSizeRule::Automatic));TextRowSlot->SetVerticalAlignment(VAlign_Center);
            auto* ContentSlot=Layers->AddChildToOverlay(C06PromptContentRow);
            ContentSlot->SetHorizontalAlignment(HAlign_Center);ContentSlot->SetVerticalAlignment(VAlign_Center);
            ContentSlot->SetPadding(FMargin(static_cast<float>(FMath::Clamp(C04ContentPadding.X,0.,48.)),static_cast<float>(FMath::Clamp(C04ContentPadding.Y,0.,48.))));
            C01PromptPaper->SetContent(Layers);
            PromptVisual->SetContent(C01PromptPaper);

            // Hidden widgets still take up layout space. Collapse the old, dedicated strip
            // and its known wrapper so it cannot bias the vertical center of the two lines.
            if(HarvestProgress)
            {
                HarvestProgress->SetVisibility(ESlateVisibility::Collapsed);
                if(USizeBox* Strip=Cast<USizeBox>(HarvestProgress->GetParent()))
                    if(Strip->GetFName()==TEXT("ProgressSize"))Strip->SetVisibility(ESlateVisibility::Collapsed);
            }
        }
    }
    if(C01PromptPaper)
    {
        C01PromptPaper->SetVisibility(ESlateVisibility::HitTestInvisible);
    }
    if(Keycap&&WidgetTree)
    {
        if(!C07PrimaryGlyph)
        {
            C07PrimaryGlyph=WidgetTree->ConstructWidget<UDWInputGlyphWidget>(UDWInputGlyphWidget::StaticClass(),TEXT("C07PrimaryGlyph"));
            Keycap->SetContent(C07PrimaryGlyph); // bound KeyText remains alive as authored fallback data
        }
        Keycap->SetBrush(FSlateNoResource());Keycap->SetBrushColor(FLinearColor::White);Keycap->SetPadding(FMargin(0));
        if(auto* KeySize=Cast<USizeBox>(Keycap->GetParent())){KeySize->SetWidthOverride(38);KeySize->SetHeightOverride(38);}
    }
    if(PromptTitle)
    {
        PromptTitle->SetJustification(ETextJustify::Center);
        if(auto* TitleSlot=Cast<UVerticalBoxSlot>(PromptTitle->Slot))
        {TitleSlot->SetHorizontalAlignment(HAlign_Center);TitleSlot->SetPadding(FMargin(0,0,0,FMath::Clamp(C04LineGap,0.f,16.f)));}
    }
    if(ActionText){ActionText->SetJustification(ETextJustify::Center);ActionText->SetAutoWrapText(false);ActionText->SetWrapTextAt(0);}
    if(KeyText)KeyText->SetJustification(ETextJustify::Center);
    if(C06PromptContentRow)
        if(auto* ContentSlot=Cast<UOverlaySlot>(C06PromptContentRow->Slot))
        {
            ContentSlot->SetHorizontalAlignment(HAlign_Center);ContentSlot->SetVerticalAlignment(VAlign_Center);
            ContentSlot->SetPadding(FMargin(float(FMath::Clamp(C04ContentPadding.X,0.,48.)),float(FMath::Clamp(C04ContentPadding.Y,0.,48.))));
        }
    if(C04PromptBackdrop)
    {
        const float Radius=FMath::Clamp(C01PromptCornerRadius,0.f,32.f);
        FProgressBarStyle ProgressStyle=C04PromptBackdrop->GetWidgetStyle();
        ProgressStyle.EnableFillAnimation=false;
        ProgressStyle.BackgroundImage=FSlateRoundedBoxBrush(C01PromptBackground,Radius);
        // Mask reveals the full-size rounded shape rather than shrinking/distorting its corners.
        ProgressStyle.FillImage=FSlateRoundedBoxBrush(FLinearColor::White,Radius);
        C04PromptBackdrop->SetBarFillType(EProgressBarFillType::LeftToRight);
        C04PromptBackdrop->SetBarFillStyle(EProgressBarFillStyle::Mask);
        C04PromptBackdrop->SetIsMarquee(false);
        C04PromptBackdrop->SetBorderPadding(FVector2D::ZeroVector);
        C04PromptBackdrop->SetWidgetStyle(ProgressStyle);
        C04PromptBackdrop->SetFillColorAndOpacity(bDemolitionPresentation?DemolitionFill:FLinearColor(1.f,1.f,1.f,FMath::Clamp(C04HarvestFillOpacity,0.f,.6f)));
        C04PromptBackdrop->SetVisibility(ESlateVisibility::HitTestInvisible);
        RefreshBackgroundProgress();
    }
    RefreshPickupIcon();RefreshInputGlyphs();
    bC04LayoutDirty=true;
    RefreshC04Layout();
    ConfigureCompletionGlow();RefreshGlowPaintBounds();
}
void UDWInteractionPromptWidget::RefreshC04Layout()
{
    UWidget* MeasuredContent=C06PromptContentRow?static_cast<UWidget*>(C06PromptContentRow.Get()):C04PromptContent.Get();
    if(!bC04LayoutDirty||!bUseC01Presentation||!PromptVisual||!C04PromptContent||!MeasuredContent||!MeasuredContent->GetCachedWidget().IsValid())return;
    // Measure the actual shaped text after typography/localization, not a guessed number of pixels.
    // The old WBP has BOTH HeightOverride=120 and CanvasSlot height=120; both runtime constraints
    // must agree. Changing padding alone leaves the action row squeezed against the bottom.
    MeasuredContent->ForceLayoutPrepass();
    const FVector2D Desired=MeasuredContent->GetDesiredSize();
    const FVector2D TextDesired=C04PromptContent->GetDesiredSize();
    if(Desired.X<=0.||Desired.Y<=0.)return;
    C04MeasuredContentSize=Desired;
    const FVector2D Pad(FMath::Clamp(C04ContentPadding.X,0.,48.),FMath::Clamp(C04ContentPadding.Y,0.,48.));
    const FVector2D Size(FMath::CeilToDouble(FMath::Max(double(C04AuthoredWidth),Desired.X+Pad.X*2.)),
        FMath::CeilToDouble(FMath::Max(double(FMath::Clamp(C04TargetPanelHeight,48.f,240.f)),Desired.Y+Pad.Y*2.)));
    // Darumadrop's visible glyph ink is lower than its centered line box. Offset only
    // this text group; constrain it to the measured free space so no line box is cut.
    const double HalfFreeHeight=FMath::Max(0.,(Size.Y-TextDesired.Y)*.5);
    C04AppliedContentOpticalOffsetY=float(FMath::Clamp(double(C04ContentOpticalOffsetY),-HalfFreeHeight,HalfFreeHeight));
    const FVector2D Translation=C04PromptContent->GetRenderTransform().Translation;
    C04PromptContent->SetRenderTranslation(FVector2D(Translation.X,C04AppliedContentOpticalOffsetY));
    PromptVisual->SetWidthOverride(float(Size.X));PromptVisual->SetHeightOverride(float(Size.Y));
    if(auto* CanvasSlot=Cast<UCanvasPanelSlot>(PromptVisual->Slot))
    {
        const FAnchors Anchors=CanvasSlot->GetAnchors();FMargin Offsets=CanvasSlot->GetOffsets();
        if(FMath::IsNearlyEqual(Anchors.Minimum.X,Anchors.Maximum.X))Offsets.Right=float(Size.X);
        if(FMath::IsNearlyEqual(Anchors.Minimum.Y,Anchors.Maximum.Y))Offsets.Bottom=float(Size.Y);
        CanvasSlot->SetOffsets(Offsets); // preserve position, anchor, alignment and width source
    }
    C04ResolvedPanelSize=Size;bC04LayoutDirty=false;
}
FVector2D UDWInteractionPromptWidget::GetMinimumDrawSize() const
{
    if(!bUseC01Presentation||!PromptVisual||C04ResolvedPanelSize.Y<=0.)return FVector2D::ZeroVector;
    const auto* CanvasSlot=Cast<UCanvasPanelSlot>(PromptVisual->Slot);
    if(!CanvasSlot)return C04ResolvedPanelSize;
    const FAnchors Anchors=CanvasSlot->GetAnchors();const FVector2D Alignment=CanvasSlot->GetAlignment();
    const FMargin Offsets=CanvasSlot->GetOffsets();FVector2D Required=FVector2D::ZeroVector;
    // Reserve paint space for the fixed-anchor rectangle and its soft fringe. The default
    // 320x104 bar needs a 372x148 host. Its bottom edge can also paint beyond the inherited
    // screen-space host bounds, without moving the authored bottom anchor or its -18 offset.
    const float Extent=bDemolitionPresentation?DemolitionGlowExtent:C05GlowExtent;
    const double Halo=(bDemolitionPresentation||bC05CompletionGlow)&&FMath::IsFinite(Extent)?FMath::Clamp(double(Extent),0.,96.)+2.:0.;
    auto Axis=[](double A,double B,double P,double Size,double Inset)
    {double R=0.;if(A>0.)R=FMath::Max(R,(Inset+B*Size-P)/A);if(A<1.)R=FMath::Max(R,(Inset+P+(1.-B)*Size)/(1.-A));return FMath::CeilToDouble(R);};
    if(FMath::IsNearlyEqual(Anchors.Minimum.X,Anchors.Maximum.X))Required.X=Axis(Anchors.Minimum.X,Alignment.X,Offsets.Left,C04ResolvedPanelSize.X,Halo);
    if(FMath::IsNearlyEqual(Anchors.Minimum.Y,Anchors.Maximum.Y))Required.Y=Axis(Anchors.Minimum.Y,Alignment.Y,Offsets.Top,C04ResolvedPanelSize.Y,Halo);
    return Required;
}
void UDWInteractionPromptWidget::MountCompletionBounce(bool bDuringOwnerInitialize)
{
    if(!C01PromptPaper||(C04CompletionBounce&&C05CompletionGlow)||IsDesignTime())return;
    auto* Extension=GetExtension<UUIComponentUserWidgetExtension>();
    const bool bNewExtension=Extension==nullptr;
    if(!Extension)Extension=AddExtension<UUIComponentUserWidgetExtension>();
    if(!Extension)return;
    const auto* Property=FindFProperty<FObjectPropertyBase>(Extension->GetClass(),TEXT("ComponentContainer"));
    auto* Container=Property?Cast<UUIComponentContainer>(Property->GetObjectPropertyValue_InContainer(Extension)):nullptr;
    if(!Container){Container=NewObject<UUIComponentContainer>(Extension);Extension->InitializeContainer(Container);}
    if(bNewExtension&&bDuringOwnerInitialize)Extension->Initialize();
    // The first component is the inner wrapper. Mount the glow BEFORE the spring so
    // the soft fringe follows the exact same scale/rotation as the filled bar.
    if(!C05CompletionGlow)
    {
        C05CompletionGlow=NewObject<UDWUIGlowPulseComponent>(Container);
        ConfigureCompletionGlow();
        Container->AddComponent(C01PromptPaper->GetFName(),C05CompletionGlow);
        C05CompletionGlow->Initialize(C01PromptPaper);
        if(IsConstructed()){C05CompletionGlow->PreConstruct(false);C05CompletionGlow->Construct();}
    }
    if(C04CompletionBounce)return;
    C04CompletionBounce=NewObject<UDWUIBounceComponent>(Container);
    C04CompletionBounce->bAutoBindButtons=false;C04CompletionBounce->bAnimateNonButtonHover=false;C04CompletionBounce->bPlayOnConstruct=false;
    C04CompletionBounce->PulseOffset=FVector2D::ZeroVector;C04CompletionBounce->PulseAngle=0.f;
    C04CompletionBounce->PulseScaleOffset=FMath::Clamp(C04CompletionBounceAmount,0.f,.3f);
    Container->AddComponent(C01PromptPaper->GetFName(),C04CompletionBounce);
    C04CompletionBounce->Initialize(C01PromptPaper);C01PromptPaper->bWrappedByComponent=true;
    if(IsConstructed()){C04CompletionBounce->PreConstruct(false);C04CompletionBounce->Construct();}
}
void UDWInteractionPromptWidget::ConfigureCompletionGlow()
{
    if(!C05CompletionGlow)return;
    if(bDemolitionPresentation)
    {
        C05CompletionGlow->PeakOpacity=FMath::IsFinite(DemolitionGlowOpacity)?FMath::Clamp(DemolitionGlowOpacity,0.f,1.f):0.f;
        C05CompletionGlow->HaloExtent=FMath::IsFinite(DemolitionGlowExtent)?FMath::Clamp(DemolitionGlowExtent,0.f,96.f):0.f;
        C05CompletionGlow->CornerRadius=FMath::IsFinite(C01PromptCornerRadius)?FMath::Clamp(C01PromptCornerRadius,0.f,32.f):0.f;
        C05CompletionGlow->GlowColor=DemolitionGlowColor;C05CompletionGlow->SetSustainedGlow(true);return;
    }
    C05CompletionGlow->RiseSeconds=C05GlowRiseSeconds;C05CompletionGlow->HoldSeconds=C05GlowHoldSeconds;C05CompletionGlow->FadeSeconds=C05GlowFadeSeconds;
    C05CompletionGlow->PeakOpacity=C05GlowPeakOpacity;C05CompletionGlow->HaloExtent=C05GlowExtent;
    C05CompletionGlow->CornerRadius=FMath::IsFinite(C01PromptCornerRadius)?FMath::Clamp(C01PromptCornerRadius,0.f,32.f):0.f;C05CompletionGlow->GlowColor=FLinearColor::White;
    C05CompletionGlow->bPlayOnConstruct=false;
    if(!bC05CompletionGlow)C05CompletionGlow->ResetPulse();
}
void UDWInteractionPromptWidget::RefreshGlowPaintBounds()
{
    if(!bUseC01Presentation||!C01PromptPaper)return;
    const float Extent=bDemolitionPresentation?DemolitionGlowExtent:C05GlowExtent;
    const float Halo=(bDemolitionPresentation||bC05CompletionGlow)&&FMath::IsFinite(Extent)?FMath::Clamp(Extent,0.f,96.f)+2.f:0.f;
    // These prompts use WidgetSpace::Screen, not a render target. The screen-layer SBox
    // already inherits viewport clipping. Permit local overflow through the runtime
    // prompt chain and extend culling bounds, without shifting the authored anchors or
    // changing the bar/text dimensions. In particular its bottom anchor stays intact.
    for(UWidget* Widget=C01PromptPaper;Widget;Widget=Widget->GetParent())
    {
        Widget->SetClipping(EWidgetClipping::Inherit);
        if(const auto SlateWidget=Widget->GetCachedWidget())SlateWidget->SetCullingBoundsExtension(FMargin(Halo));
    }
    SetClipping(EWidgetClipping::Inherit);
    if(const auto SlateWidget=GetCachedWidget())SlateWidget->SetCullingBoundsExtension(FMargin(Halo));
}
float UDWInteractionPromptWidget::GetCompletionDisplaySeconds() const
{
    const auto SafeSeconds=[](float Seconds){return FMath::IsFinite(Seconds)?FMath::Clamp(Seconds,0.f,5.f):0.f;};
    const float GlowSeconds=bC05CompletionGlow?SafeSeconds(C05GlowRiseSeconds)+SafeSeconds(C05GlowHoldSeconds)+SafeSeconds(C05GlowFadeSeconds):0.f;
    return FMath::Max(GlowSeconds,FMath::Max(FMath::Clamp(C04CompletionVisibilitySeconds,.05f,5.f),FMath::Clamp(C04CompletionBounceSeconds,.05f,5.f)));
}
void UDWInteractionPromptWidget::NotifyHarvestCompleted()
{
    if(!bUseC01Presentation||bDemolitionPresentation)return;
    const float HoldSeconds=FMath::Clamp(C04CompletedFillHoldSeconds,0.f,1.f);
    C04CompletedFillDeadline=HoldSeconds>0.f?FPlatformTime::Seconds()+double(HoldSeconds):0.;
    PlayHarvestFeedback(true);
    RefreshBackgroundProgress();
}
void UDWInteractionPromptWidget::NotifyGatheringProgressCommitted(float PreviousProgress,float CurrentProgress)
{
    if(!bUseC01Presentation||bDemolitionPresentation||!FMath::IsFinite(PreviousProgress)||!FMath::IsFinite(CurrentProgress)
        ||PreviousProgress<0.f||CurrentProgress>1.f||CurrentProgress<=PreviousProgress)return;
    // Each accepted hit pulses once. Intermediate hits preserve their actual fill fraction.
    // The final hit uses this SAME path; it must not also call NotifyHarvestCompleted.
    SetProgress(CurrentProgress,true);
    if(!IsValid(this))return;
    ++C06GatheringHitFeedbackCount;
    if(CurrentProgress>=1.f)
    {
        const float HoldSeconds=FMath::Clamp(C04CompletedFillHoldSeconds,0.f,1.f);
        C04CompletedFillDeadline=HoldSeconds>0.f?FPlatformTime::Seconds()+double(HoldSeconds):0.;
    }
    PlayHarvestFeedback(false);
    RefreshBackgroundProgress();
}
void UDWInteractionPromptWidget::ResetHarvestFeedback(float CurrentProgress)
{
    C04CompletedFillDeadline=0.;
    if(C04CompletionBounce)C04CompletionBounce->ResetBounce();
    if(C05CompletionGlow)C05CompletionGlow->ResetPulse();
    LastProgress=FMath::IsFinite(CurrentProgress)?FMath::Clamp(CurrentProgress,0.f,1.f):0.f;
    bC04ShowProgress=true;
    if(HarvestProgress)HarvestProgress->SetPercent(LastProgress);
    RefreshBackgroundProgress();
}
void UDWInteractionPromptWidget::PlayHarvestFeedback(bool bCountAsLegacyCompletion)
{
    if(C04CompletionBounce)
    {
        C04CompletionBounce->FrequencyHz=1.6f/FMath::Clamp(C04CompletionBounceSeconds,.05f,5.f);
        C04CompletionBounce->DampingRatio=.55f;
        C04CompletionBounce->PulseScaleOffset=FMath::Clamp(C04CompletionBounceAmount,0.f,.3f);
        C04CompletionBounce->Pulse();
        if(bCountAsLegacyCompletion)++C04CompletionPulseCount;
        else ++C06GatheringHitBounceCount;
    }
    ConfigureCompletionGlow();RefreshGlowPaintBounds();
    if(bC05CompletionGlow&&C05CompletionGlow)
    {
        C05CompletionGlow->Pulse();
        if(C05CompletionGlow->IsAnimating())
        {
            if(bCountAsLegacyCompletion)++C05CompletionGlowCount;
            else ++C06GatheringHitGlowCount;
        }
    }
}
void UDWInteractionPromptWidget::RefreshBackgroundProgress()
{
    if(C04PromptBackdrop)C04PromptBackdrop->SetPercent(GetCompletedFillRemainingSeconds()>0.f?1.f:bC04ShowProgress?FMath::Clamp(LastProgress,0.f,1.f):0.f);
}
float UDWInteractionPromptWidget::GetCompletedFillRemainingSeconds() const
{
    return float(FMath::Max(0.,C04CompletedFillDeadline-FPlatformTime::Seconds()));
}
void UDWInteractionPromptWidget::SetPickupIcon(UTexture2D* Icon,bool bShowSlot)
{
    UTexture2D* Resolved=bShowSlot&&IsValid(Icon)?Icon:nullptr;
    if(C06PickupIcon==Resolved&&bC06ShowPickupIcon==bShowSlot)return;
    C06PickupIcon=Resolved;bC06ShowPickupIcon=bShowSlot;
    RefreshPickupIcon();bC04LayoutDirty=true;RefreshC04Layout();RefreshGlowPaintBounds();
}
void UDWInteractionPromptWidget::RefreshPickupIcon()
{
    if(!C06PickupIconSlot||!C06PickupIconImage||!C06PickupIconPlaceholder)return;
    const float Size=FMath::IsFinite(C06PickupIconSize)?FMath::Clamp(C06PickupIconSize,24.f,128.f):56.f;
    const float Gap=FMath::IsFinite(C06PickupIconGap)?FMath::Clamp(C06PickupIconGap,0.f,48.f):12.f;
    C06PickupIconSlot->SetWidthOverride(Size);C06PickupIconSlot->SetHeightOverride(Size);
    C06PickupIconSlot->SetVisibility(bC06ShowPickupIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(auto* IconSlot=Cast<UHorizontalBoxSlot>(C06PickupIconSlot->Slot))IconSlot->SetPadding(FMargin(0,0,bC06ShowPickupIcon?Gap:0.f,0));
    const bool bHasIcon=IsValid(C06PickupIcon.Get());
    if(bHasIcon)
    {
        C06PickupIconImage->SetBrushFromTexture(C06PickupIcon.Get(),true);
        // No-resource clearing sets NoDrawType. SetBrushFromTexture preserves that
        // draw type, so explicitly restore image rendering when a texture returns.
        FSlateBrush IconBrush=C06PickupIconImage->GetBrush();
        IconBrush.DrawAs=ESlateBrushDrawType::Image;
        C06PickupIconImage->SetBrush(IconBrush);
    }
    else C06PickupIconImage->SetBrush(FSlateNoResource());
    C06PickupIconImage->SetColorAndOpacity(FLinearColor::White);
    C06PickupIconImage->SetVisibility(bHasIcon?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    C06PickupIconPlaceholder->SetVisibility(bHasIcon?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
    UFont* Font=Style&&Style->TitleFont?Style->TitleFont.Get():LoadObject<UFont>(nullptr,TEXT("/Engine/EngineFonts/Roboto.Roboto"));
    FSlateFontInfo Info(Font,FMath::Clamp(C04TitleFontSize,8,64));Info.OutlineSettings.OutlineSize=0;
    C06PickupIconPlaceholder->SetFont(Info);C06PickupIconPlaceholder->SetColorAndOpacity(FSlateColor(FLinearColor::White));
    C06PickupIconPlaceholder->SetShadowOffset(FVector2D::ZeroVector);C06PickupIconPlaceholder->SetShadowColorAndOpacity(FLinearColor::Transparent);
}

void UDWInteractionPromptWidget::SetPromptText(FText Title,FText Action,FText Key)
{
    if(PromptTitle&&!PromptTitle->GetText().EqualTo(Title)){PromptTitle->SetText(Title);bC04LayoutDirty=true;}
    if(ActionText&&!ActionText->GetText().EqualTo(Action)){ActionText->SetText(Action);bC04LayoutDirty=true;}
    if(KeyText&&!KeyText->GetText().EqualTo(Key)){KeyText->SetText(Key);bC04LayoutDirty=true;}
    RefreshInputGlyphs();
}
void UDWInteractionPromptWidget::SetPromptInputKey(FKey Key,bool bUseAppliedKey)
{
    const bool Changed=C07PrimaryKey!=Key||bC07UseAppliedKey!=bUseAppliedKey;
    C07PrimaryKey=Key;bC07UseAppliedKey=bUseAppliedKey;
    RefreshInputGlyphs();if(Changed)bC04LayoutDirty=true;
}
void UDWInteractionPromptWidget::SetSecondaryPrompt(FText Action,FKey Key,bool bShow)
{
    const bool Changed=C07SecondaryKey!=Key||bC07ShowSecondary!=bShow||!C07SecondaryText.EqualTo(Action);
    C07SecondaryKey=Key;C07SecondaryText=Action;bC07ShowSecondary=bShow;
    RefreshInputGlyphs();if(Changed)bC04LayoutDirty=true;
}
void UDWInteractionPromptWidget::RefreshInputGlyphs()
{
    const bool English=UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English;
    UFont* Font=Style?Style->KeyFont.Get():nullptr;
    if(C07PrimaryGlyph)
    {
        C07PrimaryGlyph->SetPresentationStyle(Font,English,FVector2D(38,38),C04KeyFontSize,true);
        if(bC07UseAppliedKey)C07PrimaryGlyph->SetInputKey(C07PrimaryKey,FText::GetEmpty());
        else C07PrimaryGlyph->SetTextFallback(KeyText?KeyText->GetText():FText::GetEmpty());
    }
    if(C07SecondaryRow)C07SecondaryRow->SetVisibility(bC07ShowSecondary?ESlateVisibility::HitTestInvisible:ESlateVisibility::Collapsed);
    if(C07SecondaryGlyph)
    {C07SecondaryGlyph->SetPresentationStyle(Font,English,FVector2D(38,38),C04KeyFontSize,true);C07SecondaryGlyph->SetInputKey(C07SecondaryKey,FText::GetEmpty());}
    if(C07SecondaryAction)
    {
        C07SecondaryAction->SetText(C07SecondaryText);
        if(ActionText){C07SecondaryAction->SetFont(ActionText->GetFont());C07SecondaryAction->SetColorAndOpacity(ActionText->GetColorAndOpacity());}
        C07SecondaryAction->SetShadowOffset(FVector2D::ZeroVector);
    }
}
void UDWInteractionPromptWidget::SetProgress(float Progress,bool bShowProgress)
{
    Progress=FMath::Clamp(Progress,0.f,1.f);
    bC04ShowProgress=bShowProgress;
    if(HarvestProgress)
    {
        HarvestProgress->SetPercent(Progress);
        HarvestProgress->SetVisibility(bUseC01Presentation?ESlateVisibility::Collapsed:bShowProgress?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    }
    if(!FMath::IsNearlyEqual(Progress,LastProgress,.001f)){LastProgress=Progress;OnProgressChanged(Progress);}
    RefreshBackgroundProgress();
}
void UDWInteractionPromptWidget::SetDemolitionPresentation(bool bWorking,float AuthoritativeProgress)
{
    if(!FMath::IsFinite(AuthoritativeProgress))bWorking=false;
    const bool Changed=bDemolitionPresentation!=bWorking;
    if(Changed)
    {
        bDemolitionPresentation=bWorking;C04CompletedFillDeadline=0.;
        if(C04CompletionBounce)C04CompletionBounce->ResetBounce();
        if(C05CompletionGlow)C05CompletionGlow->ResetPulse();
        if(C04PromptBackdrop)C04PromptBackdrop->SetFillColorAndOpacity(bWorking?DemolitionFill:FLinearColor(1,1,1,FMath::Clamp(C04HarvestFillOpacity,0.f,.6f)));
        if(HarvestProgress)HarvestProgress->SetFillColorAndOpacity(bWorking?DemolitionFill:(Style?Style->ProgressColor:FLinearColor::White));
        ConfigureCompletionGlow();RefreshGlowPaintBounds();
    }
    if(bWorking)SetProgress(FMath::Clamp(AuthoritativeProgress,0.f,.99f),true);
    else if(Changed)SetProgress(0.f,false);
}
void UDWInteractionPromptWidget::SetPromptVisible(bool bVisible,bool bImmediate)
{
    if(!bImmediate&&bDesiredVisible==bVisible)return;
    const bool bChanged=bDesiredVisible!=bVisible;bDesiredVisible=bVisible;Clock=0;
    StartOpacity=CurrentOpacity;StartScale=CurrentScale;StartOffset=CurrentOffset;
    if(bImmediate)
    {
        if(!bVisible)
        {
            SetDemolitionPresentation(false,0.f);
            C04CompletedFillDeadline=0.;bC04ShowProgress=false;RefreshBackgroundProgress();
            if(C04CompletionBounce)C04CompletionBounce->ResetBounce();
            if(C05CompletionGlow)C05CompletionGlow->ResetPulse();
        }
        const float Initial=Style?Style->InitialScale:.55f,Distance=Style?Style->FloatDistance:24.f;
        ApplyTransform(bVisible?1.f:Initial,bVisible?0.f:Distance,0,bVisible?1.f:0.f);
        Clock=FMath::Max(.05f,Style?(bVisible?Style->AppearDuration:Style->DisappearDuration):1.f);
    }
    if(bChanged){if(bVisible)OnPromptAppeared();else OnPromptDisappeared();}
}
void UDWInteractionPromptWidget::AdvancePresentation(float Dt)
{
    RefreshC04Layout();
    // The hold begins at the accepted commit, not at the beginning of a potentially
    // long frame. Do not subtract this frame's DeltaTime from a just-created hold.
    RefreshBackgroundProgress();
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
