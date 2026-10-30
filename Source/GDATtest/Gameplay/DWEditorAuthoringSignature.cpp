#include "DWEditorAuthoringSignature.h"
#include "DWGameplayConfig.h"
#if WITH_EDITOR
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
namespace
{
bool HashFile(const FString& File,FString& Hash)
{
 TUniquePtr<FArchive> Input(IFileManager::Get().CreateFileReader(*File));if(!Input)return false;
 FSHA1 Sha;TArray<uint8> Buffer;Buffer.SetNumUninitialized(1024*1024);
 while(Input->Tell()<Input->TotalSize())
 {const int64 N=FMath::Min<int64>(Buffer.Num(),Input->TotalSize()-Input->Tell());Input->Serialize(Buffer.GetData(),N);if(Input->IsError())return false;Sha.Update(Buffer.GetData(),uint32(N));}
 Sha.Final();uint8 Bytes[20];Sha.GetHash(Bytes);Hash=BytesToHex(Bytes,20);return true;
}
}
#endif
bool FDWEditorAuthoringSignature::Capture(const FString& Entry,const UDWGameplayConfig& Config,FDWEditorAuthoringSignature& Out,FString& Error,
 const FDWEditorAuthoringSignature* Cache,const TSet<FString>* Invalidated)
{
#if WITH_EDITOR
 TSet<FName> Pending,Seen;
 auto Add=[&](const FString& Package){if(FPackageName::IsValidLongPackageName(Package)&&!Package.StartsWith(TEXT("/Script/"))&&!Package.StartsWith(TEXT("/Engine/")))Pending.Add(FName(*Package));};
 Add(Entry);Add(Config.GetOutermost()->GetName());Add(Config.NewGameOpeningMap.ToSoftObjectPath().GetLongPackageName());Add(Config.GameplayMap);
 const TSet<FName> Roots=Pending;TArray<TSharedPtr<FJsonValue>> Excluded;
 if(Pending.Num()<2){Error=TEXT("Cannot identify saved authoring roots; 请保存地图后再试玩。");return false;}
 auto& Registry=FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
 if(Registry.IsLoadingAssets()){Error=TEXT("Asset registry is still loading; wait until ready, then 请保存地图后再试玩。");return false;}
 TMap<FString,FString> Files;
 while(!Pending.IsEmpty())
 {
  const FName Name=*Pending.CreateConstIterator();Pending.Remove(Name);if(Seen.Contains(Name))continue;Seen.Add(Name);
  if(Seen.Num()>5000){Error=TEXT("Authoring dependency limit exceeded; signature not complete. No formal save fallback.");return false;}
  const FString Package=Name.ToString();
  if(!Roots.Contains(Name))
  {
   TArray<FAssetData> Assets;Registry.GetAssetsByPackageName(Name,Assets,true);bool Include=false;
   for(const auto& Asset:Assets)
   {
    const FString Type=Asset.AssetClassPath.GetAssetName().ToString();
    Include|=Type==TEXT("World")||Type==TEXT("Blueprint")||Type==TEXT("DataTable")||Type==TEXT("DataAsset")||Type.StartsWith(TEXT("DW"));
   }
   if(!Include){Excluded.Add(MakeShared<FJsonValueString>(Package));continue;}
  }
  if(UPackage* Loaded=FindPackage(nullptr,*Package);Loaded&&Loaded->IsDirty())
  {Error=TEXT("请保存地图后再试玩：相关作者包尚未保存：")+Package;return false;}
  FString File;if(!FPackageName::DoesPackageExist(Package,&File)){Error=TEXT("请保存地图后再试玩：作者包不存在：")+Package;return false;}
  Files.Add(Package,File);
  TArray<FName> Dependencies;
  if(!Registry.GetDependencies(Name,Dependencies)){Error=TEXT("Authoring dependency registry incomplete: ")+Package;return false;}
  for(FName Dependency:Dependencies)Add(Dependency.ToString());
 }
 for(const TCHAR* Name:{TEXT("DWCombatRules.json"),TEXT("DWA01TrialDefaults.json"),TEXT("DWLanguageDefinitions.json")})
  Files.Add(FString(TEXT("rules:"))+Name,FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat")/Name);
 Files.Add(TEXT("config:DefaultGame.ini"),FPaths::ProjectConfigDir()/TEXT("DefaultGame.ini"));
 TArray<FString> Keys;Files.GetKeys(Keys);Keys.Sort();FString Canonical=TEXT("DW_EDITOR_AUTHORING_SHA1_V1\n");
 auto Manifest=MakeShared<FJsonObject>();Manifest->SetNumberField(TEXT("schema"),1.);Manifest->SetStringField(TEXT("algorithm"),TEXT("sha1-auth-v1"));
 TArray<TSharedPtr<FJsonValue>> Records;
 FDWEditorAuthoringSignature Result;
 for(const FString& Key:Keys)
 {
  const auto Stat=IFileManager::Get().GetStatData(*Files[Key]);if(!Stat.bIsValid){Error=TEXT("Missing authoring dependency: ")+Key;return false;}
  FDWEditorAuthoringFileDigest Record;Record.Filename=Files[Key];Record.Size=Stat.FileSize;Record.Modified=Stat.ModificationTime;
  const auto* Prior=Cache?Cache->Files.Find(Key):nullptr;
  const bool Force=Invalidated&&Invalidated->Contains(Key);
  if(Prior&&!Force&&Prior->Filename==Record.Filename&&Prior->Size==Record.Size&&Prior->Modified==Record.Modified)Record.Digest=Prior->Digest;
  else if(!HashFile(Record.Filename,Record.Digest)){Error=TEXT("Cannot read authoring dependency: ")+Key;return false;}
  Canonical+=Key+TEXT("\n")+Record.Digest+TEXT("\n");auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("path"),Key);Row->SetStringField(TEXT("digest"),Record.Digest);Records.Add(MakeShared<FJsonValueObject>(Row));Result.Files.Add(Key,MoveTemp(Record));
 }
 FTCHARToUTF8 Utf8(*Canonical);uint8 Bytes[20];FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Bytes);
 Result.Digest=BytesToHex(Bytes,20);Manifest->SetStringField(TEXT("signature"),Result.Digest);Manifest->SetArrayField(TEXT("dependencies"),Records);Manifest->SetArrayField(TEXT("excluded_visual_leaf_packages"),Excluded);
 Manifest->SetStringField(TEXT("coverage"),TEXT("Author map/config roots, direct blueprint/parent/data dependencies, runtime rules and admission config. Mesh/texture/material/animation/audio leaf bytes excluded; not a whole-Content signature."));
 if(!FJsonSerializer::Serialize(Manifest,TJsonWriterFactory<>::Create(&Result.ManifestJson))){Error=TEXT("Signature manifest serialization failed.");return false;}
 Out=MoveTemp(Result);Error.Reset();return true;
#else
 Error=TEXT("Editor iteration signatures are not included in packaged gameplay.");return false;
#endif
}
