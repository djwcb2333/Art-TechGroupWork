#pragma once
#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "DWLoadingTransition.generated.h"
class UFont; class USoundBase; class UAudioComponent; class UPackage; class APlayerController; class SWidget;
struct FDWLoadingPaintState;

UENUM(BlueprintType)
enum class EDWLoadingPhase:uint8 { Idle, Closing, Loading, Travelling, Complete, Opening };

/** Shared editable preset. No raster textures: the dough mascot and mask are vector geometry. */
UCLASS(BlueprintType)
class GDATTEST_API UDWLoadingTransitionSettings:public UDataAsset
{
 GENERATED_BODY()
public:
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Transition") bool bEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Transition") FLinearColor MaskColor=FLinearColor::Black;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Transition",meta=(ClampMin="0.1",Units="s")) float CloseSeconds=.85f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Transition",meta=(ClampMin="0.1",Units="s")) float OpenSeconds=.95f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Transition",meta=(ClampMin="0.1",ClampMax="10")) float IrisFrequencyHz=1.8f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Transition",meta=(ClampMin="0.1",ClampMax="2")) float IrisDampingRatio=.45f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Transition",meta=(ClampMin="0",ClampMax="1")) FVector2D CircleCenter=FVector2D(.5,.5);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading",meta=(ClampMin="0",Units="s")) float MinimumLoadingSeconds=1.2f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading",meta=(ClampMin="5",Units="s")) float TimeoutSeconds=120.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading") bool bShowMascotAndProgress=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading") TObjectPtr<UFont> Font;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading") bool bPauseDestinationUntilRevealed=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Readiness") bool bWaitForLevelStreaming=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Readiness") bool bWaitForAssetCompilation=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Readiness") bool bWaitForTextureStreaming=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Readiness",meta=(ClampMin="3",ClampMax="30")) int32 MinimumReadyFrames=3;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Readiness",meta=(ClampMin="0.1",Units="s")) float StableReadySeconds=.25f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading",meta=(ClampMin="0.5",ClampMax="2")) float ContentScale=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading") FText LoadingEnglish=FText::FromString(TEXT("Preparing your adventure..."));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading") FText LoadingChinese=FText::FromString(TEXT("正在准备冒险……"));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading") FText ReadyEnglish=FText::FromString(TEXT("Ready!"));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Loading") FText ReadyChinese=FText::FromString(TEXT("准备好了！"));
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dough") FLinearColor DoughColor=FLinearColor(1,.72f,.32f,1);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dough") FLinearColor OutlineColor=FLinearColor(.24f,.105f,.035f,1);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dough",meta=(ClampMin="0")) float DoughSize=105.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dough",meta=(ClampMin="0")) float JumpHeight=34.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dough",meta=(ClampMin="0.1")) float BounceFrequencyHz=1.35f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress") FLinearColor ProgressColor=FLinearColor(1,.62f,.19f,1);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress") FLinearColor TrackColor=FLinearColor(.13f,.085f,.05f,1);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress") FLinearColor TextColor=FLinearColor(1,.88f,.65f,1);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress",meta=(ClampMin="100")) float BarWidth=320.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress",meta=(ClampMin="4")) float BarHeight=14.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress") bool bBounceBarAtComplete=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress",meta=(ClampMin="0",Units="s")) float CompletionHoldSeconds=.7f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress",meta=(ClampMin="0",ClampMax="0.5")) float CompletionBounceStrength=.16f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress",meta=(ClampMin="0.1")) float CompletionFrequencyHz=3.5f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Progress",meta=(ClampMin="0.1",ClampMax="2")) float CompletionDampingRatio=.5f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio") TObjectPtr<USoundBase> CloseSound;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio") TObjectPtr<USoundBase> OpenSound;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Audio",meta=(ClampMin="0",ClampMax="2")) float SoundVolume=1.f;
};
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FDWLoadingEvent);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FDWLoadingFailed,FText,Reason);

