#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWWorldPickup.h"
#include "DWCombatCorePickup.generated.h"
class USceneComponent;
class UDWInteractionPromptComponent;
class UDWPickupPresentationComponent;
class UStaticMeshComponent;
class ADWPlayerCharacter;
class UDWCombatCoreDropSubsystem;
class FDWCombatCorePickupTransaction;
UCLASS(NotBlueprintable)
class GDATTEST_API ADWCombatCorePickup : public AActor, public IDWWorldPickup
{
    GENERATED_BODY()
public:
    ADWCombatCorePickup();
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    FGuid GetPickupId()const{return PickupId;}
    FName GetCoreItemId()const{return ItemId;}
    bool IsClaimed()const{return bClaimed;}
    UStaticMeshComponent* GetPickupMesh() const override { return Placeholder; }
    FName GetPickupItemId() const override { return ItemId; }
    bool IsPickupAvailable() const override;
    bool IsPickupInReach(const AActor* User) const override;
    bool TryCollect(ADWPlayerCharacter* Player) override;
    void HandlePickupPresentationFinished() override;
private:
    void BeginDropPresentation(const FVector* PopOrigin);
    void PresentCommittedPickup();
    friend class FDWCombatWorldObjectsPersistence;
    friend class UDWCombatCoreDropSubsystem;
    friend class FDWCombatCorePickupTransaction;
    UPROPERTY(VisibleAnywhere,Category="Core") TObjectPtr<USceneComponent> PickupRoot;
    UPROPERTY(VisibleAnywhere,Category="Core") TObjectPtr<UDWInteractionPromptComponent> Prompt;
    UPROPERTY(VisibleAnywhere,Category="Core") TObjectPtr<UDWPickupPresentationComponent> PickupPresentation;
    UPROPERTY(VisibleAnywhere,Category="Core") TObjectPtr<UStaticMeshComponent> Placeholder;
    FGuid DeathId,PickupId;
    FName ItemId;
    int32 Count=0;
    double PickupRadiusCm=0.;
    bool bBound=false,bClaimed=false;
    TMap<TWeakObjectPtr<ADWPlayerCharacter>,int64> LastFullInventoryRevision;
};
