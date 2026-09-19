#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWResourceNode.generated.h"

class USphereComponent;
class UStaticMeshComponent;
class UDWInteractionPromptComponent;

/** A hold-F resource source. Inventory acceptance is owned by the player. */
UCLASS(Blueprintable)
class GDATTEST_API ADWResourceNode : public AActor
{
    GENERATED_BODY()
public:
    ADWResourceNode();
    virtual void OnConstruction(const FTransform& Transform) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resource|Components") TObjectPtr<UStaticMeshComponent> ResourceMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resource|Components") TObjectPtr<USphereComponent> InteractionRange;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Resource|Components") TObjectPtr<UDWInteractionPromptComponent> InteractionPrompt;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Identity") FName PersistentId;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Harvest") FName ItemId = TEXT("Dough");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Harvest", meta=(ClampMin="0.05", Units="s")) float HarvestInterval = 3.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Harvest", meta=(ClampMin="1")) int32 HarvestAmount = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Harvest") bool bInfinite = false;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Harvest", meta=(ClampMin="0")) int32 RemainingAmount = 3;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Harvest", meta=(ClampMin="1", Units="cm")) float InteractionRadius = 220.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Resource|Harvest") bool bHideWhenDepleted = true;

    UFUNCTION(BlueprintPure, Category="Resource") bool IsAvailable() const;
    /** Additional eligibility rule. Stock/identity invariants still apply in IsAvailable. No inventory mutation here. */
    UFUNCTION(BlueprintNativeEvent, BlueprintPure, Category="Resource|Rules") bool CanHarvest() const;
    virtual bool CanHarvest_Implementation() const;
    /** Call only after the same amount has been successfully accepted by inventory. */
    UFUNCTION(BlueprintCallable, Category="Resource") void CommitHarvest(int32 ActualAmount);
    UFUNCTION(BlueprintCallable, Category="Resource|Save") void RestoreRemainingAmount(int32 NewAmount);
    UFUNCTION(BlueprintImplementableEvent, Category="Resource") void OnHarvestCommitted(int32 ActualAmount);
    /** Emitted after a committed harvest/load changes availability, and once at BeginPlay. */
    UFUNCTION(BlueprintImplementableEvent, Category="Resource|Events") void OnAvailabilityChanged(bool bAvailable);
protected:
    virtual void BeginPlay() override;
    void RefreshAvailability();
private:
    bool bAvailabilityInitialized = false;
    bool bLastAvailable = false;
    bool bCommittingHarvest = false;
};

UCLASS(Blueprintable)
class GDATTEST_API ADWWaterResourceNode : public ADWResourceNode
{
    GENERATED_BODY()
public:
    ADWWaterResourceNode();
};

UCLASS(Blueprintable)
class GDATTEST_API ADWYeastResourceNode : public ADWResourceNode
{
    GENERATED_BODY()
public:
    ADWYeastResourceNode();
};

UCLASS(Blueprintable)
class GDATTEST_API ADWDoughResourceNode : public ADWResourceNode
{
    GENERATED_BODY()
public:
    ADWDoughResourceNode();
};

UCLASS(Blueprintable)
class GDATTEST_API ADWFlourResourceNode : public ADWResourceNode
{
    GENERATED_BODY()
public:
    ADWFlourResourceNode();
};
