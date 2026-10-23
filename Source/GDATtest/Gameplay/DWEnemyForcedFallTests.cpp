#if WITH_DEV_AUTOMATION_TESTS
#include "DWEnemyForcedFall.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatCoreDropSubsystem.h"
#include "DWCombatCorePickup.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWCombatOverlapTestProbe.h"
#include "DWCombatTestWorld.h"
#include "DWCombatRules.h"
#include "DWInventoryComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/DamageType.h"
#include "Engine/DamageEvents.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/AutomationTest.h"

namespace DWForcedFallTests
{
using namespace DWCombatCommit;
struct FWorld
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* W=nullptr;ADWPlayerCharacter* P=nullptr;
    TSharedPtr<const FDWCombatRules> Rules;FString Error;
    FWorld()
    {
        W=DWCombatTestWorld::Create(GI,TEXT("DWForcedFall"));if(!W)return;
        // A deterministic in-memory probability-one fixture; the packaged numeric source is never edited.
        FString Json;FFileHelper::LoadFileToString(Json,*(FPaths::ProjectContentDir()/TEXT("DoughWorld/Gameplay/Combat/DWCombatRules.json")));
        TSharedPtr<FJsonObject> Root;if(!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json),Root))return;
        int32 Changed=0;for(const auto& V:Root->GetArrayField(TEXT("parameters")))
        {auto R=V->AsObject();if(R->GetStringField(TEXT("id"))==TEXT("core.mold.drop_probability")){R->SetNumberField(TEXT("value"),1.);++Changed;}}
        if(Changed!=1)return;Json.Reset();FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Json));
        if(!FDWCombatRules::LoadFromJson(Json,Rules,Error))return;
        W->BeginPlay();P=Spawn<ADWPlayerCharacter>(FVector(-500,0,80));
        Box(FVector(-100,0,-10),FVector(100,200,10),false);
    }
    ~FWorld(){if(W){W->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}}
    bool Ready()const{return W&&Rules&&P&&P->HasCombatVitalsReady();}
    template<class T>T* Spawn(FVector Position)
    {FActorSpawnParameters S;S.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;return W->SpawnActor<T>(T::StaticClass(),Position,FRotator::ZeroRotator,S);}
    AActor* Box(FVector Position,FVector Extent,bool Trigger)
    {
        auto* A=Spawn<AActor>(Position);auto* B=NewObject<UBoxComponent>(A);A->AddInstanceComponent(B);A->SetRootComponent(B);
        B->SetBoxExtent(Extent);B->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
        B->SetCollisionObjectType(Trigger?ECC_WorldDynamic:ECC_WorldStatic);
        B->SetCollisionResponseToAllChannels(Trigger?ECR_Ignore:ECR_Block);
        if(Trigger)B->SetCollisionResponseToChannel(ECC_Pawn,ECR_Overlap);
        B->SetGenerateOverlapEvents(Trigger);B->RegisterComponent();A->SetActorLocation(Position);return A;
    }
    ADWEnemyCharacter* Enemy(FVector Position=FVector(-80,0,80))
    {
        auto* E=Spawn<ADWEnemyCharacter>(Position);E->CombatProfile=EDWEnemyCombatProfile::MoldGrunt;
        if(!E->InitializeFormalCombat(*Rules,Error))return nullptr;
        E->GetCharacterMovement()->SetMovementMode(MOVE_Walking);return E;
    }
    UDWCombatAuthoritySubsystem* A()const{return UDWCombatAuthoritySubsystem::Get(W);}
    UDWCombatCoreDropSubsystem* Drops()const{return UDWCombatCoreDropSubsystem::Get(W);}
    FDWCombatDamageResult Push(ADWEnemyCharacter* E,float Distance=300.f)
    {
        FDWCombatDamageCommand C;C.Identity={A()->BeginAction(P),1,EActionDamageKind::Melee,1};
        C.Source=P;C.Target=E;C.TargetLife=A()->CaptureActorLife(E);C.Amount=1.f;
        C.KnockbackDirection=FVector::ForwardVector;C.KnockbackDistance=Distance;C.KnockbackSeconds=.25f;
        auto R=A()->SubmitActionDamage(C);A()->EndAction(C.Identity.Action);return R;
    }
    void MoveOff(ADWEnemyCharacter* E){Push(E);E->Tick(.25f);}
    FDWCombatDamageResult Fatal(ADWEnemyCharacter* E,EDWEnemyEnvironmentFatal Reason=EDWEnemyEnvironmentFatal::FellOutOfWorld)
    {return A()->SubmitEnemyEnvironmentFatal(*E,Reason,*GetDefault<UDamageType>());}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWForcedFallFactsTest,"DoughWorld.Combat.ForcedFall.AcceptedMoveAndContinuousFallOnly",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWForcedFallFactsTest::RunTest(const FString&)
{
    FDWEnemyForcedFall P;const FGuid Stable=FGuid::NewGuid();
    P.Accept({3,7},{9,2},{12,4},Stable,3,1);
    TestFalse(TEXT("A committed push is not yet a fall"),P.HasCredit());
    P.BeginMove(true,FVector(0,0,80),FVector::ZeroVector);P.CompleteMove(false,false,FVector::ZeroVector);
    TestFalse(TEXT("Blocked/zero actual movement cannot claim departure"),P.HasCredit());
    P.BeginMove(true,FVector(0,0,80),FVector::ZeroVector);P.CompleteMove(true,false,FVector(25,0,0));
    TestTrue(TEXT("Actual supported to unsupported movement earns this continuous fall"),P.HasCredit());
    P.FinishDriver();TestTrue(TEXT("Motion ending in air does not erase the caused fall"),P.HasCredit());
    TestTrue(TEXT("Identity matches exact target life"),P.IsCurrent(3,{12,4},1));
    TestFalse(TEXT("Restored target life cannot reuse it"),P.IsCurrent(3,{12,5},1));
    P.Accept({}, {}, {12,4},Stable,3,2);TestFalse(TEXT("Accepted nonplayer replacement clears old credit"),P.HasCredit());
    P.Accept({3,8},{9,2},{12,4},Stable,3,3);P.BeginMove(false,FVector(200,0,80),FVector::ZeroVector);P.CompleteMove(true,false,FVector::ZeroVector);
    TestFalse(TEXT("Push beginning in an unrelated fall cannot mint ground departure"),P.HasCredit());
    P.FinishDriver();TestEqual(TEXT("Unused pending push expires with its driver"),P.Saved.Stage,EDWForcedFallStage::None);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWForcedFallSaveTest,"DoughWorld.Combat.ForcedFall.SnapshotProvenanceAndAtomicReject",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWForcedFallSaveTest::RunTest(const FString&)
{
    FDWEnemyForcedFall P;const FGuid Id=FGuid::NewGuid();P.Accept({4,10},{2,3},{8,1},Id,4,1);
    P.BeginMove(true,FVector(0,0,80),FVector::ZeroVector);P.CompleteMove(true,false,FVector(30,0,0));
    FDWEnemyForcedFall Restored;TestTrue(TEXT("Validated falling snapshot rebinds to new world life"),Restored.Restore(P.Saved,Id,9,{20,7},2,false));
    TestTrue(TEXT("New runtime identity current"),Restored.IsCurrent(9,{20,7},2));
    TestEqual(TEXT("Old committed receipt retained as historical evidence only"),Restored.Saved.ReceiptEpoch,uint64(4));
    TestEqual(TEXT("Player life not recaptured as current respawn"),Restored.Saved.PlayerLife,uint64(3));
    auto Bad=P.Saved;Bad.PlayerLife=0;
    TestFalse(TEXT("Incomplete provenance rejects atomically"),Restored.Restore(Bad,Id,9,{20,7},2,false));
    TestTrue(TEXT("Rejected blob preserved prior fall"),Restored.HasCredit());
    Bad=P.Saved;Bad.Stage=EDWForcedFallStage::Armed;
    TestFalse(TEXT("Armed restore cannot invent missing movement driver"),Restored.Restore(Bad,Id,9,{20,7},2,false));
    TestTrue(TEXT("Matching coordinated driver may restore armed provenance"),Restored.Restore(Bad,Id,9,{20,7},2,true));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWForcedFallFatalTest,"DoughWorld.Combat.ForcedFall.RealSweepSafeDropAndDeadSource",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWForcedFallFatalTest::RunTest(const FString&)
{
    DWForcedFallTests::FWorld F;if(!TestTrue(TEXT("Isolated real rules world"),F.Ready()))return false;
    auto* E=F.Enemy();if(!TestNotNull(TEXT("Formal enemy"),E))return false;
    const auto SourceLife=F.A()->CaptureActorLife(F.P);const auto Push=F.Push(E);
    TestEqual(TEXT("Actual one HP hit, not fake fall damage"),Push.AppliedThisCall,1.f);
    E->Tick(.25f);const auto Cause=E->CaptureForcedFallSave();
    TestEqual(TEXT("Real swept ledge departure tracked"),Cause.Stage,EDWForcedFallStage::Falling);
    TestEqual(TEXT("Exact accepted movement receipt"),Cause.ReceiptId,Push.Receipt.Handle.Id);
    TestTrue(TEXT("Drop anchor is on original ground surface"),FMath::Abs(Cause.LastStandablePoint.Z)<.1&&Cause.LastStandablePoint.X>=-200.&&Cause.LastStandablePoint.X<=.1);
    FDamageEvent Damage;F.P->TakeDamage(10000.f,Damage,nullptr,E);TestTrue(TEXT("Original source really died"),F.P->IsDead());
    E->SetActorLocation(FVector(220,0,-2000));int32 Calls=0;FGuid Death,PickupId;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe(NewObject<UDWCombatAuthorityTestProbe>());
    Probe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        ++Calls;const auto Saved=E->CaptureFormalCombatSave();Death=Saved.CoreDropDeathId;PickupId=Saved.PickupId;
        TestEqual(TEXT("Core draw already terminal before real death callback"),Saved.DropDecision,EDWEnemyCoreDropDecision::SpawnPending);
        TestEqual(TEXT("Credit is immutable dead source life"),Saved.CreditedPlayerLife,SourceLife.Life);
        E->Destroy();
    };
    E->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);E->FellOutOfWorld(*GetDefault<UDamageType>());
    TestEqual(TEXT("One actual fatal notification"),Calls,1);Probe->AnyCallback=nullptr;
    F.Drops()->Tick(.016f);auto* Pickup=F.Drops()->FindPhysicalPickup(PickupId);
    if(!TestNotNull(TEXT("Physical core survives corpse callback destruction"),Pickup))return false;
    TestTrue(TEXT("Core materializes at last standable point, not below-world corpse"),Pickup->GetActorLocation().Equals(Cause.LastStandablePoint,.01));
    auto* Collector=F.Spawn<ADWPlayerCharacter>(Pickup->GetActorLocation());
    TestTrue(TEXT("Physical safe-position core can be collected once"),Pickup->TryCollect(Collector));
    FDWCombatCoreDropSave Record;TestTrue(TEXT("Death record retained"),F.Drops()->FindDrop(Death,Record));
    TestEqual(TEXT("Terminal drop retains the exact movement cause"),Record.ForcedFallReceiptId,Push.Receipt.Handle.Id);
    TestEqual(TEXT("Collected terminal state"),Record.Decision,EDWEnemyCoreDropDecision::Claimed);
    TestEqual(TEXT("One inventory addition"),Collector->GetInventory()->CountItem(Record.ItemId),Record.Count);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWForcedFallClearsTest,"DoughWorld.Combat.ForcedFall.LandingRestoreAndAcceptedReplacement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWForcedFallClearsTest::RunTest(const FString&)
{
    DWForcedFallTests::FWorld F;if(!F.Ready())return false;auto* E=F.Enemy();if(!E)return false;
    F.MoveOff(E);TestEqual(TEXT("Fixture made an actual fall"),E->CaptureForcedFallSave().Stage,EDWForcedFallStage::Falling);
    E->ApplyCombatKnockback(FVector::ForwardVector,1.f,.25f);
    TestEqual(TEXT("Rejected same-frame weaker push does not replace attribution"),E->CaptureForcedFallSave().Stage,EDWForcedFallStage::Falling);
    E->ApplyCombatKnockback(FVector::ForwardVector,600.f,.25f);
    TestEqual(TEXT("Accepted unattributed stronger push clears attribution"),E->CaptureForcedFallSave().Stage,EDWForcedFallStage::None);
    F.Fatal(E);TestEqual(TEXT("Environment after replacement is ineligible"),E->CaptureFormalCombatSave().DropDecision,EDWEnemyCoreDropDecision::Ineligible);
    auto* Landed=F.Enemy(FVector(-80,150,80));F.MoveOff(Landed);Landed->Landed(FHitResult());
    TestEqual(TEXT("Landing clears before external landing logic"),Landed->CaptureForcedFallSave().Stage,EDWForcedFallStage::None);
    F.Fatal(Landed);TestEqual(TEXT("Later environment death after landing is ineligible"),Landed->CaptureFormalCombatSave().DropDecision,EDWEnemyCoreDropDecision::Ineligible);
    auto* Restored=F.Enemy(FVector(-80,-150,80));F.MoveOff(Restored);Restored->SetHealthForLoad(50.f);
    TestEqual(TEXT("Live restore clears old-life cause"),Restored->CaptureForcedFallSave().Stage,EDWForcedFallStage::None);
    F.Fatal(Restored);TestEqual(TEXT("Restored enemy has no historic hit credit"),Restored->CaptureFormalCombatSave().DropDecision,EDWEnemyCoreDropDecision::Ineligible);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWForcedFallUnrelatedTest,"DoughWorld.Combat.ForcedFall.UnrelatedAirborneWallAndEnvironment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWForcedFallUnrelatedTest::RunTest(const FString&)
{
    DWForcedFallTests::FWorld F;if(!F.Ready())return false;
    auto* Air=F.Enemy(FVector(500,0,500));F.MoveOff(Air);
    TestEqual(TEXT("No supported start means no attributed fall"),Air->CaptureForcedFallSave().Stage,EDWForcedFallStage::None);
    F.Fatal(Air);TestEqual(TEXT("Pure environment no random core"),Air->CaptureFormalCombatSave().DropDecision,EDWEnemyCoreDropDecision::Ineligible);
    F.Box(FVector(-20,0,100),FVector(5,60,100),false);auto* Wall=F.Enemy();F.MoveOff(Wall);
    TestTrue(TEXT("Real blocking wall stops forced movement before ledge"),Wall->GetActorLocation().X<0.);
    TestEqual(TEXT("Wall stop did not create ground departure"),Wall->CaptureForcedFallSave().Stage,EDWForcedFallStage::None);
    auto* Other=F.Enemy(FVector(-80,150,80));F.MoveOff(Other);F.Fatal(Other,EDWEnemyEnvironmentFatal::OtherEnvironment);
    TestEqual(TEXT("Unrelated hazard during fall still cannot borrow fall credit"),Other->CaptureFormalCombatSave().DropDecision,EDWEnemyCoreDropDecision::Ineligible);
    const auto Revision=F.Drops()->GetRevision();F.Fatal(Other);TestEqual(TEXT("Repeated fatal cannot roll twice"),F.Drops()->GetRevision(),Revision);
    auto* Mutual=F.Enemy(FVector(-80,-150,80));F.MoveOff(Mutual);FDamageEvent Event;
    Mutual->TakeDamage(1000.f,Event,nullptr,Wall);
    TestEqual(TEXT("Enemy lethal hit during fall keeps its own nonplayer source"),Mutual->CaptureFormalCombatSave().DropDecision,EDWEnemyCoreDropDecision::Ineligible);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWForcedFallOverlapTest,"DoughWorld.Combat.ForcedFall.FatalDuringSweepAndRestoreCancel",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWForcedFallOverlapTest::RunTest(const FString&)
{
    DWForcedFallTests::FWorld F;if(!F.Ready())return false;
    auto* E=F.Enemy();E->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    auto* Trigger=F.Box(FVector(100,0,80),FVector(10,80,100),true);
    TStrongObjectPtr<UDWCombatOverlapTestProbe> Probe(NewObject<UDWCombatOverlapTestProbe>());int32 Calls=0;
    Probe->Callback=[&](AActor*,AActor* Other){if(Other==E){++Calls;F.Fatal(E,EDWEnemyEnvironmentFatal::FallImpact);}};
    Trigger->OnActorBeginOverlap.AddDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);F.MoveOff(E);
    TestEqual(TEXT("Real sweep invoked fatal volume exactly once"),Calls,1);
    TestEqual(TEXT("In-flight actual displacement earned credit before sweep returned"),E->CaptureFormalCombatSave().DropDecision,EDWEnemyCoreDropDecision::SpawnPending);
    TestFalse(TEXT("Old movement does not restart after fatal callback"),E->bIsCombatKnockedBack);
    Trigger->OnActorBeginOverlap.RemoveDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);
    auto* R=F.Enemy(FVector(-80,150,80));R->GetCapsuleComponent()->SetGenerateOverlapEvents(true);
    auto* Restore=F.Box(FVector(100,150,80),FVector(10,40,100),true);Calls=0;
    Probe->Callback=[&](AActor*,AActor* Other){if(Other==R){++Calls;R->SetHealthForLoad(37.f);}};
    Restore->OnActorBeginOverlap.AddDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);F.MoveOff(R);
    TestEqual(TEXT("Real swept restore callback happened"),Calls,1);TestEqual(TEXT("Restore health preserved"),R->Health,37.f);
    TestEqual(TEXT("Returning old sweep cannot create cause on restored life"),R->CaptureForcedFallSave().Stage,EDWForcedFallStage::None);
    Restore->OnActorBeginOverlap.RemoveDynamic(Probe.Get(),&UDWCombatOverlapTestProbe::Overlap);Probe->Callback=nullptr;
    auto* Authority=F.A();const auto Revision=F.Drops()->GetRevision();auto* Drops=F.Drops();F.W->bIsTearingDown=true;
    TestEqual(TEXT("World teardown rejects fatal minting"),Authority->SubmitEnemyEnvironmentFatal(*R,EDWEnemyEnvironmentFatal::FellOutOfWorld,*GetDefault<UDamageType>()).AppliedThisCall,0.f);
    F.W->bIsTearingDown=false;TestEqual(TEXT("No teardown ghost drop record"),Drops->GetRevision(),Revision);return true;
}
#endif
