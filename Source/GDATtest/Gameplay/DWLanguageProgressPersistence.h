#pragma once
#include "CoreMinimal.h"
#include "DWLanguageModel.h"
#include "DWAdventureFlowSave.h"
#include "DWCombatCommitLedger.h"
class ADWPlayerCharacter;
class UWorld;
struct FDWFormalLanguageSession;
class UDWLanguageSubsystem;
struct FDWCombatStarterGrantState;
struct FDWLanguageProgressRestorePlan
{
 TWeakObjectPtr<UDWLanguageSubsystem> Owner;
 DWLanguage::FPreparedChange Prepared;
 FString PlaythroughId;
 TSharedPtr<FDWCombatStarterGrantState> Starter;
 bool bAdopted=false,bFormal=false;
 int32 TrialContainerVersion=4;
 TSharedPtr<FDWFormalLanguageSession> FormalSession;
 TSharedPtr<FDWCombatStarterGrantState> ExpectedFormalStarter;uint64 ExpectedStarterRevision=0;
 TWeakObjectPtr<UWorld> ExpectedWorld;TWeakObjectPtr<ADWPlayerCharacter> ExpectedPlayer;
 FDWAdventureFlowSave ExpectedOwnerFlow;
 int32 ExpectedSlot=INDEX_NONE;uint64 ExpectedGeneration=0,ExpectedEpoch=0;DWCombatCommit::FEntityLife ExpectedLife;
 bool CanCommit()const;
};
/** Native prepared participant in the complete world restore; never publishes during adoption. */
class FDWLanguageProgressPersistence
{
public:
 static bool Prepare(UDWLanguageSubsystem&,const FString&,FDWLanguageProgressRestorePlan&,FString& Error);
 static void AdoptNativeFacts(FDWLanguageProgressRestorePlan&);
 static bool Publish(FDWLanguageProgressRestorePlan&,FString& Error);
};
