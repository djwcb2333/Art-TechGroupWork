#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InputCoreTypes.h"
#include "DWWeaponAnimationSet.h"
#include "DWWeaponGeometry.h"
#include "DWCombatWeaponResourceState.h"
#include "DWCombatCommitLedger.h"
#include "DWCombatInputLedger.h"
#include "DWCombatSpear.h"
#include "DWCombatSporeGun.h"
#include "DWCombatBow.h"
#include "DWCombatBottle.h"
#include "DWCombatUnarmed.h"
#include "DWCombatShieldAxe.h"
#include "DWCombatSword.h"
#include "DWCombatDagger.h"
#include "DWCombatShotgun.h"
#include "DWWeaponAnimationComponent.generated.h"

class ADWPlayerCharacter;
class ADWEnemyCharacter;
class UAnimSequence;
class FDWCombatRules;
class FDWCombatInventoryTransaction;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UNiagaraComponent;
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDWWeaponAnimationEvent,EDWAnimatedWeapon,Weapon,FName,Role,int32,EventIndex);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FDWWeaponResourceChanged,int32,LoadedShells,int64,ResourceRevision,bool,bReload);

UCLASS(ClassGroup=(DoughWorld),BlueprintType,meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWWeaponAnimationComponent : public UActorComponent
{
    GENERATED_BODY()
public:

    UDWWeaponAnimationComponent();
    bool IsFormalBowActive()const{return bFormalBowActive;}
    EDWBowPhase GetFormalBowPhase()const{return BowPlan.Phase;}
    bool IsFormalBottleActive()const{return bFormalBottleActive;}
    EDWBottlePhase GetFormalBottlePhase()const{return BottlePlan.GetPhase();}
    bool IsFormalSporeActive()const{return bFormalSporeActive;}
    EDWSporePhase GetFormalSporePhase()const{return SporePlan.GetPhase();}
    void UpdateFormalSporeGapAim(FVector Target);
    // Native authoring seam: explicit verified source timing. No automatic activation for current HC04-conflicting art.
    bool ConfigureFormalSpear(const FDWCombatRules& Rules,UAnimSequenceBase* Sequence,const FDWSpearSourceTiming& Timing,FString& Error);
    UFUNCTION(BlueprintPure,Category="Weapons|Formal Spear")
    bool IsFormalSpearConfigured() const {return bFormalSpearConfigured;}
    // Explicit art approval binding only; these source measurements do not override combat rules.
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Formal Spear") bool bEnableFormalSpearExplicitBinding=false;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Formal Spear",meta=(EditCondition="bEnableFormalSpearExplicitBinding")) TObjectPtr<UAnimSequence> ApprovedFormalSpearSequence;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Formal Spear",meta=(EditCondition="bEnableFormalSpearExplicitBinding",Units="s")) double ApprovedFormalSpearSourceDurationSeconds=0.;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Formal Spear",meta=(EditCondition="bEnableFormalSpearExplicitBinding",Units="s")) double ApprovedFormalSpearContactBeginSeconds=0.;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Formal Spear",meta=(EditCondition="bEnableFormalSpearExplicitBinding",Units="s")) double ApprovedFormalSpearContactEndSeconds=0.;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Weapons|Formal Spear") FString FormalSpearBindingError;
    EDWSpearPhase GetFormalSpearPhase() const {return SpearPlan.GetPhase();}
    // Explicit native initialization/save seam. Empty ownership/zero shells by default; never grants on BeginPlay.
    bool RestoreCombatWeaponState(const FDWCombatWeaponResourceSnapshot& Snapshot,const FDWCombatRules& Rules,FString& OutError);
    const FDWCombatWeaponResourceSnapshot& GetCombatWeaponStateSnapshot() const {return CombatWeaponResources.Snapshot;}
    int64 GetCombatWeaponResourceRevision() const {return CombatWeaponResources.Revision;}
    DWCombatCommit::FReceiptId GetLastWeaponResourceReceipt() const {return LastWeaponResourceReceipt;}
    UPROPERTY(BlueprintAssignable,Category="Events") FDWWeaponResourceChanged OnWeaponResourceChanged;
    /** Approved source release marker. Runtime windup/recovery map to the two source segments automatically. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Bow Presentation",meta=(ClampMin="0.001",Units="s")) double BowSourceReleaseSeconds=.6833333333333333;
    /** Read-only source prop evaluation; never commits ammo, damage, an action or an animation event. */
    bool EvaluateBowReleaseVisual(FDWWeaponGeometry&,FTransform& ActorLocal,UMaterialInterface*&)const;
    /** Optional independent arrow appearance. None reuses the current bow set's baked arrow. No gameplay collision. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Bow Projectile") TObjectPtr<UStaticMesh> BowProjectileMesh;
    /** Visual child transform only; +X is forward. Does not change range, speed or sweep radius. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Bow Projectile") FTransform BowProjectileVisualTransform=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weapons") bool bEnabled=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weapons") TArray<TObjectPtr<UDWWeaponAnimationSet>> AnimationSets;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Weapons") EDWAnimatedWeapon SelectedWeapon=EDWAnimatedWeapon::Unarmed;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Input") FKey ReloadKey=EKeys::R;
    /** Optional implementation mapping; conflicts with existing actions/shortcuts disable this shortcut. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Input") FKey UnarmedSelectionKey=EKeys::Zero;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Input") bool bEnableQuickSelection=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Input") TArray<FKey> WeaponSelectionKeys;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="State") int32 ActionSerial=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="State") int32 DispatchedEventCount=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="State") int32 AmmoInMagazine=0;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Trail") TObjectPtr<UNiagaraComponent> PrimaryTrailComponent;
    UPROPERTY(VisibleInstanceOnly,BlueprintReadOnly,Transient,Category="Trail") TObjectPtr<UNiagaraComponent> SecondaryTrailComponent;
    /** Fires once at each explicit source-clock event; hook bespoke projectiles/status mechanics here. */
    UPROPERTY(BlueprintAssignable,Category="Events") FDWWeaponAnimationEvent OnAnimationGameplayEvent;
    UFUNCTION(BlueprintPure,Category="Weapons") bool IsEnabled() const;
    UFUNCTION(BlueprintPure,Category="Weapons") bool IsBusy() const {return bActive;}
    UFUNCTION(BlueprintPure,Category="Weapons") UDWWeaponAnimationSet* GetSelectedSet() const;
    UFUNCTION(BlueprintPure,Category="Weapons") bool CanSelectCombatEquipment(EDWAnimatedWeapon Weapon)const;
    UFUNCTION(BlueprintCallable,Category="Weapons") bool SelectWeapon(EDWAnimatedWeapon Weapon);
    UFUNCTION(BlueprintPure,Category="Weapons") FName GetCurrentEquipmentId()const{return CombatWeaponResources.Snapshot.CurrentEquipmentId;}
    uint64 GetCombatWeaponRestoreEpoch()const{return CombatWeaponResources.RestoreEpoch;}
    double GetRemainingAttackLockSeconds()const{return FMath::Max(0.,NextAttackTime-GetCombatActionWorldSeconds());}
    EDWSpearPhase GetFormalUnarmedPhase()const{return UnarmedPlan.GetPhase();}
    bool IsFormalUnarmedActive()const{return bFormalUnarmedActive;}
    EDWSwordPhase GetFormalSwordPhase()const{return SwordPlan.GetPhase();}
    UFUNCTION(BlueprintPure,Category="Weapons|Greatsword") bool IsFormalSwordChargeReady()const{return bFormalSwordActive&&SwordPlan.IsChargeReady();}
    /** Presentation query only: never consumes input, advances a clock or commits gameplay. */
    UFUNCTION(BlueprintPure,Category="Weapons|Greatsword") bool IsFormalSwordWaitingForRelease()const{return IsFormalSwordChargeReady()&&bPrimaryHeld&&!bSwordReleasePending;}
    bool IsFormalSwordGap()const;
    bool AllowsFormalSwordDashInterruption()const;
    bool ApplyFormalSwordMovementPress(const FDWCombatInputPress&);
    bool ApplyFormalSwordGapMovement();
    void UpdateFormalSwordGapAim(FVector Target);
    void UpdateFormalSwordChargeAim(FVector Target);
    /** Pure source sampling: gameplay clocks and hurtboxes never consume this value. */
    UFUNCTION(BlueprintPure,Category="Weapons|Presentation Timing") double GetPresentationSourceSeconds()const;
    /** Read-only clip selection: a Ready presentation never replaces ActiveClip or its gameplay role. */
    const FDWWeaponAnimationClip& GetPresentationClip()const;
    /** Game-time duration for one complete authored ChargeHold loop; source geometry and animation stay unchanged. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Weapons|Greatsword Presentation",meta=(ClampMin="0.001",Units="s")) double SwordReadyLoopSeconds=1.;
    /** Source phase endpoints as fractions of the full authored clip. X=windup end; Y=active end (melee). */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Presentation Timing")
    TMap<EDWAnimatedWeapon,FVector2D> AttackSourcePhaseEnds={
        {EDWAnimatedWeapon::Spear,FVector2D(.2,.38)},
        {EDWAnimatedWeapon::ShieldAxe,FVector2D(.3916666666666667/.85,.475/.85)},
        {EDWAnimatedWeapon::SporeGun,FVector2D(0.,1.)},
        {EDWAnimatedWeapon::Shotgun,FVector2D(1./6.,1.)},
        {EDWAnimatedWeapon::Alcohol,FVector2D(.5416666666666667/1.1,1.)}};
    /** Charge/Slam/Gap/Sweep/Recovery source end fractions. Existing source data is preserved. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Presentation Timing")
    TArray<double> SwordSourcePhaseEnds={1.6/(227./60.),1.95/(227./60.),(38./15.)/(227./60.),(167./60.)/(227./60.),1.};
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Presentation Timing",meta=(ClampMin="0",ClampMax="1"))
    double SwordSlamFeedbackProgress=4./7.;
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Presentation Timing",meta=(ClampMin="0",ClampMax="1"))
    double SwordSweepFeedbackProgress=.4;
    bool HasWeaponPresentation()const{return bActive||bSwordVisualTail||bDaggerVisualTail;}
    EDWSpearPhase GetFormalDaggerPhase()const{return DaggerPlan.GetPhase();}
    /** Authoring source seconds: X is begin, Y is end. The four gameplay durations come from formal rules. */
    UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Weapons|Dual Daggers")
    TArray<FVector2D> DaggerSourceSegments={{0.,29./30.},{29./30.,28./15.},{28./15.,8./3.},{8./3.,3.85}};
    UFUNCTION(BlueprintPure,Category="Weapons|Dual Daggers") int32 GetFormalDaggerComboStage()const{return bFormalDaggerActive?DaggerComboStage+1:0;}
    UFUNCTION(BlueprintPure,Category="Weapons|Dual Daggers") double GetFormalDaggerSourceBegin()const{return DaggerSourceSegment.X;}
    UFUNCTION(BlueprintPure,Category="Weapons|Dual Daggers") double GetFormalDaggerSourceEnd()const{return DaggerSourceSegment.Y;}
    UFUNCTION(BlueprintPure,Category="Weapons|Dual Daggers") double GetFormalDaggerSegmentDuration()const{return bFormalDaggerActive?DaggerPlan.GetTuning().IntervalSeconds:0.;}
    double GetDaggerGraceSeconds()const{return bFormalDaggerConfigured?DaggerTuning.GraceSeconds:0.;}
    bool IsAlcoholReleaseEventDue()const{return bAlcoholReleaseEventDue&&HasCurrentEquipmentIdentity()&&SelectedWeapon==EDWAnimatedWeapon::Alcohol&&bActive;}
    void PublishAcceptedWeaponStartEffects();
    bool IsFormalShieldGuarding()const{return bShieldAxeConfigured&&bEnabled&&HasCurrentEquipmentIdentity()&&SelectedWeapon==EDWAnimatedWeapon::ShieldAxe&&ShieldDefense.IsHeld();}
    bool IsFormalAxeActive()const{return bFormalAxeActive;}
    bool CanPerfectShieldAt(double Now)const{return IsFormalShieldGuarding()&&ShieldDefense.CanPerfectAt(Now);}
    UFUNCTION(BlueprintPure,Category="Weapons|Shield Axe") int32 GetShieldCounterCharges()const{return ShieldDefense.GetCounterCharges();}
    FDWShieldImpactPolicy PrepareShieldImpact(double Now,EDWShieldIncomingKind,FVector Incoming)const;
    void RecordPerfectShieldFacts();void NotifyCombatBaseFormChanged(uint8 Form);
    void ClearShieldCounterFacts(){ShieldDefense.ClearCounter();}
    bool HasUnserializedShieldState()const;
    double GetCombatEventWorldSeconds()const{return GetCombatActionWorldSeconds();}
    FDWShieldAxeDefenseSnapshot CaptureShieldDefense(double Now)const{return ShieldDefense.Capture(Now);}
    UFUNCTION(BlueprintCallable,Category="Weapons") bool PrimaryPressed(FVector Target);
    UFUNCTION(BlueprintCallable,Category="Weapons") void PrimaryReleased();
    UFUNCTION(BlueprintCallable,Category="Weapons") bool SecondaryPressed();
    UFUNCTION(BlueprintCallable,Category="Weapons") void SecondaryReleased();
    UFUNCTION(BlueprintCallable,Category="Weapons") bool RequestReload();
    /** Explicit animation hook: Backstab does not imply an execution/instant-kill rule. */
    UFUNCTION(BlueprintCallable,Category="Weapons") bool PlayRole(FName Role,FVector Target);
    UFUNCTION(BlueprintCallable,Category="Weapons") void CancelAction();
    UFUNCTION(BlueprintPure,Category="Weapons") FName GetActiveRole() const {return ActiveClip.Role;}
    UFUNCTION(BlueprintPure,Category="Weapons") UAnimSequenceBase* GetActiveSequence() const {return GetPresentationClip().Animation;}
    UFUNCTION(BlueprintPure,Category="Weapons") float GetPositionSeconds() const;
    UFUNCTION(BlueprintPure,Category="Weapons") float GetActionDuration() const {return GetPresentationClip().DurationSeconds;}
    UFUNCTION(BlueprintPure,Category="Weapons") float GetElapsedSeconds() const {return static_cast<float>(ElapsedSeconds);}
    UFUNCTION(BlueprintPure,Category="Weapons") bool LocksMovement() const;
    UFUNCTION(BlueprintPure,Category="Weapons|Shotgun") bool IsFormalShotgunReloading()const{return bActive&&bFormalShotgunReload;}
    /** Current native shell cycle, read-only: never advances or completes a reload. */
    UFUNCTION(BlueprintPure,Category="Weapons|Shotgun") double GetFormalShotgunReloadRemainingSeconds()const
    {return IsFormalShotgunReloading()&&FMath::IsFinite(ShotgunReloadRemaining)?FMath::Max(0.,ShotgunReloadRemaining):0.;}
    UFUNCTION(BlueprintPure,Category="Weapons|Shotgun") double GetFormalShotgunReloadDurationSeconds()const
    {return IsFormalShotgunReloading()&&FMath::IsFinite(CombatWeaponResources.ReloadOneSeconds)?FMath::Max(0.,CombatWeaponResources.ReloadOneSeconds):0.;}
    UFUNCTION(BlueprintPure,Category="Weapons|Shotgun") double GetFormalShotgunReloadProgress()const
    {
        const double Duration=GetFormalShotgunReloadDurationSeconds();
        return Duration>0.?FMath::Clamp(1.-GetFormalShotgunReloadRemainingSeconds()/Duration,0.,1.):0.;
    }
    double GetReloadMovementMultiplier()const{return IsFormalShotgunReloading()?ShotgunConfig.ReloadMoveMultiplier:1.;}
    UFUNCTION(BlueprintPure,Category="Weapons") bool IsBlocking() const;
    /** Actual Niagara component state, not just the authored trail-window flag. */
    UFUNCTION(BlueprintPure,Category="Trail") bool IsPrimaryTrailActive() const;
    UFUNCTION(BlueprintPure,Category="Trail") bool IsSecondaryTrailActive() const;
    float FilterIncomingDamage(float Amount,AActor* Causer);
    // Resource coordinator caps each slice at this boundary; infinity means no pending weapon event.
    double GetNextCombatBoundarySeconds(double SliceStartWorldSeconds) const;
    uint64 GetCombatBoundaryRevision() const {return CombatBoundaryRevision;}
    // Zero processes an event already due. False stops the old frame after callback interruption.
    bool AdvanceAnimation(double DeltaSeconds);
    bool BindObservedInputPress(const FDWCombatInputPress& Press,bool bSecondary);
    bool ApplyObservedInputRelease(const FDWCombatInputPress& Press);
    bool CancelObservedInputAction(const FDWCombatInputPress& Press);
    UFUNCTION(BlueprintPure,Category="Weapons|InputDebug") FString GetObservedInputDebugState() const;
    const FDWWeaponGeometryStats& GetGeometryStats() const {return GeometryStats;}
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* TickFunction) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    friend class FDWCombatBottleTransaction;
    FDWBottleConfig BottleConfig;FDWBottlePlan BottlePlan;
    DWCombatCommit::FActionId BottleAction,BottlePublishedAction;
    FVector BottleRequestedTarget=FVector::ZeroVector,BottleForward=FVector::ForwardVector;
    double BottleBoundaryWorldSeconds=0.,BottlePriorAttackLock=0.;
    bool bBottleConfigured=false,bFormalBottleActive=false,bBottleCommitted=false;
    void InitializeFormalBottleRules();bool BeginFormalBottle(FVector);
    void PublishFormalBottleStartEffects();void EndFormalBottle();void AdvanceFormalBottle(double);
    bool IsCurrentFormalBottle(DWCombatCommit::FActionId)const;
    friend class FDWCombatBowShotTransaction;
    FDWBowConfig BowConfig;FDWBowPlan BowPlan;
    DWCombatCommit::FActionId BowAction,BowPublishedAction;
    FVector BowForward=FVector::ForwardVector;
    double BowBoundaryWorldSeconds=0.,BowPriorAttackLock=0.;
    bool bBowConfigured=false,bFormalBowActive=false,bBowShotCommitted=false;
    void InitializeFormalBowRules();
    EDWBowForm ReadFormalBowForm(double Offset=-1.)const;
    bool BeginFormalBow(FVector Target);
    void PublishFormalBowStartEffects();
    void EndFormalBow();
    bool IsCurrentFormalBow(DWCombatCommit::FActionId)const;
    double GetNextBowBoundarySeconds(double Start)const;
    void AdvanceFormalBow(double Dt);
    double GetBowPresentationSeconds()const;
    double BowActionSecondsForSource(double SourceSeconds)const;
    friend class FDWCombatSporeShotTransaction;
    FDWSporeGunConfig SporeConfig;
    FDWSporeShotPlan SporePlan;
    DWCombatCommit::FActionId SporeAction,SporePublishedAction;
    FVector SporeForward=FVector::ForwardVector;
    double SporeBoundaryWorldSeconds=0.,SporePriorAttackLock=0.;
    bool bSporeConfigured=false,bFormalSporeActive=false,bSporeShotCommitted=false;
    void InitializeFormalSporeRules();
    EDWSporeForm ReadFormalSporeForm(double Offset=-1.)const;
    bool BeginFormalSpore(FVector Target);
    void PublishFormalSporeStartEffects();
    void EndFormalSpore();void CancelFormalSporeFacts();
    bool IsCurrentFormalSpore(DWCombatCommit::FActionId Action)const;
    double GetNextSporeBoundarySeconds(double Start)const;
    void AdvanceFormalSpore(double Dt);
    friend class FDWCombatInventoryTransaction;
    friend class UDWCombatAuthoritySubsystem;
    FDWShotgunConfig ShotgunConfig;FDWShotgunPlan ShotgunPlan;
    FVector ShotgunForward=FVector::ForwardVector;double ShotgunReloadRemaining=0.;
    bool bShotgunConfigured=false,bFormalShotgunActive=false,bFormalShotgunReload=false,bShotgunStartPublished=false;
    void InitializeShotgunRules();bool StartFormalShotgun(FVector,bool Reload);
    bool IsCurrentFormalShotgun()const;void EndFormalShotgun();void PublishFormalShotgunStart();void AdvanceFormalShotgun(double);
    friend class FDWCombatEquipmentTransaction;
    friend class FDWCombatRecoveryTransaction;
    friend class FDWCombatPlayerStateTransaction;
    friend class FDWCombatPlayerPersistence;
    bool HasCurrentEquipmentIdentity()const;
    double EquipmentSwitchLockSeconds=0.;bool bEquipmentRulesReady=false,bAlcoholReleaseEventDue=false;
    FDWCombatUnarmedTuning UnarmedTuning;FDWSpearAttackPlan UnarmedPlan;
    DWCombatCommit::FActionId UnarmedAction,UnarmedPublishedAction;
    FVector UnarmedForward=FVector::ForwardVector;
    bool bUnarmedConfigured=false,bFormalUnarmedActive=false;
    void InitializeUnarmedRules();bool BeginFormalUnarmed(FVector Target);void EndFormalUnarmed();
    bool IsCurrentFormalUnarmed(DWCombatCommit::FActionId)const;
    void ResolveFormalUnarmedWindow(DWCombatCommit::FActionId);void AdvanceFormalUnarmed(double);
    FDWSwordTuning SwordTuning;FDWSwordAttackPlan SwordPlan;
    DWCombatCommit::FActionId SwordAction,SwordPublishedAction;
    FVector SwordForward=FVector::ForwardVector;double SwordPriorAttackLock=0.;
    double SwordReadyPresentationPhase=0.; // Presentation only; never serialized or consulted by gameplay.
    void AdvanceSwordReadyPresentation(double GameSeconds);
    bool bFormalSwordConfigured=false,bFormalSwordActive=false,bSwordVisualTail=false,bSwordReleasePending=false;
    void InitializeFormalSwordRules();bool BeginFormalSword(FVector);void EndFormalSword();
    void CancelFormalSwordFacts();bool RefreshFormalSwordReleaseAim(DWCombatCommit::FActionId);
    void QueueFormalSwordChargeRelease();void PublishFormalSwordChargeReadyEffects();
    void PublishFormalSwordStartEffects();bool IsCurrentFormalSword(DWCombatCommit::FActionId)const;
    void ResolveFormalSwordWindow(DWCombatCommit::FActionId);void AdvanceFormalSword(double);void AdvanceFormalSwordVisualTail(double);
    FDWShieldAxeTuning ShieldAxeTuning;FDWShieldAxeDefense ShieldDefense;FDWSpearAttackPlan AxePlan;
    DWCombatCommit::FActionId AxeAction;FVector AxeForward=FVector::ForwardVector;
    bool bShieldAxeConfigured=false,bFormalAxeActive=false;int32 AxePendingPresentationSerial=0;
    double AxeSwingCounterMultiplier=1.;
    void InitializeShieldAxeRules();bool BeginFormalAxeSwing(FVector);bool BeginFormalShieldGuard();
    void ReleaseFormalShieldGuard();void PublishFormalAxeStartEffects();void EndFormalAxe();
    bool IsCurrentFormalAxe(DWCombatCommit::FActionId)const;
    void ResolveFormalAxeWindow(DWCombatCommit::FActionId);void AdvanceFormalAxe(double);
    FDWDaggerTuning DaggerTuning;FDWSpearAttackPlan DaggerPlan;
    DWCombatCommit::FActionId DaggerAction,DaggerPublishedAction;
    FVector DaggerForward=FVector::ForwardVector,DaggerQueuedTarget=FVector::ZeroVector;
    TWeakObjectPtr<ADWEnemyCharacter> DaggerBackstabTarget;
    double DaggerPriorAttackLock=0.;
    int32 DaggerComboStage=0;FVector2D DaggerSourceSegment=FVector2D::ZeroVector;
    bool ReadDaggerSourceSegment(int32 Segment,FVector2D& Out,double& FullSourceDuration)const;
    bool bFormalDaggerConfigured=false,bFormalDaggerActive=false,bDaggerVisualTail=false,bDaggerQueued=false,bDaggerBackstabSelected=false;
    void InitializeFormalDaggerRules();bool RequestFormalDagger(FVector);
    bool BeginFormalDagger(FVector,bool Chain=false,double Offset=-1.,double StartTime=-1.,int32 Segment=0);void EndFormalDagger();
    bool IsCurrentFormalDagger(DWCombatCommit::FActionId)const;void PublishFormalDaggerStartEffects();
    void ResolveFormalDaggerWindow(DWCombatCommit::FActionId);void AdvanceFormalDagger(double);void AdvanceFormalDaggerVisualTail(double);
    void RefreshDaggerEquipmentFacts();void BreakDaggerGraceForDamage();
    FDWSpearTuning SpearTuning;
    FDWSpearSourceTiming SpearSourceTiming;
    FDWSpearAttackPlan SpearPlan;
    TWeakObjectPtr<UAnimSequenceBase> SpearSourceSequence;
    DWCombatCommit::FActionId SpearAction;
    FVector SpearForward=FVector::ForwardVector;
    bool bFormalSpearConfigured=false,bFormalSpearActive=false,bAdvancingFormalSpear=false;
    bool RequiresFormalSpearBinding() const {return bEnableFormalSpearExplicitBinding||bFormalSpearConfigured;}
    void InitializeExplicitFormalSpearBinding();
    EDWSpearForm ReadFormalSpearForm(double FrameOffset=-1.) const;
    bool BeginFormalSpear(FVector Target);
    void EndFormalSpear();
    bool IsCurrentFormalSpear(DWCombatCommit::FActionId Action) const;
    void ResolveFormalSpearWindow(DWCombatCommit::FActionId Action);
    void AdvanceFormalSpear(double DeltaSeconds);
    void AdvanceAnimationStep(double DeltaSeconds);
    double GetCombatActionWorldSeconds() const;
    uint64 CombatBoundaryRevision=0;
    bool bAdvancingCombatClock=false,bCombatAdvanceInterrupted=false;
    double CombatAdvanceEndWorldSeconds=0.;
    FDWCombatWeaponResourceState CombatWeaponResources;
    DWCombatCommit::FActionId ReloadResourceAction,MagazineResourceAction;
    DWCombatCommit::FReceiptId LastWeaponResourceReceipt;
    uint64 ReloadShellOrdinal=0;
    bool bReloadShellCompletionDue=false,bReloadShellCommitted=false,bMagazineShotEventDue=false;
    void InitializeCombatWeaponRules();
    bool CanUseShotgunResources() const;
    bool HasShotgunReloadCapacityAndStock() const;
    bool EnterReloadShellPhase();
    void EndWeaponResourceActions();
    UPROPERTY(Transient) FDWWeaponAnimationClip ActiveClip;
    UPROPERTY(Transient) TObjectPtr<UDWWeaponAnimationSet> ActiveSet;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> PrimaryVisual;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> SecondaryVisual;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> PrimaryDeformedVisual;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> SecondaryDeformedVisual;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> PrimaryTrailVisual;
    UPROPERTY(Transient) TObjectPtr<UProceduralMeshComponent> SecondaryTrailVisual;
    TOptional<FDWCombatInputPress> PrimaryInputOrigin;
    TOptional<FDWCombatInputPress> SecondaryInputOrigin;
    TMap<UProceduralMeshComponent*,FDWWeaponGeometryCache> GeometryCaches;
    FDWWeaponGeometryStats GeometryStats;
    FVector TargetLocation=FVector::ZeroVector;
    double ElapsedSeconds=0.,NextAttackTime=0.,BlockStartTime=0.;
    bool bSwordSlamFeedbackPlayed=false,bSwordSweepFeedbackPlayed=false;
    double GetNextFormalSpearBoundarySeconds()const;
    double GetNextSwordPresentationBoundary()const;
    double MapPresentationEventToCombatSeconds(double SourceSeconds)const;
    void PublishSwordProgressEffects();
    bool bActive=false,bPrimaryHeld=false,bSecondaryHeld=false,bCharging=false,bChargeReady=false,bReloading=false,bOldOrient=true;
    bool bPrimaryTrailEmitting=false,bSecondaryTrailEmitting=false;
    TSet<int32> FiredEvents;
    ADWPlayerCharacter* Player() const;
    bool StartRole(FName Role,float StartSeconds=0.f);
    void FinishAction();
    void DispatchEvent(int32 Index);
    void AdvanceInternal(double DeltaSeconds,int32 TransitionBudget);
    void UpdateVisuals();
    void ApplyGeometry(UProceduralMeshComponent* Component,const TArray<FVector>& Vertices,const TArray<int32>& Triangles,const TArray<FVector2D>& UVs);
    void ResetTrails();
    void UpdateParticleTrail(TObjectPtr<UNiagaraComponent>& Component,UNiagaraSystem* System,FName ComponentName,
        const FTransform& WeaponTransform,const FTransform& SourceWeaponTransform,const FVector& SourceRoot,const FVector& SourceTip,
        const FVector& Offset,const FRotator& Rotation,const FVector& Scale,bool bShouldEmit,bool& bWasEmitting);
};
