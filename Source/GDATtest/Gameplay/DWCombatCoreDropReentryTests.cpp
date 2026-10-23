#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatCorePickup.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatRules.h"
#include "DWInventoryComponent.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "DWGameInstance.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"

namespace DWCoreDeathReentryTests
{
struct FWorld
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;
    FWorld()
    {
        if(!GEngine)return;const FString Command=FCommandLine::Get();
        const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D07_DeathReentry")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
        FCommandLine::Set(*FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D07_Reentry_ -DWSaveTestDirectory=\"%s\""),*Path));
        GI.Reset(NewObject<UDWGameInstance>(GEngine));GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWCoreDeathReentry")),GetTransientPackage());
        FCommandLine::Set(*Command);World=GI->GetWorld();
        if(World){FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));World->SetGameMode(URL);World->InitializeActorsForPlay(URL);World->BeginPlay();}
    }
    ~FWorld(){if(World){World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}}
    template<class T>T* Spawn(FVector P)
    {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;return World->SpawnActor<T>(T::StaticClass(),P,FRotator::ZeroRotator,S);}
};
// Modify an in-memory copy of the packaged numeric payload only. No file is written or gameplay test bypass added.
static bool GuaranteedMoldRules(TSharedPtr<const FDWCombatRules>& Rules,FString& Error)
{
    FString Json;if(!FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWCombatRules.json"))))return false;
    TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root)||!Root.IsValid())return false;
    const TArray<TSharedPtr<FJsonValue>>* Values=nullptr;if(!Root->TryGetArrayField(TEXT("parameters"),Values))return false;
    int32 Changed=0;for(const auto& Value:*Values)
    {const auto Row=Value->AsObject();if(Row&&Row->GetStringField(TEXT("id"))==TEXT("core.mold.drop_probability")){Row->SetNumberField(TEXT("value"),1.);++Changed;}}
    if(Changed!=1)return false;Json.Reset();if(!FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Json)))return false;
    return FDWCombatRules::LoadFromJson(Json,Rules,Error);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreDestroyedCorpseTest,"DoughWorld.Combat.CoreDrop.CommittedDropSurvivesDeathCallbackCorpseDestruction",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreDestroyedCorpseTest::RunTest(const FString&)
{
    using namespace DWCombatCommit;DWCoreDeathReentryTests::FWorld F;if(!TestNotNull(TEXT("Isolated begun world"),F.World))return false;
    TSharedPtr<const FDWCombatRules> Rules;FString Error;if(!TestTrue(TEXT("Isolated probability-one rules"),DWCoreDeathReentryTests::GuaranteedMoldRules(Rules,Error)))return false;
    auto* Player=F.Spawn<ADWPlayerCharacter>(FVector(1000,0,500));auto* Enemy=F.Spawn<ADWEnemyCharacter>(FVector(0,0,500));
    if(!TestNotNull(TEXT("Player"),Player)||!TestNotNull(TEXT("Enemy"),Enemy))return false;
    Enemy->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;if(!TestTrue(TEXT("Normal formal initialization using isolated numeric rules"),Enemy->InitializeFormalCombat(*Rules,Error)))return false;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);auto* Drops=UDWCombatCoreDropSubsystem::Get(Player);
    if(!TestNotNull(TEXT("Authority"),Authority)||!TestNotNull(TEXT("Drops"),Drops))return false;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe(NewObject<UDWCombatAuthorityTestProbe>());int32 Calls=0;FGuid DeathId,PickupId;FName ItemId;
    Probe->AnyCallback=[&](AActor* Actor,float,AController*,AActor*)
    {
        ++Calls;const auto S=Enemy->CaptureFormalCombatSave();DeathId=S.CoreDropDeathId;PickupId=S.PickupId;ItemId=S.CoreItemId;
        TestEqual(TEXT("Real death notification sees committed probability-one success"),S.DropDecision,EDWEnemyCoreDropDecision::SpawnPending);
        TestEqual(TEXT("Real death fact precedes callback"),Enemy->Health,0.f);
        TestTrue(TEXT("Callback really destroys corpse"),Actor->Destroy());
    };
    Enemy->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    FDWCombatDamageCommand C;C.Source=Player;C.Target=Enemy;C.TargetLife=Authority->CaptureActorLife(Enemy);C.Amount=100.f;
    C.Identity={Authority->BeginAction(Player),1,EActionDamageKind::Melee,1};
    TestEqual(TEXT("Real fatal receipt committed"),Authority->SubmitActionDamage(C).AppliedThisCall,100.f);TestEqual(TEXT("One real callback"),Calls,1);Probe->AnyCallback=nullptr;
    Drops->Tick(.016f);Drops->Tick(.016f);auto* Pickup=Drops->FindPhysicalPickup(PickupId);
    if(!TestNotNull(TEXT("Committed drop materializes after original corpse EndPlay"),Pickup))return false;
    int32 PhysicalCount=0;for(TActorIterator<ADWCombatCorePickup> It(F.World);It;++It)if(It->GetPickupId()==PickupId)++PhysicalCount;
    TestEqual(TEXT("Exactly one physical carrier after repeated subsystem Tick"),PhysicalCount,1);
    FDWCombatCoreDropSave Record;TestTrue(TEXT("Original committed death record remains"),Drops->FindDrop(DeathId,Record));
    Player->SetActorLocation(Pickup->GetActorLocation());const int32 Before=Player->GetInventory()->CountItem(ItemId);
    TestTrue(TEXT("One legal pickup after corpse cleanup"),Pickup->TryCollect(Player));
    TestEqual(TEXT("Inventory granted exactly committed count"),Player->GetInventory()->CountItem(ItemId),Before+Record.Count);
    Drops->Tick(.016f);TestNull(TEXT("Claimed identity never creates another actor"),Drops->FindPhysicalPickup(PickupId));
    TestTrue(TEXT("Claim retained"),Drops->FindDrop(DeathId,Record));TestEqual(TEXT("Claimed state"),Record.Decision,EDWEnemyCoreDropDecision::Claimed);
    auto* NoDamage=F.Spawn<ADWEnemyCharacter>(FVector(2000,0,500));NoDamage->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;
    TestTrue(TEXT("Second enemy uses same probability-one rules"),NoDamage->InitializeFormalCombat(*Rules,Error));NoDamage->Destroy();Drops->Tick(.016f);
    TArray<FDWCombatCoreDropSave> Saved;TestTrue(TEXT("Capture final records"),Drops->CaptureSave(Saved,Error));
    TestEqual(TEXT("No-damage Destroy minted zero extra records"),Saved.Num(),1);
    return true;
}
#endif
