#include "DWCombatWorldPersistence.h"
#include "DWOpeningGatheringPolicy.h"
#include "DWCombatOpeningDaggerProjection.h"
#include "DWCombatEffectsSave.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatPlayerSave.h"
#include "DWSaveGame.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "DWCharacterA01SaveBridge.h"
#include "DWLanguageSubsystem.h"
#include "Engine/World.h"
#include "UObject/StrongObjectPtr.h"

bool FDWCombatWorldPersistence::ValidateOpeningHandoff(const FDWCombatRules& Rules,const UDWSaveGame& S,FString& Error)
{
 if(!Validate(Rules,S,Error)||S.CombatWorld.bNewAdventure||DWCharacterA01SaveBridge::HasTrialFields(S))return false;
 if(S.MapPackage==S.AdventureFlow.OpeningMap&&S.CombatWorld.Gathering.Schema!=0&&
    !DWOpeningGatheringPolicy::ValidateSourceSnapshot(S,Error))return false;
 FDWCombatEffectsSave E;FDWCombatWorldObjectsSave O;
 if(!DWCombatEffectsSave::Decode(S.CombatWorld.Effects,E,Error)||!DWCombatWorldObjectsSave::Decode(S.CombatWorld.Objects,O,Error,S.FormalLanguage.Schema==2))return false;
 const auto& P=S.CombatWorld.Player;FDWCombatPlayerVitalsConfig C;
 if(!DWCombatPlayerSave::BuildConfigForSave(Rules,S,C,Error)||S.Health<=DWCombatPlayerSave::HealthFloor(C)||
  P.bRooted||P.RootAction||P.RootReceipt||!P.ShieldPackets.IsEmpty()||P.Ring.bActive||!P.Ring.Mines.IsEmpty()||
  !P.Ring.HitFormalEnemies.IsEmpty()||!P.Ring.HitLegacyEnemies.IsEmpty()||!P.MineEmitters.IsEmpty()||!P.RingHitEntities.IsEmpty()||
  !E.Spores.IsEmpty()||!E.Arrows.IsEmpty()||!E.Bottles.IsEmpty()||!E.Areas.IsEmpty()||!E.TargetPhases.IsEmpty())
 {Error=TEXT("Opening handoff requires a living player and no cross-world active effects/root/mine/shield payload.");return false;}
 for(const auto& A:E.Authority.Ledger.Actions)if(A.Active){Error=TEXT("Finish the active action before opening handoff.");return false;}
 for(const auto& A:E.Authority.Ledger.Emitters)if(A.Active){Error=TEXT("Finish the active emitter before opening handoff.");return false;}
 for(const auto& R:E.Authority.Ledger.Receipts)if(R.Receipt.State==DWCombatCommit::EReceiptState::Pending)
 {Error=TEXT("Resolve pending receipts before opening handoff.");return false;}
 for(const auto& Enemy:O.Enemies)if(Enemy.Phase||Enemy.MeleeAction.Id){Error=TEXT("Finish enemy attack phases before opening handoff.");return false;}
 Error.Reset();return true;
}

