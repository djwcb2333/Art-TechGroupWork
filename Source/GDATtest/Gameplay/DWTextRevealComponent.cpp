#include "DWTextRevealComponent.h"
#include "DWUserSettings.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Components/AudioComponent.h"
#include "Extensions/UIComponentUserWidgetExtension.h"
#include "Framework/Text/SlateTextLayout.h"
#include "Framework/Text/SlateTextRun.h"
#include "Internationalization/BreakIterator.h"
#include "Internationalization/IBreakIterator.h"
#include "Kismet/GameplayStatics.h"
#include "HAL/PlatformTime.h"
#include "UObject/UnrealType.h"
#include "Widgets/SCompoundWidget.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

namespace DWTextRevealInternal
{
 bool IsChinese(uint32 C) { return (C>=0x3400&&C<=0x9fff)||(C>=0xf900&&C<=0xfaff)||(C>=0x20000&&C<=0x323af); }
 uint32 Codepoint(const FString& S)
 {
  if(S.IsEmpty())return 0;
  uint32 C=S[0];
  if(C>=0xd800&&C<=0xdbff&&S.Len()>1) C=0x10000+((C-0xd800)<<10)+(uint32(S[1])-0xdc00);
  return C;
 }
 bool Sentence(const FString& S) { return S.Len()==1&&FString(TEXT(".!?。！？…")).Contains(S); }
 bool Comma(const FString& S) { return S.Len()==1&&FString(TEXT(",;:，；：、")).Contains(S); }
 bool White(const FString& S) { return S.TrimStartAndEnd().IsEmpty(); }
 bool Speakable(const FString& S,bool Numbers)
 {
  const uint32 C=Codepoint(S);
  return IsChinese(C)||(C>='a'&&C<='z')||(C>='A'&&C<='Z')||(Numbers&&C>='0'&&C<='9');
 }
 // Analytic spring displacement from 1 at rest. Stable even after a long frame.
 double Spring(double Time,double Hz,double Zeta)
 {
  const double W=2*PI*FMath::Clamp(Hz,.1,30.0),Z=FMath::Clamp(Zeta,.1,3.0),T=FMath::Max(0.,Time);
  if(Z<.9999){const double D=FMath::Sqrt(1-Z*Z);return FMath::Exp(-Z*W*T)*(FMath::Cos(W*D*T)+Z/D*FMath::Sin(W*D*T));}
  if(Z<=1.0001)return FMath::Exp(-W*T)*(1+W*T);
  const double D=FMath::Sqrt(Z*Z-1),A=-W*(Z-D),B=-W*(Z+D);
  return (-B*FMath::Exp(A*T)+A*FMath::Exp(B*T))/(A-B);
 }
 template<typename T>T Property(const UObject* O,const TCHAR* Name,T Default)
 {
  const FProperty* P=O?O->GetClass()->FindPropertyByName(Name):nullptr;
  return P?*P->ContainerPtrToValuePtr<T>(O):Default;
 }
}

UDWTextVoiceProfile::UDWTextVoiceProfile()
{
 for(TCHAR C='A';C<='Z';++C)EnglishCharacterSounds.Add(FName(*FString::Chr(C)),nullptr);
 for(TCHAR C='0';C<='9';++C)EnglishCharacterSounds.Add(FName(*FString::Chr(C)),nullptr);
 ChineseBlipSounds.SetNum(8);
}
USoundBase* UDWTextVoiceProfile::ResolveCharacterSound(const FString& Character,EDWTextVoiceLanguage Language) const
{
 if(!DWTextRevealInternal::Speakable(Character,bSpeakNumbers))return nullptr;
 const uint32 C=DWTextRevealInternal::Codepoint(Character);
 const bool Chinese=Language==EDWTextVoiceLanguage::Chinese||(Language==EDWTextVoiceLanguage::Auto&&DWTextRevealInternal::IsChinese(C));
 const uint32 Hash=GetTypeHash(Character)^uint32(VoiceSeed)*196613u;
 if(Chinese)
 {
  int32 Count=0;for(const auto& Sound:ChineseBlipSounds)if(Sound)++Count;
  if(Count>0){int32 Pick=int32(Hash%uint32(Count));for(const auto& Sound:ChineseBlipSounds)if(Sound&&Pick--==0)return Sound.Get();}
 }
 else if((C>='A'&&C<='Z')||(C>='a'&&C<='z')||(C>='0'&&C<='9'))
 {
  const FName Key(*FString::Chr(FChar::ToUpper(TCHAR(C))));
  if(const auto* Sound=EnglishCharacterSounds.Find(Key);Sound&&*Sound)return Sound->Get();
 }
 // Retain serialized V1 profiles; new per-character/pool entries take precedence.
 const TCHAR Lower=FChar::ToLower(TCHAR(C));
 const int32 Slot=Chinese?int32(Hash%3):(FString(TEXT("aeiou")).Contains(FString::Chr(Lower))?0:(FString(TEXT("bdfgkpt")).Contains(FString::Chr(Lower))?1:2));
 USoundBase* Legacy[3]={Chinese?ChineseA.Get():EnglishA.Get(),Chinese?ChineseB.Get():EnglishB.Get(),Chinese?ChineseC.Get():EnglishC.Get()};
 return Legacy[Slot]?Legacy[Slot]:FallbackSound.Get();
}

