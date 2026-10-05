#if WITH_DEV_AUTOMATION_TESTS
#include "DWSaveStorage.h"
#include "DWSaveGame.h"
#include "DWPlayerCharacter.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWResourceNode.h"
#include "DWEnemyNest.h"
#include "DWEnemyCharacter.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSaveStorageTest, "DoughWorld.SaveFix.DocumentsAndAtomicWrite", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWSaveStorageTest::RunTest(const FString& Parameters)
{
    TestTrue(TEXT("Chinese folder accepted"), DWSaveStorage::IsValidFolderName(TEXT("面团世界")));
    for (const FString Invalid : {TEXT("../bad"), TEXT("C:\\bad"), TEXT("CON"), TEXT("NUL.sav"), TEXT("COM1"), TEXT("name."), TEXT("")})
        TestFalse(TEXT("Reject path escape or invalid Windows folder: ") + Invalid, DWSaveStorage::IsValidFolderName(Invalid));
    TestEqual(TEXT("Resolve current user's redirected Documents"), DWSaveStorage::DocumentsDirectory(TEXT("DoughWorld")), FPaths::ConvertRelativePathToFull(FPaths::Combine(FPlatformProcess::UserDir(), TEXT("DoughWorld"))));
    const FString Directory = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Automation") / (TEXT("DW_Save_") + FGuid::NewGuid().ToString(EGuidFormats::Digits)) / TEXT("中文存档"));
    const FString Slot = TEXT("DW_QA_Atomic");
    const FString Path = Directory / (Slot + TEXT(".sav"));
    UDWSaveGame* Save = NewObject<UDWSaveGame>(); Save->DisplayName=TEXT("原始存档"); Save->Health=73.f; Save->Version=3;
    TestTrue(TEXT("Create directory and old-format save"), DWSaveStorage::Write(Directory, Slot, Save));
    auto* Old = Cast<UDWSaveGame>(DWSaveStorage::Read(Directory, Slot));
    if (!TestNotNull(TEXT("Read version 3"), Old)) return false;
    TestEqual(TEXT("Version retained"),Old->Version,3); TestEqual(TEXT("Old health retained"),Old->Health,73.f);
    TArray<uint8> Before,After; FFileHelper::LoadFileToArray(Before,*Path);
    TestTrue(TEXT("Protect isolated original for failure test"),FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path,true));
    Save->Health=41.f;
    TestFalse(TEXT("Failed replacement reports failure"),DWSaveStorage::Write(Directory,Slot,Save));
    FFileHelper::LoadFileToArray(After,*Path); TestTrue(TEXT("Failed write preserves previous bytes"),Before==After);
    FPlatformFileManager::Get().GetPlatformFile().SetReadOnly(*Path,false);
    TestTrue(TEXT("Successful atomic replacement"),DWSaveStorage::Write(Directory,Slot,Save));
    auto* New=Cast<UDWSaveGame>(DWSaveStorage::Read(Directory,Slot));
    if(TestNotNull(TEXT("Reload replacement"),New))TestEqual(TEXT("New health persisted"),New->Health,41.f);
    TestTrue(TEXT("Exists uses same directory"),DWSaveStorage::Exists(Directory,Slot));
    TestTrue(TEXT("Explicit deletion affects only isolated test slot"),DWSaveStorage::Delete(Directory,Slot));
    TestFalse(TEXT("Deleted slot no longer exists"),DWSaveStorage::Exists(Directory,Slot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDuplicateActorSaveTest, "DoughWorld.SaveFix.DuplicateActorsRoundTrip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWDuplicateActorSaveTest::RunTest(const FString& Parameters)
{
    UWorld::InitializationValues Init;
    Init.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false).CreateFXSystem(false).SetTransactional(false);
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false,NAME_None,nullptr,true,ERHIFeatureLevel::Num,&Init);
    if(!TestNotNull(TEXT("Isolated transient world"),World))return false;
    auto* Player=World->SpawnActor<ADWPlayerCharacter>();
    Player->GameplayConfig=NewObject<UDWGameplayConfig>(Player); Player->Inventory->InitializeInventory(Player->GameplayConfig);
    auto* A=World->SpawnActor<ADWResourceNode>();auto* B=World->SpawnActor<ADWResourceNode>();auto* Unique=World->SpawnActor<ADWResourceNode>();
    A->PersistentId=B->PersistentId=TEXT("Prototype_Flour");Unique->PersistentId=TEXT("UniqueWater");
    A->RemainingAmount=17;B->RemainingAmount=43;Unique->RemainingAmount=9;
    A->SetActorLocation(FVector(300,400,100));B->SetActorLocation(FVector(900,700,100));
    auto* NestA=World->SpawnActor<ADWEnemyNest>();auto* NestB=World->SpawnActor<ADWEnemyNest>();
    NestA->PersistentId=NestB->PersistentId=TEXT("CopiedNest");NestA->Health=211;NestB->Health=388;
    auto* Enemy=World->SpawnActor<ADWEnemyCharacter>();Enemy->SourceNest=NestB;Enemy->Health=62;
    auto* Save=NewObject<UDWSaveGame>();Player->CaptureSaveData(Save);
    TSet<FString> Keys;for(const auto& S:Save->WorldActors){TestFalse(TEXT("No duplicate record keys"),Keys.Contains(S.ActorId));Keys.Add(S.ActorId);}
    TestTrue(TEXT("Unambiguous legacy ID unchanged"),Keys.Contains(TEXT("Resource:UniqueWater")));
    A->RemainingAmount=B->RemainingAmount=1;NestA->Health=NestB->Health=2;
    Player->ApplySaveData(Save);
    TestEqual(TEXT("First copied resource restored separately"),A->RemainingAmount,17);
    TestEqual(TEXT("Second copied resource restored separately"),B->RemainingAmount,43);
    TestEqual(TEXT("First copied nest restored separately"),NestA->Health,211.f);
    TestEqual(TEXT("Second copied nest restored separately"),NestB->Health,388.f);
    int32 RestoredEnemies=0;for(TActorIterator<ADWEnemyCharacter> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&It->SourceNest){++RestoredEnemies;TestTrue(TEXT("Runtime enemy belongs to correct copied nest"),It->SourceNest.Get()==NestB);TestEqual(TEXT("Enemy health retained"),It->Health,62.f);}
    TestEqual(TEXT("One runtime enemy restored"),RestoredEnemies,1);
    // Removing one copy does not alter the other copy's path identity.
    B->Destroy();A->RemainingAmount=2;Player->ApplySaveData(Save);TestEqual(TEXT("Identity stable after copy removed"),A->RemainingAmount,17);
    // A genuine old-format unique record still restores by authored ID.
    for(auto& S:Save->WorldActors)if(S.ActorId==TEXT("Resource:UniqueWater"))S.ActorPath.Empty();
    Unique->RemainingAmount=1;Player->ApplySaveData(Save);TestEqual(TEXT("Legacy unique record still loads"),Unique->RemainingAmount,9);
    World->DestroyWorld(false);
    return true;
}
#endif
