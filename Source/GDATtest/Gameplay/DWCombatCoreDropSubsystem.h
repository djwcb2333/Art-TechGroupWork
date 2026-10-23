#pragma once
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "DWEnemyCombatProfile.h"
#include "DWCombatCoreDropSubsystem.generated.h"

class ADWEnemyCharacter;
class ADWCombatCorePickup;
class FDWCombatRules;
class FDWCombatCorePickupTransaction;
class UDWCombatAuthoritySubsystem;

/** Terminal failure and claimed records are retained just like visible ground items. */
USTRUCT()
struct FDWCombatCoreDropSave
{
    GENERATED_BODY()
    UPROPERTY(SaveGame) int32 Schema=1;
    UPROPERTY(SaveGame) FGuid DeathId;
    UPROPERTY(SaveGame) FGuid StableEnemyId;
    UPROPERTY(SaveGame) EDWEnemyCombatProfile Profile=EDWEnemyCombatProfile::Legacy;
    UPROPERTY(SaveGame) EDWEnemyCoreDropDecision Decision=EDWEnemyCoreDropDecision::Unresolved;
    UPROPERTY(SaveGame) uint64 FatalEpoch=0;
    UPROPERTY(SaveGame) uint64 FatalReceipt=0;
    // Optional historical movement receipt for E09; zero for ordinary death records.
    UPROPERTY(SaveGame) uint64 ForcedFallReceiptEpoch=0;
    UPROPERTY(SaveGame) uint64 ForcedFallReceiptId=0;
    UPROPERTY(SaveGame) uint64 EnemyEntity=0;
    UPROPERTY(SaveGame) uint64 EnemyLife=0;
    UPROPERTY(SaveGame) uint64 PlayerEntity=0;
    UPROPERTY(SaveGame) uint64 PlayerLife=0;
    UPROPERTY(SaveGame) FGuid PickupId;
    UPROPERTY(SaveGame) FName ItemId;
    UPROPERTY(SaveGame) int32 Count=0;
    UPROPERTY(SaveGame) FVector Location=FVector::ZeroVector;
};
struct FDWPreparedCoreDrop
{
    FDWCombatCoreDropSave Save;
    FDWEnemyCoreDropRecord EnemyRecord;
    TWeakObjectPtr<ADWEnemyCharacter> Enemy;
    uint64 Revision=0;
    bool bValid=false;
};

/** World-owned drop facts; this is not another health/inventory authority. */
UCLASS()
class GDATTEST_API UDWCombatCoreDropSubsystem : public UTickableWorldSubsystem
{
    GENERATED_BODY()
public:
    virtual void Deinitialize() override;
    virtual void Tick(float DeltaTime) override;
    virtual TStatId GetStatId() const override;
    static UDWCombatCoreDropSubsystem* Get(const UObject* Context);
    bool CaptureSave(TArray<FDWCombatCoreDropSave>& Out,FString& Error) const;
    // Explicit cold-world loader only: never import mid-play independently of inventory/player restore.
    // D22 must coordinate the full world/player snapshot; no slot I/O is performed here.
    bool RestoreBeforeBeginPlay(const TArray<FDWCombatCoreDropSave>& Saved,const FDWCombatRules& Rules,FString& Error);
    bool ValidateEnemySavedDrop(const FDWEnemyFormalSaveData& Saved,float Health) const;
    bool BindRestoredEnemy(ADWEnemyCharacter& Enemy,const FDWEnemyFormalSaveData& Saved);
    bool FindDrop(FGuid DeathId,FDWCombatCoreDropSave& Out) const;
    ADWCombatCorePickup* FindPhysicalPickup(FGuid PickupId) const;
    uint64 GetRevision() const {return Revision;}
protected:
    virtual bool DoesSupportWorldType(EWorldType::Type Type) const override;
private:
    friend class FDWCombatWorldObjectsPersistence;
    friend class UDWCombatAuthoritySubsystem;
    friend class FDWCombatCorePickupTransaction;
    friend class ADWCombatCorePickup;
    struct FRecord
    {
        FDWCombatCoreDropSave Save;
        TWeakObjectPtr<ADWEnemyCharacter> Origin;
        TWeakObjectPtr<ADWCombatCorePickup> Physical;
        double PickupRadiusCm=0.;
        bool bMayMaterialize=false,bRestored=false;
    };
    TMap<FGuid,FRecord> Records;
    uint64 Revision=1;
    bool bStopping=false,bUpdating=false;
    bool Operational() const;
    bool PrepareFatal(ADWEnemyCharacter& Enemy,DWCombatCommit::FReceiptId Receipt,
        DWCombatCommit::FEntityLife EnemyLife,DWCombatCommit::FEntityLife PlayerLife,FDWPreparedCoreDrop& Out,const FVector* ForcedFallPoint=nullptr,DWCombatCommit::FReceiptId ForcedFallReceipt={});
    void CommitFatalUnchecked(FDWPreparedCoreDrop&& Prepared);
    void Materialize(FGuid DeathId);
    void PhysicalEnded(ADWCombatCorePickup& Pickup);
    bool IsPhysicalCurrent(const ADWCombatCorePickup& Pickup) const;
};