struct FDWRevealGlyph
{
 FString Text;
 int32 Paragraph=0,Begin=0,End=0,Line=0;
 double At=0;
};
struct FDWTextRevealState
{
 FString Source;
 TArray<FDWRevealGlyph> Glyphs;
 double Time=0,Duration=0;
 bool bShowAll=true,bClear=false,bLayoutReady=false;
 int32 LineCount=0;
};

/** One lightweight text run per grapheme, NOT one UObject/UMG widget per character. */
class FDWRevealRun : public FSlateTextRun
{
public:
 FDWRevealRun(const TSharedRef<const FString>& Text,const FTextBlockStyle& Style,const FTextRange& Range,
  UDWTextRevealComponent* InEffect,int32 InIndex):FSlateTextRun(FRunInfo(),Text,Style,Range),Index(InIndex),Effect(InEffect){}
 virtual FVector2d GetOutlineSize(int32 Start,int32 End,float Scale) const override
 {
  const double O=Style.Font.OutlineSettings.OutlineSize*Scale;
  return FVector2d((Start==0?O:0)+(End==Text->Len()?O:0),O);
 }
 virtual FVector2d GetShadowSize(int32 Start,int32 End,float Scale) const override
 {
  return FVector2d(((Style.ShadowOffset.X>0&&End==Text->Len())||(Style.ShadowOffset.X<0&&Start==0))?FMath::Abs(Style.ShadowOffset.X*Scale):0,FMath::Abs(Style.ShadowOffset.Y*Scale));
 }
 virtual int32 OnPaint(const FPaintArgs& Args,const FTextArgs& TextArgs,const FGeometry& Geometry,const FSlateRect& Cull,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle& WidgetStyle,bool Enabled) const override;
 int32 Index;
private:
 TWeakObjectPtr<UDWTextRevealComponent> Effect;
};

