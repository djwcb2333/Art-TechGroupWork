#include "DWOpeningPrototype.h"
#include "DWOpeningExitVolume.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWPlayerController.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWSourDoughEnemy.h"
#include "DWGameplayHUD.h"
#include "DWUserSettings.h"
#include "UObject/ConstructorHelpers.h"
#include "DWGameplayWidget.h"
#include "DWGameplayCinematic.h"
#include "DWDialogueSequence.h"
#include "DWTextRevealComponent.h"
#include "DWInputGlyphWidget.h"
#include "DWLocalizationLibrary.h"
#include "DWWorldEvent.h"
#include "DWMinimap.h"
#include "DWCombatEquipment.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWWeaponAnimationComponent.h"
#include "Blueprint/WidgetTree.h"
#include "Rendering/DrawElements.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Components/HorizontalBoxSlot.h"
#include "Components/InputComponent.h"
#include "Components/AudioComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraModifier_CameraShake.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Animation/AnimationAsset.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/Font.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/DamageType.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundAttenuation.h"
#include "HAL/FileManager.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

namespace DWOpening
{
 const FVector Up(0,0,1);
 float Ease(float T,float Exponent){return FMath::InterpEaseInOut(0.f,1.f,FMath::Clamp(T,0.f,1.f),Exponent);}
 float Smooth(float T){T=FMath::Clamp(T,0.f,1.f);return T*T*(3-2*T);}
 const FLinearColor Cream(.96f,.93f,.84f),Thought(.80f,.78f,.70f),Gold(.93f,.74f,.40f),Shadow(0,0,0,.85f);
 using FAnchor=FDWOpeningShot::FAnchor;
 FAnchor ToAnchor(const FVector& Subject,const FVector& Loc,const FQuat& Rot,float Focus)
 {
  const FVector To=Subject-Loc;const FRotator R=Rot.Rotator();
  const FVector L=Rot.UnrotateVector(To);const float X=FMath::Max(L.X,To.Size()*.1f+1.f);
  FAnchor A;A.View=FVector(R.Yaw,R.Pitch,FMath::Loge(FMath::Max(To.Size(),1.f)));
  A.Screen=FVector(L.Y/X,L.Z/X,FMath::Loge(FMath::Max(Focus,1.f)));
  return A;
 }
 // Yaw is unwound against a reference so blends take the short way round.
 FAnchor Unwound(FAnchor A,float RefYaw){A.View.X=RefYaw+FMath::FindDeltaAngleDegrees(RefYaw,A.View.X);return A;}
 void FromAnchor(const FVector& Subject,const FAnchor& A,FVector& Loc,FQuat& Rot)
 {
  Rot=FRotator(A.View.Y,A.View.X,0).Quaternion();
  Loc=Subject-Rot.RotateVector(FVector(1,A.Screen.X,A.Screen.Y)).GetSafeNormal()*FMath::Exp(A.View.Z);
 }
 FAnchor BlendAnchor(const FAnchor& A,const FAnchor& B,float T)
 {const FAnchor U=Unwound(B,A.View.X);FAnchor O;O.View=FMath::Lerp(A.View,U.View,T);O.Screen=FMath::Lerp(A.Screen,U.Screen,T);return O;}
}
using namespace DWOpening;
using EShot=FDWOpeningShot::EMode;

// ---------------------------------------------------------------- Overlay

void FDWOpeningFadeText::Tick(float Dt)
{
 if(!Text)return;
 if(Wanted!=Shown)
 {
  Alpha=FMath::Max(0.f,Alpha-Dt*Speed*1.6f);
  if(Alpha<=0){Shown=Wanted;Text->SetText(FText::FromString(Shown));Text->SetColorAndOpacity(WantedColor);}
 }
 else Alpha=FMath::FInterpConstantTo(Alpha,Shown.IsEmpty()?0.f:1.f,Dt,Speed);
 const float A=Smooth(Alpha);
 Text->SetRenderOpacity(A);Text->SetRenderTranslation(FVector2D(0,(1-A)*5));
}

void UDWOpeningOverlay::NativeOnInitialized()
{
 Super::NativeOnInitialized();
 auto* Root=WidgetTree->ConstructWidget<UCanvasPanel>();WidgetTree->RootWidget=Root;
 auto* Font=LoadObject<UFont>(nullptr,TEXT("/Game/DoughWorld/UI/Shared/Fonts/F_DWHandDrawn.F_DWHandDrawn"));
 auto AddImage=[&](const FAnchors& Anchors,const FMargin& Offsets,const FLinearColor& Color,UCanvasPanelSlot** OutSlot=nullptr)
 {
  auto* W=WidgetTree->ConstructWidget<UImage>();W->SetColorAndOpacity(Color);
  auto* S=Root->AddChildToCanvas(W);S->SetAnchors(Anchors);S->SetOffsets(Offsets);
  if(OutSlot)*OutSlot=S;
  return W;
 };
 auto AddText=[&](const FAnchors& Anchors,const FMargin& Offsets,int32 Size,const FLinearColor& Color,ETextJustify::Type Justify,int32 Spacing=0)
 {
  auto* W=WidgetTree->ConstructWidget<UTextBlock>();
  FSlateFontInfo Info(Font,Size);Info.LetterSpacing=Spacing;W->SetFont(Info);
  W->SetColorAndOpacity(Color);W->SetJustification(Justify);W->SetAutoWrapText(true);
  W->SetShadowOffset(FVector2D(1,2));W->SetShadowColorAndOpacity(Shadow);W->SetRenderOpacity(0);
  auto* S=Root->AddChildToCanvas(W);S->SetAnchors(Anchors);S->SetOffsets(Offsets);
  return W;
 };
 // The camera constraint is the only letterbox owner; do not draw a second fixed crop.

 // Objective: top-left, quiet. A small diamond, a label, the goal.
 ObjectiveMark=AddImage(FAnchors(.04f,.08f),FMargin(2,9,9,9),Gold);ObjectiveMark->SetRenderTransformAngle(45);ObjectiveMark->SetRenderOpacity(0);
 ObjectiveHeader=AddText(FAnchors(.04f,.08f),FMargin(22,-1,300,26),14,Gold,ETextJustify::Left,180);ObjectiveHeader->SetText(DWText(this,TEXT("目标"),TEXT("OBJECTIVE")));
 Objective.Text=AddText(FAnchors(.04f,.08f),FMargin(22,22,760,40),24,Cream,ETextJustify::Left);Objective.WantedColor=Cream;Objective.Speed=3.5f;

 // Location card.
 TitleSmall=AddText(FAnchors(.5f,.19f),FMargin(-400,-34,800,30),16,Gold,ETextJustify::Center,420);
 TitleBig=AddText(FAnchors(.5f,.19f),FMargin(-560,-2,1120,64),40,Cream,ETextJustify::Center,60);
 TitleRule=AddImage(FAnchors(.5f,.19f),FMargin(-150,66,300,2),FLinearColor(.93f,.74f,.40f,.8f));TitleRule->SetRenderOpacity(0);


 Prompt.Text=AddText(FAnchors(.5f,.78f),FMargin(-500,0,1000,44),22,Cream,ETextJustify::Center,40);Prompt.WantedColor=Cream;Prompt.Speed=4;
 PromptInputFont=Font;PromptKeyRow=WidgetTree->ConstructWidget<UHorizontalBox>();
 auto* PromptSlot=Root->AddChildToCanvas(PromptKeyRow);PromptSlot->SetAnchors(FAnchors(.5f,.78f));PromptSlot->SetAlignment(FVector2D(.5f,0));PromptSlot->SetAutoSize(true);PromptSlot->SetPosition(FVector2D::ZeroVector);
 for(int32 I=0;I<4;++I)
 {
  auto* Glyph=WidgetTree->ConstructWidget<UDWInputGlyphWidget>();Glyph->SetPresentationStyle(Font,false,FVector2D(42,42),22,true);
  PromptKeyRow->AddChildToHorizontalBox(Glyph)->SetPadding(FMargin(4,0));PromptKeyGlyphs.Add(Glyph);
  auto* Label=WidgetTree->ConstructWidget<UTextBlock>();Label->SetFont(FSlateFontInfo(Font,22));Label->SetColorAndOpacity(Cream);
  auto* LabelSlot=PromptKeyRow->AddChildToHorizontalBox(Label);LabelSlot->SetPadding(FMargin(2,4,18,0));LabelSlot->SetVerticalAlignment(VAlign_Center);PromptKeyLabels.Add(Label);
 }
 HidePromptKeys();PromptKeyRow->SetVisibility(ESlateVisibility::HitTestInvisible);PromptKeyRow->SetRenderOpacity(0);
 // Subtitles sit inside the lower letterbox bar when it is present.
 Subtitle.Text=AddText(FAnchors(.1f,.895f,.9f,.895f),FMargin(0,14,0,50),25,Cream,ETextJustify::Center);Subtitle.Speed=4.5f;

 SkipText=AddText(FAnchors(.965f,.955f),FMargin(-130,-34,130,26),15,FLinearColor(.75f,.73f,.66f),ETextJustify::Right,60);SkipText->SetText(DWText(this,TEXT("下一句"),TEXT("Next line")));
 NextLineGlyph=WidgetTree->ConstructWidget<UDWInputGlyphWidget>();NextLineGlyph->SetPresentationStyle(Font,UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English,FVector2D(36,36),18,true);NextLineGlyph->SetInputKey(EKeys::LeftMouseButton,DWText(this,TEXT("左键"),TEXT("LMB")));
 auto* NextSlot=Root->AddChildToCanvas(NextLineGlyph);NextSlot->SetAnchors(FAnchors(.965f,.955f));NextSlot->SetOffsets(FMargin(-182,-41,36,36));NextLineGlyph->SetRenderOpacity(0);

 BlackImage=AddImage(FAnchors(0,0,1,1),FMargin(0),FLinearColor::Black);BlackImage->SetRenderOpacity(0);
 // Drawn over the black so the first thoughts read in darkness.
 Center.Text=AddText(FAnchors(.1f,.47f,.9f,.47f),FMargin(0,-26,0,60),27,Thought,ETextJustify::Center);Center.WantedColor=Thought;Center.Speed=2.4f;
 // Independent hold hint above the opaque waking cover; preserve the existing LMB hint.
 HoldSkipGlyph=WidgetTree->ConstructWidget<UDWInputGlyphWidget>();HoldSkipGlyph->SetPresentationStyle(Font,UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English,FVector2D(54,36),18,true);HoldSkipGlyph->SetInputKey(EKeys::SpaceBar,DWText(this,TEXT("空格"),TEXT("Space")));
 auto* HoldGlyphSlot=Root->AddChildToCanvas(HoldSkipGlyph);HoldGlyphSlot->SetAnchors(FAnchors(.965f,.955f));HoldGlyphSlot->SetOffsets(FMargin(-191,-85,54,36));HoldSkipGlyph->SetRenderOpacity(0);
 HoldSkipText=AddText(FAnchors(.965f,.955f),FMargin(-130,-78,130,28),15,FLinearColor(.75f,.73f,.66f),ETextJustify::Right);
 HoldSkipText->SetAutoWrapText(false);
 // Game title. The font material reveals the letters behind a ragged fire front (M_SC0101_TitleBurn, param Burn 0..1).
 BurnBig=AddText(FAnchors(.5f,.5f),FMargin(-900,-88,1800,120),88,FLinearColor::White,ETextJustify::Center,150);
 BurnBig->SetAutoWrapText(false);BurnBig->SetShadowColorAndOpacity(FLinearColor::Transparent);
 if(auto* Burn=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DoughWorld/OpeningPrototype/Materials/M_SC0101_TitleBurn.M_SC0101_TitleBurn")))
 {BurnMaterial=UMaterialInstanceDynamic::Create(Burn,this);FSlateFontInfo Info=BurnBig->GetFont();Info.FontMaterial=BurnMaterial;BurnBig->SetFont(Info);}
 else BurnBig->SetColorAndOpacity(FLinearColor(.98f,.9f,.72f));
 BurnSmall=AddText(FAnchors(.5f,.5f),FMargin(-400,48,800,40),22,Gold,ETextJustify::Center,420);
 for(int32 I=0;I<56;++I)
 {auto* E=AddImage(FAnchors(.5f,.5f),FMargin(0,0,4,4),FLinearColor(1,.55f,.18f));E->SetRenderOpacity(0);Embers.Add(E);EmberState.AddDefaulted();}
 FlashImage=AddImage(FAnchors(0,0,1,1),FMargin(0),FLinearColor(1,.97f,.9f));FlashImage->SetRenderOpacity(0);
 SetVisibility(ESlateVisibility::HitTestInvisible);
}
void UDWOpeningOverlay::SetBlack(float Value)
{BlackAlpha=FMath::Clamp(Value,0.f,1.f);if(BlackImage)BlackImage->SetRenderOpacity(BlackAlpha);}
void UDWOpeningOverlay::SetSubtitle(const FString& Value,bool bSpoken)
{
 if(Value!=Subtitle.Wanted&&!Value.IsEmpty())Subtitle.WantedColor=bSpoken?Cream:Thought;
 Subtitle.Wanted=Value;
}
void UDWOpeningOverlay::HidePromptKeys()
{
 for(const auto& G:PromptKeyGlyphs)if(G)G->SetVisibility(ESlateVisibility::Collapsed);
 for(const auto& L:PromptKeyLabels)if(L)L->SetVisibility(ESlateVisibility::Collapsed);
}
void UDWOpeningOverlay::SetPromptKey(int32 Index,ADWPlayerController* Controller,int32 Action,const FText& Label)
{
 if(!Controller||!PromptKeyGlyphs.IsValidIndex(Index)||!PromptKeyLabels.IsValidIndex(Index))return;
 auto* G=PromptKeyGlyphs[Index].Get();auto* L=PromptKeyLabels[Index].Get();if(!G||!L)return;
 const auto A=static_cast<EDWInputAction>(Action);const bool English=UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English;
 G->SetPresentationStyle(PromptInputFont.Get(),English,FVector2D(42,42),22,true);
 G->SetInputKey(Controller->GetAppliedActionKey(A),Controller->GetActionKeyLabel(A));G->SetVisibility(ESlateVisibility::HitTestInvisible);
 L->SetText(Label);L->SetVisibility(Label.IsEmpty()?ESlateVisibility::Collapsed:ESlateVisibility::HitTestInvisible);
}
void UDWOpeningOverlay::ShowMovementPrompt(ADWPlayerController* Controller,bool bStretch)
{
 if(!Controller)return;HidePromptKeys();bGlyphPrompt=true;Prompt.Wanted=TEXT(" ");
 const EDWInputAction Actions[]={EDWInputAction::MoveForward,EDWInputAction::MoveLeft,EDWInputAction::MoveBackward,EDWInputAction::MoveRight};
 for(int32 I=0;I<4;++I)SetPromptKey(I,Controller,int32(Actions[I]),I==3?DWText(this,bStretch?TEXT("舒展身体"):TEXT("移动"),bStretch?TEXT("Stretch"):TEXT("Move")):FText::GetEmpty());
}
void UDWOpeningOverlay::ShowCombatPrompt(ADWPlayerController* Controller,bool bBlock,bool bDash)
{
 if(!Controller)return;HidePromptKeys();bGlyphPrompt=true;Prompt.Wanted=TEXT(" ");
 SetPromptKey(0,Controller,int32(EDWInputAction::Throw),DWText(this,TEXT("攻击"),TEXT("Attack")));
 if(bBlock)SetPromptKey(1,Controller,int32(EDWInputAction::Melee),DWText(this,TEXT("按住格挡"),TEXT("Hold to block")));
 if(bDash)SetPromptKey(2,Controller,int32(EDWInputAction::Dash),DWText(this,TEXT("冲撞"),TEXT("Charge")));
}
void UDWOpeningOverlay::ShowTitle(const FString& Small,const FString& Big,float Seconds)
{
 TitleSmall->SetText(FText::FromString(Small));TitleBig->SetText(FText::FromString(Big));
 // A deliberately line-broken localized location card gets a taller frame and rule.
 // Single-line authored Chinese retains the original 40px / 64px layout.
 const bool bTwoLine=Big.Contains(TEXT("\n"));
 FSlateFontInfo BigFont=TitleBig->GetFont();BigFont.Size=bTwoLine?32:40;TitleBig->SetFont(BigFont);
 if(auto* BigSlot=Cast<UCanvasPanelSlot>(TitleBig->Slot))BigSlot->SetOffsets(FMargin(-560,-2,1120,bTwoLine?96:64));
 if(auto* RuleSlot=Cast<UCanvasPanelSlot>(TitleRule->Slot))RuleSlot->SetOffsets(FMargin(-150,bTwoLine?102:66,300,2));
 TitleTime=0;TitleSeconds=Seconds;
}
void UDWOpeningOverlay::Advance(float Dt)
{
 // No overlay crop: the owned shot camera follows InputGate MovieAspectRatio/transition.
 DipTime+=Dt;
 const float Dip=DipTime<.18f?DipTime/.18f:DipTime<.32f?1.f:DipTime<.8f?1-(DipTime-.32f)/.48f:0.f;
 BlackImage->SetRenderOpacity(FMath::Max(BlackAlpha,Smooth(Dip)));
 FlashAlpha=FMath::Max(0.f,FlashAlpha-Dt*2.6f);FlashImage->SetRenderOpacity(FlashAlpha*FlashAlpha);
 Subtitle.Tick(Dt);Center.Tick(Dt);Objective.Tick(Dt);Prompt.Tick(Dt);
 if(PromptKeyRow)PromptKeyRow->SetRenderOpacity(bGlyphPrompt?Smooth(Prompt.Alpha):0.f);
 if(bGlyphPrompt&&Prompt.Text)Prompt.Text->SetRenderOpacity(0.f);
 const float ObjectiveA=Smooth(Objective.Alpha);
 ObjectiveHeader->SetRenderOpacity(ObjectiveA*.9f);ObjectiveMark->SetRenderOpacity(ObjectiveA);
 Objective.Text->SetRenderTranslation(FVector2D((1-ObjectiveA)*-14,0));
 TitleTime+=Dt;
 const float TitleA=TitleSeconds>0?Smooth(TitleTime/1.1f)*Smooth((TitleSeconds-TitleTime)/1.4f):0.f;
 TitleSmall->SetRenderOpacity(TitleA*.9f);TitleBig->SetRenderOpacity(TitleA);TitleRule->SetRenderOpacity(TitleA*.8f);
 TitleBig->SetRenderTranslation(FVector2D(0,(1-Smooth(TitleTime/1.6f))*8));
 TitleRule->SetRenderScale(FVector2D(Smooth(TitleTime/1.8f),1));
 SkipAlpha=FMath::FInterpConstantTo(SkipAlpha,SkipVisible,Dt,3.f);
 SkipText->SetRenderOpacity(SkipAlpha*.85f);if(NextLineGlyph)NextLineGlyph->SetRenderOpacity(SkipAlpha);
 HoldSkipAlpha=FMath::FInterpConstantTo(HoldSkipAlpha,HoldSkipVisible,Dt,5.f);
 if(HoldSkipText)HoldSkipText->SetRenderOpacity(HoldSkipAlpha*.85f);
 if(HoldSkipGlyph)HoldSkipGlyph->SetRenderOpacity(HoldSkipAlpha);
 AdvanceBurn(Dt);
}
int32 UDWOpeningOverlay::NativePaint(const FPaintArgs& Args,const FGeometry& AllottedGeometry,const FSlateRect& MyCullingRect,FSlateWindowElementList& OutDrawElements,int32 LayerId,const FWidgetStyle& InWidgetStyle,bool bParentEnabled) const
{
 const int32 LastLayer=Super::NativePaint(Args,AllottedGeometry,MyCullingRect,OutDrawElements,LayerId,InWidgetStyle,bParentEnabled);
 if(!HoldSkipGlyph||HoldSkipAlpha<=.001f||SkipProgress<=0.f)return LastLayer;
 // Follow this keycap's actual cached geometry, including viewport DPI scaling.
 const FGeometry& G=HoldSkipGlyph->GetCachedGeometry();const FVector2D Size=G.GetLocalSize();
 if(Size.ContainsNaN()||Size.X<4.||Size.Y<4.)return LastLayer;
 constexpr float Inset=1.f,Radius=7.f; // Existing cap radius 8, inset by half the 2px stroke.
 TArray<FVector2D> Path;Path.Reserve(39);Path.Add(FVector2D(Size.X*.5,Inset));Path.Add(FVector2D(Size.X-Inset-Radius,Inset));
 auto Corner=[&](const FVector2D& Center,float StartDegrees)
 {
  for(int32 I=1;I<=8;++I){const float A=FMath::DegreesToRadians(StartDegrees+90.f*float(I)/8.f);Path.Add(Center+FVector2D(FMath::Cos(A),FMath::Sin(A))*Radius);}
 };
 Corner(FVector2D(Size.X-Inset-Radius,Inset+Radius),-90.f);
 Path.Add(FVector2D(Size.X-Inset,Size.Y-Inset-Radius));Corner(FVector2D(Size.X-Inset-Radius,Size.Y-Inset-Radius),0.f);
 Path.Add(FVector2D(Inset+Radius,Size.Y-Inset));Corner(FVector2D(Inset+Radius,Size.Y-Inset-Radius),90.f);
 Path.Add(FVector2D(Inset,Inset+Radius));Corner(FVector2D(Inset+Radius,Inset+Radius),180.f);
 const FVector2D StartPoint=Path[0];Path.Add(StartPoint); // Never pass an element reference back into its owning TArray.
 float Length=0.f;for(int32 I=1;I<Path.Num();++I)Length+=FVector2D::Distance(Path[I-1],Path[I]);
 float Left=Length*FMath::Clamp(SkipProgress,0.f,1.f);
 TArray<FVector2f> Stroke;Stroke.Reserve(Path.Num());
 auto Local=[&](const FVector2D& P){return FVector2f(AllottedGeometry.AbsoluteToLocal(G.LocalToAbsolute(P)));};
 Stroke.Add(Local(Path[0]));
 for(int32 I=1;I<Path.Num()&&Left>KINDA_SMALL_NUMBER;++I)
 {
  const float Span=FVector2D::Distance(Path[I-1],Path[I]);if(Span<=SMALL_NUMBER)continue;
  if(Left>=Span){Stroke.Add(Local(Path[I]));Left-=Span;}
  else{Stroke.Add(Local(FMath::Lerp(Path[I-1],Path[I],Left/Span)));break;}
 }
 if(Stroke.Num()<2)return LastLayer;
 FSlateDrawElement::MakeLines(OutDrawElements,LastLayer+1,AllottedGeometry.ToPaintGeometry(),MoveTemp(Stroke),ESlateDrawEffect::None,FLinearColor(1,1,1,HoldSkipAlpha*InWidgetStyle.GetColorAndOpacityTint().A),true,2.f);
 return LastLayer+1;
}

void UDWOpeningOverlay::SetHoldSkipHint(bool Active,float Progress,float /*Seconds*/)
{
 const float NewVisible=Active?1.f:0.f,NewProgress=FMath::Clamp(Progress,0.f,1.f);
 const bool Repaint=HoldSkipVisible!=NewVisible||SkipProgress!=NewProgress;
 HoldSkipVisible=NewVisible;SkipProgress=NewProgress;
 if(Repaint)Invalidate(EInvalidateWidgetReason::Paint); // Native stroke depends on these values, not a Slate attribute.
 if(HoldSkipText&&Active)HoldSkipText->SetText(DWText(this,TEXT("长按跳过"),TEXT("Hold to skip")));
}
void UDWOpeningOverlay::BurnTitle(const FString& Big,const FString& Small,float Seconds,float ChapterGap)
{
 BurnBig->SetText(FText::FromString(Big));BurnSmall->SetText(FText::FromString(Small));
 if(auto* ChapterSlot=Cast<UCanvasPanelSlot>(BurnSmall->Slot))ChapterSlot->SetOffsets(FMargin(-400,32.f+FMath::Max(0.f,ChapterGap),800,40));
 BurnTime=0;BurnSeconds=Seconds;
}
void UDWOpeningOverlay::AdvanceBurn(float Dt)
{
 if(!BurnBig||BurnSeconds<=0)return;
 BurnTime+=Dt;
 // The front crosses the title in 2.8s; the letters keep a warm afterglow, the chapter line follows, all of it fades at the end.
 const float Burn=FMath::Clamp((BurnTime-.3f)/2.8f,0.f,1.f),Out=Smooth((BurnSeconds-BurnTime)/1.6f);
 if(BurnMaterial)
 {
  BurnMaterial->SetScalarParameterValue(TEXT("Burn"),Burn);
  BurnMaterial->SetScalarParameterValue(TEXT("Glow"),FMath::Clamp(1-(BurnTime-3.f)/2.6f,0.f,1.f));
  BurnBig->SetRenderOpacity(Out);
 }
 else BurnBig->SetRenderOpacity(Smooth(Burn)*Out);
 BurnSmall->SetRenderOpacity(Smooth((BurnTime-3.5f)/1.2f)*Out*.9f);
 BurnSmall->SetRenderTranslation(FVector2D(0,(1-Smooth((BurnTime-3.5f)/1.6f))*6));
 // Embers: thrown off the fire front while it crosses, a few more drifting off the hot letters afterwards.
 const FVector2D View=GetCachedGeometry().GetLocalSize();
 const float FrontX=(.2f+.6f*(Burn*1.35f-.15f)/.85f-.5f)*View.X,HalfWidth=View.X*.3f;
 const float Rate=Burn>0&&Burn<1?46.f:BurnTime<6.f&&Burn>=1?7.f:0.f;
 EmberClock+=Dt*Rate;
 for(int32 I=0;I<EmberState.Num();++I)
 {
  auto& E=EmberState[I];
  if(E.Age>=E.Life&&EmberClock>=1)
  {
   EmberClock-=1;
   const float X=Burn<1?FrontX+FMath::FRandRange(-14.f,10.f):FMath::FRandRange(-HalfWidth,HalfWidth);
   E.Pos=FVector2D(FMath::Clamp(X,-HalfWidth,HalfWidth),FMath::FRandRange(-44.f,40.f));E.Vel=FVector2D(FMath::FRandRange(-26.f,30.f),FMath::FRandRange(-120.f,-45.f));
   E.Age=0;E.Life=FMath::FRandRange(.8f,2.1f);E.Size=FMath::FRandRange(2.f,5.5f);
  }
  auto* W=Embers[I].Get();
  if(E.Age>=E.Life){W->SetRenderOpacity(0);continue;}
  E.Age+=Dt;E.Vel.X+=FMath::PerlinNoise1D(E.Age*2.f+I*7.1f)*120.f*Dt;E.Vel.Y-=18.f*Dt;E.Pos+=E.Vel*Dt;
  const float X=FMath::Clamp(E.Age/E.Life,0.f,1.f);
  W->SetColorAndOpacity(FMath::Lerp(FLinearColor(1,.85f,.45f),FLinearColor(.9f,.25f,.06f),X));
  W->SetRenderOpacity(FMath::Pow(1-X,1.5f)*Out);
  W->SetRenderTransform(FWidgetTransform(E.Pos,FVector2D(E.Size/4.f*(1-.5f*X)),FVector2D::ZeroVector,45.f));
 }
}

// ---------------------------------------------------------------- Shake

void UDWOpeningTraumaPattern::GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo)const
{OutInfo.Duration=FCameraShakeDuration::Infinite();OutInfo.BlendIn=OutInfo.BlendOut=0.f;}
void UDWOpeningTraumaPattern::UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& Out)
{
 Out.Location=FVector::ZeroVector;Out.Rotation=FRotator::ZeroRotator;
 const auto* Owner=Director.Get();if(!Owner||!FMath::IsFinite(Params.DeltaTime))return;
 Clock+=Params.DeltaTime;
 const float S=Owner->GetShakeAmount();if(S<=0.f)return;
 const float T=float(Clock)*14.f;
 Out.Location=FVector(0,FMath::PerlinNoise1D(T+11.f),FMath::PerlinNoise1D(T+37.f))*9.f*S;
 Out.Rotation=FRotator(FMath::PerlinNoise1D(T+71.f)*1.4f*S,FMath::PerlinNoise1D(T+5.f)*1.1f*S,FMath::PerlinNoise1D(T+93.f)*1.6f*S);
}
UDWOpeningTraumaShake::UDWOpeningTraumaShake(const FObjectInitializer& Init):Super(Init)
{bSingleInstance=true;SetRootShakePattern(CreateDefaultSubobject<UDWOpeningTraumaPattern>(TEXT("OpeningTrauma")));}

