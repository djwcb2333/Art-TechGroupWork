#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
#include "Components/ActorComponent.h"
#include "DWWeaponAnimationSet.h"
#include "DWGatheringActionTiming.h"
#include "DWBuildingDemolitionSession.h"
#include "DWGatheringComponent.generated.h"
class ADWPlayerCharacter;
class UDWGameplayConfig;
class UDWInventoryComponent;
class ADWGatheringResource;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UAnimSequenceBase;
USTRUCT(BlueprintType)
struct FDWGatheringItemModelBinding
{
 GENERATED_BODY()
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TObjectPtr<UStaticMesh> Mesh;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform GripAdjustment=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) TArray<TObjectPtr<UMaterialInterface>> MaterialOverrides;
 /** Model calibration remains GripAdjustment. These adjustments operate in the current visible-hand frame. */
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform HandAdjustment=FTransform::Identity;
 UPROPERTY(EditAnywhere,BlueprintReadWrite) FTransform IdleHandAdjustment=FTransform::Identity;
};
UENUM(BlueprintType)
enum class EDWGatheringWorkKind : uint8 { None,Demolition };
UCLASS(ClassGroup=(DoughWorld),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWGatheringComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWGatheringComponent();
    void InitializeGathering();
    bool IsGatheringEnabled() const;
    bool HandleInteraction(float DeltaSeconds,bool bHeld);
    bool HasAvailableInteractionTarget()const{return FindTarget()!=nullptr;}
    bool TryUseTool(FName ItemId);
    bool HandlePrimaryClick();
    void UpdateSimpleSpearVisual(const FTransform& GripToOwner,bool Show);
    /** Single post-mesh render submission; does not advance any action or grant a source. */
    bool UpdateSelectedToolVisual(const FTransform* SimpleSpearGrip);
    bool GetToolHandFrame(FTransform& Out)const;
    void ClearToolCarryPresentation(){bToolCarrySelected=false;HideGatheringToolVisual();}
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") bool bShowSelectedToolInHand=true;
    bool HasCraftingContext(FName RecipeId,FName StationId,FName BuildingId) const;
    /** Independent from click interaction reach. All requirements use this same live set. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Crafting|Proximity",meta=(ClampMin="1",Units="cm")) float CraftingStationRadiusCm=250.f;
    UFUNCTION(BlueprintPure,Category="Crafting|Proximity") TArray<FName> GetNearbyCraftingStations() const;
    UFUNCTION(BlueprintPure,Category="Crafting|Proximity") TArray<FName> GetRecipeStationRequirements(FName RecipeId) const;
    UFUNCTION(BlueprintPure,Category="Crafting|Proximity") bool EvaluateRecipeStationRequirements(FName RecipeId,TArray<FName>& MissingStations) const;
    /** Exact action ItemId wins over old type fallback. Missing IDs retain existing appearance. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") TMap<FName,FDWGatheringItemModelBinding> ItemModelBindings;
    bool FindBuildingPlacement(FName BuildingId,FTransform& Out) const;
    UFUNCTION(BlueprintPure,Category="Gathering") FName GetActiveTool() const {return ActiveTool;}
    void RestoreActiveTool(FName ItemId);
    void ClearActiveToolAfterShortcutForgotten();
    bool CaptureGatheringPersistence(FName& OutTool,double& OutCooldown,FString& Error);
    /** Pure validated facts only. The aggregate already validated the saved inventory. */
    void AdoptGatheringPersistenceFacts(FName ItemId,double CooldownRemaining);
    void PublishGatheringPersistencePresentation();
    UFUNCTION(BlueprintPure,Category="Gathering|Action") bool IsGatheringSwingActive() const { return SwingClock.bActive; }
    UFUNCTION(BlueprintPure,Category="Gathering|Action") float GetGatheringSwingProgress() const { return static_cast<float>(SwingClock.Progress()); }
    UFUNCTION(BlueprintCallable,Category="Gathering|Action") void CancelGatheringSwing();
    UFUNCTION(BlueprintPure,Category="Gathering|Work") EDWGatheringWorkKind GetWorkKind() const;
    UFUNCTION(BlueprintPure,Category="Gathering|Work") float GetDemolitionProgress() const;
    UFUNCTION(BlueprintPure,Category="Gathering|Work") AActor* GetDemolitionTarget() const;
    UFUNCTION(BlueprintPure,Category="Gathering|Action") float GetEffectiveToolSwingSeconds(FName ToolId) const;
    UFUNCTION(BlueprintCallable,Category="Gathering|Work") void CancelDemolition();
    /** Provisional test baseline: simple hammer seconds; advanced rate comes from its tool multiplier. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Demolition",meta=(ClampMin="0.05",Units="s")) float DemolitionWorkSeconds=4.f;
    virtual void Deactivate() override;
    UAnimSequenceBase* GetGatheringSequence() const { return ActiveGatheringSequence.Get(); }
    bool HasGatheringAnimationSnapshot() const;
    float GetGatheringSourceSeconds() const;
    float GetGatheringSourceDuration() const;

    // Reuse skeletal poses and grip samples only. Weapon damage events are never evaluated.
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") TObjectPtr<UDWWeaponAnimationSet> SharedTwoHandMotion;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") FName SharedMotionRole=TEXT("Gather_Clean");
    // Optional per-tool motion slots; null falls back to the existing shared animation.
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") TObjectPtr<UDWWeaponAnimationSet> AxeMotion;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") TObjectPtr<UDWWeaponAnimationSet> PickaxeMotion;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") TObjectPtr<UDWWeaponAnimationSet> HammerMotion;
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") FName AxeMotionRole=TEXT("Gather_Clean");
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") FName PickaxeMotionRole=TEXT("Gather_Clean");
    UPROPERTY(EditDefaultsOnly,BlueprintReadWrite,Category="Gathering|Action") FName HammerMotionRole=TEXT("Gather_Clean");
    /** New clean clip contains a complete cycle; never scrub its motion backwards. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action") bool bUseDirectGatheringSource=true;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="0.01",ClampMax="0.99")) float GatheringContactFraction=0.4857142857f;
    /** Negative keeps the shared contact fraction; independent source clips can specify their strike phase. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="-1",ClampMax="0.99")) float AxeGatheringContactFraction=-1.f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="-1",ClampMax="0.99")) float PickaxeGatheringContactFraction=-1.f;
    /** Keep the outgoing evaluated sequence through the existing short AnimBP idle blend. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="0",Units="s")) float GatheringPoseBlendHoldSeconds=.12f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="0.1",Units="s")) float MinimumSwingSeconds=.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="0.05",ClampMax="0.65")) float WindupFraction=.40f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="0.05",ClampMax="0.4")) float DownstrokeFraction=.15f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action",meta=(ClampMin="0",ClampMax="0.2")) float ImpactHoldFraction=.09f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action|Source",meta=(ClampMin="0",Units="s")) float SourceStartSeconds=.75f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action|Source",meta=(ClampMin="0",Units="s")) float SourceRaisedSeconds=1.6f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action|Source",meta=(ClampMin="0",Units="s")) float SourceContactSeconds=1.8f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Action|Source",meta=(ClampMin="0",Units="s")) float SourceEndSeconds=1.95f;
    // Meshes use the authored grip origin. Null = explicit temporary handle/head geometry.
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") TObjectPtr<UStaticMesh> AxeMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") TObjectPtr<UStaticMesh> PickaxeMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") TObjectPtr<UStaticMesh> HammerMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") TObjectPtr<UMaterialInterface> ToolHandleMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") TObjectPtr<UMaterialInterface> ToolHeadMaterial;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform ToolGripAdjustment=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform AxeMeshAdjustment=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform PickaxeMeshAdjustment=FTransform::Identity;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform HammerMeshAdjustment=FTransform::Identity;
    /** Per-tool fallback geometry in grip space. Authored meshes use their existing path. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform AxeProxyHandleTransform=FTransform(FQuat::Identity,FVector(0,0,-30),FVector(.07,.07,1.6));
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform PickaxeProxyHandleTransform=FTransform(FQuat::Identity,FVector(0,0,-30),FVector(.07,.07,1.6));
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform AxeProxyHeadTransform=FTransform(FQuat::Identity,FVector(0,0,-110),FVector(.48,.16,.45));
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Tool Visual") FTransform PickaxeProxyHeadTransform=FTransform(FQuat::Identity,FVector(0,0,-110),FVector(.95,.14,.18));
    static UDWGatheringComponent* Find(const UDWInventoryComponent& Inventory);
    static void Notify(ADWPlayerCharacter* Player,const FString& Chinese,const FString& English);
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
private:
    ADWPlayerCharacter* Player() const;
    const UDWGameplayConfig* Profile() const;
    bool CanAct() const;
    AActor* FindTarget() const;
    bool InteractWith(AActor* Target,bool bPressed);
    bool StartGatheringSwing(ADWGatheringResource* Resource);
    bool IsDemolitionTarget(const class ADWGatheringStation* Target) const;
    bool StartDemolitionSwing();
    bool TryCommitDemolition();
    void StopGatheringSwingOnly();
    void BeginDemolitionFeedback();void UpdateDemolitionFeedback();void EndDemolitionFeedback();
    TWeakObjectPtr<class UDWInteractionPromptComponent> DemolitionPrompt;
    FGuid DemolitionPromptId;
    FDWBuildingDemolitionSession Demolition;
    bool bDemolitionSwing=false;
    bool IsPendingHitCurrent() const;
    void UpdateGatheringToolVisual();
    bool UpdateIdleSelectedToolVisual();
    FTransform ApplyToolHandAdjustment(FName Id,const FTransform& ModelToOwner,bool Idle)const;
    bool bToolCarrySelected=true;
    void HideGatheringToolVisual();
    UPROPERTY(Transient) FName ActiveTool;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> SimpleSpearFallbackMesh;
    UPROPERTY(Transient) TMap<int32,TObjectPtr<UMaterialInterface>> SimpleSpearFallbackMaterials;
    bool bSimpleSpearVisualShown=false,bSimpleSpearVisualLookupAttempted=false;
    UPROPERTY(Transient) TObjectPtr<UAnimSequenceBase> ActiveGatheringSequence;
    UPROPERTY(Transient) TObjectPtr<UStaticMesh> ProxyCube;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> GatheringToolHandle;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> GatheringToolHead;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> GatheringAuthoredTool;
    TArray<FDWWeaponVisualSample> ActiveVisualSamples;
    TWeakObjectPtr<ADWGatheringResource> SwingTarget;
    TWeakObjectPtr<UDWInventoryComponent> SwingInventory;
    DWGatheringActionTiming::FClock SwingClock;
    DWGatheringActionTiming::FProfile SwingTiming;
    FName SwingToolId, SwingToolType;
    float SwingSharpness=0.f, SwingReach=0.f, SwingSourceDuration=1.f;
    float SwingUseInterval=0.f, SwingSpeedMultiplier=1.f;
    float ActiveSourceSeconds=0.f;
    bool bActiveDirectGatheringSource=false;
    double GatheringPoseHoldUntil=0.;
    DWCombatCommit::FActionId SwingA01Action;
    uint64 SwingGeneration=0, SwingEpoch=0, SwingPlayerEntity=0, SwingPlayerLife=0;
    uint64 SwingResourceEntity=0, SwingResourceLife=0;
    int64 SwingInventoryRevision=0;
    TWeakObjectPtr<AActor> LastTarget;
    bool bWasHeld=false;
    double NextToolTime=0.;
    double NextFailureNotice=0.;
};
