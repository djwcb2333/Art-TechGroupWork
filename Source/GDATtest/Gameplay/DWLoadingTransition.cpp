#include "DWLoadingTransition.h"
#include "DWPlayerCharacter.h"
#include "DWUserSettings.h"
#include "DWGameInstance.h"
#include "DWLocalizationLibrary.h"
#include "DWGameplayHUD.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/Pawn.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/AudioComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeRWLock.h"
#include "UObject/UObjectGlobals.h"
#include "UObject/Package.h"
#include "MoviePlayer.h"
#include "Widgets/SLeafWidget.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElementTypes.h"
#include "Fonts/FontMeasure.h"
#include "Styling/CoreStyle.h"
#include "HAL/IConsoleManager.h"
#include "Engine/LevelStreaming.h"
#include "WorldPartition/WorldPartitionSubsystem.h"
#include "ContentStreaming.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "ShaderPipelineCache.h"
#include "DWGameplayWidget.h"

namespace DWIris
{
 double Response(double T,double Hz,double Damping)
 {
  const double W=2*PI*FMath::Clamp(Hz,.1,10.),Z=FMath::Clamp(Damping,.1,2.);T=FMath::Max(0.,T);
  if(Z<.9999){const double D=FMath::Sqrt(1-Z*Z);return 1-FMath::Exp(-Z*W*T)*(FMath::Cos(W*D*T)+Z/D*FMath::Sin(W*D*T));}
  if(Z<=1.0001)return 1-FMath::Exp(-W*T)*(1+W*T);
  const double D=FMath::Sqrt(Z*Z-1),A=-W*(Z-D),B=-W*(Z+D);
  return 1-(-B*FMath::Exp(A*T)+A*FMath::Exp(B*T))/(A-B);
 }
 struct FVisual
 {
  EDWLoadingPhase Phase=EDWLoadingPhase::Idle;
  double PhaseStart=0,Start=0;
  float Radius=1,Progress=0,Close=.85f,Open=.95f,IrisHz=2.6f,IrisDamping=.65f;
  FVector2D Center=FVector2D(.5,.5);
  FLinearColor Mask,Dough,Outline,Fill,Track,Text;
  float ContentScale=1,DoughSize=105,Jump=34,BounceHz=1.35f,BarWidth=320,BarHeight=14,CompleteStrength=.16f,CompleteHz=3.5f,CompleteDamping=.5f,CompleteHold=.7f;
  bool bShow=true,bBounce=true;
  FString Loading,Ready;
  TSharedPtr<const FCompositeFont> Font;
 };
}
struct FDWLoadingPaintState
{
 mutable FRWLock Lock;
 DWIris::FVisual Visual;
};