// ---------------------------------------------------------------- Director

ADWOpeningPrototypeGameMode::ADWOpeningPrototypeGameMode(){}
ADWOpeningPrototype::ADWOpeningPrototype()
{
 PrimaryActorTick.bCanEverTick=true;PrimaryActorTick.bTickEvenWhenPaused=true;
 OpeningEnemyClass=ADWEnemyCharacter::StaticClass();
 // Run after animation has finalized the hero's bones (its form system rewrites the mesh scale there), so our pose is what renders.
 PrimaryActorTick.TickGroup=TG_PostUpdateWork;
 RootComponent=CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
 InputGate=CreateDefaultSubobject<UDWCinematicComponent>(TEXT("OpeningInputGate"));
 const auto AddOpeningLine=[](TArray<FDWDialogueLine>& TargetLines,const TCHAR* Chinese,const TCHAR* English)
 {FDWDialogueLine Line;Line.ChineseText=FText::FromString(Chinese);Line.EnglishText=FText::FromString(English);TargetLines.Add(MoveTemp(Line));};
 AddOpeningLine(WakeDarkLines,TEXT("（……）"),TEXT("(…)"));AddOpeningLine(WakeDarkLines,TEXT("（好烫。）"),TEXT("(It burns.)"));
 AddOpeningLine(WakeThoughtLines,TEXT("（……不。）"),TEXT("(…No.)"));AddOpeningLine(WakeThoughtLines,TEXT("（一点也不烫。）"),TEXT("(It doesn't burn at all.)"));
 AddOpeningLine(SinkThoughtLines,TEXT("（地面在往下沉。）"),TEXT("(The ground is sinking.)"));
 AddOpeningLine(RevealThoughtLines,TEXT("（那是什么？）"),TEXT("(What are those?)"));AddOpeningLine(RevealThoughtLines,TEXT("（它们在吃地面。）"),TEXT("(They're eating the ground.)"));
 AddOpeningLine(RevealThoughtLines,TEXT("（和它们一样的颜色……一样的质地。）"),TEXT("(The same color… the same texture.)"));AddOpeningLine(RevealThoughtLines,TEXT("（……我也是那个吗？）"),TEXT("(…Am I one of them?)"));
 AddOpeningLine(EncounterSpokenLines,TEXT("那个……"),TEXT("Um…"));AddOpeningLine(EncounterSpokenLines,TEXT("你们听得见吗？"),TEXT("Can you hear me?"));
 AddOpeningLine(ExitThoughtLines,TEXT("（……得离开这里。）"),TEXT("(…I need to get out of here.)"));
 // Native defaults are copied before instance serialization. Never refill these slots in Waiting.
 const auto AddAudio=[this](EDWOpeningAudioEvent Event,const TCHAR* Name)
 {
  FDWOpeningAudioCue CueSlot;
  CueSlot.Sound=LoadObject<USoundBase>(nullptr,*FString::Printf(TEXT("/Game/DoughWorld/OpeningPrototype/Audio/SW_%s.SW_%s"),Name,Name));
  OpeningAudioEvents.Add(Event,CueSlot);
 };
 AddAudio(EDWOpeningAudioEvent::WakeRumble,TEXT("Rumble"));
 AddAudio(EDWOpeningAudioEvent::StretchStart,TEXT("Stretch"));
 AddAudio(EDWOpeningAudioEvent::AmbienceLoop,TEXT("Ambience"));
 AddAudio(EDWOpeningAudioEvent::SplashFirst,TEXT("Splash"));
 AddAudio(EDWOpeningAudioEvent::SplashSlip,TEXT("Splash"));
 AddAudio(EDWOpeningAudioEvent::SplashPush,TEXT("Splash"));
 AddAudio(EDWOpeningAudioEvent::StretchPush,TEXT("Stretch"));
 AddAudio(EDWOpeningAudioEvent::GroundCollapse,TEXT("Sink"));
 AddAudio(EDWOpeningAudioEvent::EscapeObjective,TEXT("Chime"));
 AddAudio(EDWOpeningAudioEvent::GroundTremor,TEXT("Sink"));
 AddAudio(EDWOpeningAudioEvent::CreatureBite,TEXT("Sink"));
 AddAudio(EDWOpeningAudioEvent::ApproachObjective,TEXT("Chime"));
 AddAudio(EDWOpeningAudioEvent::LeapWhoosh,TEXT("Whoosh"));
 AddAudio(EDWOpeningAudioEvent::LeapImpact,TEXT("Impact"));
 AddAudio(EDWOpeningAudioEvent::ExitObjective,TEXT("Chime"));
 AddAudio(EDWOpeningAudioEvent::TitleBurn,TEXT("Burn"));
 AddAudio(EDWOpeningAudioEvent::FeedingChew,TEXT("Chew"));
 static ConstructorHelpers::FObjectFinder<USoundBase> DefaultOpeningBGM(TEXT("/Game/DoughWorld/Audio/Music/Dune_Patrol.Dune_Patrol"));
 OpeningBGM=DefaultOpeningBGM.Object;
 static ConstructorHelpers::FClassFinder<UDWCinematicSubtitleWidget> SubtitleClass(TEXT("/Game/DoughWorld/Cinematics/UI/WBP_DWCinematicSubtitle"));
 if(SubtitleClass.Succeeded()&&!InputGate->SubtitleWidgetClass)InputGate->SubtitleWidgetClass=SubtitleClass.Class;
}
void ADWOpeningPrototype::BeginPlay()
{
 Super::BeginPlay();
 if(InputGate)InputGate->OnExternalCancelRequested.AddUniqueDynamic(this,&ThisClass::CancelOpeningFromCinematic);
 // Covers even a disabled-iris OpenLevel path before the first world frame; no gameplay admission here.
 PrepareArrivalCover(UGameplayStatics::GetPlayerController(this,0));
 // Original package diagnostics remain isolated and are never a formal-save or real-input acceptance test.
 FString Prefix;
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
 const auto* VerificationGI=GetGameInstance<UDWGameInstance>();
 const bool bIsolated=FParse::Value(FCommandLine::Get(),TEXT("DWVerificationSavePrefix="),Prefix)&&
  Prefix.StartsWith(TEXT("DW_QA_"))&&Prefix.Len()<80&&!Prefix.Contains(TEXT("/"))&&!Prefix.Contains(TEXT("\\"))&&
  VerificationGI&&VerificationGI->GetResourceVerificationSavePrefix()==Prefix;
 bAutoTest=bIsolated&&FParse::Param(FCommandLine::Get(),TEXT("DWOpeningAutoTest"));
 bSkipTest=false; // Legacy skip diagnostics must not fast-forward the formal opening.
 if(FParse::Param(FCommandLine::Get(),TEXT("DWOpeningSkipTest")))UE_LOG(LogTemp,Warning,TEXT("SC0101 whole-shot skip diagnostics are disabled; subtitle advancement only."));
 bPoseTest=bIsolated&&FParse::Param(FCommandLine::Get(),TEXT("DWOpeningPoseTest"));
 if(bAutoTest||bPoseTest){DiagnosticsRoot=FPaths::ProjectSavedDir()/TEXT("OpeningPrototype")/Prefix;IFileManager::Get().MakeDirectory(*DiagnosticsRoot,true);}
#endif
 // No world/actor mutation before the GI admits this exact new-adventure opening.

}
bool ADWOpeningPrototype::SetLocked(bool Locked)
{
 if(!Locked){bLocked=false;if(InputGate)InputGate->EndExternalControl();UpdateDialogueInputLease();return true;}
 if(bLocked)return true;
 if(!PC.IsValid()||!Player.IsValid()||!InputGate||!InputGate->BeginExternalControl(PC.Get(),Player.Get()))return false;
 bLocked=true;Player->CancelCombatActions();PC->InvalidateCombatInputLife();
 if(!UpdateDialogueInputLease()||IsActorBeingDestroyed()||!PC.IsValid()||!Player.IsValid())
 {
  bLocked=false;InputGate->EndExternalControl(false);UpdateDialogueInputLease();return false;
 }
 if(auto* Movement=Player->GetCharacterMovement())Movement->StopMovementImmediately();
 return true;
}
void ADWOpeningPrototype::HideMinimapForOpening()
{
 if(bMinimapOverrideOwned||!Player.IsValid())return;
 auto* Map=Player->FindComponentByClass<UDWMinimapComponent>();if(!Map)return;
 HiddenMinimap=Map;bMinimapWasActive=Map->IsActive();
 if(bMinimapWasActive){Map->Deactivate();bMinimapOverrideOwned=true;}
}
void ADWOpeningPrototype::RestoreMinimapState()
{
 if(bMinimapOverrideOwned&&HiddenMinimap.IsValid()&&bMinimapWasActive&&!HiddenMinimap->IsActive())HiddenMinimap->Activate();
 bMinimapOverrideOwned=false;HiddenMinimap.Reset();
}
void ADWOpeningPrototype::RestoreGameplayHUD()
{
 if(bGameplayHUDVisibilityOwned&&HiddenGameplayHUD.IsValid()&&HiddenGameplayHUD->GetVisibility()==ESlateVisibility::Collapsed)
  HiddenGameplayHUD->SetVisibility(SavedGameplayHUDVisibility);
 bGameplayHUDVisibilityOwned=false;HiddenGameplayHUD.Reset();
}
void ADWOpeningPrototype::RestoreTitleWidgets()
{
 for(const auto& Hidden:TitleHidden)if(Hidden.Key.IsValid()&&Hidden.Key->GetVisibility()==ESlateVisibility::Collapsed)
  Hidden.Key->SetVisibility(Hidden.Value);
 TitleHidden.Reset();
}
void ADWOpeningPrototype::RestorePlayerDamage()
{
 if(bDamageOverrideOwned&&Player.IsValid()&&!Player->CanBeDamaged())Player->SetCanBeDamaged(bOriginalDamage);
 bDamageOverrideOwned=false;
}
void ADWOpeningPrototype::RestoreTimeDilation()
{
 if(bTimeDilationOwned&&GetWorld())UGameplayStatics::SetGlobalTimeDilation(this,SavedTimeDilation);
 bTimeDilationOwned=false;HitStop=0;
}
void ADWOpeningPrototype::RestoreDoubleVisibility()
{
 for(const auto& Hidden:DoubleHiddenVisibility)if(Hidden.Key.IsValid()&&!Hidden.Key->IsBeingDestroyed()&&!Hidden.Key->IsVisible())
  Hidden.Key->SetVisibility(Hidden.Value,false);
 DoubleHiddenVisibility.Reset();
}
void ADWOpeningPrototype::RestoreHeroPose()
{
 if(bHeroPoseOwned&&SourceHeroMesh.IsValid()&&!SourceHeroMesh->IsBeingDestroyed())
 {
  SourceHeroMesh->SetRelativeScale3D(MeshScale);
  SourceHeroMesh->SetRelativeLocationAndRotation(MeshLoc,MeshRot);
 }
 bHeroPoseOwned=false;bPoseScale=false;
}
bool ADWOpeningPrototype::PrepareArrivalCover(APlayerController* InController)
{
 auto* GI=GetGameInstance<UDWGameInstance>();
 if(!GI||GI->HasPlayerRestoreFailure()||!GI->IsOpeningPending()||!IsValid(InController)||InController->GetWorld()!=GetWorld()||
    InController!=GI->GetFirstLocalPlayerController()||GetClass()->GetPathName()!=TEXT("/Script/GDATtest.DWOpeningPrototype"))return false;
 if(bPresentationInitialized)return Overlay&&Overlay->IsInViewport();
 auto* OpeningController=Cast<ADWPlayerController>(InController);if(!OpeningController)return false;
 if(!Overlay)Overlay=CreateWidget<UDWOpeningOverlay>(OpeningController);
 if(!Overlay)return false;
 if(!Overlay->IsInViewport())Overlay->AddToViewport(150);
 Overlay->SetBlack(1.f);bArrivalCoverOwned=true;
 return true;
}
bool ADWOpeningPrototype::ValidSequenceParameters()const
{
 const float Values[]={WakeIntroShotSeconds,WakeCloseShotSeconds,RevealLookShotSeconds,EncounterIntroShotSeconds,EncounterTurnShotSeconds,EncounterFaceShotSeconds,RevealReturnSeconds};
 for(float V:Values)if(!FMath::IsFinite(V)||V<0.f||V>120.f)return false;
 return WakeIntroShotSeconds>=.1f&&WakeCloseShotSeconds>=.1f&&RevealLookShotSeconds>=.1f&&EncounterIntroShotSeconds>=.1f&&EncounterTurnShotSeconds>=1.7f&&EncounterFaceShotSeconds>=2.9f&&RevealReturnSeconds<=30.f;
}
bool ADWOpeningPrototype::PrepareArrivalControl(APlayerController* InController)
{
 if(!ValidSequenceParameters()){UE_LOG(LogTemp,Error,TEXT("SC0101 invalid shot settings; refusing control."));CancelArrivalCover();return false;}
 auto* GI=GetGameInstance<UDWGameInstance>();
 if(!bArrivalCoverOwned||!Overlay||!Overlay->IsInViewport()||!GI||!GI->IsOpeningPending()||
    GI->IsPlayerRestorePending()||GI->HasPlayerRestoreFailure()||!IsValid(InController)||InController->GetWorld()!=GetWorld())return false;
 PC=Cast<ADWPlayerController>(InController);Player=PC.IsValid()?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
 if(!PC.IsValid()||!Player.IsValid()||!GI->TryStartOpening(this,Player.Get()))return false;
 return SetLocked(true);
}
void ADWOpeningPrototype::FlushCinematicHandoff(){if(InputGate)InputGate->FlushExternalCompletion();}
void ADWOpeningPrototype::CancelArrivalCover()
{
 if(bPresentationInitialized&&!bPresentationCleaned)return;
 if(!bArrivalCoverOwned&&!bPresentationCleaned)return;
 if(bLocked&&InputGate){bLocked=false;InputGate->EndExternalControl(false);}
 SetActorTickEnabled(false);UpdateDialogueInputLease();
 if(Overlay){Overlay->RemoveFromParent();Overlay=nullptr;}bArrivalCoverOwned=false;
}
void ADWOpeningPrototype::CleanupPresentation(bool bKeepBlackCover)
{
 ShutdownOpeningHoldSkip();
 if(bLocked&&InputGate){bLocked=false;InputGate->EndExternalControl(bKeepBlackCover);}
 bLocked=false;StopOpeningDialogue();
 if(bPresentationCleaned)
 {
  if(!bKeepBlackCover&&Overlay){Overlay->RemoveFromParent();Overlay=nullptr;}
  return;
 }
 bPresentationCleaned=true;bArrivalCoverOwned=false;
 RestoreTimeDilation();RestoreTitleWidgets();RestoreGameplayHUD();RestoreMinimapState();
 RestoreHeroPose();RestoreDoubleVisibility();RestorePlayerDamage();
 // An aborted pre-combat opening keeps the original AI/damage gate, but releases visual requests.
 for(auto& E:Enemies)if(auto* Sour=Cast<ADWSourDoughEnemy>(E.Get());IsValid(Sour)&&Sour->bDirectorPresentationControlled)
 {Sour->SetEatingRequested(false);Sour->ClearDirectorAttackPose();Sour->SetDirectorPresentationControlled(false);Sour->SetActorTickEnabled(false);}
 if(SourceHeroMesh.IsValid()&&BoneFinalizedHandle.IsValid())SourceHeroMesh->UnregisterOnBoneTransformsFinalizedDelegate(BoneFinalizedHandle);
 BoneFinalizedHandle.Reset();
 if(TraumaCameraManager.IsValid()&&ActiveTraumaShake)TraumaCameraManager->StopCameraShake(ActiveTraumaShake,true);
 ActiveTraumaShake=nullptr;TraumaCameraManager.Reset();
 if(bViewTargetOverridden&&PC.IsValid()&&PC->GetViewTarget()==ShotCamera)
 {
  if(PreviousViewTarget.IsValid())PC->SetViewTarget(PreviousViewTarget.Get());
  else if(Player.IsValid())PC->SetViewTarget(Player.Get());
 }
 bViewTargetOverridden=false;bDoubleActive=false;
 if(Double){Double->SetVisibility(false);Double->DestroyComponent();Double=nullptr;}
 if(Ambience){Ambience->Stop();Ambience->DestroyComponent();Ambience=nullptr;}
 for(auto& WeakAudio:OpeningOneShots)if(auto* Audio=WeakAudio.Get()){Audio->Stop();if(IsValid(Audio))Audio->DestroyComponent();}
 OpeningOneShots.Reset();
 if(OpeningMusicHUD.IsValid()&&OpeningMusicLease.IsValid())OpeningMusicHUD->ReleaseOpeningMusic(OpeningMusicLease);
 OpeningMusicHUD.Reset();OpeningMusicLease.Invalidate();
 if(Overlay){if(bKeepBlackCover)Overlay->SetBlack(1.f);else{Overlay->RemoveFromParent();Overlay=nullptr;}}
 if(ShotCamera){ShotCamera->Destroy();ShotCamera=nullptr;}
}
FVector ADWOpeningPrototype::Feet(const AActor* Actor)const
{
 if(!Actor)return EnemyCenter;
 const auto* C=Cast<ACharacter>(Actor);
 return Actor->GetActorLocation()-Up*(C?C->GetCapsuleComponent()->GetScaledCapsuleHalfHeight():0.f);
}
const FDWOpeningAudioCue* ADWOpeningPrototype::ResolveOpeningAudio(EDWOpeningAudioEvent Event)const
{
 const auto* CueSlot=OpeningAudioEvents.Find(Event);
 return CueSlot&&CueSlot->bEnabled&&CueSlot->Sound?CueSlot:nullptr;
}
void ADWOpeningPrototype::TrackOpeningOneShot(UAudioComponent* Audio)
{
 OpeningOneShots.RemoveAll([](const TWeakObjectPtr<UAudioComponent>& WeakAudio){return !WeakAudio.IsValid();});
 if(IsValid(Audio))OpeningOneShots.Add(Audio);
}
void ADWOpeningPrototype::Play(EDWOpeningAudioEvent Event,float Volume,float Pitch)
{
 if(const auto* CueSlot=ResolveOpeningAudio(Event))
 {
  // CreateSound2D creates an inactive component. Route before the first audio frame.
  auto* Audio=UGameplayStatics::CreateSound2D(this,CueSlot->Sound,Volume*FMath::Max(0.f,CueSlot->VolumeMultiplier),Pitch*FMath::Max(.01f,CueSlot->PitchMultiplier),0.f,nullptr,false,true);
  if(Audio){TrackOpeningOneShot(Audio);if(auto* Settings=UDWUserSettings::Resolve(this))Settings->RouteAudio(Audio,EDWSoundCategory::SFX);Audio->Play();}
 }
}
void ADWOpeningPrototype::PlayAt(EDWOpeningAudioEvent Event,const FVector& Location,float Volume,float Pitch)
{
 if(const auto* CueSlot=ResolveOpeningAudio(Event))
 {
  auto* Audio=UGameplayStatics::CreateSound2D(this,CueSlot->Sound,Volume*FMath::Max(0.f,CueSlot->VolumeMultiplier),Pitch*FMath::Max(.01f,CueSlot->PitchMultiplier),0.f,nullptr,false,true);
  if(Audio)
  {
   TrackOpeningOneShot(Audio);
   // Configure the inactive component as a world sound before routing and playing.
   Audio->bAllowSpatialization=true;Audio->SetUISound(false);Audio->SetAttenuationSettings(ChewAttenuation);Audio->SetWorldLocation(Location);
   if(auto* Settings=UDWUserSettings::Resolve(this))Settings->RouteAudio(Audio,EDWSoundCategory::SFX);
   Audio->Play();
  }
 }
}
void ADWOpeningPrototype::StartOpeningAmbience(bool bFadeIn)
{
 if(Ambience)return;
 if(const auto* CueSlot=ResolveOpeningAudio(EDWOpeningAudioEvent::AmbienceLoop))
 {
  const float Volume=FMath::Max(0.f,CueSlot->VolumeMultiplier);
  Ambience=UGameplayStatics::CreateSound2D(this,CueSlot->Sound,.55f*Volume,FMath::Max(.01f,CueSlot->PitchMultiplier),0.f,nullptr,false,false);
  if(Ambience)
  {
   if(auto* Settings=UDWUserSettings::Resolve(this))Settings->RouteAudio(Ambience,EDWSoundCategory::SFX);
   if(bFadeIn)Ambience->FadeIn(4.f,1.f);else Ambience->Play();
  }
 }
}
void ADWOpeningPrototype::AdjustOpeningAmbience(float FadeSeconds,float TargetVolume)
{
 if(Ambience)
 {
  const auto* CueSlot=ResolveOpeningAudio(EDWOpeningAudioEvent::AmbienceLoop);
  Ambience->AdjustVolume(FadeSeconds,CueSlot?TargetVolume:0.f);
 }
}
void ADWOpeningPrototype::StartHitStop(float RealSeconds)
{
 if(!bTimeDilationOwned){SavedTimeDilation=UGameplayStatics::GetGlobalTimeDilation(this);bTimeDilationOwned=true;}
 HitStop=RealSeconds;UGameplayStatics::SetGlobalTimeDilation(this,.05f);
}
void ADWOpeningPrototype::FaceHero(const FVector& Target,float Dt)
{
 if(!Player.IsValid())return;
 const float Yaw=(Target-Player->GetActorLocation()).Rotation().Yaw;
 Player->SetActorRotation(FMath::RInterpTo(Player->GetActorRotation(),FRotator(0,Yaw,0),Dt,3.5f));
}

