#include "DWEditorIterationRoundStore.h"
#include "DWEditorAuthoringSignature.h"
#include "DWSaveStorage.h"
#include "Engine/World.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
namespace
{
FString Hash(const FString& Text)
{FTCHARToUTF8 Utf8(*Text);uint8 Bytes[20];FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Bytes);return BytesToHex(Bytes,20);}
bool IsDigest(const FString& Value)
{if(Value.Len()!=40)return false;for(TCHAR C:Value)if(!((C>='0'&&C<='9')||(C>='A'&&C<='F')))return false;return true;}
bool Read(const FString& Path,TSharedPtr<FJsonObject>& Out)
{
 const int64 Size=IFileManager::Get().FileSize(*Path);
 if(Size<=0||Size>16*1024*1024)return false;
 FString Text;return FFileHelper::LoadFileToString(Text,*Path)&&FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Out)&&Out.IsValid();
}
bool Write(const FString& Path,const TSharedRef<FJsonObject>& Json)
{FString Text;if(!FJsonSerializer::Serialize(Json,TJsonWriterFactory<>::Create(&Text)))return false;FTCHARToUTF8 Utf8(*Text);TArray<uint8> Data;Data.Append(reinterpret_cast<const uint8*>(Utf8.Get()),Utf8.Length());return DWSaveStorage::WriteBytes(Path,Data);}
bool ValidAuthoringManifest(const FString& Text,const FString& Digest)
{
 if(Text.Len()>16*1024*1024||!IsDigest(Digest))return false;
 TSharedPtr<FJsonObject> J;double Schema=0.;FString Algorithm,Recorded;
 const TArray<TSharedPtr<FJsonValue>>* Rows=nullptr;
 if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),J)||!J.IsValid()||
    !J->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1.||
    !J->TryGetStringField(TEXT("algorithm"),Algorithm)||Algorithm!=TEXT("sha1-auth-v1")||
    !J->TryGetStringField(TEXT("signature"),Recorded)||Recorded!=Digest||
    !J->TryGetArrayField(TEXT("dependencies"),Rows)||Rows->IsEmpty()||Rows->Num()>5004)return false;
 TMap<FString,FString> Files;TSet<FName> Names;
 for(const auto& Row:*Rows)
 {
  const TSharedPtr<FJsonObject>* Object=nullptr;FString Path,FileDigest;
  if(!Row.IsValid()||!Row->TryGetObject(Object)||!Object||!Object->IsValid()||
     !(*Object)->TryGetStringField(TEXT("path"),Path)||Path.IsEmpty()||Path.Contains(TEXT("\n"))||Path.Contains(TEXT("\r"))||
     !(*Object)->TryGetStringField(TEXT("digest"),FileDigest)||!IsDigest(FileDigest)||Names.Contains(FName(*Path)))return false;
  Names.Add(FName(*Path));Files.Add(Path,FileDigest);
 }
 TArray<FString> Keys;Files.GetKeys(Keys);Keys.Sort();FString Canonical=TEXT("DW_EDITOR_AUTHORING_SHA1_V1\n");
 for(const FString& Key:Keys)Canonical+=Key+TEXT("\n")+Files[Key]+TEXT("\n");
 return Hash(Canonical)==Digest;
}
bool ValidRound(const FString& Directory,const FString& Project,const FString& Id,const FString& Signature,
                double ExpectedSchema,const FString& Context)
{
 TSharedPtr<FJsonObject> J;double Schema=0.;FString Owner,Round,Digest,Manifest,StoredContext;
 return Read(Directory/TEXT("RoundManifest.json"),J)&&J->TryGetNumberField(TEXT("schema"),Schema)&&Schema==ExpectedSchema&&
  J->TryGetStringField(TEXT("project"),Owner)&&Owner==Project&&J->TryGetStringField(TEXT("round"),Round)&&Round==Id&&
  J->TryGetStringField(TEXT("signature"),Digest)&&Digest==Signature&&
  (ExpectedSchema==1.||(J->TryGetStringField(TEXT("context"),StoredContext)&&StoredContext==Context))&&
  J->TryGetStringField(TEXT("authoring_manifest"),Manifest)&&ValidAuthoringManifest(Manifest,Signature);
}
}
bool DWEditorIterationRoundStore::MakeContextKey(const FString& Entry,const FString& Config,FString& Out,FString& Error)
{
 Out.Reset();const FString Package=UWorld::RemovePIEPrefix(Entry);
 // Inputs are saved package / exact object paths, never arbitrary directory components.
 if(!FPackageName::IsValidLongPackageName(Package)||Package.StartsWith(TEXT("/Temp/"))||Package.StartsWith(TEXT("/Script/"))||
    !FPackageName::IsValidObjectPath(Config)||Config.Contains(TEXT(":"))||Config.StartsWith(TEXT("/Temp/"))||Config.StartsWith(TEXT("/Script/")))
 {Error=TEXT("Invalid saved author entry or config object identity; no context selected.");return false;}
 Out=Hash(TEXT("DW_EDITOR_CONTEXT_SHA1_V1\n")+Package.ToLower()+TEXT("\n")+Config.ToLower()+TEXT("\n"));Error.Reset();return true;
}
bool DWEditorIterationRoundStore::CanReuse(const FString& A,const FString& B){return !A.IsEmpty()&&A==B;}
bool DWEditorIterationRoundStore::IsRoundId(const FString& Id){FGuid Guid;return FGuid::ParseExact(Id,EGuidFormats::Digits,Guid)&&Guid.IsValid();}
bool DWEditorIterationRoundStore::Select(const FString& Root,const FString& Project,const FString& Context,
 const FDWEditorAuthoringSignature& Current,FDWEditorIterationRound& Out,FString& Error)
{
 Out=FDWEditorIterationRound();
 if(Root.IsEmpty()||FPaths::IsRelative(Root)||Project.IsEmpty()||!IsDigest(Context)||!ValidAuthoringManifest(Current.ManifestJson,Current.Digest))
 {Error=TEXT("Invalid editor project/context/signature; no round selected.");return false;}
 // Validate and preserve the legacy pointer; never adopt or rewrite a legacy round implicitly.
 const FString LegacyIndex=Root/TEXT("CurrentRound.json");
 if(IFileManager::Get().FileExists(*LegacyIndex))
 {
  TSharedPtr<FJsonObject> J;double Schema=0.;FString Owner,Id,Digest;
  if(!Read(LegacyIndex,J)||!J->TryGetNumberField(TEXT("schema"),Schema)||Schema!=1.||
     !J->TryGetStringField(TEXT("project"),Owner)||Owner!=Project||!J->TryGetStringField(TEXT("round"),Id)||!IsRoundId(Id)||
     !J->TryGetStringField(TEXT("signature"),Digest)||!IsDigest(Digest)||
     !ValidRound(Root/TEXT("Rounds")/Id,Project,Id,Digest,1.,FString()))
  {Error=TEXT("Legacy iteration metadata is unreadable; preserved without fallback or replacement.");return false;}
 }
 const FString Index=Root/TEXT("Contexts")/Context/TEXT("CurrentRound.json");FString PreviousId,PreviousSignature;
 if(IFileManager::Get().FileExists(*Index))
 {
  TSharedPtr<FJsonObject> J;double Schema=0.;FString Owner,StoredContext;
  if(!Read(Index,J)||!J->TryGetNumberField(TEXT("schema"),Schema)||Schema!=2.||!J->TryGetStringField(TEXT("project"),Owner)||Owner!=Project||
     !J->TryGetStringField(TEXT("context"),StoredContext)||StoredContext!=Context||
     !J->TryGetStringField(TEXT("round"),PreviousId)||!IsRoundId(PreviousId)||
     !J->TryGetStringField(TEXT("signature"),PreviousSignature)||!IsDigest(PreviousSignature)||
     !ValidRound(Root/TEXT("Rounds")/PreviousId,Project,PreviousId,PreviousSignature,2.,Context))
  {Error=TEXT("Editor context index or manifest is unreadable; old rounds preserved, no fallback.");return false;}
 }
 FDWEditorIterationRound R;R.Signature=Current.Digest;R.ContextKey=Context;
 R.Id=CanReuse(PreviousSignature,Current.Digest)?PreviousId:FGuid::NewGuid().ToString(EGuidFormats::Digits);
 R.Directory=FPaths::ConvertRelativePathToFull(Root/TEXT("Rounds")/R.Id);R.StorageIdentity=Project+TEXT(":")+R.Id+TEXT(":")+R.Signature;
 if(R.Id!=PreviousId)
 {
  if(IFileManager::Get().DirectoryExists(*R.Directory)||!IFileManager::Get().MakeDirectory(*R.Directory,true))
  {Error=TEXT("Cannot allocate a fresh editor round directory.");return false;}
  auto Round=MakeShared<FJsonObject>();Round->SetNumberField(TEXT("schema"),2.);Round->SetStringField(TEXT("project"),Project);
  Round->SetStringField(TEXT("context"),Context);Round->SetStringField(TEXT("round"),R.Id);Round->SetStringField(TEXT("signature"),R.Signature);
  Round->SetStringField(TEXT("previous_round"),PreviousId);Round->SetStringField(TEXT("created_utc"),FDateTime::UtcNow().ToIso8601());
  Round->SetStringField(TEXT("authoring_manifest"),Current.ManifestJson);
  if(!Write(R.Directory/TEXT("RoundManifest.json"),Round)){Error=TEXT("Cannot write editor round manifest; earlier rounds preserved.");return false;}
  auto NewIndex=MakeShared<FJsonObject>();NewIndex->SetNumberField(TEXT("schema"),2.);NewIndex->SetStringField(TEXT("project"),Project);
  NewIndex->SetStringField(TEXT("context"),Context);NewIndex->SetStringField(TEXT("round"),R.Id);NewIndex->SetStringField(TEXT("signature"),R.Signature);
  if(!Write(Index,NewIndex)){Error=TEXT("Cannot commit editor context index; earlier rounds preserved.");return false;}
 }
 Out=MoveTemp(R);Error.Reset();return true;
}
