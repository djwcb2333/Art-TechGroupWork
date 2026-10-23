#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWCombatMine.generated.h"
class UStaticMeshComponent;
class USceneComponent;
class UDWCombatMineSubsystem;

/** Native-only temporary geometry. No overlap delegate can independently deal damage. */
UCLASS(NotBlueprintable,NotPlaceable)
class GDATTEST_API ADWCombatMine : public AActor
{
    GENERATED_BODY()
public:
    ADWCombatMine();
    FGuid GetMineId() const{return MineId;}
    FGuid GetRingId() const{return RingId;}
    bool TryTrigger();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(EEndPlayReason::Type Reason) override;
private:
    friend class UDWCombatMineSubsystem;
    friend class FDWCombatMinePersistence;
    UPROPERTY() TObjectPtr<USceneComponent> MineRoot;
    UPROPERTY() TObjectPtr<UStaticMeshComponent> Placeholder;
    FGuid MineId,RingId;
};
