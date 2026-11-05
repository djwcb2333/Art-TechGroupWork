#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWWorldPickup.h"
#include "DWCombatRecoveryActors.generated.h"
class ADWPlayerCharacter;
class UStaticMeshComponent;
class USceneComponent;
class UDWCombatRecoverySubsystem;
class FDWCombatRecoveryTransaction;
class UDWPickupPresentationComponent;
class UDWInteractionPromptComponent;
/** Explicit capsule-centre destination. No default Map1 destination is invented. */
UCLASS(NotBlueprintable)
class GDATTEST_API ADWCombatSafePoint : public AActor
{
    GENERATED_BODY()
public:
    ADWCombatSafePoint();
    UPROPERTY(EditInstanceOnly,Category="Combat|Recovery") FGuid StablePointId;
    // Authored interaction distance. Zero disables activation until the map author supplies it.
    UPROPERTY(EditInstanceOnly,Category="Combat|Recovery",meta=(ClampMin="0",Units="cm")) double ActivationRadiusCm=0.;
    UFUNCTION(BlueprintCallable,Category="Combat|Recovery") bool ActivateForPlayer(ADWPlayerCharacter* Player);
private:
    friend class FDWCombatWorldObjectsPersistence;
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> Destination;
};
/** A recoverable world pickup with a capacity-limited atomic claim. */
UCLASS(NotBlueprintable,NotPlaceable)
class GDATTEST_API ADWCombatDeathBag : public AActor, public IDWWorldPickup
{
    GENERATED_BODY()
public:
    ADWCombatDeathBag();
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    FGuid GetBagId() const { return BagId; }
    UStaticMeshComponent* GetPickupMesh() const override { return Placeholder; }
    FName GetPickupItemId() const override { return NAME_None; }
    bool IsPickupAvailable() const override;
    bool IsPickupInReach(const AActor* User) const override;
    bool TryCollect(ADWPlayerCharacter*) override;
    void HandlePickupPresentationFinished() override;
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class UDWCombatRecoverySubsystem;
    friend class FDWCombatRecoveryTransaction;
    void PresentCommittedPickup();
    UPROPERTY(VisibleAnywhere) TObjectPtr<USceneComponent> PickupRoot;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Placeholder;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDWInteractionPromptComponent> Prompt;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UDWPickupPresentationComponent> PickupPresentation;
    FGuid BagId;
    bool bBound = false;
    bool bPickupVisualPending = false;
};
