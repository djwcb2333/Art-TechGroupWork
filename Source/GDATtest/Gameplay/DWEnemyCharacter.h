#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "DWCombatTypes.h"
#include "DWEnemyCombatProfile.h"
#include "DWCombatDagger.h"
#include "DWEnemyForcedFall.h"
#include "DWEnemyCharacter.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class USoundBase;
class ADWEnemyNest;
class UAnimationAsset;
class ADWPlayerCharacter;
class FDWCombatRules;

/** Optional attacks sharing the existing Attack state and authoritative hit resolution. */
USTRUCT(BlueprintType)
struct FDWEnemyAttackVariant
{
    GENERATED_BODY()

    /** Empty uses the character's ordinary AttackAnimation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack") TObjectPtr<UAnimationAsset> Animation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0")) float SelectionWeight = 1.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0")) float Damage = 8.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="1", Units="cm")) float Range = 150.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0", Units="s")) float Windup = 0.55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0.01", Units="s")) float Active = 0.12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0", Units="s")) float Recovery = 0.4f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Attack", meta=(ClampMin="0.05", Units="s")) float Interval = 1.2f;
};

UENUM(BlueprintType)
// Existing serialized values must remain stable; append new states only.
enum class EDWEnemyState : uint8 { Patrol = 0, Chase = 1, Attack = 2, Dead = 3, Alert = 4, ReturningHome = 5 };

/** Basic patrol, sight detection, pursuit and melee combat with editable tuning. */
UCLASS(Blueprintable)
class GDATTEST_API ADWEnemyCharacter : public ACharacter
{
    GENERATED_BODY()
public:
    /** Pawn Owner becomes its AIController after possession; keep the spawning nest separately. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Enemy|Save") TObjectPtr<ADWEnemyNest> SourceNest;

    ADWEnemyCharacter();
    /** Legacy preserves existing instance/variant tuning. New profiles use only the formal numeric source. */
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Enemy|Formal Combat") EDWEnemyCombatProfile CombatProfile=EDWEnemyCombatProfile::Legacy;
    /** Navigation technique, retaining the old 35 cm / 0.35 s values; not an ecology rule. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Enemy|Navigation",meta=(ClampMin="0",Units="cm")) float NavigationAcceptanceRadius=35.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Enemy|Navigation",meta=(ClampMin="0",Units="s")) float NavigationRepathInterval=.35f;
    void BeginPlayerDeathReturnFacts();
    bool IsReturningForPlayerDeath()const{return bPlayerDeathReturn;}
    bool InitializeFormalCombat(const FDWCombatRules& Rules,FString& Error);
    UFUNCTION(BlueprintPure,Category="Enemy|Formal Combat") bool IsFormalCombatReady()const{return bFormalCombatReady&&CombatProfile==FormalCombatConfig.Profile;}
    UFUNCTION(BlueprintPure,Category="Enemy|Formal Combat") bool HasRememberedPlayerCombat()const{return FormalMemory.bEngaged;}
    UFUNCTION(BlueprintPure,Category="Enemy|Formal Combat") bool IsHostileToCombatPlayer(const ADWPlayerCharacter* Player)const;
    double GetDaggerAlertRadius()const;bool IsEngagedForDagger()const;
    void SyncDaggerAwareness(ADWPlayerCharacter*,bool EquipmentChange=false);
    bool HasDaggerDiscoveryGrace(const ADWPlayerCharacter*)const;
    bool IsEligibleForDaggerBackstab(ADWPlayerCharacter*,double BackArc);
    FDWDaggerAwarenessSave CaptureDaggerAwarenessSave()const;
    bool RestoreDaggerAwarenessSave(const FDWDaggerAwarenessSave&,ADWPlayerCharacter*);
    FDWEnemyFormalSaveData CaptureFormalCombatSave()const;
    bool RestoreFormalCombatSave(const FDWEnemyFormalSaveData& Data,float SavedHealth,FString& Error);
    FDWEnemyForcedFallSave CaptureForcedFallSave()const;
    bool RestoreForcedFallBeforeBeginPlay(const FDWEnemyForcedFallSave&,FString& Error);
    virtual void FellOutOfWorld(const UDamageType& DamageType) override;
    virtual void Landed(const FHitResult& Hit) override;
    virtual void OnMovementModeChanged(EMovementMode Previous,uint8 PreviousCustom=0) override;
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
    /** Telegraph before a newly detected enemy starts chasing or attacking. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|AI", meta=(ClampMin="0", Units="s")) float AlertDuration = 0.45f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="0", Units="s")) float AttackWindup = 0.55f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="0.01", Units="s")) float AttackActiveDuration = 0.12f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="0", Units="s")) float AttackRecovery = 0.4f;
    /** Half of the forward hit arc. Facing tracks during windup, then locks for the strike. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="1", ClampMax="180", Units="deg")) float AttackHalfAngle = 65.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Enemy|Combat") EDWCombatPhase AttackPhase = EDWCombatPhase::None;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Enemy|Combat") int32 AttackSerial = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Enemy|Combat") bool bIsCombatKnockedBack = false;
    /** Development/PIE wireframe telegraph. It never participates in damage or collision. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat") bool bShowCombatDebug = true;
    /** Visual-only squash/lean on the existing body. Disable after replacing it with final art. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation") bool bAnimatePlaceholder = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(ClampMin="0.1", Units="s")) float DeathDestroyDelay = 1.5f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Effects") TObjectPtr<USoundBase> AttackSound;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Effects") TObjectPtr<USoundBase> DeathSound;

    /** The combat AnimInstance reads these slots; native combat never calls PlayAnimation. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation") bool bUseCombatAnimBlueprint = true;
    /** Animation's authored travel speed in cm/s. Zero keeps legacy fixed playback; positive enables speed matching. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation", meta=(ClampMin="0", Units="cm/s")) float MoveAnimationReferenceSpeed = 0.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation") TObjectPtr<UAnimationAsset> IdleAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation") TObjectPtr<UAnimationAsset> MoveAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation") TObjectPtr<UAnimationAsset> AlertAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation") TObjectPtr<UAnimationAsset> AttackAnimation;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Animation") TObjectPtr<UAnimationAsset> DeathAnimation;
    /** Empty or no positive finite weight keeps the ordinary attack. AttackRange remains the approach/start gate.
        A chosen variant's range applies to its strike; normally keep AttackRange <= each variant's Range. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Enemy|Combat", meta=(TitleProperty="Animation")) TArray<FDWEnemyAttackVariant> AttackVariants;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Transient, Category="Enemy|Combat") int32 CurrentAttackVariantIndex = INDEX_NONE;

    UFUNCTION(BlueprintPure, Category="Enemy") bool IsAlive() const { return FMath::IsFinite(Health) && Health > 0.f && State != EDWEnemyState::Dead; }
    UFUNCTION(BlueprintPure, Category="Enemy|Animation") EDWCombatAnimationState GetCombatAnimationState() const;
    UFUNCTION(BlueprintPure, Category="Enemy|Animation") float GetCombatAttackDuration() const { return CurrentAttackWindup + CurrentAttackActiveDuration + CurrentAttackRecovery; }
    UFUNCTION(BlueprintPure, Category="Enemy|Animation") float GetCombatAttackElapsedSeconds() const;
    /** The selected animation is retained during the outgoing blend and changes only on the next accepted attack/load. */
    UFUNCTION(BlueprintPure, Category="Enemy|Animation") UAnimationAsset* GetCurrentAttackAnimation() const;
    UFUNCTION(BlueprintCallable, Category="Enemy|Combat") bool TryStartMeleeAttack(AActor* Target);
    UFUNCTION(BlueprintCallable, Category="Enemy|Combat") void CancelMeleeAttack();
    /** Hard geometric and life checks are independent of the Blueprint eligibility hook. */
    UFUNCTION(BlueprintPure, Category="Enemy|Combat") bool CanResolveMeleeHit(AActor* Target) const;
    /** Distance is the maximum unobstructed horizontal travel, in cm. A wall shortens it. */
    UFUNCTION(BlueprintCallable, Category="Enemy|Combat") void ApplyCombatKnockback(FVector Direction, float Distance, float Duration);
    /** Percent uses 0..100 units. Overlaps retain the strongest unexpired effect. */
    UFUNCTION(BlueprintCallable, Category="Enemy|Combat") void ApplySlow(float Percent, float Duration);
    void RefreshCombatGroundAreaSlow();
    double GetCombatGroundAreaSlowFraction()const;
    // Separate environmental sources never overwrite or expire combat SlowEffects.
    void SetGatheringEnvironmentSlow(const AActor* Source,double Fraction);
    void RemoveGatheringEnvironmentSlow(const AActor* Source);
    double GetGatheringEnvironmentMovementMultiplier() const;
    UFUNCTION(BlueprintCallable, Category="Enemy|Save") void SetHealthForLoad(float NewHealth);
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Enemy|Rules") bool CanDetectTarget(AActor* Target) const;
    virtual bool CanDetectTarget_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Enemy|Rules") bool CanAttackTarget(AActor* Target) const;
    virtual bool CanAttackTarget_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Enemy|Rules") float GetAttackDamage(AActor* Target) const;
    virtual float GetAttackDamage_Implementation(AActor* Target) const;
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Events") void OnStateChanged(EDWEnemyState PreviousState, EDWEnemyState NewState);
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Events") void OnAttackStarted(AActor* Victim);
    /** After the authoritative damage call. ActualDamage is zero if the victim rejected the hit. */
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Events") void OnAttackResolved(AActor* Victim, float ActualDamage);
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Effects") void OnEnemyAttack(AActor* Victim);
    UFUNCTION(BlueprintImplementableEvent, Category="Enemy|Effects") void OnEnemyDied();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    void Die(bool bFromSave = false);
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class FDWCombatWorldObjectsRestorePlan;
    bool bCombatWorldStaged=false;
    FString CombatWorldStableId;

