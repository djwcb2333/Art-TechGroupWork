#pragma once
#include "CoreMinimal.h"
#include "Camera/CameraTypes.h"
#include "Components/ActorComponent.h"
#include "GameFramework/Actor.h"
#include "Blueprint/UserWidget.h"
#include "DWDialogueSequence.h"
#include "DWGameplayCinematic.generated.h"
class UBoxComponent;
class ACameraActor;
class UDWWorldEventComponent;
class UDWUIOffscreenComponent;
class UTextBlock;
class UDWTextVoiceProfile;
UENUM(BlueprintType)
enum class EDWCinematicPhase:uint8 { Idle,TravelOut,SceneEvent,Hold,TravelBack,UIReturn };
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
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Targets") TObjectPtr<AActor> TargetCamera;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Targets") TObjectPtr<AActor> EventActor;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="0",Units="s")) float TravelOutSeconds=1.5f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="0",Units="s")) float TravelBackSeconds=1.2f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="1",ClampMax="8")) float EaseExponent=2.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin=".5",ClampMax="4")) float MovieAspectRatio=2.39f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Camera",meta=(ClampMin="0",Units="s")) float AspectTransitionSeconds=.6f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Timing",meta=(ClampMin="0",Units="s")) float HoldAfterEventSeconds=1.5f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Timing",meta=(ClampMin="1",Units="s")) float EventTimeoutSeconds=30.f;
 /** Non-empty list takes priority over the legacy single subtitle. All lines share the WBP voice unless Voice Profile overrides it. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") TArray<FDWDialogueLine> SubtitleLines;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") FText ChineseSubtitle;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") FText EnglishSubtitle;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") TSubclassOf<UDWCinematicSubtitleWidget> SubtitleWidgetClass;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Cinematic|Subtitle") TObjectPtr<UDWTextVoiceProfile> VoiceProfile;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Cinematic|Diagnostics") EDWCinematicPhase Phase=EDWCinematicPhase::Idle;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Cinematic|Diagnostics") FString LastError;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Cinematic|Diagnostics") int32 PlaybackCount=0;
 UPROPERTY(BlueprintAssignable,Category="Cinematic") FDWCinematicFinished OnFinished;
 UFUNCTION(BlueprintCallable,Category="Cinematic") bool PlayCinematic(APlayerController* PlayerController);
 UFUNCTION(BlueprintCallable,Category="Cinematic") void CancelCinematic();
 UFUNCTION(BlueprintPure,Category="Cinematic") bool IsPlaying()const{return Phase!=EDWCinematicPhase::Idle;}
 UFUNCTION(BlueprintPure,Category="Cinematic") ACameraActor* GetPlaybackCamera()const{return PlaybackCamera;}
 UFUNCTION(BlueprintPure,Category="Cinematic") UDWDialogueSequenceComponent* GetDialoguePlayer()const{return Dialogue;}
 virtual void TickComponent(float Dt,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
protected:
 virtual void EndPlay(const EEndPlayReason::Type R)override;
private:
 UPROPERTY(Transient) TObjectPtr<ACameraActor> PlaybackCamera;
 UPROPERTY(Transient) TObjectPtr<UDWCinematicSubtitleWidget> Subtitle;
 UPROPERTY(Transient) TObjectPtr<UDWDialogueSequenceComponent> Dialogue;
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
