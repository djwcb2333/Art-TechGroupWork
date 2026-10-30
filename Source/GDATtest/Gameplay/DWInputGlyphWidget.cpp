#include "DWInputGlyphWidget.h"
#include "Engine/Font.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Brushes/SlateNoResource.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/SlateRenderer.h"
#include "Fonts/FontCache.h"
namespace
{
// Same measured-ink method as the current placement Q/E keys, extended only to
// dynamic one-character key labels. The actual texture sources need no offset.
class SInputInkCenteredKey final:public STextBlock
{
public:
 SLATE_BEGIN_ARGS(SInputInkCenteredKey){} SLATE_END_ARGS()
 void Construct(const FArguments&)
 {STextBlock::Construct(STextBlock::FArguments().SimpleTextMode(true).AutoWrapText(false)
  .Justification(ETextJustify::Center).ColorAndOpacity(FLinearColor::FromSRGBColor(FColor(90,81,64))).ShadowOffset(FVector2D::ZeroVector));}
 virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Cull,
  FSlateWindowElementList& Out,int32 Layer,const FWidgetStyle& Style,bool Enabled)const override
 {
  const FString Text=GetText().ToString();const float Scale=Geometry.GetAccumulatedLayoutTransform().GetScale();
  if(Text.Len()==1&&Text[0]>=32&&Text[0]<127&&FSlateApplication::IsInitialized()&&FMath::IsFinite(Scale)&&Scale>SMALL_NUMBER)
  {
   const FSlateFontInfo GlyphFontInfo=GetFont();auto& Characters=FSlateApplication::Get().GetRenderer()->GetFontCache()->GetCharacterList(GlyphFontInfo,Scale,FFontOutlineSettings::NoOutline);
   const FCharacterEntry Glyph=Characters.GetCharacter(Text[0],GlyphFontInfo.FontFallback);
   if(Glyph.Valid&&Glyph.USize>0&&Glyph.VSize>0&&Characters.GetMaxHeight()>0&&FMath::IsFinite(Glyph.BitmapRenderScale)&&Glyph.BitmapRenderScale>SMALL_NUMBER)
   {
    const float Bitmap=Glyph.BitmapRenderScale;
    const FVector2D InkCenter((double(Glyph.HorizontalOffset)+double(Glyph.USize)*Bitmap*.5)/Scale,
     (-double(Glyph.VerticalOffset)+(double(Characters.GetMaxHeight())+Glyph.GlobalDescender)/Bitmap+double(Glyph.VSize)*Bitmap*.5)/Scale);
    const FVector2D Offset=FVector2D(Geometry.GetLocalSize())*.5-InkCenter;
    const FGeometry Centered=Geometry.MakeChild(Geometry.GetLocalSize(),FSlateLayoutTransform(FVector2f(Offset)));
    return STextBlock::OnPaint(Args,Centered,Cull,Out,Layer,Style,Enabled);
   }
  }
  return STextBlock::OnPaint(Args,Geometry,Cull,Out,Layer,Style,Enabled);
 }
};
FName InputAsset(const FKey& K)
{
 if(K==EKeys::LeftMouseButton)return TEXT("T_DW_Input_MouseLeft");
 if(K==EKeys::RightMouseButton)return TEXT("T_DW_Input_MouseRight");
 if(K==EKeys::MiddleMouseButton)return TEXT("T_DW_Input_MouseMiddle");
 if(K==EKeys::MouseScrollUp)return TEXT("T_DW_Input_MouseScrollUp");
 if(K==EKeys::MouseScrollDown)return TEXT("T_DW_Input_MouseScrollDown");
 if(K==EKeys::Q)return TEXT("T_DW_Input_KeyQ");if(K==EKeys::E)return TEXT("T_DW_Input_KeyE");
 if(K==EKeys::F)return TEXT("T_DW_Input_KeyF");if(K==EKeys::R)return TEXT("T_DW_Input_KeyR");
 if(K==EKeys::Escape)return TEXT("T_DW_Input_KeyEsc");if(K==EKeys::SpaceBar)return TEXT("T_DW_Input_KeySpace");
 if(K==EKeys::LeftShift||K==EKeys::RightShift)return TEXT("T_DW_Input_KeyShift");
 if(K==EKeys::Tab)return TEXT("T_DW_Input_KeyTab");return NAME_None;
}
FText ShortKeyLabel(const FKey& K,bool English)
{
 if(!K.IsValid())return FText::FromString(English?TEXT("Unbound"):TEXT("未绑定"));
 if(K==EKeys::LeftMouseButton)return FText::FromString(English?TEXT("LMB"):TEXT("左键"));
 if(K==EKeys::RightMouseButton)return FText::FromString(English?TEXT("RMB"):TEXT("右键"));
 if(K==EKeys::MiddleMouseButton)return FText::FromString(English?TEXT("MMB"):TEXT("中键"));
 if(K==EKeys::MouseScrollUp)return FText::FromString(English?TEXT("Wheel up"):TEXT("上滚"));
 if(K==EKeys::MouseScrollDown)return FText::FromString(English?TEXT("Wheel down"):TEXT("下滚"));
 if(K==EKeys::MouseWheelAxis)return FText::FromString(English?TEXT("Wheel"):TEXT("滚轮"));
 if(K==EKeys::Tilde)return FText::FromString(TEXT("~"));return K.GetDisplayName();
}
}
UDWInputGlyphWidget::UDWInputGlyphWidget(const FObjectInitializer& O):Super(O)
{SetVisibility(ESlateVisibility::HitTestInvisible);}
void UDWInputGlyphWidget::SetInputKey(FKey Key,FText FallbackLabel)
{
 if(!bTextOnly&&Diagram==EDWInputGlyphDiagram::None&&PresentedKey==Key&&ExplicitLabel.EqualTo(FallbackLabel))return;
 PresentedKey=Key;ExplicitLabel=FallbackLabel;bTextOnly=false;Diagram=EDWInputGlyphDiagram::None;UpdatePresentation();
}
void UDWInputGlyphWidget::SetTextFallback(FText Text)
{
 if(bTextOnly&&ExplicitLabel.EqualTo(Text))return;
 PresentedKey=FKey();ExplicitLabel=Text;bTextOnly=true;Diagram=EDWInputGlyphDiagram::None;UpdatePresentation();
}
void UDWInputGlyphWidget::SetDiagram(EDWInputGlyphDiagram InDiagram)
{
 if(Diagram==InDiagram&&!bTextOnly)return;
 Diagram=InDiagram;PresentedKey=FKey();ExplicitLabel=FText::GetEmpty();bTextOnly=false;UpdatePresentation();
}
void UDWInputGlyphWidget::SetPresentationStyle(UFont* Font,bool English,FVector2D Size,int32 FontSize,bool DrawCap)
{
 if(Size.ContainsNaN())Size=FVector2D(48,48);
 Size=FVector2D(FMath::Clamp(Size.X,16.,192.),FMath::Clamp(Size.Y,16.,192.));FontSize=FMath::Clamp(FontSize,8,64);
 if(PresentationFont==Font&&bEnglish==English&&DisplaySize.Equals(Size)&&LabelSize==FontSize&&bDrawCap==DrawCap)return;
 PresentationFont=Font;bEnglish=English;DisplaySize=Size;LabelSize=FontSize;bDrawCap=DrawCap;UpdatePresentation();
}
void UDWInputGlyphWidget::SetTextureOverride(UTexture2D* Texture)
{if(TextureOverride!=Texture){TextureOverride=Texture;UpdatePresentation();}}
void UDWInputGlyphWidget::RefreshGlyphAssets()
{
 // Retain live resources while synchronously loading missing imports. A missing
 // cache entry may be retried; an already displayed texture keeps GC ownership.
 for(auto It=LoadedTextures.CreateIterator();It;++It)if(!IsValid(It.Value().Get()))It.RemoveCurrent();
 UpdatePresentation();
}
UTexture2D* UDWInputGlyphWidget::ResolveTexture(FName Name)
{
 if(Name.IsNone())return nullptr;if(const auto* Found=LoadedTextures.Find(Name))return Found->Get();
 const FString Path=FString(TEXT("/Game/DoughWorld/UI/Image/"))+Name.ToString()+TEXT(".")+Name.ToString();
 UTexture2D* Art=LoadObject<UTexture2D>(nullptr,*Path);LoadedTextures.Add(Name,Art);return Art;
}
TSharedRef<SWidget> UDWInputGlyphWidget::RebuildWidget()
{
 auto Layers=SNew(SOverlay);SAssignNew(ImageRow,SHorizontalBox);
 for(int32 I=0;I<2;++I)ImageRow->AddSlot().FillWidth(1.f).VAlign(VAlign_Center)
 [SAssignNew(IconSlots[I],SBox)
  [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::Both)
   [SAssignNew(Images[I],SImage).Image(&IconBrushes[I]).ColorAndOpacity(FLinearColor::White)]]];
 Layers->AddSlot()[ImageRow.ToSharedRef()];
 Label=SNew(SInputInkCenteredKey);
 Layers->AddSlot().HAlign(HAlign_Fill).VAlign(VAlign_Fill)
 [SNew(SScaleBox).Stretch(EStretch::ScaleToFit).StretchDirection(EStretchDirection::DownOnly)
  [Label.ToSharedRef()]];
 auto Result=SAssignNew(RootBox,SBox).WidthOverride(DisplaySize.X).HeightOverride(DisplaySize.Y).Visibility(EVisibility::HitTestInvisible)
 [SAssignNew(Cap,SBorder).BorderImage(&CapBrush).Padding(4)[Layers]];
 UpdatePresentation();return Result;
}
void UDWInputGlyphWidget::UpdatePresentation()
{
 if(!RootBox.IsValid())return;
 RootBox->SetWidthOverride(DisplaySize.X);RootBox->SetHeightOverride(DisplaySize.Y);
 CapBrush=bDrawCap?FSlateBrush(FSlateRoundedBoxBrush(FLinearColor::FromSRGBColor(FColor(230,225,199)),8.f)):FSlateBrush(FSlateNoResource());
 Cap->SetBorderImage(&CapBrush);Cap->SetBorderBackgroundColor(FLinearColor::White);Cap->SetPadding(FMargin(bDrawCap?4.f:0.f));
 FName Names[2]={NAME_None,NAME_None};FText Text;
 if(bTextOnly)Text=ExplicitLabel.IsEmpty()?ShortKeyLabel(FKey(),bEnglish):ExplicitLabel;
 else if(Diagram!=EDWInputGlyphDiagram::None)
 {
  switch(Diagram)
  {
  case EDWInputGlyphDiagram::Mouse:Names[0]=TEXT("T_DW_Input_Mouse");Text=FText::FromString(bEnglish?TEXT("Mouse"):TEXT("鼠标"));break;
  case EDWInputGlyphDiagram::MouseMove:Names[0]=TEXT("T_DW_Input_MouseMove");Text=FText::FromString(bEnglish?TEXT("Move mouse"):TEXT("移动鼠标"));break;
  case EDWInputGlyphDiagram::MouseDrag:Names[0]=TEXT("T_DW_Input_MouseDrag");Text=FText::FromString(bEnglish?TEXT("LMB drag"):TEXT("左键拖拽"));break;
  case EDWInputGlyphDiagram::WASD:Names[0]=TEXT("T_DW_Input_KeysWASD");Text=FText::FromString(TEXT("WASD"));break;
  default:break;
  }
 }
 else
 {
  // Invalid applied keys clear old art and ignore a stale authored fallback.
  Text=PresentedKey.IsValid()&&!ExplicitLabel.IsEmpty()?ExplicitLabel:ShortKeyLabel(PresentedKey,bEnglish);
  if(PresentedKey==EKeys::MouseWheelAxis){Names[0]=TEXT("T_DW_Input_MouseScrollUp");Names[1]=TEXT("T_DW_Input_MouseScrollDown");}
  else Names[0]=InputAsset(PresentedKey);
 }
 UTexture2D* Art[2]={nullptr,nullptr};
 if(!bTextOnly&&(PresentedKey.IsValid()||Diagram!=EDWInputGlyphDiagram::None))
 {Art[0]=TextureOverride?TextureOverride.Get():ResolveTexture(Names[0]);Art[1]=ResolveTexture(Names[1]);}
 bShowingGraphic=Art[0]&&(Names[1].IsNone()||Art[1]);
 for(int32 I=0;I<2;++I)
 {
  IconBrushes[I].SetResourceObject(bShowingGraphic?Art[I]:nullptr);
  IconBrushes[I].DrawAs=bShowingGraphic&&Art[I]?ESlateBrushDrawType::Image:ESlateBrushDrawType::NoDrawType;
  IconBrushes[I].ImageSize=FVector2D(64,64);IconBrushes[I].TintColor=FLinearColor::White;
  Images[I]->SetVisibility(bShowingGraphic&&Art[I]?EVisibility::HitTestInvisible:EVisibility::Collapsed);
  IconSlots[I]->SetVisibility(bShowingGraphic&&Art[I]?EVisibility::HitTestInvisible:EVisibility::Collapsed);
  Images[I]->SetImage(&IconBrushes[I]);Images[I]->Invalidate(EInvalidateWidgetReason::Paint);
 }
 ImageRow->SetVisibility(bShowingGraphic?EVisibility::HitTestInvisible:EVisibility::Collapsed);
 Label->SetVisibility(bShowingGraphic?EVisibility::Collapsed:EVisibility::HitTestInvisible);
 FSlateFontInfo Font=PresentationFont?FSlateFontInfo(PresentationFont.Get(),LabelSize):FCoreStyle::GetDefaultFontStyle("Regular",LabelSize);
 Font.OutlineSettings.OutlineSize=0;Label->SetFont(Font);Label->SetText(Text);
 bool Latin=false,CJK=false;for(TCHAR C:Text.ToString()){Latin|=(C>=TEXT('A')&&C<=TEXT('Z'))||(C>=TEXT('a')&&C<=TEXT('z'));CJK|=C>=0x2e80;}
 const FString LabelString=Text.ToString();const bool InkCentered=LabelString.Len()==1&&LabelString[0]>=32&&LabelString[0]<127;
 Label->SetRenderTransform(TOptional<FSlateRenderTransform>(FSlateRenderTransform(FVector2D(0,!InkCentered&&Latin&&!CJK?-LabelSize*.15f:0.f))));
 RootBox->Invalidate(EInvalidateWidgetReason::Layout);
}
void UDWInputGlyphWidget::ReleaseSlateResources(bool bReleaseChildren)
{
 RootBox.Reset();Cap.Reset();Label.Reset();ImageRow.Reset();for(auto& Image:Images)Image.Reset();for(auto& Box:IconSlots)Box.Reset();
 Super::ReleaseSlateResources(bReleaseChildren);
}
