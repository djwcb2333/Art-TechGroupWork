#pragma once
#include "CoreMinimal.h"
#include "TopDownActionCharacter.h"
#include "DWCombatTypes.h"
#include "DWCombatKnockback.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatPlayerVitals.h"
#include "DWCombatMoldRoot.h"
#include "DWYeastDashPlan.h"
#include "DWCombatInputLedger.h"
#include "DWCombatCoreInjectionTransaction.h"
#include "DWPlayerCharacter.generated.h"

class UDWGatheringComponent;
class UDWWeaponAnimationComponent;
class UDWCombatFeedbackComponent;
class UDWGameplayCameraShakeComponent;
class UDWGameplayConfig; class UDWInventoryComponent; class UDWSaveGame;
class ADWResourceNode; class ADWAlcoholProjectile; class UAnimationAsset;
class USpringArmComponent; class USoundBase; class UCameraShakeBase; class UNiagaraSystem;
class UStaticMeshComponent; class UAudioComponent; class UNiagaraComponent;
class UBoxComponent;
class ADWGatheringPond;

/** Uses the existing TopDown action-character movement; gameplay is independent of the imported programming pawn. */
UCLASS(Blueprintable)
class GDATTEST_API ADWPlayerCharacter : public ATopDownActionCharacter
{
    GENERATED_BODY()
public:
    /** Settle the spring arm before the loading cover is removed, even while paused. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Camera") void PrepareCameraForReveal();
    ADWPlayerCharacter();
    /** Editor PIE, exact CombatTest map and no adventure slot only. No shipping/adventure grant. */
    /** Grant missing permanent test weapons once; independent of the ammo toggle. Editor CombatTest without an adventure slot only. */
    UPROPERTY(EditAnywhere,Category="DoughWorld|Combat|Testing") bool GiveCombatTestWeapons=true;
    /** Editor-only exact CombatTest map/no-slot supply. Does not enter either form or bypass inventory Use. */
    UPROPERTY(EditAnywhere,Category="DoughWorld|Combat|Testing") bool GiveCombatTestCores=true;
    UPROPERTY(EditAnywhere,Category="DoughWorld|Combat|Testing",meta=(ClampMin="0",ClampMax="1000")) int32 CombatTestCoresPerType=10;
    UPROPERTY(EditAnywhere,Category="DoughWorld|Combat|Testing") bool GiveCombatTestAmmo=true;
    UPROPERTY(EditAnywhere,Category="DoughWorld|Combat|Testing",meta=(ClampMin="0",ClampMax="1000")) int32 CombatTestAmmoPerType=100;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Root") bool IsFormallyRooted()const{return FormalRoot.IsActive();}
    bool TryToggleFormalRoot();
    bool IsFormallyRootedAtFrameOffset(double Seconds)const;
    bool GetFormalRootIdentity(FDWMoldRootIdentity& Out)const;
    FDWMoldRootSnapshot CaptureFormalRootSnapshot()const{return FormalRoot.Capture();}
    double GetFormalRootDamageMultiplier()const{return IsFormallyRooted()?FormalRoot.GetConfig().DamageMultiplier:1.;}
    double GetFormalRootRangedRateMultiplier()const{return IsFormallyRooted()?FormalRoot.GetConfig().RangedRateMultiplier:1.;}
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Root|Animation") TObjectPtr<UAnimationAsset> RootEnterAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Root|Animation") TObjectPtr<UAnimationAsset> RootExitAnimation;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Dash") bool IsFormalDashing()const{return FormalDash.IsActive();}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Dash") double GetFormalDashCooldown()const{return FormalDash.CooldownSeconds();}
    float GetMovementCameraYaw()const{return CameraYaw;}
    bool TryStartFormalDash(const FDWCombatInputPress&,FVector MoveDirection,FVector AimDirection);
    void PublishFormalDashStartEffects(); // Call after D02 binds the accepted hold or observes its quick-tap release.
    bool ApplyFormalDashRelease(const FDWCombatInputPress&);
    void CancelFormalDashInput(const FDWCombatInputPress&);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat") bool IsCombatKnockedBack()const{return CombatKnockback.bActive;}
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Feedback") TObjectPtr<UDWCombatFeedbackComponent> CombatFeedback;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Camera") TObjectPtr<UDWGameplayCameraShakeComponent> GameplayCameraShake;
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Inventory") TObjectPtr<UDWInventoryComponent> Inventory;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Gathering") TObjectPtr<UDWGatheringComponent> GatheringComponent;
    bool CanPerformGatheringAction() const;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Equipment") TObjectPtr<UStaticMeshComponent> SwordPlaceholder;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Equipment") TObjectPtr<UDWWeaponAnimationComponent> WeaponAnimationComponent;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Equipment") FName SwordSocket=TEXT("bone_005_R");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Config") TObjectPtr<UDWGameplayConfig> GameplayConfig;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> IdleAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> WalkAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> AttackAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> TransformAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> DeathAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> DashAnimation;
    /** Enables the authored combat Animation Blueprint. Native single-node playback is completely bypassed. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") bool bUseCombatAnimBlueprint=false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> MeleeAnimation;
    /** Empty steps or a disabled combo preserve all legacy single-strike fields below. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Combo") bool bEnableMeleeCombo=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Combo",meta=(TitleProperty="Animation")) TArray<FDWMeleeComboStep> MeleeComboSteps;
    /** Accept one next press only within this many seconds of the recovery end, never during windup/active. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Combo",meta=(ClampMin="0",Units="s")) float MeleeComboInputBufferSeconds=.25f;
    /** After the next step becomes ready, an unbuffered press can continue the chain for this duration. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Combo",meta=(ClampMin="0",Units="s")) float MeleeComboResetSeconds=.65f;
    /** Extra wait between steps. The last step instead uses the existing Melee Cooldown. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Combo",meta=(ClampMin="0",Units="s")) float MeleeComboStepCooldown=.04f;
    /** Zero-based active step, or -1 while idle / using the legacy single strike. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Combat|Combo") int32 CurrentMeleeComboStep=INDEX_NONE;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Combat|Combo") bool bMeleeComboInputBuffered=false;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Animation") UAnimationAsset* GetCurrentMeleeAnimation() const;
    /** Optional deform bone for a weapon contained in the character mesh. None preserves the old static weapon behavior. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Combo") FName AnimatedMeleeWeaponBone=NAME_None;
    /** During melee only, show a reversible weapon swing when the animation or weapon socket is missing. Does not affect damage. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Placeholder",meta=(DisplayName="Enable Melee Weapon Placeholder")) bool bEnableMeleeWeaponPlaceholder=true;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Combat|Placeholder") bool bMeleeWeaponPlaceholderActive=false;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Animation") EDWCombatAnimationState GetCombatAnimationState() const;
    UAnimationAsset* GetCombatThrowSequence() const {return ActiveAttackClip;}
    float GetCombatThrowStartSeconds() const {return ActiveAttackStartSeconds;}
    float GetCombatThrowEndSeconds() const {return ActiveAttackEndSeconds;}
    float GetCombatThrowPlayRate() const {return ActiveAttackAnimationRate;}
    float GetCombatThrowElapsedSeconds() const;
    int32 GetCombatThrowSerial() const {return ThrowAnimationSerial;}
    float GetMeleeAnimationElapsedSeconds() const;
    float GetMeleeAnimationDuration() const;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Combat|Melee") TObjectPtr<UBoxComponent> MeleeDamageVolume;
    /** Length forward from capsule centre. The box follows the locked attack direction, not weapon art. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="1",Units="cm")) float MeleeRange=180.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="1",Units="cm")) float MeleeHalfWidth=65.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="1",Units="cm")) float MeleeHalfHeight=70.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="0")) float MeleeDamage=25.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="0",Units="s")) float MeleeWindupSeconds=.2f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="0.01",Units="s")) float MeleeActiveSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="0",Units="s")) float MeleeRecoverySeconds=.3f;
    /** Extra delay after recovery; one button press starts at most one attack. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="0",Units="s")) float MeleeCooldown=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="0",Units="cm")) float MeleeKnockbackDistance=80.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Melee",meta=(ClampMin="0.01",Units="s")) float MeleeKnockbackSeconds=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Dash") bool bEnableDashDamage=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Dash",meta=(ClampMin="0")) float DashDamage=35.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Dash",meta=(ClampMin="1",Units="cm")) float DashDamageRadius=60.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Dash",meta=(ClampMin="0",Units="cm")) float DashKnockbackDistance=250.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Dash",meta=(ClampMin="0.01",Units="s")) float DashKnockbackSeconds=.25f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat") bool bCombatRequireLineOfSight=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat|Debug") bool bDrawCombatDebug=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Combat") EDWCombatPhase AttackPhase=EDWCombatPhase::None;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Combat") int32 AttackSerial=0;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Combat") bool PerformMeleeAttack(FVector TargetLocation);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Combat") void CancelCombatActions();
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat") bool IsCombatActionAllowed() const;
    bool IsCombatSimulationSuspended() const;
    /** Actual transformed hurtbox query, exposed for isolated geometry and gameplay verification. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Combat") TArray<AActor*> CollectMeleeTargets();
    /** Natural clip playback multiplier. 1 plays the source speed; 0.75 is slower. Sampled once per successful throw; does not change Throw Cooldown. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation|Attack",meta=(DisplayName="Attack Animation Play Rate",ClampMin="0.05",ClampMax="4",UIMin="0.25",UIMax="2")) float AttackAnimationPlayRate=1.f;
    /** Source clip time to begin each accepted attack. Use a single cycle when the imported source contains several baked swings. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation|Attack",meta=(DisplayName="Attack Clip Start Seconds",ClampMin="0",Units="s")) float AttackAnimationStartSeconds=0.f;
    /** Source clip end time, not a target duration. 0 uses the full clip. Playback stops here and returns to idle/movement without repeating the selected segment. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation|Attack",meta=(DisplayName="Attack Clip End Seconds",ClampMin="0",Units="s")) float AttackAnimationEndSeconds=0.f;
    /** Legacy serialized value; used only when Attack Animation is not a sequence with a valid length. Real clips finish according to their length and Attack Animation Play Rate. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation|Attack",meta=(ClampMin="0.05",DeprecatedProperty,DeprecationMessage="For animation sequences use Attack Animation Play Rate; full clips are no longer compressed into this duration.")) float AttackAnimationSeconds=0.55f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Animation|Diagnostics") bool bPlayingAttackAnimation=false;
    /** Counts starts of an attack clip, not frames or damage hits. Useful for verifying one start per accepted input press. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Animation|Diagnostics") int32 AttackAnimationPlayCount=0;
    /** Current source-clip position while the attack is selected; -1 outside attack playback. */
    UFUNCTION(BlueprintPure,Category="DoughWorld|Animation|Diagnostics") float GetAttackAnimationPositionSeconds() const;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation",meta=(ClampMin="0.05")) float TransformAnimationSeconds=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat") TSubclassOf<ADWAlcoholProjectile> AlcoholProjectileClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat",meta=(ClampMin="1")) int32 AlcoholCostPerThrow=1;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat",meta=(ClampMin="0.05",Units="s")) float ThrowCooldown=0.65f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat",meta=(ClampMin="100",Units="cm")) float MaxThrowRange=1600.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Combat") FVector ThrowOriginOffset=FVector(55.f,0.f,65.f);
    /** Plays only after a projectile was spawned and its inventory cost was successfully consumed. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Throw") TObjectPtr<USoundBase> ThrowSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Throw",meta=(ClampMin="0",ClampMax="4")) float ThrowSoundVolume=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Throw",meta=(ClampMin="0.25",ClampMax="4")) float ThrowSoundPitch=1.f;
    /** Degrees per raw mouse movement unit. Player settings multiply this value; never multiply a mouse delta by frame time. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="0.01",ClampMax="4")) float CameraDragSensitivity=0.4f;
    /** Initial / requested spring-arm length. Runtime zoom remains within the editable distance limits. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="100",Units="cm")) float CameraDistance=1800.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="100",Units="cm")) float MinCameraDistance=600.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="100",Units="cm")) float MaxCameraDistance=3000.f;
    /** Positive wheel input moves closer by this many centimeters per wheel unit, independent of frame time. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="1",Units="cm")) float CameraZoomStep=150.f;
    /** Exponential interpolation speed. Zero applies the target immediately; larger positive values settle faster. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="0")) float CameraZoomInterpSpeed=10.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera") float MinCameraPitch=-80.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera") float MaxCameraPitch=-25.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation") TObjectPtr<USoundBase> TransformSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation") TSubclassOf<UCameraShakeBase> TransformCameraShake;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation",meta=(ClampMin="0.01")) float TransformShakeSeconds=0.35f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation",meta=(ClampMin="0")) float TransformShakeMagnitude=8.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation",meta=(ClampMin="1")) float TransformShakeFrequency=35.f;
    /** Material multipliers: preserve the source atlas and change only its FormTint parameter. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual") FLinearColor YeastTint=FLinearColor(1.f,0.36f,0.065f,1.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual") FLinearColor DoughTint=FLinearColor::White;
    /** Matches the authored Transform root's final scale, but persists across all yeast animations. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual",meta=(ClampMin="0.1",UIMin="1",UIMax="3")) float YeastFormScaleMultiplier=1.36446f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual") bool bMatchFormEnterBlendToAnimation=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual",meta=(ClampMin="0.01",Units="s",EditCondition="!bMatchFormEnterBlendToAnimation")) float FormEnterBlendSeconds=1.6333333f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual",meta=(ClampMin="0.01",Units="s")) float FormExitBlendSeconds=0.45f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual",meta=(ClampMin="1",UIMax="5")) float FormBlendExponent=2.f;
    /** Cancel authored root scale from the actual current pose, including frames at clip boundaries. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual") bool bCompensateTransformRootScale=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Transformation|Visual") FName FormScaleRootBone=TEXT("root");
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Transformation|Visual") float CurrentFormVisualScale=1.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Transformation|Visual") float CurrentFormBlendAlpha=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Transformation|Visual") FVector CurrentAnimationRootScale=FVector::OneVector;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Transformation") float GetCurrentFormVisualScale() const {return CurrentFormVisualScale;}
    /** Sustained dash trail. Use a world-space emitter; the component follows the capsule. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Trail",meta=(DisplayName="Dash Trail System")) TObjectPtr<UNiagaraSystem> DashNiagara;
    /** Legacy serialization only. Mesh-actor trails are disabled; use the independent Niagara dash slot. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Dash",meta=(DeprecatedProperty,DeprecationMessage="Use Dash Trail System. Legacy mesh-actor trails are no longer spawned.")) TSubclassOf<AActor> DashTrailClass;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Dash",meta=(ClampMin="0.01")) float DashTrailInterval=0.055f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX",meta=(DisplayName="Enable Player VFX")) bool bEnablePlayerVFX=true;
    /** Independent replaceable slot. Use a looping world-space system; optional spawn-rate control preserves old particles across stop/start. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust",meta=(DisplayName="Movement Dust System")) TObjectPtr<UNiagaraSystem> MovementDustNiagara;
    /** Independent optional body effect, emitted only during actual Shift sprinting, never while walking or dashing. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Sprint Body",meta=(DisplayName="Sprint Body System")) TObjectPtr<UNiagaraSystem> RunTrailNiagara;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement",meta=(ClampMin="0",Units="cm/s")) float MovementVFXMinSpeed=10.f;
    /** Reject teleports/save restoration; ordinary dash movement remains below this per-frame distance. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement",meta=(ClampMin="1",Units="cm")) float MovementVFXMaxSampleDistance=600.f;
    /** Legacy compatibility field. The Sprint Body System is now always sprint-only regardless of this old setting. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Sprint Body",meta=(DeprecatedProperty,DeprecationMessage="Sprint Body System always emits only during actual sprinting.",DisplayName="Legacy Sprint Only")) bool bRunTrailOnlyWhileSprinting=true;
    /** Offsets are relative to the capsule's foot, independent of imported skeletal-mesh orientation. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust") FVector MovementDustOffset=FVector(-12.f,0.f,3.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust") FRotator MovementDustRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust") FVector MovementDustScale=FVector::OneVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Sprint Body",meta=(DisplayName="Sprint Body Offset")) FVector RunTrailOffset=FVector(0.f,0.f,60.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Sprint Body",meta=(DisplayName="Sprint Body Rotation")) FRotator RunTrailRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Sprint Body",meta=(DisplayName="Sprint Body Scale")) FVector RunTrailScale=FVector::OneVector;
    /** Optional exposed Niagara float parameter bound to ALL dust spawn-rate modules (e.g. User.DWSpawnRate). At rest it receives 0; the live system is not reset. None, a missing parameter, or the wrong type automatically uses Activate/Deactivate for third-party systems. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust",meta=(DisplayName="Dust Spawn Rate Parameter")) FName MovementDustSpawnRateParameter=TEXT("User.DWSpawnRate");
    /** Optional exposed Niagara Float Vector3 parameter. Receives normalized world-space -ActorForwardVector, not the camera or travel direction. Missing/wrong-type parameters leave the source system unchanged. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust",meta=(DisplayName="Dust Backward Direction Parameter")) FName MovementDustBackwardDirectionParameter=TEXT("User.DWBackwardDirection");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust",meta=(DisplayName="Dust Spawn Rate",ClampMin="0")) float MovementDustSpawnRate=12.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Movement Dust",meta=(DisplayName="Sprint Dust Spawn Rate Multiplier",ClampMin="0")) float SprintDustSpawnRateMultiplier=1.4f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Trail") FVector DashTrailOffset=FVector(-20.f,0.f,35.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Trail") FRotator DashTrailRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Trail") FVector DashTrailScale=FVector::OneVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Start",meta=(DisplayName="Dash Start Burst System")) TObjectPtr<UNiagaraSystem> DashStartNiagara;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Start") FVector DashStartOffset=FVector(0.f,0.f,3.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Start") FRotator DashStartRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Start") FVector DashStartScale=FVector::OneVector;
    /** Stop emission even if a looping source asset was assigned accidentally. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Start",meta=(DisplayName="Dash Start Emission Seconds",ClampMin="0.01",Units="s")) float DashStartVFXMaxSeconds=0.6f;
    /** After emission stops, allow this many seconds for existing particles, then forcibly clear the burst. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Dash Start",meta=(DisplayName="Dash Start Tail Cleanup Seconds",ClampMin="0",Units="s")) float DashStartVFXTailSeconds=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Dash") TObjectPtr<USoundBase> DashStartSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Dash",meta=(ClampMin="0",ClampMax="4")) float DashStartSoundVolume=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Dash",meta=(ClampMin="0.25",ClampMax="4")) float DashStartSoundPitch=1.f;
    /** One reused component plays this same sound for walking and sprinting. Non-looping sources replay on a later tick only while movement remains valid. Independent of Enable Player VFX. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Movement",meta=(DisplayName="Movement Loop Sound")) TObjectPtr<USoundBase> MovementLoopSound;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Movement",meta=(DisplayName="Movement Sound Volume",ClampMin="0",ClampMax="4")) float MovementSoundVolume=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Movement",meta=(DisplayName="Movement Sound Base Pitch",ClampMin="0.25",ClampMax="4")) float MovementSoundPitch=1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Audio|Movement",meta=(DisplayName="Sprint Movement Pitch Multiplier",ClampMin="0.25",ClampMax="4")) float SprintMovementPitchMultiplier=1.2f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Audio|Diagnostics") bool bMovementAudioDesired=false;
    /** Pitch requested for the current movement loop; source playback still requires an assigned sound and a valid audio device. */
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|Audio|Diagnostics") float CurrentMovementAudioPitch=1.f;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Audio") UAudioComponent* GetMovementAudioComponent() const {return MovementAudioComponent;}
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation",meta=(DisplayName="Transform Enter Burst System")) TObjectPtr<UNiagaraSystem> TransformEnterNiagara;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation",meta=(DisplayName="Transform Exit Burst System")) TObjectPtr<UNiagaraSystem> TransformExitNiagara;
    /** Follow the capsule for a body effect. Its emitter must also use Local Space if already-born particles should travel with the character. Disable for a ground-anchored ring. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation",meta=(DisplayName="Transform VFX Follow Character")) bool bTransformVFXFollowCharacter=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation") FVector TransformVFXOffset=FVector(0.f,0.f,25.f);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation") FRotator TransformVFXRotation=FRotator::ZeroRotator;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation") FVector TransformVFXScale=FVector::OneVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation",meta=(DisplayName="Transform Emission Seconds",ClampMin="0.01",Units="s")) float TransformBurstVFXMaxSeconds=1.75f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation",meta=(DisplayName="Transform Tail Cleanup Seconds",ClampMin="0",Units="s")) float TransformBurstVFXTailSeconds=1.f;
    /** Optional LinearColor user parameter: enter receives YeastTint (orange), exit receives DoughTint. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation") FName TransformVFXColorParameter=NAME_None;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|VFX|Transformation") TObjectPtr<USoundBase> TransformExitSound;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|VFX|Diagnostics") float CurrentVFXGroundSpeed=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|VFX|Diagnostics") bool bMovementDustEmitting=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|VFX|Diagnostics") bool bRunTrailEmitting=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="DoughWorld|VFX|Diagnostics") bool bDashTrailEmitting=false;
    UFUNCTION(BlueprintPure,Category="DoughWorld|VFX") UNiagaraComponent* GetMovementDustVFX() const {return MovementDustComponent;}
    UFUNCTION(BlueprintPure,Category="DoughWorld|VFX") UNiagaraComponent* GetRunTrailVFX() const {return RunTrailComponent;}
    UFUNCTION(BlueprintPure,Category="DoughWorld|VFX") UNiagaraComponent* GetDashTrailVFX() const {return DashTrailComponent;}
    /** Called automatically before blocking menus; Immediate also clears existing particles. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|VFX") void StopPlayerVFX(bool bImmediate=true);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Interaction",meta=(ClampMin="0.01")) float InteractionScanInterval=0.12f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="DoughWorld|State") float Health=100.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="DoughWorld|State") float Transformation=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="DoughWorld|State") float SprintAlcoholElapsed=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="DoughWorld|State") float TransformationDecayElapsed=0.f;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="DoughWorld|State") bool bYeastForm=false;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Category="DoughWorld|State") bool bSprinting=false;
    UFUNCTION(BlueprintPure,Category="DoughWorld") UDWGameplayConfig* GetGameplayConfig() const {return GameplayConfig;}
    UFUNCTION(BlueprintPure,Category="DoughWorld") UDWInventoryComponent* GetInventory() const {return Inventory;}
    UFUNCTION(BlueprintPure,Category="DoughWorld") float GetHealth() const {return Health;}
    UFUNCTION(BlueprintPure,Category="DoughWorld") float GetTransformation() const {return Transformation;}
    UFUNCTION(BlueprintPure,Category="DoughWorld") float GetSprintAlcoholProgress() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld") bool IsYeastForm() const {return bYeastForm;}
    UFUNCTION(BlueprintPure,Category="DoughWorld") bool IsDead() const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld") bool UseInventoryItem(int32 SlotIndex);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Core Injection") bool IsCoreInjecting() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Core Injection") float GetCoreInjectionRemainingSeconds() const;
    // Native exact-slot entry shared by inventory UI and shortcut use. No immediate core grant.
    bool CanRequestCoreInjection(const FDWInventorySlotSnapshot& Slot,bool bAllowPausedMenu=false) const;
    bool RequestCoreInjection(const FDWInventorySlotSnapshot& Slot);
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation|Core Injection") TObjectPtr<UAnimationAsset> CoreInjectionAnimation;
    UAnimationAsset* GetCoreInjectionAnimationForPlayback() const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld") bool CraftRecipe(FName RecipeId);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Recovery") bool CanCombatRespawn()const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Recovery") bool RequestCombatRespawn();
    UFUNCTION(BlueprintCallable,Category="DoughWorld") void RequestSave();
    UFUNCTION(BlueprintCallable,Category="DoughWorld") void NotifyAlcoholHit();
    UFUNCTION(BlueprintCallable,Category="DoughWorld") void TryTransform();
    UFUNCTION(BlueprintCallable,Category="DoughWorld") void PerformDash();
    UFUNCTION(BlueprintCallable,Category="DoughWorld") bool ThrowAlcoholAt(FVector Target);
    UFUNCTION(BlueprintCallable,Category="DoughWorld") void SetSprintHeld(bool bHeld) {bSprintHeld=bHeld;}
    UFUNCTION(BlueprintCallable,Category="DoughWorld") void SetHarvestHeld(bool bHeld);
    /** Call before pausing/opening any blocking menu; feedback stops without waiting for a player tick. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Interaction") void CancelHarvestInteraction();
    UFUNCTION(BlueprintPure,Category="DoughWorld|Interaction") UAudioComponent* GetGatherLoopAudio() const {return GatherLoopComponent;}
    UFUNCTION(BlueprintPure,Category="DoughWorld") FText GetInteractionPrompt() const;
    /** Validates current distance and availability again, between the periodic focus scans. */
    UFUNCTION(BlueprintPure,Category="DoughWorld|Interaction") ADWResourceNode* GetFocusedResource() const;
    UFUNCTION(BlueprintPure,Category="DoughWorld|Interaction") float GetHarvestProgress() const;

    /** Additional Blueprint permission only; C++ still enforces action state and inventory rules. */
    UFUNCTION(BlueprintNativeEvent,BlueprintPure,Category="DoughWorld|Rules") bool CanSprint() const;
    virtual bool CanSprint_Implementation() const;
    UFUNCTION(BlueprintNativeEvent,BlueprintPure,Category="DoughWorld|Rules") bool CanDash() const;
    virtual bool CanDash_Implementation() const;
    UFUNCTION(BlueprintNativeEvent,BlueprintPure,Category="DoughWorld|Rules") bool CanThrowAlcohol(FVector TargetLocation) const;
    virtual bool CanThrowAlcohol_Implementation(FVector TargetLocation) const;
    UFUNCTION(BlueprintNativeEvent,BlueprintPure,Category="DoughWorld|Rules") bool CanEnterYeastForm() const;
    virtual bool CanEnterYeastForm_Implementation() const;

    /** Notifications run after the core action succeeds. Do not repeat its inventory/health mutation. */
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnDashStarted();
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnSprintAlcoholProduced(int32 Amount);
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnResourceHarvested(ADWResourceNode* Resource,FName ItemId,int32 Amount);
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnFormChanged(bool bNowYeastForm);
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnAlcoholThrown(ADWAlcoholProjectile* Projectile,FVector TargetLocation,int32 AlcoholSpent);
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnInventoryItemUsed(FName ItemId,float HealthRestored,float TransformationGained);
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnRecipeCrafted(FName RecipeId);
    UFUNCTION(BlueprintImplementableEvent,Category="DoughWorld|Events") void OnPlayerDied();
    void MoveCameraRelative(float Forward,float Right);
    void DragCamera(float DeltaX,float DeltaY);
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Camera") void ZoomCamera(float WheelDelta);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Camera") float GetCurrentCameraDistance() const;
    void ClearHeldActions();
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Save") void CaptureSaveData(UDWSaveGame* Save) const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Save") void ApplySaveData(const UDWSaveGame* Save);
    void TickGameplayClocks(float DeltaSeconds,bool bActuallySprinting);
    bool TryApplySaveData(const UDWSaveGame* Save);
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Resources") bool HasCombatVitalsReady() const {return CombatVitals.IsReady();}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Resources") EDWCombatBaseForm GetCombatBaseForm() const {return CombatVitals.GetForm();}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Resources") double GetCombatWater() const {return CombatVitals.GetWater();}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Resources") double GetCombatSatiety() const {return CombatVitals.GetSatiety();}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Resources") double GetCombatSpecial() const {return CombatVitals.GetSpecial();}
    UFUNCTION(BlueprintPure,Category="DoughWorld|Combat|Resources") double GetCombatFormRemaining() const {return CombatVitals.GetFormRemaining();}
    EDWCombatBaseForm GetCombatBaseFormAtFrameOffset(double Seconds) const;
    double GetCombatFrameSliceStartWorldSeconds(double DeltaSeconds) const;
    uint64 GetCombatDamageRevision() const {return CombatDamageRevision;}
    double GetLastCombatDamageWorldTime() const {return LastCombatDamageWorldTime;}
    bool IsCombatRestoreInProgress() const {return bRestoringSave;}
    // Gathering-profile native environmental facts. No new UI or Blueprint refill grant.
    bool TryReceiveGatheringWater(ADWGatheringPond& Source,double RequestedAmount,double& OutAdded);
    void SetGatheringEnvironmentSlow(const AActor* Source,double Fraction);
    void RemoveGatheringEnvironmentSlow(const AActor* Source);
    double GetGatheringEnvironmentMovementMultiplier() const;

protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void UnPossessed() override;
    virtual void NotifyHit(UPrimitiveComponent* MyComp,AActor* Other,UPrimitiveComponent* OtherComp,bool bSelfMoved,FVector HitLocation,FVector HitNormal,FVector NormalImpulse,const FHitResult& Hit) override;
private:
    bool TryGiveCombatTestWeapons();
    bool TryGiveCombatTestCores();
    bool bCombatTestCoresFinished=false;
    bool bCombatTestWeaponsFinished=false,bGivingCombatTestWeapons=false;
    TWeakObjectPtr<UWorld> CombatTestWeaponGrantWorld;
    TWeakObjectPtr<UDWWeaponAnimationComponent> CombatTestWeaponGrantComponent;
    DWCombatCommit::FEntityLife CombatTestWeaponGrantLife;
    uint64 CombatTestWeaponGrantEpoch=0,CombatTestWeaponGrantRestoreEpoch=0;
    friend class UDWWeaponAnimationComponent;
    friend class UDWCombatAuthoritySubsystem;
    friend class FDWCombatMinePersistence;
    FDWPlayerCombatKnockbackState CombatKnockback;
    uint64 CombatKnockbackRevision=0;
    FTransform CombatRollRestTransform;
    bool bCombatRollRestorePending=false;
    FDWPlayerCombatKnockback PrepareCombatKnockback(FVector Direction,float Distance,float Seconds,DWCombatCommit::FEntityLife Life)const;
    FDWPlayerCombatKnockback PrepareCombatKnockbackAtImpact(FVector Direction,float Distance,float Seconds,DWCombatCommit::FEntityLife Life,bool bRootedAtImpact)const;
    void CommitCombatKnockbackFacts(const FDWPlayerCombatKnockback& Prepared);
    void ResetCombatKnockbackFacts();
    void InterruptLegacyRollForKnockbackFacts();
    void PublishCombatKnockbackEffects();
    bool AdvanceCombatKnockback(float DeltaSeconds);
    friend class FDWCombatRecoveryTransaction;
    bool bCombatRecoveryPositionPending=false;
    friend class FDWCombatPlayerStateTransaction;
    friend class FDWCombatPlayerPersistence;
    friend class FDWCombatDashTransaction;
    FDWYeastDashPlan FormalDash;
    FDWCombatInputPress DashInputOrigin;
    DWCombatCommit::FEntityLife FormalDashLife;
    TWeakObjectPtr<UWorld> FormalDashWorld;
    uint64 FormalDashEpoch=0,LastFormalDashRequestOrdinal=0,LastPublishedFormalDashSerial=0;
    FDWCombatInputEdge LastFormalDashEdge;
    bool bApplyingFormalDashMotion=false;
    struct FPaidFormalDashSlice
    {
        bool bPending=false,bMoved=false;uint64 Serial=0;FVector Start=FVector::ZeroVector,Finish=FVector::ZeroVector;double DistanceCm=0.;
        EDWYeastDashEnd End=EDWYeastDashEnd::None;
    } PaidFormalDashSlice;
    virtual bool CanUseLegacyRoll()const override{return false;}
    bool IsFormalDashCurrent()const;
    void EndFormalDashFacts(EDWYeastDashEnd Reason);
    double GetFormalDashBoundary(double MaximumSeconds,bool& bWall)const;
    void QueueFormalDashSlice(const FDWYeastDashStep&,bool bWall);
    bool PumpFormalDashSlice();
    friend class FDWCombatCoreInjectionTransaction;
    friend class FDWCombatMoldRootTransaction;
    FDWMoldRootPlan FormalRoot;
    FDWMoldRootIdentity FormalRootIdentity;
    bool bRootEntryDue=false,bRootMovementPending=false;
    bool PrepareFormalRootEntry(FDWMoldRootConfig& Out)const;
    void EndFormalRootFacts(EDWMoldRootEnd Reason);
    bool PublishFormalRootMovementEffects();
    // D22: native only. Adopt a prevalidated candidate and fresh identity with the complete ring transaction.
    void AdoptFormalRootRestoreUnchecked(FDWMoldRootPlan&& Candidate,const FDWMoldRootIdentity& Identity)
    {FormalRoot=MoveTemp(Candidate);FormalRootIdentity=Identity;bRootMovementPending=true;bRootEntryDue=false;}
    FDWCombatPlayerVitalsRuntime CombatVitals;
    TMap<TWeakObjectPtr<AActor>,double> GatheringEnvironmentSlowSources;
    FDWCoreInjectionSession CoreInjection;
    DWCombatCommit::FReceiptId LastCoreInjectionReceipt;
    bool bCoreInjectionCompletionDue=false;
    void CancelCoreInjectionFacts();
    bool IsCoreInjectionCurrent() const;
    bool CompleteCoreInjectionAtBoundary();
    uint64 CombatDamageRevision=0;
    double LastCombatDamageWorldTime=-1.;
    bool bCombatDeathEffectsPending=false,bHasCombatFormFrameSlice=false;
    bool bAdvancingGameplayClocks=false;
    // Transient D02 pause continuation; D22 weapon/pending timeline serialization remains separate.
    struct FPaidCombatWeaponSlice
    {
        bool bPending=false,bWasBusy=false;
        double Seconds=0.,StartWorldSeconds=0.,FormExpiryOffset=0.;
        bool bStartRooted=false;
        double RootExitOffset=0.;
        EDWCombatBaseForm StartForm=EDWCombatBaseForm::Normal;
        DWCombatCommit::FEntityLife Life;
        uint64 WorldEpoch=0,BoundaryRevision=0;
        int32 ActionSerial=0;
        uint8 WeaponSelection=0;
        FName Role;
        TWeakObjectPtr<UWorld> World;
        TWeakObjectPtr<UDWWeaponAnimationComponent> Weapon;
    } PaidCombatWeaponSlice;
    bool bCombatFrameStartRooted=false;
    double CombatFrameRootExitOffset=0.;
    EDWCombatBaseForm CombatFrameStartForm=EDWCombatBaseForm::Normal;
    double CombatFrameExpiryOffset=0.,CombatSliceStartWorldSeconds=0.;
    DWCombatCommit::FEntityLife CombatFrameLife;
    void InitializeCombatVitals();
    void SyncCombatVitalsCompatibility();
    void RefreshCombatMovementSpeed();
    float GetCombatHealthFloor() const;
    double GetCombatIncomingMultiplier() const;
    bool CanReceiveCombatDamage() const;
    void RecordCombatDamageFacts(float Applied);
    void CommitCombatDeathFacts();
    void PublishCombatDeathEffects();
    UPROPERTY(Transient) TObjectPtr<USpringArmComponent> CameraArm;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> ActiveAnimation;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> ActiveAttackClip;
    UPROPERTY(Transient) FDWMeleeComboStep ActiveMeleeStep;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> ActiveMeleeClip;
    bool bHasMeleeAnimationSnapshot=false,bActiveMeleeCombo=false,bRestartMeleeAnimation=false;
    int32 NextMeleeComboStep=INDEX_NONE;
    float MeleeComboExpiresAt=0.f;
    FVector BufferedMeleeTarget=FVector::ZeroVector;
    FName LastAnimatedMeleeWeaponBone=NAME_None;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> GatherLoopComponent;
    UPROPERTY(Transient) TObjectPtr<UAudioComponent> MovementAudioComponent;
    // Playback implementation only. Author resources and transforms in the public VFX settings.
    UPROPERTY(BlueprintReadOnly,Category="DoughWorld|VFX",meta=(AllowPrivateAccess="true")) TObjectPtr<UNiagaraComponent> MovementDustComponent;
    UPROPERTY(BlueprintReadOnly,Category="DoughWorld|VFX",meta=(AllowPrivateAccess="true")) TObjectPtr<UNiagaraComponent> RunTrailComponent;
    UPROPERTY(BlueprintReadOnly,Category="DoughWorld|VFX",meta=(AllowPrivateAccess="true")) TObjectPtr<UNiagaraComponent> DashTrailComponent;
    UPROPERTY(BlueprintReadOnly,Category="DoughWorld|VFX",meta=(AllowPrivateAccess="true")) TObjectPtr<UNiagaraComponent> DashStartComponent;
    UPROPERTY(BlueprintReadOnly,Category="DoughWorld|VFX",meta=(AllowPrivateAccess="true")) TObjectPtr<UNiagaraComponent> TransformBurstComponent;
    FVector PreviousVFXLocation=FVector::ZeroVector;
    bool bHasVFXLocationSample=false;
    bool bMovementDustRateSystemStarted=false;
    bool bMovementAudioRestartPending=false;
    float DashStartVFXStopTime=0.f,TransformBurstVFXStopTime=0.f;
    float DashStartVFXCleanupTime=0.f,TransformBurstVFXCleanupTime=0.f;
    TWeakObjectPtr<ADWResourceNode> HarvestTarget;
    TWeakObjectPtr<ADWResourceNode> GatherFeedbackTarget;
    bool bSprintHeld=false,bHarvestHeld=false,bDeathHandled=false;
    bool bGameplayActionInProgress=false,bRestoringSave=false;
    float HarvestElapsed=0.f,ScanElapsed=0.f,NextThrowTime=0.f;
    float AttackUntil=0.f,TransformUntil=0.f,ShakeElapsed=100.f,TrailElapsed=0.f;
    float ActiveAttackAnimationRate=1.f;
    float ActiveAttackStartSeconds=0.f;
    float ActiveAttackEndSeconds=0.f,ThrowAnimationStartTime=0.f;
    int32 ThrowAnimationSerial=0;
    float MeleeStartTime=0.f,MeleeActiveStartTime=0.f,MeleeActiveEndTime=0.f,MeleeEndTime=0.f,NextMeleeTime=0.f;
    float ActiveMeleeDuration=.62f,FrozenMeleeAnimationElapsed=0.f;
    FVector MeleeDirection=FVector::ForwardVector,LastDashCombatLocation=FVector::ZeroVector;
    FVector DashCombatDirection=FVector::ForwardVector;
    TSet<TWeakObjectPtr<AActor>> MeleeHitActors,DashHitActors;
    DWCombatCommit::FActionId MeleeCommitAction,DashCommitAction;
    bool bMeleeWindowSampled=false,bDashCombatActive=false,bApplyingCombatDamage=false;
    bool bMeleePreviousOrientToMovement=true;
    FTransform MeleeWeaponRestTransform;
    TWeakObjectPtr<USceneComponent> MeleeWeaponRestParent;
    FName MeleeWeaponRestSocket=NAME_None;
    bool bMeleeWeaponRestVisible=false,bMeleeWeaponRestHidden=false;
    bool bMeleeWeaponRestAbsoluteLocation=false,bMeleeWeaponRestAbsoluteRotation=false,bMeleeWeaponRestAbsoluteScale=false;
    bool bMeleeWeaponCapsuleFallback=false,bMeleeWeaponDefaultCube=false;
    bool bRestartAttackAnimation=false;
    float CameraYaw=0.f,CameraPitch=-60.f;
    FVector CameraRestOffset=FVector::ZeroVector;
    FVector DoughMeshScale=FVector::OneVector;
    FDelegateHandle FormBoneFinalizedHandle;
    float FormBlendStartAlpha=0.f,FormBlendTargetAlpha=0.f,FormBlendElapsed=0.f,FormBlendDuration=1.f;
    bool bFormVisualsInitialized=false,bApplyingFormScale=false;
    bool bGatherFeedbackActive=false,bGatherFailureLatched=false;
    bool CanPlayPlayerVFX() const;
    bool CanPlayMovementFeedback() const;
    bool CanContinueMovementAudio() const;
    void UpdateMovementAudio(bool bActuallyMoving);
    void StopMovementAudio(bool bDestroyComponent=false);
    UFUNCTION() void HandleMovementAudioFinished();
    void UpdatePlayerVFX(float DeltaSeconds);
    void SetLoopingPlayerVFX(UNiagaraComponent* Component,UNiagaraSystem* System,bool bEmit,bool& bWasEmitting,const FVector& FootOffset,const FRotator& Rotation,const FVector& Scale);
    void PlayPlayerBurstVFX(UNiagaraComponent* Component,UNiagaraSystem* System,const FVector& FootOffset,const FRotator& Rotation,const FVector& Scale);
    void PlayFormTransitionVFX(bool bEntering);
    void UpdateHarvest(float DeltaSeconds);
    void UpdateAnimation();
    void UpdateCombat(float DeltaSeconds);
    void UpdateMeleeDamageVolume();
    bool BeginMeleeStep(FVector TargetLocation,int32 ComboStep);
    void FinishMeleeStep();
    void ClearMeleeComboContinuation();
    void TryStartBufferedMeleeStep();
    void UpdateAnimatedMeleeWeaponVisibility();
    void BeginMeleeWeaponPlaceholder();
    void UpdateMeleeWeaponPlaceholder();
    void RestoreMeleeWeaponPlaceholder();
    void SweepDashCombat(const FVector& Start,const FVector& End);
    bool HasCombatLineOfSight(AActor* Target,const FVector& Origin,const FVector& End) const;
    bool CanDamageCombatTarget(AActor* Target) const;
    void DamageCombatTarget(AActor* Target,float Amount,float KnockbackDistance,float KnockbackSeconds,const FVector& Direction,TSet<TWeakObjectPtr<AActor>>& HitActors,DWCombatCommit::FActionDamage Identity={});
    float ClampCameraDistance(float Distance) const;
    void StartAttackAnimation();
    void InitializeFormVisuals();
    void UpdateFormAppearance(bool bImmediate=false);
    void TickFormAppearance(float DeltaSeconds);
    void ApplyFormMeshScale();
    void HandleFormBonesFinalized();
    void UpdateGatherFeedback(ADWResourceNode* Resource);
    void StopGatherFeedback(bool bPlayStop=true,bool bResetSession=true);
    bool CanContinueGatherFeedback() const;
    UFUNCTION() void HandleGatherLoopFinished();
    void Die();
    void Notify(const FText& Message) const;
};