// ---- Shot list

FDWOpeningShot& ADWOpeningPrototype::AddMove(float Duration,const FVector& A,const FVector& B,const FVector& LookA,const FVector& LookB,float FovA,float FovB)
{
 auto& S=Shots.AddDefaulted_GetRef();S.Mode=EShot::Move;S.Duration=Duration;S.LookA=LookA;S.LookB=LookB;S.FovA=FovA;S.FovB=FovB;
 S.A=Clear(LookA,A);S.B=Clear(LookB,B);
 return S;
}
FDWOpeningShot& ADWOpeningPrototype::AddPath()
{auto& S=Shots.AddDefaulted_GetRef();S.Mode=EShot::Path;S.Duration=0;S.Ease=1.25f;return S;}
void ADWOpeningPrototype::Key(FDWOpeningShot& Path,float Time,const FVector& Pos,const FVector& Look,float Fov)
{
 auto& K=Path.Keys.AddDefaulted_GetRef();K.Time=Time;K.Pos=Clear(Look,Pos);K.Look=Look;K.Fov=Fov;
 if(Path.bAnchored)
 {
  K.Anchor=ToAnchor(HeroCenter(),K.Pos,(Look-K.Pos).Rotation().Quaternion(),(Look-K.Pos).Size());
  if(Path.Keys.Num()>1)K.Anchor=Unwound(K.Anchor,Path.Keys[Path.Keys.Num()-2].Anchor.View.X);
 }
 Path.Duration=FMath::Max(Path.Duration,Time);
}
FVector ADWOpeningPrototype::Clear(const FVector& Look,const FVector& Camera)const
{
 // Like a spring arm: if solid scenery sits between subject and lens, bring the lens in front of it.
 // Whatever the hero or the creatures carry is ignored, and the sweep starts clear of the body.
 FCollisionQueryParams Query(TEXT("OpeningShotClear"),false,this);
 TArray<AActor*> Ignored;
 if(Player.IsValid())Ignored.Add(Player.Get());
 for(const auto& E:Enemies)if(IsValid(E.Get())&&!E->IsActorBeingDestroyed())Ignored.Add(E.Get());
 for(int32 I=0;I<Ignored.Num();++I)if(IsValid(Ignored[I])&&!Ignored[I]->IsActorBeingDestroyed())Ignored[I]->GetAttachedActors(Ignored,false,true);
 Query.AddIgnoredActors(Ignored);
 const FVector Dir=(Camera-Look).GetSafeNormal();const float Length=(Camera-Look).Size();
 const FVector Start=Look+Dir*FMath::Min(110.f*Unit,Length*.5f);
 FHitResult Hit;
 if(GetWorld()->SweepSingleByChannel(Hit,Start,Camera,FQuat::Identity,ECC_Camera,FCollisionShape::MakeSphere(24.f),Query)&&!Hit.bStartPenetrating)
 {
  UE_LOG(LogTemp,Display,TEXT("SC0101 Clear: lens pulled in %.0fcm by %s"),(Camera-Hit.Location).Size(),*GetNameSafe(Hit.GetActor()));
  return Hit.Location-Dir*10.f;
 }
 return Camera;
}
FVector ADWOpeningPrototype::Place(const FVector& Subject,const FRotator& View,float Dist,float Sx,float Sy,float Fov)const
{
 const float Tx=FMath::Tan(FMath::DegreesToRadians(Fov*.5f));
 return Subject-View.RotateVector(FVector(1,Sx*Tx,Sy*Tx*9.f/16.f)).GetSafeNormal()*Dist;
}
FVector ADWOpeningPrototype::PlaceToward(const FVector& Subject,const FVector& Target,float Dist,float Sx,float Sy,float Fov)const
{
 FVector Cam=Subject-(Target-Subject).GetSafeNormal()*Dist;
 for(int32 I=0;I<6;++I)Cam=Place(Subject,(Target-Cam).Rotation(),Dist,Sx,Sy,Fov);
 return Cam;
}
FVector ADWOpeningPrototype::HeroCenter()const
{return bDoubleActive?DoubleCenter:Feet(Player.Get())+Up*62*Unit;}
FDWOpeningShot& ADWOpeningPrototype::AddHold(float Duration)
{auto& S=Shots.AddDefaulted_GetRef();S.Mode=EShot::Hold;S.Duration=Duration;return S;}
FDWOpeningShot& ADWOpeningPrototype::AddToGameplay(float Duration)
{auto& S=Shots.AddDefaulted_GetRef();S.Mode=EShot::ToGameplay;S.Duration=Duration;S.Handheld=0;return S;}
float ADWOpeningPrototype::ShotsTotal()const
{float Sum=0;for(const auto& S:Shots)Sum+=S.Duration;return Sum;}
void ADWOpeningPrototype::CapturePose()
{
 if(!PC.IsValid()||!PC->PlayerCameraManager)return;
 if(OpeningSequence&&IsValid(ShotCamera)&&PC->GetViewTarget()==ShotCamera)
 {BlendLoc=ShotCamera->GetActorLocation();BlendRot=ShotCamera->GetActorQuat();BlendFov=ShotCamera->GetCameraComponent()->FieldOfView;return;}
 BlendLoc=PC->PlayerCameraManager->GetCameraLocation();BlendRot=PC->PlayerCameraManager->GetCameraRotation().Quaternion();
 BlendFov=PC->PlayerCameraManager->GetFOVAngle();
}
void ADWOpeningPrototype::PlayShots()
{
 if(!Shots.Num()||!ShotCamera||!PC.IsValid())return;
 // Start from exactly what is on screen, so entering a beat never pops.
 CapturePose();ShotIndex=0;ShotAge=0;CaptureShot=INDEX_NONE;
 InputGate->ApplyAuthoredCameraPose(ShotCamera,BlendLoc,BlendRot,BlendFov);
 if(PC->GetViewTarget()!=ShotCamera){PC->SetViewTarget(ShotCamera);bViewTargetOverridden=true;}
 TickShots(0.f);
}
void ADWOpeningPrototype::CutToGameplay()
{
 ShotIndex=INDEX_NONE;
 if(ShotCamera)ShotCamera->GetCameraComponent()->PostProcessBlendWeight=0;
 if(PC.IsValid()&&Player.IsValid()&&PC->GetViewTarget()==ShotCamera)PC->SetViewTarget(Player.Get());
 bViewTargetOverridden=false;
}
void ADWOpeningPrototype::TickShots(float Dt)
{
 if(ShotIndex==INDEX_NONE||!ShotCamera)return;
 if(!OpeningSequence)
 {
  // Keep the original non-sequenced Stretch shot behavior and its remainder.
  ShotAge+=Dt;
  while(ShotAge>=Shots[ShotIndex].Duration)
  {
   const bool bWasReturn=Shots[ShotIndex].Mode==EShot::ToGameplay;
   ShotAge-=Shots[ShotIndex].Duration;
   if(++ShotIndex>=Shots.Num()){ShotIndex=INDEX_NONE;if(bWasReturn)CutToGameplay();return;}
   CapturePose();
  }
 }
 else
 {
  if(Shots[ShotIndex].Duration<=0.f)
  {const bool bReturn=Shots[ShotIndex].Mode==EShot::ToGameplay;ShotIndex=INDEX_NONE;if(bReturn)CutToGameplay();return;}
  ShotAge=FMath::Min(Shots[ShotIndex].Duration,ShotAge+FMath::Max(0.f,Dt));
 }
 const FDWOpeningShot& S=Shots[ShotIndex];
 const bool bShotFinished=OpeningSequence&&ShotAge>=S.Duration;
 const float T=ShotAge/S.Duration,A=Ease(T,S.Ease);
 FVector Loc=BlendLoc;FQuat Rot=BlendRot;float Fov=BlendFov,Focus=LastFstop>0?ShotCamera->GetCameraComponent()->PostProcessSettings.DepthOfFieldFocalDistance:1000.f;
 float Fstop=S.Fstop,Weight=1;
 switch(S.Mode)
 {
 case EShot::Move:
 {
  FVector Look=FMath::Lerp(S.LookA,S.LookB,A);
  Loc=FMath::Lerp(S.A,S.B,A);
  if(S.Track.IsValid())Look=FMath::Lerp(Look,S.Track->GetActorLocation(),.8f);
  Rot=(Look-Loc).Rotation().Quaternion();Fov=FMath::Lerp(S.FovA,S.FovB,A);Focus=(Look-Loc).Size();
  break;
 }
 case EShot::Path:
 {
  // Time-parameterised Catmull-Rom through the keys; the ends are extrapolated so the take starts and lands smoothly.
  const auto& K=S.Keys;const float Time=A*S.Duration;
  int32 I=0;while(I<K.Num()-2&&Time>K[I+1].Time)++I;
  const int32 I0=FMath::Max(I-1,0),I2=FMath::Min(I+1,K.Num()-1),I3=FMath::Min(I+2,K.Num()-1);
  const float T0=I0==I?K[I].Time-1.f:K[I0].Time,T1=K[I].Time,T2=K[I2].Time,T3=I3==I2?K[I2].Time+1.f:K[I3].Time;
  const float Clamped=FMath::Clamp(Time,T1,T2);
  Fov=FMath::Lerp(K[I].Fov,K[I2].Fov,Ease((Clamped-T1)/FMath::Max(T2-T1,.01f),1.6f));
  if(S.bAnchored)
  {
   FAnchor P;
   P.View=FMath::CubicCRSplineInterpSafe(K[I0].Anchor.View,K[I].Anchor.View,K[I2].Anchor.View,K[I3].Anchor.View,T0,T1,T2,T3,Clamped);
   P.Screen=FMath::CubicCRSplineInterpSafe(K[I0].Anchor.Screen,K[I].Anchor.Screen,K[I2].Anchor.Screen,K[I3].Anchor.Screen,T0,T1,T2,T3,Clamped);
   FromAnchor(HeroCenter(),P,Loc,Rot);Focus=FMath::Exp(P.Screen.Z);
   break;
  }
  Loc=FMath::CubicCRSplineInterpSafe(K[I0].Pos,K[I].Pos,K[I2].Pos,K[I3].Pos,T0,T1,T2,T3,Clamped);
  FVector Look=FMath::CubicCRSplineInterpSafe(K[I0].Look,K[I].Look,K[I2].Look,K[I3].Look,T0,T1,T2,T3,Clamped);
  if(S.Track.IsValid())Look=FMath::Lerp(Look,S.Track->GetActorLocation(),.6f);
  Rot=(Look-Loc).Rotation().Quaternion();Focus=(Look-Loc).Size();
  break;
 }
 case EShot::Hold:Fstop=LastFstop;break;
 case EShot::ToGameplay:
  if(GameCamera.IsValid())
  {
   // Swing round the hero rather than flying straight at the gameplay camera, so it never leaves the frame.
   const FVector C=HeroCenter();
   const FAnchor From=ToAnchor(C,BlendLoc,BlendRot,Focus),To=ToAnchor(C,GameCamera->GetComponentLocation(),GameCamera->GetComponentQuat(),Focus);
   FromAnchor(C,BlendAnchor(From,To,A),Loc,Rot);
   // Land exactly on the gameplay camera (roll and all).
   if(A>.98f){const float E=(A-.98f)/.02f;Loc=FMath::Lerp(Loc,GameCamera->GetComponentLocation(),E);Rot=FQuat::Slerp(Rot,GameCamera->GetComponentQuat(),E);}
   Fov=FMath::Lerp(BlendFov,GameCamera->FieldOfView,A);
  }
  // Lose the shallow focus early in the pull-out; gameplay has none.
  Fstop=LastFstop;Weight=1-Ease(T*2.5f,2.f);
  break;
 }
 if(S.BlendIn>0&&ShotAge<S.BlendIn)
 {
  const float B=Ease(ShotAge/S.BlendIn,2.f);
  if(S.bAnchored)
  {
   const FVector C=HeroCenter();
   FromAnchor(C,BlendAnchor(ToAnchor(C,BlendLoc,BlendRot,Focus),ToAnchor(C,Loc,Rot,Focus),B),Loc,Rot);
  }
  else{Loc=FMath::Lerp(BlendLoc,Loc,B);Rot=FQuat::Slerp(BlendRot,Rot,B);}
  Fov=FMath::Lerp(BlendFov,Fov,B);
 }
 // Slow, low-frequency operator drift. Trauma shake is layered on top by the camera manager.
 const float H=S.Handheld*(S.Mode==EShot::ToGameplay?1-A:1);
 Rot=Rot*FRotator(FMath::PerlinNoise1D(TotalAge*.33f)*H,FMath::PerlinNoise1D(TotalAge*.27f+17.f)*H,FMath::PerlinNoise1D(TotalAge*.21f+41.f)*H*.5f).Quaternion();
 auto* Cam=ShotCamera->GetCameraComponent();
 InputGate->ApplyAuthoredCameraPose(ShotCamera,Loc,Rot,Fov);
 auto& PP=Cam->PostProcessSettings;
 PP.bOverride_DepthOfFieldFstop=PP.bOverride_DepthOfFieldFocalDistance=Fstop>0;
 PP.DepthOfFieldFstop=FMath::Max(Fstop,.7f);PP.DepthOfFieldFocalDistance=Focus;
 // Gentle shutter: authored moves should read, not smear.
 PP.bOverride_MotionBlurAmount=true;PP.MotionBlurAmount=.3f;
 Cam->PostProcessBlendWeight=Fstop>0?Weight:0.f;
 if(S.Mode!=EShot::Hold&&S.Mode!=EShot::ToGameplay)LastFstop=S.Fstop;
 if(bShotFinished)
 {
  const bool bWasReturn=S.Mode==EShot::ToGameplay;
  if(++ShotIndex>=Shots.Num()){ShotIndex=INDEX_NONE;if(bWasReturn)CutToGameplay();return;}
  ShotAge=0.f;CapturePose();
 }
}

