#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWGameplayCameraShake.generated.h"
class UCameraShakeBase;
class APlayerCameraManager;

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
 UFUNCTION(BlueprintCallable,Category="Gameplay Shake") void StopGameplayShake(bool bImmediately=true);
 UFUNCTION(BlueprintPure,Category="Gameplay Shake|Diagnostics") UCameraShakeBase* GetActiveShake()const{return ActiveShake;}
 UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Gameplay Shake|Diagnostics") int32 StartCount=0;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
protected:
 virtual void EndPlay(const EEndPlayReason::Type Reason)override;
private:
 UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveShake;
 TWeakObjectPtr<APlayerCameraManager> CameraManager;
 bool bStopping=false,bPlayedThisPeriod=false;
 float StartedScale=0.f;
};
