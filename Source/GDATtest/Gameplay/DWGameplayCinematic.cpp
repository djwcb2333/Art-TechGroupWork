#include "DWGameplayCinematic.h"
#if WITH_EDITOR
#include "HAL/PlatformTime.h"
#include "Misc/ScopeExit.h"
#include "HAL/IConsoleManager.h"
#include "CoreGlobals.h"
#include "AssetCompilingManager.h"
#include "ShaderCompiler.h"
#include "PipelineStateCache.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include <limits>
#endif
#endif

#include "ProfilingDebugging/CpuProfilerTrace.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "DWGameplayCameraShake.h"
#include "Camera/CameraShakeBase.h"
#include "DWWorldEvent.h"
#include "DWUIOffscreenComponent.h"
#include "DWTextRevealComponent.h"
#include "DWLocalizationLibrary.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWGameplayHUD.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/TextBlock.h"
#include "Components/InputComponent.h"
#include "InputCoreTypes.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

void UDWCinematicSubtitleWidget::ShowCue(FText Text,UDWTextVoiceProfile* Voice)
{if(!SubtitleText)return;if(auto* R=UDWTextRevealLibrary::GetTextRevealComponent(SubtitleText)){if(Voice)R->VoiceProfile=Voice;R->PlayText(Text);}else SubtitleText->SetText(Text);}
UDWCinematicComponent::UDWCinematicComponent(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;}
void UDWCinematicComponent::BeginPlay()
{
 Super::BeginPlay();
 if(bViewOnly){ViewEvent=NewObject<UDWWorldEventComponent>(GetOwner(),NAME_None,RF_Transient);ViewEvent->EventId=ViewEventId;ViewEvent->RegisterComponent();}
}
bool UDWCinematicComponent::BeginExternalControl(ADWPlayerController* Controller,APawn* ControlledPawn)
{
 if(bExternalControlOwned)return !bExternalCompletionPending&&ExternalController.Get()==Controller;
 if(IsPlaying()||!IsValid(Controller)||!IsValid(ControlledPawn)||Controller->GetPawn()!=ControlledPawn||!GetWorld())return false;
 auto* Events=GetWorld()->GetSubsystem<UDWWorldEventSubsystem>();
 if(!Events||!Events->Acquire(this))return false;
 ExternalEvents=Events;ExternalController=Controller;bExternalControlOwned=true;
 bExternalCompletionPending=false;bExternalCompletionSuccessful=true;bExternalDeferToDirector=false;bExternalResourcesReleased=false;
 bExternalCursor=Controller->bShowMouseCursor;Controller->bShowMouseCursor=false;
 Controller->SetIgnoreMoveInput(true);Controller->SetIgnoreLookInput(true);
 SetComponentTickEnabled(false);SetPhase(EDWCinematicPhase::External);++PlaybackCount;LastError.Empty();
 return true;
}
void UDWCinematicComponent::EndExternalControl(bool bSuccessful)
{
 if(!bExternalControlOwned)return;
 bExternalCompletionPending=true;bExternalCompletionSuccessful&=bSuccessful;
}
void UDWCinematicComponent::ReleaseExternalResources()
{
 if(!bExternalControlOwned||bExternalResourcesReleased)return;
 bExternalResourcesReleased=true;
 if(ExternalEvents.IsValid())ExternalEvents->Release(this);
 if(ExternalController.IsValid()){
  ExternalController->SetIgnoreMoveInput(false);ExternalController->SetIgnoreLookInput(false);
  ExternalController->bShowMouseCursor=bExternalCursor;ExternalController->InvalidateCombatInputLife();
 }
}
void UDWCinematicComponent::FlushExternalCompletion()
{
 if(!bExternalControlOwned||!bExternalCompletionPending)return;
 const bool bSuccessful=bExternalCompletionSuccessful;
 ReleaseExternalResources();
 bExternalControlOwned=false;bExternalCompletionPending=false;bExternalDeferToDirector=false;ExternalEvents.Reset();ExternalController.Reset();SetPhase(EDWCinematicPhase::Idle);
 OnFinished.Broadcast(bSuccessful);
}
void UDWCinematicComponent::RequestExternalAspect(ACameraActor* Camera,float TargetAspect)
{
 if(!IsValid(Camera)||!Camera->GetCameraComponent())return;
 auto* Cam=Camera->GetCameraComponent();ExternalAspectFrom=Cam->AspectRatio;
 ExternalAspectTo=TargetAspect;ExternalAspectAge=0.f;Cam->SetConstraintAspectRatio(true);
}
void UDWCinematicComponent::AdvanceExternalAspect(ACameraActor* Camera,float DeltaSeconds)
{
 if(!IsValid(Camera)||!Camera->GetCameraComponent())return;
 ExternalAspectAge+=FMath::Max(0.f,DeltaSeconds);
 const float T=FMath::SmoothStep(0.f,1.f,ExternalAspectAge/FMath::Max(.001f,AspectTransitionSeconds));
 Camera->GetCameraComponent()->SetAspectRatio(FMath::Lerp(ExternalAspectFrom,ExternalAspectTo,T));
}
void UDWCinematicComponent::ApplyAuthoredCameraPose(ACameraActor* Camera,const FVector& Location,const FQuat& Rotation,float FieldOfView)
{
 if(!IsValid(Camera)||!Camera->GetCameraComponent())return;
 Camera->SetActorLocationAndRotation(Location,Rotation);Camera->GetCameraComponent()->SetFieldOfView(FieldOfView);
}
UDWCinematicSubtitleWidget* UDWCinematicComponent::CreateAuthoredSubtitle(APlayerController* Controller,int32 ZOrder)
{
#if WITH_EDITOR
 const double PIEWidgetStarted=FPlatformTime::Seconds();
 ON_SCOPE_EXIT
 {if(GetWorld()&&GetWorld()->WorldType==EWorldType::PIE)UE_LOG(LogTemp,Display,TEXT("DW_PIE_SUBTITLE_CREATE owner=%s class=%s cpu_ms=%.3f"),*GetNameSafe(GetOwner()),*GetNameSafe(SubtitleWidgetClass.Get()),(FPlatformTime::Seconds()-PIEWidgetStarted)*1000.);};
#endif

 TRACE_CPUPROFILER_EVENT_SCOPE(DW_Cinematic_CreateSubtitle);
 if(!IsValid(Controller)||!SubtitleWidgetClass)return nullptr;
 auto* Widget=CreateWidget<UDWCinematicSubtitleWidget>(Controller,SubtitleWidgetClass);
 if(Widget)Widget->AddToViewport(ZOrder);
 return Widget;
}
UDWDialogueSequenceComponent* UDWCinematicComponent::CreateAuthoredDialogue()
{
 if(!IsValid(GetOwner()))return nullptr;
 auto* Queue=NewObject<UDWDialogueSequenceComponent>(GetOwner(),NAME_None,RF_Transient);
 if(Queue)Queue->RegisterComponent();
 return Queue;
}
void UDWCinematicComponent::SetPhase(EDWCinematicPhase P)
{
#if WITH_EDITOR
 if(GetWorld()&&GetWorld()->WorldType==EWorldType::PIE)UE_LOG(LogTemp,Display,TEXT("DW_PIE_CINEMATIC_PHASE owner=%s phase=%d"),*GetNameSafe(GetOwner()),int32(P));
#endif
TRACE_BOOKMARK(TEXT("DW Cinematic phase=%d owner=%s"),int32(P),*GetNameSafe(GetOwner()));Phase=P;PhaseSeconds=0;if(P==EDWCinematicPhase::TravelBack&&bStopShakeWhenReturning)StopCinematicShake(false);}
void UDWCinematicComponent::StartCinematicShake()
{
 if(!PC.IsValid()||!PC->PlayerCameraManager||!PlaybackCamera||PC->GetViewTarget()!=PlaybackCamera||!IsPlaying()||Phase==EDWCinematicPhase::UIReturn)return;
 StopCinematicShake(true);
 if(bCinematicShakeEnabled&&CinematicShakeClass&&CinematicShakeScale>0.f)
  ActiveCinematicShake=PC->PlayerCameraManager->StartCameraShake(CinematicShakeClass,CinematicShakeScale,ECameraShakePlaySpace::CameraLocal);
}
void UDWCinematicComponent::StopCinematicShake(bool Immediately)
{
 if(ActiveCinematicShake&&PC.IsValid()&&PC->PlayerCameraManager)PC->PlayerCameraManager->StopCameraShake(ActiveCinematicShake,Immediately);
 if(Immediately)ActiveCinematicShake=nullptr;
}
bool UDWCinematicComponent::PlayCinematic(APlayerController* C)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(DW_Cinematic_Play);
 LastError.Empty();
 auto Fail=[this](const TCHAR* M){LastError=M;return false;};
 if(IsPlaying()||bExternalControlOwned)return Fail(TEXT("Already playing"));
 if(!IsValid(C)||!C->IsLocalController()||!C->GetPawn()||C->IsPaused())return Fail(TEXT("Player is not ready"));
 if(!IsValid(TargetCamera)||(!bViewOnly&&!IsValid(EventActor)))return Fail(TEXT("Assign Target Camera and either View Only + View Event ID, or Event Actor"));
 if(!SubtitleLines.IsEmpty()){bool HasText=false;for(const auto& Line:SubtitleLines)HasText|=!Line.Resolve(this).IsEmptyOrWhitespace();if(!HasText||!SubtitleWidgetClass)return Fail(TEXT("Subtitle Lines needs a non-empty line and a Subtitle Widget Class"));}
 if(TargetCamera->GetWorld()!=GetWorld()||(!bViewOnly&&EventActor->GetWorld()!=GetWorld()))return Fail(TEXT("Targets must be in the same world"));
 auto* E=bViewOnly?ViewEvent.Get():EventActor->FindComponentByClass<UDWWorldEventComponent>();if(!E||!E->IsReady())return Fail(TEXT("Event is completed, running, or has an invalid/duplicate ID"));
 auto* GI=GetWorld()->GetGameInstance<UDWGameInstance>();
 if(GI&&(GI->IsPlayerRestorePending()||GI->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive()))return Fail(TEXT("Wait for loading/restoration"));
 if(auto* H=Cast<ADWGameplayHUD>(C->GetHUD());H&&H->IsBlockingGameplay())return Fail(TEXT("Close gameplay menus before starting"));
 if(auto* P=Cast<ADWPlayerCharacter>(C->GetPawn());P&&P->IsDead())return Fail(TEXT("Player is dead"));
 auto* S=GetWorld()->GetSubsystem<UDWWorldEventSubsystem>();if(!S->Acquire(this))return Fail(TEXT("Another cinematic is playing"));
 TRACE_BOOKMARK(TEXT("DW Cinematic accepted owner=%s target=%s"),*GetNameSafe(GetOwner()),*GetNameSafe(TargetCamera));