// ---- Beats. Every position is derived from the live hero/enemy layout so the coverage survives level edits.
// Each beat is one continuous take; the only hard cut in the opening is to the creature's face before it pounces.
// Framing rule: distance = subject size / (2 * screen share * tan(FOV/2)).
// Offsets are authored for a hero of radius 90 (about 180 wide, 136 tall) and scale with the measured body (K).
void ADWOpeningPrototype::BuildWake(bool bOnlyCloseUp)
{
 Shots.Reset();const float K=Unit;const FVector C=HeroCenter();
 const FVector Close=C+FVector(40,-600,70)*K,CloseLook=C+FVector(0,0,4)*K;
 auto& Take=AddPath();Take.Shake(bOnlyCloseUp?.14f:.16f).Dof(bOnlyCloseUp?2.8f:3.2f);
 if(!bOnlyCloseUp)
 {
  Key(Take,0.f,C+FVector(-20,-640,150)*K,CrystalFocus+FVector(0,0,40),36);
  Key(Take,WakeIntroShotSeconds*(8.5f/14.4f),C+FVector(-5,-632,140)*K,CrystalFocus+FVector(20,-40,-140),36);
  Key(Take,WakeIntroShotSeconds,C+FVector(20,-615,105)*K,C+FVector(0,0,80)*K,34);
 }
 else
 {
  Key(Take,0.f,C+FVector(20,-615,105)*K,C+FVector(0,0,80)*K,34);
  Key(Take,WakeCloseShotSeconds,Close,CloseLook,32);
 }
}
void ADWOpeningPrototype::BuildReveal()
{
 Shots.Reset();
 const float K=Unit;
 const FVector C=HeroCenter(),E=Enemies.Num()?Feet(Enemies[0].Get()):EnemyCenter;
 // One over-the-shoulder composition held through the whole beat: the hero in the lower left, the feeding ground
 // across the fissure. It only drifts sideways a little; no push-in and no telephoto creep.
 const FVector T=E+Up*50*K;
 auto& Take=AddPath();Take.Anchored().Blend(1.4f).Shake(.1f).Dof(4.f);
 Key(Take,0.f,PlaceToward(C,T,620*K,-.5f,-.32f,40),T,40);
 Key(Take,RevealLookShotSeconds*.5f,PlaceToward(C,T,605*K,-.56f,-.34f,38),T,38);
 Key(Take,RevealLookShotSeconds,PlaceToward(C,T,595*K,-.6f,-.35f,37),T,37);
}
void ADWOpeningPrototype::BuildEncounter(int32 Segment)
{
 Shots.Reset();
 if(!Enemies.IsValidIndex(0)||!IsValid(Enemies[0].Get())||Enemies[0]->IsActorBeingDestroyed()||!EnemyPoses.IsValidIndex(0))return;
 const float K=Unit;
 auto* First=Enemies[0].Get();
 EncounterHero=Player->GetActorLocation();
 const FVector H=Feet(Player.Get()),C=HeroCenter(),E=Feet(First),D=(E-H).GetSafeNormal2D(),Side=-FVector::CrossProduct(Up,D);
 LeapEnd=FVector(H.X+D.X*190*K,H.Y+D.Y*190*K,First->GetActorLocation().Z);
 auto& P=EnemyPoses[0];if(Segment==0){P.TurnFrom=P.Yaw;P.TurnTo=P.Yaw+FMath::FindDeltaAngleDegrees(P.Yaw,(H-E).Rotation().Yaw);}
 // Over the hero's shoulder at the creature's back (hero lower right); held, with a slight drift, through both lines and the turn.
 const FVector Look=E+Up*65*K;
 const FVector BackA=PlaceToward(C,Look,800*K,.5f,-.12f,32),BackB=PlaceToward(C,Look,790*K,.47f,-.11f,32);
 const FVector Mid=FMath::Lerp(BackA,BackB,1.1f/7.6f);
 if(Segment==0)AddMove(EncounterIntroShotSeconds,BackA,Mid,Look,Look,32,32).Blend(FMath::Min(1.1f,EncounterIntroShotSeconds)).Dof(2.8f).Shake(.1f).Curve(1.6f);
 else if(Segment==1)AddMove(EncounterTurnShotSeconds,Mid,BackB,Look,Look,32,32).Dof(2.8f).Shake(.1f).Curve(1.6f);
 else {auto& Face=AddMove(EncounterFaceShotSeconds,H+D*165*K+Side*255*K+Up*123*K,H+D*175*K+Side*250*K+Up*121*K,E+Up*70*K,E+Up*68*K,31,31).Dof(2.4f).Shake(.06f);Face.Track=First;}
}

// ---- Stage flow

void ADWOpeningPrototype::Enter(EDWOpeningStage Next)
{
 // Fail before taking a stage lock; the arrival path validates before acquiring its own lock too.
 if((Next==EDWOpeningStage::Wake||Next==EDWOpeningStage::Reveal||Next==EDWOpeningStage::Encounter)&&!ValidSequenceParameters())
 {FailOpeningDialogue(TEXT("Invalid opening shot settings."));return;}
 // Exit dialogue may continue during the linear fade; it finishes before the title starts.
 const bool Lock=Next==EDWOpeningStage::Wake||Next==EDWOpeningStage::StretchPrompt||Next==EDWOpeningStage::Stretch||Next==EDWOpeningStage::Reveal||Next==EDWOpeningStage::Encounter;
 if(!SetLocked(Lock))return; // No stage advance, shots or cues without our lease.
 Stage=Next;Age=PrevAge=0;bShotTaken=false;Visited.Add(Next);RequestOpeningAspect(Lock);
 ResetOpeningHoldSkipForStage();
 TimingActiveBeat=NAME_None;
 OpeningSequence.Reset();PreparedSequenceStep=DWOpeningSteps::Step::Done;
 if(Next==EDWOpeningStage::Wake||Next==EDWOpeningStage::Reveal||Next==EDWOpeningStage::Encounter)
 {
  const auto P=Next==EDWOpeningStage::Wake?DWOpeningSteps::Scene::Wake:Next==EDWOpeningStage::Reveal?DWOpeningSteps::Scene::Reveal:DWOpeningSteps::Scene::Encounter;
  OpeningSequence=MakeUnique<DWOpeningSteps::Sequence>(P);
 }
 RefreshExitTriggerStatus();
 UE_LOG(LogTemp,Display,TEXT("SC0101 Stage=%d"),int32(Next));
 if(Overlay){Overlay->SetLetterbox(Lock);Overlay->SetPrompt(TEXT(""));if(Lock)Overlay->SetObjective(TEXT(""));}
 switch(Next)
 {
 case EDWOpeningStage::Wake:Play(EDWOpeningAudioEvent::WakeRumble,.85f);break;
 case EDWOpeningStage::Stretch:
  StretchStart=TotalAge+.08f;Play(EDWOpeningAudioEvent::StretchStart,.9f);
  {
   // The lens stays put and only tilts up with the hero as it rises, so the head never leaves the frame.
   Shots.Reset();
   const FVector Cam=PC->PlayerCameraManager->GetCameraLocation(),Look=Cam+PC->PlayerCameraManager->GetCameraRotation().Vector()*FVector::Dist(Cam,HeroCenter());
   AddMove(3.8f,Cam,Cam+Up*25*Unit,Look,Feet(Player.Get())+Up*82*Unit,32,34).Dof(2.8f).Shake(.08f).Curve(1.6f);
   AddToGameplay(2.2f).Curve(2.4f);PlayShots();
  }
  break;
 case EDWOpeningStage::Escape:TremorTimer=5.5f;EquipStartingWeapon();break;
 case EDWOpeningStage::Reveal:if(Overlay)Overlay->HideTitle();break;
 case EDWOpeningStage::Encounter:break;
 case EDWOpeningStage::Combat:
 {
  // The opening owns a silent lease until the actual combat tutorial begins.
  // Reuse the HUD's single component and the same owner/token; never layer a second BGM.
  if(OpeningMusicHUD.IsValid()&&OpeningMusicLease.IsValid())
  {
   const FGuid CombatMusicLease=OpeningMusicHUD->AcquireOpeningMusic(this,OpeningBGM,OpeningBGMVolumeMultiplier,OpeningBGMPitchMultiplier);
   if(CombatMusicLease.IsValid())OpeningMusicLease=CombatMusicLease;
  }
  RestoreTimeDilation();
  CutToGameplay();
  RestorePlayerDamage();
  if(bHeroPoseOwned&&SourceHeroMesh.IsValid())SourceHeroMesh->SetRelativeLocationAndRotation(MeshLoc,MeshRot);
  RestoreMinimapState();
  for(int32 I=0;I<Enemies.Num();++I)if(auto* E=Enemies[I].Get();IsValid(E)&&!E->IsActorBeingDestroyed())
  {
   if(auto* Sour=Cast<ADWSourDoughEnemy>(E))
   {Sour->SetEatingRequested(false);Sour->ClearDirectorAttackPose();Sour->SetDirectorPresentationControlled(false);}
   else if(E->PlaceholderBody&&EnemyPoses.IsValidIndex(I))E->PlaceholderBody->SetRelativeTransform(EnemyPoses[I].BodyBase);
   E->SetCanBeDamaged(true);E->SetActorTickEnabled(true);E->GetCharacterMovement()->SetMovementMode(MOVE_Walking);E->DetectionRadius=3000;E->LoseTargetRadius=4000;
  }
  RestoreGameplayHUD();
  AdjustOpeningAmbience(1.5f,1.f);
  if(Overlay)
  {
   Overlay->SetLetterbox(false,7.f);Overlay->SetSubtitle(TEXT(""),true);
   Overlay->ShowTitle(DWText(this,TEXT("系 统"),TEXT("SYSTEM")).ToString(),DWText(this,TEXT("战斗教程开始"),TEXT("COMBAT TUTORIAL")).ToString(),3.4f);Overlay->SetObjective(DWText(this,TEXT("击退馊团"),TEXT("Defeat the Sour Dough")).ToString());
   UpdateCombatPrompt();
  }
  break;
 }
 case EDWOpeningStage::Title:
 {
  bExitTitleStarted=false;ExitTitleElapsed=0.f;
  // Keep the actual player camera/control while entering the light and fading. No forced walking.
  if(Overlay){Overlay->SetObjective(TEXT(""));Overlay->SetPrompt(TEXT(""));Overlay->SetBlack(0.f);}
  AdjustOpeningAmbience(2.f,.6f);
  break;
 }
 default:break;
 }
}
void ADWOpeningPrototype::SkipCurrentShot()
{
 // Legacy unconditional Blueprint entry stays disabled; guarded Space-hold uses CompleteHeldOpeningScene.
 UE_LOG(LogTemp,Display,TEXT("SC0101 legacy unguarded skip is disabled; hold Space for a safe scene skip. Left click advances one line."));
}

// ---- Bodies

