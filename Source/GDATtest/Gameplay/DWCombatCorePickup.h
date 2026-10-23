#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCombatCorePickup.generated.h"
class USphereComponent;
class UStaticMeshComponent;
class ADWPlayerCharacter;
class UDWCombatCoreDropSubsystem;
class FDWCombatCorePickupTransaction;
UCLASS(NotBlueprintable)
class GDATTEST_API ADWCombatCorePickup : public AActor
{
    GENERATED_BODY()
public:
    ADWCombatCorePickup();
    virtual void Tick(float DeltaSeconds) override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
    FGuid GetPickupId()const{return PickupId;}
    FName GetCoreItemId()const{return ItemId;}
    bool IsClaimed()const{return bClaimed;}
    // Same validated entry as proximity polling; no inventory mutation is exposed.
    bool TryCollect(ADWPlayerCharacter* Player);
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class UDWCombatCoreDropSubsystem;
    friend class FDWCombatCorePickupTransaction;
    UPROPERTY(VisibleAnywhere,Category="Core") TObjectPtr<USphereComponent> PickupRange;
    UPROPERTY(VisibleAnywhere,Category="Core") TObjectPtr<UStaticMeshComponent> Placeholder;
    FGuid DeathId,PickupId;
    FName ItemId;
    int32 Count=0;
    double PickupRadiusCm=0.;
    bool bBound=false,bClaimed=false;
    TMap<TWeakObjectPtr<ADWPlayerCharacter>,int64> LastFullInventoryRevision;
};
