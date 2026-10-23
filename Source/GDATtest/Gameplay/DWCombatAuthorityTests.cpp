#include "DWCombatAuthorityTestProbe.h"
void UDWCombatAuthorityTestProbe::Any(AActor* Actor,float Damage,const UDamageType*,AController* Instigator,AActor* Causer)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(AnyCallback)AnyCallback(Actor,Damage,Instigator,Causer);
#endif
}
void UDWCombatAuthorityTestProbe::Point(AActor* Actor,float Damage,AController* Instigator,FVector,UPrimitiveComponent*,FName,FVector,const UDamageType*,AActor* Causer)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(PointCallback)PointCallback(Actor,Damage,Instigator,Causer);
#endif
}
void UDWCombatAuthorityTestProbe::Radial(AActor* Actor,float Damage,const UDamageType*,FVector,const FHitResult&,AController* Instigator,AActor* Causer)
{
#if WITH_DEV_AUTOMATION_TESTS
    if(RadialCallback)RadialCallback(Actor,Damage,Instigator,Causer);
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatAuthoritySubsystem.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWDamageVolumeLibrary.h"
#include "DWSaveGame.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatUnarmed.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DamageEvents.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/DamageType.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "DWCombatTestWorld.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

namespace DWCombatAuthorityTests
{
using namespace DWCombatCommit;
struct FScope
{
    TStrongObjectPtr<UDWGameInstance> CombatTestGI;
        UWorld* World=nullptr;
    ADWPlayerCharacter* Player=nullptr;
    UDWCombatAuthoritySubsystem* Authority=nullptr;
    TStrongObjectPtr<UDWCombatAuthorityTestProbe> Probe{NewObject<UDWCombatAuthorityTestProbe>()};
    FScope()
    {
        World=DWCombatTestWorld::Create(CombatTestGI,TEXT("DWCombatAuthorityTests"));
            if(!World)return;
        Player=Spawn<ADWPlayerCharacter>(FVector(0,0,500));
        Player->bCombatRequireLineOfSight=false;Player->WeaponAnimationComponent->bEnabled=false;
        Authority=UDWCombatAuthoritySubsystem::Get(Player);
    }
    ~FScope()
    {
        Probe->AnyCallback=nullptr;Probe->PointCallback=nullptr;Probe->RadialCallback=nullptr;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        CombatTestGI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T> T* Spawn(FVector Location)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        T* Actor=World->SpawnActor<T>(T::StaticClass(),Location,FRotator::ZeroRotator,Params);
        if(Actor&&!Actor->HasActorBegunPlay())Actor->DispatchBeginPlay();
        return Actor;
    }
    ADWEnemyCharacter* Enemy(float X=120.f)
    {
        auto* E=Spawn<ADWEnemyCharacter>(FVector(X,0,500));E->Health=100.f;
        E->PersistentId=MakeUniqueObjectName(World,ADWEnemyCharacter::StaticClass(),TEXT("D05Enemy"));
        E->OnTakeAnyDamage.AddDynamic(Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
        return E;
    }
    FDWCombatDamageCommand Hit(AActor* Target,FActionId Action={},FId Packet=1,float Amount=10.f)
    {
        FDWCombatDamageCommand C;C.Source=Player;C.Target=Target;C.Amount=Amount;
        C.Identity={Action.Id?Action:Authority->BeginAction(Player),1,EActionDamageKind::Melee,Packet};
        C.TargetLife=Authority->CaptureActorLife(Target);return C;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityReentryTest,"DoughWorld.Combat.Authority.ReentrantFIFO",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityReentryTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;auto* Victim=F.Enemy();auto* Other=F.Enemy(240);
    if(!TestNotNull(TEXT("World authority"),F.Authority))return false;
    const auto Command=F.Hit(Victim);const auto Counter=F.Hit(Other);
    TArray<FString> Order;int32 Calls=0;int32 Depth=0,MaximumDepth=0;
    F.Probe->AnyCallback=[&](AActor* Target,float,AController*,AActor*)
    {
        ++Calls;++Depth;MaximumDepth=FMath::Max(MaximumDepth,Depth);
        if(Target==Victim&&Calls==1)
        {
            Order.Add(TEXT("first-enter"));
            TestEqual(TEXT("Facts committed before callback"),Victim->Health,90.f);
            const auto Duplicate=F.Authority->SubmitActionDamage(Command);
            TestTrue(TEXT("Same-key callback is duplicate"),Duplicate.Admission==EAdmission::Duplicate);
            TestTrue(TEXT("Receipt already committed before callback"),Duplicate.Receipt.State==EReceiptState::Committed);
            TestEqual(TEXT("Duplicate reports no fresh effect"),Duplicate.AppliedThisCall,0.f);
            auto SecondPacket=Command;SecondPacket.Identity.PacketOrdinal=2;SecondPacket.Amount=5.f;
            TestEqual(TEXT("Distinct same-target packet not swallowed by a global guard"),F.Authority->SubmitActionDamage(SecondPacket).AppliedThisCall,5.f);
            TestEqual(TEXT("Distinct hit commits synchronously"),F.Authority->SubmitActionDamage(Counter).AppliedThisCall,10.f);
            TestEqual(TEXT("Second HP already visible"),Other->Health,90.f);
            TestEqual(TEXT("Second callback waits for FIFO"),Calls,1);
            Order.Add(TEXT("first-exit"));
        }
        else Order.Add(Target==Victim?TEXT("same-target"):TEXT("second"));
        --Depth;
    };
    TestEqual(TEXT("Initial applied"),F.Authority->SubmitActionDamage(Command).AppliedThisCall,10.f);
    TestEqual(TEXT("Three real dynamic callbacks"),Calls,3);
    TestEqual(TEXT("No recursive notification pump"),MaximumDepth,1);
    TestEqual(TEXT("FIFO order"),FString::Join(Order,TEXT(",")),FString(TEXT("first-enter,first-exit,same-target,second")));
    TestEqual(TEXT("Finalized payloads released"),F.Authority->GetPendingDamageCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityPendingTest,"DoughWorld.Combat.Authority.PendingLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityPendingTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;auto* Victim=F.Enemy();int32 Calls=0;
    F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*){++Calls;};
    const auto C=F.Hit(Victim);const auto A=F.Authority->AdmitActionDamage(C);
    TestTrue(TEXT("Admitted pending snapshot"),A.Receipt.State==EReceiptState::Pending);
    F.Authority->EndAction(C.Identity.Action);F.Authority->NotifyActorDeath(F.Player);F.Player->Destroy();
    TestEqual(TEXT("Landed packet survives source exit and destruction"),F.Authority->ResolvePendingDamage(A.Receipt.Handle,EOutcome::Committed).AppliedThisCall,10.f);
    TestEqual(TEXT("Resolve cannot execute twice"),F.Authority->ResolvePendingDamage(A.Receipt.Handle,EOutcome::Committed).AppliedThisCall,0.f);
    TestEqual(TEXT("One actual callback"),Calls,1);
    TestEqual(TEXT("No finalized payload retained"),F.Authority->GetPendingDamageCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityRestoreTest,"DoughWorld.Combat.Authority.TargetRestoreAndInvalidHealth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityRestoreTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;auto* Victim=F.Enemy();int32 Calls=0;
    F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*){++Calls;};
    const auto Old=F.Hit(Victim);const auto A=F.Authority->AdmitActionDamage(Old);
    Victim->SetHealthForLoad(100);
    TestEqual(TEXT("Restore prunes old pending payload"),F.Authority->GetPendingDamageCount(),0);
    TestEqual(TEXT("Old life cannot resolve"),F.Authority->ResolvePendingDamage(A.Receipt.Handle,EOutcome::Committed).AppliedThisCall,0.f);
    TestTrue(TEXT("Stored request cannot retarget new life"),F.Authority->SubmitActionDamage(Old).Admission==EAdmission::Stale);
    TestEqual(TEXT("No fake callback on invalidation"),Calls,0);
    const auto B=F.Authority->AdmitActionDamage(F.Hit(Victim));
    Victim->Health=std::numeric_limits<float>::infinity();
    const auto Rejected=F.Authority->ResolvePendingDamage(B.Receipt.Handle,EOutcome::Committed);
    TestTrue(TEXT("Invalid live HP terminally rejects"),Rejected.Receipt.State==EReceiptState::Rejected);
    TestEqual(TEXT("Invalid-health snapshot released"),F.Authority->GetPendingDamageCount(),0);
    Victim->SetHealthForLoad(100);
    const auto DestroyPending=F.Authority->AdmitActionDamage(F.Hit(Victim));Victim->Destroy();
    TestEqual(TEXT("Destroy prunes pending payload"),F.Authority->GetPendingDamageCount(),0);
    TestEqual(TEXT("Destroyed target cannot resolve"),F.Authority->ResolvePendingDamage(DestroyPending.Receipt.Handle,EOutcome::Committed).AppliedThisCall,0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityDeathFactsTest,"DoughWorld.Combat.Authority.DeathFactsVisibleDuringNotification",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityDeathFactsTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;auto* Victim=F.Enemy();
    auto* Nest=F.Spawn<ADWEnemyNest>(FVector(500,0,500));Nest->PersistentId=TEXT("D05DeathNest");
    Nest->Health=5.f;Nest->bPlayerInRange=true;Nest->SpawnProgress=3.f;
    Nest->OnTakeAnyDamage.AddDynamic(F.Probe.Get(),&UDWCombatAuthorityTestProbe::Any);
    Victim->Health=5.f;Victim->AttackPhase=EDWCombatPhase::Active;
    int32 Calls=0;
    F.Probe->AnyCallback=[&](AActor* Target,float,AController*,AActor*)
    {
        ++Calls;
        if(Target==Victim)
        {
            TestTrue(TEXT("Enemy dead state precedes callback"),Victim->State==EDWEnemyState::Dead);
            TestTrue(TEXT("Enemy attack canceled before callback"),Victim->AttackPhase==EDWCombatPhase::None);
        }
        else {TestFalse(TEXT("Nest range fact committed"),Nest->bPlayerInRange);TestEqual(TEXT("Nest progress cleared"),Nest->SpawnProgress,0.f);}
        TStrongObjectPtr<UDWSaveGame> Capture{NewObject<UDWSaveGame>()};F.Player->CaptureSaveData(Capture.Get());
        const FName Id=Target==Victim?Victim->PersistentId:Nest->PersistentId;
        bool bFound=false;
        for(const auto& Saved:Capture->WorldActors)
            if(Saved.ActorId.EndsWith(Id.ToString())){bFound=true;TestEqual(TEXT("Saved HP already zero"),Saved.Health,0.f);TestTrue(TEXT("Saved destroyed fact already true"),Saved.bDestroyed);}
        TestTrue(TEXT("Real world save capture contains victim"),bFound);
    };
    TestEqual(TEXT("Enemy overkill capped"),F.Authority->SubmitActionDamage(F.Hit(Victim)).AppliedThisCall,5.f);
    TestEqual(TEXT("Nest overkill capped"),F.Authority->SubmitActionDamage(F.Hit(Nest)).AppliedThisCall,5.f);
    TestEqual(TEXT("Both actual callbacks ran"),Calls,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityLegacyEventsTest,"DoughWorld.Combat.Authority.LegacyPointRadialAny",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityLegacyEventsTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;auto* Victim=F.Enemy();
    auto* Controller=F.Spawn<APlayerController>(FVector::ZeroVector);
    Victim->OnTakePointDamage.AddDynamic(F.Probe.Get(),&UDWCombatAuthorityTestProbe::Point);
    Victim->OnTakeRadialDamage.AddDynamic(F.Probe.Get(),&UDWCombatAuthorityTestProbe::Radial);
    TArray<FString> Order;int32 AnyCalls=0;
    F.Probe->PointCallback=[&](AActor*,float Damage,AController* I,AActor* C){Order.Add(TEXT("point"));TestEqual(TEXT("Point value"),Damage,10.f);TestTrue(TEXT("Point identity preserved"),I==Controller&&C==F.Player);};
    F.Probe->RadialCallback=[&](AActor*,float Damage,AController* I,AActor* C){Order.Add(TEXT("radial"));TestEqual(TEXT("Radial notification falloff preserved"),Damage,6.f);TestTrue(TEXT("Radial identity preserved"),I==Controller&&C==F.Player);};
    F.Probe->AnyCallback=[&](AActor*,float,AController* I,AActor* C){++AnyCalls;Order.Add(TEXT("any"));TestTrue(TEXT("Any identity preserved"),I==Controller&&C==F.Player);};
    FHitResult Hit;Hit.Component=Victim->GetCapsuleComponent();Hit.ImpactPoint=FVector(50,0,0);
    FPointDamageEvent Point(10.f,Hit,FVector::ForwardVector,UDamageType::StaticClass());
    TestEqual(TEXT("Legacy point core once"),Victim->TakeDamage(10.f,Point,Controller,F.Player),10.f);
    FRadialDamageEvent Radial;Radial.DamageTypeClass=UDamageType::StaticClass();Radial.Origin=FVector::ZeroVector;
    Radial.Params=FRadialDamageParams(10.f,2.f,0.f,100.f,1.f);Radial.ComponentHits.Add(Hit);
    TestEqual(TEXT("Legacy radial retains old HP calculation"),Victim->TakeDamage(10.f,Radial,Controller,F.Player),10.f);
    TestEqual(TEXT("Only one core application per compatibility call"),Victim->Health,80.f);
    TestEqual(TEXT("Real generic callbacks counted"),AnyCalls,2);
    TestEqual(TEXT("Point/Radial before Any"),FString::Join(Order,TEXT(",")),FString(TEXT("point,any,radial,any")));
    TestTrue(TEXT("Pawn LastHitBy preserved"),Victim->LastHitBy==Controller);
    FDamageEvent Generic(UDamageType::StaticClass());
    Victim->TakeDamage(1.f,Generic,Controller,F.Player);Victim->TakeDamage(1.f,Generic,Controller,F.Player);
    TestEqual(TEXT("Independent legacy calls not falsely deduplicated by same frame/value"),Victim->Health,78.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityWorldTest,"DoughWorld.Combat.Authority.WorldIdentityIsolation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityWorldTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope A;FScope B;
    const auto ActionA=A.Authority->BeginAction(A.Player);const auto ActionB=B.Authority->BeginAction(B.Player);
    TestTrue(TEXT("Concurrent worlds have distinct epochs"),A.Authority->GetWorldEpoch()!=B.Authority->GetWorldEpoch());
    auto* Victim=B.Enemy();auto Foreign=B.Hit(Victim,ActionA);
    TestEqual(TEXT("Foreign action cannot damage this world"),B.Authority->SubmitActionDamage(Foreign).AppliedThisCall,0.f);
    B.Authority->EndAction(ActionA);
    TestEqual(TEXT("Foreign EndAction cannot remove local same-number action"),B.Authority->SubmitActionDamage(B.Hit(Victim,ActionB)).AppliedThisCall,10.f);
    auto Cross=A.Hit(A.Enemy());Cross.Target=Victim;Cross.TargetLife=B.Authority->CaptureActorLife(Victim);
    TestEqual(TEXT("Cross-world target rejected"),A.Authority->SubmitActionDamage(Cross).AppliedThisCall,0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityRestoreCallbackTest,"DoughWorld.Combat.Authority.RestoreInsideNotification",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityRestoreCallbackTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;auto* Victim=F.Enemy();Victim->Health=5.f;int32 Calls=0;
    F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*){++Calls;Victim->SetHealthForLoad(100.f);};
    const auto Result=F.Authority->SubmitActionDamage(F.Hit(Victim));
    TestEqual(TEXT("Actual damage callback ran"),Calls,1);
    TestTrue(TEXT("Original receipt remains committed"),Result.Receipt.State==EReceiptState::Committed);
    TestEqual(TEXT("Restored HP not overwritten by old death finalization"),Victim->Health,100.f);
    TestTrue(TEXT("Restored actor remains alive and collidable"),Victim->IsAlive()&&Victim->GetActorEnableCollision());
    F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*){++Calls;Victim->Destroy();};
    TestEqual(TEXT("Destroy callback still reports committed damage"),F.Authority->SubmitActionDamage(F.Hit(Victim)).AppliedThisCall,10.f);
    TestEqual(TEXT("Destroy callback really ran"),Calls,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityBudgetFactsTest,"DoughWorld.Combat.Authority.NotificationBudgetKeepsFactsComplete",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityBudgetFactsTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;auto* Victim=F.Enemy();Victim->Health=1000.f;int32 Calls=0;
    const auto First=F.Hit(Victim,{},1,1.f);
    F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*)
    {
        if(++Calls!=1)return;
        for(uint64 Packet=2;Packet<=300;++Packet)
        {
            auto C=First;C.Identity.PacketOrdinal=Packet;C.Amount=Packet==300?2000.f:1.f;
            F.Authority->SubmitActionDamage(C);
        }
        TestEqual(TEXT("All core damage committed during callback"),Victim->Health,0.f);
        TestTrue(TEXT("Deferred final notice cannot defer death fact"),Victim->State==EDWEnemyState::Dead&&Victim->AttackPhase==EDWCombatPhase::None);
    };
    F.Authority->SubmitActionDamage(First);
    TestEqual(TEXT("Finite notification budget enforced"),Calls,256);
    TestTrue(TEXT("Death facts complete while notification queue remains"),Victim->State==EDWEnemyState::Dead);
    F.Authority->Tick(0.f);
    TestEqual(TEXT("Remaining FIFO drained on next authority tick"),Calls,300);
    TestEqual(TEXT("No pending payloads retained after core commits"),F.Authority->GetPendingDamageCount(),0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWAuthorityPlayerMeleeTest,"DoughWorld.Combat.Authority.LegacyPlayerMeleePath",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWAuthorityPlayerMeleeTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatAuthorityTests;FScope F;FDWCombatUnarmedTuning T;FString Error;
    if(!TestTrue(TEXT("Formal unarmed rules"),FDWCombatUnarmedTuning::Build(*F.CombatTestGI->GetCombatRules(),T,Error)))return false;
    // Preserve the public compatibility entry; the old Actor melee fields are retired.
    auto* C=F.Player->WeaponAnimationComponent.Get();C->bEnabled=true;
    F.Player->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
    auto* Victim=F.Enemy(static_cast<float>(T.Attack.RangeCm*.5));Victim->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);int32 Calls=0;
    auto* Box=NewObject<UDWDamageHitboxComponent>(Victim);Victim->AddInstanceComponent(Box);Box->SetupAttachment(Victim->GetRootComponent());
    Box->SetBoxExtent(FVector(10));Box->RegisterComponent();Box->SetWorldLocation(Victim->GetActorLocation());
    F.Player->bEnableMeleeCombo=false;F.Player->MeleeWindupSeconds=0.f;F.Player->MeleeDamage=999.f;
    const float Expected=100.f-static_cast<float>(T.Attack.Damage);
    F.Probe->AnyCallback=[&](AActor*,float,AController*,AActor*){++Calls;TestEqual(TEXT("Real callback sees committed formal HP"),Victim->Health,Expected);};
    TestTrue(TEXT("New character is explicitly empty-handed"),C->GetCurrentEquipmentId().IsNone());
    TestTrue(TEXT("Public melee entry starts formal unarmed"),F.Player->PerformMeleeAttack(Victim->GetActorLocation()));
    auto Advance=[&](double Dt){F.World->TimeSeconds+=static_cast<float>(Dt);C->AdvanceAnimation(Dt);};
    Advance(T.Attack.WindupSeconds);Advance(T.Attack.ActiveSeconds*.5);Advance(T.Attack.ActiveSeconds*.5);
    TestEqual(TEXT("Persistent formal melee window applies one packet"),Victim->Health,Expected);
    TestEqual(TEXT("One actual damage callback and no second legacy hit"),Calls,1);
    TestEqual(TEXT("Retired Actor melee serial is never started"),F.Player->AttackSerial,0);
    TestEqual(TEXT("Retired Actor phase stays inactive"),F.Player->AttackPhase,EDWCombatPhase::None);
    TestEqual(TEXT("Compatibility entry does not grant a permanent weapon"),C->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Num(),0);
    return true;
}
#endif
