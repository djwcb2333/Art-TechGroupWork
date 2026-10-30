#include "DWLanguageProgressPersistence.h"
#include "DWFormalLanguagePersistence.h"
#include "DWLanguageSubsystem.h"
#include "DWCombatEquipment.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FDWLanguageProgressRestorePlan::CanCommit()const
{if(bFormal)return FDWFormalLanguagePersistence::CanCommit(*this);return !bAdopted&&Owner.IsValid()&&Owner->GetNativeModel()&&Owner->GetNativeModel()->CanCommit(Prepared);}
bool FDWLanguageProgressPersistence::Prepare(UDWLanguageSubsystem& S,const FString& Json,FDWLanguageProgressRestorePlan& Out,FString& Error)
{
 if(!S.IsTrialWorld()||S.IsFormalSession()||!S.ValidateProgressJson(Json,Error))return false;
 TSharedPtr<FJsonObject> Root;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root)return false;
 const auto ModelObject=Root->GetObjectField(TEXT("model"));FString NativeJson;
 if(!FJsonSerializer::Serialize(ModelObject.ToSharedRef(),TJsonWriterFactory<>::Create(&NativeJson)))return false;
 DWLanguage::FState State;if(!DWLanguage::StateFromJson(S.GetNativeModel()->GetCatalog(),NativeJson,State,Error))return false;
 FDWLanguageProgressRestorePlan Candidate;DWLanguage::FOperationResult R;
 if(!S.GetNativeModel()->PrepareRestore(State,Candidate.Prepared,R)){Error=TEXT("Cannot prepare language restoration.");return false;}
 Candidate.Owner=&S;Candidate.PlaythroughId=Root->GetStringField(TEXT("playthrough_id"));
 Candidate.Starter=MakeShared<FDWCombatStarterGrantState>();Candidate.Starter->PlaythroughId=Candidate.PlaythroughId;Candidate.Starter->bGranted=true;
 Out=MoveTemp(Candidate);return true;
}
void FDWLanguageProgressPersistence::AdoptNativeFacts(FDWLanguageProgressRestorePlan& P)
{
 if(P.bFormal){FDWFormalLanguagePersistence::Adopt(P);return;}
 check(P.CanCommit());auto* S=P.Owner.Get();DWLanguage::FOperationResult R;
 S->Model->AdoptPreparedUnchecked(MoveTemp(P.Prepared),R);
 S->PlaythroughId=MoveTemp(P.PlaythroughId);S->RequestedPanel=EDWLanguagePanel::None;
 S->RequestedEntry=S->RequestedContext=NAME_None;S->ActiveCarrier.Reset();S->Selections.Reset();
 // The complete trial writer requires its once-only starter receipt; no grant is replayed on load.
 S->TrialContainerVersion=P.TrialContainerVersion;S->StarterGrant=MoveTemp(P.Starter);S->FormalSession.Reset();++S->SessionGeneration;
 P.bAdopted=true;
}
bool FDWLanguageProgressPersistence::Publish(FDWLanguageProgressRestorePlan& P,FString& Error)
{
 if(!P.bAdopted||!P.Owner.IsValid()){Error=TEXT("Language restoration owner was destroyed.");return false;}
 P.Owner->PublishAfterRestore();Error.Reset();return true;
}
