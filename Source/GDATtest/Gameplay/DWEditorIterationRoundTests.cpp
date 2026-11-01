#if WITH_DEV_AUTOMATION_TESTS
#include "DWEditorMapIterationSession.h"
#include "DWEditorAuthoringSignature.h"
#include "DWEditorIterationRoundStore.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
namespace
{
FDWEditorAuthoringSignature Fixture(const FString& Revision)
{
 FDWEditorAuthoringSignature S;const FString Key=TEXT("/Game/Fixture/Author"),Digest=Revision;
 const FString Canonical=TEXT("DW_EDITOR_AUTHORING_SHA1_V1\n")+Key+TEXT("\n")+Digest+TEXT("\n");
 FTCHARToUTF8 Utf8(*Canonical);uint8 Hash[20];FSHA1::HashBuffer(Utf8.Get(),Utf8.Length(),Hash);S.Digest=BytesToHex(Hash,20);
 auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("schema"),1.);J->SetStringField(TEXT("algorithm"),TEXT("sha1-auth-v1"));J->SetStringField(TEXT("signature"),S.Digest);
 auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("path"),Key);Row->SetStringField(TEXT("digest"),Digest);
 TArray<TSharedPtr<FJsonValue>> Rows;Rows.Add(MakeShared<FJsonValueObject>(Row));J->SetArrayField(TEXT("dependencies"),Rows);
 FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&S.ManifestJson));return S;
}
bool PutJson(const FString& Path,const TSharedRef<FJsonObject>& J)
{FString Text;return FJsonSerializer::Serialize(J,TJsonWriterFactory<>::Create(&Text))&&FFileHelper::SaveStringToFile(Text,*Path);}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEditorIterationPolicyTest,"DoughWorld.EditorIteration.RoundIsolation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEditorIterationPolicyTest::RunTest(const FString&)
{
 using namespace DWEditorIterationRoundStore;
 TestTrue(TEXT("PIE opt-in mode allowed"),FDWEditorMapIterationSession::ModeAllowed(true,true,false,false,true));
 TestFalse(TEXT("Packaged build cannot enter editor rounds"),FDWEditorMapIterationSession::ModeAllowed(false,true,false,false,true));
 TestFalse(TEXT("Standalone excluded"),FDWEditorMapIterationSession::ModeAllowed(true,false,false,false,true));
 TestFalse(TEXT("Commandlets excluded"),FDWEditorMapIterationSession::ModeAllowed(true,true,true,false,true));
 TestFalse(TEXT("Explicit QA retains storage"),FDWEditorMapIterationSession::ModeAllowed(true,true,false,true,true));
 TestFalse(TEXT("Disabled remains disabled"),FDWEditorMapIterationSession::ModeAllowed(true,true,false,false,false));
 FString Menu,Scene,Normalized,OtherConfig,Error;
 const FString Config=TEXT("/Game/Fixture/DA_Main.DA_Main");
 if(!TestTrue(TEXT("Menu context"),MakeContextKey(TEXT("/Game/Fixture/Menu"),Config,Menu,Error))||
    !TestTrue(TEXT("Scene context"),MakeContextKey(TEXT("/Game/Fixture/Scene"),Config,Scene,Error)))return false;
 TestTrue(TEXT("PIE prefix/case normalized"),MakeContextKey(TEXT("/Game/Fixture/UEDPIE_17_Menu"),Config.ToLower(),Normalized,Error)&&Normalized==Menu);
 TestTrue(TEXT("Exact config object identity matters"),MakeContextKey(TEXT("/Game/Fixture/Menu"),TEXT("/Game/Fixture/DA_Other.DA_Other"),OtherConfig,Error)&&OtherConfig!=Menu);
 TestFalse(TEXT("Temporary map rejected"),MakeContextKey(TEXT("/Temp/Untitled"),Config,Normalized,Error));
 TestFalse(TEXT("Config subobject rejected"),MakeContextKey(TEXT("/Game/Fixture/Menu"),Config+TEXT(":Sub"),Normalized,Error));
 const FString Root=FPaths::ConvertRelativePathToFull(FString(FPlatformProcess::UserTempDir())/TEXT("DWEditorIterationContextQA")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
 const auto A=Fixture(TEXT("1111111111111111111111111111111111111111"));
 const auto B=Fixture(TEXT("2222222222222222222222222222222222222222"));
 FDWEditorIterationRound First,Same,SceneRound,Back,Changed,Returned,SceneSameContent,Rejected;
 if(!TestTrue(TEXT("First context allocates"),Select(Root,TEXT("TestProject"),Menu,A,First,Error)))return false;
 const FString RetainedPath=First.Directory/TEXT("retained.fixture");
 TestTrue(TEXT("Fixture written only in isolated temp"),FFileHelper::SaveStringToFile(TEXT("retained exact bytes"),*RetainedPath));
 TestTrue(TEXT("Unchanged context/version reused"),Select(Root,TEXT("TestProject"),Menu,A,Same,Error)&&Same.Id==First.Id);
 TestTrue(TEXT("Other context selects own round"),Select(Root,TEXT("TestProject"),Scene,B,SceneRound,Error)&&SceneRound.Id!=First.Id);
 TestTrue(TEXT("Menu A / Scene B / Menu A reuses Menu"),Select(Root,TEXT("TestProject"),Menu,A,Back,Error)&&Back.Id==First.Id);
 TestTrue(TEXT("Same context new content starts fresh"),Select(Root,TEXT("TestProject"),Menu,B,Changed,Error)&&Changed.Id!=First.Id);
 TestTrue(TEXT("Same context A-B-A never silently rolls back"),Select(Root,TEXT("TestProject"),Menu,A,Returned,Error)&&Returned.Id!=First.Id&&Returned.Id!=Changed.Id);
 TestTrue(TEXT("Same digest in different context stays separate"),Select(Root,TEXT("TestProject"),Scene,A,SceneSameContent,Error)&&SceneSameContent.Id!=Returned.Id);
 FString Retained;TestTrue(TEXT("Old fixture preserved"),FFileHelper::LoadFileToString(Retained,*RetainedPath)&&Retained==TEXT("retained exact bytes"));
 TestTrue(TEXT("Storage provenance differs"),First.StorageIdentity!=Changed.StorageIdentity&&First.StorageIdentity!=Returned.StorageIdentity);
 TestFalse(TEXT("Round path injection rejected"),IsRoundId(TEXT("../../SaveGames")));
 TestFalse(TEXT("Context path injection rejected"),Select(Root,TEXT("TestProject"),TEXT("../SaveGames"),A,Rejected,Error));
 TestFalse(TEXT("Project mismatch rejects"),Select(Root,TEXT("OtherProject"),Menu,A,Rejected,Error));
 auto Forged=A;Forged.Digest=B.Digest;
 TestFalse(TEXT("Forged current canonical signature rejects"),Select(Root,TEXT("TestProject"),Menu,Forged,Rejected,Error));
 const FString SceneManifest=SceneSameContent.Directory/TEXT("RoundManifest.json");
 FString ManifestText;FFileHelper::LoadFileToString(ManifestText,*SceneManifest);
 TSharedPtr<FJsonObject> Manifest;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(ManifestText),Manifest);
 if(!TestTrue(TEXT("Fixture manifest parses"),Manifest.IsValid()))return false;
 Manifest->SetStringField(TEXT("context"),Menu);TestTrue(TEXT("Tamper isolated fixture"),PutJson(SceneManifest,Manifest.ToSharedRef()));
 TestFalse(TEXT("Cross-context manifest tampering rejects"),Select(Root,TEXT("TestProject"),Scene,A,Rejected,Error));
 const FString Index=Root/TEXT("Contexts")/Menu/TEXT("CurrentRound.json");
 TestTrue(TEXT("Break isolated index"),FFileHelper::SaveStringToFile(TEXT("broken index"),*Index));
 TestFalse(TEXT("Broken context index fails closed"),Select(Root,TEXT("TestProject"),Menu,A,Rejected,Error));
 TestTrue(TEXT("Original fixture remains after rejection"),IFileManager::Get().FileExists(*RetainedPath));
 // Legacy is a separate temp fixture. It must remain byte-identical and is NOT automatically adopted.
 const FString LegacyRoot=Root/TEXT("LegacyCase"),LegacyId=FGuid::NewGuid().ToString(EGuidFormats::Digits);
 const FString LegacyDir=LegacyRoot/TEXT("Rounds")/LegacyId;
 IFileManager::Get().MakeDirectory(*LegacyDir,true);
 auto Legacy=MakeShared<FJsonObject>();Legacy->SetNumberField(TEXT("schema"),1.);Legacy->SetStringField(TEXT("project"),TEXT("TestProject"));
 Legacy->SetStringField(TEXT("round"),LegacyId);Legacy->SetStringField(TEXT("signature"),A.Digest);
 auto LegacyManifest=MakeShared<FJsonObject>(*Legacy);LegacyManifest->SetStringField(TEXT("authoring_manifest"),A.ManifestJson);
 TestTrue(TEXT("Write legacy fixture manifest"),PutJson(LegacyDir/TEXT("RoundManifest.json"),LegacyManifest));
 const FString LegacyIndex=LegacyRoot/TEXT("CurrentRound.json");TestTrue(TEXT("Write legacy fixture index"),PutJson(LegacyIndex,Legacy));
 TArray<uint8> Before,After;FFileHelper::LoadFileToArray(Before,*LegacyIndex);
 TestTrue(TEXT("New context does not auto-adopt legacy"),Select(LegacyRoot,TEXT("TestProject"),Menu,A,Same,Error)&&Same.Id!=LegacyId);
 FFileHelper::LoadFileToArray(After,*LegacyIndex);TestTrue(TEXT("Legacy pointer bytes untouched"),Before==After);
 TestTrue(TEXT("Broken legacy fixture written"),FFileHelper::SaveStringToFile(TEXT("broken legacy"),*LegacyIndex));
 TestFalse(TEXT("Broken legacy metadata fails closed"),Select(LegacyRoot,TEXT("TestProject"),Menu,A,Rejected,Error));
 // Only temp JSON and .fixture; no USaveGame, World, actors, user slots, or cleanup of user paths.
 AddInfo(FString(TEXT("Isolated fixture root: "))+Root);return true;
}
#endif
