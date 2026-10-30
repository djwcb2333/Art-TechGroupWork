#pragma once
#include "CoreMinimal.h"
#include "DWFormalLanguageSave.h"
#include "DWAdventureFlowSave.h"
#include "DWCharacterA01SaveBridge.h"

class UDWSaveGame;
class FDWCombatRules;
class FJsonObject;

// Pure candidate validation only: never touches an actor, a world, a live subsystem, or a file.
namespace DWFormalLanguageProjection
{
 enum class EBlockKind : uint8 { Legacy, Trial, Formal };

 // Empty formal block leaves old/trial validation to its existing caller. Mixed or partial blocks fail.
 bool Classify(const FDWFormalLanguageSave&,const UDWSaveGame&,EBlockKind& Out,FString& Error);

 // Candidate for a fresh unopened adventure only. Save must already contain the immutable
 // DWCombatPlayerSave::MakeNew mirrors and CombatWorld.bNewAdventure=true. Use separate output
 // temporaries (do not alias a member of Save); this function never changes the input mirrors.
 // Approved formal-new-slot policy imports that untouched legacy HP/water/satiety into A01,
 // then refuses creation unless the whole reconstructed mirror exactly matches the original.
 bool InitializeNew(const FDWAdventureFlowSave&,int32 ExpectedSlot,const UDWSaveGame&,
  const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,const FDWCombatRules&,
  FDWFormalLanguageSave& Out,FDWA01SavedProjection& OutProjection,FString& Error);

 // Persisted-record path: verifies the complete supplied Flow against Save.AdventureFlow,
 // then validates GUID, slot, starter receipt, wrapper, native facts and legacy mirrors.
 // ExpectedSlot is the save slot index 0..2 (zero is valid). Outputs only on complete success.
 bool Prepare(const FDWFormalLanguageSave&,const FDWAdventureFlowSave&,int32 ExpectedSlot,
  const UDWSaveGame&,const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,
  const FDWCombatRules&,FDWA01SavedProjection& Out,FString& Error);

 // Formal-only validation. Legacy/trial callers must dispatch to their original validators.
 bool ValidateFormal(const FDWFormalLanguageSave&,const FDWAdventureFlowSave&,int32 ExpectedSlot,
  const UDWSaveGame&,const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,
  const FDWCombatRules&,FString& Error);

 // Saved-record envelope binding: a schema-1 adventure cannot silently lose its formal block.
 // Legacy/trial output still requires its existing separate world/trial validator; this is not
 // an all-save validator, a map authorization check, or proof of a live authority receipt.
 bool ValidateBound(const FDWFormalLanguageSave&,const FDWAdventureFlowSave&,int32 ExpectedSlot,
  const UDWSaveGame&,const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,
  const FDWCombatRules&,EBlockKind& OutKind,FString& Error);

 // Preferred persisted-record entry point after the owner adds Save.FormalLanguage.
 // The dependent member is resolved at instantiation, so this candidate remains independent
 // of the not-yet-merged DWSaveGame header. Never substitute an external envelope here.
 template<typename TSave>
 bool ValidateBoundStored(const TSave& Save,int32 ExpectedSlot,const DWLanguage::FCatalog& Catalog,
  const TSharedPtr<FJsonObject>& Trial,const FDWCombatRules& Rules,EBlockKind& OutKind,FString& Error)
 {
  return ValidateBound(Save.FormalLanguage,Save.AdventureFlow,ExpectedSlot,Save,Catalog,Trial,Rules,OutKind,Error);
 }

 template<typename TSave>
 bool PrepareStored(const TSave& Save,int32 ExpectedSlot,const DWLanguage::FCatalog& Catalog,
  const TSharedPtr<FJsonObject>& Trial,const FDWCombatRules& Rules,FDWA01SavedProjection& Out,FString& Error)
 {
  return Prepare(Save.FormalLanguage,Save.AdventureFlow,ExpectedSlot,Save,Catalog,Trial,Rules,Out,Error);
 }

 // Formal capture has one source of truth: Flow.bStarterGranted. A pending new slot may be
 // initialized without a fake granted receipt; running capture requires Flow.bStarterGranted=true.
 // The caller must separately prove that Flow represents the authority-bound native receipt.
 // The supplied save is a previously captured in-memory aggregate; no field is modified here.
 bool Capture(const FDWAdventureFlowSave&,int32 ExpectedSlot,const UDWSaveGame&,
  const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,const FDWCombatRules&,
  const DWLanguage::FState&,const FDWCharacterA01Runtime&,double Cooldown,
  FDWFormalLanguageSave& Out,FDWA01SavedProjection& OutProjection,FString& Error);

 // Special unpersisted first-grant transaction only. Save is the captured aggregate with the
 // original pending/false AdventureFlow. All outputs must be separate temporaries and commit only on success.
 // This is NOT authorization or evidence that any native grant happened: caller must verify the
 // real committed receipt and current owner/world/slot/epoch before AND immediately before write.
 // Place returned Flow and envelope into the SAME candidate Save and run persisted ValidateBound.
 bool CapturePreparedFirstStarter(const FDWAdventureFlowSave& ExpectedStoredFlow,int32 ExpectedSlot,
  const UDWSaveGame& UnpersistedCapture,const DWLanguage::FCatalog&,const TSharedPtr<FJsonObject>& Trial,
  const FDWCombatRules&,const DWLanguage::FState&,const FDWCharacterA01Runtime&,double Cooldown,
  FDWAdventureFlowSave& OutPreparedFlow,FDWFormalLanguageSave& Out,FDWA01SavedProjection& OutProjection,FString& Error);
}