void ADWOpeningPrototype::TickHero(float Dt)
{
 if(!Player.IsValid())return;
 TickDouble(Dt);
 // The real mesh only ever squashes: the recoil when the creature hits it.
 auto* Mesh=Player->GetMesh();
 Recoil+=Dt;
 if(Recoil>=.4f){if(bHeroPoseOwned)RestoreHeroPose();return;}
 const float K=FMath::Sin(Recoil/.4f*PI)*(1-Recoil/.4f);
 const FVector S(1+.3f*K,1+.3f*K,1-.35f*K);
 // S is in capsule axes; the imported mesh is rotated, so put the height on whichever of its own axes points up.
 FVector Local(S.X);
 {
  int32 UpAxis=2;float Best=-1;
  for(int32 I=0;I<3;++I){FVector E(0);E[I]=1;const float Z=FMath::Abs(MeshRot.RotateVector(E).Z);if(Z>Best){Best=Z;UpAxis=I;}}
  Local[UpAxis]=S.Z;
 }
 PoseScale=MeshScale*Local;bHeroPoseOwned=true;bPoseScale=true;Mesh->SetRelativeScale3D(PoseScale);
 // Squash about the soles.
 const FVector Soles(0,0,-Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
 Mesh->SetRelativeLocation(Soles+S*(MeshLoc-Soles));
}
void ADWOpeningPrototype::TickDouble(float Dt)
{
 if(!bDoubleActive||!Double)return;
 // Pose: 1 = the last frame of the hero's fall (lying), 0 = its first (standing); played backwards, the fall is a get-up
 // the bones actually perform. Roll: 1 = turned onto its back, 0 = the fall's own lying pose.
 // Beats: a heave that doesn't make it, a slip back with a splash, then a real push, rolling forward onto its feet.
 struct FKey{float Time,Pose,Roll;};
 static const FKey Keys[]={{0.f,1.f,1.f},{.5f,.8f,.78f},{.95f,.97f,.94f},{1.25f,.92f,.86f},{2.1f,.28f,0.f},{2.65f,0.f,0.f}};
 constexpr int32 Last=UE_ARRAY_COUNT(Keys)-1;
 const float T=StretchStart<0?-1.f:TotalAge-StretchStart;
 float Pose=1,Roll=1;
 if(T>=Keys[Last].Time){Pose=0;Roll=0;}
 else if(T>0)
 {
  int32 I=0;while(T>Keys[I+1].Time)++I;
  const float A=Ease((T-Keys[I].Time)/(Keys[I+1].Time-Keys[I].Time),2.f);
  Pose=FMath::Lerp(Keys[I].Pose,Keys[I+1].Pose,A);Roll=FMath::Lerp(Keys[I].Roll,Keys[I+1].Roll,A);
 }
 // 1 while it floats, easing to 0 as its feet find the floor.
 const float Float=T<0?1.f:1-Smooth((T-1.f)/1.1f);
 const float K=Unit,Clock=TotalAge;
 // Shallow water: the body rides a little proud of the floor, bobbing and rocking; the struggle kicks it about.
 const float Depth=bWaterFound?WaterZ-DoubleBase.GetLocation().Z:0.f;
 float Lift=FMath::Max(0.f,Depth-26.f*K)+(3.2f*FMath::Sin(Clock*1.35f)+1.2f*FMath::Sin(Clock*2.2f+1.f))*K;
 float RockRoll=3.5f*FMath::Sin(Clock*1.1f),RockPitch=2.f*FMath::Sin(Clock*.8f+2.f);
 if(T>0&&T<1.3f){const float Kick=FMath::Exp(-T*2.2f)*FMath::Sin(T*10.f);Lift+=4.f*K*Kick;RockRoll+=7.f*Kick;}
 Lift*=Float;RockRoll*=Float;RockPitch*=Float;
 // Landing: a squash as the weight arrives on its feet.
 FVector Scale(1);
 if(T>2.35f&&T<2.9f){const float X=(T-2.35f)/.55f,S=FMath::Sin(X*PI)*(1-.5f*X);Scale=FVector(1+.07f*S,1+.07f*S,1-.1f*S);}
 FVector Perp=FVector::CrossProduct(FVector::UpVector,LieAxis).GetSafeNormal();if(Perp.IsNearlyZero())Perp=FVector::ForwardVector;
 const FQuat Q=FQuat(LieAxis,FMath::DegreesToRadians(LieAngle*Roll+RockRoll))*FQuat(Perp,FMath::DegreesToRadians(RockPitch));
 const FVector Pivot=FMath::Lerp(StandPivot,LiePivot,Pose);
 // Scale about the soles, roll about the body's middle, then float.
 FTransform World=FTransform(FQuat::Identity,FVector::ZeroVector,Scale)*FTransform(Q,Pivot-Q.RotateVector(Pivot))*DoubleBase;
 World.AddToTranslation(FVector(0,0,Lift));
 Double->SetWorldTransform(World);
 if(auto* Instance=Double->GetSingleNodeInstance()){Instance->SetPlaying(false);Instance->SetPosition(Instance->GetLength()*Pose,false);}
 DoubleCenter=World.TransformPosition(Pivot);
 // Hand back to the real mesh once it stands: same mesh, same spot, its idle from here on.
 if(T>=2.95f){bDoubleActive=false;ShowDouble(false);}
}
void ADWOpeningPrototype::ProbeLyingPose()
{
 UAnimationAsset* Fall=Player->DeathAnimation;
 if(!Double||!Fall)return;
 Double->PlayAnimation(Fall,false);
 auto* Instance=Double->GetSingleNodeInstance();if(!Instance)return;
 Instance->SetPlaying(false);
 const auto Sample=[&](float Time)
 {
  Instance->SetPosition(Time,false);Double->TickAnimation(0.f,false);Double->RefreshBoneTransforms();
  return TArray<FTransform>(Double->GetComponentSpaceTransforms());
 };
 const TArray<FTransform> Stand=Sample(0.f),Lie=Sample(Instance->GetLength());
 Instance->SetPosition(Instance->GetLength(),false);
 if(Stand.Num()<3||Stand.Num()!=Lie.Num()){UE_LOG(LogTemp,Warning,TEXT("SC0101 Probe: no bones (%d/%d)"),Stand.Num(),Lie.Num());return;}
 StandPivot=LiePivot=FVector::ZeroVector;
 for(int32 I=0;I<Stand.Num();++I){StandPivot+=Stand[I].GetLocation()/Stand.Num();LiePivot+=Lie[I].GetLocation()/Lie.Num();}
 // Body = the bone right under the root; head = the highest bone when standing.
 int32 Body=1,Head=1;
 for(int32 I=1;I<Stand.Num();++I)if(Stand[I].GetLocation().Z>Stand[Head].GetLocation().Z)Head=I;
 const FQuat Turn=Lie[Body].GetRotation()*Stand[Body].GetRotation().Inverse();
 // The mesh is yawed inside the capsule; the actor's forward, in the mesh's own space, is where its face is.
 const FVector Face=MeshRot.Inverse().RotateVector(FVector::ForwardVector),Belly=Turn.RotateVector(Face);
 const FVector L=(Lie[Head].GetLocation()-Lie[Body].GetLocation()).GetSafeNormal();
 const FVector B=(Belly-L*(Belly|L)).GetSafeNormal(),Z=(FVector::UpVector-L*L.Z).GetSafeNormal();
 if(!L.IsNearlyZero()&&!B.IsNearlyZero()&&!Z.IsNearlyZero()){LieAxis=L;LieAngle=FMath::RadiansToDegrees(FMath::Atan2(L|(B^Z),B|Z));}
 UE_LOG(LogTemp,Display,TEXT("SC0101 Probe bones=%d head=%s face=%s belly=%s axis=%s roll=%.0f standPivot=%s liePivot=%s"),Stand.Num(),*Double->GetBoneName(Head).ToString(),
  *Face.ToCompactString(),*Belly.ToCompactString(),*LieAxis.ToCompactString(),LieAngle,*StandPivot.ToCompactString(),*LiePivot.ToCompactString());
}
void ADWOpeningPrototype::AddRipple(const FVector& At,float Radius,float Strength,float Life)
{
 if(!bWaterFound||!RippleMesh||!RippleMaterial)return;
 int32 I=Ripples.IndexOfByPredicate([](const FRipple& R){return R.Age>=R.Life;});
 if(I==INDEX_NONE)
 {
  if(Ripples.Num()>=12)return;
  auto* Disc=NewObject<UStaticMeshComponent>(this);
  Disc->SetupAttachment(RootComponent);Disc->SetUsingAbsoluteLocation(true);Disc->SetUsingAbsoluteRotation(true);Disc->SetUsingAbsoluteScale(true);
  Disc->SetStaticMesh(RippleMesh);Disc->SetCollisionEnabled(ECollisionEnabled::NoCollision);Disc->SetCastShadow(false);Disc->RegisterComponent();
  auto* Mid=UMaterialInstanceDynamic::Create(RippleMaterial,this);Disc->SetMaterial(0,Mid);
  I=Ripples.AddDefaulted();RippleDiscs.Add(Disc);RippleMids.Add(Mid);
 }
 Ripples[I].Age=0;Ripples[I].Life=Life;Ripples[I].Radius=Radius;Ripples[I].Strength=Strength;
 RippleDiscs[I]->SetWorldLocation(FVector(At.X,At.Y,WaterZ+.6f));RippleDiscs[I]->SetVisibility(true);
 TickRipples(0.f);
}
void ADWOpeningPrototype::TickRipples(float Dt)
{
 if(Water.IsValid())WaterZ=Water->GetComponentsBoundingBox(true).Max.Z;
 for(int32 I=0;I<Ripples.Num();++I)
 {
  auto& R=Ripples[I];auto* Disc=RippleDiscs[I].Get();
  if(!Disc)continue;
  if(R.Age>=R.Life){Disc->SetVisibility(false);continue;}
  R.Age+=Dt;
  const float X=FMath::Clamp(R.Age/R.Life,0.f,1.f),Radius=R.Radius*(.2f+.8f*(1-FMath::Square(1-X)));
  // The ring sits at 0.6 of the disc's bounding radius (M_SC0101_Ripple).
  Disc->SetWorldScale3D(FVector(Radius/42.4f,Radius/42.4f,.004f));
  const FVector At=Disc->GetComponentLocation();Disc->SetWorldLocation(FVector(At.X,At.Y,WaterZ+.6f));
  RippleMids[I]->SetScalarParameterValue(TEXT("Fade"),R.Strength*FMath::Square(1-X));
 }
}
void ADWOpeningPrototype::EquipStartingWeapon()
{
 if(bWeaponGiven||!bFormalFlowAdmitted||!Player.IsValid()||!Player->IsCombatActionAllowed())return;
 auto* W=Player->WeaponAnimationComponent.Get();
 // Ownership and inventory were committed together before the durable opening checkpoint.
 // This presentation callback only selects the admitted owned weapon; it never grants a second item.
 if(W&&!W->GetCurrentEquipmentId().IsNone()){bWeaponGiven=true;return;} // Preserve an earlier explicit selection.
 if(!W||!W->CanSelectCombatEquipment(StartingWeapon))
 {
  if(!bStarterSelectionWarningShown)
  {
   bStarterSelectionWarningShown=true;UE_LOG(LogTemp,Warning,TEXT("DW_OPENING_STARTER_SELECTION_NOT_READY"));
   if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)H->Notify(DWText(this,TEXT("起始武器尚未就绪；可在背包中选择已拥有的武器。"),TEXT("Starting weapon is not ready. Choose a weapon you own in your inventory.")));
  }
  return;
 }
 bWeaponGiven=W->SelectWeapon(StartingWeapon);
 if(bWeaponGiven)UE_LOG(LogTemp,Display,TEXT("DW_OPENING_STARTER_SELECTED kind=%s"),*StaticEnum<EDWAnimatedWeapon>()->GetNameStringByValue(int64(StartingWeapon)));
}
void ADWOpeningPrototype::UpdateCombatPrompt()
{
 if(!Overlay||!PC.IsValid()||!Player.IsValid())return;
 const auto* W=Player->WeaponAnimationComponent.Get();EDWAnimatedWeapon Kind=EDWAnimatedWeapon::Unarmed;
 const bool bShield=W&&DWCombatEquipment::ToPresentation(W->GetCurrentEquipmentId(),Kind)&&Kind==EDWAnimatedWeapon::ShieldAxe;
 // A01 Normal cannot sprint/charge. The controller still owns all real ability admission.
 const bool bCharge=Player->GetCombatBaseForm()==EDWCombatBaseForm::Yeast;
 Overlay->ShowCombatPrompt(PC.Get(),bShield,bCharge);
}
bool ADWOpeningPrototype::TickFormalFlow(float RealDt)
{
 if(!bFormalFlowAdmitted)return false;
 auto* GI=GetGameInstance<UDWGameInstance>();
 if(!GI||!Player.IsValid()||!PC.IsValid()){CleanupPresentation();SetActorTickEnabled(false);return true;}
 if(Player->IsDead()||bAwaitingRestart)
 {
  // The GI rejects death autosave during the opening, preserving the living start checkpoint.
  bAwaitingRestart=true;CleanupPresentation();DeathRetryElapsed+=FMath::Max(0.f,RealDt);
  if(DeathRetryElapsed>=FMath::Max(0.f,DeathRetryDelaySeconds))
  {
   DeathRetryElapsed=0.f;
   if(InputGate)InputGate->ReleaseExternalResources();
   const bool bRestarted=GI->RestartOpening(this,Player.Get());
   // Load replaces the GI request owner even when map travel rejects. Never
   // keep retrying this old director's now-invalid registration.
   SetActorTickEnabled(false);
   if(!bRestarted&&PC.IsValid()&&!bFlowErrorShown)
   {bFlowErrorShown=true;if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->Notify(GI->LastSaveError.IsEmpty()?DWText(this,TEXT("开场重试未能启动，请读取当前槽回到安全起点。"),TEXT("Could not restart the opening. Load this slot to return to the safe checkpoint.")):GI->LastSaveError);}
  }
  return true;
 }
 if(bAwaitingHandoff)
 {
  FlowRetryElapsed+=FMath::Max(0.f,RealDt);
  if(FlowRetryElapsed>=FMath::Max(.1f,HandoffRetrySeconds))
  {
   FlowRetryElapsed=0.f;
   if(IsActorBeingDestroyed()||!Player.IsValid()||!PC.IsValid()||!GI->IsOpeningDirectorCurrent(this,Player.Get()))
   {CleanupPresentation();SetActorTickEnabled(false);return true;}
   if(GI->CompleteOpening(this,Player.Get()))SetActorTickEnabled(false);
   else
   {
    // Failure must reveal the recovery/error UI, never strand the player under our retained cover.
    CleanupPresentation();
    if(!bFlowErrorShown)
    {
     bFlowErrorShown=true;
     if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)
      H->Notify(GI->LastSaveError.IsEmpty()?DWText(this,TEXT("开场交接已暂停，请返回标题后读取当前槽重试。"),TEXT("The opening handoff was paused. Return to the title screen and reload this slot.")):GI->LastSaveError);
    }
    // Phase 2 may already be durable, or a callback may have selected another slot.
    // Neither case may submit the old director again or claim an unverified disk result.
    if(!GI->IsOpeningDirectorCurrent(this,Player.Get()))SetActorTickEnabled(false);
   }
  }
  return true;
 }
 if(bPresentationCleaned){SetActorTickEnabled(false);return true;}
 return false;
}
void ADWOpeningPrototype::OnOpeningEnemyDestroyed(AActor* Actor)
{
 // OnDestroyed runs while this matching actor's fields still exist, before GC
 // nulls the stored reference. Disappearance alone is never a defeat fact.
 const int32 Index=Enemies.IndexOfByPredicate([Actor](const auto& E){return E.Get()==Actor;});
 auto* Enemy=Cast<ADWEnemyCharacter>(Actor);
 if(!Enemy||!EnemyPoses.IsValidIndex(Index))return;
 if(FMath::IsFinite(Enemy->Health)&&Enemy->Health<=0.f&&Enemy->State==EDWEnemyState::Dead)
  EnemyPoses[Index].bDefeatCommitted=true;
 else if(GetWorld()&&!GetWorld()->bIsTearingDown&&!IsActorBeingDestroyed())bUnexpectedEnemyLoss=true;
}
void ADWOpeningPrototype::RefreshEnemyDefeatFacts()
{
 for(int32 I=0;I<EnemyPoses.Num();++I)
 {
  auto* Enemy=Enemies.IsValidIndex(I)?Enemies[I].Get():nullptr;
  if(IsValid(Enemy)&&FMath::IsFinite(Enemy->Health)&&Enemy->Health<=0.f&&Enemy->State==EDWEnemyState::Dead)
   EnemyPoses[I].bDefeatCommitted=true;
  if((!IsValid(Enemy)||Enemy->IsActorBeingDestroyed())&&!EnemyPoses[I].bDefeatCommitted)bUnexpectedEnemyLoss=true;
 }
}
bool ADWOpeningPrototype::AllEnemiesDown()const
{
 if(Enemies.IsEmpty()||Enemies.Num()!=EnemyPoses.Num()||bUnexpectedEnemyLoss)return false;
 for(const auto& Pose:EnemyPoses)if(!Pose.bDefeatCommitted)return false;
 return true;
}
void ADWOpeningPrototype::CreateDouble()
{
 auto* Source=Player.IsValid()?Player->GetMesh():nullptr;
 if(Double||!Source||!Source->GetSkeletalMeshAsset())return;
 Double=NewObject<USkeletalMeshComponent>(this,TEXT("HeroDouble"));
 Double->SetupAttachment(RootComponent);Double->SetUsingAbsoluteLocation(true);Double->SetUsingAbsoluteRotation(true);Double->SetUsingAbsoluteScale(true);
 Double->SetSkeletalMesh(Source->GetSkeletalMeshAsset());
 Double->SetCollisionEnabled(ECollisionEnabled::NoCollision);Double->SetCastShadow(true);Double->bEnableUpdateRateOptimizations=false;
 Double->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 Double->RegisterComponent();
 for(int32 I=0;I<Source->GetNumMaterials();++I)Double->SetMaterial(I,Source->GetMaterial(I));
 Double->SetWorldTransform(Source->GetComponentTransform());
 Double->SetAnimationMode(EAnimationMode::AnimationSingleNode);
 Double->SetVisibility(false);
 DoubleBase=Source->GetComponentTransform();
 ProbeLyingPose();
 UE_LOG(LogTemp,Display,TEXT("SC0101 Double mesh=%s death=%s idle=%s"),*GetNameSafe(Source->GetSkeletalMeshAsset()),*GetNameSafe(Player->DeathAnimation),*GetNameSafe(Player->IdleAnimation));
}
void ADWOpeningPrototype::ShowDouble(bool bShow)
{
 if(!Double||!Player.IsValid())return;
 Double->SetVisibility(bShow);
 if(bShow)
 {
  auto* Source=Player->GetMesh();
  if(DoubleHiddenVisibility.IsEmpty())
  {
   TArray<USceneComponent*> VisualChildren;Source->GetChildrenComponents(true,VisualChildren);VisualChildren.Add(Source);
   for(auto* Child:VisualChildren)if(IsValid(Child))DoubleHiddenVisibility.Add(Child,Child->IsVisible());
  }
  Source->SetVisibility(false,true);
 }
 else RestoreDoubleVisibility();
}
void ADWOpeningPrototype::ReapplyPoseScale()
{if(bPoseScale&&SourceHeroMesh.IsValid()&&!SourceHeroMesh->GetRelativeScale3D().Equals(PoseScale))SourceHeroMesh->SetRelativeScale3D(PoseScale);}
void ADWOpeningPrototype::TickEnemies(float Dt)
{
 if(Stage>=EDWOpeningStage::Combat)return;
 for(int32 I=0;I<Enemies.Num()&&I<EnemyPoses.Num();++I)
 {
  auto* E=Enemies[I].Get();auto& P=EnemyPoses[I];
  if(!IsValid(E))continue;
  if(auto* Sour=Cast<ADWSourDoughEnemy>(E))
  {
   // Reuse the preserved Phase values as staggered starts; the AnimBP owns Enter/Loop/Exit.
   Sour->SetEatingRequested(P.bFeeding&&TotalAge>=P.Phase);
  }
  else if(E->PlaceholderBody)
  {
   float Nod=0;
   if(P.bFeeding){const float W=FMath::Max(0.f,FMath::Sin(TotalAge*3.3f+P.Phase));Nod=W*W;}
   FTransform T=P.BodyBase;
   T.SetRotation(FRotator(-18.f*Nod,0,0).Quaternion()*P.BodyBase.GetRotation());
   T.AddToTranslation(FVector(12.f*Nod,0,-8.f*Nod));
   T.SetScale3D(P.BodyBase.GetScale3D()*FVector(1+.06f*Nod,1+.06f*Nod,1-.09f*Nod));
   E->PlaceholderBody->SetRelativeTransform(T);
  }
  if(!P.bFree)
  {
   E->SetActorRotation(FRotator(0,P.Yaw,0));
   E->SetActorLocation(FVector(P.Home.X,P.Home.Y,P.Home.Z-P.Sink),false,nullptr,ETeleportType::TeleportPhysics);
  }
 }
}