class SDWTextReveal : public SCompoundWidget
{
public:
 SLATE_BEGIN_ARGS(SDWTextReveal){} SLATE_END_ARGS()
 void Construct(const FArguments&,UDWTextRevealComponent* InEffect,TSharedRef<SWidget> Content)
 {
  Effect=InEffect;Original=Content;ChildSlot[Content];SetCanTick(true);
  SetVisibility(TAttribute<EVisibility>::CreateLambda([Weak=TWeakPtr<SWidget>(Content)](){auto C=Weak.Pin();return C?C->GetVisibility():EVisibility::Collapsed;}));
  Layout=FSlateTextLayout::Create(this,FTextBlockStyle());
 }
 void Refresh(bool bForce=false)
 {
  UDWTextRevealComponent* E=Effect.Get();UTextBlock* T=E?Cast<UTextBlock>(E->GetOwner().Get()):nullptr;
  if(!T||E->bDesignTime)return;
  E->SyncSource();
  const auto& S=E->State;
  FTextBlockStyle NewStyle;NewStyle.SetFont(T->GetFont()).SetColorAndOpacity(T->GetColorAndOpacity()).SetShadowOffset(T->GetShadowOffset()).SetShadowColorAndOpacity(T->GetShadowColorAndOpacity());
  const float Width=T->GetAutoWrapText()&&LastWidth>1?LastWidth:T->GetWrapTextAt();
  const FMargin Margin=DWTextRevealInternal::Property(T,TEXT("Margin"),FMargin());
  const float Height=DWTextRevealInternal::Property(T,TEXT("LineHeightPercentage"),1.f);
  const auto Justify=DWTextRevealInternal::Property(T,TEXT("Justification"),TEnumAsByte<ETextJustify::Type>(ETextJustify::Left));
  const auto Policy=DWTextRevealInternal::Property(T,TEXT("WrappingPolicy"),ETextWrappingPolicy::DefaultWrapping);
  if(bForce||S->Source!=CachedSource||!CachedStyle.IsIdenticalTo(NewStyle)||!FMath::IsNearlyEqual(Width,CachedWidth)||!FMath::IsNearlyEqual(LastScale,CachedScale)||Margin!=CachedMargin||Height!=CachedHeight||Justify!=CachedJustify||Policy!=CachedPolicy)
  {
   CachedSource=S->Source;CachedStyle=NewStyle;CachedWidth=Width;CachedScale=LastScale;CachedMargin=Margin;CachedHeight=Height;CachedJustify=Justify;CachedPolicy=Policy;
   Layout->ClearLines();Layout->SetDefaultTextStyle(NewStyle);Layout->SetScale(LastScale);Layout->SetWrappingWidth(FMath::Max(0.f,Width));Layout->SetWrappingPolicy(Policy);Layout->SetMargin(Margin);Layout->SetLineHeightPercentage(Height);Layout->SetJustification(Justify);
   TArray<FTextRange> Lines;FTextRange::CalculateLineRangesFromString(S->Source,Lines);
   for(int32 P=0;P<Lines.Num();++P)
   {
    const TSharedRef<FString> Line=MakeShared<FString>(S->Source.Mid(Lines[P].BeginIndex,Lines[P].Len()));
    TArray<TSharedRef<IRun>> Runs;
    for(int32 I=0;I<S->Glyphs.Num();++I)if(S->Glyphs[I].Paragraph==P)
     Runs.Add(MakeShared<FDWRevealRun>(Line,NewStyle,FTextRange(S->Glyphs[I].Begin,S->Glyphs[I].End),E,I));
    if(Runs.IsEmpty())Runs.Add(FSlateTextRun::Create(FRunInfo(),Line,NewStyle));
    Layout->AddLine(FTextLayout::FNewLineData(Line,Runs));
   }
   Layout->UpdateIfNeeded();
   const auto& Views=Layout->GetLineViews();S->LineCount=Views.Num();
   for(int32 L=0;L<Views.Num();++L)for(const auto& B:Views[L].Blocks)
   {
    // Empty paragraphs use a normal empty run and have no corresponding glyph.
    if(B->GetTextRange().Len()>0)
    {
     const auto Run=StaticCastSharedRef<FDWRevealRun>(B->GetRun());
     if(S->Glyphs.IsValidIndex(Run->Index))S->Glyphs[Run->Index].Line=L;
    }
   }
   S->bLayoutReady=true;E->RebuildSchedule();Invalidate(EInvalidateWidgetReason::Layout|EInvalidateWidgetReason::Paint);
  }
 }
 virtual void Tick(const FGeometry& G,double,float) override
 {
  LastWidth=G.GetLocalSize().X;LastScale=G.GetAccumulatedLayoutTransform().GetScale();Refresh();
 }
 virtual FVector2D ComputeDesiredSize(float Scale) const override
 {
  auto* E=Effect.Get();
  if(E&&!E->bDesignTime){auto* Self=const_cast<SDWTextReveal*>(this);Self->LastScale=Scale;Self->Refresh();}
  if(E&&!E->bDesignTime&&E->State&&E->State->bLayoutReady)
  {
   // FTextLayout::GetSize already returns unscaled Slate units.
   FVector2D Size=Layout->GetSize();
   if(auto* T=Cast<UTextBlock>(E->GetOwner().Get()))Size.X=FMath::Max(Size.X,double(T->GetMinDesiredWidth()));
   return Size;
  }
  return SCompoundWidget::ComputeDesiredSize(Scale);
 }
 virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& G,const FSlateRect& Cull,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle& Style,bool Enabled) const override
 {
  UDWTextRevealComponent* E=Effect.Get();
  if(!E||E->bDesignTime)return SCompoundWidget::OnPaint(Args,G,Cull,Draw,Layer,Style,Enabled);
  if(!E->State||!E->State->bLayoutReady)return Layer;
  const auto C=Original.Pin();
  if(!C)return Layer;
  FWidgetStyle ChildStyle=Style;ChildStyle.BlendColorAndOpacityTint(FLinearColor(1,1,1,C->GetRenderOpacity()));
  // Transparent parents can still paint their children. Count only a visibly painted frame.
  E->LastEffectivePaintAlpha=ChildStyle.GetColorAndOpacityTint().A*CachedStyle.ColorAndOpacity.GetColor(ChildStyle).A;
  if(E->LastEffectivePaintAlpha>.01f){E->LastPaint=FPlatformTime::Seconds();E->bHasVisiblePaint=true;}
  const FGeometry Child=G.MakeChild(G.GetLocalSize(),FSlateLayoutTransform(),C->GetRenderTransform().Get(FSlateRenderTransform()),C->GetRenderTransformPivot());
  // Match SlateTextBlockLayout: justification needs the actual visible region,
  // not just the measured glyph width (a wide centered logo otherwise paints left).
  FVector2D AutoScroll=FVector2D::ZeroVector;
  const float Overflow=Layout->GetSize().X-G.GetLocalSize().X;
  const auto VisualJustification=Layout->GetVisualJustification();
  if(Overflow>0)
  {
   if(VisualJustification==ETextJustify::Center)AutoScroll.X=Overflow*.5f;
   else if(VisualJustification==ETextJustify::Right)AutoScroll.X=Overflow;
  }
  Layout->SetVisibleRegion(G.GetLocalSize(),AutoScroll*Layout->GetScale());
  Layout->UpdateIfNeeded();
  return Layout->OnPaint(Args,Child,Cull,Draw,Layer,ChildStyle,Enabled&&C->IsEnabled());
 }
