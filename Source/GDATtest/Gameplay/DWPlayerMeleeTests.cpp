#if WITH_DEV_AUTOMATION_TESTS
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWCombatUnarmed.h"
#include "DWPlayerController.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWDamageVolumeLibrary.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"
#include "DWCombatTestWorld.h"
#include "DWSaveGame.h"
#include "DWCombatPlayerSave.h"

namespace DWPlayerMeleeTests
{
    struct FWorldScope
    {
        TStrongObjectPtr<UDWGameInstance> CombatTestGI;
        UWorld* World=nullptr;
        FWorldScope()
        {
            World=DWCombatTestWorld::Create(CombatTestGI,TEXT("DWPlayerMeleeTests"));
            if(!World)return;
        }
        ~FWorldScope()
        {
            for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            CombatTestGI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
        }
        template<class T> T* Spawn(FVector Position=FVector(0,0,500))
        {
            FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            T* Actor=World->SpawnActor<T>(T::StaticClass(),Position,FRotator::ZeroRotator,Params);
            if(Actor&&!Actor->HasActorBegunPlay())Actor->DispatchBeginPlay();
            return Actor;
        }
        UDWDamageHitboxComponent* Box(AActor* Owner,FVector Center,FVector Extent)
        {
            auto* Component=NewObject<UDWDamageHitboxComponent>(Owner);Owner->AddInstanceComponent(Component);
            Component->SetupAttachment(Owner->GetRootComponent());Component->SetBoxExtent(Extent);
            Component->RegisterComponent();Component->SetWorldLocation(Center);
            // The nest's root is scaled 2.5 x 2.5 x 2. Test extents below are world centimetres.
            Component->SetWorldScale3D(FVector::OneVector);return Component;
        }
        void Advance(ADWPlayerCharacter* Player,float Seconds)
        {World->TimeSeconds+=Seconds;Player->Tick(Seconds);}
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerMeleeGeometryTest,"DoughWorld.Combat.PlayerMeleeHurtboxGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerMeleeGeometryTest::RunTest(const FString& Parameters)
{
    DWPlayerMeleeTests::FWorldScope Scope;
    auto* Player=Scope.Spawn<ADWPlayerCharacter>();Player->bCombatRequireLineOfSight=false;
    auto* Nest=Scope.Spawn<ADWEnemyNest>(FVector(1000,0,500));Nest->SetHealthForLoad(300.f);
    auto* Box=Scope.Box(Nest,FVector(210,0,500),FVector(40,20,20));
    TestEqual(TEXT("Fixture hurtbox world extents ignore the nest art scale"),Box->GetScaledBoxExtent(),FVector(40,20,20));
    Scope.Box(Nest,FVector(210,0,500),FVector(40,20,20))->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Melee reaches actual hurtbox edge despite distant actor origin"),Player->CollectMeleeTargets().Contains(Nest));
    Box->SetWorldLocation(FVector(221,0,500));
    TestFalse(TEXT("Gap beyond front box misses"),Player->CollectMeleeTargets().Contains(Nest));
    Box->SetWorldLocation(FVector(-60,0,500));
    TestFalse(TEXT("Behind-player box misses"),Player->CollectMeleeTargets().Contains(Nest));
    Box->SetWorldLocation(FVector(100,0,610));
    TestFalse(TEXT("Vertical separation misses"),Player->CollectMeleeTargets().Contains(Nest));
    Box->SetWorldLocation(FVector(100,0,500));Box->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Nest->SetActorLocation(FVector(100,0,500));
    TestFalse(TEXT("Disabled explicit hurtbox never falls back to decorative actor collision"),Player->CollectMeleeTargets().Contains(Nest));
    TestFalse(TEXT("Player attack volume can never become incoming damage hurtbox"),UDWDamageVolumeLibrary::IsDamageHitComponent(Player->MeleeDamageVolume));
    Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);Box->SetWorldLocation(FVector(140,0,500));
    auto* Wall=Scope.Spawn<AActor>(FVector(70,0,500));
    auto* WallBox=NewObject<UBoxComponent>(Wall);Wall->SetRootComponent(WallBox);Wall->AddInstanceComponent(WallBox);
    WallBox->SetBoxExtent(FVector(5,100,100));WallBox->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
    WallBox->SetCollisionObjectType(ECC_WorldStatic);WallBox->SetCollisionResponseToAllChannels(ECR_Block);
    WallBox->RegisterComponent();WallBox->SetWorldLocation(FVector(70,0,500));
    Player->bCombatRequireLineOfSight=true;
    TestFalse(TEXT("Solid obstacle rejects an otherwise overlapping hurtbox"),Player->CollectMeleeTargets().Contains(Nest));
    WallBox->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    TestTrue(TEXT("Removing obstacle restores geometric target"),Player->CollectMeleeTargets().Contains(Nest));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerMeleeStagesTest,"DoughWorld.Combat.PlayerMeleePhasesAndCancellation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerMeleeStagesTest::RunTest(const FString& Parameters)
{
    DWPlayerMeleeTests::FWorldScope Scope;FDWCombatUnarmedTuning T;FString Error;
    if(!TestTrue(TEXT("Formal unarmed tuning"),FDWCombatUnarmedTuning::Build(*Scope.CombatTestGI->GetCombatRules(),T,Error)))return false;
    auto* Player=Scope.Spawn<ADWPlayerCharacter>();auto* C=Player->WeaponAnimationComponent.Get();
    Player->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Ignore);
    const FVector At=Player->GetActorLocation()+FVector(T.Attack.RangeCm*.5,0,0);
    auto* Nest=Scope.Spawn<ADWEnemyNest>(At);Nest->SetHealthForLoad(300.f);
    CastChecked<UPrimitiveComponent>(Nest->GetRootComponent())->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Scope.Box(Nest,At,FVector(10));Scope.Box(Nest,At,FVector(12));
    auto* Weapon=Player->SwordPlaceholder.Get();
    Weapon->SetRelativeLocationAndRotation(FVector(11,22,33),FRotator(17,23,11));
    Weapon->SetAbsolute(false,true,false);Weapon->SetVisibility(false);Weapon->SetHiddenInGame(true);
    const FTransform WeaponBefore=Weapon->GetRelativeTransform();auto* Parent=Weapon->GetAttachParent();const FName Socket=Weapon->GetAttachSocketName();
    const auto ArtPreserved=[&](){return Weapon->GetAttachParent()==Parent&&Weapon->GetAttachSocketName()==Socket&&
        Weapon->GetRelativeTransform().Equals(WeaponBefore)&&!Weapon->IsVisible()&&Weapon->bHiddenInGame&&
        !Weapon->IsUsingAbsoluteLocation()&&Weapon->IsUsingAbsoluteRotation()&&!Weapon->IsUsingAbsoluteScale();};
    auto Advance=[&](double Dt){Scope.World->TimeSeconds+=static_cast<float>(Dt);C->AdvanceAnimation(Dt);};
    const FVector Target=At+FVector(400,0,0);const float After=300.f-static_cast<float>(T.Attack.Damage);
    // Formal empty-handed gameplay replaces Actor.MeleeDamage/AttackPhase and its temporary sword.
    // These authored attachment assertions remain as an independent preservation test.
    Player->MeleeDamage=999.f;
    TestTrue(TEXT("One public input begins formal unarmed melee"),Player->PerformMeleeAttack(Target));
    TestTrue(TEXT("Formal unarmed never exposes a hidden sword placeholder"),ArtPreserved()&&!Player->bMeleeWeaponPlaceholderActive);
    TestFalse(TEXT("Repeated input during attack rejected"),Player->PerformMeleeAttack(Target));
    TestEqual(TEXT("No retired Actor phase or duplicate animation clock"),Player->AttackPhase,EDWCombatPhase::None);
    Advance(T.Attack.WindupSeconds*.9);TestEqual(TEXT("Windup cannot damage"),Nest->Health,300.f);
    Advance(T.Attack.WindupSeconds*.1+.000001);TestEqual(TEXT("Active hits once across two explicit hurtboxes"),Nest->Health,After);
    Advance(T.Attack.ActiveSeconds*.5);TestEqual(TEXT("Second active sample cannot repeat damage"),Nest->Health,After);
    Advance(T.Attack.ActiveSeconds*.5);TestTrue(TEXT("Formal recovery retains busy state"),C->IsBusy());
    Advance(T.Attack.RecoverySeconds+.001);TestFalse(TEXT("Formal melee ends with no assigned animation"),C->IsBusy());
    TestTrue(TEXT("Natural finish preserves attachment transform visibility and absolute flags"),ArtPreserved());
    TestTrue(TEXT("Second action accepted after formal interval"),Player->PerformMeleeAttack(Target));
    Player->CancelCombatActions();TestFalse(TEXT("Cancel retains accepted interval lock"),Player->PerformMeleeAttack(Target));Advance(T.Attack.IntervalSeconds+.01);
    TestTrue(TEXT("Explicit cancellation preserves authored art"),ArtPreserved());TestEqual(TEXT("Cancelled windup never damages later"),Nest->Health,After);
    TestTrue(TEXT("Next input accepted after cancelled lock"),Player->PerformMeleeAttack(Target));
    FDamageEvent Event;Player->TakeDamage(1000.f,Event,nullptr,nullptr);Advance(T.Attack.IntervalSeconds);
    TestTrue(TEXT("Real authority death cancels pending action"),Player->IsDead()&&!C->IsBusy());
    TestTrue(TEXT("Death preserves authored weapon state"),ArtPreserved()&&!Player->bMeleeWeaponPlaceholderActive);
    TestEqual(TEXT("Dead attack cannot damage"),Nest->Health,After);TestFalse(TEXT("Dead player cannot start melee"),Player->PerformMeleeAttack(Target));
    auto* NoPlaceholder=Scope.Spawn<ADWPlayerCharacter>(FVector(0,1000,500));NoPlaceholder->bEnableMeleeWeaponPlaceholder=false;
    NoPlaceholder->SwordPlaceholder->SetVisibility(false);
    TestTrue(TEXT("Disabled placeholder keeps formal gameplay available"),NoPlaceholder->PerformMeleeAttack(FVector(600,1000,500)));
    TestFalse(TEXT("Disabled placeholder cannot force hidden art visible"),NoPlaceholder->bMeleeWeaponPlaceholderActive||NoPlaceholder->SwordPlaceholder->IsVisible());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerDashDamageTest,"DoughWorld.Combat.PlayerDashSweepAndDeduplication",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerDashDamageTest::RunTest(const FString& Parameters)
{
    DWPlayerMeleeTests::FWorldScope Scope;
    auto* Player=Scope.Spawn<ADWPlayerCharacter>();Player->bCombatRequireLineOfSight=false;
    // Preserve the swept-hurtbox/deduplication assertions, using the paid formal dash.
    // Physics obstacle behavior has a separate wall regression; this fixture isolates hurtboxes.
    Player->PerformDash();TestFalse(TEXT("Normal form does not use the old dash path"),Player->bIsRolling);
    FDWCombatPlayerVitalsConfig Config;FDWCombatPlayerVitalsRuntime Vitals;FString Error;
    if(!FDWCombatPlayerVitalsConfig::Build(*Scope.CombatTestGI->GetCombatRules(),Config,Error)||
        !Vitals.Initialize(Config,EDWCombatVitalsReset::NewCharacter)||!Vitals.EnterCommittedForm(EDWCombatBaseForm::Yeast)||!Vitals.SetSpecialAfterCommittedCost(Config.Vitals.SpecialMax))return false;
    auto* Save=NewObject<UDWSaveGame>();Player->CaptureSaveData(Save);
    Save->CombatPlayerResources.Vitals=Vitals.Capture(Player->Health);
    Save->Transformation=static_cast<float>(Vitals.GetSpecial());Save->bYeastForm=true;
    if(!TestTrue(TEXT("Explicit isolated Yeast snapshot"),Player->TryApplySaveData(Save)))return false;
    Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
    auto* Nest=Scope.Spawn<ADWEnemyNest>(FVector(160,0,500));Nest->SetHealthForLoad(300.f);
    CastChecked<UPrimitiveComponent>(Nest->GetRootComponent())->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Scope.Box(Nest,FVector(160,0,500),FVector(20));
    FDWYeastDashConfig DashRules;if(!FDWYeastDashConfig::Build(*Scope.CombatTestGI->GetCombatRules(),DashRules,Error))return false;
    FDWCombatInputPress Press;Press.Edge.Context={1,1,1};Press.Edge.Sequence=1;Press.Edge.Key=TEXT("SpaceBar");Press.Edge.bDown=true;Press.Action=TEXT("Dash");
    TestTrue(TEXT("Formal paid dash starts"),Player->TryStartFormalDash(Press,FVector::ForwardVector,FVector::RightVector));
    TestEqual(TEXT("Dash does not hit out-of-range origin"),Nest->Health,300.f);
    // Advance the movement endpoint across a thin target; combat sweeps the complete segment.
    Scope.Advance(Player,static_cast<float>(250./DashRules.SpeedCmPerSecond));
    TestEqual(TEXT("Low-frame endpoint crossing still hits thin hurtbox"),Nest->Health,300.f-static_cast<float>(DashRules.Damage));
    Player->SetActorLocation(FVector(160,0,500));Scope.Advance(Player,.01f);
    TestEqual(TEXT("Same dash cannot hit a target twice"),Nest->Health,300.f-static_cast<float>(DashRules.Damage));
    TestEqual(TEXT("Stationary nest is never knocked away"),Nest->GetActorLocation(),FVector(160,0,500));
    Player->CancelCombatActions();Scope.Advance(Player,.01f);
    TestEqual(TEXT("Cancelled dash damage cannot resume"),Nest->Health,300.f-static_cast<float>(DashRules.Damage));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPlayerMeleeInputTest,"DoughWorld.Combat.PlayerMeleeInputCompatibility",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPlayerMeleeInputTest::RunTest(const FString& Parameters)
{
    DWPlayerMeleeTests::FWorldScope Scope;auto* Controller=Scope.Spawn<ADWPlayerController>();
    FText Error;
    TestEqual(TEXT("Existing Throw enum remains index 9"),int32(EDWInputAction::Throw),9);
    TestEqual(TEXT("New melee is appended after old actions"),int32(EDWInputAction::Melee),15);
    TestEqual(TEXT("All key arrays include melee"),Controller->GetConfiguredKeys().Num(),int32(EDWInputAction::Count));
    TestEqual(TEXT("Left mouse throw preserved"),Controller->KeyThrow,EKeys::LeftMouseButton);
    TestEqual(TEXT("Default new melee uses right mouse"),Controller->KeyMelee,EKeys::RightMouseButton);
    TestTrue(TEXT("Default actions have no duplicate bindings"),Controller->ValidateInputBindings(Error));
    Controller->KeyCameraDrag=EKeys::RightMouseButton;Controller->KeyMelee=FKey();
    TestTrue(TEXT("Legacy RMB camera can coexist with unbound melee"),Controller->ValidateInputBindings(Error));
    TestTrue(TEXT("Melee remains rebindable without persisting test settings"),Controller->SetActionBinding(EDWInputAction::Melee,EKeys::Q,Error,false));
    TestEqual(TEXT("Rebind preserves old camera"),Controller->KeyCameraDrag,EKeys::RightMouseButton);
    return true;
}
#endif

