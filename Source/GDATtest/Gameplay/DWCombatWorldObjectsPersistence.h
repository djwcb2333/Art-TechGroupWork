#pragma once
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatEffectsPersistence.h"
class ADWPlayerCharacter;
class ADWEnemyCharacter;
class UWorld;
struct FDWCombatWorldObjectsRestoreState;
struct FDWCombatWorldObjectsRestoreStateDeleter {void operator()(FDWCombatWorldObjectsRestoreState*)const;};
class GDATTEST_API FDWCombatWorldObjectsRestorePlan
{
public:
    FDWCombatWorldObjectsRestorePlan();~FDWCombatWorldObjectsRestorePlan();
    FDWCombatWorldObjectsRestorePlan(FDWCombatWorldObjectsRestorePlan&&);
    FDWCombatWorldObjectsRestorePlan& operator=(FDWCombatWorldObjectsRestorePlan&&);
    FDWCombatWorldObjectsRestorePlan(const FDWCombatWorldObjectsRestorePlan&)=delete;
    FDWCombatWorldObjectsRestorePlan& operator=(const FDWCombatWorldObjectsRestorePlan&)=delete;
    const TArray<FDWCombatObjectBinding>& LiveBindings()const;
    const TArray<FDWCombatObjectBinding>& RestoredBindings()const;
    const TArray<uint64>& KeepActionIds()const;
    bool IsStagingIntact(FString& Error)const;
    bool CanCommit(FString& Error)const;
    void DiscardStagedObjects(); // Only actors made by this plan; never destroy current world actors on rejection.
private:
    friend class FDWCombatWorldObjectsPersistence;
    TUniquePtr<FDWCombatWorldObjectsRestoreState,FDWCombatWorldObjectsRestoreStateDeleter> State;
};
class GDATTEST_API FDWCombatWorldObjectsPersistence
{
public:
    static void RecordDestroyedEnemy(ADWEnemyCharacter&);
    static bool Capture(UWorld&,ADWPlayerCharacter&,FDWCombatWorldObjectsSave&,TArray<FDWPersistedActorState>& OutLegacy,
        TArray<FDWCombatObjectBinding>& OutBindings,FString& Error);
    static bool PrepareWorldObjects(UWorld&,ADWPlayerCharacter&,const FDWCombatWorldObjectsSave&,
        const TArray<FDWPersistedActorState>& Legacy,FDWCombatWorldObjectsRestorePlan&,FString& Error);
    static bool PrepareWorldFacts(FDWCombatWorldObjectsRestorePlan&,const FDWCombatAuthorityRestorePlan&,FString& Error);
    // Root's single world-restore barrier must be active. No Spawn/Destroy/transform/delegate operations here.
    static void AdoptNativeFacts(FDWCombatWorldObjectsRestorePlan&);
    // Root calls with all player/world/effect facts already adopted. Revalidates epoch after every scene callback.
    static bool Publish(FDWCombatWorldObjectsRestorePlan&,FString& Error);
private:
    friend class FDWCombatWorldObjectsRestorePlan;
    static bool CaptureEnemy(ADWEnemyCharacter&,const TArray<FDWCombatObjectBinding>&,FDWCombatEnemyObjectSave&,FString&);
    static bool CaptureInternal(UWorld&,ADWPlayerCharacter&,FDWCombatWorldObjectsSave&,TArray<FDWPersistedActorState>&,
        TArray<FDWCombatObjectBinding>&,FString&,const TSet<TWeakObjectPtr<AActor>>& Ignored);
};
