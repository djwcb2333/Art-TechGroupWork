#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DWEnemyCharacter.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class USoundBase;
class ADWEnemyNest;

UENUM(BlueprintType)
enum class EDWEnemyState : uint8 { Patrol, Chase, Attack, Dead };

/** Basic patrol, sight detection, pursuit and melee combat with editable tuning. */
UCLASS(Blueprintable)
class GDATTEST_API ADWEnemyCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    /** Pawn Owner becomes its AIController after possession; keep the spawning nest separately. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Enemy|Save") TObjectPtr<ADWEnemyNest> SourceNest;
    ADWEnemyCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual float TakeDamage(float DamageAmount, const FDamageEvent& DamageEvent, AController* EventInstigator, AActor* DamageCauser) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Components") TObjectPtr<USphereComponent> DetectionRange;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Enemy|Components") TObjectPtr<UStaticMeshComponent> PlaceholderBody;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Save") FName PersistentId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Health", meta=(ClampMin="1")) float MaxHealth = 100.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Enemy|Health") float Health = 100.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Enemy|AI") EDWEnemyState State = EDWEnemyState::Patrol;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|AI", meta=(ClampMin="1", Units="cm")) float DetectionRadius = 1000.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|AI", meta=(ClampMin="1", Units="cm")) float LoseTargetRadius = 1500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|AI", meta=(ClampMin="0", Units="cm")) float PatrolRadius = 450.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|AI", meta=(ClampMin="0", Units="s")) float PatrolWaitTime = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|AI", meta=(ClampMin="0", Units="cm/s")) float MoveSpeed = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|AI") bool bRequireLineOfSight = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="1", Units="cm")) float AttackRange = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="0")) float AttackDamage = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="0.05", Units="s")) float AttackInterval = 1.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="0.1", Units="s")) float DeathDestroyDelay = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Effects") TObjectPtr<USoundBase> AttackSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Effects") TObjectPtr<USoundBase> DeathSound;

    UFUNCTION(BlueprintPure, Category="Enemy") bool IsAlive() const { return Health > 0.f && State != EDWEnemyState::Dead; }
    /** Percent uses 0..100 units. Overlaps retain the strongest unexpired effect. */
    UFUNCTION(BlueprintCallable, Category="Enemy|Combat") void ApplySlow(float Percent, float Duration);
    UFUNCTION(BlueprintCallable, Category="Enemy|Save") void SetHealthForLoad(float NewHealth);
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Enemy|Rules") bool CanDetectTarget(AActor* Target) const;
    virtual bool CanDetectTarget_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Enemy|Rules") bool CanAttackTarget(AActor* Target) const;
    virtual bool CanAttackTarget_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Enemy|Rules") float GetAttackDamage(AActor* Target) const;
    virtual float GetAttackDamage_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Events") void OnStateChanged(EDWEnemyState PreviousState, EDWEnemyState NewState);
    /** After the authoritative damage call. ActualDamage is zero if the victim rejected the hit. */
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Events") void OnAttackResolved(AActor* Victim, float ActualDamage);
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Effects") void OnEnemyAttack(AActor* Victim);
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Effects") void OnEnemyDied();
protected:
    virtual void BeginPlay() override;
    void Die(bool bFromSave = false);
private:
    bool HasSightTo(const AActor* Target) const;
    void ChoosePatrolGoal();
    void MoveToward(const FVector& Goal, float DeltaSeconds);
    void StopAIMovement();
    void UpdateSlowEffects();
    void SetAIState(EDWEnemyState NewState);
    void FinishPersistentDeath();
    TWeakObjectPtr<APawn> TargetPawn;
    FVector HomeLocation = FVector::ZeroVector;
    FVector PatrolGoal = FVector::ZeroVector;
    float NextPatrolTime = 0.f;
    float NextAttackTime = 0.f;
    float NextPathRequestTime = 0.f;
    bool bHasPatrolGoal = false;
    TArray<FVector2D> SlowEffects;
    FTimerHandle DeathCleanupTimer;
    bool bHealthRestoredFromSave = false;
};
