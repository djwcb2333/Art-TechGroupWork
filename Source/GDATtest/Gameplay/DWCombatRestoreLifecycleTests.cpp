#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWSaveGame.h"
#include "Components/CapsuleComponent.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRestoreTeleportLifecycleTest,"DoughWorld.Combat.PlayerResources.RestoreTeleportLifecycle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRestoreTeleportLifecycleTest::RunTest(const FString& Parameters)
{
    for(int32 Mode=0;Mode<2;++Mode)
    {
        TStrongObjectPtr<UDWGameInstance> GI;
        UWorld* World=DWCombatTestWorld::Create(GI,TEXT("DWRestoreLifecycle"));
        if(!TestNotNull(TEXT("Isolated real GI world"),World))return false;
        World->BeginPlay();
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* P=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Spawn);
        auto* Nest=World->SpawnActor<ADWEnemyNest>(ADWEnemyNest::StaticClass(),FVector(2000,0,500),FRotator::ZeroRotator,Spawn);
        Nest->PersistentId=TEXT("RestoreGuardNest");Nest->SetHealthForLoad(300.f);
        auto* Save=NewObject<UDWSaveGame>();P->CaptureSaveData(Save);
        Save->PlayerTransform.SetLocation(FVector(1000,0,500));
        for(auto& Row:Save->WorldActors)if(Row.ActorId.Contains(TEXT("RestoreGuardNest")))Row.Health=17.f;
        int32 TransformCalls=0,InventoryNotices=0;
        TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
        Probe->TestHook=[&](){++InventoryNotices;};
        P->Inventory->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
        const FDelegateHandle Handle=P->GetCapsuleComponent()->TransformUpdated.AddLambda(
            [&](USceneComponent* Component,EUpdateTransformFlags,ETeleportType)
            {
                if(TransformCalls||FMath::Abs(Component->GetComponentLocation().X-1000.)>1.)return;
                ++TransformCalls;
                if(Mode==0)P->Destroy();else P->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            });
        TestFalse(TEXT("Interrupted restore is not reported successful"),P->TryApplySaveData(Save));
        TestEqual(TEXT("Real teleport callback executed once"),TransformCalls,1);
        TestEqual(TEXT("Old restore stops before mutating the next world actor"),Nest->Health,300.f);
        TestEqual(TEXT("No stale restored-resource notification after EndPlay"),InventoryNotices,0);
        P->GetCapsuleComponent()->TransformUpdated.Remove(Handle);Probe->TestHook=nullptr;
        World->EndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    return true;
}
#endif
