#include "DWGameInstance.h"
#include "DWFormalLanguagePersistence.h"
#include "DWLanguageSubsystem.h"
#include "DWOpeningMapCompatibility.h"
#include "DWAdventureFlowSave.h"
#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatWorldPersistence.h"
#include "DWOpeningGatheringPolicy.h"
#include "DWGatheringPersistenceOptIn.h"
#include "DWGatheringWorldPersistence.h"
#include "DWPlayerCharacter.h"
#include "DWPlayerController.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGameplayHUD.h"
#include "DWGameplayConfig.h"
#include "DWLoadingTransition.h"
#include "DWLocalizationLibrary.h"
#include "DWSaveGame.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

namespace DWOpeningSave
{
bool Same(DWCombatCommit::FEntityLife A,DWCombatCommit::FEntityLife B){return A.Entity&&A.Entity==B.Entity&&A.Life==B.Life;}
bool ExactGuid(const FString& S){FGuid G;return FGuid::ParseExact(S,EGuidFormats::Digits,G)&&G.IsValid();}
FString Counts(const UDWSaveGame& S)
{
 auto N=[&](FName Id){int64 Total=0;for(const auto& Row:S.Inventory)if(Row.ItemId==Id)Total+=Row.Quantity;return Total;};
 return FString::Printf(TEXT("slots=%d owned=%d loaded=%d alcohol=%lld spore=%lld arrows=%lld shells=%lld"),S.Inventory.Num(),
  S.CombatPlayerResources.OwnedPermanentWeaponIds.Num(),S.CombatPlayerResources.LoadedShotgunShells,
  static_cast<long long>(N(FName(TEXT("Alcohol")))),static_cast<long long>(N(FName(TEXT("DWAmmo_Spore")))),
  static_cast<long long>(N(FName(TEXT("DWAmmo_MoldArrow")))),static_cast<long long>(N(FName(TEXT("DWAmmo_BreadShell")))));
}
bool LoadStarter(TArray<FDWItemStack>& Out,FString& Error)
{
 FString Text;TSharedPtr<FJsonObject> J;const TSharedPtr<FJsonObject>* Parameters=nullptr;
 if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWA01TrialDefaults.json")))||
  !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J||!J->TryGetObjectField(TEXT("parameters"),Parameters))
 {Error=TEXT("Formal starter quantities are unavailable; no starter batch was committed.");return false;}
 auto Count=[&](const TCHAR* Key,int32& N){const TSharedPtr<FJsonObject>* Row=nullptr;double V=0.;
  if(!(*Parameters)->TryGetObjectField(Key,Row)||!(*Row)->TryGetNumberField(TEXT("value"),V)||!FMath::IsFinite(V)||V<1.||V>MAX_int32||FMath::FloorToDouble(V)!=V)return false;
  N=static_cast<int32>(V);return true;};
 int32 Permanent=0,Alcohol=0,Spore=0,Arrow=0,Shell=0;
 if(!Count(TEXT("weapon.starter_permanent_count"),Permanent)||Permanent!=1||!Count(TEXT("weapon.starter_alcohol_count"),Alcohol)||
  !Count(TEXT("weapon.starter_spore_ammo_count"),Spore)||!Count(TEXT("weapon.starter_arrow_count"),Arrow)||!Count(TEXT("weapon.starter_shell_count"),Shell))
 {Error=TEXT("Invalid formal starter quantities; no partial grant.");return false;}
 TArray<FDWItemStack> Items;
 for(const TCHAR* Id:{TEXT("SugarSpear"),TEXT("DWWeapon_ShieldAxe"),TEXT("DWWeapon_Greatsword"),TEXT("DWWeapon_DualDaggers"),TEXT("DWWeapon_SporeGun"),TEXT("DWWeapon_MoldBow"),TEXT("DWWeapon_Shotgun")})Items.Emplace(FName(Id),Permanent);
 Items.Emplace(FName(TEXT("Alcohol")),Alcohol);Items.Emplace(FName(TEXT("DWAmmo_Spore")),Spore);Items.Emplace(FName(TEXT("DWAmmo_MoldArrow")),Arrow);Items.Emplace(FName(TEXT("DWAmmo_BreadShell")),Shell);
 Out=MoveTemp(Items);Error.Reset();return true;
}
}

