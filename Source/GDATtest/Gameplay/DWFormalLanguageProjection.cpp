#include "DWFormalLanguageProjection.h"
#include "DWCombatRules.h"
#include "DWCombatPlayerSave.h"
#include "DWSaveGame.h"
#include "DWFormalSaveVersionPolicy.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "UObject/StrongObjectPtr.h"

namespace DWFormalProjectionPrivate
{
bool Fail(FString& Error,const TCHAR* Reason){Error=Reason;return false;}
bool TrialFieldsPresent(const UDWSaveGame& Save)
{return Save.LanguageTrialSchema!=0||!Save.LanguageProgressJson.IsEmpty()||!Save.A01StateJson.IsEmpty();}
bool EmptyFormal(const FDWFormalLanguageSave& Envelope)
{return Envelope.Schema==0&&Envelope.SlotIndex==INDEX_NONE&&Envelope.ProgressJson.IsEmpty()&&Envelope.A01StateJson.IsEmpty();}
bool SameFlow(const FDWAdventureFlowSave& Given,const FDWAdventureFlowSave& Stored,FString& Error)
{
 bool SameId=Given.PlaythroughId.IsEmpty()&&Stored.PlaythroughId.IsEmpty();
 if(!SameId)
 {
  FGuid A,B;
  SameId=FGuid::ParseExact(Given.PlaythroughId,EGuidFormats::Digits,A)&&A.IsValid()&&
   FGuid::ParseExact(Stored.PlaythroughId,EGuidFormats::Digits,B)&&B.IsValid()&&A==B;
 }
 if(Given.Schema!=Stored.Schema||!SameId||Given.ScenarioId!=Stored.ScenarioId||
  Given.OpeningMap!=Stored.OpeningMap||Given.DestinationMap!=Stored.DestinationMap||
  Given.Phase!=Stored.Phase||Given.bStarterGranted!=Stored.bStarterGranted)
  return Fail(Error,TEXT("Supplied adventure flow does not match the saved flow identity and receipt."));
 return true;
}
bool Number(const TSharedPtr<FJsonObject>& Trial,const TCHAR* Key,double& Value)
{
 const TSharedPtr<FJsonObject>* Parameters=nullptr;const TSharedPtr<FJsonObject>* Row=nullptr;
 return Trial.IsValid()&&Trial->TryGetObjectField(TEXT("parameters"),Parameters)&&Parameters&&Parameters->IsValid()&&
  (*Parameters)->TryGetObjectField(Key,Row)&&Row&&Row->IsValid()&&(*Row)->TryGetNumberField(TEXT("value"),Value)&&FMath::IsFinite(Value);
}
bool CheckFlow(const FDWAdventureFlowSave& Flow,int32 Slot,const UDWSaveGame& Save,FString& Error)
{
 FGuid Id;
 if(Save.Version!=DWFormalSaveVersionPolicy::FormalVersion||Slot<0||Slot>=3||Flow.Schema!=1||!FGuid::ParseExact(Flow.PlaythroughId,EGuidFormats::Digits,Id)||!Id.IsValid())
  return Fail(Error,TEXT("Formal slot, adventure schema or playthrough ID is invalid."));
 if(Save.CombatWorld.bNewAdventure)
 {
  if(Flow.Phase!=EDWAdventureOpeningPhase::Pending||Flow.bStarterGranted)
   return Fail(Error,TEXT("Unstarted new adventure must be pending without a granted starter receipt."));
 }
 else if(!Flow.bStarterGranted||(Flow.Phase!=EDWAdventureOpeningPhase::Pending&&
  Flow.Phase!=EDWAdventureOpeningPhase::CompletedPendingDestination&&Flow.Phase!=EDWAdventureOpeningPhase::Completed))
  return Fail(Error,TEXT("Running formal adventure requires an authentic granted starter receipt and valid phase."));
 return true;
}
bool SameMirror(const FDWA01SavedProjection& Projection,const UDWSaveGame& Save,FString& Error)
{
 const auto& A=Projection.LegacySnapshot;const auto& B=Save.CombatPlayerResources.Vitals;
 const auto Equal=[](double X,double Y){return FMath::IsFinite(X)&&FMath::IsFinite(Y)&&FMath::Abs(X-Y)<=1.e-8;};
 if(Save.CombatPlayerResources.SubblockSchema!=DWCombatPlayerSave::SubblockSchema||
  B.ResourceSchema!=A.ResourceSchema||B.RulesCombatSchema!=A.RulesCombatSchema||B.BaseForm!=A.BaseForm||
  !Equal(B.Health,A.Health)||!Equal(B.Water,A.Water)||!Equal(B.Satiety,A.Satiety)||!Equal(B.Special,A.Special)||
  !Equal(B.FormRemainingSeconds,A.FormRemainingSeconds)||B.FormTimeCarry!=0.||B.SpecialGrowthCarry!=0.||
  B.WaterDrainCarry!=0.||B.SatietyDrainCarry!=0.||B.WaterEmptyElapsedSeconds!=0.||B.SatietyEmptyElapsedSeconds!=0.||
  B.WaterEmptyTimeCarry!=0.||B.SatietyEmptyTimeCarry!=0.||B.DehydrationOrdinal!=A.DehydrationOrdinal||
  B.StarvationOrdinal!=A.StarvationOrdinal||Save.Health!=static_cast<float>(A.Health)||
  Save.Transformation!=static_cast<float>(A.Special)||Save.bYeastForm!=(A.BaseForm==EDWCombatBaseForm::Yeast))
  return Fail(Error,TEXT("Formal A01 and existing compatibility mirrors disagree."));
 return true;
}
bool Wrap(const DWLanguage::FCatalog& Catalog,const DWLanguage::FState& State,const FString& Id,FString& Out,FString& Error)
{
 FString NativeJson;if(!DWLanguage::StateToJson(Catalog,State,NativeJson,Error))return false;
 TSharedPtr<FJsonObject> Model;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(NativeJson),Model)||!Model.IsValid())
  return Fail(Error,TEXT("Native language state could not be wrapped."));
 auto Root=MakeShared<FJsonObject>();Root->SetNumberField(TEXT("schema_version"),2.);
 Root->SetStringField(TEXT("playthrough_id"),Id);Root->SetObjectField(TEXT("model"),Model);
 FString Candidate;if(!FJsonSerializer::Serialize(Root,TJsonWriterFactory<>::Create(&Candidate)))
  return Fail(Error,TEXT("Formal language wrapper could not be serialized."));
 Out=MoveTemp(Candidate);return true;
}
}