bool ADWOpeningPrototype::IsOpeningDialogueBusy()const
{return !PendingDialogueBeats.IsEmpty()||(OpeningDialogue&&OpeningDialogue->IsPlaying());}
bool ADWOpeningPrototype::UpdateDialogueInputLease()
{
 if(bSubtitlePressHeld&&DialogueLeaseController.IsValid()&&DialogueLeaseController->HasObservedPresentationKeyRelease(this))bSubtitlePressHeld=false;
 const bool Need=bLocked||IsOpeningDialogueBusy()||bSubtitlePressHeld;
 if(!Need)
 {
  if(DialogueLeaseController.IsValid())
  {
   if(bSubtitleInputPushed&&OpeningSubtitleInput)DialogueLeaseController->PopInputComponent(OpeningSubtitleInput);
   if(bPresentationKeyOwned)DialogueLeaseController->ReleasePresentationKey(this);
  }
  bSubtitleInputPushed=bPresentationKeyOwned=false;DialogueLeaseController.Reset();return true;
 }
 if(!PC.IsValid())return false;
 if(!bPresentationKeyOwned)
 {
  if(!PC->AcquirePresentationKey(this,EKeys::LeftMouseButton))return false;
  DialogueLeaseController=PC;bPresentationKeyOwned=true;bSubtitlePressHeld=PC->IsInputKeyDown(EKeys::LeftMouseButton);
 }
 if(!OpeningSubtitleInput)
 {
  OpeningSubtitleInput=NewObject<UInputComponent>(this,NAME_None,RF_Transient);OpeningSubtitleInput->RegisterComponent();
  OpeningSubtitleInput->Priority=10000;OpeningSubtitleInput->bBlockInput=false;
  for(const auto Event:{IE_Pressed,IE_DoubleClick}){auto& B=OpeningSubtitleInput->BindKey(EKeys::LeftMouseButton,Event,this,&ThisClass::OpeningSubtitlePressed);B.bConsumeInput=true;B.bExecuteWhenPaused=true;}
  auto& ReleaseBinding=OpeningSubtitleInput->BindKey(EKeys::LeftMouseButton,IE_Released,this,&ThisClass::OpeningSubtitleReleased);ReleaseBinding.bConsumeInput=true;ReleaseBinding.bExecuteWhenPaused=true;
 }
 if(!bSubtitleInputPushed){PC->PushInputComponent(OpeningSubtitleInput);bSubtitleInputPushed=true;}
 return true;
}
void ADWOpeningPrototype::OpeningSubtitlePressed()
{
 if(bSubtitlePressHeld)return;bSubtitlePressHeld=true;
 if(InputGate&&InputGate->bAllowClickToNextLine&&PC.IsValid()&&!PC->IsPaused()&&OpeningDialogue&&OpeningDialogue->IsPlaying())OpeningDialogue->SkipDialogueLine();
 // A finished final line never changes Stage/Age/shot state or calls SkipCurrentShot.
}
void ADWOpeningPrototype::OpeningSubtitleReleased()
{if(DialogueLeaseController.IsValid()&&DialogueLeaseController->HasObservedPresentationKeyRelease(this))bSubtitlePressHeld=false;}
void ADWOpeningPrototype::CancelOpeningFromCinematic()
{
 SetActorTickEnabled(false);
 if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)
  H->Notify(DWText(this,TEXT("开场已取消，请读取当前槽重试；安全起点已保留。"),TEXT("The opening was cancelled. Reload this slot; the safe checkpoint is preserved.")));
 CleanupPresentation(false);
 if(bDirectorTickInProgress&&InputGate)InputGate->DeferExternalCompletionUntilDirectorTickExit();
}
void ADWOpeningPrototype::OpeningDialogueFinished(bool Completed)
{
 if(bStoppingOpeningDialogue)return;
 if(!Completed){CompleteSequenceDialogue(false);FailOpeningDialogue(TEXT("The shared subtitle queue stopped before completion."));return;}
 CompleteSequenceDialogue(true);
 TimingActiveBeat=NAME_None;
 if(OpeningSubtitle)OpeningSubtitle->SetVisibility(ESlateVisibility::Collapsed);
 if(Overlay)Overlay->SetDialogueHint(false);
 // ActorTick releases the input stack only after the physical Up; never Pop during this callback.
}
void ADWOpeningPrototype::StopOpeningDialogue()
{
 if(OpeningSequence)OpeningSequence->Cancel();
 TGuardValue<bool> Stopping(bStoppingOpeningDialogue,true);PendingDialogueBeats.Reset();if(Overlay)Overlay->SetDialogueHint(false);
 if(OpeningDialogue){OpeningDialogue->OnFinished.RemoveDynamic(this,&ThisClass::OpeningDialogueFinished);OpeningDialogue->StopDialogue();OpeningDialogue->DestroyComponent();OpeningDialogue=nullptr;}
 if(OpeningSubtitle){OpeningSubtitle->RemoveFromParent();OpeningSubtitle=nullptr;}bSubtitleCanvasLayout=false;
 if(DialogueLeaseController.IsValid())
 {
  if(bSubtitleInputPushed&&OpeningSubtitleInput)DialogueLeaseController->PopInputComponent(OpeningSubtitleInput);
  if(bPresentationKeyOwned)DialogueLeaseController->ReleasePresentationKey(this);
 }
 bSubtitleInputPushed=bPresentationKeyOwned=false;DialogueLeaseController.Reset();bSubtitlePressHeld=false;
 if(OpeningSubtitleInput){OpeningSubtitleInput->DestroyComponent();OpeningSubtitleInput=nullptr;}
}
void ADWOpeningPrototype::FailOpeningDialogue(const FString& Error)
{
 if(bDialogueFailure)return;bDialogueFailure=true;UE_LOG(LogTemp,Error,TEXT("SC0101 subtitle rejected: %s"),*Error);
 CleanupPresentation();SetActorTickEnabled(false);
 if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)H->Notify(DWText(this,TEXT("开场字幕初始化失败，请读取当前槽重试；安全起点已保留。"),TEXT("Opening subtitles could not start. Reload the current slot; the safe checkpoint is preserved.")));
 if(!bDirectorTickInProgress)FlushCinematicHandoff();
}
bool ADWOpeningPrototype::QueueDialogueBeat(FName Beat)
{
 if(StartedDialogueBeats.Contains(Beat))return true;StartedDialogueBeats.Add(Beat);PendingDialogueBeats.Add(Beat);
 return StartNextDialogueBeat();
}
bool ADWOpeningPrototype::StartNextDialogueBeat()
{
 if(bDialogueFailure)return false;if((OpeningDialogue&&OpeningDialogue->IsPlaying())||PendingDialogueBeats.IsEmpty())return true;
 if(!PC.IsValid()||!InputGate||!InputGate->SubtitleWidgetClass){FailOpeningDialogue(TEXT("Missing formal subtitle widget class or player controller."));return false;}
 if(!OpeningSubtitle)
 {
  OpeningSubtitle=InputGate->CreateAuthoredSubtitle(PC.Get(),160);
  if(!OpeningSubtitle){FailOpeningDialogue(TEXT("Cannot create formal subtitle widget."));return false;}
  OpeningSubtitle->SetVisibility(ESlateVisibility::HitTestInvisible);
  auto* Text=OpeningSubtitle->GetDialogueTextBlock();auto* Reveal=UDWTextRevealLibrary::GetTextRevealComponent(Text);
  if(!Text||!Reveal||Reveal->bExternalClock){FailOpeningDialogue(TEXT("SubtitleText needs the existing automatic DW Text Reveal."));return false;}
  if(InputGate->VoiceProfile)Reveal->VoiceProfile=InputGate->VoiceProfile; // Null preserves the authored WBP voice.
  if(auto* Canvas=Cast<UCanvasPanelSlot>(Text->Slot)){OriginalSubtitleLayout=Canvas->GetLayout();bSubtitleCanvasLayout=true;}
 }
 if(!OpeningDialogue)
 {
  OpeningDialogue=InputGate->CreateAuthoredDialogue();if(!OpeningDialogue){FailOpeningDialogue(TEXT("Cannot create shared dialogue queue."));return false;}OpeningDialogue->bAutoAdvance=true;
  OpeningDialogue->OnFinished.AddUniqueDynamic(this,&ThisClass::OpeningDialogueFinished);
 }
 const FName Beat=PendingDialogueBeats[0];PendingDialogueBeats.RemoveAt(0);
 const TArray<FDWDialogueLine>* ConfiguredLines=nullptr;const bool Centered=Beat==TEXT("WakeDark");
 if(Beat==TEXT("WakeDark"))ConfiguredLines=&WakeDarkLines;
 else if(Beat==TEXT("WakeThought"))ConfiguredLines=&WakeThoughtLines;
 else if(Beat==TEXT("SinkThought"))ConfiguredLines=&SinkThoughtLines;
 else if(Beat==TEXT("RevealThought"))ConfiguredLines=&RevealThoughtLines;
 else if(Beat==TEXT("EncounterSpoken"))ConfiguredLines=&EncounterSpokenLines;
 else if(Beat==TEXT("ExitThought"))ConfiguredLines=&ExitThoughtLines;
 else {FailOpeningDialogue(TEXT("Unknown subtitle beat."));return false;}
 if(!FMath::IsFinite(DialogueHoldMultiplier)||DialogueHoldMultiplier<0.f){FailOpeningDialogue(TEXT("Invalid subtitle hold multiplier."));return false;}
 // Preserve authored instance English. Only known legacy Chinese lines with empty English
 // receive the matching fallback; unknown user edits remain intact for the map audit.
 static const TMap<FString,FString> LegacyOpeningTranslations={{TEXT("（……）"),TEXT("(…)")},{TEXT("（好烫。）"),TEXT("(It burns.)")},{TEXT("（……不。）"),TEXT("(…No.)")},{TEXT("（一点也不烫。）"),TEXT("(It doesn't burn at all.)")},{TEXT("（地面在往下沉。）"),TEXT("(The ground is sinking.)")},{TEXT("（那是什么？）"),TEXT("(What are those?)")},{TEXT("（它们在吃地面。）"),TEXT("(They're eating the ground.)")},{TEXT("（和它们一样的颜色……一样的质地。）"),TEXT("(The same color… the same texture.)")},{TEXT("（……我也是那个吗？）"),TEXT("(…Am I one of them?)")},{TEXT("那个……"),TEXT("Um…")},{TEXT("你们听得见吗？"),TEXT("Can you hear me?")},{TEXT("（……得离开这里。）"),TEXT("(…I need to get out of here.)")}};
 TArray<FDWDialogueLine> Lines=*ConfiguredLines;bool HasText=false;
 for(auto& Line:Lines)if(Line.EnglishText.IsEmptyOrWhitespace())
  if(const FString* English=LegacyOpeningTranslations.Find(Line.ChineseText.ToString()))Line.EnglishText=FText::FromString(*English);
 for(auto& Line:Lines)
 {
  if(!FMath::IsFinite(Line.HoldSeconds)||Line.HoldSeconds<0.f){FailOpeningDialogue(TEXT("Invalid per-line subtitle hold."));return false;}
  Line.HoldSeconds*=DialogueHoldMultiplier;
  if(!FMath::IsFinite(Line.HoldSeconds)){FailOpeningDialogue(TEXT("Subtitle hold overflow."));return false;}
  HasText|=!Line.Resolve(this).IsEmptyOrWhitespace();
 }

 TimingActiveBeat=Beat;
 if(!HasText){CompleteSequenceDialogue(true);TimingActiveBeat=NAME_None;if(OpeningSubtitle)OpeningSubtitle->SetVisibility(ESlateVisibility::Collapsed);return true;}
 auto* Text=OpeningSubtitle->GetDialogueTextBlock();
 if(auto* Canvas=Cast<UCanvasPanelSlot>(Text->Slot);Canvas&&bSubtitleCanvasLayout)
 {
  Canvas->SetLayout(OriginalSubtitleLayout);
  if(Centered){Canvas->SetAnchors(FAnchors(.1f,.47f,.9f,.47f));Canvas->SetAlignment(FVector2D::ZeroVector);Canvas->SetOffsets(FMargin(0,-26,0,60));}
 }
 OpeningSubtitle->SetVisibility(ESlateVisibility::HitTestInvisible);OpeningDialogue->Lines=MoveTemp(Lines);
 // No ShowCue/PlayText here: PlayDialogue alone owns reveal, hold, and the shared WBP voice.
 if(!OpeningDialogue->PlayDialogue(Text)||!UpdateDialogueInputLease()){FailOpeningDialogue(OpeningDialogue?OpeningDialogue->LastError:TEXT("Subtitle presentation input is unavailable."));return false;}
 return true;
}
void ADWOpeningPrototype::TickOpeningDialogue()
{
 if(OpeningDialogue&&OpeningDialogue->IsPlaying())
 {
  const bool Paused=PC.IsValid()&&PC->IsPaused();if(Paused&&!OpeningDialogue->IsPaused())OpeningDialogue->PauseDialogue();else if(!Paused&&OpeningDialogue->IsPaused())OpeningDialogue->ResumeDialogue();
 }
 if(Overlay)Overlay->SetDialogueHint(InputGate&&InputGate->bAllowClickToNextLine&&OpeningDialogue&&OpeningDialogue->IsPlaying());
 if(PC.IsValid()&&PC->IsPaused())return;
 if(!StartNextDialogueBeat())return;
 UpdateDialogueInputLease();
}
void ADWOpeningPrototype::RequestOpeningAspect(bool Movie)
{if(ShotCamera&&InputGate)InputGate->RequestExternalAspect(ShotCamera,Movie?FMath::Clamp(InputGate->MovieAspectRatio,.5f,4.f):InitialOpeningAspect);}
void ADWOpeningPrototype::UpdateOpeningAspect(float Dt)
{if(ShotCamera&&InputGate)InputGate->AdvanceExternalAspect(ShotCamera,Dt);}

void ADWOpeningPrototype::CompleteSequenceDialogue(bool bSuccess)
{
 if(!OpeningSequence||OpeningSequence->CurrentKind()!=DWOpeningSteps::Kind::Dialogue)return;
 const auto S=OpeningSequence->Current();
 const FName Expected=S==DWOpeningSteps::Step::WakeDark?FName(TEXT("WakeDark")):S==DWOpeningSteps::Step::WakeThought?FName(TEXT("WakeThought")):S==DWOpeningSteps::Step::RevealThought?FName(TEXT("RevealThought")):FName(TEXT("EncounterSpoken"));
 if(TimingActiveBeat==Expected)OpeningSequence->Complete(ActiveSequenceDialogueToken,DWOpeningSteps::Kind::Dialogue,bSuccess?DWOpeningSteps::Completion::Success:DWOpeningSteps::Completion::Failed);
}
bool ADWOpeningPrototype::PrepareSequenceStep()
{
 bSequencePreparedThisFrame=false;if(!OpeningSequence)return true;
 const auto S=OpeningSequence->Current();
 if(S==DWOpeningSteps::Step::Cancelled){FailOpeningDialogue(TEXT("Opening sequence cancelled."));return false;}
 if(S==DWOpeningSteps::Step::Done)
 {
  const auto Next=Stage==EDWOpeningStage::Wake?EDWOpeningStage::StretchPrompt:Stage==EDWOpeningStage::Reveal?EDWOpeningStage::Approach:EDWOpeningStage::Combat;
  Enter(Next);return false;
 }
 if(S==PreparedSequenceStep)return true;
 PreparedSequenceStep=S;bSequencePreparedThisFrame=true;
 switch(S)
 {
  case DWOpeningSteps::Step::WakeDark:ActiveSequenceDialogueToken=OpeningSequence->CurrentToken();return QueueDialogueBeat(TEXT("WakeDark"));
  case DWOpeningSteps::Step::WakeIntro:BuildWake(false);PlayShots();break;
  case DWOpeningSteps::Step::WakeThought:ActiveSequenceDialogueToken=OpeningSequence->CurrentToken();return QueueDialogueBeat(TEXT("WakeThought"));
  case DWOpeningSteps::Step::WakeClose:BuildWake(true);PlayShots();break;
  case DWOpeningSteps::Step::RevealLook:BuildReveal();PlayShots();break;
  case DWOpeningSteps::Step::RevealThought:ActiveSequenceDialogueToken=OpeningSequence->CurrentToken();return QueueDialogueBeat(TEXT("RevealThought"));
  case DWOpeningSteps::Step::RevealBite:
   Age=PrevAge=6.6f;PlayAt(EDWOpeningAudioEvent::CreatureBite,Enemies.IsValidIndex(0)&&IsValid(Enemies[0].Get())?Enemies[0]->GetActorLocation():EnemyCenter,1.f,1.2f);AddTrauma(.2f);
   Shots.Reset();AddHold(.35f);PlayShots();break;
  case DWOpeningSteps::Step::RevealReturn:
   if(RevealReturnSeconds==0.f){CutToGameplay();OpeningSequence->Complete(OpeningSequence->CurrentToken(),DWOpeningSteps::Kind::Camera,DWOpeningSteps::Completion::Success);return true;}
   Shots.Reset();AddToGameplay(RevealReturnSeconds).Curve(2.2f);PlayShots();break;
  case DWOpeningSteps::Step::EncounterIntro:BuildEncounter(0);PlayShots();break;
  case DWOpeningSteps::Step::EncounterSpoken:ActiveSequenceDialogueToken=OpeningSequence->CurrentToken();if(EnemyPoses.IsValidIndex(0))EnemyPoses[0].bFeeding=false;return QueueDialogueBeat(TEXT("EncounterSpoken"));
  case DWOpeningSteps::Step::EncounterTurn:Age=PrevAge=5.9f;BuildEncounter(1);PlayShots();break;
  case DWOpeningSteps::Step::EncounterFace:
   for(auto& Other:EnemyPoses)Other.bFeeding=false;AdjustOpeningAmbience(.35f,.03f);
   Age=PrevAge=7.6f;BuildEncounter(2);PlayShots();break;
  default:break;
 }
 if(ShotsDone()){FailOpeningDialogue(TEXT("Opening camera step could not start."));return false;}
 ActiveSequenceShotSeconds=Shots[ShotIndex].Duration;
 return true;
}

// ---- Tick

