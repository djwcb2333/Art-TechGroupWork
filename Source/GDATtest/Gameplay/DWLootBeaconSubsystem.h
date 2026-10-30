#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "TimerManager.h"
#include "DWLootBeaconSubsystem.generated.h"

class UStaticMeshComponent;
class UStaticMesh;
class UDWLootVisualCatalog;

USTRUCT()
struct FDWLootBeaconRecord
{
    GENERATED_BODY()
    UPROPERTY(Transient) TWeakObjectPtr<AActor> Owner;
    UPROPERTY(Transient) TWeakObjectPtr<UStaticMeshComponent> Anchor;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Visual;
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Beacon;
    UPROPERTY(Transient) TWeakObjectPtr<UStaticMesh> OriginalMesh;
    UPROPERTY(Transient) FName ItemId;
    bool bBorrowedHidden=false,bOriginalHidden=false;
};

/** Cosmetic-only world service. Callers must withdraw on collection/staging/EndPlay. */
UCLASS()
class GDATTEST_API UDWLootBeaconSubsystem : public UWorldSubsystem
{
    GENERATED_BODY()
public:
    // Must be called only after the source transaction has published its facts.
    bool PublishBeacon(AActor* Owner,UStaticMeshComponent* Anchor,FName ItemId,bool* bMeshChanged=nullptr);
    bool PublishDeathBag(AActor* Owner,UStaticMeshComponent* Anchor);
    void SuspendBeacon(AActor* Owner);
    // Immediate stop on withdrawal, before callbacks can re-enter gameplay.
    void WithdrawBeacon(AActor* Owner);
    virtual void Deinitialize() override;
private:
    bool LoadCatalog();
    bool IsCurrent(AActor* Owner,FName ItemId,UStaticMeshComponent* Anchor)const;
    bool PublishInternal(AActor* Owner,UStaticMeshComponent* Anchor,FName ItemId,bool bDeathBag,bool* bMeshChanged);
    void ClearRecord(int32 Index);
    void Rebalance();
    UPROPERTY(Transient) TObjectPtr<UDWLootVisualCatalog> Catalog;
    UPROPERTY(Transient) TArray<FDWLootBeaconRecord> Records;
    TSet<TWeakObjectPtr<AActor>> Building;
    FTimerHandle BudgetTimer;
};
