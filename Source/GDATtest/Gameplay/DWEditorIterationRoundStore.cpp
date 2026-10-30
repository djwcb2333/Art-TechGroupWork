#include "DWEditorIterationRoundStore.h"
#include "DWEditorAuthoringSignature.h"
#include "DWSaveStorage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
namespace
{
bool Read(const FString& Path,TSharedPtr<FJsonObject>& Out)
{FString Text;return FFileHelper::LoadFileToString(Text,*Path)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)&&Out.IsValid();}
bool Write(const FString& Path,const TSharedRef<FJsonObject>& Json)
{FString Text;if(!FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text)))return false;FTCHARToUTF8 Utf8(*Text);TArray<uint8> Data;Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());return DWSaveStorage::WriteBytes(Path,Data);}
}
bool DWEditorIterationRoundStore::CanReuse(const FString& A,const FString& B){return !A.IsEmpty()&&A==B;}
bool DWEditorIterationRoundStore::IsRoundId(const FString& Id){FGuid Guid;return FGuid::ParseExact(Id,EGuidFormats::Digits,Guid)&&Guid.IsValid();}
bool DWEditorIterationRoundStore::Select(const FString& Root,const FString& Project,const FDWEditorAuthoringSignature& Current,FDWEditorIterationRound& Out,FString& Error)
{
 if(Project.IsEmpty()||Current.Digest.IsEmpty()){Error=TEXT("Missing editor project/signature; no round selected.");return false;}
 const FString Index=Root/TEXT("CurrentRound.json");FString PreviousId,PreviousSignature;
 if(IFileManager::Get().FileExists(*Index))
 {
  TSharedPtr<FJsonObject> J;double Schema=0.;FString Owner;
  if(!Read(Index,J)||!J->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1.||!J->TryGetStringField(TEXT("project"),Owner)||Owner!=Project||
     !J->TryGetStringField(TEXT("round"),PreviousId)||!IsRoundId(PreviousId)||!J->TryGetStringField(TEXT("signature"),PreviousSignature))
  {Error=TEXT("Editor iteration index is unreadable; old rounds preserved, no formal fallback.");return false;}
 }
 FDWEditorIterationRound R;R.Signature=Current.Digest;R.Id=CanReuse(PreviousSignature,Current.Digest)?PreviousId:FGuid::NewGuid().ToString(EGuidFormats::Digits);
 R.Directory=FPaths::ConvertRelativePathToFull(Root/TEXT("Rounds")/R.Id);R.StorageIdentity=Project+TEXT(":")+R.Id+TEXT(":")+R.Signature;
 if(R.Id==PreviousId)
 {
  TSharedPtr<FJsonObject> Existing;FString Id,Signature,Owner;double Schema=0.;
  if(!Read(R.Directory/TEXT("RoundManifest.json"),Existing)||!Existing->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1.||!Existing->TryGetStringField(TEXT("project"),Owner)||Owner!=Project||!Existing->TryGetStringField(TEXT("round"),Id)||Id!=R.Id||!Existing->TryGetStringField(TEXT("signature"),Signature)||Signature!=R.Signature)
  {Error=TEXT("Current editor round manifest is missing or changed; no fallback.");return false;}
 }
 else
 {
  if(IFileManager::Get().DirectoryExists(*R.Directory)||!IFileManager::Get().MakeDirectory(*R.Directory,true))
  {Error=TEXT("Cannot allocate a fresh editor round directory.");return false;}
  auto Round=MakeShared<FJsonObject>();Round->SetNumberField(TEXT("schema"),1.);Round->SetStringField(TEXT("project"),Project);Round->SetStringField(TEXT("round"),R.Id);Round->SetStringField(TEXT("signature"),R.Signature);Round->SetStringField(TEXT("previous_round"),PreviousId);
  Round->SetStringField(TEXT("created_utc"),FDateTime::UtcNow().ToIso8601());Round->SetStringField(TEXT("authoring_manifest"),Current.ManifestJson);
  if(!Write(R.Directory/TEXT("RoundManifest.json"),Round)){Error=TEXT("Cannot write editor round manifest; earlier rounds preserved.");return false;}
  auto NewIndex=MakeShared<FJsonObject>();NewIndex->SetNumberField(TEXT("schema"),1.);NewIndex->SetStringField(TEXT("project"),Project);NewIndex->SetStringField(TEXT("round"),R.Id);NewIndex->SetStringField(TEXT("signature"),R.Signature);
  if(!Write(Index,NewIndex)){Error=TEXT("Cannot commit editor round index; earlier rounds preserved.");return false;}
 }
 Out=MoveTemp(R);Error.Reset();return true;
}
