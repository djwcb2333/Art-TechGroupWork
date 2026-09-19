#if WITH_DEV_AUTOMATION_TESTS
#include "DWDamageVolumeLibrary.h"
#include "DWAlcoholArea.h"
#include "DWEnemyNest.h"
#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace DWDamageVolumeTests
{
    struct FWorldScope
    {
        UWorld* World;
        FWorldScope()
        {
            World = UWorld::CreateWorld(EWorldType::Game, false, MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DWVolumeTest")), GetTransientPackage());
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
        }
        ~FWorldScope()
        {
            for (TActorIterator<AActor> It(World); It; ++It) if (It->HasActorBegunPlay()) It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
        }
        template<class T> T* Spawn(FVector P=FVector(0,0,500))
        {
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            return World->SpawnActor<T>(T::StaticClass(),P,FRotator::ZeroRotator,Params);
        }
        UDWDamageHitboxComponent* Box(AActor* Owner,FVector Center,FVector Extent)
        {
            auto* B=NewObject<UDWDamageHitboxComponent>(Owner); Owner->AddInstanceComponent(B);
            B->SetupAttachment(Owner->GetRootComponent()); B->SetBoxExtent(Extent); B->RegisterComponent(); B->SetWorldLocation(Center); B->SetWorldScale3D(FVector(1));
            return B;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDamageVolumesTest,"DoughWorld.Combat.DamageVolumeGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWDamageVolumesTest::RunTest(const FString& Parameters)
{
    using namespace DWDamageVolumeTests;
    FWorldScope S;
    auto* Nest=S.Spawn<ADWEnemyNest>(FVector(1000,0,500));
    auto* Area=S.Spawn<ADWAlcoholArea>();
    Nest->SetHealthForLoad(500.f);
    auto* B1=S.Box(Nest,FVector(600,0,500),FVector(450,30,30));
    auto* B2=S.Box(Nest,FVector(600,0,500),FVector(450,30,30));
    if (!Area->HasActorBegunPlay()) Area->DispatchBeginPlay();
    auto Targets=Area->CollectDamageTargets();
    TestTrue(TEXT("Large building edge hits despite origin 1000cm outside"),Targets.Contains(Nest));
    TestEqual(TEXT("Two hurtboxes return one target"),Targets.FilterByPredicate([Nest](AActor* A){return A==Nest;}).Num(),1);
    Area->ApplyAreaTick();
    TestEqual(TEXT("Two overlapping hurtboxes cost one damage tick"),Nest->Health,490.f);
    Area->ApplyAreaTick();
    TestEqual(TEXT("Next tick still applies damage"),Nest->Health,480.f);
    B2->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    B1->SetWorldLocation(FVector(800,0,500));
    TestFalse(TEXT("Nonintersecting volumes miss"),Area->CollectDamageTargets().Contains(Nest));
    Nest->SetActorLocation(FVector(0,0,500));
    B1->SetWorldLocation(FVector(800,0,500));
    TestFalse(TEXT("Origin inside does not count; decorative mesh is excluded"),Area->CollectDamageTargets().Contains(Nest));
    B1->SetWorldLocation(FVector(0,0,900));
    TestFalse(TEXT("Vertical gap does not hit elevated volume"),Area->CollectDamageTargets().Contains(Nest));
    B1->SetWorldLocation(FVector(400,0,500)); B1->SetBoxExtent(FVector(350,20,30));
    B1->SetWorldRotation(FRotator(0,90,0));
    TestFalse(TEXT("Rotated narrow volume misses"),Area->CollectDamageTargets().Contains(Nest));
    B1->SetWorldRotation(FRotator::ZeroRotator);
    TestTrue(TEXT("Final rotation is respected"),Area->CollectDamageTargets().Contains(Nest));
    B1->SetWorldScale3D(FVector(.1));
    TestFalse(TEXT("Final scaling is respected"),Area->CollectDamageTargets().Contains(Nest));
    B1->SetWorldScale3D(FVector(1)); B1->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestFalse(TEXT("Disabled explicit boxes never fall back to decorative mesh"),Area->CollectDamageTargets().Contains(Nest));
    B1->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    Nest->SetCanBeDamaged(false);
    TestFalse(TEXT("Can Be Damaged false opts out"),Area->CollectDamageTargets().Contains(Nest));
    Nest->SetCanBeDamaged(true);
    TestFalse(TEXT("Caller ignored actors are excluded"),UDWDamageVolumeLibrary::GetDamageTargetsInVolume(Area->DamageVolume,{Nest}).Contains(Nest));
    TestTrue(TEXT("Sphere reaches volume edge despite target origin elsewhere"),UDWDamageVolumeLibrary::IsTargetInDamageSphere(Nest,FVector(800,0,500),100));
    TestFalse(TEXT("Sphere outside final volume misses"),UDWDamageVolumeLibrary::IsTargetInDamageSphere(Nest,FVector(1000,0,500),100));
    auto* Enemy=S.Spawn<ADWEnemyCharacter>(FVector(800,0,500));
    Enemy->bRequireLineOfSight=false; Enemy->AttackRange=100;
    TestTrue(TEXT("Enemy melee uses collision volumes"),Enemy->CanAttackTarget(Nest));
    Enemy->SetActorLocation(FVector(1000,0,500));
    TestFalse(TEXT("Enemy melee rejects volume miss"),Enemy->CanAttackTarget(Nest));
    auto* Generic=S.Spawn<AActor>(FVector(0,0,500));
    auto* GBox=S.Box(Generic,FVector(0,0,500),FVector(20));
    TestTrue(TEXT("Tagged generic damage actor is supported without enemy cast"),Area->CollectDamageTargets().Contains(Generic));
    Generic->SetActorEnableCollision(false);
    TestFalse(TEXT("Disabled actor collision opts out"),Area->CollectDamageTargets().Contains(Generic));
    Nest->SetHealthForLoad(0);
    Area->ApplyAreaTick();
    TestEqual(TEXT("Dead nest never receives further health loss"),Nest->Health,0.f);
    TestFalse(TEXT("Player friendly fire remains off"),Area->CanAffectTarget(S.Spawn<ADWPlayerCharacter>(FVector(0,0,3000))));
    S.World->TimeSeconds+=10;
    Nest->SetHealthForLoad(500);
    Area->ApplyAreaTick();
    TestEqual(TEXT("Expired area does no damage"),Nest->Health,500.f);
    return true;
}
#endif