void ADWOpeningPrototype::Tick(float Dt)
{
 TGuardValue<bool> TickGuard(bDirectorTickInProgress,true);
 ON_SCOPE_EXIT { FlushCinematicHandoff(); };
 Super::Tick(Dt);
 const float RealDt=GetWorld()->DeltaRealTimeSeconds;
 TickOpeningDialogue();if(bDialogueFailure)return;
 if(TickFormalFlow(RealDt)){InvalidateOpeningHoldSkip();return;}
 if(TickOpeningHoldSkip())return;
 if(Stage==EDWOpeningStage::Waiting)
 {
  auto* ArrivalGI=GetGameInstance<UDWGameInstance>();
  if(ArrivalGI&&ArrivalGI->HasPlayerRestoreFailure()){CancelArrivalCover();SetActorTickEnabled(false);return;}
  PrepareArrivalCover(UGameplayStatics::GetPlayerController(this,0));
 }
 if(UGameplayStatics::IsGamePaused(this))return;
 if(HitStop>0){HitStop-=RealDt;if(HitStop<=0)RestoreTimeDilation();}
 if(OpeningSequence&&(!Player.IsValid()||!PC.IsValid()||!IsValid(ShotCamera)||!IsValid(ShotCamera->GetCameraComponent())||!IsValid(InputGate)))
 {FailOpeningDialogue(TEXT("An opening sequence participant was removed."));return;}
 if(!PrepareSequenceStep())return;
 float DirectorDt=Dt;
 if(OpeningSequence)
 {
  if(bSequencePreparedThisFrame||OpeningSequence->CurrentKind()==DWOpeningSteps::Kind::Dialogue)DirectorDt=0.f;
  else if(Shots.IsValidIndex(ShotIndex))DirectorDt=FMath::Min(Dt,FMath::Max(0.f,Shots[ShotIndex].Duration-ShotAge));
  if(OpeningSequence->Current()==DWOpeningSteps::Step::EncounterFace)
  {
   const float Rate=2.9f/FMath::Max(2.9f,ActiveSequenceShotSeconds);
   for(const float Boundary:{9.f,9.8f,10.18f,10.22f,10.5f})if(Age<Boundary&&Age+DirectorDt*Rate>Boundary){DirectorDt=(Boundary-Age)/Rate;break;}
  }
 }
 TotalAge+=OpeningSequence?Dt:DirectorDt;PrevAge=Age;
 if(OpeningSequence&&OpeningSequence->Current()==DWOpeningSteps::Step::EncounterIntro)Age=1.1f*FMath::Clamp((ShotAge+DirectorDt)/ActiveSequenceShotSeconds,0.f,1.f);
 else if(OpeningSequence&&OpeningSequence->Current()==DWOpeningSteps::Step::EncounterTurn)Age=5.9f+1.7f*FMath::Clamp((ShotAge+DirectorDt)/ActiveSequenceShotSeconds,0.f,1.f);
 else if(OpeningSequence&&OpeningSequence->Current()==DWOpeningSteps::Step::EncounterFace)Age=7.6f+2.9f*FMath::Clamp((ShotAge+DirectorDt)/ActiveSequenceShotSeconds,0.f,1.f);
 else Age+=DirectorDt;
 Trauma=FMath::Max(0.f,Trauma-Dt*.8f);
 if(bAutoTest&&TotalAge>200){FinishTest(false,TEXT("timeout"));return;}
 if(Stage==EDWOpeningStage::Waiting)
 {
  if(!bPresentationInitialized)
  {
  PC=Cast<ADWPlayerController>(UGameplayStatics::GetPlayerController(this,0));Player=PC.IsValid()?Cast<ADWPlayerCharacter>(PC->GetPawn()):nullptr;
  if(!Player.IsValid())return;
  auto* GI=GetGameInstance<UDWGameInstance>();
  // With destination pause disabled, do not start shots while the loader still primes the pawn POV.
  if(auto* Loading=GI?GI->GetSubsystem<UDWLoadingTransitionSubsystem>():nullptr;Loading&&Loading->IsTransitionActive())return;
  if(!ValidSequenceParameters()){FailOpeningDialogue(TEXT("Invalid opening shot settings."));return;}
  if(!GI||!GI->TryStartOpening(this,Player.Get()))return;
  bFormalFlowAdmitted=true;
  for(const TCHAR* Tag:{TEXT("OpeningWater"),TEXT("OpeningPitFloor")})
  {TArray<AActor*> Found;UGameplayStatics::GetAllActorsWithTag(this,Tag,Found);for(auto* A:Found)A->SetActorEnableCollision(false);}
  auto* OriginalMesh=Player->GetMesh();
  if(!OriginalMesh)
  {
   CleanupPresentation();SetActorTickEnabled(false);
   if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->Notify(DWText(this,TEXT("开场角色组件缺失，请检查角色蓝图后重新读取；起点档已保留。"),TEXT("The opening character mesh component is missing. Check the character Blueprint and reload; the checkpoint is preserved.")));
   return;
  }
  if(!SetLocked(true))return;
  SourceHeroMesh=OriginalMesh;MeshScale=OriginalMesh->GetRelativeScale3D();MeshLoc=OriginalMesh->GetRelativeLocation();MeshRot=OriginalMesh->GetRelativeRotation().Quaternion();
  PreviousViewTarget=PC->GetViewTarget();bPresentationInitialized=true;bPresentationCleaned=false;bArrivalCoverOwned=false;
  if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))
  {
   OpeningMusicLease=H->AcquireOpeningMusic(this,nullptr,OpeningBGMVolumeMultiplier,OpeningBGMPitchMultiplier);
   if(OpeningMusicLease.IsValid())OpeningMusicHUD=H;
   H->SetSessionStarted(true);H->ClosePanels();
   if(auto* W=H->GetGameplayWidget())
   {HiddenGameplayHUD=W;SavedGameplayHUDVisibility=W->GetVisibility();bGameplayHUDVisibilityOwned=true;W->SetVisibility(ESlateVisibility::Collapsed);}
  }
  // Keep the host input mode; this director owns only ignore counts and cursor.
  HideMinimapForOpening();
  InitialHealth=Player->GetHealth();bOriginalDamage=Player->CanBeDamaged();Player->SetCanBeDamaged(false);bDamageOverrideOwned=true;
  // The hero's form system rewrites its mesh scale every tick; pose and cameras are applied after it.
  AddTickPrerequisiteActor(Player.Get());
  BoneFinalizedHandle=Player->GetMesh()->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateUObject(this,&ADWOpeningPrototype::ReapplyPoseScale));
  UE_LOG(LogTemp,Display,TEXT("SC0101 Mesh rel loc=%s rot=%s scale=%s"),*MeshLoc.ToCompactString(),*MeshRot.Rotator().ToCompactString(),*MeshScale.ToCompactString());
  GameCamera=Player->FindComponentByClass<UCameraComponent>();
  {const FVector Extent=Player->GetMesh()->Bounds.BoxExtent;Unit=FMath::Clamp(FMath::Max(Extent.X,Extent.Y)/90.f,.4f,4.f);}
  if(!Overlay)Overlay=CreateWidget<UDWOpeningOverlay>(PC.Get());
  if(!Overlay){CleanupPresentation();SetActorTickEnabled(false);return;}
  if(!Overlay->IsInViewport())Overlay->AddToViewport(150);Overlay->SetBlack(1);
  ShotCamera=GetWorld()->SpawnActor<ACameraActor>();
  if(!ShotCamera){CleanupPresentation();SetActorTickEnabled(false);return;}
  {
   int32 Width=0,Height=0;PC->GetViewportSize(Width,Height);const auto POV=PC->PlayerCameraManager->GetCameraCacheView();
   InitialOpeningAspect=POV.bConstrainAspectRatio?POV.AspectRatio:(Height>0?float(Width)/Height:POV.AspectRatio);
   ShotCamera->GetCameraComponent()->SetAspectRatio(InitialOpeningAspect);ShotCamera->GetCameraComponent()->SetConstraintAspectRatio(true);
  }
  {
   FAddCameraShakeParams Params(1.f);const TWeakObjectPtr<ADWOpeningPrototype> Self(this);
   Params.Initializer=FOnInitializeCameraShake::CreateLambda([Self](UCameraShakeBase* Shake)
   {if(auto* Pattern=Cast<UDWOpeningTraumaPattern>(Shake->GetRootShakePattern()))Pattern->Director=Self;});
   TraumaCameraManager=PC->PlayerCameraManager;
   if(TraumaCameraManager.IsValid())ActiveTraumaShake=TraumaCameraManager->StartCameraShake(UDWOpeningTraumaShake::StaticClass(),Params);
  }
  ChewAttenuation=NewObject<USoundAttenuation>(this);
  {
   auto& A=ChewAttenuation->Attenuation;A.bAttenuate=true;A.bSpatialize=true;A.AttenuationShape=EAttenuationShape::Sphere;
   A.AttenuationShapeExtents=FVector(250,0,0);A.FalloffDistance=2600;
  }
  TArray<AActor*> Found;
  UGameplayStatics::GetAllActorsWithTag(this,TEXT("OpeningCrumbs"),Found);for(auto* A:Found){Crumbs.Add(A);CrumbHome.Add(A->GetActorLocation());}
  Found.Reset();UGameplayStatics::GetAllActorsWithTag(this,TEXT("OpeningFeedingGround"),Found);for(auto* A:Found)FeedingGround.Add(A);
  Found.Reset();UGameplayStatics::GetAllActorsWithTag(this,TEXT("OpeningPitFloor"),Found);for(auto* A:Found)PitFloor.Add(A);
  Found.Reset();UGameplayStatics::GetAllActorsWithTag(this,TEXT("OpeningWater"),Found);
  if(Found.Num()){Water=Found[0];bWaterFound=true;WaterZ=Found[0]->GetComponentsBoundingBox(true).Max.Z;}
  RippleMesh=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
  RippleMaterial=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DoughWorld/OpeningPrototype/Materials/M_SC0101_Ripple.M_SC0101_Ripple"));
  // Feeding with their backs to the pit, so "it turns around" is an event.
  const float Yaws[3]={8,-26,32},Phases[3]={0,2.1f,4.f};
  constexpr int32 ExpectedOpeningEnemies=UE_ARRAY_COUNT(Yaws);
  // Same pale dough as the hero, a little greyer: "the same colour, the same texture".
  auto* SourBody=LoadObject<UMaterialInterface>(nullptr,TEXT("/Game/DoughWorld/OpeningPrototype/Materials/MI_SC0101_SourBody.MI_SC0101_SourBody"));
  for(int32 I=0;I<ExpectedOpeningEnemies;++I)
  {
   FVector P=EnemyCenter+FVector(I==0?0:210,I==0?0:(I==1?-250:250),0);
   auto* E=GetWorld()->SpawnActorDeferred<ADWEnemyCharacter>(OpeningEnemyClass ? OpeningEnemyClass.Get() : ADWEnemyCharacter::StaticClass(),FTransform(FRotator(0,Yaws[I],0),P));
   if(!E)continue;E->MaxHealth=60;E->AttackDamage=4;E->bShowCombatDebug=false;E->PatrolRadius=0;E->DetectionRadius=1;
   // Only Sour opts out of its CDO's formal profile for this tutorial instance, before BeginPlay.
   if(auto* Sour=Cast<ADWSourDoughEnemy>(E))
   {Sour->CombatProfile=EDWEnemyCombatProfile::Legacy;Sour->Health=60;Sour->SetDirectorPresentationControlled(true);}
   E->FinishSpawning(FTransform(FRotator(0,Yaws[I],0),P));if(!IsValid(E)||E->IsActorBeingDestroyed())continue;
   // Sour ticks crumbs while its flag suppresses AI; other enemy classes keep the old disabled tick.
   if(auto* Sour=Cast<ADWSourDoughEnemy>(E))Sour->SetDirectorPresentationControlled(true,this);
   E->SetActorTickEnabled(Cast<ADWSourDoughEnemy>(E)!=nullptr);E->GetCharacterMovement()->DisableMovement();E->SetCanBeDamaged(false);
   FHitResult Hit;FCollisionQueryParams Query(TEXT("OpeningFloor"),false,E);
   if(GetWorld()->LineTraceSingleByChannel(Hit,P+Up*400,P-Up*1200,ECC_Visibility,Query))P.Z=Hit.ImpactPoint.Z+E->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
   E->SetActorLocation(P);if(!IsValid(E)||E->IsActorBeingDestroyed())continue;
   E->OnDestroyed.AddUniqueDynamic(this,&ADWOpeningPrototype::OnOpeningEnemyDestroyed);
   // Preserve Sour's authored slots and the old placeholder material for all other enemy classes.
   if(!Cast<ADWSourDoughEnemy>(E)&&SourBody&&E->PlaceholderBody)E->PlaceholderBody->SetMaterial(0,SourBody);
   FEnemyPose Pose;Pose.Home=P;Pose.Yaw=Yaws[I];Pose.Phase=Phases[I];
   if(Cast<ADWSourDoughEnemy>(E)&&E->GetMesh())Pose.BodyBase=E->GetMesh()->GetRelativeTransform();
   else if(E->PlaceholderBody)Pose.BodyBase=E->PlaceholderBody->GetRelativeTransform();
   Enemies.Add(E);EnemyPoses.Add(Pose);
  }
  if(Enemies.Num()!=ExpectedOpeningEnemies||!IsValid(Enemies[0].Get())||Enemies[0]->IsActorBeingDestroyed())
  {
   CleanupPresentation();SetActorTickEnabled(false);
   if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)H->Notify(DWText(this,TEXT("开场敌人初始化不完整，请读取当前槽回到安全起点。"),TEXT("Opening enemies did not initialize. Reload this slot to return to the safe checkpoint.")));
   return;
  }
  EnemyCenter=Enemies[0]->GetActorLocation();
  const auto* FirstOpeningCapsule=Enemies[0]->GetCapsuleComponent();
  UE_LOG(LogTemp,Display,TEXT("SC0101 Size hero capsule r=%.0f hh=%.0f mesh=%s loc=%s | enemy capsule r=%.0f hh=%.0f body=%s loc=%s | gamecam fov=%.0f dist=%.0f"),
   Player->GetCapsuleComponent()->GetScaledCapsuleRadius(),Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight(),*Player->GetMesh()->Bounds.BoxExtent.ToString(),*Player->GetActorLocation().ToString(),
   IsValid(FirstOpeningCapsule)?FirstOpeningCapsule->GetScaledCapsuleRadius():0.f,IsValid(FirstOpeningCapsule)?FirstOpeningCapsule->GetScaledCapsuleHalfHeight():0.f,
   Cast<ADWSourDoughEnemy>(Enemies[0].Get())&&IsValid(Enemies[0]->GetMesh())?*Enemies[0]->GetMesh()->Bounds.BoxExtent.ToString():(IsValid(Enemies[0]->PlaceholderBody.Get())&&!Enemies[0]->PlaceholderBody->IsBeingDestroyed()?*Enemies[0]->PlaceholderBody->Bounds.BoxExtent.ToString():TEXT("-")),*EnemyCenter.ToString(),
   GameCamera.IsValid()?GameCamera->FieldOfView:0.f,GameCamera.IsValid()?FVector::Dist(GameCamera->GetComponentLocation(),Player->GetActorLocation()):0.f);
  CreateDouble();
  if(bPoseTest){Stage=EDWOpeningStage::Approach;Overlay->SetBlack(0);ShowDouble(true);PC->SetViewTarget(ShotCamera);bViewTargetOverridden=true;return;}
  // It wakes lying on its back in the water: the stand-in performs until it is on its feet.
  if(Double){bDoubleActive=true;ShowDouble(true);TickDouble(0.f);}
  UE_LOG(LogTemp,Display,TEXT("SC0101 Water found=%d z=%.1f floor=%.1f"),bWaterFound,WaterZ,DoubleBase.GetLocation().Z);
  }
  Enter(EDWOpeningStage::Wake);return;
 }
 if(!Player.IsValid()||!PC.IsValid())return;
 // Controller suspension is refreshed on its next input tick after releasing our lease.
 // Select only through the normal ownership/input gate once that boundary is ready.
 if(!bWeaponGiven&&(Stage==EDWOpeningStage::Escape||Stage==EDWOpeningStage::Approach||Stage==EDWOpeningStage::Combat))EquipStartingWeapon();
 if(bPoseTest)
 {
  // QA: lay out the stand-in's clips frame by frame from two angles.
  UAnimationAsset* Clips[3]={Player->DeathAnimation,Player->TransformAnimation,Player->WalkAnimation};
  const int32 Sample=FMath::FloorToInt(Age/.3f),Clip=Sample/18,Index=Sample%18,Frame=Index%9;
  if(Clip>=3){FinishTest(true,TEXT("pose sheet"));return;}
  if(Double&&Clips[Clip]&&(!Double->GetSingleNodeInstance()||Double->GetSingleNodeInstance()->GetAnimationAsset()!=Clips[Clip]))Double->PlayAnimation(Clips[Clip],false);
  if(auto* Instance=Double?Double->GetSingleNodeInstance():nullptr){Instance->SetPlaying(false);Instance->SetPosition(Instance->GetLength()*Frame/8.f,false);}
  const FVector C=HeroCenter();const FVector Eye=Index<9?C+FVector(0,-430,30):C+FVector(330,-280,60);
  ShotCamera->SetActorLocationAndRotation(Eye,(C-Eye).Rotation());ShotCamera->GetCameraComponent()->SetFieldOfView(40);
  if(Cue(Sample*.3f+.2f))Capture(FString::Printf(TEXT("Pose_%d_%s_%d"),Clip,Index<9?TEXT("side"):TEXT("34"),Frame));
  return;
 }
 if(bAutoTest&&Stage<EDWOpeningStage::Combat&&Player->GetHealth()!=InitialHealth){FinishTest(false,TEXT("damage before control handoff"));return;}
 TickEnemies(DirectorDt);
 const FVector Hero=Player->GetActorLocation();
 RefreshEnemyDefeatFacts();
 if(bUnexpectedEnemyLoss)
 {
  CleanupPresentation();SetActorTickEnabled(false);
  if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)H->Notify(DWText(this,TEXT("开场敌人异常缺失，未判定通关；请读取当前槽重试。"),TEXT("An opening enemy is missing. The encounter was not completed; reload this slot to retry.")));
  return;
 }
 const FVector First=Enemies.IsValidIndex(0)&&IsValid(Enemies[0].Get())&&!Enemies[0]->IsActorBeingDestroyed()?Enemies[0]->GetActorLocation():EnemyCenter;
 switch(Stage)
 {
 case EDWOpeningStage::Wake:
  Overlay->SetBlack(1-Ease((FMath::Clamp(Age/FMath::Max(.1f,WakeIntroShotSeconds),0.f,1.f)*14.4f-5.f)/2.8f,1.6f));
  if(OpeningSequence&&OpeningSequence->Current()==DWOpeningSteps::Step::WakeIntro&&Cue(WakeIntroShotSeconds*(4.4f/14.4f)))StartOpeningAmbience(true);
  break;
 case EDWOpeningStage::StretchPrompt:
 {
  Overlay->SetBlack(0);
  if(Age>.6f)Overlay->ShowMovementPrompt(PC.Get(),true);else Overlay->SetPrompt(TEXT(""));
  bool Press=false;for(auto A:{EDWInputAction::MoveForward,EDWInputAction::MoveBackward,EDWInputAction::MoveLeft,EDWInputAction::MoveRight})Press|=PC->WasInputKeyJustPressed(PC->GetAppliedActionKey(A));
  if(Press||(bAutoTest&&Age>1.4f))Enter(EDWOpeningStage::Stretch);
  break;
 }
 case EDWOpeningStage::Stretch:
  // Get up first (TickDouble): a heave and a splash, a slip back, the real push. Then the ground answers.
  if(Cue(.12f)){Play(EDWOpeningAudioEvent::SplashFirst,.8f);AddRipple(DoubleCenter,170*Unit,1.f);}
  if(Cue(.95f)){Play(EDWOpeningAudioEvent::SplashSlip,.55f,.85f);AddRipple(DoubleCenter,140*Unit,.7f);}
  if(Cue(1.3f)){Play(EDWOpeningAudioEvent::SplashPush,.9f,1.1f);Play(EDWOpeningAudioEvent::StretchPush,.7f,1.25f);AddRipple(DoubleCenter,210*Unit,1.f,1.9f);}
  if(Cue(2.45f))AddRipple(Feet(Player.Get()),130*Unit,.8f);
  if(Cue(3.2f)){AddTrauma(.55f);Play(EDWOpeningAudioEvent::GroundCollapse,.9f);}
  if(Between(3.2f,4.4f))
  {
   StretchCrumbsApplied+=Dt;
   for(int32 I=0;I<Crumbs.Num();++I)if(IsValid(Crumbs[I]))
    Crumbs[I]->AddActorWorldOffset((PitCenter-CrumbHome[I]).GetSafeNormal2D()*Dt*9+FVector(0,0,-Dt*15));
  }
  if(Between(3.2f,4.2f))
  {
   StretchFloorApplied+=Dt;
   for(auto A:PitFloor)if(IsValid(A))A->AddActorWorldOffset(FVector(0,0,-Dt*9));
  }
  if(Cue(3.5f)&&!QueueDialogueBeat(TEXT("SinkThought")))return;
  if(Cue(3.6f))RequestOpeningAspect(false);
  // The location card lands as the camera settles into gameplay height.
  if(Cue(4.6f))Overlay->ShowTitle(DWText(this,TEXT("第 一 日"),TEXT("DAY ONE")).ToString(),DWText(this,TEXT("两族与馊团的交界地带"),TEXT("BORDERLANDS OF THE TWO\nPEOPLES AND THE SOUR DOUGH")).ToString(),5.4f);
  if(ShotsDone())Enter(EDWOpeningStage::Escape);
  break;
 case EDWOpeningStage::Escape:
  // SinkThought continues from Stretch; entering Escape never restarts the line.
  if(Cue(2.6f)){Overlay->SetObjective(DWText(this,TEXT("离开下陷区域"),TEXT("Leave the sinking ground")).ToString());Play(EDWOpeningAudioEvent::EscapeObjective,.45f);}
  if(Player->GetVelocity().Size2D()>60)bMoved=true;
  if(!bMoved&&Age>1.4f)Overlay->ShowMovementPrompt(PC.Get(),false);else Overlay->SetPrompt(TEXT(""));
  if((TremorTimer-=Dt)<=0)
  {
   TremorTimer=FMath::FRandRange(4.5f,6.5f);AddTrauma(.24f);Play(EDWOpeningAudioEvent::GroundTremor,.35f,1.15f);
   for(auto A:PitFloor)if(IsValid(A))A->AddActorWorldOffset(FVector(0,0,-4));
  }
  if(bAutoTest)Player->AddMovementInput(FVector(1,0,0));
  if(bAutoTest&&Cue(2.2f))Capture(TEXT("04_Escape_title"));
  if(FVector::Dist2D(Hero,PitCenter)>EscapeRadius)Enter(EDWOpeningStage::Reveal);
  break;
 case EDWOpeningStage::Reveal:
 {
  FaceHero(First,Dt);
  // The identity beat lives here now, over the telephoto on them: no separate inspect step.
  // Only the bitten ground drops; the body stays on its feet so nothing sinks into the floor.
  const float SinkStep=OpeningSequence&&OpeningSequence->Current()==DWOpeningSteps::Step::RevealBite?FMath::Max(0.f,FMath::Min(Age,6.95f)-FMath::Max(PrevAge,6.6f)):0.f;
  if(SinkStep>0)
  {
   AActor* Nearest=nullptr;for(auto A:FeedingGround)if(IsValid(A)&&(!Nearest||FVector::Dist2D(A->GetActorLocation(),First)<FVector::Dist2D(Nearest->GetActorLocation(),First)))Nearest=A;
   if(Nearest){Nearest->AddActorWorldOffset(FVector(0,0,-SinkStep*40));RevealBiteApplied+=SinkStep;RevealBiteGround=Nearest;}
  }
  break;
 }
 case EDWOpeningStage::Approach:
  if(Cue(.3f)){Overlay->SetObjective(DWText(this,TEXT("靠近它们"),TEXT("Approach them")).ToString());Play(EDWOpeningAudioEvent::ApproachObjective,.45f);}
  if(bAutoTest){Player->AddMovementInput((First-Hero).GetSafeNormal2D());if(Cue(1.4f))Capture(TEXT("08_Approach"));}
  if(FVector::Dist2D(Hero,First)<EncounterDistance)Enter(EDWOpeningStage::Encounter);
  break;
 case EDWOpeningStage::Encounter:
 {
  FaceHero(First,Dt);
  if(!EnemyPoses.Num())break;
  if(!Enemies.IsValidIndex(0)||!IsValid(Enemies[0].Get())||Enemies[0]->IsActorBeingDestroyed()||!EnemyPoses.IsValidIndex(0))
  {
   CleanupPresentation();SetActorTickEnabled(false);
   if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)H->Notify(DWText(this,TEXT("开场敌人已失效，请读取当前槽回到安全起点。"),TEXT("An opening enemy is no longer available. Reload this slot to return to the safe checkpoint.")));
   return;
  }
  auto& P=EnemyPoses[0];auto* E=Enemies[0].Get();
  if(Cue(.5f))P.bFeeding=false;
  if(Age>=5.9f&&!P.bFree)P.Yaw=FMath::Lerp(P.TurnFrom,P.TurnTo,Ease((Age-5.9f)/1.1f,2.4f));
  if(Between(9.f,9.8f))
  {
   if(auto* Sour=Cast<ADWSourDoughEnemy>(E))Sour->SetDirectorAttackPose(.30f*Ease((Age-9.f)/.8f,2.f));
   else if(E->PlaceholderBody)
   {
    const float K=Ease((Age-9.f)/.8f,2.f);FTransform T=P.BodyBase;
    T.SetScale3D(P.BodyBase.GetScale3D()*FVector(1+.22f*K,1+.22f*K,1-.32f*K));T.AddToTranslation(FVector(0,0,-12*K));
    E->PlaceholderBody->SetRelativeTransform(T);
   }
  }
  if(Cue(9.8f)){P.bFree=true;LeapStart=E->GetActorLocation();Play(EDWOpeningAudioEvent::LeapWhoosh,1.f);}
  if(Age>=9.8f)
  {
   const float T=FMath::Clamp((Age-9.8f)/.42f,0.f,1.f);
   E->SetActorLocation(FMath::Lerp(LeapStart,LeapEnd,T)+Up*170*FMath::Sin(T*PI),false,nullptr,ETeleportType::TeleportPhysics);
   if(auto* Sour=Cast<ADWSourDoughEnemy>(E))
   {
    constexpr float ContactPose=33.f/56.f; // Authored contact sample: frame 34 in the 57-key attack.
    const float Pose=Age<10.18f ? FMath::Lerp(.30f,ContactPose,FMath::Clamp((Age-9.8f)/.38f,0.f,1.f))
     : FMath::Lerp(ContactPose,1.f,FMath::Clamp((Age-10.18f)/.32f,0.f,1.f));
    Sour->SetDirectorAttackPose(Pose);
   }
   else if(E->PlaceholderBody){FTransform B=P.BodyBase;B.SetScale3D(P.BodyBase.GetScale3D()*FVector(.85f,.85f,1.3f));E->PlaceholderBody->SetRelativeTransform(B);}
  }
  if(Cue(10.18f)){StartHitStop(.13f);Overlay->Flash(.85f);AddTrauma(.85f);Play(EDWOpeningAudioEvent::LeapImpact,1.f);Recoil=0;AdjustOpeningAmbience(1.2f,1.f);}
  break;
 }
 case EDWOpeningStage::Combat:
  if(Age<7.f)UpdateCombatPrompt();else Overlay->SetPrompt(TEXT(""));
  if(bAutoTest&&(Cue(.6f)||Cue(2.4f)))Capture(FString::Printf(TEXT("10_Combat_%s"),Age<1?TEXT("a"):TEXT("b")));
  // QA doesn't fight: once the captures are in it fells them through the normal damage path.
  if(bAutoTest&&Cue(3.f))for(auto& E:Enemies)if(IsValid(E)&&E->IsAlive())
  {const TWeakObjectPtr<ADWEnemyCharacter> SafeEnemy(E.Get());UGameplayStatics::ApplyDamage(E,9999.f,PC.Get(),Player.Get(),UDamageType::StaticClass());UE_LOG(LogTemp,Display,TEXT("SC0101 QA fell %s alive=%d"),*GetNameSafe(SafeEnemy.Get()),SafeEnemy.IsValid()?SafeEnemy->IsAlive():false);}
  if(AllEnemiesDown())
  {
   if(ClearedAt<0)
   {
    ClearedAt=Age;
    if(OpeningMusicHUD.IsValid()&&OpeningMusicLease.IsValid())OpeningMusicHUD->FadeOpeningMusicToSilence(OpeningMusicLease);
   }
   if(Age-ClearedAt>1.8f)Enter(EDWOpeningStage::Exit);
  }
  break;
 case EDWOpeningStage::Exit:
  if(Cue(.2f)){Overlay->SetObjective(DWText(this,TEXT("走出交界地带"),TEXT("Leave the borderlands")).ToString());Play(EDWOpeningAudioEvent::ExitObjective,.45f);}
  if(Cue(.6f)&&!QueueDialogueBeat(TEXT("ExitThought")))return;
  if(bAutoTest){Player->AddMovementInput((ExitPoint-Hero).GetSafeNormal2D());if(Cue(2.f))Capture(TEXT("11_Exit"));}
  RefreshExitTriggerStatus();
  {FString Reason;if(CanEnterExit(Reason))Enter(EDWOpeningStage::Title);}
  break;
 case EDWOpeningStage::Title:
 {
  const float Fade=FMath::Clamp(Age/FMath::Max(.05f,ExitFadeToBlackSeconds),0.f,1.f);
  Overlay->SetBlack(Fade); // Deliberately linear; the title is not visible until this reaches opaque.
  if(!bExitTitleStarted)
  {
   if(Fade<1.f||IsOpeningDialogueBusy())break;
   if(!SetLocked(true))
   {
    CleanupPresentation();SetActorTickEnabled(false);
    if(auto* H=PC.IsValid()?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr)H->Notify(DWText(this,TEXT("另一段演出仍占用控制，开场交接已安全停止；关闭界面后可读取当前槽重试。"),TEXT("Another scene still owns control. Opening handoff stopped safely; reload the current slot to retry.")));
    return;
   }
   StopOpeningDialogue();bExitTitleStarted=true;ExitTitleElapsed=0.f;
   TArray<UUserWidget*> Widgets;UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this,Widgets,UUserWidget::StaticClass(),true);
   for(auto* W:Widgets)if(W!=Overlay&&W->IsInViewport()&&W->IsVisible()){TitleHidden.Add(W,W->GetVisibility());W->SetVisibility(ESlateVisibility::Collapsed);}
   HideMinimapForOpening();
   Overlay->BurnTitle(GameTitle,DWText(this,TEXT("第一章  ·  苏醒"),TEXT("CHAPTER ONE  ·  AWAKENING")).ToString(),FMath::Max(.1f,ExitTitleSeconds),ChapterTitleGap);
   Play(EDWOpeningAudioEvent::TitleBurn,.9f);
   UE_LOG(LogTemp,Display,TEXT("DW_OPENING_TITLE_FULL_BLACK location=%s"),*Hero.ToCompactString());
  }
  else ExitTitleElapsed+=Dt;
  if(ExitTitleElapsed>=FMath::Max(.1f,ExitTitleSeconds))
  {
   // Release gameplay/presentation leases for the authoritative capture, retaining an opaque cover.
   CutToGameplay();CleanupPresentation(true);
   if(InputGate)InputGate->ReleaseExternalResources(); // The save capture requires no active world-event lock.
   auto* OpeningGI=GetGameInstance<UDWGameInstance>();
   if(IsActorBeingDestroyed()||!Player.IsValid()||!PC.IsValid()||!OpeningGI||!OpeningGI->IsOpeningDirectorCurrent(this,Player.Get()))
   {CleanupPresentation();SetActorTickEnabled(false);return;}
   Player->CancelCombatActions();
   bAwaitingHandoff=true;FlowRetryElapsed=FMath::Max(.1f,HandoffRetrySeconds);
   TickFormalFlow(0.f);return;
  }
  break;
 }
 default:break;
 }
 // Distant chewing from the moment the hero can stand: attenuated, so it grows as they approach.
 if(Stage>=EDWOpeningStage::Escape&&Stage<EDWOpeningStage::Combat&&(ChewTimer-=Dt)<=0)
 {
  ChewTimer=FMath::FRandRange(.35f,.8f);
  TArray<int32> Feeding;for(int32 I=0;I<EnemyPoses.Num();++I)if(EnemyPoses[I].bFeeding&&IsValid(Enemies[I]))Feeding.Add(I);
  if(Feeding.Num()){const int32 I=Feeding[FMath::RandHelper(Feeding.Num())];PlayAt(EDWOpeningAudioEvent::FeedingChew,Enemies[I]->GetActorLocation(),.9f,FMath::FRandRange(.85f,1.2f));}
 }
 if(bWaterFound&&(RippleTimer-=Dt)<=0)
 {
  if(bDoubleActive&&StretchStart<0){RippleTimer=2.4f;AddRipple(DoubleCenter,150*Unit,.35f,2.6f);}
  // Standing in it: a wake while wading, a slow ring around the legs while still.
  else if(!bDoubleActive&&FVector::Dist2D(Hero,PitCenter)<430)
  {
   const bool bWading=Player->GetVelocity().Size2D()>60;
   RippleTimer=bWading?.38f:1.7f;AddRipple(Feet(Player.Get()),(bWading?80.f:100.f)*Unit,bWading?.55f:.3f,bWading?1.1f:2.f);
  }
 }
 TickRipples(DirectorDt);
 TickShots(DirectorDt);
 if(OpeningSequence&&PreparedSequenceStep==OpeningSequence->Current()&&ShotsDone())
 {
  const auto K=OpeningSequence->CurrentKind();
  if(K==DWOpeningSteps::Kind::Camera||K==DWOpeningSteps::Kind::Action)OpeningSequence->Complete(OpeningSequence->CurrentToken(),K,DWOpeningSteps::Completion::Success);
 }
 UpdateOpeningAspect(Dt);
 TickHero(DirectorDt);
 Overlay->Advance(RealDt);
 // QA: sample every take on a fixed cadence (denser while the hero gets up).
 if(bAutoTest&&!ShotsDone())
 {
  const float Period=Stage==EDWOpeningStage::Stretch?.5f:Stage==EDWOpeningStage::Encounter?1.25f:Stage==EDWOpeningStage::Title?2.f:2.5f;
  if(FMath::FloorToInt(PrevAge/Period)!=FMath::FloorToInt(Age/Period))
   Capture(FString::Printf(TEXT("%02d_%s_%04.1f"),int32(Stage),*StaticEnum<EDWOpeningStage>()->GetNameStringByValue(int64(Stage)),Age));
 }
}
void ADWOpeningPrototype::Capture(const FString& Name)
{
 if(PC.IsValid()&&PC->PlayerCameraManager&&Player.IsValid())
  UE_LOG(LogTemp,Display,TEXT("SC0101 Shot %s cam=%s rot=%s fov=%.1f hero=%s dist=%.0f view=%s"),*Name,*PC->PlayerCameraManager->GetCameraLocation().ToCompactString(),*PC->PlayerCameraManager->GetCameraRotation().ToCompactString(),PC->PlayerCameraManager->GetFOVAngle(),*HeroCenter().ToCompactString(),FVector::Dist(PC->PlayerCameraManager->GetCameraLocation(),HeroCenter()),*GetNameSafe(PC->GetViewTarget()));
 FScreenshotRequest::RequestScreenshot(DiagnosticsRoot/FString::Printf(TEXT("%s%s.png"),bSkipTest?TEXT("Skip_"):TEXT(""),*Name),true,false);
}
void ADWOpeningPrototype::FinishTest(bool Passed,const FString& Detail)
{
 const FString Path=DiagnosticsRoot/(bSkipTest?TEXT("skip-test.txt"):TEXT("playthrough-test.txt"));
 FFileHelper::SaveStringToFile(FString::Printf(TEXT("%s\n%s\nstages=%d enemies=%d\n"),Passed?TEXT("PASS"):TEXT("FAIL"),*Detail,Visited.Num(),Enemies.Num()),*Path);
 UE_LOG(LogTemp,Display,TEXT("SC0101_QA %s: %s"),Passed?TEXT("PASS"):TEXT("FAIL"),*Detail);
 bAutoTest=false;UKismetSystemLibrary::QuitGame(this,PC.Get(),EQuitPreference::Quit,false);
}
void ADWOpeningPrototype::EndPlay(const EEndPlayReason::Type Reason)
{
 for(auto& Enemy:Enemies)if(IsValid(Enemy.Get()))Enemy->OnDestroyed.RemoveDynamic(this,&ADWOpeningPrototype::OnOpeningEnemyDestroyed);
 CleanupPresentation();
 Super::EndPlay(Reason);
}

