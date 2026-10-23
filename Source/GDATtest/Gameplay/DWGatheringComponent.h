#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWGatheringComponent.generated.h"
class ADWPlayerCharacter;
class UDWGatheringProfile;
class UDWInventoryComponent;
UCLASS(ClassGroup=(DoughWorld),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWGatheringComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDWGatheringComponent();
    void InitializeGathering();
    bool IsGatheringEnabled() const;
    bool HandleInteraction(float DeltaSeconds,bool bHeld);
    bool TryUseTool(FName ItemId);
    bool HandlePrimaryClick();
    bool HasCraftingContext(FName RecipeId,FName StationId,FName BuildingId) const;
    bool FindBuildingPlacement(FName BuildingId,FTransform& Out) const;
    UFUNCTION(BlueprintPure,Category="Gathering") FName GetActiveTool() const {return ActiveTool;}
    void RestoreActiveTool(FName ItemId);
    static UDWGatheringComponent* Find(const UDWInventoryComponent& Inventory);
    static void Notify(ADWPlayerCharacter* Player,const FString& Chinese,const FString& English);
protected:
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
    ADWPlayerCharacter* Player() const;
    const UDWGatheringProfile* Profile() const;
    bool CanAct() const;
    AActor* FindTarget() const;
    bool InteractWith(AActor* Target,bool bPressed);
    UPROPERTY(Transient) FName ActiveTool;
    TWeakObjectPtr<AActor> LastTarget;
    bool bWasHeld=false;
    double NextToolTime=0.;
    double NextFailureNotice=0.;
};