/** Pure Slate: no UObject reads during painting, so the MoviePlayer can animate during blocking travel. */
class SDWLoadingOverlay:public SLeafWidget
{
public:
 SLATE_BEGIN_ARGS(SDWLoadingOverlay){} SLATE_END_ARGS()
 void Construct(const FArguments&,TSharedPtr<FDWLoadingPaintState,ESPMode::ThreadSafe> InState,bool Movie=false)
 {
  State=InState;bMovie=Movie;SetCanTick(false);ForceVolatile(true);
  White=FCoreStyle::Get().GetBrush("WhiteBrush");
  Resource=FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White);
 }
 virtual bool SupportsKeyboardFocus() const override{return true;}
 virtual FReply OnMouseButtonDown(const FGeometry&,const FPointerEvent&) override{return FReply::Handled();}
 virtual FReply OnMouseButtonUp(const FGeometry&,const FPointerEvent&) override{return FReply::Handled();}
 virtual FReply OnMouseWheel(const FGeometry&,const FPointerEvent&) override{return FReply::Handled();}
 virtual FReply OnKeyDown(const FGeometry&,const FKeyEvent&) override{return FReply::Handled();}
 virtual FReply OnKeyUp(const FGeometry&,const FKeyEvent&) override{return FReply::Handled();}
 virtual FReply OnAnalogValueChanged(const FGeometry&,const FAnalogInputEvent&) override{return FReply::Handled();}
 virtual FVector2D ComputeDesiredSize(float) const override{return FVector2D(1280,720);}
 virtual int32 OnPaint(const FPaintArgs&,const FGeometry& G,const FSlateRect&,FSlateWindowElementList& Draw,int32 Layer,const FWidgetStyle&,bool) const override
 {
  DWIris::FVisual V;{FReadScopeLock Guard(State->Lock);V=State->Visual;}
  if(V.Phase==EDWLoadingPhase::Idle)return Layer;
  if(bMovie&&!bLoggedMovieFrame){bLoggedMovieFrame=true;UE_LOG(LogTemp,Display,TEXT("DW Iris MoviePlayer vector frame painted"));}
  const double Now=FPlatformTime::Seconds(),Age=FMath::Max(0.,Now-V.PhaseStart);
  const FVector2D Size=G.GetLocalSize(),Center=Size*V.Center;
  auto Polygon=[&](const TArray<FVector2D>& Points,FLinearColor Color)
  {
   if(Points.Num()<3)return;
   TArray<FSlateVertex> Vertices;TArray<SlateIndex> Indices;
   for(const auto& P:Points)Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),FVector2f(P),FVector2f(.5f,.5f),Color.ToFColor(true)));
   for(int32 I=1;I<Points.Num()-1;++I){Indices.Add(0);Indices.Add(I);Indices.Add(I+1);}
   FSlateDrawElement::MakeCustomVerts(Draw,++Layer,Resource,Vertices,Indices,nullptr,0,0);
  };
  auto Ellipse=[&](FVector2D C,FVector2D R,FLinearColor Color)
  {
   TArray<FVector2D> P;for(int I=0;I<64;++I){double A=2*PI*I/64;P.Add(C+FVector2D(FMath::Cos(A)*R.X,FMath::Sin(A)*R.Y));}Polygon(P,Color);
  };
  auto Rect=[&](FVector2D P,FVector2D S,FLinearColor C){FSlateDrawElement::MakeBox(Draw,++Layer,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),White,ESlateDrawEffect::None,C);};
  auto RoundBar=[&](FVector2D C,FVector2D S,FLinearColor Color)
  {
   const double R=FMath::Min(S.Y*.5,S.X*.5);TArray<FVector2D> P;
   for(int I=0;I<=24;++I){double A=-PI*.5+PI*I/24;P.Add(C+FVector2D(S.X*.5-R+R*FMath::Cos(A),R*FMath::Sin(A)));}
   for(int I=0;I<=24;++I){double A=PI*.5+PI*I/24;P.Add(C+FVector2D(-S.X*.5+R+R*FMath::Cos(A),R*FMath::Sin(A)));}Polygon(P,Color);
  };
  float Radius=0;
  if(!bMovie&&V.Phase==EDWLoadingPhase::Closing)Radius=UDWLoadingTransitionSubsystem::EvaluateIris(Age,V.Close,V.IrisHz,V.IrisDamping,false);
  if(!bMovie&&V.Phase==EDWLoadingPhase::Opening)Radius=UDWLoadingTransitionSubsystem::EvaluateIris(Age,V.Open,V.IrisHz,V.IrisDamping,true);
  double MaxR=0;for(const auto& C:{FVector2D(0,0),FVector2D(Size.X,0),Size,FVector2D(0,Size.Y)})MaxR=FMath::Max(MaxR,(C-Center).Size());MaxR+=3;
  V.Mask.A=1; // The covered stage must be opaque even if the preset color picker has alpha < 1.
  if(Radius<=.0001)Rect(FVector2D::ZeroVector,Size,V.Mask);
  else if(Radius<1)
  {
   TArray<FSlateVertex> Vertices;TArray<SlateIndex> Indices;
   const double Inner=Radius*MaxR,Outer=MaxR*1.5;
   for(int I=0;I<=192;++I)
   {
    const double A=2*PI*I/192;const FVector2D D(FMath::Cos(A),FMath::Sin(A));
    for(double R:{Inner,Outer})Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(G.GetAccumulatedRenderTransform(),FVector2f(Center+D*R),FVector2f(.5f,.5f),V.Mask.ToFColor(true)));
    if(I<192){int B=I*2;Indices.Append({SlateIndex(B),SlateIndex(B+1),SlateIndex(B+2),SlateIndex(B+1),SlateIndex(B+3),SlateIndex(B+2)});}
   }
   FSlateDrawElement::MakeCustomVerts(Draw,++Layer,Resource,Vertices,Indices,nullptr,0,0);
  }
  if(!V.bShow||(!bMovie&&(V.Phase==EDWLoadingPhase::Closing||V.Phase==EDWLoadingPhase::Opening)))return Layer;
  const double S=FMath::Min(Size.X/1280.,Size.Y/720.)*V.ContentScale;
  const FVector2D C=Size*.5;
  const double Phase=(Now-V.Start)*V.BounceHz,Jump=FMath::Abs(FMath::Sin(PI*Phase));
  const double Squash=FMath::Exp(-Jump*8)*.18,Stretch=Jump*.08;
  const FVector2D BodySize(V.DoughSize*.56*S*(1+Squash-Stretch),V.DoughSize*.52*S*(1-Squash+Stretch));
  const FVector2D Ground=C+FVector2D(0,0),Body=Ground-FVector2D(0,BodySize.Y+V.Jump*S*Jump);
  Ellipse(Ground+FVector2D(0,8*S),FVector2D(V.DoughSize*.4*S*(1-.2*Jump),6*S),FLinearColor(V.Dough.R,V.Dough.G,V.Dough.B,.12f));
  TArray<FVector2D> Points;
  for(int I=0;I<=80;++I){double A=2*PI*I/80;double Shape=1+.035*FMath::Cos(3*A)-.018*FMath::Sin(5*A);Points.Add(Body+FVector2D(FMath::Cos(A)*BodySize.X,FMath::Sin(A)*BodySize.Y)*Shape);}
  Polygon(Points,V.Dough);
  TArray<FVector2f> Outline;for(auto P:Points)Outline.Add(FVector2f(P));FSlateDrawElement::MakeLines(Draw,++Layer,G.ToPaintGeometry(),Outline,ESlateDrawEffect::None,V.Outline,true,2.8*S);
  Ellipse(Body+FVector2D(-BodySize.X*.28,BodySize.Y*.06),FVector2D(3.5*S,5*S),V.Outline);
  Ellipse(Body+FVector2D(BodySize.X*.28,BodySize.Y*.06),FVector2D(3.5*S,5*S),V.Outline);
  const FLinearColor Cheek(1,.31f,.2f,.36f);
  Ellipse(Body+FVector2D(-BodySize.X*.48,BodySize.Y*.24),FVector2D(7*S,3.8*S),Cheek);
  Ellipse(Body+FVector2D(BodySize.X*.48,BodySize.Y*.24),FVector2D(7*S,3.8*S),Cheek);
  TArray<FVector2f> Smile;for(int I=0;I<=16;++I){double A=PI*.12+PI*.76*I/16;Smile.Add(FVector2f(Body+FVector2D(FMath::Cos(A)*8*S,FMath::Sin(A)*5*S+BodySize.Y*.18)));}FSlateDrawElement::MakeLines(Draw,++Layer,G.ToPaintGeometry(),Smile,ESlateDrawEffect::None,V.Outline,true,2*S);
  Ellipse(Body+FVector2D(-BodySize.X*.25,-BodySize.Y*.45),FVector2D(13*S,5*S),FLinearColor(1,1,.9f,.35f));
  double Bounce=V.Phase==EDWLoadingPhase::Complete?UDWLoadingTransitionSubsystem::EvaluateCompletionScale(Age,V.CompleteHold,V.bBounce,V.CompleteStrength,V.CompleteHz,V.CompleteDamping)-1:0;
  const FVector2D BarCenter=C+FVector2D(0,(66-25*Bounce)*S),BarSize(V.BarWidth*S*(1+Bounce),V.BarHeight*S*(1+Bounce));
  RoundBar(BarCenter,BarSize,V.Track);
  if(V.Progress>.0001){const double W=BarSize.X*FMath::Clamp(V.Progress,0.f,1.f);RoundBar(BarCenter-FVector2D((BarSize.X-W)*.5,0),FVector2D(W,BarSize.Y),V.Fill);}
  auto Label=[&](FString Text,double Y,int FontSize)
  {
   const FSlateFontInfo Font=V.Font?FSlateFontInfo(V.Font,FontSize,FName(TEXT("Default"))):FCoreStyle::GetDefaultFontStyle("Bold",FontSize);
   const FVector2D TextSize=FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text,Font);
   const FGeometry TextG=G.MakeChild(TextSize,FSlateLayoutTransform(S,C+FVector2D(-TextSize.X*S*.5,Y*S)));
   FSlateDrawElement::MakeText(Draw,++Layer,TextG.ToPaintGeometry(),Text,Font,ESlateDrawEffect::None,V.Text);
  };
  Label(FString::Printf(TEXT("%d%%"),FMath::FloorToInt(V.Progress*100+.001f)),91,24);
  Label(V.Progress>=1?V.Ready:V.Loading,128,16);
  return Layer;
 }
