#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Camera/CameraShakeBase.h"
#include "DWGameplayCameraShake.generated.h"
class UCameraShakeBase;
class APlayerCameraManager;
class UDWGameplayCameraShakeComponent;


/** Event names only; the component is presentation and never commits combat facts. */
UENUM(BlueprintType)
enum class EDWCombatShakeEvent:uint8 { SwordAttack,Block,PerfectBlock,ChargeReady };

USTRUCT(BlueprintType)
struct GDATTEST_API FDWGameplayCameraPulseSettings
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pulse",meta=(ClampMin="0.001",Units="s")) float DurationSeconds=.16f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pulse",meta=(ClampMin="0",Units="cm")) float PositionAmplitudeCm=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pulse",meta=(ClampMin="0",Units="deg")) float RotationAmplitudeDegrees=.12f;
 // Keep the serialized property name so existing Blueprint values remain compatible.
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Pulse",meta=(ClampMin="0.001",DisplayName="Return Stiffness",ToolTip="Non-oscillating return rate (1/s). Higher values settle sooner; Duration fixes the end time. Serialized as FrequencyHz for compatibility; this is not repeated vibration.")) float FrequencyHz=20.f;
 FDWGameplayCameraPulseSettings()=default;
 FDWGameplayCameraPulseSettings(float Duration,float Position,float Rotation,float Frequency)
  :DurationSeconds(Duration),PositionAmplitudeCm(Position),RotationAmplitudeDegrees(Rotation),FrequencyHz(Frequency){}
 bool IsValid()const;
};

/** Independent continuous presentation settings; no attack or animation duration. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWGameplayCameraHoldSettings
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hold",meta=(ClampMin="0",Units="cm")) float PositionAmplitudeCm=.45f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hold",meta=(ClampMin="0",Units="deg")) float RotationAmplitudeDegrees=.055f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Hold",meta=(ClampMin="0.001",Units="Hz")) float FrequencyHz=12.f;
 bool IsValid()const;
};

/** Infinite native hold, gated by the live component's read-only weapon state. */
UCLASS(Transient,NotBlueprintable)
class GDATTEST_API UDWGameplayCameraHoldPattern:public UCameraShakePattern
{
 GENERATED_BODY()
public:
 void Configure(const FDWGameplayCameraHoldSettings& Value,UDWGameplayCameraShakeComponent* Owner);
private:
 FDWGameplayCameraHoldSettings Settings;
 TWeakObjectPtr<UDWGameplayCameraShakeComponent> HoldOwner;
 double PhaseCycles=0.;
 bool bFinished=false;
 virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo)const override;
 virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params)override;
 virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& OutResult)override;
 virtual bool IsFinishedImpl()const override;
 virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params)override;
};

UCLASS(Transient,NotBlueprintable)
class GDATTEST_API UDWGameplayHoldCameraShake:public UCameraShakeBase
{
 GENERATED_BODY()
public:
 UDWGameplayHoldCameraShake(const FObjectInitializer& Init=FObjectInitializer::Get());
};

/** Self-contained native pulse. Requires no camera-shake asset or optional plugin module. */
UCLASS(Transient,NotBlueprintable)
class GDATTEST_API UDWGameplayCameraPulsePattern:public UCameraShakePattern
{
 GENERATED_BODY()
public:
 UDWGameplayCameraPulsePattern(const FObjectInitializer& Init=FObjectInitializer::Get());
 void Configure(const FDWGameplayCameraPulseSettings& Value,UWorld* World);
private:
 FDWGameplayCameraPulseSettings Settings;
 TWeakObjectPtr<UWorld> GameWorld;
 double Elapsed=0.;
 bool bFinished=false,bHasEvaluatedSample=false;
 virtual void GetShakePatternInfoImpl(FCameraShakeInfo& OutInfo)const override;
 virtual void StartShakePatternImpl(const FCameraShakePatternStartParams& Params)override;
 virtual void UpdateShakePatternImpl(const FCameraShakePatternUpdateParams& Params,FCameraShakePatternUpdateResult& OutResult)override;
 virtual bool IsFinishedImpl()const override;
 virtual void StopShakePatternImpl(const FCameraShakePatternStopParams& Params)override;
};

UCLASS(Transient,NotBlueprintable)
class GDATTEST_API UDWGameplayPulseCameraShake:public UCameraShakeBase
{
 GENERATED_BODY()
public:
 UDWGameplayPulseCameraShake(const FObjectInitializer& Init=FObjectInitializer::Get());
};

