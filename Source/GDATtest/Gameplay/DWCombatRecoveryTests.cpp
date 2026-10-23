#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatRecoveryData.h"
#include "DWCombatRecoverySubsystem.h"
#include "DWCombatRecoveryActors.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatTestWorld.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWCombatAuthorityTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWInventoryComponent.h"
#include "DWWeaponAnimationComponent.h"
#include "DWGameplayConfig.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "Engine/DamageEvents.h"
#include "Misc/AutomationTest.h"

namespace DWRecoveryTests
{
FDWItemStack Stack(FName Id,int32 Count){FDWItemStack S;S.ItemId=Id;S.Quantity=Count;return S;}
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe{NewObject<UDWInventoryTransactionTestProbe>()};
    UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;ADWCombatSafePoint* Safe=nullptr;bool Ready=false;FString Error;
    FFixture(bool Activate=true)
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D21Recovery"));if(!World)return;
        P=Spawn<ADWPlayerCharacter>(FVector(0,0,110));if(!P)return;
        auto* S=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*S,Error))return;
        S->bHasPlayerTransform=true;S->PlayerTransform=FTransform(FRotator::ZeroRotator,FVector(0,0,110));
        S->CombatPlayerResources.OwnedPermanentWeaponIds={TEXT("DWWeapon_Shotgun")};S->CombatPlayerResources.LoadedShotgunShells=2;
        S->CombatPlayerResources.CurrentEquipmentId=TEXT("DWWeapon_Shotgun");
        S->Inventory={Stack(TEXT("DWAmmo_BreadShell"),7),Stack(TEXT("Alcohol"),3),Stack(FDWCombatItemRegistry::MoldCoreId(),2),Stack(FDWCombatItemRegistry::YeastCoreId(),1),Stack(TEXT("Dough"),4)};
        if(!P->TryApplySaveData(S)||!R())return;
        if(Activate)
        {
            Safe=Spawn<ADWCombatSafePoint>(P->GetActorLocation());Safe->StablePointId=FGuid::NewGuid();Safe->ActivationRadiusCm=150.;
            if(!Safe->ActivateForPlayer(P))return;
        }
        P->Inventory->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);Ready=!GI->HasActiveSlot();
    }
    ~FFixture()
    {
        Probe->TestHook=nullptr;if(!World)return;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    template<class T>T* Spawn(FVector At=FVector::ZeroVector)
    {
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* A=World->SpawnActor<T>(T::StaticClass(),At,FRotator::ZeroRotator,Params);if(A&&!A->HasActorBegunPlay())A->DispatchBeginPlay();return A;
    }
    UDWCombatRecoverySubsystem* R()const{return UDWCombatRecoverySubsystem::Get(P);}
    UDWWeaponAnimationComponent* W()const{return P->WeaponAnimationComponent.Get();}
    void Kill(){P->TakeDamage(P->Health*2.f+1.f,FDamageEvent(),nullptr,nullptr);}
    FDWCombatRecoverySave Snapshot(){FDWCombatRecoverySave S;R()->CaptureSave(S,Error);return S;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRecoverySchemaTest,"DoughWorld.Combat.Recovery.StrictSchemaAndClassifiedDrops",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRecoverySchemaTest::RunTest(const FString&)
{
    using namespace DWRecoveryTests;FString E;FDWCombatRecoverySave S;
    TestFalse(TEXT("Missing schema is not a new recovery state"),DWCombatRecovery::Validate(S,E));
    S.Schema=1;S.PlayerId=FGuid::NewGuid();TestTrue(TEXT("Explicit fresh state"),DWCombatRecovery::Validate(S,E));
    TArray<FDWItemStack> K;TArray<FDWDeathBagStack> D;
    TestTrue(TEXT("Classify only approved combat stock"),DWCombatRecovery::Partition({Stack(TEXT("Flour"),3),Stack(TEXT("Dough"),2),Stack(TEXT("Yeast"),4),Stack(FDWCombatItemRegistry::YeastCoreId(),1),Stack(TEXT("DWAmmo_Spore"),8),Stack(TEXT("Alcohol"),2)},K,D));
    TestEqual(TEXT("Old materials remain"),K.Num(),3);TestEqual(TEXT("Core ammo and bottles drop"),D.Num(),3);
    FDWDeathBagSave B;B.Schema=1;B.BagId=FGuid::NewGuid();B.PlayerId=S.PlayerId;B.MapPackage=TEXT("/Game/QA/Recovery");B.Contents=D;
    S.Bags={B};S.bAwaitingRespawn=true;S.CurrentDeathBagId=B.BagId;TestTrue(TEXT("Valid pending death"),DWCombatRecovery::Validate(S,E));
    auto Bad=S;Bad.Bags[0].Contents[0].Remaining++;TestFalse(TEXT("Cannot restore more than originally dropped"),DWCombatRecovery::Validate(Bad,E));
    Bad=S;Bad.Bags.Add(B);TestFalse(TEXT("Duplicate bag id rejected"),DWCombatRecovery::Validate(Bad,E));
    Bad=S;Bad.Bags[0].bClaimed=true;TestFalse(TEXT("Claimed flag cannot hide leftover items"),DWCombatRecovery::Validate(Bad,E));
    Bad=S;Bad.SafePoint.bActive=true;TestFalse(TEXT("No invented safe point default"),DWCombatRecovery::Validate(Bad,E));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRecoveryDeathTest,"DoughWorld.Combat.Recovery.FatalFactsAndRespawnPreserveEquipment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRecoveryDeathTest::RunTest(const FString&)
{
    DWRecoveryTests::FFixture F;if(!TestTrue(TEXT("Isolated fixture ready"),F.Ready))return false;
    const auto Weapons=F.W()->GetCombatWeaponStateSnapshot();const auto Rev=F.W()->GetCombatWeaponResourceRevision();
    TestTrue(TEXT("Accepted shotgun windup creates an existing lock"),F.W()->PrimaryPressed(F.P->GetActorLocation()+FVector(1000,0,0)));
    F.W()->PublishAcceptedWeaponStartEffects();const double AttackLock=F.W()->GetRemainingAttackLockSeconds();
    int32 Notices=0;F.Probe->TestHook=[&]()
    {
        ++Notices;const auto S=F.Snapshot();TestTrue(TEXT("Death and bag facts precede inventory callback"),F.P->IsDead()&&S.bAwaitingRespawn&&S.Bags.Num()==1);
        TestEqual(TEXT("Already removed reserve before callback"),F.P->Inventory->CountItem(TEXT("DWAmmo_BreadShell")),0);
        TestFalse(TEXT("Reentrant death cannot duplicate a bag"),F.P->TakeDamage(5.f,FDamageEvent(),nullptr,nullptr)>0.f);
    };
    F.Kill();F.Probe->TestHook=nullptr;TestEqual(TEXT("One inventory death notice"),Notices,1);
    TestEqual(TEXT("Old Dough remains"),F.P->Inventory->CountItem(TEXT("Dough")),4);
    TestEqual(TEXT("Loaded shells remain dead"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,2);
    TestTrue(TEXT("Native controlled respawn"),F.P->RequestCombatRespawn());TestFalse(TEXT("Repeated respawn rejected"),F.P->RequestCombatRespawn());
    const auto After=F.W()->GetCombatWeaponStateSnapshot();TestEqual(TEXT("Loaded shells remain alive"),After.LoadedShotgunShells,2);
    TestEqual(TEXT("Equipment unchanged"),After.CurrentEquipmentId,Weapons.CurrentEquipmentId);TestEqual(TEXT("No weapon resource rewrite"),F.W()->GetCombatWeaponResourceRevision(),Rev);
    TestTrue(TEXT("Permanent ownership retained"),After.OwnedPermanentWeaponIds.Num()==Weapons.OwnedPermanentWeaponIds.Num()&&After.OwnedPermanentWeaponIds.Contains(TEXT("DWWeapon_Shotgun")));
    TestTrue(TEXT("Death and respawn do not refresh an existing attack lock"),F.W()->GetRemainingAttackLockSeconds()>=AttackLock&&AttackLock>0.);
    FDWCombatPlayerVitalsConfig C;TestTrue(TEXT("Read same rules"),FDWCombatPlayerVitalsConfig::Build(*F.GI->GetCombatRules(),C,F.Error));
    TestEqual(TEXT("Health from independent respawn ratio"),F.P->Health,DWCombatPlayerSave::NativeHealth(C.Vitals.HealthMax*C.Vitals.RespawnHealthFraction,C));
    TestEqual(TEXT("Water from independent respawn ratio"),F.P->GetCombatWater(),C.Vitals.WaterMax*C.Vitals.RespawnWaterFraction);
    TestTrue(TEXT("Normal form and zero remaining form"),F.P->GetCombatBaseForm()==EDWCombatBaseForm::Normal&&F.P->GetCombatFormRemaining()==0.);
    TestTrue(TEXT("Destination is the activated point"),F.P->GetActorLocation().Equals(F.Safe->GetActorLocation(),.01));
    TestEqual(TEXT("Reserve not restored from old save"),F.P->Inventory->CountItem(TEXT("DWAmmo_BreadShell")),0);
    TestFalse(TEXT("Live recovery snapshot cannot be replayed independently"),F.R()->RestoreBeforeBeginPlay(F.Snapshot(),F.Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRecoveryClaimTest,"DoughWorld.Combat.Recovery.PartialClaimRetainsRemainderAndTombstone",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRecoveryClaimTest::RunTest(const FString&)
{
    using namespace DWRecoveryTests;FFixture F;if(!TestTrue(TEXT("Fixture ready"),F.Ready))return false;F.Kill();const FGuid Id=F.Snapshot().CurrentDeathBagId;
    if(!TestTrue(TEXT("Respawn"),F.P->RequestCombatRespawn()))return false;F.R()->Tick(0.f);
    auto* Bag=F.R()->FindPhysicalBag(Id);if(!TestNotNull(TEXT("Native bag materialized"),Bag))return false;
    const int32 Limit=F.P->Inventory->GetEffectiveStackLimit(TEXT("DWAmmo_BreadShell"));TArray<FDWItemStack> Full;
    Full.Add(Stack(TEXT("DWAmmo_BreadShell"),Limit-1));for(int32 I=1;I<F.P->GetGameplayConfig()->MaxInventorySlots;++I)Full.Add(Stack(TEXT("Dough"),1));
    TestTrue(TEXT("Capacity fixture fills all slots"),F.P->Inventory->SetSlots(Full));TestTrue(TEXT("Only one unit fits"),Bag->TryCollect(F.P));
    FDWDeathBagSave Current;F.R()->FindBag(Id,Current);int32 Remaining=0;for(const auto& S:Current.Contents)Remaining+=S.Remaining;
    TestEqual(TEXT("Full bag does not swallow remaining stock"),Remaining,12);TestFalse(TEXT("No space no commit"),Bag->TryCollect(F.P));
    TestTrue(TEXT("Make room explicitly in fixture"),F.P->Inventory->SetSlots({}));TestTrue(TEXT("Collect remaining stock"),Bag->TryCollect(F.P));
    F.R()->FindBag(Id,Current);TestTrue(TEXT("Terminal claim persists"),Current.bClaimed&&DWCombatRecovery::IsEmpty(Current));
    F.R()->Tick(0.f);TestNull(TEXT("Claimed marker never respawns"),F.R()->FindPhysicalBag(Id));
    TestEqual(TEXT("Ammo remainder exactly six"),F.P->Inventory->CountItem(TEXT("DWAmmo_BreadShell")),6);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRecoveryNoPointTest,"DoughWorld.Combat.Recovery.NoImplicitSafePointAndEnemyFactsSurvive",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRecoveryNoPointTest::RunTest(const FString&)
{
    DWRecoveryTests::FFixture F(false);if(!TestTrue(TEXT("Fixture ready"),F.Ready))return false;
    auto* Alive=F.Spawn<ADWEnemyCharacter>(FVector(300,0,100));auto* Dead=F.Spawn<ADWEnemyCharacter>(FVector(600,0,100));
    Alive->CombatProfile=EDWEnemyCombatProfile::DoughElite;
    if(!TestTrue(TEXT("Formal elite initialized"),Alive->InitializeFormalCombat(*F.GI->GetCombatRules(),F.Error)))return false;
    Alive->Health=51.f;Alive->TakeDamage(1.f,FDamageEvent(),nullptr,F.P);Dead->Health=0.f;
    TestTrue(TEXT("Elite remembers player damage"),Alive->HasRememberedPlayerCombat());
    const auto Life=UDWCombatAuthoritySubsystem::Get(F.P)->CaptureActorLife(Alive);
    F.Kill();TestFalse(TEXT("No prior point means no fallback teleport"),F.P->RequestCombatRespawn());
    TestTrue(TEXT("Living enemy is directed home"),Alive->IsReturningForPlayerDeath());
    TestTrue(TEXT("Death does not erase E05 engagement memory"),Alive->HasRememberedPlayerCombat());
    TestEqual(TEXT("Living enemy health unchanged"),Alive->Health,50.f);TestEqual(TEXT("Dead enemy not resurrected"),Dead->Health,0.f);
    const auto After=UDWCombatAuthoritySubsystem::Get(F.P)->CaptureActorLife(Alive);TestTrue(TEXT("No enemy restore/life rewrite"),Life.Entity==After.Entity&&Life.Life==After.Life);
    TestTrue(TEXT("Death state stays pending"),F.Snapshot().bAwaitingRespawn);return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRecoveryReentryTest,"DoughWorld.Combat.Recovery.ClaimCallbackDeathCommitsOnceInFIFO",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRecoveryReentryTest::RunTest(const FString&)
{
    DWRecoveryTests::FFixture F;if(!TestTrue(TEXT("Fixture ready"),F.Ready))return false;
    F.Kill();const FGuid First=F.Snapshot().CurrentDeathBagId;if(!TestTrue(TEXT("Respawn"),F.P->RequestCombatRespawn()))return false;
    F.R()->Tick(0.f);auto* B=F.R()->FindPhysicalBag(First);if(!TestNotNull(TEXT("Bag ready"),B))return false;
    int32 Depth=0,MaxDepth=0,Calls=0;bool Killed=false;
    F.Probe->TestHook=[&]()
    {
        ++Depth;++Calls;MaxDepth=FMath::Max(MaxDepth,Depth);
        if(!Killed)
        {
            Killed=true;FDWDeathBagSave Claimed;F.R()->FindBag(First,Claimed);
            TestTrue(TEXT("Old claim facts precede callback"),Claimed.bClaimed);
            F.Kill();const auto S=F.Snapshot();TestTrue(TEXT("Nested fatal commits second bag synchronously"),S.bAwaitingRespawn&&S.Bags.Num()==2);
            TestEqual(TEXT("Nested fatal removed newly claimed ammo"),F.P->Inventory->CountItem(TEXT("DWAmmo_BreadShell")),0);
        }
        --Depth;
    };
    TestTrue(TEXT("Claim accepted once"),B->TryCollect(F.P));F.Probe->TestHook=nullptr;
    TestEqual(TEXT("Shared FIFO prevents recursive inventory notifications"),MaxDepth,1);TestEqual(TEXT("One claim and one death notice"),Calls,2);
    const auto S=F.Snapshot();FDWDeathBagSave Old;F.R()->FindBag(First,Old);TestTrue(TEXT("Old tombstone remains"),Old.bClaimed);
    FDWDeathBagSave New;TestTrue(TEXT("Second death has its own id"),S.CurrentDeathBagId!=First&&F.R()->FindBag(S.CurrentDeathBagId,New));
    int32 Ammo=0;for(const auto& Stack:New.Contents)if(Stack.ItemId==TEXT("DWAmmo_BreadShell"))Ammo+=Stack.Remaining;
    TestEqual(TEXT("Seven reserve shells exist only in new bag"),Ammo,7);return true;
}
#endif