/** Persistent across level travel. Use TravelToLevel instead of a raw Open Level node. */
UCLASS()
class GDATTEST_API UDWLoadingTransitionSubsystem:public UGameInstanceSubsystem
{
 GENERATED_BODY()
public:
 virtual void Initialize(FSubsystemCollectionBase& Collection) override;
 virtual void Deinitialize() override;
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading",meta=(AdvancedDisplay="OverrideSettings")) bool TravelToLevel(TSoftObjectPtr<UWorld> Destination,UDWLoadingTransitionSettings* OverrideSettings=nullptr);
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading",meta=(AdvancedDisplay="OverrideSettings")) bool BeginManualTransition(UDWLoadingTransitionSettings* OverrideSettings=nullptr);
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading") void SetManualProgress(float ProgressZeroToOne);
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading") void CompleteManualTransition();
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading") void CancelManualTransition();
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading",meta=(AdvancedDisplay="OverrideSettings")) bool PreviewTransition(float LoadingSeconds=2.f,UDWLoadingTransitionSettings* OverrideSettings=nullptr);
 UFUNCTION(BlueprintPure,Category="DoughWorld|Loading") bool IsTransitionActive() const {return Phase!=EDWLoadingPhase::Idle;}
 /** Target BeginPlay can hold the cover for asynchronous quest/dialogue/custom data. Complete every named task. */
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading") void AddReadinessTask(FName Id,FText Description);
 UFUNCTION(BlueprintCallable,Category="DoughWorld|Loading") void CompleteReadinessTask(FName Id);
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") FText ReadinessStatus;
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") int32 PendingLevels=0;
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") int32 PendingShaderJobs=0;
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") int32 PendingAssets=0;
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") int32 PendingTextures=0;
 UFUNCTION(BlueprintPure,Category="DoughWorld|Loading") UDWLoadingTransitionSettings* GetDefaultSettings();
 UFUNCTION(BlueprintPure,Category="DoughWorld|Loading") static float EvaluateCompletionScale(float Elapsed,float HoldSeconds,bool bEnabled,float Strength,float FrequencyHz,float DampingRatio);
 UFUNCTION(BlueprintPure,Category="DoughWorld|Loading") static float EvaluateIris(float Elapsed,float Duration,float FrequencyHz,float DampingRatio,bool bOpening);
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") EDWLoadingPhase Phase=EDWLoadingPhase::Idle;
 /** Stage-weighted work: packages 0-50%, world/streaming/compile/textures 50-97%, rendered readiness 97-99%, complete 100%. */
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") float Progress=0;
 UPROPERTY(BlueprintReadOnly,Category="DoughWorld|Loading") FText LastError;
 UPROPERTY(BlueprintAssignable,Category="DoughWorld|Loading") FDWLoadingEvent OnCovered;
 UPROPERTY(BlueprintAssignable,Category="DoughWorld|Loading") FDWLoadingEvent OnLoadingComplete;
 UPROPERTY(BlueprintAssignable,Category="DoughWorld|Loading") FDWLoadingEvent OnFinished;
 UPROPERTY(BlueprintAssignable,Category="DoughWorld|Loading") FDWLoadingFailed OnFailed;
 bool RequestMap(const FString& Map,UDWLoadingTransitionSettings* OverrideSettings=nullptr);
private:
 enum class EMode:uint8{Travel,Manual,Preview};
 EMode Mode=EMode::Manual;
 UPROPERTY(Transient) TObjectPtr<UDWLoadingTransitionSettings> DefaultSettings;
 UPROPERTY(Transient) TObjectPtr<UDWLoadingTransitionSettings> ActiveSettings;
 UPROPERTY(Transient) TObjectPtr<UPackage> PreloadedPackage;
 UPROPERTY(Transient) TObjectPtr<UWorld> PreloadedWorld;
 UPROPERTY(Transient) TObjectPtr<UAudioComponent> TransitionSound;
 TSharedPtr<FDWLoadingPaintState,ESPMode::ThreadSafe> PaintState;
 TSharedPtr<SWidget> Overlay;
 TWeakObjectPtr<APlayerController> LockedController;
 TWeakObjectPtr<UWorld> ArrivedWorld;
 FTSTicker::FDelegateHandle TickHandle;
 FDelegateHandle PostLoadHandle,TravelFailureHandle;
 FDelegateHandle BeginDrawHandle,EndDrawHandle;
 TWeakObjectPtr<class UGameViewportClient> ObservedViewport;
 bool PrimeDestinationCamera();
 bool CheckDestinationReadiness();
 TMap<FName,FText> ReadinessTasks;
 int32 PeakCompileWork=0,PeakTextureWork=0;
 bool bReadyToDraw=false,bReadinessStalled=false;
 double ReadySince=0;
 void BeforeDestinationDraw();
 void AfterDestinationDraw();
 void UnbindDrawEvents();
 FString TargetMap;
 double PhaseStarted=0,LoadingStarted=0,RequestStarted=0;
 float PreviewSeconds=2;
 uint32 RequestSerial=0;
 int32 ReadyFrames=0;
 bool bPausedDestination=false,bDestinationWasPaused=false;
 bool bAlive=true,bWorkComplete=false,bPreloadComplete=false,bOwnsMovie=false,bFailed=false;
 bool Begin(EMode NewMode,UDWLoadingTransitionSettings* OverrideSettings);
 bool Tick(float Delta);
 void SetPhase(EDWLoadingPhase NewPhase);
 void AttachOverlay();
 void LockInput();
 void ReleaseInput();
 void Publish();
 void StartPreload();
 void StartTravel();
 void MapLoaded(UWorld* World);
 void FailTransition(const FString& Reason);
 void Finish();
 void PlayTransitionSound(USoundBase* Sound);
};