// GI-only fresh-destination construction, not a general cross-map restore API.
// Source Objects/Effects remain intact in the durable phase-2 file. Target
// Objects/Effects/authority IDs are freshly captured from the real destination.
bool FDWCombatWorldPersistence::BuildFreshOpeningDestination(ADWPlayerCharacter& P,const UDWSaveGame& Source,
 const FString& ExpectedDestination,UDWSaveGame& Out,FDWOpeningDaggerProjectionProof& Proof,FString& Error)
{
 auto* GI=P.GetGameInstance<UDWGameInstance>();auto* W=P.GetWorld();const auto Rules=GI?GI->GetCombatRules():nullptr;
    auto* DefinitionOwner=GI?GI->GetSubsystem<UDWLanguageSubsystem>():nullptr;
    DWCharacterA01SaveBridge::FScopedFormalDefinitions DefinitionScope(DefinitionOwner&&DefinitionOwner->GetNativeModel()?&DefinitionOwner->GetNativeModel()->GetCatalog():nullptr,DefinitionOwner?DefinitionOwner->GetTrialConfig():TSharedPtr<FJsonObject>());
 if(!GI||!Rules||!W||W->bIsTearingDown||P.IsDead()||P.IsCombatRestoreInProgress()||
  Source.AdventureFlow.Schema!=1||Source.AdventureFlow.Phase!=EDWAdventureOpeningPhase::CompletedPendingDestination||
  !Source.AdventureFlow.bStarterGranted||Source.AdventureFlow.OpeningMap!=Source.MapPackage||
  Source.AdventureFlow.DestinationMap!=ExpectedDestination||
  UWorld::RemovePIEPrefix(W->GetOutermost()->GetName())!=ExpectedDestination||
  !GI->AreGameplayMapProfilesCompatible(Source.MapPackage,ExpectedDestination)||!ValidateOpeningHandoff(*Rules,Source,Error))return false;
 FDWCombatWeaponResourceState IncomingWeapon;
 if(!DWCombatPlayerSave::BuildWeaponCandidate(*Rules,Source.CombatPlayerResources,IncomingWeapon,Error))return false;
 TStrongObjectPtr<UDWSaveGame> Fresh(NewObject<UDWSaveGame>());
 if(!Capture(P,*Fresh,Error)||DWCharacterA01SaveBridge::HasTrialFields(*Fresh)||!ValidateOpeningHandoff(*Rules,*Fresh,Error))return false;
 const FDWGatheringWorldSave CapturedDestinationGathering=Fresh->CombatWorld.Gathering;
 if(!PrepareOpeningDaggerProjection(P,IncomingWeapon,*Fresh,Proof,Error))return false;
 const uint64 Entity=Fresh->CombatWorld.Player.PlayerEntity,Life=Fresh->CombatWorld.Player.PlayerLife;
 // Complete personal resources, owned/loaded weapon state and cooldowns are copied.
 // Spatial/world/authority identity is exclusively the actual destination's data.
 Fresh->Version=Source.Version;Fresh->FormalLanguage=Source.FormalLanguage;
 Fresh->Inventory=Source.Inventory;Fresh->CombatPlayerResources=Source.CombatPlayerResources;
 Fresh->Health=Source.Health;Fresh->Transformation=Source.Transformation;Fresh->bYeastForm=Source.bYeastForm;
 Fresh->SprintAlcoholElapsed=Source.SprintAlcoholElapsed;Fresh->TransformationDecayElapsed=Source.TransformationDecayElapsed;
 Fresh->CombatWorld.Player=Source.CombatWorld.Player;
 Fresh->CombatWorld.Player.PlayerEntity=Entity;Fresh->CombatWorld.Player.PlayerLife=Life;
 Fresh->DisplayName=Source.DisplayName;Fresh->VisitedMaps=Source.VisitedMaps;Fresh->WorldMapMarkers=Source.WorldMapMarkers;
 Fresh->AdventureFlow=Source.AdventureFlow;Fresh->AdventureFlow.Phase=EDWAdventureOpeningPhase::Completed;
 if(Source.CombatWorld.Gathering.Schema!=0&&
    !DWOpeningGatheringPolicy::ValidateDestinationBoundary(Source,*Fresh,CapturedDestinationGathering,Error))return false;
 // Includes exact object-map, player-life, dagger-observation and all existing
 // native codec cross-checks. No field is excluded or silently reset to pass.
 if(!Validate(*Rules,*Fresh,Error))return false;
 Out.Version=Fresh->Version;Out.DisplayName=Fresh->DisplayName;Out.Timestamp=Fresh->Timestamp;
 Out.MapPackage=Fresh->MapPackage;Out.PlayerTransform=Fresh->PlayerTransform;Out.bHasPlayerTransform=Fresh->bHasPlayerTransform;
 Out.Health=Fresh->Health;Out.Transformation=Fresh->Transformation;Out.bYeastForm=Fresh->bYeastForm;
 Out.SprintAlcoholElapsed=Fresh->SprintAlcoholElapsed;Out.TransformationDecayElapsed=Fresh->TransformationDecayElapsed;
 Out.Inventory=Fresh->Inventory;Out.CombatPlayerResources=Fresh->CombatPlayerResources;Out.CombatWorld=Fresh->CombatWorld;
 Out.WorldActors=Fresh->WorldActors;Out.CompletedWorldEvents=Fresh->CompletedWorldEvents;Out.VisitedMaps=Fresh->VisitedMaps;
 Out.LanguageTrialSchema=Fresh->LanguageTrialSchema;Out.LanguageProgressJson=Fresh->LanguageProgressJson;Out.A01StateJson=Fresh->A01StateJson;
 Out.FormalLanguage=Fresh->FormalLanguage;Out.AdventureFlow=Fresh->AdventureFlow;Out.WorldMapMarkers=Fresh->WorldMapMarkers;Error.Reset();return true;
}
