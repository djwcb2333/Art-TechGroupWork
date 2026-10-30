#pragma once
#include "CoreMinimal.h"
#include "DWGatheringSave.h"
class ADWPlayerCharacter;
class UDWGameplayConfig;
class UWorld;
struct FDWCombatObjectBinding;
struct FDWCombatAuthoritySave;
class FDWCombatAuthorityRestorePlan;
struct FDWGatheringRestoreState;
struct FDWGatheringRestoreStateDeleter {void operator()(FDWGatheringRestoreState*)const;};

class GDATTEST_API FDWGatheringRestorePlan
{
public:
    FDWGatheringRestorePlan();~FDWGatheringRestorePlan();
    FDWGatheringRestorePlan(FDWGatheringRestorePlan&&);
    FDWGatheringRestorePlan& operator=(FDWGatheringRestorePlan&&);
    FDWGatheringRestorePlan(const FDWGatheringRestorePlan&)=delete;
    FDWGatheringRestorePlan& operator=(const FDWGatheringRestorePlan&)=delete;
    bool CanCommit(FString& Error)const;
    /** Merge current registered resources and saved typed resources into root's two complete binding tables. */
    bool AppendAuthorityBindings(const FDWCombatAuthoritySave&,TArray<FDWCombatObjectBinding>& Live,
        TArray<FDWCombatObjectBinding>& Restored,FString& Error);
    bool ValidateRestoredAuthority(const FDWCombatAuthorityRestorePlan&,FString& Error)const;
    void DiscardStagedObjects();
private:
    friend class FDWGatheringWorldPersistence;
    TUniquePtr<FDWGatheringRestoreState,FDWGatheringRestoreStateDeleter> State;
};

/** Participant of the existing D22 aggregate. Never writes a slot or restores inventory. */
class GDATTEST_API FDWGatheringWorldPersistence
{
public:
    static bool IsSupportedMap(const FString& Map);
    static bool IsApprovedProfilePath(const FString& Path);
    static bool IsApprovedProfileForMap(const FString& Map,const FString& Path);
    static bool IsApprovedBuildingIdentity(FName ItemId,const FString& DefinitionPath,const FString& ClassPath);
    static bool MakeNewAdventure(const FString& Map,FDWGatheringWorldSave& Out,FString& Error);
    static bool Validate(const FDWGatheringWorldSave&,const FString& Map,const TArray<FDWItemStack>& Inventory,
        bool bNewAdventure,FString& Error);
    static bool ValidateForConfig(const UDWGameplayConfig&,const FDWGatheringWorldSave&,
        const TArray<FDWItemStack>& Inventory,FString& Error);
    /** Admission/capability only; capture may additionally reject an unfinished interaction. */
    static bool SupportsWorld(UWorld&,FString& Error);
    static bool Capture(ADWPlayerCharacter&,FDWGatheringWorldSave& Out,FString& Error);
    /** No registration here: only already-registered authored Resource actors receive typed bindings. */
    static bool AppendCaptureAuthorityBindings(ADWPlayerCharacter&,const FDWGatheringWorldSave&,
        TArray<FDWCombatObjectBinding>& InOut,FString& Error);
    /** Pure cross-check; untouched Resources need no Authority row. No classes/assets are loaded. */
    static bool ValidateAuthorityReferences(const FDWGatheringWorldSave&,const FDWCombatAuthoritySave&,FString& Error);
    static bool Prepare(ADWPlayerCharacter&,const FDWGatheringWorldSave&,const TArray<FDWItemStack>& SavedInventory,
        FDWGatheringRestorePlan& Out,FString& Error);
    /** Only inside root's aggregate barrier; no scene/UI/callback/transform operations. */
    static void AdoptNativeFacts(FDWGatheringRestorePlan&);
    static bool Publish(FDWGatheringRestorePlan&,FString& Error);
private:
    friend class FDWGatheringRestorePlan;
    static bool CaptureInternal(ADWPlayerCharacter&,FDWGatheringWorldSave&,FString& Error);
};