    friend class UDWCombatAuthoritySubsystem;
    friend class UDWCombatCoreDropSubsystem;
    friend class FDWCombatCorePickupTransaction;
    FDWEnemyCombatConfig FormalCombatConfig;
    FDWEnemyCoreDropConfig FormalCoreDropConfig;
    friend class UDWWeaponAnimationComponent;
    FDWDaggerAwareness DaggerAwareness;DWCombatCommit::FEntityLife DaggerPlayerLife;
    TWeakObjectPtr<ADWPlayerCharacter> PendingDaggerAlertTarget;
    void PublishDaggerDamageAlertEffects();
    void RecordDaggerDamageFacts(AActor* Source,bool SourceWasPlayer);
    void RefreshDaggerGraceFacts(ADWPlayerCharacter*,double Now,double Grace);
    void DrawDaggerAlertRing(ADWPlayerCharacter*)const;
    FDWEnemyCombatMemory FormalMemory;
    FDWEnemyCoreDropRecord FormalCoreDrop;
    FDWEnemyForcedFall ForcedFall;
    bool FindStandableFallPoint(FVector Position,FVector& Out)const;
    void BeginForcedFallMove();
    void CompleteForcedFallMove();
    void ObserveForcedFallLanding();
    bool CaptureForcedFallCredit(DWCombatCommit::FEntityLife& Player,FVector& Point);
    FGuid FormalEnemyId;
    bool bFormalCombatReady=false,bFormalHomeCaptured=false,bFormalFacingLocked=false,bFormalGeneratedPersistentId=false;
    bool bFormalNeedsChaseState=false,bFormalAttackContinuesDuringKnockback=false;
    double FormalAlertEndTime=0.,FormalAttackStartedTime=0.,FormalPhaseEndTime=0.,FormalNextAttackTime=0.;
    double FormalKnockbackInitialDistance=0.,FormalKnockbackCommittedTravelThisFrame=0.;
    uint64 FormalKnockbackFrame=MAX_uint64,FormalObservedPlayerDamageRevision=0,FormalKnockbackRevision=0;
    DWCombatCommit::FActionId FormalMeleeAction;
    DWCombatCommit::FEntityLife FormalMeleeTargetLife,FormalObservedPlayerLife;
    bool bPlayerDeathReturn=false;
    bool TickPlayerDeathReturn(float DeltaSeconds);
    bool FormalCombatOperational()const;
    bool CanResolveFormalMeleeHit(AActor* Target,bool bFacing)const;
    void TickFormalCombat(float DeltaSeconds);
    bool TryStartFormalMeleeAttack(AActor* Target);
    void UpdateFormalMeleeAttack(double Now);
    void ResolveFormalMeleeHit();
    FDWEnemyFormalKnockback PrepareFormalKnockback(FVector Direction,float Distance,float Seconds)const;
    void CommitFormalKnockbackFacts(const FDWEnemyFormalKnockback& Snapshot,
        DWCombatCommit::FReceiptId Receipt={},DWCombatCommit::FEntityLife Player={},DWCombatCommit::FEntityLife Enemy={});
    bool UpdateFormalKnockback(float DeltaSeconds);
    void RecordFormalDamageFacts(AActor* Source,DWCombatCommit::FEntityLife SourceLife,DWCombatCommit::FReceiptId Receipt,
        DWCombatCommit::FEntityLife TargetLife,bool bFatal,bool bSourceWasPlayer);
    bool HasSightTo(const AActor* Target) const;
    void ChoosePatrolGoal();
    void MoveToward(const FVector& Goal, float DeltaSeconds);
    void StopAIMovement();
    void UpdateSlowEffects();
    void SetAIState(EDWEnemyState NewState);
    void FinishPersistentDeath();
    void UpdateMeleeAttack(float Now);
    void ResolveMeleeHit();
    void UpdateCombatKnockback(float DeltaSeconds);
    void DrawCombatDebug() const;
    void UpdatePlaceholderPresentation();
    void RestorePlaceholderPresentation();
    bool IsCombatTargetAlive(const AActor* Target) const;
    void BeginAlert(APawn* NewTarget);
    TWeakObjectPtr<APawn> TargetPawn;
    TWeakObjectPtr<AActor> MeleeTarget;
    FVector LockedAttackDirection = FVector::ForwardVector;
    FVector KnockbackDirection = FVector::ZeroVector;
    float AttackPhaseEndTime = 0.f;
    float AttackStartedTime = 0.f;
    float CurrentAttackWindup = 0.f;
    float CurrentAttackActiveDuration = 0.f;
    float CurrentAttackRecovery = 0.f;
    float CurrentAttackRange = 150.f;
    float CurrentAttackDamage = 8.f;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> CurrentAttackAnimation;
    bool bHasAttackSnapshot = false;
    float AlertEndTime = 0.f;
    float KnockbackDistanceRemaining = 0.f;
    float KnockbackTimeRemaining = 0.f;
    float KnockbackSpeed = 0.f;
    bool bAttackResolved = false;
    bool bStartingMeleeAttack = false;
    bool bDamageInProgress = false;
    bool bDeathTransitionInProgress = false;
    bool bCombatDeathEffectsPending = false;
    EDWEnemyState CombatDeathPreviousState = EDWEnemyState::Patrol;
    void CommitCombatDeathFacts();
    void PublishCombatDeathEffects(bool bFromSave = false);
    bool bCombatRestoreInProgress = false;
    bool bPlaceholderTransformCaptured = false;
    bool bPlaceholderPresentationApplied = false;
    FTransform PlaceholderBaseTransform = FTransform::Identity;
    bool bHasPreDeathState = false;
    bool bPreDeathCollision = true;
    bool bPreDeathHidden = false;
    bool bPreDeathTick = true;
    bool bPreDeathMeshTick = true;
    uint8 PreDeathMovementMode = 1;
    uint8 PreDeathCustomMovementMode = 0;
    FVector HomeLocation = FVector::ZeroVector;
    FVector PatrolGoal = FVector::ZeroVector;
    float NextPatrolTime = 0.f;
    float NextAttackTime = 0.f;
    float NextPathRequestTime = 0.f;
    bool bHasPatrolGoal = false;
    TArray<FVector2D> SlowEffects;
    TMap<TWeakObjectPtr<AActor>,double> GatheringEnvironmentSlowSources;
    FTimerHandle DeathCleanupTimer;
    bool bHealthRestoredFromSave = false;
};