#if WITH_EDITOR
 BeginPIEEntryFrameDiagnostics(TEXT("TriggerEnter"));
#endif

#if WITH_EDITOR
 const bool bPIEEntry=GetWorld()->WorldType==EWorldType::PIE;
 const double PIEEntryStarted=FPlatformTime::Seconds();double PIEPrevious=PIEEntryStarted;
 FString PIEEntrySteps;
 const auto MarkPIEEntry=[&](const TCHAR* Step)
 {if(bPIEEntry){const double Now=FPlatformTime::Seconds();PIEEntrySteps+=FString::Printf(TEXT(" %s=%.3f"),Step,(Now-PIEPrevious)*1000.);PIEPrevious=Now;}};
 ON_SCOPE_EXIT
 {if(bPIEEntry)UE_LOG(LogTemp,Display,TEXT("DW_PIE_CINEMATIC_ENTER owner=%s event=%s cpu_ms=%.3f%s"),*GetNameSafe(GetOwner()),*ViewEventId.ToString(),(FPlatformTime::Seconds()-PIEEntryStarted)*1000.,*PIEEntrySteps);};
#endif

 PC=C;Pawn=C->GetPawn();PreviousViewTarget=C->GetViewTarget();Event=E;
 if(auto* G=Pawn->FindComponentByClass<UDWGameplayCameraShakeComponent>())G->StopGameplayShake(true);
 PreviousViewTarget->CalcCamera(0,OriginalPOV);TargetCamera->CalcCamera(0,TargetPOV);
