#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "DWDialogueSequence.h"
#include "DWGameplayCinematic.generated.h"
class UCameraShakeBase;
class UInputComponent;
class ADWPlayerController;
class UBoxComponent;
class ACameraActor;
class UDWWorldEventComponent;
class UDWWorldEventSubsystem;
class UDWUIOffscreenComponent;
class UTextBlock;
class UDWTextVoiceProfile;
#if WITH_EDITOR
struct FDWPIEEntryFrameWindow
{
 bool bActive=false,bInvalidClock=false;
 double StartedAt=0,PreviousAt=0,MaxGapMs=0,FirstGapMs[8]={};
 uint64 PreviousFrame=0;
 int32 Samples=0,Hitches=0;
 void Begin(double Now,uint64 Frame)
 {*this=FDWPIEEntryFrameWindow();if(FMath::IsFinite(Now)){StartedAt=PreviousAt=Now;PreviousFrame=Frame;bActive=true;}}
 bool Sample(double Now,uint64 Frame)
 {
  if(!bActive||Frame==PreviousFrame)return false;
  if(!FMath::IsFinite(Now)||Now<PreviousAt){bInvalidClock=true;bActive=false;return true;}
  const double GapMs=(Now-PreviousAt)*1000.;PreviousAt=Now;PreviousFrame=Frame;
  if(Samples<8)FirstGapMs[Samples]=GapMs;
  ++Samples;MaxGapMs=FMath::Max(MaxGapMs,GapMs);if(GapMs>33.334)++Hitches;
  if(Samples>=120||Now-StartedAt>=2.){bActive=false;return true;}return false;
 }
};
#endif

UENUM(BlueprintType)
enum class EDWCinematicPhase:uint8 { Idle,TravelOut,SceneEvent,Hold,TravelBack,UIReturn,External };
UENUM(BlueprintType)
enum class EDWCinematicShakeStart:uint8 { AtTarget,AtStart,Manual };
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWCinematicTargetReached);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWCinematicExternalCancelRequested);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWCinematicFinished,bool,bSuccessful);

UCLASS(Blueprintable)
class GDATTEST_API UDWCinematicSubtitleWidget:public UUserWidget
{
 GENERATED_BODY()
public:
 void ShowCue(FText Text,UDWTextVoiceProfile* Voice);
 UFUNCTION(BlueprintPure,Category="Dialogue") UTextBlock* GetDialogueTextBlock()const{return SubtitleText;}
protected:
 UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> SubtitleText;
};

/** Add to a trigger actor. The event actor owns the permanent change; this component owns presentation. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,Blueprintable,meta=(BlueprintSpawnableComponent,DisplayName="DW Gameplay Cinematic"))
class GDATTEST_API UDWCinematicComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UDWCinematicComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera Shake") bool bCinematicShakeEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera Shake") TSubclassOf<UCameraShakeBase> CinematicShakeClass;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera Shake",meta=(ClampMin="0",UIMax="2")) float CinematicShakeScale=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera Shake") EDWCinematicShakeStart ShakeStart=EDWCinematicShakeStart::AtTarget;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera Shake",meta=(ToolTip="Fade out as return travel begins. Always force-stop before the player camera is restored.")) bool bStopShakeWhenReturning=true;
 UFUNCTION(BlueprintCallable,Category="Cinematic|Camera Shake") void StartCinematicShake();
 UFUNCTION(BlueprintCallable,Category="Cinematic|Camera Shake") void StopCinematicShake(bool bImmediately=true);
 UFUNCTION(BlueprintPure,Category="Cinematic|Diagnostics") UCameraShakeBase* GetActiveCinematicShake()const{return ActiveCinematicShake;}
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Targets") TObjectPtr<AActor> TargetCamera;
 /** View-only introductions commit their own saved ID after the camera and UI return. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Targets") bool bViewOnly=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Targets",meta=(EditCondition="bViewOnly")) FName ViewEventId;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Targets",meta=(EditCondition="!bViewOnly")) TObjectPtr<AActor> EventActor;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="0",Units="s")) float TravelOutSeconds=1.5f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="0",Units="s")) float TravelBackSeconds=1.2f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="1",ClampMax="8")) float EaseExponent=2.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin=".5",ClampMax="4")) float MovieAspectRatio=2.39f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="0",Units="s")) float AspectTransitionSeconds=.6f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Timing",meta=(ClampMin="0",Units="s")) float HoldAfterEventSeconds=1.5f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Timing",meta=(ClampMin="1",Units="s")) float EventTimeoutSeconds=30.f;
 /** Non-empty list takes priority over the legacy single subtitle. All lines share the WBP voice unless Voice Profile overrides it. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") TArray<FDWDialogueLine> SubtitleLines;
 /** Left click skips directly to the next line. The final click skips the extra hold, but waits for the world event before returning. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") bool bAllowClickToNextLine=true;
 UFUNCTION(BlueprintCallable,Category="Cinematic|Subtitle") bool SkipSubtitleLine();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") FText ChineseSubtitle;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") FText EnglishSubtitle;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") TSubclassOf<UDWCinematicSubtitleWidget> SubtitleWidgetClass;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") TObjectPtr<UDWTextVoiceProfile> VoiceProfile;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Cinematic|Diagnostics") EDWCinematicPhase Phase=EDWCinematicPhase::Idle;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Cinematic|Diagnostics") FString LastError;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Cinematic|Diagnostics") int32 PlaybackCount=0;
 UPROPERTY(BlueprintAssignable,Category="Cinematic") FDWCinematicFinished OnFinished;
 UPROPERTY() FDWCinematicExternalCancelRequested OnExternalCancelRequested;
 UPROPERTY(BlueprintAssignable,Category="Cinematic") FDWCinematicTargetReached OnTargetReached;
 /** Shared cinematic controls for an externally authored sequence. No automatic target/event/shot timer is started. */
 bool BeginExternalControl(ADWPlayerController* Controller,APawn* ControlledPawn);
 void EndExternalControl(bool bSuccessful=true);
 void ReleaseExternalResources();
 void FlushExternalCompletion();
 void DeferExternalCompletionUntilDirectorTickExit(){bExternalDeferToDirector=true;}
 void RequestExternalAspect(ACameraActor* Camera,float TargetAspect);
 void AdvanceExternalAspect(ACameraActor* Camera,float DeltaSeconds);
 void ApplyAuthoredCameraPose(ACameraActor* Camera,const FVector& Location,const FQuat& Rotation,float FieldOfView);
