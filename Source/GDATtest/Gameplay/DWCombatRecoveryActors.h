#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCombatRecoveryActors.generated.h"
class ADWPlayerCharacter;
class UStaticMeshComponent;
class USceneComponent;
class UDWCombatRecoverySubsystem;
class FDWCombatRecoveryTransaction;
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
/** Native placeholder; capsule contact offers one capacity-limited atomic claim. */
UCLASS(NotBlueprintable,NotPlaceable)
class GDATTEST_API ADWCombatDeathBag : public AActor
{
    GENERATED_BODY()
public:
    ADWCombatDeathBag();
    virtual void Tick(float DeltaTime) override;
    bool TryCollect(ADWPlayerCharacter*);
    FGuid GetBagId()const{return BagId;}
    bool Touches(const ADWPlayerCharacter&)const;
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class UDWCombatRecoverySubsystem;
    friend class FDWCombatRecoveryTransaction;
    UPROPERTY(VisibleAnywhere) TObjectPtr<UStaticMeshComponent> Placeholder;
    FGuid BagId;
    bool bBound=false;
    int64 LastAttemptInventoryRevision=INDEX_NONE;
};
