#pragma once
#include "CoreMinimal.h"
#include "TopDownActionCharacter.h"
#include "DWPlayerCharacter.generated.h"
class UDWGameplayConfig; class UDWInventoryComponent; class UDWSaveGame;
class ADWResourceNode; class ADWAlcoholProjectile; class UAnimationAsset;
class USpringArmComponent; class USoundBase; class UCameraShakeBase; class UNiagaraSystem;
class UStaticMeshComponent; class UAudioComponent; class UNiagaraComponent;

/** Uses the existing TopDown action-character movement; gameplay is independent of the imported programming pawn. */
UCLASS(Blueprintable)
class GDATTEST_API ADWPlayerCharacter : public ATopDownActionCharacter
{
    GENERATED_BODY()
public:
    /** Settle the spring arm before the loading cover is removed, even while paused. */
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Camera") void PrepareCameraForReveal();
    ADWPlayerCharacter();
    virtual void Tick(float DeltaSeconds) override;
    virtual float TakeDamage(float Amount,const FDamageEvent& Event,AController* InstigatorController,AActor* Causer) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Inventory") TObjectPtr<UDWInventoryComponent> Inventory;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="DoughWorld|Equipment") TObjectPtr<UStaticMeshComponent> SwordPlaceholder;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Equipment") FName SwordSocket=TEXT("bone_005_R");
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Config") TObjectPtr<UDWGameplayConfig> GameplayConfig;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> IdleAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> WalkAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> AttackAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> TransformAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> DeathAnimation;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Animation") TObjectPtr<UAnimationAsset> DashAnimation;
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
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="0.01")) float CameraDragSensitivity=0.22f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="DoughWorld|Camera",meta=(ClampMin="100",Units="cm")) float CameraDistance=1800.f;
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
    UFUNCTION(BlueprintPure,Category="DoughWorld") bool IsDead() const {return Health<=0.f;}
    UFUNCTION(BlueprintCallable,Category="DoughWorld") bool UseInventoryItem(int32 SlotIndex);
    UFUNCTION(BlueprintCallable,Category="DoughWorld") bool CraftRecipe(FName RecipeId);
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
    void ClearHeldActions();
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Save") void CaptureSaveData(UDWSaveGame* Save) const;
    UFUNCTION(BlueprintCallable,Category="DoughWorld|Save") void ApplySaveData(const UDWSaveGame* Save);
    void TickGameplayClocks(float DeltaSeconds,bool bActuallySprinting);
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TObjectPtr<USpringArmComponent> CameraArm;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> ActiveAnimation;
    UPROPERTY(Transient) TObjectPtr<UAnimationAsset> ActiveAttackClip;
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
