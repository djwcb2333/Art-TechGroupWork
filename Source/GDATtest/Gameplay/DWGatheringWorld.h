#pragma once

#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "DWGameplayTypes.h"
#include "DWGatheringWorld.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UDWInteractionPromptComponent;
class UDWBuildingEffectsComponent;
class UDWGatheringPresentationComponent;
class UDWPickupPresentationComponent;
class UStaticMesh;
class ADWPlayerCharacter;
class FDWGatheringPersistence;

/** Gathering-only world objects; no dependency on legacy ResourceNode persistence. */
UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringInteractable : public AActor
{
    GENERATED_BODY()
public:
    ADWGatheringInteractable();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") TObjectPtr<UStaticMeshComponent> Mesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") TObjectPtr<UDWInteractionPromptComponent> Prompt;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName PersistentId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="1", Units="cm")) float InteractionRadius = 250.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="0", Units="cm")) float HeightTolerance = 180.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Visual", meta=(ClampMin="0.1", ClampMax="2")) float RandomScaleMin = .7f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Visual", meta=(ClampMin="0.1", ClampMax="2")) float RandomScaleMax = 1.f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") bool bRuntimeSpawned = false;
    UFUNCTION(BlueprintPure, Category="Gathering") bool IsWithinReach(const AActor* User, float MaxReach = 0.f) const;
    UFUNCTION(BlueprintPure, Category="Gathering") virtual bool IsInteractionAvailable() const;
    bool CanPlayerInteract(const ADWPlayerCharacter* User, float MaxReach = 0.f) const;
    bool IsStaged() const { return bStaged; }
    /** Facts only. Caller publishes after every participant has adopted its facts. */
    void SetStaged(bool Value) { bStaged = Value; }
    virtual void PublishInteractionState();
protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;
    bool IsGatheringWorld() const;
    bool bStaged = false;
private:
    friend class FDWGatheringPersistence;
};

UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringResource : public ADWGatheringInteractable
{
    GENERATED_BODY()
public:
    ADWGatheringResource();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Gathering Presentation") TObjectPtr<UDWGatheringPresentationComponent> GatheringPresentation;
    /** Runtime/node-owned progress survives leaving and returning; this is not disk persistence. */
    UFUNCTION(BlueprintPure,Category="Gathering") float GetGatheringProgress() const;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Drops") TMap<FName,TSoftObjectPtr<UStaticMesh>> DropMeshes;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Drops") TMap<FName,FTransform> DropMeshTransforms;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName RequiredTool = TEXT("Axe");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="1")) double MaxDurability = 100.;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="0")) double Durability = 100.;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") TArray<FDWItemStack> Drops;
    /** Minimum reward delay. Animated resources wait at least until their fall/fracture contact. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="0", Units="s")) float DropDelaySeconds = .35f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") bool bDropsReleased = false;
    virtual bool IsInteractionAvailable() const override;
    bool ApplyToolHit(ADWPlayerCharacter* User, FName Kind, double Sharpness, float Reach,FName ToolItemId=NAME_None,DWCombatCommit::FActionId ParentAction={});
    bool ReleaseDrops();
    void RestoreResource(double Health, bool Released);
    float GetPendingDropSeconds() const;
    UFUNCTION() void HandleGatheringPhysicalGroundContact();
    /** Pure validated-fact assignment; scene and prompt changes are deferred to publish. */
    void AdoptResourceState(double Health, bool Released, float RemainingDelay);
    bool CanCaptureGatheringPersistence(FString& Error) const;
    bool CanPrepareGatheringPersistence(bool bSavedReleased,FString& Error) const;
    void ResetGatheringPersistencePresentation();
    bool PublishGatheringPersistencePresentation(FString& Error);
    void PublishResourceState();
    virtual void PublishInteractionState() override;
    UFUNCTION(BlueprintImplementableEvent, Category="Gathering") void OnGatheringHit(double Damage, double RemainingDurability);
    UFUNCTION(BlueprintImplementableEvent, Category="Gathering") void OnGatheringExhausted();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    friend class FDWGatheringPersistence;
    void ScheduleDrops(float Delay);
    FTimerHandle DropTimer;
    FTimerHandle FeedbackHoldTimer;
    float PendingDropDelay = -1.f;
    bool bApplyingHit = false;
    bool bReleasingDrops = false;
    bool bRestorePresentationPending = false;
    bool bPersistenceSourceDefaultsCaptured = false;
    bool bPersistenceSourceVisible = true;
    bool bPersistenceSourceHiddenInGame = false;
    ECollisionEnabled::Type PersistenceSourceCollision = ECollisionEnabled::QueryAndPhysics;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> RestoredStump;
};

UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringPickup : public ADWGatheringInteractable
{
    GENERATED_BODY()
public:
    ADWGatheringPickup();
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Pickup Presentation") TObjectPtr<UDWPickupPresentationComponent> PickupPresentation;
    /** Visual completion only; quantity is already committed and never adjusted here. */
    void HandlePickupPresentationFinished();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName ItemId = TEXT("SugarChunk");
    /** Opt out of catalog replacement to preserve a deliberately authored placeholder mesh. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Visual") bool bUseItemCatalogVisuals=true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="0")) int32 Quantity = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Visual") bool bPopOnSpawn = false;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Visual",meta=(ClampMin="0.05",Units="s")) float PopDuration=.55f;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Gathering|Visual",meta=(ClampMin="0",Units="cm")) float PopHeight=65.f;
    /** Set before deferred FinishSpawning; only the mesh flies, logical pickup identity remains stable. */
    void SetPopOrigin(const FVector& Origin) { PopOriginWorld=Origin; bHasPopOrigin=true; }
    virtual bool IsInteractionAvailable() const override;
    bool TryCollect(ADWPlayerCharacter* User);
    void RefreshPickup();
    bool CanCaptureGatheringPersistence(FString& Error) const;
    /** Called only inside the complete aggregate adoption barrier. */
    void AdoptGatheringPersistenceFacts(FName SavedItem,int32 SavedQuantity);
    void ResetGatheringPersistencePresentation();
    virtual void PublishInteractionState() override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnConstruction(const FTransform& Transform) override;
    virtual void Tick(float DeltaSeconds) override;
private:
    friend class FDWGatheringPersistence;
    friend class FDWGatheringWorldPersistence;
    bool bCollecting = false;
    bool bPopStarted = false;
    float PopElapsed = 0.f;
    FVector MeshRestLocation = FVector::ZeroVector;
    FVector PopOriginWorld = FVector::ZeroVector;
    bool bHasPopOrigin = false;
};

UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringStation : public ADWGatheringInteractable
{
    GENERATED_BODY()
public:
    ADWGatheringStation();
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building Effects") TObjectPtr<UDWBuildingEffectsComponent> BuildingEffects;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName StationId = TEXT("GatherStation_CraftingTable");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName BuildingItemId = TEXT("CraftingTable");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") bool bStationEnabled = true;
    // 0 legacy unknown, 1 recipe-at-placement, 2 unresolved placement, 3 actual direct-craft, 4 saved legacy recipe estimate.
    // These are data facts only. Never call this historical payment for recipe-at-placement.
    UPROPERTY(Transient) uint8 DemolitionCostOrigin=0;
    UPROPERTY(Transient) FName DemolitionCostRecipe;
    UPROPERTY(Transient) TArray<FDWItemStack> DemolitionCostInputs;
    /** Authored map actors remain protected unless explicitly enabled on that instance. */
    UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Gathering|Demolition") bool bAllowAuthoredDemolition=false;
    /** Optional explicit materials for an enabled map-authored building; empty means no materials were configured. */
    UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Gathering|Demolition") TArray<FDWItemStack> AuthoredDemolitionReturn;
    UPROPERTY(EditInstanceOnly,BlueprintReadWrite,Category="Gathering|Demolition") bool bAllowAuthoredEmptyReturn=false;
    UPROPERTY(Transient) bool bDemolished=false;
    virtual bool IsInteractionAvailable() const override;
    /** Configure data only, so deferred/staged transactions cannot publish early. */
    bool ConfigureBuilding(FName ItemId);
    virtual void PublishInteractionState() override;
};

UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringRemains : public ADWGatheringInteractable
{
    GENERATED_BODY()
public:
    ADWGatheringRemains();
};
