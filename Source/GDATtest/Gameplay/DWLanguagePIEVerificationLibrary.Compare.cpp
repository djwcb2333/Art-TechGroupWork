#include "DWLanguagePIEVerificationLibrary.h"
#include "DWSaveGame.h"
#include "DWCombatEffectsSave.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWLanguageSubsystem.h"
#include "DWLanguageModel.h"
#include "Dom/JsonObject.h"
#include "JsonObjectConverter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DWIdleCaptureComparison
{
FString Encode(const TSharedRef<FJsonObject>& O){FString S;FJsonSerializer::Serialize(O,TJsonWriterFactory<>::Create(&S));return S;}
FString Reject(const FString& E){auto O=MakeShared<FJsonObject>();O->SetBoolField(TEXT("ok"),false);O->SetStringField(TEXT("error"),E);return Encode(O);}
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
bool Decode(const FString& Json,UDWSaveGame& S,FDWCombatEffectsSave& E,FDWCombatWorldObjectsSave& W,FString& Error)
{
    if(Json.Len()>64*1024*1024){Error=TEXT("Capture JSON size limit.");return false;}
    TSharedPtr<FJsonObject> O;FText Why;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),O)||!O||
        !FJsonObjectConverter::JsonObjectToUStruct(O.ToSharedRef(),UDWSaveGame::StaticClass(),&S,0,0,true,&Why))
    {Error=TEXT("Strict full-capture JSON decode failed: ")+Why.ToString();return false;}
    if(S.Version!=4||S.LanguageTrialSchema!=1||S.LanguageProgressJson.IsEmpty()||S.A01StateJson.IsEmpty()||S.CombatWorld.Schema!=1||S.CombatWorld.bNewAdventure)
    {Error=TEXT("Current complete LANG trial capture required; not a new-adventure marker.");return false;}
    if(!DWCombatEffectsSave::Decode(S.CombatWorld.Effects,E,Error)||!DWCombatWorldObjectsSave::Decode(S.CombatWorld.Objects,W,Error))return false;
    // This narrow seam deliberately rejects active payloads instead of erasing them.
    if(!E.Spores.IsEmpty()||!E.Arrows.IsEmpty()||!E.Bottles.IsEmpty()||!E.Areas.IsEmpty()||!E.TargetPhases.IsEmpty()||S.CombatWorld.Player.bRooted||!S.CombatWorld.Player.ShieldPackets.IsEmpty())
    {Error=TEXT("Idle comparison rejects released effects, root episode or pending shield payload.");return false;}
    for(const auto& A:E.Authority.Ledger.Actions)if(A.Active){Error=TEXT("Idle comparison rejects an active ledger action.");return false;}
    for(const auto& A:E.Authority.Ledger.Emitters)if(A.Active){Error=TEXT("Idle comparison rejects an active emitter.");return false;}
    for(const auto& R:E.Authority.Ledger.Receipts)if(R.Receipt.State==DWCombatCommit::EReceiptState::Pending){Error=TEXT("Idle comparison rejects a pending receipt.");return false;}
    for(const auto& Enemy:W.Enemies)if(Enemy.Phase||Enemy.MeleeAction.Id){Error=TEXT("Idle comparison rejects an enemy active melee phase.");return false;}
    return true;
}
bool ReEpoch(FDWCombatEffectsSave& E,uint64 Epoch,FString& Error)
{
    // Exact production remapping of all epoch-bearing ledger slots, not a UI copy.
    // Only this local ledger is adopted; no authority/world is looked up or mutated.
    DWCombatCommit::FLedger Local;DWCombatCommit::FLedgerRestorePlan Plan;std::string Why;
    if(!Local.PrepareSnapshotRestore(E.Authority.Ledger,Epoch,{},Plan,Why)){Error=UTF8_TO_TCHAR(Why.c_str());return false;}
    if(!Local.CanCommitSnapshotRestore(Plan)){Error=TEXT("Local canonical ledger plan was stale.");return false;}
    Local.CommitSnapshotRestoreUnchecked(std::move(Plan));E.Authority.Ledger=Local.CaptureSnapshot();return true;
}
#endif
}
FString DWCompareIdleCapturePayloads(UDWLanguageSubsystem& Language,const FString& BeforeNativeJson,const FString& AfterNativeJson)
{
    using namespace DWIdleCaptureComparison;
#if !(WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS)
    return Reject(TEXT("Editor development read-only codec comparison only."));
#else
    TStrongObjectPtr<UDWSaveGame> Before(NewObject<UDWSaveGame>(GetTransientPackage(),NAME_None,RF_Transient));
    TStrongObjectPtr<UDWSaveGame> After(NewObject<UDWSaveGame>(GetTransientPackage(),NAME_None,RF_Transient));
    FDWCombatEffectsSave E0,E1;FDWCombatWorldObjectsSave W0,W1;FString Error;
    if(!Decode(BeforeNativeJson,*Before,E0,W0,Error)||!Decode(AfterNativeJson,*After,E1,W1,Error))return Reject(Error);
    const uint64 OldEpoch=E0.Authority.Ledger.Epoch,NewEpoch=E1.Authority.Ledger.Epoch;
    if(NewEpoch<=OldEpoch||NewEpoch==MAX_uint64)return Reject(TEXT("Ordered real restore must advance its authority epoch, with room for canonical remapping."));
    auto O=MakeShared<FJsonObject>();auto Meta=MakeShared<FJsonObject>();
    const auto Exact=[](uint64 N){return FString::Printf(TEXT("%llu"),static_cast<unsigned long long>(N));};
    Meta->SetStringField(TEXT("authority_epoch_before"),Exact(OldEpoch));Meta->SetStringField(TEXT("authority_epoch_after"),Exact(NewEpoch));
    const auto HighWater=[&](const TCHAR* Name,uint64& A,uint64& B)
    {
        if(B<A){Error=FString(Name)+TEXT(" moved backwards; never normalized.");return false;}
        auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("before"),Exact(A));Row->SetStringField(TEXT("after"),Exact(B));Row->SetBoolField(TEXT("changed"),A!=B);Meta->SetObjectField(Name,Row);A=B;return true;
    };
    if(!Language.ValidateProgressJson(Before->LanguageProgressJson,Error)||!Language.ValidateProgressJson(After->LanguageProgressJson,Error))return Reject(Error);
    TSharedPtr<FJsonObject> Lang0,Lang1;const TSharedPtr<FJsonObject>* Model0=nullptr,*Model1=nullptr;
    if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Before->LanguageProgressJson),Lang0)||!Lang0||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(After->LanguageProgressJson),Lang1)||!Lang1||
        !Lang0->TryGetObjectField(TEXT("model"),Model0)||!Model0||!Lang1->TryGetObjectField(TEXT("model"),Model1)||!Model1)return Reject(TEXT("Language wrapper model missing."));
    DWLanguage::FState State0,State1;const auto& Catalog=Language.GetNativeModel()->GetCatalog();
    if(!DWLanguage::StateFromJson(Catalog,Encode((*Model0).ToSharedRef()),State0,Error)||
        !DWLanguage::StateFromJson(Catalog,Encode((*Model1).ToSharedRef()),State1,Error))return Reject(Error);
    if(State0.Revision>=MAX_int64-1||State1.Revision!=State0.Revision+1)return Reject(TEXT("Idle one-load comparison requires language.model.revision to advance exactly once; other pre-load language mutations are outside this seam."));
    auto LanguageMeta=MakeShared<FJsonObject>();LanguageMeta->SetStringField(TEXT("before"),LexToString(State0.Revision));LanguageMeta->SetStringField(TEXT("after"),LexToString(State1.Revision));Meta->SetObjectField(TEXT("LanguageProgressJson.model.revision"),LanguageMeta);
    State0.Revision=State1.Revision;FString ModelJson0,ModelJson1;TSharedPtr<FJsonObject> CanonicalModel0,CanonicalModel1;
    if(!DWLanguage::StateToJson(Catalog,State0,ModelJson0,Error)||!DWLanguage::StateToJson(Catalog,State1,ModelJson1,Error)||
        !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ModelJson0),CanonicalModel0)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ModelJson1),CanonicalModel1))return Reject(TEXT("Validated language canonical export failed: ")+Error);
    Lang0->SetObjectField(TEXT("model"),CanonicalModel0);Lang1->SetObjectField(TEXT("model"),CanonicalModel1);
    const bool LanguageEqual=FJsonValue::CompareEqual(FJsonValueObject(Lang0),FJsonValueObject(Lang1));
    Before->LanguageProgressJson=Encode(Lang0.ToSharedRef());After->LanguageProgressJson=Encode(Lang1.ToSharedRef());
    // Object key order is not payload state. Only after complete semantic equality
    // is established do both native FString properties share one serialization.
    if(LanguageEqual)Before->LanguageProgressJson=After->LanguageProgressJson;
    auto& L0=E0.Authority.Ledger;auto& L1=E1.Authority.Ledger;
    if(!HighWater(TEXT("Authority.LastEntity"),E0.Authority.LastEntity,E1.Authority.LastEntity)||
        !HighWater(TEXT("Ledger.LastAction"),L0.LastAction,L1.LastAction)||!HighWater(TEXT("Ledger.LastEmitter"),L0.LastEmitter,L1.LastEmitter)||!HighWater(TEXT("Ledger.LastReceipt"),L0.LastReceipt,L1.LastReceipt))return Reject(Error);
    // Actor Life/RestoreRevision, identities, receipts/outcomes, keys, every object,
    // inventory, language/talent/paid ledger and character field remain compared.
    if(!ReEpoch(E0,NewEpoch+1,Error)||!ReEpoch(E1,NewEpoch+1,Error)||
        !DWCombatEffectsSave::Encode(E0,Before->CombatWorld.Effects,Error)||!DWCombatEffectsSave::Encode(E1,After->CombatWorld.Effects,Error)||
        !DWCombatWorldObjectsSave::Encode(W0,Before->CombatWorld.Objects,Error)||!DWCombatWorldObjectsSave::Encode(W1,After->CombatWorld.Objects,Error))return Reject(Error);
    const bool EffectsEqual=Before->CombatWorld.Effects==After->CombatWorld.Effects,ObjectsEqual=Before->CombatWorld.Objects==After->CombatWorld.Objects;
    TArray<TSharedPtr<FJsonValue>> Differences;
    for(TFieldIterator<FProperty> It(UDWSaveGame::StaticClass());It;++It)
        if(!It->Identical_InContainer(Before.Get(),After.Get(),0,PPF_None))Differences.Add(MakeShared<FJsonValueString>(TEXT("aggregate.")+It->GetName()));
    auto J0=MakeShared<FJsonObject>(),J1=MakeShared<FJsonObject>();
    if(!FJsonObjectConverter::UStructToJsonObject(UDWSaveGame::StaticClass(),Before.Get(),J0,0,0)||
        !FJsonObjectConverter::UStructToJsonObject(UDWSaveGame::StaticClass(),After.Get(),J1,0,0))return Reject(TEXT("Complete normalized reflection export failed."));
    O->SetBoolField(TEXT("ok"),true);O->SetBoolField(TEXT("equal"),Differences.IsEmpty());O->SetArrayField(TEXT("native_differences"),Differences);
    O->SetBoolField(TEXT("effects_native_codec_equal"),EffectsEqual);O->SetBoolField(TEXT("world_objects_native_codec_equal"),ObjectsEqual);O->SetObjectField(TEXT("normalized_metadata"),Meta);
    O->SetObjectField(TEXT("normalized_before"),J0);O->SetObjectField(TEXT("normalized_after"),J1);
    O->SetBoolField(TEXT("language_native_model_equal"),LanguageEqual);O->SetObjectField(TEXT("language_normalized_before"),Lang0);O->SetObjectField(TEXT("language_normalized_after"),Lang1);
    O->SetStringField(TEXT("boundary"),TEXT("Pure strict capture decode and native codec/complete reflected-property comparison; only production ledger epoch remapping, four checked monotonic allocator high-water marks, and exact one-load language.model.revision increment normalized. Actor RestoreRevision is NOT excluded. No world, file, save format or gameplay mutation."));
    return Encode(O);
#endif
}