void AppendLayoutDebug(const TSharedRef<FJsonObject>& J) const
 {
  J->SetNumberField(TEXT("layout_width"),Layout->GetSize().X);
  J->SetNumberField(TEXT("desired_width"),GetDesiredSize().X);
  J->SetNumberField(TEXT("layout_scale"),CachedScale);
  J->SetNumberField(TEXT("allocated_width"),GetCachedGeometry().GetLocalSize().X);
 }
private:
 TWeakObjectPtr<UDWTextRevealComponent> Effect;
 TWeakPtr<SWidget> Original;
 TSharedPtr<FSlateTextLayout> Layout;
 FString CachedSource=TEXT("\x01");
 FTextBlockStyle CachedStyle;
 float LastWidth=0,LastScale=1,CachedWidth=-1,CachedScale=1,CachedHeight=1;
 FMargin CachedMargin;
 TEnumAsByte<ETextJustify::Type> CachedJustify=ETextJustify::Left;
 ETextWrappingPolicy CachedPolicy=ETextWrappingPolicy::DefaultWrapping;
};

int32 FDWRevealRun::OnPaint(const FPaintArgs& Args,const FTextArgs& TextArgs,const FGeometry& G,const FSlateRect& Cull,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle& StyleIn,bool Enabled) const
{
 const UDWTextRevealComponent* E=Effect.Get();
 if(!E)return Layer;
 // Public render-query is represented by the debug-free private shared state via friend-free helper below.
 float Scale=1,Alpha=1;FVector2D Offset=FVector2D::ZeroVector;
 extern bool DWGetGlyphPresentation(const UDWTextRevealComponent*,int32,float&,float&,FVector2D&);
 if(!DWGetGlyphPresentation(E,Index,Scale,Alpha,Offset))return Layer;
 const FVector2D Center=(TextArgs.Block->GetLocationOffset()+TextArgs.Block->GetSize()*.5)/G.GetAccumulatedLayoutTransform().GetScale();
 const FGeometry Animated=G.MakeChild(G.GetLocalSize(),FSlateLayoutTransform(),FSlateRenderTransform(Scale,FVector2f(Center*(1-Scale)+Offset)),FVector2D::ZeroVector);
 FWidgetStyle PaintedStyle=StyleIn;PaintedStyle.BlendColorAndOpacityTint(FLinearColor(1,1,1,Alpha));
 return FSlateTextRun::OnPaint(Args,TextArgs,Animated,Cull,Draw,Layer,PaintedStyle,Enabled);
}