#if WITH_EDITOR
 void BeginPIEEntryFrameDiagnostics(const TCHAR* Label);
 void AdvancePIEEntryFrameDiagnostics();
 void FinishPIEEntryFrameDiagnostics(const TCHAR* Reason);
#endif

 UDWCinematicSubtitleWidget* CreateAuthoredSubtitle(APlayerController* Controller,int32 ZOrder);
 UDWDialogueSequenceComponent* CreateAuthoredDialogue();
 UFUNCTION(BlueprintCallable,Category="Cinematic") bool PlayCinematic(APlayerController* PlayerController);
 UFUNCTION(BlueprintCallable,Category="Cinematic") void CancelCinematic();
 UFUNCTION(BlueprintPure,Category="Cinematic") bool IsPlaying()const{return Phase!=EDWCinematicPhase::Idle;}
 UFUNCTION(BlueprintPure,Category="Cinematic") ACameraActor* GetPlaybackCamera()const{return PlaybackCamera;}
 UFUNCTION(BlueprintPure,Category="Cinematic") UDWDialogueSequenceComponent* GetDialoguePlayer()const{return Dialogue;}
 virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
protected:
 virtual void BeginPlay()override;
 virtual void EndPlay(const EEndPlayReason::Type R)override;
private:
#if WITH_EDITOR
 FDWPIEEntryFrameWindow PIEEntryFrameWindow;
 FString PIEEntryFrameLabel;
 bool bPIEEntryReportPending=false;
 int32 PIEEntryAssets=0,PIEEntryShaders=0,PIEEntryPSOs=0;
#endif

 UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveCinematicShake;
 UPROPERTY(Transient) TObjectPtr<UDWWorldEventComponent> ViewEvent;
 UPROPERTY(Transient) TObjectPtr<ACameraActor> PlaybackCamera;
 UPROPERTY(Transient) TObjectPtr<UDWCinematicSubtitleWidget> Subtitle;
 UPROPERTY(Transient) TObjectPtr<UDWDialogueSequenceComponent> Dialogue;
 UPROPERTY(Transient) TObjectPtr<UInputComponent> SubtitleInput;
 TWeakObjectPtr<ADWPlayerController> ExternalController;
 TWeakObjectPtr<UDWWorldEventSubsystem> ExternalEvents;
 bool bExternalControlOwned=false,bExternalCursor=false,bExternalCancelInFlight=false;
 bool bExternalCompletionPending=false,bExternalCompletionSuccessful=true,bExternalDeferToDirector=false,bExternalResourcesReleased=false;
 float ExternalAspectFrom=1.777778f,ExternalAspectTo=1.777778f,ExternalAspectAge=0.f;
 bool bSkipPressHeld=false,bSubtitleDismissed=false;
 void SubtitlePressed();
 void SubtitleReleased();
 TWeakObjectPtr<APlayerController> PC;
 TWeakObjectPtr<APawn> Pawn;
 TWeakObjectPtr<AActor> PreviousViewTarget;
 TWeakObjectPtr<UDWWorldEventComponent> Event;
 TArray<TWeakObjectPtr<UDWUIOffscreenComponent>> UI;
 FMinimalViewInfo OriginalPOV,TargetPOV;
 float PhaseSeconds=0,InitialAspect=1.777778f,UIReturnDuration=0;
 uint8 PreviousMovementMode=0,PreviousCustomMode=0;
 bool bSavedCursor=false,bSavedDamage=true,bInputLocked=false,bMovementLocked=false;
 void SetPhase(EDWCinematicPhase P);
 void UpdateCamera(float T,bool bReturning);
 void Cleanup(bool bSuccessful);
 UFUNCTION() void SceneCompleted();
};

UCLASS(Blueprintable)
class GDATTEST_API ADWCinematicTrigger:public AActor
{
 GENERATED_BODY()
public:
 ADWCinematicTrigger();
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cinematic") TObjectPtr<UBoxComponent> TriggerBox;
 UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Cinematic") TObjectPtr<UDWCinematicComponent> Cinematic;
protected:
 virtual void BeginPlay()override;
 UFUNCTION() void Enter(UPrimitiveComponent* Overlapped,AActor* Other,UPrimitiveComponent* OtherComp,int32 BodyIndex,bool bSweep,const FHitResult& Hit);
};