// Spatial admission only; all title/save/travel ownership remains in the existing director flow.
bool ADWOpeningPrototype::CanEnterExit(FString& Reason) const
{
 if(!bEnableExitTrigger){Reason=TEXT("Disabled by director / 导演已关闭出口");return false;}
 if(!IsValid(ExitVolume)){Reason=TEXT("Missing explicit Exit Volume / 未指定出口体积（不回退旧半径）");return false;}
 if(ExitVolume->GetWorld()!=GetWorld()){Reason=TEXT("Exit Volume belongs to another world / 体积不属于本关");return false;}
 if(!ExitVolume->IsAvailable(Reason))return false;
 if(!GetWorld()||!GetWorld()->IsGameWorld()){Reason=TEXT("Editor configuration valid; gameplay not tested / 仅编辑器配置有效，未作运行判定");return false;}
 if(Stage==EDWOpeningStage::Title){Reason=TEXT("Already entering title / 已触发标题，不重复进入");return false;}
 if(Stage!=EDWOpeningStage::Exit){Reason=TEXT("Waiting for Exit stage / 等待战斗及剧情进入 Exit 阶段");return false;}
 if(!StartedDialogueBeats.Contains(TEXT("ExitThought"))){Reason=TEXT("Waiting for exit dialogue to start / 等待离开独白启动");return false;}
 if(!ExitVolume->ContainsExitPlayer(Player.Get(),PC.Get(),Reason))return false;
 Reason=TEXT("Ready / 玩家胶囊与出口盒相交，允许进入原淡黑标题流程");return true;
}

void ADWOpeningPrototype::RefreshExitTriggerStatus()
{
 FString Reason;CanEnterExit(Reason);
 if(Reason==ExitTriggerStatus)return;
 ExitTriggerStatus=Reason;
 if(GetWorld()&&GetWorld()->IsGameWorld())UE_LOG(LogTemp,Display,TEXT("DW_OPENING_EXIT_STATUS %s volume=%s"),*Reason,*GetNameSafe(ExitVolume));
}
