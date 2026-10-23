#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWCombatMoldRoot.h"
#include "DWCombatFeedbackComponent.generated.h"
class UMaterialInterface;class UMaterialInstanceDynamic;class USoundBase;class UMeshComponent;
class UAudioComponent;class UNiagaraSystem;class UNiagaraComponent;class ADWPlayerCharacter;
UENUM(BlueprintType)
enum class EDWCombatFeedbackCue:uint8 { ChargeReady, SwordSlam, SwordSweep, Block, PerfectBlock };
USTRUCT()
struct FDWCombatFlashBinding
{
    GENERATED_BODY()
    UPROPERTY() TWeakObjectPtr<UMeshComponent> Mesh;
    UPROPERTY() TObjectPtr<UMaterialInterface> OriginalOverlay;
    UPROPERTY() TArray<TObjectPtr<UMaterialInterface>> OriginalMaterials;
    bool bReplaceMaterials=false;
};
/** Presentation only. Never deals damage, spends resources, changes phases or submits gameplay events. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWCombatFeedbackComponent:public UActorComponent
{
    GENERATED_BODY()
public:
    UDWCombatFeedbackComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback") bool bFeedbackEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Sound") TObjectPtr<USoundBase> ChargeReadySound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Sound") TObjectPtr<USoundBase> BlockSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Sound") TObjectPtr<USoundBase> PerfectBlockSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Sound",meta=(ClampMin="0",UIMax="2")) float SoundVolume=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Flash") TObjectPtr<UMaterialInterface> FlashMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Flash",meta=(ClampMin="0",Units="s")) float FlashDurationSeconds=.22f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Flash",meta=(ClampMin="0",UIMax="20")) float FlashStrength=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Flash") FLinearColor ChargeReadyColor=FLinearColor(1.f,.65f,.12f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Flash") FLinearColor PerfectBlockColor=FLinearColor(.1f,.75f,1.f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Trails") bool bHighlightTrails=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Trails") TObjectPtr<UMaterialInterface> TrailHighlightMaterial;
    /** Optional future Niagara User material parameter. None leaves Niagara authoring unchanged. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Trails") FName NiagaraTrailMaterialParameter=NAME_None;
    /** Root has no source animation yet: these are committed-state presentation slots, not animation timing or gameplay events. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root") bool bMoldRootFeedbackEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Sound") TObjectPtr<USoundBase> RootEnterSound;
    /** Use a looping SoundWave/SoundCue. A finite sound is restarted while this live stance remains valid; source assets are never modified. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Sound") TObjectPtr<USoundBase> RootLoopSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Sound") TObjectPtr<USoundBase> RootExitSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Sound",meta=(ClampMin="0",UIMax="2")) float RootSoundVolume=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Niagara") TObjectPtr<UNiagaraSystem> RootEnterNiagara;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Niagara") TObjectPtr<UNiagaraSystem> RootLoopNiagara;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Niagara") TObjectPtr<UNiagaraSystem> RootExitNiagara;
    /** Relative to the capsule's foot, not its centre. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Niagara") FVector RootEffectFootOffset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Niagara") FRotator RootEffectRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Combat Feedback|Mold Root|Niagara") FVector RootEffectScale=FVector::OneVector;
    UFUNCTION(BlueprintPure,Category="Combat Feedback|Mold Root|Diagnostics") bool IsMoldRootFeedbackBound()const{return bRootFeedbackBound;}
    UFUNCTION(BlueprintPure,Category="Combat Feedback|Mold Root|Diagnostics") UAudioComponent* GetRootLoopAudioComponent()const{return RootLoopAudio;}
    UFUNCTION(BlueprintPure,Category="Combat Feedback|Mold Root|Diagnostics") UNiagaraComponent* GetRootLoopVFXComponent()const{return RootLoopVFX;}
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Feedback|Mold Root|Diagnostics") int32 RootEnterCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Feedback|Mold Root|Diagnostics") int32 RootExitCount=0;
    /** Counts state bindings, including an intentional None/None loop; not a claim that content was played. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Feedback|Mold Root|Diagnostics") int32 RootLoopBindCount=0;
    UFUNCTION(BlueprintCallable,Category="Combat Feedback") void PlayFeedback(EDWCombatFeedbackCue Cue);
    UFUNCTION(BlueprintCallable,Category="Combat Feedback") void StopFlash();
    UFUNCTION(BlueprintPure,Category="Combat Feedback") bool IsFlashActive()const{return FlashRemainingSeconds>0.f;}
    UFUNCTION(BlueprintPure,Category="Combat Feedback") float GetFlashRemainingSeconds()const{return FlashRemainingSeconds;}
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Feedback|Diagnostics") int32 ChargeReadyCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Feedback|Diagnostics") int32 SwordAttackCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Feedback|Diagnostics") int32 BlockCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Combat Feedback|Diagnostics") int32 PerfectBlockCount=0;
    virtual void TickComponent(float,ELevelTick,FActorComponentTickFunction*)override;
protected:
    virtual void BeginPlay()override;
    virtual void EndPlay(const EEndPlayReason::Type)override;
private:
    friend class ADWPlayerCharacter;
    // Only the player's post-commit movement publication may schedule enter/exit.
    // Future animation-normalized scheduling can call the separate presentation helpers;
    // it must still keep this committed root identity as its eligibility gate.
    void PublishMoldRootFeedback(bool bRooted);
    void TickMoldRootFeedback();
    void StopMoldRootLoops();
    void StopMoldRootBursts();
    void ClearMoldRootFeedback();
    void EnsureMoldRootLoops();
    void PlayMoldRootBurst(bool bEntering);
    bool IsMoldRootEpisodeCurrent(bool bRooted)const;
    FDWMoldRootIdentity RootFeedbackIdentity;
    bool bRootFeedbackBound=false,bRootEnterPending=false,bRootExitPending=false;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> RootLoopAudio;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> RootLoopVFX;
    UPROPERTY(Transient) TArray<TObjectPtr<UAudioComponent>> RootBurstAudio;
    UPROPERTY(Transient) TArray<TObjectPtr<UNiagaraComponent>> RootBurstVFX;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> FlashInstance;
    UPROPERTY(Transient) TArray<FDWCombatFlashBinding> FlashBindings;
    float FlashRemainingSeconds=0.f;
    double FlashLastWorldSeconds=0.;
    void ApplyFlashToMeshes();
};