bool UDWGameInstance::ConfigureOpeningNewGame(UDWSaveGame& Save,FString& Error)
{
 const auto* C=GetConfig();if(!C)return false;
 const FString Map=DWOpeningMapCompatibility::CanonicalPackage(C->NewGameOpeningMap.ToSoftObjectPath().GetLongPackageName());
 if(Map.IsEmpty()){Error=TEXT("The new-adventure opening map is not configured; no new slot was written.");return false;}
 if(Map==C->GameplayMap||Map==C->MainMenuMap||!FPackageName::IsValidLongPackageName(Map)||!FPackageName::DoesPackageExist(Map)||
  !IsAllowedGameplayMap(Map)||!AreGameplayMapProfilesCompatible(Map,C->GameplayMap))
 {Error=TEXT("Opening map/destination must exist, be registered and share the same gameplay profile.");return false;}
 Save.AdventureFlow.Schema=1;Save.AdventureFlow.PlaythroughId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 Save.AdventureFlow.ScenarioId=TEXT("SC0101");Save.AdventureFlow.OpeningMap=Map;Save.AdventureFlow.DestinationMap=C->GameplayMap;
 Save.AdventureFlow.Phase=EDWAdventureOpeningPhase::Pending;Save.MapPackage=Map;Error.Reset();return true;
}
bool UDWGameInstance::ValidateAdventureFlow(const UDWSaveGame& Save)
{
 const auto& F=Save.AdventureFlow;
 if(F.Schema==0)return F.PlaythroughId.IsEmpty()&&F.ScenarioId.IsNone()&&F.OpeningMap.IsEmpty()&&F.DestinationMap.IsEmpty()&&
  F.Phase==EDWAdventureOpeningPhase::Legacy&&!F.bStarterGranted;
 const auto* C=GetDefaultGameplayConfig();
 if(F.Schema!=1||!C||!DWOpeningSave::ExactGuid(F.PlaythroughId)||F.ScenarioId!=TEXT("SC0101")||
  !FPackageName::IsValidLongPackageName(F.OpeningMap)||!FPackageName::IsValidLongPackageName(F.DestinationMap)||
  F.OpeningMap==F.DestinationMap||Save.LanguageTrialSchema!=0||!Save.LanguageProgressJson.IsEmpty()||!Save.A01StateJson.IsEmpty())return false;
 if(F.bStarterGranted)
  for(const TCHAR* Id:{TEXT("SugarSpear"),TEXT("DWWeapon_ShieldAxe"),TEXT("DWWeapon_Greatsword"),TEXT("DWWeapon_DualDaggers"),TEXT("DWWeapon_SporeGun"),TEXT("DWWeapon_MoldBow"),TEXT("DWWeapon_Shotgun")})
   if(!Save.CombatPlayerResources.OwnedPermanentWeaponIds.Contains(FDWCombatItemRegistry::CanonicalWeaponItemId(FName(Id))))return false;
 if(F.Phase==EDWAdventureOpeningPhase::Completed)
  // Historical opening/destination packages need not still exist or match current configuration.
  // Current-map admission and all original profile/inventory/D22 checks remain in ValidateSave.
  return F.bStarterGranted&&!Save.CombatWorld.bNewAdventure&&Save.MapPackage!=F.OpeningMap&&Save.MapPackage!=C->MainMenuMap&&
   IsAllowedGameplayMap(Save.MapPackage);
 if(F.OpeningMap!=DWOpeningMapCompatibility::CanonicalPackage(C->NewGameOpeningMap.ToSoftObjectPath().GetLongPackageName())||!IsAllowedGameplayMap(F.OpeningMap)||
  !FPackageName::DoesPackageExist(F.OpeningMap)||!IsAllowedGameplayMap(F.DestinationMap)||!FPackageName::DoesPackageExist(F.DestinationMap)||
  !AreGameplayMapProfilesCompatible(F.OpeningMap,F.DestinationMap))return false;
 if(F.Phase==EDWAdventureOpeningPhase::Pending)
  return Save.MapPackage==F.OpeningMap&&(Save.CombatWorld.bNewAdventure?!F.bStarterGranted:F.bStarterGranted);
 if(F.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination)
  return Save.MapPackage==F.OpeningMap&&!Save.CombatWorld.bNewAdventure&&F.bStarterGranted;
 return false;
}
bool UDWGameInstance::ResolveAdventureTravelMap(UDWSaveGame& Save,FString& Map,FString& Error)
{
 Map=Save.MapPackage;
 if(Save.AdventureFlow.Schema==1&&Save.AdventureFlow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination)
 {
  // The destination was chosen dynamically when completion was atomically committed.
  // A retry must honour that exact committed handoff, not silently select a new map.
  Map=Save.AdventureFlow.DestinationMap;
 }
 if(!ValidateAdventureFlow(Save)||!IsAllowedGameplayMap(Map)||!FPackageName::DoesPackageExist(Map))
 {Error=TEXT("Adventure opening progress or destination is invalid; the source file is preserved.");return false;}
 Error.Reset();return true;
}
bool UDWGameInstance::PendingSaveMatchesWorld(const UDWSaveGame& Save,const UWorld& World)const
{
 const FString Map=UWorld::RemovePIEPrefix(World.GetOutermost()->GetName());
 return Save.MapPackage==Map||(Save.AdventureFlow.Schema==1&&Save.AdventureFlow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination&&Save.AdventureFlow.DestinationMap==Map);
}
bool UDWGameInstance::IsOpeningPending()const
{return HasActiveSlot()&&AdventureFlow.Schema==1&&AdventureFlow.Phase==EDWAdventureOpeningPhase::Pending&&GetWorld()&&
 UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==AdventureFlow.OpeningMap;}
