#include "DWBuildingPlacementHints.h"
#include "DWInputGlyphWidget.h"
#include "DWBuildingPlacementComponent.h"
#include "DWGameplayWidget.h"
#include "DWGameplayHUD.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWLocalizationLibrary.h"
#include "DWC01UIStyle.h"
#include "DWUIOffscreenComponent.h"
#include "DWWorldEvent.h"
#include "DWLoadingTransition.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "UnrealClient.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontMeasure.h"
#include "Fonts/FontCache.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
// Q/E only: center the rendered glyph quad at the current layout/DPI scale.
// Other labels keep their existing text shaping and optical compensation.
class SPlacementInkCenteredKey final : public STextBlock
{
public:
 SLATE_BEGIN_ARGS(SPlacementInkCenteredKey) {} SLATE_END_ARGS()
 void Construct(const FArguments&)
 {
  STextBlock::Construct(STextBlock::FArguments().SimpleTextMode(true)
   .ColorAndOpacity(DWC01UI::Ink()).ShadowOffset(FVector2D::ZeroVector)
   .Justification(ETextJustify::Center));
 }
 virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& AllottedGeometry,
  const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,
  const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const override
 {
  const FString Key=GetText().ToString();
  const float Scale=AllottedGeometry.GetAccumulatedLayoutTransform().GetScale();
  if(FSlateApplication::IsInitialized()&&FMath::IsFinite(Scale)&&Scale>SMALL_NUMBER
   &&Key.Len()==1&&(Key[0]==TEXT('Q')||Key[0]==TEXT('E')))
  {
   const FSlateFontInfo KeyFontInfo=GetFont();
   FCharacterList& Characters=FSlateApplication::Get().GetRenderer()->GetFontCache()
    ->GetCharacterList(KeyFontInfo,Scale,FFontOutlineSettings::NoOutline);
   const FCharacterEntry Glyph=Characters.GetCharacter(Key[0],KeyFontInfo.FontFallback);
   if(Glyph.Valid&&Glyph.USize>0&&Glyph.VSize>0&&Characters.GetMaxHeight()>0
    &&FMath::IsFinite(Glyph.BitmapRenderScale)&&Glyph.BitmapRenderScale>SMALL_NUMBER)
   {
    // UE 5.8 ElementBatcher.cpp AddTextElement: bearings, height and bitmap scale.
    // StartU/StartV locate the atlas image; they are NOT screen-space bearings.
    const float BitmapScale=Glyph.BitmapRenderScale;
    const FVector2D InkCenter(
     (double(Glyph.HorizontalOffset)+double(Glyph.USize)*BitmapScale*.5)/Scale,
     (-double(Glyph.VerticalOffset)+(double(Characters.GetMaxHeight())+Glyph.GlobalDescender)/BitmapScale
       +double(Glyph.VSize)*BitmapScale*.5)/Scale);
    const FVector2D Offset=FVector2D(AllottedGeometry.GetLocalSize())*.5-InkCenter;
    const FGeometry Centered=AllottedGeometry.MakeChild(AllottedGeometry.GetLocalSize(),
     FSlateLayoutTransform(FVector2f(Offset)));
    return STextBlock::OnPaint(Args,Centered,MyCullingRect,OutDrawElements,LayerId,InWidgetStyle,bParentEnabled);
   }
  }
  return STextBlock::OnPaint(Args,AllottedGeometry,MyCullingRect,OutDrawElements,LayerId,InWidgetStyle,bParentEnabled);
 }
};

FSlateFontInfo FitPlacementHintFont(FSlateFontInfo Nominal,const FText& Text,FVector2D Available)
{
 // Always start from the nominal font, so a short label or language switch grows back.
 if(FSlateApplication::IsInitialized())
 {
  const auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
  for(int32 Pass=0;Pass<3;++Pass)
  {
   const FVector2D Measured=Measure->Measure(Text,Nominal);
   float Scale=1.f;
   if(Measured.X>0.)Scale=FMath::Min(Scale,float(FMath::Max(1.,Available.X-1.)/Measured.X));
   if(Measured.Y>0.)Scale=FMath::Min(Scale,float(FMath::Max(1.,Available.Y-1.)/Measured.Y));
   if(Scale>=1.f)break;
   // No readability floor may force overflow at a user-reduced HUD diameter.
   Nominal.Size=FMath::Max(1.f,Nominal.Size*Scale);
  }
 }
 return Nominal;
}
}