bool DWFormalLanguageProjection::Classify(const FDWFormalLanguageSave& Envelope,const UDWSaveGame& Save,EBlockKind& Out,FString& Error)
{
 using namespace DWFormalProjectionPrivate;
 DWFormalSaveVersionPolicy::EFamily Family;
 if(!DWFormalSaveVersionPolicy::Classify(Save,Envelope,Family,Error))return false;
 Out=Family==DWFormalSaveVersionPolicy::EFamily::Formal?EBlockKind::Formal:
  Family==DWFormalSaveVersionPolicy::EFamily::Trial?EBlockKind::Trial:EBlockKind::Legacy;
 Error.Reset();return true;
}

namespace DWFormalProjectionPrivate
{
bool PrepareContents(const FDWFormalLanguageSave& Envelope,const FDWAdventureFlowSave& Flow,int32 Slot,
 const UDWSaveGame& Save,const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,
 const FDWCombatRules& Rules,FDWA01SavedProjection& Out,FString& Error)
{
 DWFormalLanguageProjection::EBlockKind Kind;
 if(!DWFormalLanguageProjection::Classify(Envelope,Save,Kind,Error)||Kind!=DWFormalLanguageProjection::EBlockKind::Formal)
  return Fail(Error,TEXT("No valid formal language block to prepare."));
 if(!CheckFlow(Flow,Slot,Save,Error))return false;
 if(Envelope.SlotIndex!=Slot)return Fail(Error,TEXT("Formal language block belongs to a different slot."));
 if(!DWLanguage::ValidateCatalog(Catalog,Error))return false;
 TSharedPtr<FJsonObject> Root;const TSharedPtr<FJsonObject>* Model=nullptr;FString Id;double Version=0.;FGuid StoredId,FlowId;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Envelope.ProgressJson),Root)||!Root.IsValid()||
  Root->Values.Num()!=3||!Root->TryGetNumberField(TEXT("schema_version"),Version)||Version!=2.||
  !Root->TryGetStringField(TEXT("playthrough_id"),Id)||!FGuid::ParseExact(Id,EGuidFormats::Digits,StoredId)||!StoredId.IsValid()||
  !FGuid::ParseExact(Flow.PlaythroughId,EGuidFormats::Digits,FlowId)||StoredId!=FlowId||
  !Root->TryGetObjectField(TEXT("model"),Model)||!Model||!Model->IsValid())
  return Fail(Error,TEXT("Formal language wrapper or playthrough ID is invalid."));
 FString NativeJson;if(!FJsonSerializer::Serialize(Model->ToSharedRef(),TJsonWriterFactory<>::Create(&NativeJson)))
  return Fail(Error,TEXT("Formal model could not be serialized for validation."));
 FDWA01SavedProjection Candidate;
 if(!DWLanguage::StateFromJson(Catalog,NativeJson,Candidate.LanguageState,Error))return false;
 TSet<FName> Learned;for(const auto& Pair:Candidate.LanguageState.LearnedTalentPaidCosts)Learned.Add(Pair.Key);
 FDWCharacterA01Config Derived;double MaximumCooldown=0.;
 if(!DWCharacterA01SaveBridge::BuildDerivedConfig(Trial,Learned,Derived,Error)||
  !Number(Trial,TEXT("yeast.dash.cooldown_s"),MaximumCooldown)||MaximumCooldown<0.||
  !DWCharacterA01SaveBridge::FormalStateFromJson(Envelope.A01StateJson,Derived,MaximumCooldown,Candidate.Runtime,Candidate.CooldownRemaining,Error)||
  !DWCharacterA01SaveBridge::BuildMirror(Rules,Candidate.Runtime,Candidate.LegacyConfig,Candidate.LegacySnapshot,Error))return false;
 double Yeast=0.,Mold=0.,Bonus=0.;
 if(!Number(Trial,TEXT("yeast.duration_s"),Yeast)||!Number(Trial,TEXT("mold.duration_s"),Mold)||
  !Number(Trial,TEXT("talent.b3.duration_add_s"),Bonus)||
  Candidate.Runtime.GetState().EntryDurationSeconds>FMath::Max(Yeast,Mold)+Bonus)
  return Fail(Error,TEXT("Formal A01 entry duration exceeds the configured possible maximum."));
 if(!SameMirror(Candidate,Save,Error))return false;
 Candidate.MaximumCooldown=MaximumCooldown;Out=MoveTemp(Candidate);Error.Reset();return true;
}
}

