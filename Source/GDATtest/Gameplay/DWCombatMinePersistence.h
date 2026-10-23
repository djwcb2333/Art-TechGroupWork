#pragma once
#include "DWCombatWorldSave.h"
#include "DWCombatEffectsPersistence.h"
#include "DWCombatMoldRoot.h"
class ADWPlayerCharacter;
struct FDWCombatWorldObjectsSave;
struct FDWCombatMineRestoreState;
struct FDWCombatMineRestoreStateDeleter {void operator()(FDWCombatMineRestoreState*)const;};
class GDATTEST_API FDWCombatMineRestorePlan
{
public:
    FDWCombatMineRestorePlan();~FDWCombatMineRestorePlan();
    FDWCombatMineRestorePlan(FDWCombatMineRestorePlan&&);FDWCombatMineRestorePlan& operator=(FDWCombatMineRestorePlan&&);
    FDWCombatMineRestorePlan(const FDWCombatMineRestorePlan&)=delete;FDWCombatMineRestorePlan& operator=(const FDWCombatMineRestorePlan&)=delete;
    bool CanCommit(FString& Error)const;
private:
    friend class FDWCombatMinePersistence;
    TUniquePtr<FDWCombatMineRestoreState,FDWCombatMineRestoreStateDeleter> State;
};
class GDATTEST_API FDWCombatMinePersistence
{
public:
    // Only writes root/ring/emitter/hit/MineConfig fields; other player fields remain owned by root's aggregate.
    static bool Capture(ADWPlayerCharacter&,FDWCombatPlayerExtendedSave&,FString&);
    static bool ValidateSnapshot(const FDWCombatPlayerExtendedSave&,const FDWCombatAuthoritySave&,FString&);
    static bool ValidateTargetHistory(const FDWCombatPlayerExtendedSave&,const FDWCombatWorldObjectsSave&,FString&);
    static bool Prepare(ADWPlayerCharacter&,const FDWCombatPlayerExtendedSave&,const FDWCombatAuthoritySave&,
        const FDWCombatAuthorityRestorePlan&,const FDWMoldRootIdentity& RestoredRootIdentity,FDWCombatMineRestorePlan&,FString&);
    // Same callback-free authority barrier as Effects + Player + World; no independent apply/load.
    static void AdoptNativeFacts(FDWCombatMineRestorePlan&);
    static bool Publish(FDWCombatMineRestorePlan&,FString&);
private:
    friend class FDWCombatMineRestorePlan;
    static bool Fingerprint(class UDWCombatMineSubsystem&,ADWPlayerCharacter&,TArray<uint8>&,FString&);
};
