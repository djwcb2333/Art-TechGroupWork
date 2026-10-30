#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "DWSourDeathPresentationClock.h"
#include "DWSourDeathPresentationComponent.generated.h"

class ADWSourDoughEnemy;
class UAnimInstance;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraSystem;
class UNiagaraComponent;
class UStaticMesh;
class UStaticMeshComponent;
class USkeletalMeshComponent;

USTRUCT(BlueprintType)
struct FDWSourDeathDissolveSurface
{
    GENERATED_BODY()
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Death Dissolve") TSoftObjectPtr<UMaterialInterface> OriginalMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Death Dissolve") TSoftObjectPtr<UMaterialInterface> DissolveMaterial;
};
USTRUCT()
struct FDWSourDeathMaterialBinding
{
    GENERATED_BODY()
    UPROPERTY() int32 Slot=INDEX_NONE;
    UPROPERTY() TObjectPtr<UMaterialInterface> Original;
    UPROPERTY() TObjectPtr<UMaterialInstanceDynamic> Instance;
};

/** Sour-only presentation. Gameplay death, drops, and rewards are committed elsewhere. */
UCLASS(ClassGroup=(DoughWorld),BlueprintType,meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWSourDeathPresentationComponent:public UActorComponent
{
    GENERATED_BODY()
public:
    UDWSourDeathPresentationComponent();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Timing",meta=(ClampMin="0.05",Units="s")) float DeathDissolveDuration=1.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Timing",meta=(ClampMin="0",Units="s")) float DeathAfterAnimationHold=0.f;
    /** Abnormal graph/signal fallback; never the ordinary animation duration. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Timing",meta=(ClampMin="1",ClampMax="120",Units="s")) float DeathAnimationSignalTimeout=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Material") TArray<FDWSourDeathDissolveSurface> DeathDissolveSurfaces;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Material") FLinearColor DeathDissolveEdgeColor=FLinearColor(1.f,.82f,.08f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Material",meta=(ClampMin="0.001",ClampMax="0.3")) float DeathDissolveEdgeWidth=.045f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Material",meta=(ClampMin="0")) float DeathDissolveEdgeStrength=5.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust") TSoftObjectPtr<UNiagaraSystem> DeathDustSystem;
    /** When Niagara is not supplied, use the same native cloud assets as gathering. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust") TSoftObjectPtr<UStaticMesh> DeathDustMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust") TSoftObjectPtr<UMaterialInterface> DeathDustMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust",meta=(ClampMin="0",ClampMax="24")) int32 DeathDustCount=9;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust",meta=(ClampMin="0",Units="cm")) float DeathDustRadius=28.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust",meta=(ClampMin="0.05",Units="s")) float DeathDustDuration=.85f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust") FLinearColor DeathDustColor=FLinearColor(.94f,.87f,.66f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust") FVector DeathDustOffset=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust") FRotator DeathDustRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Sour Death|Dust") FVector DeathDustScale=FVector::OneVector;
    bool BeginCommittedDeath();
    void CancelPresentation();
    bool NotifyAnimationCompleted(const UAnimInstance* Sender,uint64 Generation);
    uint64 GetPresentationGeneration()const{return bEpisodeOwned?EpisodeGeneration:0;}
    bool OwnsEpisode(uint64 Generation)const;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction)override;
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason)override;
private:
    DWSourDeathPresentation::FIdentity CurrentIdentity()const;
    bool IsCurrentEpisode()const;
    void StartDissolve();
    void StartNativeDust();
    void UpdateNativeDust(double Now);
    void RestoreMaterialsAndDust();
    void ForceDeathAnimationEvaluation(USkeletalMeshComponent* Mesh);
    void RestoreDeathAnimationEvaluation();
    void FinishOwnedPresentation();
    DWSourDeathPresentation::FClock Clock;
    DWSourDeathPresentation::FIdentity Identity{};
    uint64 EpisodeGeneration=0;
    bool bEpisodeOwned=false,bDissolveStarted=false,bDissolveComplete=false;
    double StartedAt=0.,SignalTimeoutSeconds=0.,DustStartedAt=0.,DustDurationRuntime=0.;
    float DustMeshDiameter=1.f;
    UPROPERTY(Transient) TArray<FDWSourDeathMaterialBinding> Bindings;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> OriginalRawOverrides;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> SavedSlotOverlays;
    UPROPERTY(Transient) TWeakObjectPtr<USkeletalMeshComponent> BoundMesh;
    TWeakObjectPtr<USkeletalMeshComponent> ForcedTickMesh;
    EVisibilityBasedAnimTickOption SavedVisibilityTick=EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
    bool bSavedUpdateRateOptimizations=false, bSavedMeshTickEnabled=false, bTickSettingsCaptured=false;
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> DustComponent;
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> NativeDust;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> NativeDustMID;
};