bool DWFormalLanguageProjection::Prepare(const FDWFormalLanguageSave& Envelope,const FDWAdventureFlowSave& Flow,int32 Slot,
 const UDWSaveGame& Save,const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,
 const FDWCombatRules& Rules,FDWA01SavedProjection& Out,FString& Error)
{
 if(!DWFormalProjectionPrivate::SameFlow(Flow,Save.AdventureFlow,Error))return false;
 return DWFormalProjectionPrivate::PrepareContents(Envelope,Flow,Slot,Save,Catalog,Trial,Rules,Out,Error);
}

bool DWFormalLanguageProjection::InitializeNew(const FDWAdventureFlowSave& Flow,int32 Slot,const UDWSaveGame& Save,
 const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,const FDWCombatRules& Rules,
 FDWFormalLanguageSave& Out,FDWA01SavedProjection& OutProjection,FString& Error)
{
 using namespace DWFormalProjectionPrivate;
 if(!SameFlow(Flow,Save.AdventureFlow,Error))return false;
 if(!Save.CombatWorld.bNewAdventure||TrialFieldsPresent(Save)||!EmptyFormal(Out)||!CheckFlow(Flow,Slot,Save,Error))
  return Fail(Error,TEXT("Formal initialization requires an untouched pending new adventure."));
 TStrongObjectPtr<UDWSaveGame> Original(NewObject<UDWSaveGame>());
 if(!Original.IsValid()||!DWCombatPlayerSave::MakeNew(Rules,*Original,Error))return false;
 const bool LegacyResources=FDWCombatPlayerResourceSave::StaticStruct()->CompareScriptStruct(&Save.CombatPlayerResources,&Original->CombatPlayerResources,PPF_None);
 TStrongObjectPtr<UDWSaveGame> Full(NewObject<UDWSaveGame>());
 if(!Full.IsValid()||!DWCombatPlayerSave::MakeNewFullSurvival(Rules,*Full,Error))return false;
 const bool FullResources=FDWCombatPlayerResourceSave::StaticStruct()->CompareScriptStruct(&Save.CombatPlayerResources,&Full->CombatPlayerResources,PPF_None);
 if((!LegacyResources&&!FullResources)||
  Save.Health!=Original->Health||Save.Transformation!=Original->Transformation||Save.bYeastForm!=Original->bYeastForm||
  Save.SprintAlcoholElapsed!=Original->SprintAlcoholElapsed||Save.TransformationDecayElapsed!=Original->TransformationDecayElapsed)
  return Fail(Error,TEXT("Formal initialization refuses a modified new-adventure legacy resource block."));
 DWLanguage::FState Empty;FDWCharacterA01Config Config;
 if(!DWLanguage::ValidateState(Catalog,Empty,Error)||!DWCharacterA01SaveBridge::BuildDerivedConfig(Trial,{},Config,Error))return false;
 if(FullResources&&(Save.CombatPlayerResources.Vitals.Water!=Config.WaterMax||Save.CombatPlayerResources.Vitals.Satiety!=Config.SatietyMax))
  return Fail(Error,TEXT("Full new-adventure survival maxima disagree across combat and A01 definitions."));
 FDWCharacterA01Runtime Runtime;std::string NativeError;
 if(!Runtime.Initialize(Config,&NativeError)){Error=UTF8_TO_TCHAR(NativeError.c_str());return false;}
 // Mirror the validated new-slot resource snapshot. Older pending snapshots stay readable.
 auto State=Runtime.Capture();const auto& Legacy=Save.CombatPlayerResources.Vitals;
 State.CurrentHealth=Legacy.Health;State.Water=Legacy.Water;State.Satiety=Legacy.Satiety;
 if(!Runtime.Restore(Config,State,&NativeError)){Error=UTF8_TO_TCHAR(NativeError.c_str());return false;}
 FDWFormalLanguageSave Candidate;Candidate.Schema=2;Candidate.SlotIndex=Slot;
 if(!Wrap(Catalog,Empty,Flow.PlaythroughId,Candidate.ProgressJson,Error)||
  !DWCharacterA01SaveBridge::FormalStateToJson(Runtime.Capture(),0.,Candidate.A01StateJson,Error))return false;
 FDWA01SavedProjection Projection;
 // The A01 projection must agree with the accepted, untouched resource snapshot.
 if(!Prepare(Candidate,Flow,Slot,Save,Catalog,Trial,Rules,Projection,Error))return false;
 Out=MoveTemp(Candidate);OutProjection=MoveTemp(Projection);Error.Reset();return true;
}