UDWBuildingPlacementHints::UDWBuildingPlacementHints(const FObjectInitializer& ObjectInitializer):Super(ObjectInitializer)
{
 SetVisibility(ESlateVisibility::HitTestInvisible);SetRenderOpacity(0.f);
 KeycapBrush=FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(230,225,199)),10.f);
 MouseLeftIcon=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Image/T_DW_Input_MouseLeft.T_DW_Input_MouseLeft")));
 MouseRightIcon=TSoftObjectPtr<UTexture2D>(FSoftObjectPath(TEXT("/Game/DoughWorld/UI/Image/T_DW_Input_MouseRight.T_DW_Input_MouseRight")));
}
TSharedRef<SWidget> UDWBuildingPlacementHints::RebuildWidget()
{
 LoadedMouseIcons.SetNum(2);LoadedMouseIcons[0]=MouseLeftIcon.LoadSynchronous();LoadedMouseIcons[1]=MouseRightIcon.LoadSynchronous();
 InputGlyphs.SetNum(2);
 for(int32 I=0;I<2;++I)
 {
  if(!InputGlyphs[I])InputGlyphs[I]=NewObject<UDWInputGlyphWidget>(this);
  InputGlyphs[I]->SetPresentationStyle(nullptr,true,FVector2D(40,40),22,false);
  InputGlyphs[I]->SetTextureOverride(LoadedMouseIcons[I]);
  // Placement currently binds these literal keys in DWBuildingPlacementComponent.cpp.
  InputGlyphs[I]->SetInputKey(I==0?EKeys::LeftMouseButton:EKeys::RightMouseButton,FText::GetEmpty());
 }
 for(int32 Icon=0;Icon<2;++Icon)
 {
  MouseBrushes[Icon].SetResourceObject(LoadedMouseIcons[Icon]);MouseBrushes[Icon].DrawAs=ESlateBrushDrawType::Image;
  MouseBrushes[Icon].ImageSize=FVector2D(40.f,40.f);MouseBrushes[Icon].TintColor=FLinearColor::White;
 }
 if(!LoadedMouseIcons[0]||!LoadedMouseIcons[1])UE_LOG(LogTemp,Warning,TEXT("Building placement hint icon assets are missing; text fallback is diagnostic only, not approved artwork."));
 FLinearColor FunctionShadow=DWC01UI::Ink();FunctionShadow.A=FMath::Clamp(FunctionShadowOpacity,0.f,1.f);
 auto Rows=SNew(SVerticalBox);
 for(int32 Index=0;Index<4;++Index)
 {
  auto KeyContent=SNew(SOverlay);
  TSharedRef<STextBlock> KeyLabel=Index<2
   ? StaticCastSharedRef<STextBlock>(SNew(SPlacementInkCenteredKey))
   : SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",KeyFontSize))
      .ColorAndOpacity(DWC01UI::Ink()).Justification(ETextJustify::Center);
  KeyLabels[Index]=KeyLabel;
  KeyContent->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)[KeyLabel];
  if(Index>=2)KeyContent->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
  [InputGlyphs[Index-2]->TakeWidget()];
  Rows->AddSlot().AutoHeight().Padding(0.f,0.f,0.f,Index<3?RowGap:0.f)
  [SAssignNew(RowBoxes[Index],SBox).HeightOverride(KeycapSize.Y)
   [SNew(SHorizontalBox)
    +SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
    [SNew(SBox).WidthOverride(KeycapSize.X).HeightOverride(KeycapSize.Y)
     [SAssignNew(Keycaps[Index],SBorder).BorderImage(&KeycapBrush).Padding(FMargin(Index>=2?4.f:6.f)).HAlign(HAlign_Center).VAlign(VAlign_Center)
      [KeyContent]]]
    +SHorizontalBox::Slot().FillWidth(1.f).Padding(KeyTextGap,0.f,4.f,0.f).VAlign(VAlign_Center)
    [SAssignNew(FunctionLabels[Index],STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular",FunctionFontSize))
     .ColorAndOpacity(FLinearColor::White).ShadowOffset(FVector2D(0.f,1.f)).ShadowColorAndOpacity(FunctionShadow).AutoWrapText(false)]]];
 }
 auto Content=SAssignNew(RootBox,SBox).WidthOverride(EffectivePanelWidth)
  .HeightOverride(KeycapSize.Y*4.f+RowGap*3.f).Visibility(EVisibility::HitTestInvisible)[Rows];
 if(!Motion){Motion=NewObject<UDWUIOffscreenComponent>(this);Motion->Initialize(this);}
 // Aggregate request includes build/menu/cinematic. Avoid a second registry owner.
 Motion->bFollowCinematics=false;Motion->ExitEdge=EDWUIExitEdge::Right;
 Motion->bAutoExitDistance=false;Motion->ExitDistance=FMath::Max(1.f,SlideDistance);
 Motion->AnticipationSeconds=0.f;Motion->AnticipationDistance=0.f;
 Motion->ExitSeconds=FMath::Max(.01f,ExitSeconds);Motion->ReturnSeconds=FMath::Max(.01f,EnterSeconds);
 Motion->DampingRatio=1.f;Motion->ReturnOscillations=1.f;
 Motion->PreConstruct(IsDesignTime());
 auto Result=Motion->RebuildWidgetWithContent(Content);Motion->Construct();
 bRequestedVisible=false;bHasShown=false;bStyleInitialized=false;
 SetRenderOpacity(0.f);Motion->HideForCinematic();return Result;
}
void UDWBuildingPlacementHints::RefreshPresentation(UDWGameplayWidget* Screen)
{
 if(!Screen||!RootBox.IsValid()||!Motion)return;
 auto* PC=Screen->GetOwningPlayer();auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 auto* Player=Screen->GetPlayer();auto* Placement=IsValid(Player)?Player->FindComponentByClass<UDWBuildingPlacementComponent>():nullptr;
 const auto* LocalPlayer=PC?PC->GetLocalPlayer():nullptr;
 const bool bViewportFocused=LocalPlayer&&LocalPlayer->ViewportClient&&LocalPlayer->ViewportClient->Viewport&&LocalPlayer->ViewportClient->Viewport->HasFocus();
 const auto* DWPC=Cast<ADWPlayerController>(PC);
 auto* GI=Screen->GetGameInstance();auto* Loading=GI?GI->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;
 const bool bHardHidden=!Screen->bUseC01HUD||!HUD||!HUD->IsSessionStarted()||!IsValid(Player)||Player->IsDead()||Player->IsActorBeingDestroyed()
  ||HUD->GetMenuPage()!=EDWMenuPage::None||UDWWorldEventSubsystem::IsPlaying(Screen)
  ||!bViewportFocused||(DWPC&&DWPC->IsGameplayBlocked())
  ||(Loading&&Loading->IsTransitionActive())||(FSlateApplication::IsInitialized()&&!FSlateApplication::Get().IsActive());
 const bool bShow=!bHardHidden&&Placement&&Placement->bPlacing;
 // Preserve the final copy while leaving; a successful commit clears the gameplay flags.
 const bool bValid=Placement&&Placement->bPlacing?Placement->bPlacementValid:bLastValid;
 const bool bEnglish=UDWLocalizationLibrary::GetLanguage(Screen)==EDWGameLanguage::English;
 const float Diameter=FMath::Clamp(FMath::IsFinite(Screen->C01HUDDiameter)?Screen->C01HUDDiameter:248.f,160.f,400.f);
 const FVector2D Margin=Screen->C01HUDMargin.ContainsNaN()?FVector2D(72.f,30.f):Screen->C01HUDMargin;
 // Match the right edge, not the map diameter: preserve readable space on small maps.
 const float SafeMinimumWidth=FMath::Clamp(FMath::IsFinite(MinimumPanelWidth)?MinimumPanelWidth:248.f,248.f,400.f);
 const float DesiredPanelWidth=FMath::Max(Diameter,SafeMinimumWidth);
 if(!FMath::IsNearlyEqual(EffectivePanelWidth,DesiredPanelWidth))
 {EffectivePanelWidth=DesiredPanelWidth;RootBox->SetWidthOverride(EffectivePanelWidth);bStyleInitialized=false;}
 UFont* Font=HUD?HUD->GameUIFont.Get():nullptr;
 if(!bStyleInitialized||bLastEnglish!=bEnglish||bLastValid!=bValid||LastFont.Get()!=Font)
 {
  // Current handbook uses this source in DWGameplayBook.cpp. Paper() is the darker SLOT color;
  // HUD->InventoryPanelTexture belongs to the older InventoryCard and is not the current book.
  KeycapBrush=DWC01UI::Texture(TEXT("T_DW_C01_Menu_Paper_1160x650"),KeycapSize);
  if(!KeycapBrush.GetResourceObject())KeycapBrush=FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(230,225,199)),10.f);
  const TCHAR* KeysCN[]={TEXT("Q"),TEXT("E"),TEXT("左键"),TEXT("右键")};
  const TCHAR* KeysEN[]={TEXT("Q"),TEXT("E"),TEXT("LMB"),TEXT("RMB")};
  const TCHAR* FunctionsCN[]={TEXT("逆时针旋转"),TEXT("顺时针旋转"),bValid?TEXT("确认放置"):TEXT("无法放置"),TEXT("取消")};
  const TCHAR* FunctionsEN[]={TEXT("Rotate counterclockwise"),TEXT("Rotate clockwise"),bValid?TEXT("Place building"):TEXT("Cannot place"),TEXT("Cancel")};
  const FSlateFontInfo NominalFunctionFont=Font?FSlateFontInfo(Font,FunctionFontSize):FCoreStyle::GetDefaultFontStyle("Regular",FunctionFontSize);
  FSlateFontInfo FunctionFont=NominalFunctionFont;FunctionFont.OutlineSettings.OutlineSize=0;
  FText FunctionTexts[4];
  for(int32 Index=0;Index<4;++Index)
  {
   FunctionTexts[Index]=FText::FromString(bEnglish?FunctionsEN[Index]:FunctionsCN[Index]);
   const FSlateFontInfo Fitted=FitPlacementHintFont(NominalFunctionFont,FunctionTexts[Index],FVector2D(EffectivePanelWidth-KeycapSize.X-KeyTextGap-4.f,KeycapSize.Y-12.f));
   FunctionFont.Size=FMath::Min(FunctionFont.Size,Fitted.Size);
  }
  // All four functions share the smallest fitted size; do not visually shrink only the longest row.
  for(int32 Index=0;Index<4;++Index)
  {
   FSlateFontInfo KeyFont=Font?FSlateFontInfo(Font,Index>=2?22:KeyFontSize):FCoreStyle::GetDefaultFontStyle("Regular",Index>=2?22:KeyFontSize);
   KeyFont.OutlineSettings.OutlineSize=0;
   const FText KeyText=FText::FromString(bEnglish?KeysEN[Index]:KeysCN[Index]);
   if(Index>=2)KeyFont=FitPlacementHintFont(KeyFont,KeyText,FVector2D(KeycapSize.X-12.f,KeycapSize.Y-12.f));
   KeyLabels[Index]->SetFont(KeyFont);KeyLabels[Index]->SetText(KeyText);
   KeyLabels[Index]->SetVisibility(Index>=2?EVisibility::Collapsed:EVisibility::HitTestInvisible);
   if(Index>=2&&InputGlyphs.IsValidIndex(Index-2)&&InputGlyphs[Index-2])
   {
    InputGlyphs[Index-2]->SetPresentationStyle(Font,bEnglish,FVector2D(40,40),22,false);
    InputGlyphs[Index-2]->SetInputKey(Index==2?EKeys::LeftMouseButton:EKeys::RightMouseButton,KeyText);
   }
   FunctionLabels[Index]->SetFont(FunctionFont);FunctionLabels[Index]->SetText(FunctionTexts[Index]);
   // Q/E center their actual glyph in OnPaint; do not add the global offset again.
   // Mouse diagnostic fallback and function labels retain their existing offsets.
   const float KeyOffset=(Index>=2&&bEnglish)?-KeyFont.Size*Screen->C01ButtonLatinOpticalOffsetEm:0.f;
   KeyLabels[Index]->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0.f,KeyOffset))));
   FunctionLabels[Index]->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0.f,bEnglish?-FunctionFont.Size*Screen->C01ButtonLatinOpticalOffsetEm:0.f))));
   Keycaps[Index]->SetRenderOpacity(Index==2&&!bValid?.55f:1.f);
   FunctionLabels[Index]->SetColorAndOpacity(FSlateColor(Index==2&&!bValid?FLinearColor::FromSRGBColor(FColor(242,233,220)):FLinearColor::White));
  }
  LastFont=Font;bLastEnglish=bEnglish;bLastValid=bValid;bStyleInitialized=true;
 }
 if(auto* Layout=Cast<UCanvasPanelSlot>(Slot))
 {
  Layout->SetAnchors(FAnchors(1.f,1.f));Layout->SetAlignment(FVector2D(1.f,1.f));
  Layout->SetPosition(FVector2D(-Margin.X,-Margin.Y-Diameter-MinimapGap));
  Layout->SetSize(FVector2D(EffectivePanelWidth,KeycapSize.Y*4.f+RowGap*3.f));
 }
 if(bShow!=bRequestedVisible)
 {bRequestedVisible=bShow;if(bShow){bHasShown=true;Motion->ReturnToScreen();}else Motion->HideForCinematic();}
 // Alpha samples the existing component's offset: no new animation timer/spring.
 const float Alpha=1.f-FMath::Clamp(float(Motion->GetCurrentOffset().X/FMath::Max(1.f,SlideDistance)),0.f,1.f);
 SetRenderOpacity(bHardHidden||!bHasShown?0.f:Alpha);
 SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UDWBuildingPlacementHints::ReleaseSlateResources(bool bReleaseChildren)
{
 if(Motion)Motion->Destruct();RootBox.Reset();
 for(int32 Index=0;Index<4;++Index){RowBoxes[Index].Reset();Keycaps[Index].Reset();KeyLabels[Index].Reset();FunctionLabels[Index].Reset();}
 for(const auto& Glyph:InputGlyphs)if(Glyph)Glyph->ReleaseSlateResources(bReleaseChildren);
 bRequestedVisible=false;bHasShown=false;bStyleInitialized=false;Super::ReleaseSlateResources(bReleaseChildren);
}
