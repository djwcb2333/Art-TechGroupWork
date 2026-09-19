#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWAlcoholArea.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class USoundAttenuation;
class UAudioComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** One bottle's area effect, with configurable repeated transformation gain. */
UCLASS(Blueprintable)
class GDATTEST_API ADWAlcoholArea : public AActor
{
    GENERATED_BODY()
public:
    ADWAlcoholArea();
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alcohol|Components") TObjectPtr<USphereComponent> DamageRange;
    /** Hidden query-only collision cylinder. Radius/VerticalTolerance drive its size; independent of VFX. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alcohol|Components") TObjectPtr<UStaticMeshComponent> DamageVolume;
    /** Override in Blueprint to author another attack shape using Get Damage Targets In Volume. */
    UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Alcohol|Rules") TArray<AActor*> CollectDamageTargets() const;
    virtual TArray<AActor*> CollectDamageTargets_Implementation() const;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alcohol|Components") TObjectPtr<UStaticMeshComponent> PlaceholderPool;
    /** Visible gameplay range disc; independent of the DamageRange editor debug shape and AreaVFX. The mesh can be replaced in Blueprint. */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alcohol|Components") TObjectPtr<UStaticMeshComponent> GroundIndicator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Damage", meta=(ClampMin="0.05", Units="s")) float Duration = 5.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Damage", meta=(ClampMin="0.01", Units="s")) float DamageInterval = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Damage", meta=(ClampMin="0")) float DamagePerTick = 10.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Damage", meta=(ClampMin="1", Units="cm")) float Radius = 220.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Damage", meta=(ClampMin="0", ClampMax="100")) float SlowPercent = 30.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Damage") bool bGrantTransformationOnEveryDamageTick = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Damage", meta=(ClampMin="1", Units="cm")) float VerticalTolerance = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects") TObjectPtr<UNiagaraSystem> AreaVFX;
    /** Optional ground-burning loop. Empty stays silent. Short non-looping clips replay on the same component until the damage area ends. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio", meta=(DisplayName="Ground Burning Loop Sound")) TObjectPtr<USoundBase> AreaSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio", meta=(ClampMin="0", ClampMax="4")) float AreaSoundVolume = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio", meta=(ClampMin="0.125", ClampMax="4")) float AreaSoundPitch = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio") TObjectPtr<USoundAttenuation> AreaSoundAttenuation;
    /** Final fade measured in game seconds, sampled at BeginPlay and clamped to the active duration. Zero stops directly at expiration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio", meta=(ClampMin="0", Units="s")) float AreaSoundFadeOutSeconds = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire") FVector AreaVFXScale = FVector(1.f);
    /** Offset in the ground surface frame; positive Z lifts the fire away from the surface. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire", meta=(Units="cm")) FVector AreaVFXOffset = FVector(0.f, 0.f, 2.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire") FRotator AreaVFXRotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire") bool bAlignAreaVFXToGround = true;
    /** Component XY scaling fallback only. A valid float AreaVFXRadiusParameter takes priority and disables this automatic scaling. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire") bool bScaleAreaVFXToRadius = true;
    /** The effect's authored radius at scale 1. Used for component XY scaling only when no valid radius parameter exists. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire", meta=(ClampMin="1", Units="cm")) float AreaVFXReferenceRadius = 220.f;
    /** Emission stops at Duration. Existing particles may finish for at most this time. Does not extend damage. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire", meta=(ClampMin="0", Units="s")) float AreaVFXFadeOutTime = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire") bool bHidePlaceholderWhenVFXActive = true;
    /** Optional exposed float user parameter receiving Radius in cm. When valid, replaces automatic component radius scaling; None, missing or wrong-type names use the component fallback. Manual AreaVFXScale still applies. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire") FName AreaVFXRadiusParameter = NAME_None;
    /** Optional float user parameter receiving gameplay Duration in seconds. None skips it. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Fire") FName AreaVFXDurationParameter = NAME_None;
    /** Material for a 100 cm XY plane. None hides the indicator instead of drawing an unmasked square. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Indicator", meta=(DisplayName="Ground Indicator Material")) TObjectPtr<UMaterialInterface> GroundIndicatorMaterial;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Indicator", meta=(DisplayName="Show Ground Indicator")) bool bShowGroundIndicator = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Indicator", meta=(DisplayName="Indicator Color")) FLinearColor IndicatorColor = FLinearColor(1.f, 0.28f, 0.03f, 1.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Indicator", meta=(DisplayName="Indicator Fill Opacity", ClampMin="0", ClampMax="1")) float IndicatorFillOpacity = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Indicator", meta=(DisplayName="Indicator Ring Opacity", ClampMin="0", ClampMax="1")) float IndicatorRingOpacity = 0.8f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Indicator", meta=(DisplayName="Indicator Ring Width Ratio", ClampMin="0", ClampMax="0.5")) float IndicatorRingWidthRatio = 0.025f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Ground Indicator", meta=(DisplayName="Indicator Height Offset", Units="cm")) float IndicatorHeightOffset = 2.f;

    UFUNCTION(BlueprintCallable, Category="Alcohol") void ApplyAreaTick();
    /** Normalized burn envelope (0..1), excluding AreaSoundVolume. Uses the original damage deadline; pause freezes it. Zero when no loop exists or it has ended. */
    UFUNCTION(BlueprintPure, Category="Alcohol|Audio") float GetBurnAudioGain() const;
    UFUNCTION(BlueprintPure, Category="Alcohol|Audio") UAudioComponent* GetBurnAudioComponent() const { return BurnAudioComponent; }
    /** Additional Blueprint filter after physical hit-volume detection. Player friendly fire is disabled by default; tagged custom actors may receive Event AnyDamage. */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Alcohol|Rules") bool CanAffectTarget(AActor* Target) const;
    virtual bool CanAffectTarget_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Alcohol|Rules") float GetDamageForTarget(AActor* Target) const;
    virtual float GetDamageForTarget_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintImplementableEvent, Category="Alcohol|Events") void OnAreaActivated();
    /** Damage, slow and transformation have already been applied when this notification runs. */
    UFUNCTION(BlueprintImplementableEvent, Category="Alcohol|Events") void OnTargetDamaged(AActor* Target, float ActualDamage);
    UFUNCTION(BlueprintImplementableEvent, Category="Alcohol|Events") void OnAreaTickCompleted(int32 DamagedTargets, float TotalDamage);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> ActiveAreaVFX;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> GroundIndicatorMID;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> GroundIndicatorMIDSource;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> BurnAudioComponent;
    void StartBurnAudio(const FVector& SurfaceLocation);
    void StopBurnAudio();
    UFUNCTION() void HandleBurnAudioFinished();
    void ResolveGroundSurface(FVector& SurfaceLocation, FQuat& SurfaceRotation) const;
    void SetupGroundIndicator(const FVector& SurfaceLocation, const FQuat& SurfaceRotation);
    FTimerHandle DamageTimer;
    bool bGrantedAttackCharge = false;
    float ExpirationTime = 0.f;
    float BurnAudioActiveDuration = 0.f;
    float BurnAudioFadeDuration = 0.f;
    bool bBurnAudioActive = false;
    bool bBurnAudioRestartPending = false;
    bool bApplyingAreaTick = false;
};
