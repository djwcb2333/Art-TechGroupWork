#if WITH_DEV_AUTOMATION_TESTS
#include "DWEditorMapIterationSession.h"
#include "DWEditorAuthoringSignature.h"
#include "DWEditorIterationRoundStore.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEditorIterationPolicyTest,"DoughWorld.EditorIteration.RoundIsolation",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWEditorIterationPolicyTest::RunTest(const FString&)
{
 using namespace DWEditorIterationRoundStore;
 TestTrue(TEXT("PIE opt-in mode allowed"),FDWEditorMapIterationSession::ModeAllowed(true,true,false,false,true));
 TestFalse(TEXT("Packaged build cannot enter editor rounds"),FDWEditorMapIterationSession::ModeAllowed(false,true,false,false,true));
 TestFalse(TEXT("Standalone is outside this PIE feature"),FDWEditorMapIterationSession::ModeAllowed(true,false,false,false,true));
 TestFalse(TEXT("Commandlets do not create iteration sessions"),FDWEditorMapIterationSession::ModeAllowed(true,true,true,false,true));
 TestFalse(TEXT("Explicit QA retains its own storage mode"),FDWEditorMapIterationSession::ModeAllowed(true,true,false,true,true));
 TestFalse(TEXT("Disabled switch stays disabled"),FDWEditorMapIterationSession::ModeAllowed(true,true,false,false,false));
 const FString Root=FPaths::ProjectSavedDir()/TEXT("Automation/EditorIterationPolicy")/FGuid::NewGuid().ToString(EGuidFormats::Digits);
 FDWEditorAuthoringSignature A;A.Digest=TEXT("SignatureA");A.ManifestJson=TEXT("{\"fixture\":true}");auto B=A;B.Digest=TEXT("SignatureB");
 FDWEditorIterationRound First,Same,Second,Third;FString Error;
 if(!TestTrue(TEXT("First signature allocates empty isolated round"),Select(Root,TEXT("TestProject"),A,First,Error)))return false;
 const FString Fixture=First.Directory/TEXT("retained.fixture");FFileHelper::SaveStringToFile(TEXT("retained exact bytes"),*Fixture);
 TestTrue(TEXT("Same signature selects same round"),Select(Root,TEXT("TestProject"),A,Same,Error)&&First.Id==Same.Id);
 TestTrue(TEXT("Changed signature starts another round"),Select(Root,TEXT("TestProject"),B,Second,Error)&&First.Id!=Second.Id);
 TestTrue(TEXT("A-B-A starts a new A round instead of an old checkpoint"),Select(Root,TEXT("TestProject"),A,Third,Error)&&Third.Id!=First.Id&&Third.Id!=Second.Id);
 FString Retained;TestTrue(TEXT("Previous round file remains untouched"),FFileHelper::LoadFileToString(Retained,*Fixture)&&Retained==TEXT("retained exact bytes"));
 TestTrue(TEXT("Different rounds have different storage provenance"),First.StorageIdentity!=Second.StorageIdentity&&First.StorageIdentity!=Third.StorageIdentity);
 TestFalse(TEXT("Path injection cannot be a round ID"),IsRoundId(TEXT("../../SaveGames")));
 FFileHelper::SaveStringToFile(TEXT("broken index"),*(Root/TEXT("CurrentRound.json")));
 FDWEditorIterationRound Rejected;TestFalse(TEXT("Broken index rejects instead of falling back or overwriting old slots"),Select(Root,TEXT("TestProject"),A,Rejected,Error));
 TestTrue(TEXT("Original round remains after rejection"),IFileManager::Get().FileExists(*Fixture));
 // Only isolated JSON/manifests and a .fixture were written. No user sav, world, actors or legacy import.
 return true;
}
#endif
