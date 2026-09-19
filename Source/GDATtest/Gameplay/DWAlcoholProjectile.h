#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWAlcoholProjectile.generated.h"

class ADWAlcoholArea;
class USphereComponent;
class UStaticMeshComponent;
class UProjectileMovementComponent;
class UNiagaraSystem;
class UNiagaraComponent;
class USoundBase;
class USoundAttenuation;

UCLASS(Blueprintable)
class GDATTEST_API ADWAlcoholProjectile : public AActor
{
    GENERATED_BODY()
public:
    ADWAlcoholProjectile();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alcohol|Components") TObjectPtr<USphereComponent> CollisionSphere;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alcohol|Components") TObjectPtr<UStaticMeshComponent> BottleMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Alcohol|Components") TObjectPtr<UProjectileMovementComponent> ProjectileMovement;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Throw") TSubclassOf<ADWAlcoholArea> AreaClass;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Throw", meta=(ClampMin="0.05", Units="s")) float FlightTime = 0.65f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Throw", meta=(ClampMin="0.1", Units="s")) float MaxFlightLifetime = 6.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Throw", meta=(ClampMin="1", Units="cm")) float MaxThrowDistance = 1200.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects") TObjectPtr<UNiagaraSystem> TrailVFX;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects") TObjectPtr<UNiagaraSystem> ImpactVFX;
    /** Optional bottle collision / ignition sound; this does not add explosion damage. Empty stays silent. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio", meta=(DisplayName="Explosion / Impact Sound")) TObjectPtr<USoundBase> ImpactSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio", meta=(ClampMin="0", ClampMax="4")) float ImpactSoundVolume = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio", meta=(ClampMin="0.125", ClampMax="4")) float ImpactSoundPitch = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Audio") TObjectPtr<USoundAttenuation> ImpactSoundAttenuation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Trail") FVector TrailVFXScale = FVector(1.f);
    /** Local offset from the bottle. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Trail", meta=(Units="cm")) FVector TrailVFXOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Trail") FRotator TrailVFXRotation = FRotator::ZeroRotator;
    /** Zero emits until the bottle bursts; otherwise stop earlier after this duration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Trail", meta=(ClampMin="0", Units="s")) float TrailEmissionDuration = 0.f;
    /** Hard cleanup deadline after emission stops, including looping third-party systems. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Trail", meta=(ClampMin="0", Units="s")) float TrailFadeOutTime = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Impact") FVector ImpactVFXScale = FVector(1.f);
    /** Local offset in the impact surface frame when surface alignment is enabled. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Impact", meta=(Units="cm")) FVector ImpactVFXOffset = FVector::ZeroVector;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Impact") FRotator ImpactVFXRotation = FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Impact") bool bAlignImpactToSurface = true;
    /** A short ignition burst, independent of the damaging ground area's duration. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Impact", meta=(ClampMin="0.01", Units="s")) float ImpactEmissionDuration = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Alcohol|Effects|Impact", meta=(ClampMin="0", Units="s")) float ImpactFadeOutTime = 1.5f;
    UFUNCTION(BlueprintCallable, Category="Alcohol|Throw") void LaunchAtTarget(const FVector& Target);
    /** Return a velocity only. Native LaunchAtTarget remains the single movement launch action. */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Alcohol|Rules") FVector CalculateLaunchVelocity(const FVector& Target) const;
    virtual FVector CalculateLaunchVelocity_Implementation(const FVector& Target) const;
    UFUNCTION(BlueprintImplementableEvent, Category="Alcohol|Events") void OnProjectileLaunched(FVector RequestedTarget, FVector InitialVelocity);
    /** Area has already been spawned. This projectile is destroyed immediately after this event returns. */
    UFUNCTION(BlueprintImplementableEvent, Category="Alcohol|Events") void OnProjectileBurst(ADWAlcoholArea* SpawnedArea, FVector BurstLocation);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UFUNCTION() void OnProjectileHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComponent, FVector NormalImpulse, const FHitResult& Hit);
    void ExpireInFlight();
    void Burst(const FVector& Location, const FVector& ImpactNormal = FVector::UpVector);
    void FinishTrail();
    UPROPERTY(Transient) TObjectPtr<UNiagaraComponent> ActiveTrailVFX;
    bool bHasBurst = false;
    FTimerHandle FlightTimer;
    FTimerHandle TrailEmissionTimer;
};