#if WITH_EDITOR
 MarkPIEEntry(TEXT("POVs"));
#endif

 TRACE_BOOKMARK(TEXT("DW Cinematic POV from=(%.0f,%.0f,%.0f) fov=%.2f pp=%.2f proj=%d to=(%.0f,%.0f,%.0f) fov=%.2f"),OriginalPOV.Location.X,OriginalPOV.Location.Y,OriginalPOV.Location.Z,OriginalPOV.FOV,OriginalPOV.PostProcessBlendWeight,int32(OriginalPOV.ProjectionMode),TargetPOV.Location.X,TargetPOV.Location.Y,TargetPOV.Location.Z,TargetPOV.FOV);
 int32 Width=0,Height=0;C->GetViewportSize(Width,Height);InitialAspect=OriginalPOV.bConstrainAspectRatio?OriginalPOV.AspectRatio:(Height>0?float(Width)/Height:OriginalPOV.AspectRatio);
 FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 PlaybackCamera=GetWorld()->SpawnActor<ACameraActor>(OriginalPOV.Location,OriginalPOV.Rotation,P);
 if(!PlaybackCamera){S->Release(this);return Fail(TEXT("Unable to create playback camera"));}
#if WITH_EDITOR
 MarkPIEEntry(TEXT("SpawnCamera"));