bool UDWGameInstance::IsOpeningPersistenceBlocked()const
{return HasActiveSlot()&&AdventureFlow.Schema==1&&GetWorld()&&
 (AdventureFlow.Phase==EDWAdventureOpeningPhase::Pending||AdventureFlow.Phase==EDWAdventureOpeningPhase::CompletedPendingDestination)&&
 UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==AdventureFlow.OpeningMap;}
bool UDWGameInstance::HasFormalStarterIdentity(const ADWPlayerCharacter* P,const FString& Id)const
{
 // Owner/source/slot/generation/world/life checks only; no derived A01/vitals query.
 if(!IsInGameThread()||!bCombatSaveAlive||bCombatRestoreFailure||!HasActiveSlotSourceOwner()||AdventureFlow.Schema!=1||
  !DWOpeningSave::ExactGuid(Id)||Id!=FormalPlaythroughId||Id!=AdventureFlow.PlaythroughId||!FormalStarterGrant||FormalStarterGrant->PlaythroughId!=Id||
  !IsValid(P)||P->IsActorBeingDestroyed()||P->IsCombatRestoreInProgress()||!GetWorld()||GetWorld()->bIsTearingDown||
  P->GetWorld()!=GetWorld()||P->GetGameInstance()!=this||UGameplayStatics::GetPlayerPawn(this,0)!=P||
  UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName())==TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest"))return false;
 auto* A=UDWCombatAuthoritySubsystem::Get(P);if(!A)return false;
 const FString CurrentMap=UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
 if(CurrentMap!=AdventureFlow.OpeningMap&&CurrentMap!=AdventureFlow.DestinationMap)return false;
 if(FormalStarterGrant->bGranted)return true; // Strict same-owner idempotent no-op; no fresh admission.
 return IsOpeningPending()&&bOpeningStartupReady&&OpeningStartupPlayer.Get()==P&&OpeningStartupWorld.Get()==GetWorld()&&
  OpeningStartupSlot==ActiveSlot&&OpeningStartupGeneration==CombatSaveGeneration&&OpeningStartupEpoch==A->GetWorldEpoch()&&
  DWOpeningSave::Same(OpeningStartupLife,A->FindRegisteredActorLife(P));
}
bool UDWGameInstance::CanGrantFormalStarter(const ADWPlayerCharacter* P,const FString& Id)const
{
 // Identity/session validation must not call vitals predicates that depend on that session.
 // The actual starter admission still requires the unchanged death and readiness gates.
 return HasFormalStarterIdentity(P,Id)&&!P->IsDead()&&P->HasCombatVitalsReady();
}
bool UDWGameInstance::OpeningStartupIdentityIsCurrent(const ADWPlayerCharacter* P)const
{
 auto* A=UDWCombatAuthoritySubsystem::Get(P);
 return A&&HasFormalStarterIdentity(P,FormalPlaythroughId)&&bOpeningStartupReady&&OpeningStartupPlayer.Get()==P&&
  OpeningStartupWorld.Get()==GetWorld()&&OpeningStartupSlot==ActiveSlot&&OpeningStartupGeneration==CombatSaveGeneration&&
  OpeningStartupEpoch==A->GetWorldEpoch()&&DWOpeningSave::Same(OpeningStartupLife,A->FindRegisteredActorLife(P));
}
bool UDWGameInstance::OpeningStartupIsCurrent(const ADWPlayerCharacter* P)const
{
 auto* A=UDWCombatAuthoritySubsystem::Get(P);
 return A&&CanGrantFormalStarter(P,FormalPlaythroughId)&&bOpeningStartupReady&&OpeningStartupPlayer.Get()==P&&
  OpeningStartupWorld.Get()==GetWorld()&&OpeningStartupSlot==ActiveSlot&&OpeningStartupGeneration==CombatSaveGeneration&&
  OpeningStartupEpoch==A->GetWorldEpoch()&&DWOpeningSave::Same(OpeningStartupLife,A->FindRegisteredActorLife(P));
}
bool UDWGameInstance::FinishOpeningPlayerRestore(ADWPlayerCharacter& P,const UDWSaveGame& Save,FString& Error)
{
 if(Save.AdventureFlow.Schema!=1||Save.AdventureFlow.Phase!=EDWAdventureOpeningPhase::Pending)return true;
 TSharedPtr<const FDWActiveSlotSource> StartupSource;
 TStrongObjectPtr<UDWSaveGame> StartupDisk(ReadActiveSlotForCapture(StartupSource,Error));
 if(!StartupDisk.Get()||StartupDisk->Version!=Save.Version||StartupDisk->MapPackage!=Save.MapPackage||
  !FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&StartupDisk->AdventureFlow,&Save.AdventureFlow,PPF_None))return false;
 TStrongObjectPtr<ADWPlayerCharacter> KeepPlayer(&P);
 auto* A=UDWCombatAuthoritySubsystem::Get(&P);if(!A||!FormalStarterGrant||FormalPlaythroughId!=Save.AdventureFlow.PlaythroughId)return false;
 bOpeningStartupReady=true;OpeningStartupPlayer=&P;OpeningStartupWorld=P.GetWorld();OpeningStartupSlot=ActiveSlot;
 OpeningStartupGeneration=CombatSaveGeneration;OpeningStartupEpoch=A->GetWorldEpoch();OpeningStartupLife=A->FindRegisteredActorLife(&P);
 if(!FormalStarterGrant->bGranted)
 {
  TArray<FDWItemStack> Items;if(!DWOpeningSave::LoadStarter(Items,Error)||!P.WeaponAnimationComponent)return false;
  const auto Grant=FDWCombatEquipmentTransaction::GrantFormalStarterBatch(*P.WeaponAnimationComponent,*this,Items,FormalPlaythroughId);
  if(!Grant.bCommittedThisCall&&!Grant.bAlreadyGranted){Error=Grant.Error;return false;}
 }
 if(!CanGrantFormalStarter(&P,FormalPlaythroughId)||!FormalStarterGrant->bGranted)return false;
 if(!Save.CombatWorld.bNewAdventure)return true; // Loaded durable start checkpoint: never rewrite or re-grant it.
 // Grant publication may re-enter gameplay. Require the same disk source again
 // before either legacy or Formal2 capture; never treat a failed read as empty.
 TSharedPtr<const FDWActiveSlotSource> BeforeCaptureSource;
 TStrongObjectPtr<UDWSaveGame> BeforeCaptureDisk(ReadActiveSlotForCapture(BeforeCaptureSource,Error));
 if(!BeforeCaptureDisk.Get()||BeforeCaptureSource!=StartupSource)return false;
 TStrongObjectPtr<UDWSaveGame> Checkpoint(NewObject<UDWSaveGame>());
 const bool bFormal=Save.FormalLanguage.Schema==2;FDWFormalCaptureStamp CheckpointStamp;
 if(bFormal)
 {if(!FDWFormalLanguagePersistence::CaptureFirstStarter(*this,P,*Checkpoint,CheckpointStamp,Error))return false;}
 else if(!FDWCombatWorldPersistence::Capture(P,*Checkpoint,Error))return false;
 Checkpoint->Version=Save.Version;Checkpoint->AdventureFlow=Save.AdventureFlow;
 Checkpoint->DisplayName=Save.DisplayName;Checkpoint->VisitedMaps=Save.VisitedMaps;Checkpoint->WorldMapMarkers=BeforeCaptureDisk->WorldMapMarkers;
 Checkpoint->AdventureFlow.bStarterGranted=true;Checkpoint->Timestamp=FDateTime::UtcNow();
 if(!ValidateSave(Checkpoint.Get())||(bFormal&&!FDWFormalLanguagePersistence::IsCaptureCurrent(CheckpointStamp))||!CanGrantFormalStarter(&P,FormalPlaythroughId)||
  OpeningStartupGeneration!=CombatSaveGeneration||OpeningStartupEpoch!=A->GetWorldEpoch()||
  !DWOpeningSave::Same(OpeningStartupLife,A->FindRegisteredActorLife(&P))||!CommitActiveSlotSave(*Checkpoint,StartupSource,Save.AdventureFlow,Error))
 {Error=TEXT("Opening start checkpoint could not be committed; the opening has not started.");return false;}
 AdventureFlow=Checkpoint->AdventureFlow;
 UE_LOG(LogTemp,Display,TEXT("DW_OPENING_CHECKPOINT_COMMITTED slot=%d guid=%s map=%s phase=Pending starter=%d %s"),
  ActiveSlot+1,*AdventureFlow.PlaythroughId,*Checkpoint->MapPackage,AdventureFlow.bStarterGranted?1:0,*DWOpeningSave::Counts(*Checkpoint));
 Error.Reset();return true;
}
bool UDWGameInstance::IsOpeningDirectorCurrent(AActor* Director,const ADWPlayerCharacter* P)const
{
 return IsOpeningPending()&&OpeningDirector.Get()==Director&&IsValid(Director)&&!Director->IsActorBeingDestroyed()&&
  Director->GetWorld()==GetWorld()&&OpeningStartupIsCurrent(P);
}
bool UDWGameInstance::TryStartOpening(AActor* Director,ADWPlayerCharacter* P)
{
 if(!IsOpeningPending()||PendingSave||bCombatRestoreFailure||!OpeningStartupIsCurrent(P)||
  !FormalStarterGrant->bGranted||!IsValid(Director)||Director->IsActorBeingDestroyed()||Director->GetWorld()!=GetWorld()||
  Director->GetClass()->GetPathName()!=TEXT("/Script/GDATtest.DWOpeningPrototype"))return false;
 if(OpeningDirector.IsValid())return OpeningDirector.Get()==Director;
 auto* PC=Cast<ADWPlayerController>(P->GetController());auto* HUD=PC?Cast<ADWGameplayHUD>(PC->GetHUD()):nullptr;
 if(!PC||!HUD)return false;
 // Close normal menus BEFORE the director acquires its cinematic/input lock.
 HUD->SetSessionStarted(true);HUD->ClosePanels();PC->SetPause(false);
 if(!IsOpeningPending()||PendingSave||!IsValid(Director)||Director->IsActorBeingDestroyed()||Director->GetWorld()!=GetWorld()||!OpeningStartupIsCurrent(P))return false;
 OpeningDirector=Director;return true;
}
bool UDWGameInstance::CompleteOpening(AActor* Director,ADWPlayerCharacter* P)
{
 if(bCompletingOpening||!IsOpeningPending()||PendingSave||bSaveTravelInFlight||OpeningDirector.Get()!=Director||
  !IsValid(Director)||Director->GetWorld()!=GetWorld()||!OpeningStartupIsCurrent(P)||!FormalStarterGrant->bGranted)return false;
 if(UDWGatheringPersistenceOptIn::Find(AdventureFlow.OpeningMap)||
    (GetWorld()&&UDWGatheringPersistenceOptIn::Find(UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()))))
 {
  FString GatheringError;const auto* GatheringConfig=GetConfig();
  const FString CurrentMap=GetWorld()?UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()):FString();
  if(!GetWorld()||!GatheringConfig||GatheringConfig!=GetDefaultGameplayConfig()||
     !DWOpeningGatheringPolicy::ValidateSourceFlow(AdventureFlow,CurrentMap,GatheringConfig->GetPathName(),GatheringError)||
     !FDWGatheringWorldPersistence::SupportsWorld(*GetWorld(),GatheringError))
   return Fail(FText::FromString(GatheringError.IsEmpty()?TEXT("Opening gathering source is not an explicitly audited formal world."):GatheringError));
 }
 TGuardValue<bool> Guard(bCompletingOpening,true);FString Error;const FDWAdventureFlowSave OwnerFlow=AdventureFlow;
 TSharedPtr<const FDWActiveSlotSource> HandoffSource;
 TStrongObjectPtr<UDWSaveGame> Existing(ReadActiveSlotForCapture(HandoffSource,Error));
 if(!Existing.Get())return Fail(FText::FromString(Error));
 TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());
    auto* DefinitionOwner=GetSubsystem<UDWLanguageSubsystem>();
    DWCharacterA01SaveBridge::FScopedFormalDefinitions DefinitionScope(DefinitionOwner&&DefinitionOwner->GetNativeModel()?&DefinitionOwner->GetNativeModel()->GetCatalog():nullptr,DefinitionOwner?DefinitionOwner->GetTrialConfig():TSharedPtr<FJsonObject>());
 const bool bFormal=ActiveSaveVersion==5;FDWFormalCaptureStamp HandoffStamp;if(bFormal&&!FDWFormalLanguagePersistence::MakeCaptureStamp(*P,HandoffStamp,Error))return Fail(FText::FromString(Error));
 const auto* C=GetConfig();const FString Destination=C?C->GameplayMap:FString();const uint64 Generation=CombatSaveGeneration;const int32 Slot=ActiveSlot;
 if(!C||!AreGameplayMapProfilesCompatible(AdventureFlow.OpeningMap,Destination)||!IsAllowedGameplayMap(Destination)||
  !FDWCombatWorldPersistence::Capture(*P,*Save,Error)||!FDWCombatWorldPersistence::ValidateOpeningHandoff(*CombatRules,*Save,Error))return Fail(FText::FromString(Error));
 Save->DisplayName=Existing->DisplayName;Save->VisitedMaps=Existing->VisitedMaps;Save->WorldMapMarkers=Existing->WorldMapMarkers;
 Save->AdventureFlow=AdventureFlow;Save->AdventureFlow.bStarterGranted=true;Save->AdventureFlow.DestinationMap=Destination;
 Save->AdventureFlow.Phase=EDWAdventureOpeningPhase::CompletedPendingDestination;Save->Timestamp=FDateTime::UtcNow();
 if(!ValidateSave(Save.Get())||(bFormal&&!FDWFormalLanguagePersistence::IsCaptureCurrent(HandoffStamp))||Generation!=CombatSaveGeneration||Slot!=ActiveSlot||!IsOpeningPending()||
  OpeningDirector.Get()!=Director||!OpeningStartupIsCurrent(P)||!CommitActiveSlotSave(*Save,HandoffSource,OwnerFlow,Error))return Fail(FText::FromString(Error));
 UE_LOG(LogTemp,Display,TEXT("DW_OPENING_HANDOFF_COMMITTED slot=%d guid=%s map=%s destination=%s phase=CompletedPendingDestination starter=%d %s"),
  Slot+1,*Save->AdventureFlow.PlaythroughId,*Save->MapPackage,*Save->AdventureFlow.DestinationMap,
  Save->AdventureFlow.bStarterGranted?1:0,*DWOpeningSave::Counts(*Save));
 // The durable source is complete and the opening will never replay, even if travel fails/crashes.
 AdventureFlow=Save->AdventureFlow;PreviousTravelSource=ActiveSlotSource;ResetCombatSaveFlow();PendingSave=Save.Get();PreviousTravelSlot=ActiveSlot;bSaveTravelInFlight=true;
 auto* T=GetSubsystem<UDWLoadingTransitionSubsystem>();
 if(!T||!T->RequestMap(Destination))
 {HandleTravelFailure(T?T->LastError:DWText(this,TEXT("过渡系统不可用，开场完成记录已保留；读取该槽会重试目的地。"),TEXT("The transition system is unavailable. Completion was saved; loading this slot retries its destination.")));return false;}
 if(!T->IsTransitionActive())bSaveTravelInFlight=false;return true;
}
bool UDWGameInstance::RestartOpening(AActor* Director,ADWPlayerCharacter* P)
{
 if(!IsOpeningPending()||OpeningDirector.Get()!=Director||!IsValid(Director)||!IsValid(P)||P->GetWorld()!=GetWorld()||
  UGameplayStatics::GetPlayerPawn(this,0)!=P||PendingSave||bSaveTravelInFlight)return false;
 // Caller first performs its own scoped cleanup. Loading preserves the existing checkpoint bytes.
 return LoadGameSlot(ActiveSlot);
}
bool UDWGameInstance::ExecuteOpeningFreshDestination(ADWPlayerCharacter& P,const UDWSaveGame& Source,bool& Committed,FString& Error)
{
 TGuardValue<bool> FormalDestinationScope(bPreparingFormalFreshDestination,true);
 Committed=false;const uint64 Generation=CombatSaveGeneration;const int32 Slot=ActiveSlot;
 TSharedPtr<const FDWActiveSlotSource> DestinationSource;
 TStrongObjectPtr<UDWSaveGame> SourceDisk(ReadActiveSlotForCapture(DestinationSource,Error));
 if(!SourceDisk.Get()||SourceDisk->Version!=Source.Version||SourceDisk->MapPackage!=Source.MapPackage||
  !FDWAdventureFlowSave::StaticStruct()->CompareScriptStruct(&SourceDisk->AdventureFlow,&Source.AdventureFlow,PPF_None))return false;
 const TWeakObjectPtr<UWorld> World(P.GetWorld());const TWeakObjectPtr<ADWPlayerCharacter> Player(&P);
 TStrongObjectPtr<UDWSaveGame> Projected(NewObject<UDWSaveGame>());
 if(!FDWCombatWorldPersistence::RestoreOpeningFreshDestination(P,Source,Source.AdventureFlow.DestinationMap,*Projected,Committed,Error))return false;
 if(!Player.IsValid()||!World.IsValid()||GetWorld()!=World.Get()||P.GetWorld()!=World.Get()||P.IsDead()||
  Generation!=CombatSaveGeneration||Slot!=ActiveSlot||PendingSave.Get()!=&Source)return false;
 TStrongObjectPtr<UDWSaveGame> Final(NewObject<UDWSaveGame>());
 AdventureFlow=Projected->AdventureFlow; // Native aggregate already adopted; durable phase-2 file remains until final write.
 const FDWAdventureFlowSave FinalOwnerFlow=AdventureFlow;
 TStrongObjectPtr<UDWSaveGame> RetainedPhase2(ReadSlotMatchingSource(Slot,DestinationSource,Error));
 if(!RetainedPhase2.Get()||!ValidateSave(RetainedPhase2.Get(),Slot))return false;
 const bool bFormal=Source.FormalLanguage.Schema==2;FDWFormalCaptureStamp FinalStamp;if((bFormal&&!FDWFormalLanguagePersistence::MakeCaptureStamp(P,FinalStamp,Error))||!FDWCombatWorldPersistence::Capture(P,*Final,Error))return false;
 Final->DisplayName=Source.DisplayName;Final->VisitedMaps=Source.VisitedMaps;Final->WorldMapMarkers=RetainedPhase2->WorldMapMarkers;Final->AdventureFlow=Projected->AdventureFlow;Final->Timestamp=FDateTime::UtcNow();
 if(!ValidateSave(Final.Get())||(bFormal&&!FDWFormalLanguagePersistence::IsCaptureCurrent(FinalStamp))||Generation!=CombatSaveGeneration||Slot!=ActiveSlot||PendingSave.Get()!=&Source||
  !CommitActiveSlotSave(*Final,DestinationSource,FinalOwnerFlow,Error))
 {Error=TEXT("Destination was initialized but its final record was not committed; the durable source handoff is retained for reload.");return false;}
 AdventureFlow=Final->AdventureFlow;bOpeningStartupReady=false;OpeningDirector.Reset();
 UE_LOG(LogTemp,Display,TEXT("DW_OPENING_DESTINATION_COMMITTED slot=%d guid=%s map=%s phase=Completed starter=%d %s"),
  Slot+1,*AdventureFlow.PlaythroughId,*Final->MapPackage,AdventureFlow.bStarterGranted?1:0,*DWOpeningSave::Counts(*Final));
 Error.Reset();return true;
}