// Kept on the component so UI animation wrappers never write the owner's text or render transform.
bool DWGetGlyphPresentation(const UDWTextRevealComponent* E,int32 Index,float& Scale,float& Alpha,FVector2D& Offset)
{
 const auto& S=E->State;
 if(!S||S->bClear||!S->Glyphs.IsValidIndex(Index))return false;
 if(S->bShowAll)return true;
 const double Age=S->Time-S->Glyphs[Index].At;
 if(Age<0)return false;
 if(Age>=FMath::Max(.1f,E->SpringSettleSeconds))return true;
 const double Spring=DWTextRevealInternal::Spring(Age,E->FrequencyHz,E->DampingRatio);
 Scale=FMath::Clamp(float(1+(FMath::Clamp(E->StartScale,.01f,1.f)-1)*Spring),.01f,2.f);
 Offset=E->EntryOffset*Spring;
 Alpha=E->FadeInSeconds>0?FMath::Clamp(float(Age/E->FadeInSeconds),0.f,1.f):1;
 return true;
}

bool UDWTextRevealComponent::IsSupportedOwner() const { return Cast<UTextBlock>(GetOwner().Get())!=nullptr; }
TSharedRef<SWidget> UDWTextRevealComponent::RebuildWidgetWithContent(TSharedRef<SWidget> Content)
{
 if(!IsSupportedOwner())return Content;
 if(!State)State=MakeShared<FDWTextRevealState>();
 TSharedRef<SDWTextReveal> W=SNew(SDWTextReveal,this,Content);View=W;return W;
}
void UDWTextRevealComponent::OnPreConstruct(bool Design){Super::OnPreConstruct(Design);bDesignTime=Design;}
void UDWTextRevealComponent::OnConstruct()
{
 Super::OnConstruct();bConstructed=IsSupportedOwner()&&!GetOwner()->IsDesignTime();bDesignTime=!bConstructed;
 if(!bConstructed)return;
 if(!State)State=MakeShared<FDWTextRevealState>();
 if(bPlayOnConstruct)Replay();else {const bool Old=bReplayOnTextChanged;bReplayOnTextChanged=false;SyncSource(true);bReplayOnTextChanged=Old;Stop(true);}
}
void UDWTextRevealComponent::OnDestruct(){bConstructed=false;bPlaying=false;++Revision;EndTicker();StopAudio(true);Super::OnDestruct();}
void UDWTextRevealComponent::BeginDestroy(){bConstructed=false;EndTicker();StopAudio(true);View.Reset();State.Reset();Super::BeginDestroy();}
#if WITH_EDITOR
EDataValidationResult UDWTextRevealComponent::IsDataValidForOwner(const UWidget* W,FDataValidationContext& C) const
{
 if(!Cast<UTextBlock>(W)){C.AddError(FText::FromString(TEXT("DW Text Reveal requires an ordinary TextBlock. Add it to the label, not its Button/Border or RichTextBlock.")));return EDataValidationResult::Invalid;}
 return EDataValidationResult::Valid;
}
#endif
void UDWTextRevealComponent::SyncSource(bool Force)
{
 const UTextBlock* T=Cast<UTextBlock>(GetOwner().Get());if(!T)return;
 if(!State)State=MakeShared<FDWTextRevealState>();
 const FString New=T->GetText().ToString();if(!Force&&State->Source==New)return;
 const bool Changed=State->Source!=New;
 State->Source=New;State->Glyphs.Reset();State->bLayoutReady=false;
 TArray<FTextRange> Lines;FTextRange::CalculateLineRangesFromString(New,Lines);
 for(int32 P=0;P<Lines.Num();++P)
 {
  const FString Line=New.Mid(Lines[P].BeginIndex,Lines[P].Len());
  auto Iterator=FBreakIterator::CreateCharacterBoundaryIterator();Iterator->SetString(Line);
  int32 Start=Iterator->ResetToBeginning();
  for(int32 End=Iterator->MoveToNext();End!=INDEX_NONE;Start=End,End=Iterator->MoveToNext())
  {FDWRevealGlyph G;G.Text=Line.Mid(Start,End-Start);G.Paragraph=P;G.Begin=Start;G.End=End;G.Line=P;State->Glyphs.Add(MoveTemp(G));}
 }
 if(Changed&&bReplayOnTextChanged&&bConstructed)
 {
  ++Revision;StopAudio();State->Time=0;State->bShowAll=false;State->bClear=false;bPlaying=true;bPaused=false;NextCharacter=0;EnglishCounter=ChineseCounter=BlipsPlayed=0;LastBlip=-100;EnsureTicker();OnStarted.Broadcast();
 }
 RebuildSchedule();
}
void UDWTextRevealComponent::RebuildSchedule()
{
 if(!State)return;
 double At=FMath::Max(0.f,InitialDelay);int32 PreviousLine=0;
 for(auto& G:State->Glyphs)
 {
  At+=FMath::Max(0,G.Line-PreviousLine)*FMath::Max(0.f,LinePause);PreviousLine=G.Line;G.At=At;
  At+=1./FMath::Max(1.f,DWTextRevealInternal::IsChinese(DWTextRevealInternal::Codepoint(G.Text))?ChineseCharactersPerSecond:EnglishCharactersPerSecond)*(DWTextRevealInternal::White(G.Text)?.4:1.);
  if(DWTextRevealInternal::Sentence(G.Text))At+=FMath::Max(0.f,SentencePause);else if(DWTextRevealInternal::Comma(G.Text))At+=FMath::Max(0.f,CommaPause);
 }
 State->Duration=State->Glyphs.IsEmpty()?0:FMath::Max(At,State->Glyphs.Last().At+FMath::Max(.1f,SpringSettleSeconds));
}
void UDWTextRevealComponent::PlayText(FText NewText){if(auto* T=Cast<UTextBlock>(GetOwner().Get())){T->SetText(NewText);Replay();}}
void UDWTextRevealComponent::Replay()
{
 if(!bConstructed)return;
 // Avoid two started events when this explicit command also changes the source.
 const bool Old=bReplayOnTextChanged;bReplayOnTextChanged=false;SyncSource(true);bReplayOnTextChanged=Old;
 ++Revision;StopAudio();State->Time=0;State->bShowAll=false;State->bClear=false;bPlaying=true;bPaused=false;
 NextCharacter=0;EnglishCounter=ChineseCounter=BlipsPlayed=0;LastBlip=-100;LastPaint=0;bHasVisiblePaint=false;LastEffectivePaintAlpha=0.f;
 if(auto V=View.Pin())V->Refresh(true);
 EnsureTicker();OnStarted.Broadcast();
}
void UDWTextRevealComponent::Pause(){bPaused=true;StopAudio();}
void UDWTextRevealComponent::Resume(){if(bPlaying){bPaused=false;LastTick=FPlatformTime::Seconds();EnsureTicker();}}
void UDWTextRevealComponent::SkipToEnd()
{
 if(!State||!bPlaying)return;
 ++Revision;bPlaying=false;bPaused=false;State->Time=State->Duration;State->bShowAll=true;State->bClear=false;NextCharacter=State->Glyphs.Num();StopAudio();
 if(auto V=View.Pin())V->Invalidate(EInvalidateWidgetReason::Paint);OnFinished.Broadcast(true);
}
void UDWTextRevealComponent::Stop(bool ShowAll)
{
 const bool WasPlaying=bPlaying;++Revision;bPlaying=false;bPaused=false;StopAudio();
 if(State){State->bShowAll=ShowAll;State->bClear=!ShowAll;}
 if(auto V=View.Pin())V->Invalidate(EInvalidateWidgetReason::Paint);if(WasPlaying)OnStopped.Broadcast();
}
void UDWTextRevealComponent::SetPlaybackTime(float Seconds)
{
 if(!bConstructed)return;
 SyncSource();if(auto V=View.Pin())V->Refresh();if(!State)return;
 ++Revision;StopAudio();State->Time=FMath::Max(0.f,Seconds);State->bShowAll=false;State->bClear=false;
 NextCharacter=0;for(const auto& G:State->Glyphs)if(G.At<=State->Time)++NextCharacter;
 if(auto V=View.Pin())V->Invalidate(EInvalidateWidgetReason::Paint);
}
float UDWTextRevealComponent::GetPlaybackTime() const{return State?State->Time:0;}
float UDWTextRevealComponent::GetDuration() const{return State?State->Duration:0;}
int32 UDWTextRevealComponent::GetCharacterCount() const{return State?State->Glyphs.Num():0;}
int32 UDWTextRevealComponent::GetVisualLineCount() const{return State?State->LineCount:0;}
int32 UDWTextRevealComponent::GetVisibleCharacterCount() const
{
 if(!State||State->bClear)return 0;if(State->bShowAll)return State->Glyphs.Num();int32 N=0;for(const auto& G:State->Glyphs)if(G.At<=State->Time)++N;return N;
}
bool UDWTextRevealComponent::IsBlipPlaying() const{return IsValid(BlipAudio)&&BlipAudio->IsPlaying();}
void UDWTextRevealComponent::EnsureTicker()
{
 if(!TickHandle.IsValid()){LastTick=FPlatformTime::Seconds();TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UDWTextRevealComponent::TickPlayback));}
}
void UDWTextRevealComponent::EndTicker(){if(TickHandle.IsValid()){if(!bInsideTick)FTSTicker::RemoveTicker(TickHandle);TickHandle.Reset();}}
bool UDWTextRevealComponent::TickPlayback(float)
{
 bInsideTick=true;const double Now=FPlatformTime::Seconds();const double Dt=FMath::Max(0.,Now-LastTick);LastTick=Now;
 if(!bConstructed||!GetOwner().IsValid()){StopAudio(true);TickHandle.Reset();bInsideTick=false;return false;}
 if(IsValid(BlipAudio)&&BlipAudio->IsPlaying()&&!bBlipFading&&Now>=BlipEnd)
 {
  const float Fade=VoiceProfile?FMath::Clamp(VoiceProfile->BlipFadeOut,0.f,.1f):0;
  if(Fade>0)BlipAudio->FadeOut(Fade,0);else BlipAudio->Stop();bBlipFading=true;
 }
 if(!bEnableBlips)StopAudio();
 const bool Hidden=bPauseWhenNotPainted&&(!bHasVisiblePaint||LastEffectivePaintAlpha<=.01f||Now-LastPaint>.15);
 if(Hidden||bPaused)StopAudio();
 if(bPlaying&&!bPaused&&!Hidden&&!bExternalClock&&State&&State->bLayoutReady)
 {
  State->Time+=Dt*FMath::Clamp(PlaybackRate,.1f,10.f);const int32 R=Revision;
  while(State&&NextCharacter<State->Glyphs.Num()&&State->Glyphs[NextCharacter].At<=State->Time)
  {
   const int32 I=NextCharacter++;const FString Character=State->Glyphs[I].Text;
   PlayBlip(I);OnCharacterRevealed.Broadcast(I+1,Character);if(R!=Revision)break;
   const auto Snapshot=Cues;
   for(const auto& Cue:Snapshot)if(Cue.AfterCharacter==I+1&&!Cue.CueName.IsNone()){OnCue.Broadcast(Cue.CueName);if(R!=Revision)break;}
   if(R!=Revision)break;
  }
  if(R==Revision&&State&&State->Time>=State->Duration){bPlaying=false;State->bShowAll=true;StopAudio();OnFinished.Broadcast(false);}
  if(auto V=View.Pin())V->Invalidate(EInvalidateWidgetReason::Paint);
 }
 const bool Keep=bPlaying||IsBlipPlaying();if(!Keep)TickHandle.Reset();bInsideTick=false;return Keep;
}
void UDWTextRevealComponent::StopAudio(bool Destroy)
{
 if(IsValid(BlipAudio)){BlipAudio->Stop();if(Destroy){BlipAudio->DestroyComponent();BlipAudio=nullptr;}}bBlipFading=false;
}
void UDWTextRevealComponent::PlayBlip(int32 Index)
{
 auto* P=VoiceProfile.Get();if(!bEnableBlips||!P||!State||!State->Glyphs.IsValidIndex(Index))return;
 const auto& G=State->Glyphs[Index];if(!DWTextRevealInternal::Speakable(G.Text,P->bSpeakNumbers))return;
 const uint32 C=DWTextRevealInternal::Codepoint(G.Text);const bool Chinese=VoiceLanguage==EDWTextVoiceLanguage::Chinese||(VoiceLanguage==EDWTextVoiceLanguage::Auto&&DWTextRevealInternal::IsChinese(C));
 int32& Counter=Chinese?ChineseCounter:EnglishCounter;
 const int32 N=FMath::Max(1,Chinese?P->ChineseEveryNCharacters:P->EnglishEveryNLetters);if(Counter++%N!=0)return;
 const double Now=FPlatformTime::Seconds();if(Now-LastBlip<FMath::Max(.02f,P->MinimumBlipInterval))return;
 // Never play a burst of stale sounds after a frame hitch/fast-forward.
 if(State->Time-G.At>.12)return;
 const uint32 Hash=GetTypeHash(G.Text)^uint32(P->VoiceSeed)*196613u^uint32(Index)*3145739u;
 USoundBase* Sound=P->ResolveCharacterSound(G.Text,VoiceLanguage);if(!Sound)return;
 const float Volume=FMath::Clamp(P->Volume,0.f,1.f)*FMath::Clamp(VolumeMultiplier,0.f,1.f);if(Volume<=0)return;
 const float Variation=((Hash%1001)/500.f-1)*FMath::Clamp(P->PitchVariation,0.f,.5f);
 const float Pitch=FMath::Clamp((Chinese?P->ChinesePitch:P->EnglishPitch)*(1+Variation),.25f,4.f);
 if(!IsValid(BlipAudio))BlipAudio=UGameplayStatics::CreateSound2D(GetOwner().Get(),Sound,Volume,Pitch,0,nullptr,false,false);
 if(auto* S=UDWUserSettings::Resolve(GetOwner().Get()))S->RouteAudio(BlipAudio,EDWSoundCategory::Voice);
 if(!BlipAudio)return;
 BlipAudio->Stop();BlipAudio->SetSound(Sound);BlipAudio->SetVolumeMultiplier(Volume);BlipAudio->SetPitchMultiplier(Pitch);BlipAudio->bIsUISound=true;BlipAudio->Play();
 LastBlip=Now;BlipEnd=Now+FMath::Max(.02f,P->MaximumBlipDuration);bBlipFading=false;++BlipsPlayed;
}
FString UDWTextRevealComponent::GetPlaybackDebugJson() const
{
 auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("time"),GetPlaybackTime());J->SetNumberField(TEXT("duration"),GetDuration());J->SetNumberField(TEXT("visible"),GetVisibleCharacterCount());J->SetNumberField(TEXT("lines"),GetVisualLineCount());J->SetBoolField(TEXT("playing"),bPlaying);J->SetBoolField(TEXT("paused"),bPaused);J->SetNumberField(TEXT("blips"),BlipsPlayed);J->SetBoolField(TEXT("has_visible_paint"),bHasVisiblePaint);J->SetNumberField(TEXT("paint_alpha"),LastEffectivePaintAlpha);
 if(auto V=View.Pin())V->AppendLayoutDebug(J);
 TArray<TSharedPtr<FJsonValue>> A;if(State)for(const auto& G:State->Glyphs){auto O=MakeShared<FJsonObject>();O->SetStringField(TEXT("text"),G.Text);O->SetNumberField(TEXT("at"),G.At);O->SetNumberField(TEXT("line"),G.Line);A.Add(MakeShared<FJsonValueObject>(O));}J->SetArrayField(TEXT("glyphs"),A);
 FString Out;auto Writer=TJsonWriterFactory<>::Create(&Out);FJsonSerializer::Serialize(J,Writer);return Out;
}
UDWTextRevealComponent* UDWTextRevealLibrary::GetTextRevealComponent(UWidget* Widget)
{
 if(!IsValid(Widget))return nullptr;const UWidgetTree* Tree=Cast<UWidgetTree>(Widget->GetOuter());const UUserWidget* User=Tree?Cast<UUserWidget>(Tree->GetOuter()):Widget->GetTypedOuter<UUserWidget>();if(!User)return nullptr;
 auto* Ext=User->GetExtension<UUIComponentUserWidgetExtension>();return Ext&&Ext->IsContainerInitialized()?Cast<UDWTextRevealComponent>(Ext->GetComponent(UDWTextRevealComponent::StaticClass(),Widget->GetFName())):nullptr;
}
bool UDWTextRevealLibrary::RevealWidgetText(UTextBlock* TextWidget,FText Text){if(auto* C=GetTextRevealComponent(TextWidget)){C->PlayText(Text);return true;}return false;}