#endif

 auto* Cam=PlaybackCamera->GetCameraComponent();Cam->ProjectionMode=OriginalPOV.ProjectionMode;Cam->OrthoWidth=OriginalPOV.OrthoWidth;Cam->PostProcessSettings=OriginalPOV.PostProcessSettings;Cam->PostProcessBlendWeight=OriginalPOV.PostProcessBlendWeight;Cam->SetFieldOfView(OriginalPOV.FOV);Cam->SetAspectRatio(InitialAspect);Cam->SetConstraintAspectRatio(true);
 bSavedCursor=C->bShowMouseCursor;C->bShowMouseCursor=false;bSavedDamage=Pawn->CanBeDamaged();Pawn->SetCanBeDamaged(false);
 C->SetIgnoreMoveInput(true);C->SetIgnoreLookInput(true);bInputLocked=true;
 if(auto* Ch=Cast<ACharacter>(Pawn.Get())){auto* M=Ch->GetCharacterMovement();PreviousMovementMode=M->MovementMode;PreviousCustomMode=M->CustomMovementMode;M->StopMovementImmediately();M->DisableMovement();bMovementLocked=true;}
 if(auto* Hero=Cast<ADWPlayerCharacter>(Pawn.Get()))Hero->ClearHeldActions();
#if WITH_EDITOR
 MarkPIEEntry(TEXT("InputAndClearActions"));
#endif

 UI.Reset();for(auto* U:UDWUIOffscreenComponent::FindForPlayer(C)){UI.Add(U);U->HideForCinematic();}
#if WITH_EDITOR
 MarkPIEEntry(TEXT("HideUI"));
#endif

 E->OnCompleted.AddUniqueDynamic(this,&ThisClass::SceneCompleted);
 bSubtitleDismissed=false;bSkipPressHeld=C->IsInputKeyDown(EKeys::LeftMouseButton);
 SubtitleInput=NewObject<UInputComponent>(this,NAME_None,RF_Transient);SubtitleInput->RegisterComponent();
 SubtitleInput->Priority=10000;SubtitleInput->bBlockInput=false;
 auto& Press=SubtitleInput->BindKey(EKeys::LeftMouseButton,IE_Pressed,this,&ThisClass::SubtitlePressed);Press.bConsumeInput=true;Press.bExecuteWhenPaused=true;
 auto& Release=SubtitleInput->BindKey(EKeys::LeftMouseButton,IE_Released,this,&ThisClass::SubtitleReleased);Release.bConsumeInput=true;Release.bExecuteWhenPaused=true;
 C->PushInputComponent(SubtitleInput);
#if WITH_EDITOR
 MarkPIEEntry(TEXT("InputComponent"));
