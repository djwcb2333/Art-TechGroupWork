#pragma once
#include "DWCombatEffectsSave.h"
class UDWCombatAuthoritySubsystem;
class UDWCombatGroundAreaSubsystem;
struct FDWCombatAuthorityRestoreState;
struct FDWCombatAuthorityRestoreStateDeleter{void operator()(FDWCombatAuthorityRestoreState*)const;};
struct FDWCombatEffectsRestoreState;
struct FDWCombatEffectsRestoreStateDeleter{void operator()(FDWCombatEffectsRestoreState*)const;};

class GDATTEST_API FDWCombatAuthorityRestorePlan
{
public:
    FDWCombatAuthorityRestorePlan();~FDWCombatAuthorityRestorePlan();
    FDWCombatAuthorityRestorePlan(FDWCombatAuthorityRestorePlan&&);FDWCombatAuthorityRestorePlan& operator=(FDWCombatAuthorityRestorePlan&&);
    FDWCombatAuthorityRestorePlan(const FDWCombatAuthorityRestorePlan&)=delete;FDWCombatAuthorityRestorePlan& operator=(const FDWCombatAuthorityRestorePlan&)=delete;
    uint64 NewEpoch()const;
    TArray<DWCombatCommit::FReceiptId> PendingDamageReceipts()const;
    std::optional<DWCombatCommit::FReceipt> FindReceipt(uint64 Id)const;
    AActor* FindActor(uint64 Entity)const;
    AActor* FindActor(const FString& StableId)const;
    DWCombatCommit::FEntityLife FindLife(uint64 Entity)const;
    uint64 FindRestoreRevision(uint64 Entity)const;
    TArray<DWCombatCommit::FActionId> ActiveActions()const;
    DWCombatCommit::FEntityLife FindActionOwner(uint64 Id)const;
private:
    friend class UDWCombatAuthoritySubsystem;friend class FDWCombatEffectsPersistence;
    TUniquePtr<FDWCombatAuthorityRestoreState,FDWCombatAuthorityRestoreStateDeleter> State;
};
class GDATTEST_API FDWCombatEffectsRestorePlan
{
public:
    FDWCombatEffectsRestorePlan();~FDWCombatEffectsRestorePlan();
    FDWCombatEffectsRestorePlan(FDWCombatEffectsRestorePlan&&);FDWCombatEffectsRestorePlan& operator=(FDWCombatEffectsRestorePlan&&);
    FDWCombatEffectsRestorePlan(const FDWCombatEffectsRestorePlan&)=delete;FDWCombatEffectsRestorePlan& operator=(const FDWCombatEffectsRestorePlan&)=delete;
    FDWCombatAuthorityRestorePlan& Authority(){return AuthorityPlan;}
    const FDWCombatAuthorityRestorePlan& Authority()const{return AuthorityPlan;}
    // Root calls only after every world participant has validated. Rejects stale candidates before any adoption.
    bool CanCommit(FString& Error)const;
private:
    friend class FDWCombatEffectsPersistence;
    FDWCombatAuthorityRestorePlan AuthorityPlan;
    TUniquePtr<FDWCombatEffectsRestoreState,FDWCombatEffectsRestoreStateDeleter> State;
};
class GDATTEST_API FDWCombatEffectsPersistence
{
public:
    static bool Capture(UDWCombatAuthoritySubsystem&,const TArray<FDWCombatObjectBinding>&,FDWCombatEffectsSave&,FString&);
    static bool PrepareRestore(UDWCombatAuthoritySubsystem&,const FDWCombatEffectsSave&,const TArray<FDWCombatObjectBinding>& LiveBindings,const TArray<FDWCombatObjectBinding>& RestoredBindings,
        const TArray<uint64>& KeepActionIds,FDWCombatEffectsRestorePlan&,FString&);
    // Requires root's native world-restore barrier. No scene operations, delegates, Spawn, or notifications.
    static void CommitFactsUnchecked(FDWCombatEffectsRestorePlan&);
    // Call only after root adopted player/enemy/shield/root-ring/drop/recovery facts and released its barrier.
    static void PublishRestoredEffects(FDWCombatEffectsRestorePlan&);
};