bool DWFormalLanguageProjection::ValidateFormal(const FDWFormalLanguageSave& Envelope,const FDWAdventureFlowSave& Flow,int32 Slot,
 const UDWSaveGame& Save,const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,
 const FDWCombatRules& Rules,FString& Error)
{
 FDWA01SavedProjection Candidate;
 return Prepare(Envelope,Flow,Slot,Save,Catalog,Trial,Rules,Candidate,Error);
}

bool DWFormalLanguageProjection::ValidateBound(const FDWFormalLanguageSave& Envelope,const FDWAdventureFlowSave& Flow,int32 Slot,
 const UDWSaveGame& Save,const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,
 const FDWCombatRules& Rules,EBlockKind& OutKind,FString& Error)
{
 EBlockKind CandidateKind;
 if(Slot<0||Slot>=3)return DWFormalProjectionPrivate::Fail(Error,TEXT("Saved slot index is invalid."));
 if(!DWFormalProjectionPrivate::SameFlow(Flow,Save.AdventureFlow,Error))return false;
 if(!Classify(Envelope,Save,CandidateKind,Error))return false;
 if(CandidateKind!=EBlockKind::Formal)
 {
  OutKind=CandidateKind;Error.Reset();return true; // Caller must still run the original legacy/trial validator.
 }
 if(!ValidateFormal(Envelope,Flow,Slot,Save,Catalog,Trial,Rules,Error))return false;
 OutKind=CandidateKind;Error.Reset();return true;
}

