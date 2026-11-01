#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWGameplayCameraShake.h"
#include "DWCombatCommitLedger.h"
#include "DWCommittedHitFeedbackComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
class UMeshComponent;
class APlayerCameraManager;
class UCameraShakeBase;

USTRUCT()
struct FDWEnemyHitOverlayBinding
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> Original;
};

/** Presentation only; published by the damage authority after positive health loss. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWCommittedHitFeedbackComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWCommittedHitFeedbackComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage Feedback",meta=(ClampMin="0",Units="s")) float DurationSeconds=.20f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage Feedback|Enemy") TObjectPtr<UMaterialInterface> EnemyFlashMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage Feedback|Enemy") FLinearColor EnemyFlashColor=FLinearColor::White;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage Feedback|Enemy",meta=(ClampMin="0",UIMax="20")) float EnemyFlashStrength=4.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage Feedback|Enemy",meta=(ClampMin="0",ClampMax="1")) float EnemyFlashOpacity=.65f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage Feedback|Camera") FDWGameplayCameraPulseSettings DamagePulse=FDWGameplayCameraPulseSettings(.14f,.55f,.07f,22.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Damage Feedback|Camera",meta=(ClampMin="0",Units="s")) float MinimumShakeIntervalSeconds=.14f;
    UFUNCTION(BlueprintPure,Category="Damage Feedback") float GetCombatHitIntensity()const;
    UFUNCTION(BlueprintPure,Category="Damage Feedback") float GetCombatHitRemainingSeconds()const;
    /** Presentation identity only; native uint64 because Blueprint does not expose uint64. */
    uint64 GetCombatHitSerial()const{return CombatHitSerial;}
    void QueueCommittedImpact(DWCombatCommit::FActionId,const FDWGameplayCameraPulseSettings&,float Weight=1.f);
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
private:
    friend class UDWCombatAuthoritySubsystem;
    void PublishCommittedDamage(float Applied,DWCombatCommit::FActionId Impact={});
    void ClearFeedback(bool StopPulse=true);
    bool IsEpisodeCurrent()const;
    void ApplyEnemyOverlay();
    void StopDamageShake();
    void TryPlayDamageShake(const FDWGameplayCameraPulseSettings* Override=nullptr);
    bool IsPulseOwnerCurrent()const;
    FDWGameplayCameraPulseSettings PendingImpactPulse;
    DWCombatCommit::FActionId PendingImpactAction{};
    DWCombatCommit::FEntityLife PendingImpactLife{},PulseLife{};
    uint64 PendingImpactEpoch=0,PulseEpoch=0;
    bool bPendingImpact=false;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashInstance;
    UPROPERTY(Transient) TArray<FDWEnemyHitOverlayBinding> Bindings;
    UPROPERTY(Transient) TObjectPtr<UCameraShakeBase> ActiveDamageShake;
    TWeakObjectPtr<APlayerCameraManager> DamageCameraManager;
    DWCombatCommit::FEntityLife EpisodeLife{};
    uint64 EpisodeEpoch=0;
    uint64 CombatHitSerial=0;
    double StartedAt=-1.,EndsAt=-1.,LastShakeAt=-1.;
};
