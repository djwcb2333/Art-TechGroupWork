#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatMinePersistence.h"
#include "DWCombatWorldObjectsSave.h"
#include "DWCombatMineSubsystem.h"
#include "DWCombatMine.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatPlayerSave.h"
#include "DWCombatTestWorld.h"
#include "DWPlayerCharacter.h"
#include "DWSaveGame.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include <limits>
namespace DWMineRestoreTests
{
struct FSample
{
    FDWCombatPlayerExtendedSave Player;FDWCombatAuthoritySave Authority;FDWCombatWorldObjectsSave World;
    FSample()
    {
        using namespace DWCombatCommit;FLedger L;L.SetWorldEpoch(5);L.SetLife({1,1});L.SetLife({10,1});L.SetLife({20,1});
        const auto Action=L.BeginAction({10,1});const auto Fee=L.AdmitResource({Action,1,EResourceKind::RootDeployment,1});L.Resolve(Fee.Receipt.Handle,EOutcome::Committed);
        const auto E1=L.ReleaseEmitter(Fee.Receipt.Handle,1),E2=L.ReleaseEmitter(Fee.Receipt.Handle,2);
        const auto Hit=L.AdmitDamage({FRootMineDamage{E1.Handle},{20,1}});L.Resolve(Hit.Receipt.Handle,EOutcome::Committed);L.RetireEmitter(E1.Handle);L.SetLife({20,2});
        Authority.Ledger=L.CaptureSnapshot();Authority.LastEntity=20;Authority.Actors.Add({TEXT("player:0"),{10,1},1,EDWCombatSavedActorKind::Player,false,false});Authority.Actors.Add({TEXT("enemy:test"),{20,2},1,EDWCombatSavedActorKind::Enemy,false,false});
        auto& S=Player;S.Schema=1;S.bRooted=true;S.PlayerEntity=10;S.PlayerLife=1;S.RootAction=Action.Id;S.RootReceipt=Fee.Receipt.Handle.Id;S.RootAnchor={0,0,100};
        S.Ring.bActive=true;S.Ring.RingId=FGuid::NewGuid();S.Ring.Anchor=S.RootAnchor;S.Ring.CooldownRemainingSeconds=7.;
        FDWMoldMineSave M1;M1.MineId=FGuid::NewGuid();M1.Ordinal=1;M1.Location={100,0,4.1};M1.bConsumed=true;S.Ring.Mines.Add(M1);
        auto M2=M1;M2.MineId=FGuid::NewGuid();M2.Ordinal=2;M2.Location={-100,0,4.1};M2.bConsumed=false;S.Ring.Mines.Add(M2);
        S.MineEmitters.Add({M1.MineId,E1.Handle.Id});S.MineEmitters.Add({M2.MineId,E2.Handle.Id});S.RingHitEntities={20};S.Ring.HitLegacyEnemies={TEXT("EnemyPersistent")};
        auto& C=S.MineConfig;C.Count=2;C.HitsPerTarget=1;C.RingRadiusCm=100.;C.TriggerRadiusCm=60.;C.DamageRadiusCm=150.;C.Damage=40.;C.KnockbackCm=80.;C.KnockbackSeconds=.2;C.CooldownSeconds=10.;C.GroundToleranceCm=40.;
        FDWCombatEnemyObjectSave E;E.Life={20,2};E.Profile=EDWEnemyCombatProfile::Legacy;E.PersistentId=TEXT("EnemyPersistent");World.Enemies.Add(E);
    }
};
struct FWorld
{
    TStrongObjectPtr<UDWGameInstance> GI;UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;FString Error;bool Ready=false;
    FWorld()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("D22MineRestore"));if(!World)return;World->BeginPlay();
        FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* Floor=World->SpawnActor<AActor>(AActor::StaticClass(),FVector(0,0,-10),FRotator::ZeroRotator,Spawn);
        if(!Floor)return;
        auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);Floor->AddInstanceComponent(Box);Box->SetBoxExtent(FVector(2000,2000,10));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Floor->SetActorLocation(FVector(0,0,-10));
        Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,100),FRotator::ZeroRotator,Spawn);if(!Player)return;
        auto* Save=NewObject<UDWSaveGame>();if(!DWCombatPlayerSave::MakeNew(*GI->GetCombatRules(),*Save,Error))return;
        FDWCombatPlayerVitalsConfig Config;if(!FDWCombatPlayerVitalsConfig::Build(*GI->GetCombatRules(),Config,Error))return;
        auto& V=Save->CombatPlayerResources.Vitals;V.BaseForm=EDWCombatBaseForm::Mold;V.FormRemainingSeconds=Config.FormDurationSeconds;V.Special=Config.Vitals.SpecialMax;V.FormTimeCarry=V.SpecialGrowthCarry=0.;
        Save->Transformation=static_cast<float>(V.Special);Save->bYeastForm=false;Save->PlayerTransform=FTransform(FRotator::ZeroRotator,FVector(0,0,Player->GetCapsuleComponent()->GetScaledCapsuleHalfHeight()+2.));Save->bHasPlayerTransform=true;
        if(!Player->TryApplySaveData(Save))return;Player->SetActorTickEnabled(false);Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
        Ready=Player->TryToggleFormalRoot();
    }
    ~FWorld(){if(!World)return;World->EndPlay(EEndPlayReason::EndPlayInEditor);GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);}
    int32 HiddenCount()const{int32 N=0;for(TActorIterator<ADWCombatMine> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&It->IsHidden())++N;return N;}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMinePersistValidateTest,"DoughWorld.Combat.Persistence.MinePaidMappingAndWholeRingHistory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMinePersistValidateTest::RunTest(const FString&)
{
    using namespace DWMineRestoreTests;FSample S;FString Error;
    if(!TestTrue(TEXT("Paid consumed/live mines with respawned target validate"),FDWCombatMinePersistence::ValidateSnapshot(S.Player,S.Authority,Error))){AddError(Error);return false;}
    TestTrue(TEXT("Entity to persistent enemy history agrees"),FDWCombatMinePersistence::ValidateTargetHistory(S.Player,S.World,Error));
    auto Bad=S.Player;Bad.Ring.Mines[0].bConsumed=false;TestFalse(TEXT("Consumed emitter cannot be revived"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));
    Bad=S.Player;Bad.MineEmitters[1].Emitter=Bad.MineEmitters[0].Emitter;TestFalse(TEXT("One emitter cannot create two mines"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));
    Bad=S.Player;Bad.RingHitEntities.Reset();Bad.Ring.HitLegacyEnemies.Reset();TestFalse(TEXT("Committed hit cannot be forgotten"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));
    Bad=S.Player;Bad.PlayerLife=2;TestFalse(TEXT("Root cannot re-sign a different owner life"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));
    Bad=S.Player;Bad.MineConfig.Damage=std::numeric_limits<double>::infinity();TestFalse(TEXT("Nonfinite released tuning rejects"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));
    Bad=S.Player;Bad.Ring.Mines[1].Ordinal=3;TestFalse(TEXT("Ordinal exceeds saved ring count"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));
    Bad=S.Player;Bad.RootReceipt=999;TestFalse(TEXT("Unpaid root rejects"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));
    Bad=S.Player;Bad.Ring.HitLegacyEnemies[0]=TEXT("OtherEnemy");TestFalse(TEXT("Historical target alias rejects"),FDWCombatMinePersistence::ValidateTargetHistory(Bad,S.World,Error));
    auto World=S.World;World.Enemies.Reset();TestFalse(TEXT("Missing target tombstone rejects"),FDWCombatMinePersistence::ValidateTargetHistory(S.Player,World,Error));
    Bad=S.Player;Bad.Ring.CooldownRemainingSeconds=100.;TestTrue(TEXT("Saved cooldown not clamped by present config"),FDWCombatMinePersistence::ValidateSnapshot(Bad,S.Authority,Error));return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMinePersistStageTest,"DoughWorld.Combat.Persistence.MineInertStagingCleanupAndStaleFacts",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMinePersistStageTest::RunTest(const FString&)
{
    DWMineRestoreTests::FWorld F;if(!TestTrue(TEXT("Real paid root in isolated world"),F.Ready))return false;
    auto* A=UDWCombatAuthoritySubsystem::Get(F.Player);auto* M=UDWCombatMineSubsystem::Get(F.Player);if(!A||!M)return false;
    const int32 Live=M->GetLiveMineCount(F.Player);if(!TestTrue(TEXT("Physical ring exists"),Live>0))return false;const double Special=F.Player->GetCombatSpecial();
    FDWCombatPlayerExtendedSave S;if(!TestTrue(TEXT("Capture existing ring"),FDWCombatMinePersistence::Capture(*F.Player,S,F.Error)))return false;
    const double Cooldown=S.Ring.CooldownRemainingSeconds;TArray<FDWCombatObjectBinding> Bindings{{TEXT("player:0"),F.Player}};
    FDWCombatEffectsSave Effects;if(!TestTrue(TEXT("Capture authority and paid emissions"),FDWCombatEffectsPersistence::Capture(*A,Bindings,Effects,F.Error)))return false;
    FDWCombatEffectsRestorePlan EffectsPlan;if(!TestTrue(TEXT("Prepare full authority candidate"),FDWCombatEffectsPersistence::PrepareRestore(*A,Effects,Bindings,Bindings,{S.RootAction},EffectsPlan,F.Error)))return false;
    FDWMoldRootIdentity Identity;if(!F.Player->GetFormalRootIdentity(Identity))return false;Identity.WorldEpoch=Identity.Action.Epoch=Identity.Receipt.Epoch=EffectsPlan.Authority().NewEpoch();++Identity.RootSerial;
    {
        FDWCombatMineRestorePlan Plan;if(!TestTrue(TEXT("Stage saved ring without another fee"),FDWCombatMinePersistence::Prepare(*F.Player,S,Effects.Authority,EffectsPlan.Authority(),Identity,Plan,F.Error))){AddError(F.Error);return false;}
        TestTrue(TEXT("Current full native fingerprint"),Plan.CanCommit(F.Error));TestEqual(TEXT("Each unconsumed mine staged hidden"),F.HiddenCount(),Live);
        TestEqual(TEXT("Existing live ring unchanged"),M->GetLiveMineCount(F.Player),Live);TestEqual(TEXT("No Special fee or refund"),F.Player->GetCombatSpecial(),Special);TestEqual(TEXT("Cooldown unchanged"),M->GetCooldownRemaining(F.Player),Cooldown);
    }
    TestEqual(TEXT("Abandoned candidate cleans only hidden staging"),F.HiddenCount(),0);TestEqual(TEXT("Old paid mines remain"),M->GetLiveMineCount(F.Player),Live);
    FDWCombatMineRestorePlan Plan;if(!TestTrue(TEXT("Prepare another candidate"),FDWCombatMinePersistence::Prepare(*F.Player,S,Effects.Authority,EffectsPlan.Authority(),Identity,Plan,F.Error)))return false;
    for(TActorIterator<ADWCombatMine> It(F.World);It;++It)if(!It->IsHidden()&&!It->IsActorBeingDestroyed()){It->Destroy();break;}
    TestFalse(TEXT("Existing mine destruction invalidates candidate"),Plan.CanCommit(F.Error));return true;
}
#endif