#endif

 C->SetViewTarget(PlaybackCamera);SetPhase(EDWCinematicPhase::TravelOut);SetComponentTickEnabled(true);++PlaybackCount;if(ShakeStart==EDWCinematicShakeStart::AtStart)StartCinematicShake();return true;
}
void UDWCinematicComponent::SubtitlePressed()
{
 if(bSkipPressHeld)return;
 bSkipPressHeld=true;SkipSubtitleLine();
}
void UDWCinematicComponent::SubtitleReleased(){bSkipPressHeld=false;}
bool UDWCinematicComponent::SkipSubtitleLine()
{
 if(!bAllowClickToNextLine||bSubtitleDismissed||!PC.IsValid()||PC->IsPaused()||!Subtitle||
    (Phase!=EDWCinematicPhase::SceneEvent&&Phase!=EDWCinematicPhase::Hold))return false;
 if(auto* H=Cast<ADWGameplayHUD>(PC->GetHUD());H&&H->IsBlockingGameplay())return false;
 if(Dialogue){
  if(Dialogue->IsPaused())return false;
  if(Dialogue->IsPlaying()){
   if(!Dialogue->SkipDialogueLine())return false;
   if(!IsPlaying()||!Subtitle||!Dialogue)return true;
   if(Dialogue->IsPlaying())return true;
   if(!Dialogue->bCompleted)return false;
  }
 }else{
  const FText Text=DWText(this,*ChineseSubtitle.ToString(),*EnglishSubtitle.ToString());
  if(Text.IsEmptyOrWhitespace())return false;
  if(auto* R=UDWTextRevealLibrary::GetTextRevealComponent(Subtitle->GetDialogueTextBlock()))R->Stop(false);
 }
 bSubtitleDismissed=true;Subtitle->SetVisibility(ESlateVisibility::Collapsed);
 return true;
}
void UDWCinematicComponent::UpdateCamera(float T,bool Back)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(DW_Cinematic_UpdateCamera);
 if(!PlaybackCamera)return;
 const float A=FMath::InterpEaseInOut(0.f,1.f,FMath::Clamp(T,0.f,1.f),FMath::Clamp(EaseExponent,1.f,8.f));
 const auto& From=Back?TargetPOV:OriginalPOV;const auto& To=Back?OriginalPOV:TargetPOV;
 ApplyAuthoredCameraPose(PlaybackCamera,FMath::Lerp(From.Location,To.Location,A),FQuat::Slerp(From.Rotation.Quaternion(),To.Rotation.Quaternion(),A),FMath::Lerp(From.FOV,To.FOV,A));
 auto* Cam=PlaybackCamera->GetCameraComponent();
 const float B=FMath::SmoothStep(0.f,1.f,PhaseSeconds/FMath::Max(.001f,AspectTransitionSeconds));
 const float Movie=FMath::Clamp(MovieAspectRatio,.5f,4.f);Cam->SetAspectRatio(Back?FMath::Lerp(Movie,InitialAspect,B):FMath::Lerp(InitialAspect,Movie,B));
}
void UDWCinematicComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)
{
 TRACE_CPUPROFILER_EVENT_SCOPE(DW_Cinematic_Tick);
 Super::TickComponent(Dt,Type,F);if(!IsPlaying()||Phase==EDWCinematicPhase::External)return;
#if WITH_EDITOR
 AdvancePIEEntryFrameDiagnostics();
#endif

 if(ActiveCinematicShake&&(!ActiveCinematicShake->IsActive()||ActiveCinematicShake->IsFinished()))ActiveCinematicShake=nullptr;
 if(!PC.IsValid()||!Pawn.IsValid()||!PlaybackCamera||!Event.IsValid()||!PreviousViewTarget.IsValid()){LastError=TEXT("A cinematic participant was removed");Cleanup(false);return;}
 if(Dialogue&&!Dialogue->IsPlaying()&&!Dialogue->bCompleted){LastError=Dialogue->LastError;Cleanup(false);return;}
 PhaseSeconds+=Dt;
 switch(Phase)
 {
 case EDWCinematicPhase::TravelOut:
  UpdateCamera(PhaseSeconds/FMath::Max(.001f,TravelOutSeconds),false);
  if(PhaseSeconds>=FMath::Max(TravelOutSeconds,AspectTransitionSeconds)){
   SetPhase(EDWCinematicPhase::SceneEvent);
   const FText Text=DWText(this,*ChineseSubtitle.ToString(),*EnglishSubtitle.ToString());
   if(!SubtitleLines.IsEmpty()){
    Subtitle=CreateAuthoredSubtitle(PC.Get(),100);
    if(!Subtitle){LastError=TEXT("Unable to create dialogue widget");Cleanup(false);break;}
    if(auto* R=UDWTextRevealLibrary::GetTextRevealComponent(Subtitle->GetDialogueTextBlock())){if(VoiceProfile)R->VoiceProfile=VoiceProfile;}
    Dialogue=CreateAuthoredDialogue();if(!Dialogue){LastError=TEXT("Unable to create dialogue queue");Cleanup(false);break;}Dialogue->Lines=SubtitleLines;
    if(!Dialogue->PlayDialogue(Subtitle->GetDialogueTextBlock())){LastError=Dialogue->LastError;Cleanup(false);break;}
   }else if(!Text.IsEmpty()&&SubtitleWidgetClass){Subtitle=CreateWidget<UDWCinematicSubtitleWidget>(PC.Get(),SubtitleWidgetClass);if(Subtitle){Subtitle->AddToViewport(100);Subtitle->ShowCue(Text,VoiceProfile);}}
   if(!Event->RequestEvent()){LastError=TEXT("Scene event could not start");Cleanup(false);break;}
   if(ShakeStart==EDWCinematicShakeStart::AtTarget)StartCinematicShake();
   OnTargetReached.Broadcast();
   if(bViewOnly&&IsPlaying())SetPhase(EDWCinematicPhase::Hold);
  }break;
 case EDWCinematicPhase::SceneEvent:
  if(PhaseSeconds>FMath::Max(1.f,EventTimeoutSeconds)){LastError=TEXT("Scene event timed out: call Complete Event when its animation finishes");Cleanup(false);}break;
 case EDWCinematicPhase::Hold:
  if(Dialogue&&Dialogue->IsPlaying()){PhaseSeconds=0;break;}
  if(bSubtitleDismissed||PhaseSeconds>=FMath::Max(0.f,HoldAfterEventSeconds)){if(Subtitle){Subtitle->RemoveFromParent();Subtitle=nullptr;}SetPhase(EDWCinematicPhase::TravelBack);}break;
 case EDWCinematicPhase::TravelBack:
  UpdateCamera(PhaseSeconds/FMath::Max(.001f,TravelBackSeconds),true);
  if(PhaseSeconds>=FMath::Max(TravelBackSeconds,AspectTransitionSeconds)){
   StopCinematicShake(true);PC->SetViewTarget(PreviousViewTarget.Get());UIReturnDuration=0;
   for(auto U:UI)if(U.IsValid()){U->ReturnToScreen();UIReturnDuration=FMath::Max(UIReturnDuration,U->ReturnSeconds);}
   SetPhase(EDWCinematicPhase::UIReturn);
  }break;
 case EDWCinematicPhase::UIReturn:if(PhaseSeconds>=UIReturnDuration)Cleanup(true);break;
 default:break;
 }
}
void UDWCinematicComponent::SceneCompleted(){if(Phase==EDWCinematicPhase::SceneEvent)SetPhase(EDWCinematicPhase::Hold);}
void UDWCinematicComponent::CancelCinematic()
{
 if(Phase==EDWCinematicPhase::External){
  if(bExternalCancelInFlight)return;
  TGuardValue<bool> Guard(bExternalCancelInFlight,true);LastError=TEXT("Cancelled");
  OnExternalCancelRequested.Broadcast();if(bExternalControlOwned){EndExternalControl(false);if(!bExternalDeferToDirector)FlushExternalCompletion();}return;
 }
 if(IsPlaying()){LastError=TEXT("Cancelled");Cleanup(false);}
}
void UDWCinematicComponent::Cleanup(bool Success)
{
#if WITH_EDITOR
 FinishPIEEntryFrameDiagnostics(TEXT("Cleanup"));
#endif

 TRACE_CPUPROFILER_EVENT_SCOPE(DW_Cinematic_Cleanup);
 if(SubtitleInput){if(PC.IsValid())PC->PopInputComponent(SubtitleInput);SubtitleInput->DestroyComponent();SubtitleInput=nullptr;}
 bSkipPressHeld=false;bSubtitleDismissed=false;
 StopCinematicShake(true);
 if(Event.IsValid()){Event->OnCompleted.RemoveDynamic(this,&ThisClass::SceneCompleted);if(!Success)Event->CancelEvent();else if(Event.Get()==ViewEvent.Get())Event->CompleteEvent();}
 for(auto U:UI)if(U.IsValid())U->RestoreImmediately();UI.Reset();
 if(Dialogue){Dialogue->StopDialogue();Dialogue->DestroyComponent();Dialogue=nullptr;}
 if(Subtitle){Subtitle->RemoveFromParent();Subtitle=nullptr;}
 if(PC.IsValid()){
  if(PreviousViewTarget.IsValid())PC->SetViewTarget(PreviousViewTarget.Get());else if(Pawn.IsValid())PC->SetViewTarget(Pawn.Get());
  if(bInputLocked){PC->SetIgnoreMoveInput(false);PC->SetIgnoreLookInput(false);}PC->bShowMouseCursor=bSavedCursor;
 }
 if(Pawn.IsValid()){
  Pawn->SetCanBeDamaged(bSavedDamage);
  if(bMovementLocked)if(auto* Ch=Cast<ACharacter>(Pawn.Get()))Ch->GetCharacterMovement()->SetMovementMode(EMovementMode(PreviousMovementMode),PreviousCustomMode);
 }
 bInputLocked=bMovementLocked=false;
 if(PlaybackCamera){PlaybackCamera->Destroy();PlaybackCamera=nullptr;}
 if(auto* W=GetWorld())W->GetSubsystem<UDWWorldEventSubsystem>()->Release(this);
 SetPhase(EDWCinematicPhase::Idle);SetComponentTickEnabled(false);Event.Reset();OnFinished.Broadcast(Success);
}
void UDWCinematicComponent::EndPlay(const EEndPlayReason::Type R){EndExternalControl(false);FlushExternalCompletion();if(IsPlaying())Cleanup(false);if(ViewEvent){ViewEvent->DestroyComponent();ViewEvent=nullptr;}Super::EndPlay(R);}
ADWCinematicTrigger::ADWCinematicTrigger()
{
 TriggerBox=CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));RootComponent=TriggerBox;TriggerBox->SetBoxExtent(FVector(160,300,150));TriggerBox->SetCollisionProfileName(TEXT("Trigger"));TriggerBox->SetGenerateOverlapEvents(true);
 Cinematic=CreateDefaultSubobject<UDWCinematicComponent>(TEXT("Cinematic"));
}
void ADWCinematicTrigger::BeginPlay(){Super::BeginPlay();TriggerBox->OnComponentBeginOverlap.AddUniqueDynamic(this,&ThisClass::Enter);}
void ADWCinematicTrigger::Enter(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,int32,bool,const FHitResult&)
{if(auto* P=Cast<APawn>(Other))if(auto* C=Cast<APlayerController>(P->GetController()))Cinematic->PlayCinematic(C);}
#if WITH_EDITOR
void UDWCinematicComponent::BeginPIEEntryFrameDiagnostics(const TCHAR* Label)
{
 if(!GetWorld()||GetWorld()->WorldType!=EWorldType::PIE)return;
 FinishPIEEntryFrameDiagnostics(TEXT("NextEntry"));
 PIEEntryFrameLabel=Label;PIEEntryFrameWindow.Begin(FPlatformTime::Seconds(),GFrameCounter);bPIEEntryReportPending=true;
 PIEEntryAssets=FAssetCompilingManager::Get().GetNumRemainingAssets();
 PIEEntryShaders=GShaderCompilingManager?GShaderCompilingManager->GetNumRemainingJobs():0;
 PIEEntryPSOs=PipelineStateCache::GetNumActivePipelinePrecompileTasks();
}
void UDWCinematicComponent::AdvancePIEEntryFrameDiagnostics()
{
 if(!PIEEntryFrameWindow.bActive)return;
 if(!GetWorld()||GetWorld()->WorldType!=EWorldType::PIE||GetWorld()->IsPaused()){FinishPIEEntryFrameDiagnostics(TEXT("PausedOrInvalidWorld"));return;}
 if(PIEEntryFrameWindow.Sample(FPlatformTime::Seconds(),GFrameCounter))FinishPIEEntryFrameDiagnostics(TEXT("BoundReached"));
}
void UDWCinematicComponent::FinishPIEEntryFrameDiagnostics(const TCHAR* Reason)
{
 if(!bPIEEntryReportPending)return;
 bPIEEntryReportPending=false;PIEEntryFrameWindow.bActive=false;
 int32 Width=0,Height=0;if(auto* C=GetWorld()?GetWorld()->GetFirstPlayerController():nullptr)C->GetViewportSize(Width,Height);
 const auto* SP=IConsoleManager::Get().FindConsoleVariable(TEXT("r.ScreenPercentage"));
 FString First;for(int32 I=0;I<FMath::Min(PIEEntryFrameWindow.Samples,8);++I)First+=FString::Printf(TEXT(" %.2f"),PIEEntryFrameWindow.FirstGapMs[I]);
 UE_LOG(LogTemp,Display,TEXT("DW_PIE_CINEMATIC_FRAME_WINDOW owner=%s label=%s reason=%s samples=%d elapsed_ms=%.2f max_gap_ms=%.2f gaps_over_33ms=%d invalid_clock=%d first_gaps_ms=[%s] viewport=%dx%d raw_screenpercentage=%.2f pending_assets=%d/%d pending_shaders=%d/%d pending_nativepso=%d/%d"),
 *GetNameSafe(GetOwner()),*PIEEntryFrameLabel,Reason,PIEEntryFrameWindow.Samples,(PIEEntryFrameWindow.PreviousAt-PIEEntryFrameWindow.StartedAt)*1000.,PIEEntryFrameWindow.MaxGapMs,PIEEntryFrameWindow.Hitches,PIEEntryFrameWindow.bInvalidClock,*First,Width,Height,SP?SP->GetFloat():-999.f,
 PIEEntryAssets,FAssetCompilingManager::Get().GetNumRemainingAssets(),PIEEntryShaders,GShaderCompilingManager?GShaderCompilingManager->GetNumRemainingJobs():0,PIEEntryPSOs,PipelineStateCache::GetNumActivePipelinePrecompileTasks());
}
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPIECinematicEntryWindowTest,"DoughWorld.Cinematic.PIEEntryFrameWindow",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPIECinematicEntryWindowTest::RunTest(const FString&)
{
 FDWPIEEntryFrameWindow W;W.Begin(100.,10);
 TestFalse(TEXT("Same engine frame is not sampled twice"),W.Sample(100.001,10));TestEqual(TEXT("Duplicate frame count stays zero"),W.Samples,0);
 TestFalse(TEXT("First later frame remains inside window"),W.Sample(100.016,11));TestEqual(TEXT("One frame recorded"),W.Samples,1);
 TestTrue(TEXT("First frame gap retained"),FMath::IsNearlyEqual(W.FirstGapMs[0],16.,.001));
 W.Sample(100.070,12);TestEqual(TEXT("Large frame gap counted"),W.Hitches,1);
 TestTrue(TEXT("Two-second wall-clock bound ends"),W.Sample(102.,13));TestFalse(TEXT("Bound stops sampling"),W.bActive);
 TestFalse(TEXT("Completed window stays stopped"),W.Sample(103.,14));TestEqual(TEXT("Completed count stable"),W.Samples,3);
 W.Begin(200.,20);TestEqual(TEXT("New entry resets count"),W.Samples,0);TestEqual(TEXT("New entry resets maximum"),W.MaxGapMs,0.);
 for(int32 I=1;I<120;++I)TestFalse(TEXT("Fast frames stay below sample cap"),W.Sample(200.+I*.001,20+I));
 TestTrue(TEXT("120th sample ends even at high FPS"),W.Sample(200.120,140));TestEqual(TEXT("Hard sample cap"),W.Samples,120);
 W.Begin(300.,30);TestTrue(TEXT("Nonfinite sample clock stops"),W.Sample(std::numeric_limits<double>::quiet_NaN(),31));TestTrue(TEXT("Bad clock identified"),W.bInvalidClock);
 W.Begin(400.,40);TestTrue(TEXT("Clock rollback stops"),W.Sample(399.,41));
 return true;
}
#endif
#endif