private:
 TSharedPtr<FDWLoadingPaintState,ESPMode::ThreadSafe> State;
 const FSlateBrush* White=nullptr;FSlateResourceHandle Resource;bool bMovie=false;mutable bool bLoggedMovieFrame=false;
};

float UDWLoadingTransitionSubsystem::EvaluateCompletionScale(float Elapsed,float Hold,bool Enabled,float Strength,float Hz,float Damping)
{
 if(!Enabled||Elapsed<=0||Elapsed>=Hold)return 1;
 return 1+FMath::Clamp(Strength,0.f,.5f)*float(DWIris::Response(Elapsed,Hz,Damping)-DWIris::Response(FMath::Max(0.f,Elapsed-.1f),Hz,Damping));
}
float UDWLoadingTransitionSubsystem::EvaluateIris(float Elapsed,float Duration,float Hz,float Damping,bool Opening)
{
 if(Elapsed<=0)return Opening?0.f:1.f;
 if(Duration<=0||Elapsed>=Duration)return Opening?1.f:0.f;
 const double End=DWIris::Response(Duration,Hz,Damping),Value=DWIris::Response(Elapsed,Hz,Damping)/FMath::Max(.001,End);
 return FMath::Clamp(float(Opening?Value:1-Value),0.f,1.f);
}
void UDWLoadingTransitionSubsystem::Initialize(FSubsystemCollectionBase& C)
{
 Super::Initialize(C);bAlive=true;
 TickHandle=FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this,&UDWLoadingTransitionSubsystem::Tick));
 PostLoadHandle=FCoreUObjectDelegates::PostLoadMapWithWorld.AddUObject(this,&UDWLoadingTransitionSubsystem::MapLoaded);
 if(GEngine)TravelFailureHandle=GEngine->OnTravelFailure().AddWeakLambda(this,[this](UWorld* W,ETravelFailure::Type,const FString& Message){if(IsTransitionActive()&&W&&W->GetGameInstance()==GetGameInstance())FailTransition(Message);});
}
void UDWLoadingTransitionSubsystem::Deinitialize()
{
 bAlive=false;++RequestSerial;FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(PostLoadHandle);
 if(GEngine)GEngine->OnTravelFailure().Remove(TravelFailureHandle);
 Finish();Super::Deinitialize();
}
UDWLoadingTransitionSettings* UDWLoadingTransitionSubsystem::GetDefaultSettings()
{
 if(!DefaultSettings)
 {
  if(auto* GI=Cast<UDWGameInstance>(GetGameInstance()))DefaultSettings=GI->LoadingTransitionSettings.LoadSynchronous();
  if(!DefaultSettings)DefaultSettings=NewObject<UDWLoadingTransitionSettings>(this);
 }
 return DefaultSettings;
}
bool UDWLoadingTransitionSubsystem::TravelToLevel(TSoftObjectPtr<UWorld> Destination,UDWLoadingTransitionSettings* Override){return RequestMap(Destination.ToSoftObjectPath().GetLongPackageName(),Override);}
bool UDWLoadingTransitionSubsystem::RequestMap(const FString& Map,UDWLoadingTransitionSettings* Override)
{
 if(IsTransitionActive())return false;
 if(!GetWorld()||!FPackageName::IsValidLongPackageName(Map)||!FPackageName::DoesPackageExist(Map)){LastError=DWText(this,TEXT("目标地图不存在，未开始切换。"),TEXT("The destination map is missing. Travel was not started."));return false;}
 TargetMap=Map;
 auto* Settings=Override?Override:GetDefaultSettings();
 if(!Settings->bEnabled){UGameplayStatics::OpenLevel(this,FName(*Map));return true;}
 return Begin(EMode::Travel,Settings);
}
bool UDWLoadingTransitionSubsystem::BeginManualTransition(UDWLoadingTransitionSettings* Override){return Begin(EMode::Manual,Override);}
bool UDWLoadingTransitionSubsystem::PreviewTransition(float Seconds,UDWLoadingTransitionSettings* Override){if(IsTransitionActive())return false;PreviewSeconds=FMath::Max(.1f,Seconds);return Begin(EMode::Preview,Override);}
void UDWLoadingTransitionSubsystem::SetManualProgress(float P){if(Mode==EMode::Manual&&IsTransitionActive()&&FMath::IsFinite(P)){Progress=FMath::Max(Progress,FMath::Clamp(P,0.f,.99f));Publish();}}
void UDWLoadingTransitionSubsystem::CompleteManualTransition(){if(Mode==EMode::Manual&&IsTransitionActive())bWorkComplete=true;}
void UDWLoadingTransitionSubsystem::CancelManualTransition(){if(Mode!=EMode::Travel&&IsTransitionActive()){bFailed=true;SetPhase(EDWLoadingPhase::Opening);}}
bool UDWLoadingTransitionSubsystem::Begin(EMode NewMode,UDWLoadingTransitionSettings* Override)
{
 if(!bAlive||IsTransitionActive()||!GetWorld()||!GetGameInstance()->GetGameViewportClient())return false;
 ActiveSettings=Override?Override:GetDefaultSettings();Mode=NewMode;LastError=FText();bFailed=false;bWorkComplete=false;bPreloadComplete=false;ReadyFrames=0;ArrivedWorld.Reset();PreloadedPackage=nullptr;PreloadedWorld=nullptr;++RequestSerial;Progress=0;
 ReadinessTasks.Reset();ReadinessStatus=FText();PeakCompileWork=PeakTextureWork=0;PendingLevels=PendingShaderJobs=PendingAssets=PendingTextures=0;bReadyToDraw=false;bReadinessStalled=false;ReadySince=0;
 RequestStarted=FPlatformTime::Seconds();LoadingStarted=0;
 PaintState=MakeShared<FDWLoadingPaintState,ESPMode::ThreadSafe>();
 auto& V=PaintState->Visual;const auto* S=ActiveSettings.Get();
 if(S->Font&&S->Font->GetCompositeFont())V.Font=MakeShared<FCompositeFont>(*S->Font->GetCompositeFont());
 V.Start=RequestStarted;V.Close=FMath::Max(.1f,S->CloseSeconds);V.Open=FMath::Max(.1f,S->OpenSeconds);V.IrisHz=S->IrisFrequencyHz;V.IrisDamping=S->IrisDampingRatio;
 V.Center=FVector2D(FMath::Clamp(S->CircleCenter.X,0.,1.),FMath::Clamp(S->CircleCenter.Y,0.,1.));V.Mask=S->MaskColor;V.Dough=S->DoughColor;V.Outline=S->OutlineColor;V.Fill=S->ProgressColor;V.Track=S->TrackColor;V.Text=S->TextColor;
 V.ContentScale=FMath::Clamp(S->ContentScale,.5f,2.f);V.DoughSize=FMath::Max(1.f,S->DoughSize);V.Jump=FMath::Max(0.f,S->JumpHeight);V.BounceHz=FMath::Max(.1f,S->BounceFrequencyHz);V.BarWidth=FMath::Max(100.f,S->BarWidth);V.BarHeight=FMath::Max(4.f,S->BarHeight);V.CompleteStrength=S->CompletionBounceStrength;V.CompleteHz=S->CompletionFrequencyHz;V.CompleteDamping=S->CompletionDampingRatio;V.CompleteHold=S->CompletionHoldSeconds;V.bBounce=S->bBounceBarAtComplete;V.bShow=S->bShowMascotAndProgress;
 const bool EN=UDWLocalizationLibrary::GetLanguage(this)==EDWGameLanguage::English;V.Loading=(EN?S->LoadingEnglish:S->LoadingChinese).ToString();V.Ready=(EN?S->ReadyEnglish:S->ReadyChinese).ToString();
 Overlay=SNew(SDWLoadingOverlay,PaintState,false);AttachOverlay();LockInput();SetPhase(EDWLoadingPhase::Closing);return true;
}
void UDWLoadingTransitionSubsystem::AttachOverlay()
{
 if(Overlay.IsValid())if(auto* View=GetGameInstance()->GetGameViewportClient()){View->RemoveViewportWidgetContent(Overlay.ToSharedRef());View->AddViewportWidgetContent(Overlay.ToSharedRef(),100000);}
}
void UDWLoadingTransitionSubsystem::LockInput()
{
 auto* PC=GetGameInstance()->GetFirstLocalPlayerController();if(!PC||LockedController.Get()==PC)return;
 ReleaseInput();LockedController=PC;PC->SetIgnoreMoveInput(true);PC->SetIgnoreLookInput(true);
 if(Overlay&&FSlateApplication::IsInitialized())FSlateApplication::Get().SetKeyboardFocus(Overlay,EFocusCause::SetDirectly);
}
void UDWLoadingTransitionSubsystem::ReleaseInput(){if(auto* PC=LockedController.Get()){PC->SetIgnoreMoveInput(false);PC->SetIgnoreLookInput(false);}LockedController.Reset();}
void UDWLoadingTransitionSubsystem::PlayTransitionSound(USoundBase* Sound)
{
 if(TransitionSound){TransitionSound->Stop();TransitionSound->DestroyComponent();TransitionSound=nullptr;}
 if(Sound){TransitionSound=UGameplayStatics::CreateSound2D(this,Sound,ActiveSettings->SoundVolume,1,0,nullptr,true,true);if(TransitionSound){if(auto* S=UDWUserSettings::Resolve(this))S->RouteAudio(TransitionSound,EDWSoundCategory::SFX);TransitionSound->SetUISound(true);TransitionSound->Play();}}
}
void UDWLoadingTransitionSubsystem::SetPhase(EDWLoadingPhase P)
{
 Phase=P;PhaseStarted=FPlatformTime::Seconds();Publish();
 UE_LOG(LogTemp,Display,TEXT("DW Iris phase=%d progress=%.3f target=%s"),int(P),Progress,*TargetMap);
 if(P==EDWLoadingPhase::Closing)PlayTransitionSound(ActiveSettings->CloseSound);
 if(P==EDWLoadingPhase::Opening)PlayTransitionSound(ActiveSettings->OpenSound);
}
void UDWLoadingTransitionSubsystem::Publish(){if(PaintState){FWriteScopeLock Guard(PaintState->Lock);PaintState->Visual.Phase=Phase;PaintState->Visual.Progress=Progress;PaintState->Visual.PhaseStart=PhaseStarted;if(!ReadinessStatus.IsEmpty())PaintState->Visual.Loading=ReadinessStatus.ToString();}}
void UDWLoadingTransitionSubsystem::StartPreload()
{
 // Reloading the current world must let LoadMap collect that old world normally.
 if(GetWorld()&&UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==TargetMap)
 {
  bPreloadComplete=true;Progress=.5f;Publish();return;
 }
 const uint32 Serial=RequestSerial;
 LoadPackageAsync(TargetMap,FLoadPackageAsyncDelegate::CreateWeakLambda(this,[this,Serial](const FName&,UPackage* Package,EAsyncLoadingResult::Type Result)
 {
  if(!bAlive||Serial!=RequestSerial||Phase!=EDWLoadingPhase::Loading||Mode!=EMode::Travel)return;
  if(Result!=EAsyncLoadingResult::Succeeded||!Package){FailTransition(TEXT("The destination package could not be loaded."));return;}
  PreloadedPackage=Package;PreloadedWorld=UWorld::FindWorldInPackage(Package);
  if(!PreloadedWorld)PreloadedWorld=UWorld::FollowWorldRedirectorInPackage(Package);
  if(!PreloadedWorld){FailTransition(TEXT("The destination package does not contain a world."));return;}
  bPreloadComplete=true;Progress=.5f;Publish();
 }));
}
void UDWLoadingTransitionSubsystem::StartTravel()
{
 Progress=.5f;ReadinessStatus=DWText(this,TEXT("初始化关卡与玩家数据"),TEXT("Initializing world and player data"));SetPhase(EDWLoadingPhase::Travelling);
 // MoviePlayer is unavailable in PIE. A separate pure Slate instance covers blocking loads in standalone.
 if(GetWorld()->WorldType!=EWorldType::PIE&&IsMoviePlayerEnabled()&&GetMoviePlayer()->IsInitialized())
 {
  FLoadingScreenAttributes A;A.bAutoCompleteWhenLoadingCompletes=true;A.bMoviesAreSkippable=false;A.bWaitForManualStop=false;A.bAllowEngineTick=false;
  A.WidgetLoadingScreen=SNew(SDWLoadingOverlay,PaintState,true);GetMoviePlayer()->SetupLoadingScreen(A);bOwnsMovie=true;
 }
 UGameplayStatics::SetGamePaused(this,false);UGameplayStatics::OpenLevel(this,FName(*TargetMap));
}
void UDWLoadingTransitionSubsystem::MapLoaded(UWorld* W)
{
 if(!bAlive||Mode!=EMode::Travel||Phase!=EDWLoadingPhase::Travelling||!W||W->GetGameInstance()!=GetGameInstance())return;
 if(UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())!=TargetMap){FailTransition(TEXT("The engine loaded a different map."));return;}
 ArrivedWorld=W;ReadyFrames=0;AttachOverlay();LockInput();
 UnbindDrawEvents();
 if(auto* V=GetGameInstance()->GetGameViewportClient()){ObservedViewport=V;BeginDrawHandle=V->OnBeginDraw().AddUObject(this,&UDWLoadingTransitionSubsystem::BeforeDestinationDraw);EndDrawHandle=V->OnEndDraw().AddUObject(this,&UDWLoadingTransitionSubsystem::AfterDestinationDraw);}
 // Prime the destination POV under the opaque cover, before any visible frame.
 // Preserve explicit cinematic/menu camera targets; only replace the controller fallback.
 if(auto* PC=GetGameInstance()->GetFirstLocalPlayerController())
 {
  if(PC->GetPawn()&&PC->GetViewTarget()==PC)PC->SetViewTarget(PC->GetPawn());
  PC->UpdateCameraManager(0.f);
  if(PC->PlayerCameraManager)PC->PlayerCameraManager->SetGameCameraCutThisFrame();
 }
 bDestinationWasPaused=UGameplayStatics::IsGamePaused(W);bPausedDestination=ActiveSettings->bPauseDestinationUntilRevealed;
 if(bPausedDestination)UGameplayStatics::SetGamePaused(W,true);
}
bool UDWLoadingTransitionSubsystem::Tick(float)
{
 if(!bAlive)return false;if(!IsTransitionActive())return true;
 const double Now=FPlatformTime::Seconds(),Age=Now-PhaseStarted;
 if(Phase!=EDWLoadingPhase::Opening&&Now-RequestStarted>FMath::Max(5.f,ActiveSettings->TimeoutSeconds))
 {if(Mode==EMode::Travel){if(!bReadinessStalled){bReadinessStalled=true;UE_LOG(LogTemp,Warning,TEXT("DW loading still waiting; cover retained, not reporting completion."));}}else{FailTransition(TEXT("The transition timed out."));return true;}}
 LockInput();
 switch(Phase)
 {
 case EDWLoadingPhase::Closing:
  if(Age>=FMath::Max(.1f,ActiveSettings->CloseSeconds)){LoadingStarted=Now;if(Mode==EMode::Travel)ReadinessStatus=DWText(this,TEXT("读取关卡资源"),TEXT("Reading level resources"));SetPhase(EDWLoadingPhase::Loading);OnCovered.Broadcast();if(Phase==EDWLoadingPhase::Loading&&Mode==EMode::Travel)StartPreload();}break;
 case EDWLoadingPhase::Loading:
  if(Mode==EMode::Travel)
  {
   if(bPreloadComplete){StartTravel();break;}
   const float P=GetAsyncLoadPercentage(FName(*TargetMap));if(P>=0)Progress=FMath::Clamp(P*.005f,0.f,.5f);
  }
  else if(Mode==EMode::Preview){Progress=FMath::Clamp(float((Now-LoadingStarted)/PreviewSeconds),0.f,.99f);bWorkComplete=Now-LoadingStarted>=PreviewSeconds;}
  if(bWorkComplete&&Now-LoadingStarted>=ActiveSettings->MinimumLoadingSeconds){Progress=1;SetPhase(EDWLoadingPhase::Complete);OnLoadingComplete.Broadcast();}break;
 case EDWLoadingPhase::Travelling:
   bReadyToDraw=CheckDestinationReadiness();if(!bReadyToDraw){ReadyFrames=0;ReadySince=0;}else if(ReadySince==0)ReadySince=Now;
   if(auto* W=ArrivedWorld.Get())if(W->HasBegunPlay()&&GetGameInstance()->GetFirstLocalPlayerController())
   {
    auto* PC=GetGameInstance()->GetFirstLocalPlayerController();
    if(PC->GetPawn()&&PC->GetViewTarget()==PC){PC->SetViewTarget(PC->GetPawn());ReadyFrames=0;}
    PC->UpdateCameraManager(0.f);
   // Keep the cover for rendered-world initialization ticks; player BeginPlay restores pending saves.
   if(bReadyToDraw&&ReadyFrames>=FMath::Max(3,ActiveSettings->MinimumReadyFrames)&&Now-ReadySince>=ActiveSettings->StableReadySeconds&&Now-LoadingStarted>=ActiveSettings->MinimumLoadingSeconds){Progress=1;PreloadedPackage=nullptr;PreloadedWorld=nullptr;SetPhase(EDWLoadingPhase::Complete);OnLoadingComplete.Broadcast();}
  }break;
 case EDWLoadingPhase::Complete:
  if(Mode==EMode::Travel&&!CheckDestinationReadiness()){ReadyFrames=0;ReadySince=0;bReadyToDraw=false;SetPhase(EDWLoadingPhase::Travelling);break;}
  Progress=1;
  if(Age>=FMath::Max(0.f,ActiveSettings->CompletionHoldSeconds))SetPhase(EDWLoadingPhase::Opening);break;
 case EDWLoadingPhase::Opening:
  if(Age>=FMath::Max(.1f,ActiveSettings->OpenSeconds))Finish();break;
 default:break;
 }
 Publish();return true;
}
void UDWLoadingTransitionSubsystem::FailTransition(const FString& Message)
{
 if(bFailed)return;
 LastError=DWText(this,TEXT("过渡失败，已恢复界面。请检查目标关卡与日志。"),TEXT("Transition failed. Check the destination level and log."));
 UE_LOG(LogTemp,Error,TEXT("DW Iris failure: %s"),*Message);bFailed=true;++RequestSerial;PreloadedPackage=nullptr;PreloadedWorld=nullptr;
 if(bOwnsMovie&&IsMoviePlayerEnabled()){GetMoviePlayer()->StopMovie();bOwnsMovie=false;}
 SetPhase(EDWLoadingPhase::Opening);OnFailed.Broadcast(LastError);
 if(auto* PC=GetGameInstance()->GetFirstLocalPlayerController())if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->Notify(LastError);
}
void UDWLoadingTransitionSubsystem::Finish()
{
 UnbindDrawEvents();
 const bool WasActive=IsTransitionActive();Phase=EDWLoadingPhase::Idle;Publish();
 if(Overlay)if(auto* V=GetGameInstance()->GetGameViewportClient())V->RemoveViewportWidgetContent(Overlay.ToSharedRef());
 Overlay.Reset();ReleaseInput();PreloadedPackage=nullptr;PreloadedWorld=nullptr;
 if(bPausedDestination&&ArrivedWorld.IsValid())UGameplayStatics::SetGamePaused(ArrivedWorld.Get(),bDestinationWasPaused);
 bPausedDestination=false;ArrivedWorld.Reset();
 if(bOwnsMovie&&IsMoviePlayerEnabled()){GetMoviePlayer()->StopMovie();GetMoviePlayer()->SetupLoadingScreen(FLoadingScreenAttributes());}bOwnsMovie=false;
 if(TransitionSound){TransitionSound->Stop();TransitionSound->DestroyComponent();TransitionSound=nullptr;}
 PaintState.Reset();ActiveSettings=nullptr;
 if(bAlive&&WasActive)
 {
  UE_LOG(LogTemp,Display,TEXT("DW Iris finished progress=%.3f failed=%d"),Progress,bFailed);
  if(auto* PC=GetGameInstance()->GetFirstLocalPlayerController())if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD()))H->ShowMenu(H->GetMenuPage());
  OnFinished.Broadcast();
 }
}
bool UDWLoadingTransitionSubsystem::PrimeDestinationCamera()
{
 auto* W=ArrivedWorld.Get();auto* PC=GetGameInstance()->GetFirstLocalPlayerController();
 if(!W||!W->HasBegunPlay()||!PC||PC->GetWorld()!=W||!PC->PlayerCameraManager)return false;
 const auto* GI=Cast<UDWGameInstance>(GetGameInstance());
 if(GI&&GI->HasActiveSlot())
 {
  auto* P=Cast<ADWPlayerCharacter>(PC->GetPawn());if(!P||!P->HasActorBegunPlay())return false;
  P->PrepareCameraForReveal();if(PC->GetViewTarget()!=P)PC->SetViewTarget(P);
 }
 PC->UpdateCameraManager(0.f);PC->PlayerCameraManager->SetGameCameraCutThisFrame();return PC->GetViewTarget()!=PC;
}
void UDWLoadingTransitionSubsystem::BeforeDestinationDraw(){if(IsTransitionActive())PrimeDestinationCamera();}
void UDWLoadingTransitionSubsystem::AfterDestinationDraw(){if(Phase==EDWLoadingPhase::Travelling){if(bReadyToDraw&&PrimeDestinationCamera())++ReadyFrames;else ReadyFrames=0;}}
void UDWLoadingTransitionSubsystem::UnbindDrawEvents(){if(auto* V=ObservedViewport.Get()){V->OnBeginDraw().Remove(BeginDrawHandle);V->OnEndDraw().Remove(EndDrawHandle);}BeginDrawHandle.Reset();EndDrawHandle.Reset();ObservedViewport.Reset();}
void UDWLoadingTransitionSubsystem::AddReadinessTask(FName Id,FText Description){if(IsTransitionActive()&&!Id.IsNone()){ReadinessTasks.Add(Id,Description);ReadyFrames=0;bReadyToDraw=false;ReadySince=0;}}
void UDWLoadingTransitionSubsystem::CompleteReadinessTask(FName Id){ReadinessTasks.Remove(Id);}
bool UDWLoadingTransitionSubsystem::CheckDestinationReadiness()
{
 auto* W=ArrivedWorld.Get();auto* PC=GetGameInstance()->GetFirstLocalPlayerController();
 Progress=.5f;ReadinessStatus=DWText(this,TEXT("初始化关卡与玩家数据"),TEXT("Initializing world and player data"));
 if(!W||!W->HasBegunPlay()||!PC||PC->GetWorld()!=W)return false;
 if(auto* GI=Cast<UDWGameInstance>(GetGameInstance());GI&&GI->HasActiveSlot())if(GI->IsPlayerRestorePending()||!Cast<ADWPlayerCharacter>(PC->GetPawn()))return false;
 auto* HUD=Cast<ADWGameplayHUD>(PC->GetHUD());if(!HUD||!HUD->GetGameplayWidget()||!HUD->GetGameplayWidget()->IsInViewport())return false;
 if(!PrimeDestinationCamera())return false;
 Progress=.65f;PendingLevels=0;
 if(ActiveSettings->bWaitForLevelStreaming){for(auto* L:W->GetStreamingLevels())if(L&&L->IsStreamingStatePending())++PendingLevels;if(auto* WP=W->GetSubsystem<UWorldPartitionSubsystem>();WP&&!WP->IsAllStreamingCompleted())++PendingLevels;}
 if(PendingLevels){ReadinessStatus=FText::Format(DWText(this,TEXT("加载地形 / 子关卡：剩余 {0}"),TEXT("Streaming terrain / levels: {0} pending")),PendingLevels);return false;}
 Progress=.75f;PendingShaderJobs=PendingAssets=0;
 if(ActiveSettings->bWaitForAssetCompilation){
  if(GShaderCompilingManager){GShaderCompilingManager->ProcessAsyncResults(true,false);PendingShaderJobs=GShaderCompilingManager->GetNumRemainingJobs();}
  FAssetCompilingManager::Get().ProcessAsyncTasks(true);PendingAssets=FAssetCompilingManager::Get().GetNumRemainingAssets();
  PendingShaderJobs+=int32(FShaderPipelineCache::NumPrecompilesRemaining());
 }
 const int32 CompileWork=PendingShaderJobs+PendingAssets;PeakCompileWork=FMath::Max(PeakCompileWork,CompileWork);
 if(CompileWork){Progress=.75f+.15f*(1-float(CompileWork)/FMath::Max(1,PeakCompileWork));ReadinessStatus=FText::Format(DWText(this,TEXT("准备着色器 {0} / 资源 {1}"),TEXT("Preparing shaders {0} / assets {1}")),PendingShaderJobs,PendingAssets);return false;}
 Progress=.90f;PendingTextures=ActiveSettings->bWaitForTextureStreaming?IStreamingManager::Get().GetRenderAssetStreamingManager().GetNumWantingResources():0;
 PeakTextureWork=FMath::Max(PeakTextureWork,PendingTextures);
 if(PendingTextures){Progress=.90f+.07f*(1-float(PendingTextures)/FMath::Max(1,PeakTextureWork));ReadinessStatus=FText::Format(DWText(this,TEXT("准备纹理：剩余 {0}"),TEXT("Streaming textures: {0} pending")),PendingTextures);return false;}
 if(ReadinessTasks.Num()){ReadinessStatus=ReadinessTasks.CreateConstIterator().Value();return false;}
 Progress=.97f+.02f*FMath::Clamp(float(ReadyFrames)/FMath::Max(3,ActiveSettings->MinimumReadyFrames),0.f,1.f);
 ReadinessStatus=DWText(this,TEXT("确认镜头与最终画面"),TEXT("Verifying camera and rendered frames"));return true;
}
static FAutoConsoleCommandWithWorld GPreviewDWIris(TEXT("dw.Transition.Preview"),TEXT("Preview the reusable iris transition without changing maps."),FConsoleCommandWithWorldDelegate::CreateLambda([](UWorld* W){if(W&&W->GetGameInstance())W->GetGameInstance()->GetSubsystem<UDWLoadingTransitionSubsystem>()->PreviewTransition();}));


