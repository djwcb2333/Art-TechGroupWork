#pragma once
#include "CoreMinimal.h"
#include "DWCombatCommitLedger.h"
#include "DWLanguageModel.h"
#include "DWCharacterA01SaveBridge.h"
#include "DWAdventureFlowSave.h"
#include "DWA01AbilityPlan.h"

class UDWGameInstance;
class UDWLanguageSubsystem;
class UDWGameplayConfig;
class ADWPlayerCharacter;
class UDWSaveGame;
class UWorld;
class UDWCombatAuthoritySubsystem;
class FDWFormalLanguagePersistence;
struct FDWLanguageProgressRestorePlan;
struct FDWCombatStarterGrantState;

// Runtime identity is never serialized. Numeric GUID/life equality cannot substitute
// for the exact owning GI, pawn, world, profile and accepted slot request.
struct FDWFormalLanguageSession
{
 TWeakObjectPtr<UDWGameInstance> Owner;
 TWeakObjectPtr<ADWPlayerCharacter> Player;
 TWeakObjectPtr<UWorld> World;
 TWeakObjectPtr<UDWGameplayConfig> Profile;
 FGuid Playthrough;
 int32 Slot=INDEX_NONE;
 uint64 Generation=0,Epoch=0;
 DWCombatCommit::FEntityLife Life;
 bool bSuspended=false;
};
struct FDWFormalCaptureStamp
{
 TWeakObjectPtr<UDWLanguageSubsystem> Language;
 TWeakObjectPtr<ADWPlayerCharacter> Player;
 TSharedPtr<FDWFormalLanguageSession> Session;
 TSharedPtr<FDWCombatStarterGrantState> Starter;
 FDWAdventureFlowSave Flow;
 const DWLanguage::FModel* Model=nullptr;
 int64 ModelRevision=0,InventoryRevision=0,WeaponRevision=0,StarterRevision=0;
 uint64 ResourceRevision=0,DamageRevision=0,WeaponRestoreEpoch=0,WeaponBoundary=0;float Health=0.f;double At=0.,AbilityCooldown=0.;bool bFirstCheckpoint=false;
};
class FDWFormalRespawnPlan final
{
public:
 FDWFormalRespawnPlan()=default;
 FDWFormalRespawnPlan(FDWFormalRespawnPlan&&)=default;FDWFormalRespawnPlan& operator=(FDWFormalRespawnPlan&&)=default;
 FDWFormalRespawnPlan(const FDWFormalRespawnPlan&)=delete;FDWFormalRespawnPlan& operator=(const FDWFormalRespawnPlan&)=delete;
private:
 friend class FDWFormalLanguagePersistence;
 FDWFormalCaptureStamp Stamp;
 DWLanguage::FPreparedChange Model;
 TSharedPtr<FDWFormalLanguageSession> NewSession;
 FDWCharacterA01Runtime Runtime;FDWA01AbilityPlan Ability;double Cooldown=0.;
 bool bAdopted=false;
};

/** Native participant adapter. No independent file writer, grant, or world restore. */
class GDATTEST_API FDWFormalLanguagePersistence
{
public:
 static bool InitializeNew(UDWGameInstance&,UDWSaveGame&,int32 Slot,FString&);
 static bool Validate(UDWGameInstance&,const UDWSaveGame&,int32 ExpectedSlot,FString&);
 static bool IsCurrent(const UDWLanguageSubsystem&,bool bAllowPendingCheckpoint=false);
 static void Suspend(UDWGameInstance&);
 static bool ResumeAfterTravelFailure(UDWGameInstance&,const UDWSaveGame* Previous);
 static bool PrepareRespawn(ADWPlayerCharacter&,FDWFormalRespawnPlan&,FDWCombatPlayerVitalsRuntime& OutLegacy,float& OutHealth,FString&);
 static bool CanCommitRespawn(const FDWFormalRespawnPlan&);
 static void AdoptRespawnUnchecked(FDWFormalRespawnPlan&);
 static bool Prepare(ADWPlayerCharacter&,const UDWSaveGame&,uint64 NewEpoch,DWCombatCommit::FEntityLife NewLife,FDWLanguageProgressRestorePlan&,FString&);
 static bool CanCommit(const FDWLanguageProgressRestorePlan&);
 static void Adopt(FDWLanguageProgressRestorePlan&);
 static bool InitializeAfterLegacyMarker(UDWGameInstance&,ADWPlayerCharacter&,const UDWSaveGame&,FString&);
 static bool Capture(ADWPlayerCharacter&,UDWSaveGame&,FString&);
 static bool MakeCaptureStamp(ADWPlayerCharacter&,FDWFormalCaptureStamp&,FString&);
 static bool IsCaptureCurrent(const FDWFormalCaptureStamp&);
 // Only the GI's first checkpoint uses this scope; actual native grant and owner
 // admission are revalidated on entry and on final write. It does not mutate Flow.
 static bool CaptureFirstStarter(UDWGameInstance&,ADWPlayerCharacter&,UDWSaveGame&,FDWFormalCaptureStamp&,FString&);
private:
 friend class UDWCombatAuthoritySubsystem;
 static TSharedPtr<FDWFormalLanguageSession> PrepareDeathBinding(ADWPlayerCharacter*);
 static void AdoptCommittedDeathBinding(ADWPlayerCharacter*,const TSharedPtr<FDWFormalLanguageSession>&);
};
