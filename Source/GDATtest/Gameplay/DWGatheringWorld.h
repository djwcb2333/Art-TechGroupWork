#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWGameplayTypes.h"
#include "DWGatheringWorld.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UDWInteractionPromptComponent;
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
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName RequiredTool = TEXT("Axe");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="1")) double MaxDurability = 100.;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="0")) double Durability = 100.;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") TArray<FDWItemStack> Drops;
    /** Provisional delay until a real exhaustion animation is provided. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="0", Units="s")) float DropDelaySeconds = .35f;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Gathering") bool bDropsReleased = false;
    virtual bool IsInteractionAvailable() const override;
    bool ApplyToolHit(ADWPlayerCharacter* User, FName Kind, double Sharpness, float Reach);
    bool ReleaseDrops();
    void RestoreResource(double Health, bool Released);
    float GetPendingDropSeconds() const;
    void AdoptResourceState(double Health, bool Released, float RemainingDelay);
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
    float PendingDropDelay = -1.f;
    bool bApplyingHit = false;
    bool bReleasingDrops = false;
};

UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringPickup : public ADWGatheringInteractable
{
    GENERATED_BODY()
public:
    ADWGatheringPickup();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName ItemId = TEXT("SugarChunk");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering", meta=(ClampMin="0")) int32 Quantity = 1;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering|Visual") bool bPopOnSpawn = false;
    virtual bool IsInteractionAvailable() const override;
    bool TryCollect(ADWPlayerCharacter* User);
    void RefreshPickup();
    virtual void PublishInteractionState() override;
protected:
    virtual void BeginPlay() override;
    virtual void Tick(float DeltaSeconds) override;
private:
    friend class FDWGatheringPersistence;
    bool bCollecting = false;
    bool bPopStarted = false;
    float PopElapsed = 0.f;
    FVector MeshRestLocation = FVector::ZeroVector;
};

UCLASS(Blueprintable)
class GDATTEST_API ADWGatheringStation : public ADWGatheringInteractable
{
    GENERATED_BODY()
public:
    ADWGatheringStation();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName StationId = TEXT("GatherStation_CraftingTable");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") FName BuildingItemId = TEXT("CraftingTable");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Gathering") bool bStationEnabled = true;
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
