#pragma once
#include "CoreMinimal.h"
#include "DWGatheringWorld.h"
#include "DWPlaceableBuilding.generated.h"

class UDWBuildingDefinition;
class UBoxComponent;

/** One Blueprint subclass per building, backed by the same data-driven native behaviour. */
UCLASS(Blueprintable)
class GDATTEST_API ADWPlaceableBuilding : public ADWGatheringStation
{
    GENERATED_BODY()
public:
    ADWPlaceableBuilding();
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Building") TObjectPtr<UDWBuildingDefinition> Definition;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Building") TObjectPtr<UBoxComponent> BuildingCollision;
    UFUNCTION(BlueprintCallable, Category="Building") bool RebuildBuildingVisuals();
    UFUNCTION(BlueprintPure, Category="Building") bool HasCompleteBuildingVisuals() const {return bVisualsReady;}
    virtual bool IsInteractionAvailable() const override;
    virtual void PublishInteractionState() override;
protected:
    virtual void BeginPlay() override;
    virtual void OnConstruction(const FTransform& Transform) override;
private:
    UPROPERTY(Transient) TArray<TObjectPtr<UStaticMeshComponent>> GeneratedParts;
    UPROPERTY(Transient) bool bVisualsReady = false;
};
