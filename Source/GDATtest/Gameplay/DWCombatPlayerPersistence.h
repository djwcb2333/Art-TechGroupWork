#pragma once
#include "DWCombatWorldSave.h"
#include "DWCombatEffectsPersistence.h"
#include "DWCombatMoldRoot.h"
class ADWPlayerCharacter;
class UDWSaveGame;
class FDWCombatRules;
struct FDWCombatPlayerRestoreState;
struct FDWCombatPlayerRestoreStateDeleter {void operator()(FDWCombatPlayerRestoreState*)const;};
struct FDWCombatShieldRestoreState;
struct FDWCombatShieldRestoreStateDeleter {void operator()(FDWCombatShieldRestoreState*)const;};

class GDATTEST_API FDWCombatShieldRestorePlan
{
public:
    FDWCombatShieldRestorePlan();~FDWCombatShieldRestorePlan();
    FDWCombatShieldRestorePlan(FDWCombatShieldRestorePlan&&);FDWCombatShieldRestorePlan& operator=(FDWCombatShieldRestorePlan&&);
private:
    friend class UDWCombatAuthoritySubsystem;
    TUniquePtr<FDWCombatShieldRestoreState,FDWCombatShieldRestoreStateDeleter> State;
};
class GDATTEST_API FDWCombatPlayerRestorePlan
{
public:
    FDWCombatPlayerRestorePlan();~FDWCombatPlayerRestorePlan();
    FDWCombatPlayerRestorePlan(FDWCombatPlayerRestorePlan&&);FDWCombatPlayerRestorePlan& operator=(FDWCombatPlayerRestorePlan&&);
    bool CanCommit(FString& Error)const;
    const FDWMoldRootIdentity& RestoredRootIdentity()const;
private:
    friend class FDWCombatPlayerPersistence;
    TUniquePtr<FDWCombatPlayerRestoreState,FDWCombatPlayerRestoreStateDeleter> State;
};
/** One participant of the D22 aggregate; never an independent public load path. */
class GDATTEST_API FDWCombatPlayerPersistence
{
public:
    static bool CaptureBase(ADWPlayerCharacter&,UDWSaveGame&,FString&);
    static bool Capture(ADWPlayerCharacter&,const TArray<FDWCombatObjectBinding>&,FDWCombatPlayerExtendedSave&,FString&);
    static bool Validate(const FDWCombatRules&,const UDWSaveGame&,const FDWCombatEffectsSave&,FString&);
    static bool Prepare(ADWPlayerCharacter&,const UDWSaveGame&,const FDWCombatEffectsSave&,
        const FDWCombatAuthorityRestorePlan&,FDWCombatPlayerRestorePlan&,FString&);
    static void AdoptNativeFacts(FDWCombatPlayerRestorePlan&);
    // Keeps the player gate closed. Aggregate calls FinishPublish only after every world participant publishes.
    static bool Publish(FDWCombatPlayerRestorePlan&,FString&);
    static bool FinishPublish(FDWCombatPlayerRestorePlan&,FString&);
private:
    friend class FDWCombatPlayerRestorePlan;
    static bool CanCommit(const FDWCombatPlayerRestorePlan&,FString&);
    static bool Fingerprint(ADWPlayerCharacter&,TArray<uint64>&,TArray<double>&,TArray<bool>&,FString&);
    static bool IsPublishedLifeCurrent(const FDWCombatPlayerRestoreState&);
};
