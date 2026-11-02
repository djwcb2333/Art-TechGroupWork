#include "DWFormalLanguagePersistence.h"
#include "DWFormalLanguageProjection.h"
#include "DWLanguageProgressPersistence.h"
#include "DWLanguageSubsystem.h"
#include "DWLanguageUIBridgeComponent.h"
#include "DWCharacterA01Component.h"
#include "DWCombatWorldPersistence.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatEquipment.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGameInstance.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayConfig.h"
#include "DWSaveGame.h"
#include "DWFormalSaveVersionPolicy.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

namespace
{
bool SameGuid(const FString& Text,const FGuid& Id)
{FGuid Parsed;return FGuid::ParseExact(Text,EGuidFormats::Digits,Parsed)&&Parsed.IsValid()&&Parsed==Id;}
bool SameFormalLanguageSessionLife(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B)
{return A.Entity&&A.Entity==B.Entity&&A.Life==B.Life;}
bool Fail(FString& E,const TCHAR* M){E=M;return false;}
}
bool FDWFormalLanguagePersistence::InitializeNew(UDWGameInstance& GI,UDWSaveGame& S,int32 Slot,FString& Error)
{
 auto* L=GI.GetSubsystem<UDWLanguageSubsystem>();const auto Rules=GI.GetCombatRules();
 if(!L||!L->bInitialized||!L->Model||!Rules)return Fail(Error,TEXT("Validated formal language definitions are unavailable; no new slot was written."));
 FDWFormalLanguageSave Block;FDWA01SavedProjection Projection;
 if(!DWFormalLanguageProjection::InitializeNew(S.AdventureFlow,Slot,S,L->Model->GetCatalog(),L->TrialConfig,*Rules,Block,Projection,Error))return false;
 S.FormalLanguage=MoveTemp(Block);return true;
}
bool FDWFormalLanguagePersistence::Validate(UDWGameInstance& GI,const UDWSaveGame& S,int32 ExpectedSlot,FString& Error)
{
 DWFormalSaveVersionPolicy::EFamily Family;
 if(!DWFormalSaveVersionPolicy::Classify(S,Family,Error))return false;
 // Old opening slots retain their old resources and original opening validator.
 // Neither an absent catalog nor a new runtime profile may manufacture Formal2.
 if(Family!=DWFormalSaveVersionPolicy::EFamily::Formal)return true;
 if(DWCharacterA01SaveBridge::IsExactTrialMap(S.MapPackage))return Fail(Error,TEXT("Formal adventure data cannot enter the isolated trial map."));
 auto* L=GI.GetSubsystem<UDWLanguageSubsystem>();const auto Rules=GI.GetCombatRules();
 if(!L||!L->bInitialized||!L->Model||!Rules)return Fail(Error,TEXT("Formal envelope validation requires the validated catalog."));
 DWFormalLanguageProjection::EBlockKind Kind;
 return DWFormalLanguageProjection::ValidateBoundStored(S,ExpectedSlot,L->Model->GetCatalog(),L->TrialConfig,*Rules,Kind,Error);
}
bool FDWFormalLanguagePersistence::IsCurrent(const UDWLanguageSubsystem& L,bool bAllowPendingCheckpoint)
{
 const auto S=L.FormalSession;auto* GI=S?S->Owner.Get():nullptr;auto* P=S?S->Player.Get():nullptr;auto* W=S?S->World.Get():nullptr;
 auto* A=UDWCombatAuthoritySubsystem::Get(P);
 return L.bInitialized&&L.Model&&L.Model->IsReady()&&S&&!S->bSuspended&&GI&&P&&W&&!W->bIsTearingDown&&!P->IsActorBeingDestroyed()&&P->GetWorld()==W&&
  P->GetGameInstance()==GI&&GI->GetWorld()==W&&L.GetWorld()==W&&L.ActivePlayer.Get()==P&&
  UGameplayStatics::GetPlayerPawn(GI,0)==P&&GI->bCombatSaveAlive&&!GI->bCombatRestoreFailure&&
  GI->ActiveSlot==S->Slot&&GI->CombatSaveGeneration==S->Generation&&GI->GetConfig()==S->Profile.Get()&&
  GI->ActiveSaveVersion==DWFormalSaveVersionPolicy::FormalVersion&&GI->AdventureFlow.Schema==1&&SameGuid(GI->AdventureFlow.PlaythroughId,S->Playthrough)&&SameGuid(L.PlaythroughId,S->Playthrough)&&
  GI->FormalStarterGrant&&GI->FormalStarterGrant->bGranted&&SameGuid(GI->FormalStarterGrant->PlaythroughId,S->Playthrough)&&
  (GI->AdventureFlow.bStarterGranted||(bAllowPendingCheckpoint&&GI->bPreparingFormalFirstCheckpoint&&GI->OpeningStartupIdentityIsCurrent(P)))&&
  A&&A->GetWorldEpoch()==S->Epoch&&SameFormalLanguageSessionLife(A->FindRegisteredActorLife(P),S->Life);
}
void FDWFormalLanguagePersistence::Suspend(UDWGameInstance& GI)
{
 auto* L=GI.GetSubsystem<UDWLanguageSubsystem>();if(!L)return;
 ++L->SessionGeneration;
 if(L->FormalSession)L->FormalSession->bSuspended=true;
 if(auto* P=L->ActivePlayer.Get())if(auto* B=UDWLanguageUIBridgeComponent::Find(Cast<APlayerController>(P->GetController())))B->CloseForLifecycle();
 L->InvalidateLifeContextUnchecked();
 // Keep the previous model privately until a complete restore commits. IsReady
 // rejects this suspended owner, so a failed travel cannot expose another slot.
}
bool FDWFormalLanguagePersistence::ResumeAfterTravelFailure(UDWGameInstance& GI,const UDWSaveGame* Previous)
{
 auto* L=GI.GetSubsystem<UDWLanguageSubsystem>();
 if(!Previous||Previous->FormalLanguage.Schema==0)return true;
 const auto Binding=L?L->FormalSession:TSharedPtr<FDWFormalLanguageSession>();auto* P=Binding?Binding->Player.Get():nullptr;
 auto* A=UDWCombatAuthoritySubsystem::Get(P);auto* W=Binding?Binding->World.Get():nullptr;
 if(!L||!Binding||!Binding->bSuspended||!W||W!=GI.GetWorld()||W->bIsTearingDown||!P||P->GetWorld()!=W||
  P->IsActorBeingDestroyed()||UGameplayStatics::GetPlayerPawn(&GI,0)!=P||GI.ActiveSlot!=Binding->Slot||
  Previous->FormalLanguage.SlotIndex!=Binding->Slot||!SameGuid(Previous->AdventureFlow.PlaythroughId,Binding->Playthrough)||
  Previous->AdventureFlow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination||
  UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())!=Previous->MapPackage||GI.GetConfig()!=Binding->Profile.Get()||
  !A||A->GetWorldEpoch()!=Binding->Epoch||!SameFormalLanguageSessionLife(A->FindRegisteredActorLife(P),Binding->Life)||
  !GI.FormalStarterGrant||!GI.FormalStarterGrant->bGranted||!SameGuid(GI.FormalStarterGrant->PlaythroughId,Binding->Playthrough))return false;
 // A failed request left this exact world/pawn/life and private model untouched.
 // Rebind only its accepted request generation; do not load disk progress over it.
 Binding->Generation=GI.CombatSaveGeneration;Binding->bSuspended=false;++L->SessionGeneration;return IsCurrent(*L);
}
bool FDWFormalLanguagePersistence::Prepare(ADWPlayerCharacter& P,const UDWSaveGame& S,uint64 NewEpoch,
 DWCombatCommit::FEntityLife NewLife,FDWLanguageProgressRestorePlan& Out,FString& Error)
{
 auto* GI=P.GetGameInstance<UDWGameInstance>();auto* A=UDWCombatAuthoritySubsystem::Get(&P);
 auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 FGuid OwnerId;if(GI)FGuid::ParseExact(GI->AdventureFlow.PlaythroughId,EGuidFormats::Digits,OwnerId);
 if(!GI||GI->ActiveSaveVersion!=DWFormalSaveVersionPolicy::FormalVersion||S.Version!=GI->ActiveSaveVersion||!A||!L||!L->Model||!GI->HasActiveSlot()||!NewEpoch||!NewLife.Entity||!NewLife.Life||
  GI->bCombatRestoreFailure||P.IsActorBeingDestroyed()||P.GetWorld()!=GI->GetWorld()||
  UGameplayStatics::GetPlayerPawn(GI,0)!=&P||GI->AdventureFlow.Schema!=1||
  !SameGuid(S.AdventureFlow.PlaythroughId,OwnerId)||!Validate(*GI,S,GI->ActiveSlot,Error)||!GI->FormalStarterGrant)return false;
 const auto& Flow=GI->AdventureFlow;const auto& SavedFlow=S.AdventureFlow;
 const bool FreshDestination=GI->bPreparingFormalFreshDestination&&GI->bApplyingCombatPending&&
  Flow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination&&SavedFlow.Phase==EDWAdventureOpeningPhase::Completed&&
  UWorld::RemovePIEPrefix(P.GetWorld()->GetOutermost()->GetName())==Flow.DestinationMap;
 if((!GI->bApplyingCombatPending&&!IsCurrent(*L))||Flow.Schema!=SavedFlow.Schema||Flow.ScenarioId!=SavedFlow.ScenarioId||
  Flow.OpeningMap!=SavedFlow.OpeningMap||Flow.DestinationMap!=SavedFlow.DestinationMap||
  Flow.bStarterGranted!=SavedFlow.bStarterGranted||Flow.StarterPolicyVersion!=SavedFlow.StarterPolicyVersion||(!FreshDestination&&Flow.Phase!=SavedFlow.Phase))
  return Fail(Error,TEXT("Formal restore is outside its accepted owner/phase transaction."));
 FDWA01SavedProjection Projection;
 if(!DWFormalLanguageProjection::PrepareStored(S,GI->ActiveSlot,L->Model->GetCatalog(),L->TrialConfig,*GI->CombatRules,Projection,Error))return false;
 FDWLanguageProgressRestorePlan Candidate;DWLanguage::FOperationResult R;
 if(!L->Model->PrepareRestore(Projection.LanguageState,Candidate.Prepared,R))return Fail(Error,TEXT("Formal language restore candidate was rejected."));
 Candidate.Owner=L;Candidate.PlaythroughId=S.AdventureFlow.PlaythroughId;Candidate.bFormal=true;
 Candidate.ExpectedSlot=GI->ActiveSlot;Candidate.ExpectedGeneration=GI->CombatSaveGeneration;
 Candidate.ExpectedOwnerFlow=GI->AdventureFlow;Candidate.ExpectedWorld=P.GetWorld();Candidate.ExpectedPlayer=&P;
 Candidate.ExpectedFormalStarter=GI->FormalStarterGrant;Candidate.ExpectedStarterRevision=GI->FormalStarterGrant->Revision;
 Candidate.ExpectedEpoch=A->GetWorldEpoch();Candidate.ExpectedLife=A->FindRegisteredActorLife(&P);
 Candidate.FormalSession=MakeShared<FDWFormalLanguageSession>();auto& Binding=*Candidate.FormalSession;
 Binding.Owner=GI;Binding.Player=&P;Binding.World=P.GetWorld();Binding.Profile=GI->GetConfig();Binding.Slot=GI->ActiveSlot;
 FGuid::ParseExact(S.AdventureFlow.PlaythroughId,EGuidFormats::Digits,Binding.Playthrough);
 Binding.Generation=GI->CombatSaveGeneration;Binding.Epoch=NewEpoch;Binding.Life=NewLife;
 if(!CanCommit(Candidate))return Fail(Error,TEXT("Formal owner changed during preparation."));
 Out=MoveTemp(Candidate);Error.Reset();return true;
}
bool FDWFormalLanguagePersistence::CanCommit(const FDWLanguageProgressRestorePlan& Plan)
{
 if(!Plan.bFormal||Plan.bAdopted||!Plan.Owner.IsValid()||!Plan.FormalSession)return false;
 const auto& S=*Plan.FormalSession;auto* GI=S.Owner.Get();auto* P=Plan.ExpectedPlayer.Get();auto* A=UDWCombatAuthoritySubsystem::Get(P);
 return GI&&P&&A&&!P->IsActorBeingDestroyed()&&P->GetWorld()==Plan.ExpectedWorld.Get()&&P->GetWorld()==GI->GetWorld()&&
  !P->GetWorld()->bIsTearingDown&&UGameplayStatics::GetPlayerPawn(GI,0)==P&&GI->bCombatSaveAlive&&!GI->bCombatRestoreFailure&&
  GI->ActiveSaveVersion==DWFormalSaveVersionPolicy::FormalVersion&&GI->ActiveSlot==Plan.ExpectedSlot&&GI->CombatSaveGeneration==Plan.ExpectedGeneration&&GI->GetConfig()==S.Profile.Get()&&
  GI->FormalStarterGrant==Plan.ExpectedFormalStarter&&GI->FormalStarterGrant&&GI->FormalStarterGrant->Revision==Plan.ExpectedStarterRevision&&
  FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&GI->AdventureFlow,&Plan.ExpectedOwnerFlow,PPF_None)&&
  A->GetWorldEpoch()==Plan.ExpectedEpoch&&SameFormalLanguageSessionLife(A->FindRegisteredActorLife(P),Plan.ExpectedLife)&&
  Plan.Owner->Model&&Plan.Owner->Model->CanCommit(Plan.Prepared);
}
void FDWFormalLanguagePersistence::Adopt(FDWLanguageProgressRestorePlan& Plan)
{
 // Called only after the full aggregate's final CanCommit and inside its barrier.
 auto* L=Plan.Owner.Get();check(L&&Plan.bFormal&&!Plan.bAdopted&&Plan.FormalSession);
 DWLanguage::FOperationResult R;L->Model->AdoptPreparedUnchecked(MoveTemp(Plan.Prepared),R);
 L->PlaythroughId=MoveTemp(Plan.PlaythroughId);L->ActivePlayer=Plan.FormalSession->Player;
 L->FormalSession=MoveTemp(Plan.FormalSession);L->StarterGrant.Reset();
 ++L->SessionGeneration;
 L->RequestedPanel=EDWLanguagePanel::None;L->RequestedEntry=L->RequestedContext=NAME_None;L->ActiveCarrier.Reset();L->Selections.Reset();Plan.bAdopted=true;
}
bool FDWFormalLanguagePersistence::InitializeAfterLegacyMarker(UDWGameInstance& GI,ADWPlayerCharacter& P,const UDWSaveGame& S,FString& Error)
{
 if(!S.CombatWorld.bNewAdventure||S.FormalLanguage.Schema!=2||!P.CharacterA01||P.CharacterA01->IsEnabled())return false;
 auto* A=UDWCombatAuthoritySubsystem::Get(&P);auto* L=GI.GetSubsystem<UDWLanguageSubsystem>();if(!A||!L)return false;
 FDWA01SavedProjection Projection;if(!DWFormalLanguageProjection::PrepareStored(S,GI.ActiveSlot,L->Model->GetCatalog(),L->TrialConfig,*GI.CombatRules,Projection,Error))return false;
 FDWA01AbilityPlan Ability;if(!P.CharacterA01->PrepareFormalAbility(Ability,Error))return false;
 FDWLanguageProgressRestorePlan Plan;
 if(!Prepare(P,S,A->GetWorldEpoch(),A->FindRegisteredActorLife(&P),Plan,Error)||!CanCommit(Plan))return false;
 // Legacy MakeNew was already adopted. These two new participants commit together,
 // without grants/events/file I/O; no user callback can observe half initialization.
 P.CharacterA01->AdoptFormalStateUnchecked(MoveTemp(Projection.Runtime),MoveTemp(Ability),Projection.CooldownRemaining);
 Adopt(Plan);return true;
}
bool FDWFormalLanguagePersistence::Capture(ADWPlayerCharacter& P,UDWSaveGame& S,FString& Error)
{
 auto* GI=P.GetGameInstance<UDWGameInstance>();auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 if(!GI||!L||!IsCurrent(*L,true)||!P.CharacterA01||!P.CharacterA01->IsEnabled())return Fail(Error,TEXT("Formal capture owner is not current."));
 if(!P.CharacterA01->IsPersistenceSettled(Error))return false;
 const int64 Revision=L->Model->GetState().Revision;const uint64 Resource=P.CharacterA01->GetResourceRevision();
 auto Runtime=P.CharacterA01->GetRuntime();if(!Runtime.SyncCurrentHealth(P.GetHealth()))return false;
 S.Version=GI->ActiveSaveVersion;S.AdventureFlow=GI->AdventureFlow;S.MapPackage=UWorld::RemovePIEPrefix(P.GetWorld()->GetOutermost()->GetName());
 FDWA01SavedProjection Projection;FDWFormalLanguageSave Block;FDWAdventureFlowSave PreparedFlow;
 const double Cooldown=P.CharacterA01->GetAbility().GetState().CooldownRemainingSeconds;
 const bool First=GI->bPreparingFormalFirstCheckpoint;
 const bool Ok=First?DWFormalLanguageProjection::CapturePreparedFirstStarter(S.AdventureFlow,GI->ActiveSlot,S,L->Model->GetCatalog(),L->TrialConfig,*GI->CombatRules,L->Model->GetState(),Runtime,Cooldown,PreparedFlow,Block,Projection,Error):
  DWFormalLanguageProjection::Capture(S.AdventureFlow,GI->ActiveSlot,S,L->Model->GetCatalog(),L->TrialConfig,*GI->CombatRules,L->Model->GetState(),Runtime,Cooldown,Block,Projection,Error);
 if(!Ok||!IsCurrent(*L,true)||Revision!=L->Model->GetState().Revision||Resource!=P.CharacterA01->GetResourceRevision())return false;
 if(First)S.AdventureFlow=MoveTemp(PreparedFlow);S.FormalLanguage=MoveTemp(Block);
 return Validate(*GI,S,GI->ActiveSlot,Error);
}
bool FDWFormalLanguagePersistence::MakeCaptureStamp(ADWPlayerCharacter& P,FDWFormalCaptureStamp& Out,FString& Error)
{
 auto* GI=P.GetGameInstance<UDWGameInstance>();auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 if(!L||!IsCurrent(*L,true)||!P.CharacterA01||!P.GetInventory()||!P.WeaponAnimationComponent)return Fail(Error,TEXT("Formal capture stamp owner unavailable."));
 FDWFormalCaptureStamp S;S.Language=L;S.Player=&P;S.Session=L->FormalSession;S.Starter=GI->FormalStarterGrant;
 S.Flow=GI->AdventureFlow;
 S.Model=L->Model.Get();S.ModelRevision=S.Model->GetState().Revision;S.ResourceRevision=P.CharacterA01->GetResourceRevision();
 S.InventoryRevision=P.GetInventory()->GetInventoryRevision();S.WeaponRevision=P.WeaponAnimationComponent->GetCombatWeaponResourceRevision();
 S.WeaponRestoreEpoch=P.WeaponAnimationComponent->GetCombatWeaponRestoreEpoch();S.WeaponBoundary=P.WeaponAnimationComponent->GetCombatBoundaryRevision();S.DamageRevision=P.GetCombatDamageRevision();S.Health=P.GetHealth();
 S.AbilityCooldown=P.CharacterA01->GetAbility().GetState().CooldownRemainingSeconds;
 S.StarterRevision=S.Starter->Revision;S.At=P.GetWorld()->GetTimeSeconds();S.bFirstCheckpoint=GI->bPreparingFormalFirstCheckpoint;
 Out=MoveTemp(S);return true;
}
bool FDWFormalLanguagePersistence::IsCaptureCurrent(const FDWFormalCaptureStamp& S)
{
 auto* L=S.Language.Get();auto* P=S.Player.Get();auto* GI=S.Session?S.Session->Owner.Get():nullptr;
 if(!L||!P||!GI||GI->ActiveSaveVersion!=DWFormalSaveVersionPolicy::FormalVersion||L->FormalSession!=S.Session||S.Session->bSuspended||L->Model.Get()!=S.Model||!S.Model||
  S.Model->GetState().Revision!=S.ModelRevision||!P->CharacterA01||P->CharacterA01->GetResourceRevision()!=S.ResourceRevision||
  !P->GetInventory()||P->GetInventory()->GetInventoryRevision()!=S.InventoryRevision||!P->WeaponAnimationComponent||
  P->WeaponAnimationComponent->GetCombatWeaponResourceRevision()!=S.WeaponRevision||P->WeaponAnimationComponent->GetCombatWeaponRestoreEpoch()!=S.WeaponRestoreEpoch||
  P->WeaponAnimationComponent->GetCombatBoundaryRevision()!=S.WeaponBoundary||P->GetCombatDamageRevision()!=S.DamageRevision||P->GetHealth()!=S.Health||GI->FormalStarterGrant!=S.Starter||
  P->CharacterA01->GetAbility().GetState().CooldownRemainingSeconds!=S.AbilityCooldown||
  !S.Starter||!S.Starter->bGranted||S.Starter->Revision!=S.StarterRevision||!P->GetWorld()||P->GetWorld()->GetTimeSeconds()!=S.At)return false;
 if(!FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&S.Flow,&GI->AdventureFlow,PPF_None))return false;
 if(!S.bFirstCheckpoint)return IsCurrent(*L);
 auto* A=UDWCombatAuthoritySubsystem::Get(P);
 return GI->OpeningStartupIsCurrent(P)&&!GI->AdventureFlow.bStarterGranted&&GI->ActiveSlot==S.Session->Slot&&
  GI->CombatSaveGeneration==S.Session->Generation&&P->GetWorld()==S.Session->World.Get()&&GI->GetConfig()==S.Session->Profile.Get()&&
  A&&A->GetWorldEpoch()==S.Session->Epoch&&SameFormalLanguageSessionLife(A->FindRegisteredActorLife(P),S.Session->Life);
}
bool FDWFormalLanguagePersistence::CaptureFirstStarter(UDWGameInstance& GI,ADWPlayerCharacter& P,UDWSaveGame& Out,FDWFormalCaptureStamp& Stamp,FString& Error)
{
 if(GI.bPreparingFormalFirstCheckpoint||!GI.OpeningStartupIsCurrent(&P)||!GI.FormalStarterGrant||!GI.FormalStarterGrant->bGranted||GI.AdventureFlow.bStarterGranted)return false;
 const auto Receipt=GI.FormalStarterGrant;const uint64 Revision=Receipt->Revision;
 TGuardValue<bool> Scope(GI.bPreparingFormalFirstCheckpoint,true);
 FDWFormalCaptureStamp PreparedStamp;
 if(!MakeCaptureStamp(P,PreparedStamp,Error)||!FDWCombatWorldPersistence::Capture(P,Out,Error)||!IsCaptureCurrent(PreparedStamp)||!GI.OpeningStartupIsCurrent(&P)||GI.FormalStarterGrant!=Receipt||Receipt->Revision!=Revision||!Receipt->bGranted)return false;
 Stamp=MoveTemp(PreparedStamp);Error.Reset();return true;
}
TSharedPtr<FDWFormalLanguageSession> FDWFormalLanguagePersistence::PrepareDeathBinding(ADWPlayerCharacter* P)
{
 auto* GI=P?P->GetGameInstance<UDWGameInstance>():nullptr;auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
 return L&&IsCurrent(*L)?L->FormalSession:TSharedPtr<FDWFormalLanguageSession>();
}
void FDWFormalLanguagePersistence::AdoptCommittedDeathBinding(ADWPlayerCharacter* P,const TSharedPtr<FDWFormalLanguageSession>& Binding)
{
 if(!P||!Binding)return;auto* GI=Binding->Owner.Get();auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(P);
 if(!L||L->FormalSession!=Binding||Binding->bSuspended||Binding->Player.Get()!=P||!P->IsDead()||!A||
  GI->GetWorld()!=Binding->World.Get()||P->GetWorld()!=Binding->World.Get()||GI->ActiveSlot!=Binding->Slot||
  GI->CombatSaveGeneration!=Binding->Generation||GI->GetConfig()!=Binding->Profile.Get()||A->GetWorldEpoch()!=Binding->Epoch)return;
 const auto NewLife=A->FindRegisteredActorLife(P);
 if(NewLife.Entity!=Binding->Life.Entity||Binding->Life.Life==MAX_uint64||NewLife.Life!=Binding->Life.Life+1)return;
 // NotifyActorDeath just committed this exact life transition without callbacks.
 // Keep language/talents/paid facts; invalidate only old-life interaction leases.
 Binding->Life=NewLife;++L->SessionGeneration;L->InvalidateLifeContextUnchecked();
}
bool FDWFormalLanguagePersistence::PrepareRespawn(ADWPlayerCharacter& P,FDWFormalRespawnPlan& Out,
 FDWCombatPlayerVitalsRuntime& OutLegacy,float& OutHealth,FString& Error)
{
 auto* GI=P.GetGameInstance<UDWGameInstance>();auto* L=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;auto* A=UDWCombatAuthoritySubsystem::Get(&P);
 if(!GI||!L||!A||!P.IsDead()||GI->IsOpeningPersistenceBlocked()||!IsCurrent(*L)||!P.CharacterA01||!P.CharacterA01->IsPersistenceSettled(Error)||!A->CanRestorePlayerState(&P))return false;
 FDWFormalRespawnPlan Candidate;if(!MakeCaptureStamp(P,Candidate.Stamp,Error))return false;
 auto Current=P.CharacterA01->GetRuntime();TSet<FName> Learned;
 for(const auto& Pair:L->Model->GetState().LearnedTalentPaidCosts)Learned.Add(Pair.Key);
 FDWCharacterA01Config Derived;if(!P.CharacterA01->BuildConfigForLearned(Learned,Derived,Error)||!Current.ApplyConfig(Derived))return false;
 FDWCombatPlayerVitalsRuntime Legacy;float Health=0.f;
 if(!DWCharacterA01SaveBridge::PrepareRespawnResources(*GI->CombatRules,Current,Candidate.Runtime,Legacy,Health,Error)||
  !P.CharacterA01->PrepareFormalAbility(Candidate.Ability,Error))return false;
 Candidate.Cooldown=P.CharacterA01->GetAbility().GetState().CooldownRemainingSeconds;
 if(!Candidate.Ability.RestoreIdleCooldown(Candidate.Cooldown,P.CharacterA01->GetAbility().GetState().bPhysicalDown))return false;
 DWLanguage::FOperationResult Result;
 if(!L->Model->PrepareRestore(L->Model->GetState(),Candidate.Model,Result))return Fail(Error,TEXT("Cannot prepare formal respawn language owner."));
 Candidate.NewSession=MakeShared<FDWFormalLanguageSession>(*L->FormalSession);++Candidate.NewSession->Life.Life;
 if(!CanCommitRespawn(Candidate))return Fail(Error,TEXT("Formal respawn participants changed during preparation."));
 Out=MoveTemp(Candidate);OutLegacy=MoveTemp(Legacy);OutHealth=Health;Error.Reset();return true;
}
bool FDWFormalLanguagePersistence::CanCommitRespawn(const FDWFormalRespawnPlan& P)
{
 auto* Player=P.Stamp.Player.Get();auto* L=P.Stamp.Language.Get();auto* A=UDWCombatAuthoritySubsystem::Get(Player);
 return !P.bAdopted&&P.NewSession&&Player&&Player->IsDead()&&A&&A->CanRestorePlayerState(Player)&&
  IsCaptureCurrent(P.Stamp)&&L&&L->Model&&L->Model->CanCommit(P.Model);
}
void FDWFormalLanguagePersistence::AdoptRespawnUnchecked(FDWFormalRespawnPlan& Plan)
{
 auto* P=Plan.Stamp.Player.Get();auto* L=Plan.Stamp.Language.Get();auto* A=UDWCombatAuthoritySubsystem::Get(P);
 check(P&&L&&A&&!Plan.bAdopted&&Plan.NewSession&&SameFormalLanguageSessionLife(A->FindRegisteredActorLife(P),Plan.NewSession->Life));
 // Existing PlayerRespawn receipt, actor life, resources, model and binding are
 // adopted before the recovery notice. No grant/refund/clock advance or callback.
 P->CharacterA01->AdoptFormalStateUnchecked(MoveTemp(Plan.Runtime),MoveTemp(Plan.Ability),Plan.Cooldown);
 DWLanguage::FOperationResult Result;L->Model->AdoptPreparedUnchecked(MoveTemp(Plan.Model),Result);
 L->FormalSession=MoveTemp(Plan.NewSession);++L->SessionGeneration;L->InvalidateLifeContextUnchecked();Plan.bAdopted=true;
}