/** Owns only the normal-play shake. Cinematics own a separate instance. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,meta=(BlueprintSpawnableComponent,DisplayName="DW Gameplay Camera Shake"))
class GDATTEST_API UDWGameplayCameraShakeComponent:public UActorComponent
{
 GENERATED_BODY()
public:
 UDWGameplayCameraShakeComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake") bool bShakeEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake") TSubclassOf<UCameraShakeBase> ShakeClass;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake",meta=(ClampMin="0",UIMax="2")) float ShakeScale=1.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake") bool bOnlyWhileMoving=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake",meta=(ClampMin="0",Units="cm/s",EditCondition="bOnlyWhileMoving")) float MinimumSpeed=10.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake",meta=(ToolTip="Restart finite shakes while gameplay is eligible. For smooth continuous motion set Duration=0 in the shake asset.")) bool bRepeatFiniteShake=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat|Sword Ready Hold",meta=(DisplayName="Legacy Sword Ready Hold Enabled (Unused)",ToolTip="Retained for older Blueprint compatibility. Continuous ready shake is disabled even if this saved value is true. Use Sword Charge Ready Pulse.")) bool bSwordReadyHoldEnabled=false;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat|Sword Ready Hold",meta=(ToolTip="Legacy settings retained only for loading older Blueprints. Ready now produces one Sword Charge Ready Pulse, not a continuous hold.")) FDWGameplayCameraHoldSettings SwordReadyHold;
 UFUNCTION(BlueprintPure,Category="Gameplay Shake|Diagnostics") bool IsSwordReadyHoldActive()const;
 UFUNCTION(BlueprintPure,Category="Gameplay Shake|Diagnostics") UCameraShakeBase* GetActiveSwordReadyHold()const{return ActiveSwordReadyHold;}
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gameplay Shake|Diagnostics") int32 SwordReadyHoldStartCount=0;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat") bool bCombatPulseEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat") FDWGameplayCameraPulseSettings SwordChargeReadyPulse=FDWGameplayCameraPulseSettings(.18f,.8f,.10f,18.f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat") FDWGameplayCameraPulseSettings SwordAttackPulse=FDWGameplayCameraPulseSettings(.22f,3.f,.35f,18.f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat") FDWGameplayCameraPulseSettings BlockPulse=FDWGameplayCameraPulseSettings(.12f,.7f,.09f,22.f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat") FDWGameplayCameraPulseSettings PerfectBlockPulse=FDWGameplayCameraPulseSettings(.20f,2.2f,.30f,24.f);
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gameplay Shake|Combat",meta=(ClampMin="1",ClampMax="16")) int32 MaxConcurrentCombatPulses=4;
 UFUNCTION(BlueprintCallable,Category="Gameplay Shake|Combat") bool PlayCombatPulse(EDWCombatShakeEvent Event,float Scale=1.f);
 UFUNCTION(BlueprintCallable,Category="Gameplay Shake|Combat") bool PlayConfiguredCombatPulse(const FDWGameplayCameraPulseSettings& Settings,float Scale=1.f);
 UFUNCTION(BlueprintCallable,Category="Gameplay Shake|Combat") void StopCombatPulses();
 UFUNCTION(BlueprintPure,Category="Gameplay Shake|Diagnostics") int32 GetActiveCombatPulseCount()const;
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gameplay Shake|Diagnostics") int32 CombatPulseStartCount=0;
 UFUNCTION(BlueprintCallable,Category="Gameplay Shake") void StopGameplayShake(bool bImmediately=true);
 UFUNCTION(BlueprintPure,Category="Gameplay Shake|Diagnostics") UCameraShakeBase* GetActiveShake()const{return ActiveShake;}
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gameplay Shake|Diagnostics") int32 StartCount=0;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
protected:
 virtual void BeginPlay()override;
 virtual void EndPlay(const EEndPlayReason::Type Reason)override;
private:
 friend class UDWGameplayCameraHoldPattern;
 bool ShouldPlaySwordReadyHold()const;
 void UpdateSwordReadyHold();
 void StopSwordReadyHold();
 UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveSwordReadyHold;
 TWeakObjectPtr<APlayerCameraManager> SwordReadyCameraManager;
 FDWGameplayCameraHoldSettings StartedSwordReadyHoldSettings;
 UPROPERTY(Transient) TArray<TObjectPtr<UCameraShakeBase>> ActiveCombatPulses;
 TWeakObjectPtr<APlayerCameraManager> CombatCameraManager;
 bool CanPlayCombatPulse()const;
 void PruneCombatPulses();
 UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveShake;
 TWeakObjectPtr<APlayerCameraManager> CameraManager;
 bool bStopping=false,bPlayedThisPeriod=false;
 float StartedScale=0.f;
};