bool DWFormalLanguageProjection::Capture(const FDWAdventureFlowSave& Flow,int32 Slot,const UDWSaveGame& Save,
 const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,const FDWCombatRules& Rules,
 const DWLanguage::FState& State,const FDWCharacterA01Runtime& Runtime,double Cooldown,
 FDWFormalLanguageSave& Out,FDWA01SavedProjection& OutProjection,FString& Error)
{
 using namespace DWFormalProjectionPrivate;
 if(!SameFlow(Flow,Save.AdventureFlow,Error))return false;
 if(&State==&OutProjection.LanguageState||&Runtime==&OutProjection.Runtime)
  return Fail(Error,TEXT("Formal capture outputs must not alias source model or runtime."));
 if(Save.CombatWorld.bNewAdventure||!CheckFlow(Flow,Slot,Save,Error)||!Runtime.IsReady())
  return Fail(Error,TEXT("Formal capture requires a running adventure with an authentic starter receipt."));
 FDWFormalLanguageSave Candidate;Candidate.Schema=2;Candidate.SlotIndex=Slot;
 if(!Wrap(Catalog,State,Flow.PlaythroughId,Candidate.ProgressJson,Error)||
  !DWCharacterA01SaveBridge::FormalStateToJson(Runtime.Capture(),Cooldown,Candidate.A01StateJson,Error))return false;
 FDWA01SavedProjection Projection;
 if(!Prepare(Candidate,Flow,Slot,Save,Catalog,Trial,Rules,Projection,Error))return false;
 Out=MoveTemp(Candidate);OutProjection=MoveTemp(Projection);Error.Reset();return true;
}

bool DWFormalLanguageProjection::CapturePreparedFirstStarter(const FDWAdventureFlowSave& ExpectedStoredFlow,int32 Slot,
 const UDWSaveGame& Save,const DWLanguage::FCatalog& Catalog,const TSharedPtr<FJsonObject>& Trial,const FDWCombatRules& Rules,
 const DWLanguage::FState& State,const FDWCharacterA01Runtime& Runtime,double Cooldown,
 FDWAdventureFlowSave& OutPreparedFlow,FDWFormalLanguageSave& Out,FDWA01SavedProjection& OutProjection,FString& Error)
{
 using namespace DWFormalProjectionPrivate;
 if(&OutPreparedFlow==&Save.AdventureFlow||&OutPreparedFlow==&ExpectedStoredFlow||
  &State==&OutProjection.LanguageState||&Runtime==&OutProjection.Runtime)
  return Fail(Error,TEXT("Prepared outputs must not alias input flow, model or runtime."));
 if(!SameFlow(ExpectedStoredFlow,Save.AdventureFlow,Error))return false;
 if(Slot<0||Slot>=3||Save.CombatWorld.bNewAdventure||ExpectedStoredFlow.Schema!=1||
  ExpectedStoredFlow.Phase!=EDWAdventureOpeningPhase::Pending||ExpectedStoredFlow.bStarterGranted||!Runtime.IsReady())
  return Fail(Error,TEXT("Prepared starter capture requires a pending ungranted flow and unpersisted world capture."));
 FDWAdventureFlowSave CandidateFlow=ExpectedStoredFlow;CandidateFlow.bStarterGranted=true;
 if(!CheckFlow(CandidateFlow,Slot,Save,Error))return false;
 FDWFormalLanguageSave Candidate;Candidate.Schema=2;Candidate.SlotIndex=Slot;
 if(!Wrap(Catalog,State,CandidateFlow.PlaythroughId,Candidate.ProgressJson,Error)||
  !DWCharacterA01SaveBridge::FormalStateToJson(Runtime.Capture(),Cooldown,Candidate.A01StateJson,Error))return false;
 FDWA01SavedProjection Projection;
 // Deliberately private prepared-content check: public persisted validators reject false/true mismatch.
 if(!PrepareContents(Candidate,CandidateFlow,Slot,Save,Catalog,Trial,Rules,Projection,Error))return false;
 OutPreparedFlow=MoveTemp(CandidateFlow);Out=MoveTemp(Candidate);OutProjection=MoveTemp(Projection);Error.Reset();return true;
}
