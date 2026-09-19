#include "DWGameplayCinematic.h"
#include "DWWorldEvent.h"
#include "DWUIOffscreenComponent.h"
#include "DWTextRevealComponent.h"
#include "DWLocalizationLibrary.h"
#include "DWGameInstance.h"
#include "DWLoadingTransition.h"
#include "DWGameplayHUD.h"
#include "DWPlayerCharacter.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/TextBlock.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"

void UDWCinematicSubtitleWidget::ShowCue(FText Text,UDWTextVoiceProfile* Voice)
{if(!SubtitleText)return;if(auto* R=UDWTextRevealLibrary::GetTextRevealComponent(SubtitleText)){if(Voice)R->VoiceProfile=Voice;R->PlayText(Text);}else SubtitleText->SetText(Text);}
UDWCinematicComponent::UDWCinematicComponent(){PrimaryComponentTick.bCanEverTick=true;PrimaryComponentTick.bStartWithTickEnabled=false;}
void UDWCinematicComponent::SetPhase(EDWCinematicPhase P){Phase=P;PhaseSeconds=0;}
bool UDWCinematicComponent::PlayCinematic(APlayerController* C)
{
 LastError.Empty();
 auto Fail=[this](const TCHAR* M){LastError=M;return false;};
 if(IsPlaying())return Fail(TEXT("Already playing"));
 if(!IsValid(C)||!C->IsLocalController()||!C->GetPawn()||C->IsPaused())return Fail(TEXT("Player is not ready"));
 if(!IsValid(TargetCamera)||!IsValid(EventActor))return Fail(TEXT("Assign Target Camera and Event Actor"));
 if(!SubtitleLines.IsEmpty()){bool HasText=false;for(const auto& Line:SubtitleLines)HasText|=!Line.Resolve(this).IsEmptyOrWhitespace();if(!HasText||!SubtitleWidgetClass)return Fail(TEXT("Subtitle Lines needs a non-empty line and a Subtitle Widget Class"));}
 if(TargetCamera->GetWorld()!=GetWorld()||EventActor->GetWorld()!=GetWorld())return Fail(TEXT("Targets must be in the same world"));
 auto* E=EventActor->FindComponentByClass<UDWWorldEventComponent>();if(!E||!E->IsReady())return Fail(TEXT("Event is completed, running, or has an invalid/duplicate ID"));
 auto* GI=GetWorld()->GetGameInstance<UDWGameInstance>();
 if(GI&&(GI->IsPlayerRestorePending()||GI->GetSubsystem<UDWLoadingTransitionSubsystem>()->IsTransitionActive()))return Fail(TEXT("Wait for loading/restoration"));
 if(auto* H=Cast<ADWGameplayHUD>(C->GetHUD());H&&H->IsBlockingGameplay())return Fail(TEXT("Close gameplay menus before starting"));
 if(auto* P=Cast<ADWPlayerCharacter>(C->GetPawn());P&&P->IsDead())return Fail(TEXT("Player is dead"));
 auto* S=GetWorld()->GetSubsystem<UDWWorldEventSubsystem>();if(!S->Acquire(this))return Fail(TEXT("Another cinematic is playing"));
 PC=C;Pawn=C->GetPawn();PreviousViewTarget=C->GetViewTarget();Event=E;
 OriginalPOV=C->PlayerCameraManager->GetCameraCacheView();TargetCamera->CalcCamera(0,TargetPOV);
 int32 Width=0,Height=0;C->GetViewportSize(Width,Height);InitialAspect=OriginalPOV.bConstrainAspectRatio?OriginalPOV.AspectRatio:(Height>0?float(Width)/Height:OriginalPOV.AspectRatio);
 FActorSpawnParameters P;P.ObjectFlags|=RF_Transient;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 PlaybackCamera=GetWorld()->SpawnActor<ACameraActor>(OriginalPOV.Location,OriginalPOV.Rotation,P);
 if(!PlaybackCamera){S->Release(this);return Fail(TEXT("Unable to create playback camera"));}
 auto* Cam=PlaybackCamera->GetCameraComponent();Cam->ProjectionMode=OriginalPOV.ProjectionMode;Cam->OrthoWidth=OriginalPOV.OrthoWidth;Cam->PostProcessSettings=OriginalPOV.PostProcessSettings;Cam->PostProcessBlendWeight=OriginalPOV.PostProcessBlendWeight;Cam->SetFieldOfView(OriginalPOV.FOV);Cam->SetAspectRatio(InitialAspect);Cam->SetConstraintAspectRatio(true);
 bSavedCursor=C->bShowMouseCursor;C->bShowMouseCursor=false;bSavedDamage=Pawn->CanBeDamaged();Pawn->SetCanBeDamaged(false);
 C->SetIgnoreMoveInput(true);C->SetIgnoreLookInput(true);bInputLocked=true;
 if(auto* Ch=Cast<ACharacter>(Pawn.Get())){auto* M=Ch->GetCharacterMovement();PreviousMovementMode=M->MovementMode;PreviousCustomMode=M->CustomMovementMode;M->StopMovementImmediately();M->DisableMovement();bMovementLocked=true;}
 if(auto* Hero=Cast<ADWPlayerCharacter>(Pawn.Get()))Hero->ClearHeldActions();
 UI.Reset();for(auto* U:UDWUIOffscreenComponent::FindForPlayer(C)){UI.Add(U);U->HideForCinematic();}
 E->OnCompleted.AddUniqueDynamic(this,&ThisClass::SceneCompleted);
 C->SetViewTarget(PlaybackCamera);SetPhase(EDWCinematicPhase::TravelOut);SetComponentTickEnabled(true);++PlaybackCount;return true;
}
void UDWCinematicComponent::UpdateCamera(float T,bool Back)
{
 if(!PlaybackCamera)return;
 const float A=FMath::InterpEaseInOut(0.f,1.f,FMath::Clamp(T,0.f,1.f),FMath::Clamp(EaseExponent,1.f,8.f));
 const auto& From=Back?TargetPOV:OriginalPOV;const auto& To=Back?OriginalPOV:TargetPOV;
 PlaybackCamera->SetActorLocationAndRotation(FMath::Lerp(From.Location,To.Location,A),FQuat::Slerp(From.Rotation.Quaternion(),To.Rotation.Quaternion(),A));
 auto* Cam=PlaybackCamera->GetCameraComponent();Cam->SetFieldOfView(FMath::Lerp(From.FOV,To.FOV,A));
 const float B=FMath::SmoothStep(0.f,1.f,PhaseSeconds/FMath::Max(.001f,AspectTransitionSeconds));
 const float Movie=FMath::Clamp(MovieAspectRatio,.5f,4.f);Cam->SetAspectRatio(Back?FMath::Lerp(Movie,InitialAspect,B):FMath::Lerp(InitialAspect,Movie,B));
}
void UDWCinematicComponent::TickComponent(float Dt,ELevelTick Type,FActorComponentTickFunction* F)
{
 Super::TickComponent(Dt,Type,F);if(!IsPlaying())return;
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
    Subtitle=CreateWidget<UDWCinematicSubtitleWidget>(PC.Get(),SubtitleWidgetClass);
    if(!Subtitle){LastError=TEXT("Unable to create dialogue widget");Cleanup(false);break;}
    Subtitle->AddToViewport(100);
    if(auto* R=UDWTextRevealLibrary::GetTextRevealComponent(Subtitle->GetDialogueTextBlock())){if(VoiceProfile)R->VoiceProfile=VoiceProfile;}
    Dialogue=NewObject<UDWDialogueSequenceComponent>(GetOwner(),NAME_None,RF_Transient);Dialogue->RegisterComponent();Dialogue->Lines=SubtitleLines;
    if(!Dialogue->PlayDialogue(Subtitle->GetDialogueTextBlock())){LastError=Dialogue->LastError;Cleanup(false);break;}
   }else if(!Text.IsEmpty()&&SubtitleWidgetClass){Subtitle=CreateWidget<UDWCinematicSubtitleWidget>(PC.Get(),SubtitleWidgetClass);if(Subtitle){Subtitle->AddToViewport(100);Subtitle->ShowCue(Text,VoiceProfile);}}
   if(!Event->RequestEvent()){LastError=TEXT("Scene event could not start");Cleanup(false);}
  }break;
 case EDWCinematicPhase::SceneEvent:
  if(PhaseSeconds>FMath::Max(1.f,EventTimeoutSeconds)){LastError=TEXT("Scene event timed out: call Complete Event when its animation finishes");Cleanup(false);}break;
 case EDWCinematicPhase::Hold:
  if(Dialogue&&Dialogue->IsPlaying()){PhaseSeconds=0;break;}
  if(PhaseSeconds>=FMath::Max(0.f,HoldAfterEventSeconds)){if(Subtitle){Subtitle->RemoveFromParent();Subtitle=nullptr;}SetPhase(EDWCinematicPhase::TravelBack);}break;
 case EDWCinematicPhase::TravelBack:
  UpdateCamera(PhaseSeconds/FMath::Max(.001f,TravelBackSeconds),true);
  if(PhaseSeconds>=FMath::Max(TravelBackSeconds,AspectTransitionSeconds)){
   PC->SetViewTarget(PreviousViewTarget.Get());UIReturnDuration=0;
   for(auto U:UI)if(U.IsValid()){U->ReturnToScreen();UIReturnDuration=FMath::Max(UIReturnDuration,U->ReturnSeconds);}
   SetPhase(EDWCinematicPhase::UIReturn);
  }break;
 case EDWCinematicPhase::UIReturn:if(PhaseSeconds>=UIReturnDuration)Cleanup(true);break;
 default:break;
 }
}
void UDWCinematicComponent::SceneCompleted(){if(Phase==EDWCinematicPhase::SceneEvent)SetPhase(EDWCinematicPhase::Hold);}
void UDWCinematicComponent::CancelCinematic(){if(IsPlaying()){LastError=TEXT("Cancelled");Cleanup(false);}}
void UDWCinematicComponent::Cleanup(bool Success)
{
 if(Event.IsValid()){Event->OnCompleted.RemoveDynamic(this,&ThisClass::SceneCompleted);if(!Success)Event->CancelEvent();}
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
void UDWCinematicComponent::EndPlay(const EEndPlayReason::Type R){if(IsPlaying())Cleanup(false);Super::EndPlay(R);}
ADWCinematicTrigger::ADWCinematicTrigger()
{
 TriggerBox=CreateDefaultSubobject<UBoxComponent>(TEXT("TriggerBox"));RootComponent=TriggerBox;TriggerBox->SetBoxExtent(FVector(160,300,150));TriggerBox->SetCollisionProfileName(TEXT("Trigger"));TriggerBox->SetGenerateOverlapEvents(true);
 Cinematic=CreateDefaultSubobject<UDWCinematicComponent>(TEXT("Cinematic"));
}
void ADWCinematicTrigger::BeginPlay(){Super::BeginPlay();TriggerBox->OnComponentBeginOverlap.AddUniqueDynamic(this,&ThisClass::Enter);}
void ADWCinematicTrigger::Enter(UPrimitiveComponent*,AActor* Other,UPrimitiveComponent*,int32,bool,const FHitResult&)
{if(auto* P=Cast<APawn>(Other))if(auto* C=Cast<APlayerController>(P->GetController()))Cinematic->PlayCinematic(C);}