#if WITH_DEV_AUTOMATION_TESTS
static FAutoConsoleCommandWithWorldAndArgs GVerifyDWIris(TEXT("dw.Transition.VerifyTravel"),TEXT("Standalone development smoke test; travels to the supplied map then exits the test process."),FConsoleCommandWithWorldAndArgsDelegate::CreateLambda([](const TArray<FString>& Args,UWorld* W)
{
 if(GIsEditor||!W||!W->GetGameInstance()||Args.Num()!=1)return;
 auto* S=W->GetGameInstance()->GetSubsystem<UDWLoadingTransitionSubsystem>();if(!S->RequestMap(Args[0]))return;
 const TWeakObjectPtr<UDWLoadingTransitionSubsystem> Weak(S);const double Started=FPlatformTime::Seconds();
 FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Weak,Started](float)
 {
  if(!Weak.IsValid()){UE_LOG(LogTemp,Error,TEXT("DW Iris standalone verification lost subsystem"));FPlatformMisc::RequestExit(false);return false;}
  if(!Weak->IsTransitionActive())
  {
   UE_LOG(LogTemp,Display,TEXT("DW Iris standalone verification complete progress=%.3f error=%s"),Weak->Progress,*Weak->LastError.ToString());FPlatformMisc::RequestExit(false);return false;
  }
  if(FPlatformTime::Seconds()-Started>90){UE_LOG(LogTemp,Error,TEXT("DW Iris standalone verification timed out"));FPlatformMisc::RequestExit(false);return false;}return true;
 }));
}));
#endif
