#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatCorePickup.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatRules.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWGameInstance.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DWCombatCoreTests
{
struct FWorld
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* W=nullptr;FString Error;
    FWorld()
    {
        if(!GEngine)return;const FString Command=FCommandLine::Get();
        const FString Path=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("Automation/D07_Core")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
        FCommandLine::Set(*FString::Printf(TEXT("-DWVerificationSavePrefix=DW_QA_D07_ -DWSaveTestDirectory=\"%s\""),*Path));
        GI.Reset(NewObject<UDWGameInstance>(GEngine));GI->InitializeStandalone(MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWCoreTest")),GetTransientPackage());
        FCommandLine::Set(*Command);W=GI->GetWorld();
        if(W){FURL URL;URL.AddOption(TEXT("game=/Script/Engine.GameModeBase"));W->SetGameMode(URL);W->InitializeActorsForPlay(URL);}
    }
    ~FWorld(){if(W){W->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}}
    UDWCombatCoreDropSubsystem* Drops()const{return UDWCombatCoreDropSubsystem::Get(W);}
    void Begin(){W->BeginPlay();}
    template<class T>T* Spawn(FVector P=FVector(0,0,500))
    {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;return W->SpawnActor<T>(T::StaticClass(),P,FRotator::ZeroRotator,S);}
    FDWCombatCoreDropSave Ground(EDWEnemyCombatProfile Profile,int32 Serial=1)
    {
        FDWCombatCoreDropSave S;S.DeathId=FGuid::NewGuid();S.StableEnemyId=FGuid::NewGuid();S.Profile=Profile;
        S.Decision=EDWEnemyCoreDropDecision::Spawned;S.FatalEpoch=777;S.FatalReceipt=Serial;S.EnemyEntity=100+Serial;S.EnemyLife=1;S.PlayerEntity=3;S.PlayerLife=1;
        S.PickupId=FGuid::NewGuid();S.ItemId=Profile==EDWEnemyCombatProfile::MoldGrunt?FDWCombatItemRegistry::MoldCoreId():FDWCombatItemRegistry::YeastCoreId();
        S.Count=1;S.Location=FVector(0,0,500);return S;
    }
    bool Restore(const TArray<FDWCombatCoreDropSave>& Saved)
    {return Drops()&&GI->GetCombatRules()&&Drops()->RestoreBeforeBeginPlay(Saved,*GI->GetCombatRules(),Error);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreFatalFactsTest,"DoughWorld.Combat.CoreDrop.FatalDecisionBeforeDamageNotification",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreFatalFactsTest::RunTest(const FString&)
{
    using namespace DWCombatCommit;DWCombatCoreTests::FWorld F;if(!TestNotNull(TEXT("Isolated world"),F.W))return false;F.Begin();
    auto* Player=F.Spawn<ADWPlayerCharacter>(FVector(1000,0,500));auto* Enemy=F.Spawn<ADWEnemyCharacter>();
    if(!TestNotNull(TEXT("Player"),Player)||!TestNotNull(TEXT("Enemy"),Enemy))return false;
    Enemy->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;if(!TestTrue(TEXT("Formal config"),Enemy->InitializeFormalCombat(*F.GI->GetCombatRules(),F.Error)))return false;
    auto* Authority=UDWCombatAuthoritySubsystem::Get(Player);int32 Calls=0;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe(NewObject<UDWCombatAuthorityTestProbe>());
    Probe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;const auto S=Enemy->CaptureFormalCombatSave();
        TestTrue(TEXT("Failure or success fixed before first callback"),S.DropDecision==EDWEnemyCoreDropDecision::Failed||S.DropDecision==EDWEnemyCoreDropDecision::SpawnPending);
        FDWCombatCoreDropSave D;TestTrue(TEXT("Death ID committed into world snapshot"),F.Drops()->FindDrop(S.CoreDropDeathId,D));
        TestEqual(TEXT("Record has exact original fatal receipt"),D.FatalReceipt,S.FatalReceiptId);
    };
    Enemy->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    FDWCombatDamageCommand C;C.Source=Player;C.Target=Enemy;C.TargetLife=Authority->CaptureActorLife(Enemy);C.Amount=100.f;
    C.Identity={Authority->BeginAction(Player),1,EActionDamageKind::Melee,1};const auto First=Authority->SubmitActionDamage(C);
    TestEqual(TEXT("Actual lethal health commit"),First.AppliedThisCall,100.f);TestEqual(TEXT("One actual delegate"),Calls,1);
    const auto Before=Enemy->CaptureFormalCombatSave();const uint64 Revision=F.Drops()->GetRevision();
    TestEqual(TEXT("Duplicate receipt never commits another death"),Authority->ResolvePendingDamage(First.Receipt.Handle,EOutcome::Committed).AppliedThisCall,0.f);
    TestEqual(TEXT("Duplicate causes no new draw/record"),F.Drops()->GetRevision(),Revision);
    TestTrue(TEXT("Death identity retained"),Enemy->CaptureFormalCombatSave().CoreDropDeathId==Before.CoreDropDeathId);
    Enemy->OnTakeAnyDamage.RemoveDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);Probe->AnyCallback=nullptr;return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreRestoreRadiusTest,"DoughWorld.Combat.CoreDrop.RestoreAndExactPickupRadius",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreRestoreRadiusTest::RunTest(const FString&)
{
    DWCombatCoreTests::FWorld F;if(!TestNotNull(TEXT("World"),F.W))return false;
    auto Mold=F.Ground(EDWEnemyCombatProfile::MoldGrunt),Yeast=F.Ground(EDWEnemyCombatProfile::YeastGrunt,2);Yeast.Location.X=1000.;
    auto Failed=F.Ground(EDWEnemyCombatProfile::MoldGrunt,3);Failed.Decision=EDWEnemyCoreDropDecision::Failed;Failed.PickupId={};Failed.ItemId=NAME_None;Failed.Count=0;
    if(!TestTrue(TEXT("Controlled cold-world restore"),F.Restore({Mold,Yeast,Failed})))return false;
    F.Begin();auto* Player=F.Spawn<ADWPlayerCharacter>(FVector(150.01,0,500));F.Drops()->Tick(.016f);
    auto* M=F.Drops()->FindPhysicalPickup(Mold.PickupId);auto* Y=F.Drops()->FindPhysicalPickup(Yeast.PickupId);
    if(!TestNotNull(TEXT("Actual mold placeholder actor"),M)||!TestNotNull(TEXT("Actual yeast placeholder actor"),Y))return false;
    TestFalse(TEXT("Outside 150 cm, no capsule-radius expansion"),M->TryCollect(Player));
    TestEqual(TEXT("No inventory cost or gain on rejection"),Player->GetInventory()->CountItem(Mold.ItemId),0);
    Player->SetActorLocation(FVector(150,0,500));M->Tick(.016f);
    TestEqual(TEXT("At exact rule radius, automatic native Tick collects one"),Player->GetInventory()->CountItem(Mold.ItemId),1);
    TestEqual(TEXT("Other core is a distinct item"),Player->GetInventory()->CountItem(Yeast.ItemId),0);
    TestFalse(TEXT("No timed expiration"),Y->GetLifeSpan()>0.f);
    FDWCombatCoreDropSave S;TestTrue(TEXT("Claim snapshot retained after physical destruction"),F.Drops()->FindDrop(Mold.DeathId,S));
    TestEqual(TEXT("Claimed terminal state"),S.Decision,EDWEnemyCoreDropDecision::Claimed);
    TestTrue(TEXT("Failure snapshot still present"),F.Drops()->FindDrop(Failed.DeathId,S));TestEqual(TEXT("Failure not rerolled"),S.Decision,EDWEnemyCoreDropDecision::Failed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreFullBagReentryTest,"DoughWorld.Combat.CoreDrop.FullBagRetainedAndDynamicReentryOnce",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreFullBagReentryTest::RunTest(const FString&)
{
    DWCombatCoreTests::FWorld F;if(!TestNotNull(TEXT("World"),F.W))return false;const auto Saved=F.Ground(EDWEnemyCombatProfile::MoldGrunt);
    if(!TestTrue(TEXT("Restore one ground core"),F.Restore({Saved})))return false;F.Begin();auto* Player=F.Spawn<ADWPlayerCharacter>();
    F.Drops()->Tick(.016f);auto* Pickup=F.Drops()->FindPhysicalPickup(Saved.PickupId);if(!TestNotNull(TEXT("Physical core"),Pickup))return false;
    TStrongObjectPtr<UDWGameplayConfig> Config(NewObject<UDWGameplayConfig>());Config->MaxInventorySlots=1;Config->MaxStackSize=99;Config->Items.Reset();
    for(const FName Id:{FDWCombatItemRegistry::MoldCoreId(),FDWCombatItemRegistry::YeastCoreId()}){FDWItemDefinition D;D.ItemId=Id;Config->Items.Add(D);}
    auto* Inventory=Player->GetInventory();Inventory->InitializeInventory(Config.Get());
    if(!TestTrue(TEXT("Use actual rules bridge for two core caps"),Inventory->TryApplyCombatStackPolicy(*F.GI->GetCombatRules(),F.Error)))return false;
    int32 Limit=0;F.GI->GetCombatRules()->TryGetCount(TEXT("core.stack_limit"),Limit,F.Error);
    TestTrue(TEXT("Fill one complete stack"),Inventory->TryAddItem(Saved.ItemId,Limit));const auto Revision=Inventory->GetInventoryRevision();
    TestFalse(TEXT("Full bag keeps physical item"),Pickup->TryCollect(Player));TestFalse(TEXT("Full bag has not claimed"),Pickup->IsClaimed());
    TestEqual(TEXT("Rejected pickup did not edit inventory"),Inventory->GetInventoryRevision(),Revision);
    TestTrue(TEXT("Legitimately free one item of capacity"),Inventory->TryRemoveItem(Saved.ItemId,1));int32 Calls=0;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    Probe->TestHook=[&]()
    {
        ++Calls;FDWCombatCoreDropSave D;TestTrue(TEXT("Listener sees persistent claim"),F.Drops()->FindDrop(Saved.DeathId,D));
        TestEqual(TEXT("Claim and inventory precede callback"),D.Decision,EDWEnemyCoreDropDecision::Claimed);
        TestEqual(TEXT("Listener sees complete added stack"),Inventory->CountItem(Saved.ItemId),Limit);
        TestFalse(TEXT("Synchronous dynamic listener cannot claim twice"),Pickup->TryCollect(Player));
    };
    Inventory->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Capacity now permits one atomic claim"),Pickup->TryCollect(Player));TestEqual(TEXT("One dynamic notification"),Calls,1);
    Inventory->OnChanged.RemoveDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);Probe->TestHook=nullptr;
    TArray<FDWCombatCoreDropSave> Snapshot;if(!TestTrue(TEXT("Capture claimed record"),F.Drops()->CaptureSave(Snapshot,F.Error)))return false;
    DWCombatCoreTests::FWorld Reload;TestTrue(TEXT("Claimed cold restore accepted without regrant"),Reload.Restore(Snapshot));Reload.Begin();Reload.Drops()->Tick(.1f);
    TestNull(TEXT("Claimed record creates no physical duplicate after load"),Reload.Drops()->FindPhysicalPickup(Saved.PickupId));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreRestoreValidationTest,"DoughWorld.Combat.CoreDrop.RestoreRejectsDuplicateAndLiveImports",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreRestoreValidationTest::RunTest(const FString&)
{
    DWCombatCoreTests::FWorld F;if(!TestNotNull(TEXT("World"),F.W))return false;auto A=F.Ground(EDWEnemyCombatProfile::MoldGrunt);
    auto B=F.Ground(EDWEnemyCombatProfile::YeastGrunt,2);B.PickupId=A.PickupId;
    TestFalse(TEXT("Duplicate stable pickup IDs reject whole snapshot"),F.Restore({A,B}));FDWCombatCoreDropSave Out;
    TestFalse(TEXT("Invalid import has no partial record"),F.Drops()->FindDrop(A.DeathId,Out));
    B=F.Ground(EDWEnemyCombatProfile::YeastGrunt,2);B.FatalReceipt=A.FatalReceipt;
    TestFalse(TEXT("One fatal receipt cannot create two death records"),F.Restore({A,B}));
    B=F.Ground(EDWEnemyCombatProfile::YeastGrunt,2);B.EnemyEntity=A.EnemyEntity;B.EnemyLife=A.EnemyLife;
    TestFalse(TEXT("One runtime enemy life cannot be imported under two stable IDs"),F.Restore({A,B}));
    B=F.Ground(EDWEnemyCombatProfile::YeastGrunt,2);B.Location.X=std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Nonfinite position fails atomically"),F.Restore({A,B}));
    TestTrue(TEXT("Valid full snapshot still accepts after rejection"),F.Restore({A}));F.Begin();
    TestFalse(TEXT("No independent live-world restore behind inventory authority"),F.Restore({}));
    TestTrue(TEXT("Rejected live import preserved original record"),F.Drops()->FindDrop(A.DeathId,Out));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCoreEndPlayTest,"DoughWorld.Combat.CoreDrop.EndPlayAndTeardownNeverCreateGhostDrops",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCoreEndPlayTest::RunTest(const FString&)
{
    DWCombatCoreTests::FWorld F;if(!TestNotNull(TEXT("World"),F.W))return false;const auto A=F.Ground(EDWEnemyCombatProfile::MoldGrunt);
    if(!TestTrue(TEXT("Restore"),F.Restore({A})))return false;F.Begin();F.Drops()->Tick(.016f);
    auto* Actor=F.Drops()->FindPhysicalPickup(A.PickupId);if(!TestNotNull(TEXT("Actual actor"),Actor))return false;
    Actor->Destroy();F.Drops()->Tick(.016f);TestNull(TEXT("Unexpected actor destruction does not mint replacement"),F.Drops()->FindPhysicalPickup(A.PickupId));
    FDWCombatCoreDropSave Out;TestTrue(TEXT("Unclaimed record retained for explicit world save/load"),F.Drops()->FindDrop(A.DeathId,Out));
    TestEqual(TEXT("Destruction did not count as claim"),Out.Decision,EDWEnemyCoreDropDecision::Spawned);
    auto* Enemy=F.Spawn<ADWEnemyCharacter>();Enemy->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;
    TestTrue(TEXT("Formal enemy"),Enemy->InitializeFormalCombat(*F.GI->GetCombatRules(),F.Error));Enemy->Destroy();
    TArray<FDWCombatCoreDropSave> Records;TestTrue(TEXT("Snapshot"),F.Drops()->CaptureSave(Records,F.Error));
    TestEqual(TEXT("Enemy EndPlay is not an eligible death"),Records.Num(),1);
    auto* Drops=F.Drops();F.W->bIsTearingDown=true;Drops->Tick(.1f);TestNull(TEXT("No new physical actor during world teardown"),Drops->FindPhysicalPickup(A.PickupId));F.W->bIsTearingDown=false;
    return true;
}
#endif
