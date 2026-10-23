#if WITH_DEV_AUTOMATION_TESTS
#include "DWWorldEvent.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatTestWorld.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWPlayerCharacter.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace DWWorldEventRestoreTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> FirstProbe{NewObject<UDWInventoryTransactionTestProbe>()};
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> SecondProbe{NewObject<UDWInventoryTransactionTestProbe>()};
    UWorld* World=nullptr;
    ADWPlayerCharacter* Player=nullptr;
    UDWCombatAuthoritySubsystem* Authority=nullptr;
    UDWWorldEventSubsystem* Events=nullptr;
    UDWWorldEventComponent* First=nullptr;
    UDWWorldEventComponent* Second=nullptr;
    FString Error;
    bool Ready=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D22WorldEventRestore"));if(!World)return;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,110),FRotator::ZeroRotator,Params);
        if(!Player)return;if(!Player->HasActorBegunPlay())Player->DispatchBeginPlay();
        Authority=UDWCombatAuthoritySubsystem::Get(Player);Events=World->GetSubsystem<UDWWorldEventSubsystem>();
        if(!Authority||!Events||!Player->HasCombatVitalsReady())return;
        if(!Authority->CaptureActorLife(Player).Entity)return;
        First=AddEvent(TEXT("D22Event_A"));Second=AddEvent(TEXT("D22Event_B"));
        if(!First||!Second||!First->IsReady()||!Second->IsReady())return;
        First->OnApplyState.AddDynamic(FirstProbe.Get(),&UDWInventoryTransactionTestProbe::OnWorldEventState);
        Second->OnApplyState.AddDynamic(SecondProbe.Get(),&UDWInventoryTransactionTestProbe::OnWorldEventState);
        Ready=!GI->HasActiveSlot();
    }
    UDWWorldEventComponent* AddEvent(FName Id)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Actor=World->SpawnActor<AActor>(AActor::StaticClass(),FVector::ZeroVector,FRotator::ZeroRotator,Params);
        if(!Actor)return nullptr;
        auto* Event=NewObject<UDWWorldEventComponent>(Actor);Actor->AddInstanceComponent(Event);
        Event->EventId=Id;Event->RegisterComponent();
        if(!Actor->HasActorBegunPlay())Actor->DispatchBeginPlay();
        return Event;
    }
    ~FFixture()
    {
        FirstProbe->WorldEventHook=nullptr;SecondProbe->WorldEventHook=nullptr;
        if(!World)return;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    bool Publish(FDWWorldEventRestorePlan& Plan,uint64 Epoch,DWCombatCommit::FEntityLife Life)
    {return Events->PublishCompletedFacts(Plan,Authority,Epoch,Player,Life,Error);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWorldEventsFactsTest,"DoughWorld.Combat.Persistence.WorldEvents.AllFactsPrecedeFirstDynamicNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWorldEventsFactsTest::RunTest(const FString&)
{
    DWWorldEventRestoreTests::FFixture F;if(!TestTrue(TEXT("Isolated registered events and player"),F.Ready))return false;
    if(!TestTrue(TEXT("First live event running"),F.First->RequestEvent())||
       !TestTrue(TEXT("Second live event running"),F.Second->RequestEvent()))return false;
    int32 FirstCalls=0,SecondCalls=0;
    F.FirstProbe->WorldEventHook=[&](bool Value)
    {
        ++FirstCalls;TestTrue(TEXT("First notice uses target completed value"),Value);
        TestTrue(TEXT("Completed ledger already adopted"),F.Events->IsCompleted(F.First->EventId));
        TestFalse(TEXT("Uncompleted ledger already adopted"),F.Events->IsCompleted(F.Second->EventId));
        TestTrue(TEXT("First component native completion adopted"),F.First->IsCompleted());
        TestFalse(TEXT("Second component native completion adopted before its notice"),F.Second->IsCompleted());
        TestFalse(TEXT("First running flag cleared before notice"),F.First->IsRunning());
        TestFalse(TEXT("Second running flag cleared before first notice"),F.Second->IsRunning());
        TestEqual(TEXT("Second notice has not run early"),SecondCalls,0);
    };
    F.SecondProbe->WorldEventHook=[&](bool Value){++SecondCalls;TestFalse(TEXT("Second notice preserves false final state"),Value);};
    FDWWorldEventRestorePlan Plan;
    if(!TestTrue(TEXT("Prepare target event set"),F.Events->PrepareCompletedFacts({F.First->EventId},Plan,F.Error)))return false;
    TestTrue(TEXT("Prepare leaves both running facts untouched"),F.First->IsRunning()&&F.Second->IsRunning());
    TestEqual(TEXT("Prepare publishes nothing"),FirstCalls+SecondCalls,0);
    if(!TestTrue(TEXT("Prepared current facts validate"),Plan.CanCommit(F.Error)))return false;
    F.Events->AdoptCompletedFacts(Plan);
    TestTrue(TEXT("Both target facts ready before publication"),F.First->IsCompleted()&&!F.Second->IsCompleted()&&!F.First->IsRunning()&&!F.Second->IsRunning());
    TestEqual(TEXT("Native adopt publishes nothing"),FirstCalls+SecondCalls,0);
    TestTrue(TEXT("Publish actual reflected delegates"),F.Publish(Plan,F.Authority->GetWorldEpoch(),F.Authority->FindRegisteredActorLife(F.Player)));
    TestEqual(TEXT("First event publishes once"),FirstCalls,1);TestEqual(TEXT("Second event publishes once"),SecondCalls,1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWorldEventsStaleTest,"DoughWorld.Combat.Persistence.WorldEvents.StalePreparePreservesLiveFacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWorldEventsStaleTest::RunTest(const FString&)
{
    DWWorldEventRestoreTests::FFixture F;if(!TestTrue(TEXT("Fixture"),F.Ready))return false;
    int32 Notices=0;F.FirstProbe->WorldEventHook=[&](bool){++Notices;};F.SecondProbe->WorldEventHook=[&](bool){++Notices;};
    FDWWorldEventRestorePlan Plan;
    if(!TestTrue(TEXT("Prepare completed candidate"),F.Events->PrepareCompletedFacts({F.First->EventId,F.Second->EventId},Plan,F.Error)))return false;
    if(!TestTrue(TEXT("Actual live request mutates running fact after prepare"),F.Second->RequestEvent()))return false;
    TestFalse(TEXT("Changed running fact rejects stale candidate"),Plan.CanCommit(F.Error));
    TestTrue(TEXT("Failure explains rejection"),!F.Error.IsEmpty());
    TestTrue(TEXT("Live running event retained"),F.Second->IsRunning());
    TestTrue(TEXT("Unrelated live component stays ready"),F.First->IsReady());
    TestFalse(TEXT("No first completion partially adopted"),F.First->IsCompleted());
    TestFalse(TEXT("No second completion partially adopted"),F.Second->IsCompleted());
    TestEqual(TEXT("Completed ledger unchanged"),F.Events->ExportCompleted().Num(),0);
    TestEqual(TEXT("Stale rejection emits no presentation notice"),Notices,0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWWorldEventsReentryTest,"DoughWorld.Combat.Persistence.WorldEvents.CallbackLifecycleStopsRemainingNoticesAndReplay",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWWorldEventsReentryTest::RunTest(const FString&)
{
    for(int32 Case=0;Case<2;++Case)
    {
        DWWorldEventRestoreTests::FFixture F;if(!TestTrue(TEXT("Isolated lifecycle fixture"),F.Ready))return false;
        int32 FirstCalls=0,SecondCalls=0;
        const uint64 Epoch=F.Authority->GetWorldEpoch();const auto Life=F.Authority->FindRegisteredActorLife(F.Player);
        F.FirstProbe->WorldEventHook=[&](bool)
        {
            ++FirstCalls;
            TestTrue(TEXT("Later component was adopted before destructive callback"),F.Second->IsCompleted());
            if(Case==0)TestTrue(TEXT("Real event owner destruction from callback"),F.First->GetOwner()->Destroy());
            else F.Authority->NotifyActorRestored(F.Player);
        };
        F.SecondProbe->WorldEventHook=[&](bool){++SecondCalls;};
        FDWWorldEventRestorePlan Plan;
        if(!TestTrue(TEXT("Prepare two completed events"),F.Events->PrepareCompletedFacts({F.First->EventId,F.Second->EventId},Plan,F.Error))||
           !TestTrue(TEXT("Candidate remains current"),Plan.CanCommit(F.Error)))return false;
        F.Events->AdoptCompletedFacts(Plan);
        TestFalse(FString::Printf(TEXT("Lifecycle callback %d stops old publication"),Case),F.Publish(Plan,Epoch,Life));
        TestEqual(TEXT("First callback occurred once"),FirstCalls,1);
        TestEqual(TEXT("Later object receives no stale notice"),SecondCalls,0);
        TestTrue(TEXT("Later object's already committed fact remains intact"),F.Second->IsCompleted());
        if(Case==1)TestTrue(TEXT("Actual authority life changed"),F.Authority->FindRegisteredActorLife(F.Player).Life>Life.Life);
        TestFalse(TEXT("Interrupted plan cannot replay already emitted notifications"),F.Publish(Plan,Epoch,Life));
        TestEqual(TEXT("Replay cannot notify first object again"),FirstCalls,1);
        TestEqual(TEXT("Replay cannot leak later notification"),SecondCalls,0);
    }
    return true;
}
#endif
