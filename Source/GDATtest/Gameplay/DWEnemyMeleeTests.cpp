#if WITH_DEV_AUTOMATION_TESTS

#include "DWEnemyCharacter.h"
#include "DWPlayerCharacter.h"
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include "TimerManager.h"
#include <limits>

namespace DWEnemyMeleeTests
{
    // Uses only transient actors and world time. No map, package, INI or disk save is touched.
    struct FWorldScope
    {
        UWorld* World = nullptr;
        FWorldScope()
        {
            if (!GEngine) return;
            World = UWorld::CreateWorld(EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DWEnemyMeleeWorld")), GetTransientPackage());
            if (!World) return;
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
        }
        ~FWorldScope()
        {
            if (!World) return;
            for (TActorIterator<AActor> It(World); It; ++It)
                if (It->HasActorBegunPlay()) It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
        template<class T> T* Spawn(FVector Location = FVector(0.f, 0.f, 500.f))
        {
            if (!World) return nullptr;
            FActorSpawnParameters SpawnParams;
            SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            return World->SpawnActor<T>(T::StaticClass(), Location, FRotator::ZeroRotator, SpawnParams);
        }
        ADWEnemyCharacter* Enemy(FVector Location = FVector(0.f, 0.f, 500.f))
        {
            ADWEnemyCharacter* Result = Spawn<ADWEnemyCharacter>(Location);
            if (!Result) return nullptr;
            Result->SetHealthForLoad(100.f);
            Result->bRequireLineOfSight = false;
            Result->bShowCombatDebug = false;
            Result->PatrolRadius = 0.f;
            Result->AttackRange = 175.f;
            Result->AttackDamage = 10.f;
            Result->AttackWindup = 0.5f;
            Result->AttackActiveDuration = 0.2f;
            Result->AttackRecovery = 0.3f;
            Result->AttackInterval = 1.2f;
            return Result;
        }
        void Advance(ADWEnemyCharacter* EnemyActor, float Seconds)
        {
            World->TimeSeconds += Seconds;
            EnemyActor->Tick(Seconds);
        }
        UBoxComponent* Wall(FVector Location, FVector Extent)
        {
            AActor* WallActor = Spawn<AActor>();
            if (!WallActor) return nullptr;
            UBoxComponent* Box = NewObject<UBoxComponent>(WallActor);
            WallActor->AddInstanceComponent(Box);
            WallActor->SetRootComponent(Box);
            Box->SetBoxExtent(Extent);
            Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
            Box->SetCollisionObjectType(ECC_WorldStatic);
            Box->SetCollisionResponseToAllChannels(ECR_Block);
            Box->RegisterComponent();
            Box->SetWorldLocation(Location);
            return Box;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemyMeleeTimingTest, "DoughWorld.Combat.EnemyMelee.TimingAndOneHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemyMeleeTimingTest::RunTest(const FString& Parameters)
{
    DWEnemyMeleeTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Transient world"), Scope.World)) return false;
    ADWEnemyCharacter* Attacker = Scope.Enemy();
    ADWEnemyCharacter* Victim = Scope.Enemy(FVector(125.f, 0.f, 500.f));
    if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Victim"), Victim)) return false;
    const FTransform OriginalBody = Attacker->PlaceholderBody->GetRelativeTransform();
    const FVector OriginalActorScale = Attacker->GetActorScale3D();
    TestTrue(TEXT("First attack begins"), Attacker->TryStartMeleeAttack(Victim));
    TestEqual(TEXT("Attack enters windup"), Attacker->AttackPhase, EDWCombatPhase::Windup);
    TestEqual(TEXT("Animation enters Attack even with empty assets"), Attacker->GetCombatAnimationState(), EDWCombatAnimationState::Attack);
    TestEqual(TEXT("Duration snapshots all three phases"), Attacker->GetCombatAttackDuration(), 1.f);
    TestEqual(TEXT("Starting attack never deals immediate contact damage"), Victim->Health, 100.f);
    TestFalse(TEXT("Repeated input cannot restart the current windup"), Attacker->TryStartMeleeAttack(Victim));
    Scope.Advance(Attacker, 0.49f);
    TestEqual(TEXT("Victim remains unharmed throughout windup"), Victim->Health, 100.f);
    TestTrue(TEXT("Windup visibly compresses the placeholder with debug drawing disabled"), Attacker->PlaceholderBody->GetRelativeScale3D().Z < OriginalBody.GetScale3D().Z);
    TestTrue(TEXT("Placeholder presentation does not resize the actor or its hurtboxes"), Attacker->GetActorScale3D().Equals(OriginalActorScale));
    Scope.Advance(Attacker, 0.02f);
    TestEqual(TEXT("Strike phase begins after windup"), Attacker->AttackPhase, EDWCombatPhase::Active);
    TestEqual(TEXT("First valid strike deals configured damage"), Victim->Health, 90.f);
    TestTrue(TEXT("Strike visibly leans the placeholder forward"), Attacker->PlaceholderBody->GetRelativeLocation().X > OriginalBody.GetLocation().X);
    Scope.Advance(Attacker, 0.05f);
    TestEqual(TEXT("Active-window ticking cannot damage twice"), Victim->Health, 90.f);
    Scope.Advance(Attacker, 0.16f);
    TestEqual(TEXT("Active window ends in recovery"), Attacker->AttackPhase, EDWCombatPhase::Recovery);
    TestFalse(TEXT("Recovery refuses another attack"), Attacker->TryStartMeleeAttack(Victim));
    Scope.Advance(Attacker, 0.3f);
    TestEqual(TEXT("Recovery returns to no attack"), Attacker->AttackPhase, EDWCombatPhase::None);
    TestTrue(TEXT("Animation exits Attack so the next attack restarts its sequence"), Attacker->GetCombatAnimationState() != EDWCombatAnimationState::Attack);
    TestTrue(TEXT("Recovery restores the exact authored placeholder transform"), Attacker->PlaceholderBody->GetRelativeTransform().Equals(OriginalBody));
    TestFalse(TEXT("Original minimum interval remains enforced"), Attacker->TryStartMeleeAttack(Victim));
    Scope.World->TimeSeconds += 0.2f;
    TestTrue(TEXT("Next attack starts after both recovery and interval"), Attacker->TryStartMeleeAttack(Victim));
    TestEqual(TEXT("Each attack has a distinct serial"), Attacker->AttackSerial, 2);
    Scope.Advance(Attacker, 1.1f);
    TestEqual(TEXT("A long frame still resolves exactly one hit"), Victim->Health, 80.f);
    TestEqual(TEXT("A long frame completes the attack cycle"), Attacker->AttackPhase, EDWCombatPhase::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemyMeleeRevalidationTest, "DoughWorld.Combat.EnemyMelee.RevalidateRangeSightFacing",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemyMeleeRevalidationTest::RunTest(const FString& Parameters)
{
    DWEnemyMeleeTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Transient world"), Scope.World)) return false;
    ADWEnemyCharacter* Attacker = Scope.Enemy();
    ADWEnemyCharacter* Victim = Scope.Enemy(FVector(125.f, 0.f, 500.f));
    if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Victim"), Victim)) return false;
    TestTrue(TEXT("Windup begins in range"), Attacker->TryStartMeleeAttack(Victim));
    Victim->SetActorLocation(FVector(500.f, 0.f, 500.f));
    Scope.Advance(Attacker, 0.51f);
    TestEqual(TEXT("Leaving range during windup avoids damage"), Victim->Health, 100.f);
    Victim->SetActorLocation(FVector(-125.f, 0.f, 500.f));
    Scope.Advance(Attacker, 0.02f);
    TestFalse(TEXT("Locked strike facing rejects a target behind"), Attacker->CanResolveMeleeHit(Victim));
    TestEqual(TEXT("Strikes do not home after the windup ends"), Victim->Health, 100.f);
    Victim->SetActorLocation(FVector(125.f, 0.f, 900.f));
    TestFalse(TEXT("Vertical separation rejects the hurtbox"), Attacker->CanResolveMeleeHit(Victim));
    Victim->SetActorLocation(FVector(125.f, 0.f, 500.f));
    Attacker->bRequireLineOfSight = true;
    UBoxComponent* BlockingWall = Scope.Wall(FVector(62.f, 0.f, 525.f), FVector(5.f, 120.f, 120.f));
    if (!TestNotNull(TEXT("Temporary blocking wall"), BlockingWall)) return false;
    TestFalse(TEXT("A new wall blocks the hit at resolution time"), Attacker->CanResolveMeleeHit(Victim));
    Scope.Advance(Attacker, 0.02f);
    TestEqual(TEXT("No damage passes through the new wall"), Victim->Health, 100.f);
    BlockingWall->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Scope.Advance(Attacker, 0.02f);
    TestEqual(TEXT("One valid sample inside the active window can connect"), Victim->Health, 90.f);
    Victim->SetHealthForLoad(0.f);
    TestFalse(TEXT("Dead targets fail the hard hit guard"), Attacker->CanResolveMeleeHit(Victim));
    Scope.Advance(Attacker, 0.01f);
    TestEqual(TEXT("Target death cancels remaining attack"), Attacker->AttackPhase, EDWCombatPhase::None);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemyKnockbackTest, "DoughWorld.Combat.EnemyMelee.KnockbackSweepAndCancellation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemyKnockbackTest::RunTest(const FString& Parameters)
{
    DWEnemyMeleeTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Transient world"), Scope.World)) return false;
    ADWEnemyCharacter* Attacker = Scope.Enemy();
    ADWEnemyCharacter* Victim = Scope.Enemy(FVector(125.f, 0.f, 500.f));
    if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Victim"), Victim)) return false;
    TestTrue(TEXT("Windup begins before knockback"), Attacker->TryStartMeleeAttack(Victim));
    Attacker->ApplyCombatKnockback(FVector(-1.f, 0.f, 0.f), 200.f, 0.5f);
    TestEqual(TEXT("Knockback cancels pending attack"), Attacker->AttackPhase, EDWCombatPhase::None);
    TestTrue(TEXT("Knockback becomes active"), Attacker->bIsCombatKnockedBack);
    TestFalse(TEXT("A knocked back actor cannot start melee"), Attacker->TryStartMeleeAttack(Victim));
    Scope.Advance(Attacker, 0.25f);
    TestEqual(TEXT("Half the duration travels half the unobstructed distance"), static_cast<float>(Attacker->GetActorLocation().X), -100.f);
    Scope.Advance(Attacker, 0.5f);
    TestEqual(TEXT("A long final frame cannot overshoot configured distance"), static_cast<float>(Attacker->GetActorLocation().X), -200.f);
    TestFalse(TEXT("Knockback finishes"), Attacker->bIsCombatKnockedBack);
    TestEqual(TEXT("Cancelled windup never damages its old target"), Victim->Health, 100.f);
    Attacker->SetActorLocation(FVector(0.f, 0.f, 500.f));
    UBoxComponent* BlockingWall = Scope.Wall(FVector(-160.f, 0.f, 500.f), FVector(10.f, 100.f, 200.f));
    if (!TestNotNull(TEXT("Temporary knockback wall"), BlockingWall)) return false;
    Attacker->ApplyCombatKnockback(FVector(-1.f, 0.f, 0.f), 300.f, 0.2f);
    Scope.Advance(Attacker, 0.3f);
    TestTrue(TEXT("Swept capsule moves toward wall"), Attacker->GetActorLocation().X < -50.f);
    TestTrue(TEXT("Swept capsule stops on near side of wall"), Attacker->GetActorLocation().X > -150.f);
    TestFalse(TEXT("Blocking impact ends knockback"), Attacker->bIsCombatKnockedBack);
    AddInfo(TEXT("This is an isolated root-capsule sweep test; real CharacterMovement, NavMesh and packaged play still require runtime verification."));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemyDeathRestoreAnimationTest, "DoughWorld.Combat.EnemyMelee.DeathRestoreAnimationAndFiniteValues",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemyDeathRestoreAnimationTest::RunTest(const FString& Parameters)
{
    DWEnemyMeleeTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Transient world"), Scope.World)) return false;
    ADWEnemyCharacter* EnemyActor = Scope.Enemy();
    ADWEnemyCharacter* Victim = Scope.Enemy(FVector(125.f, 0.f, 500.f));
    if (!TestNotNull(TEXT("Enemy"), EnemyActor) || !TestNotNull(TEXT("Victim"), Victim)) return false;
    // InitializeActorsForPlay prepares the transient world without beginning play on these actors.
    // Native AActor::BeginPlay registers/enables PrimaryActorTick. Manual Tick calls in other tests
    // deliberately do not imply that lifecycle step, so enter it explicitly for the restore test.
    if (!EnemyActor->HasActorBegunPlay()) EnemyActor->DispatchBeginPlay();
    TestTrue(TEXT("Restore fixture has begun normal actor play"), EnemyActor->HasActorBegunPlay());
    TestTrue(TEXT("Live actor tick is enabled before death"), EnemyActor->IsActorTickEnabled());
    EnemyActor->GetMesh()->SetComponentTickEnabled(true);
    TestTrue(TEXT("Live skeletal mesh tick is enabled before death"), EnemyActor->GetMesh()->IsComponentTickEnabled());
    TestEqual(TEXT("Legacy Patrol enum remains zero"), static_cast<uint8>(EDWEnemyState::Patrol), static_cast<uint8>(0));
    TestEqual(TEXT("Legacy Dead enum remains three"), static_cast<uint8>(EDWEnemyState::Dead), static_cast<uint8>(3));
    TestEqual(TEXT("Alert appends without renumbering old states"), static_cast<uint8>(EDWEnemyState::Alert), static_cast<uint8>(4));
    TestEqual(TEXT("Stationary enemy exposes Idle"), EnemyActor->GetCombatAnimationState(), EDWCombatAnimationState::Idle);
    EnemyActor->GetCharacterMovement()->Velocity = FVector(100.f, 0.f, 0.f);
    TestEqual(TEXT("Moving enemy exposes Move"), EnemyActor->GetCombatAnimationState(), EDWCombatAnimationState::Move);
    EnemyActor->GetCharacterMovement()->StopMovementImmediately();
    APlayerController* Controller = Scope.Spawn<APlayerController>();
    ADWPlayerCharacter* PlayerPawn = Scope.Spawn<ADWPlayerCharacter>(FVector(500.f, 500.f, 500.f));
    if (!TestNotNull(TEXT("Transient controller"), Controller) || !TestNotNull(TEXT("Transient pawn"), PlayerPawn)) return false;
    Controller->Possess(PlayerPawn);
    FDamageEvent DamageEvent;
    EnemyActor->TakeDamage(1.f, DamageEvent, Controller, PlayerPawn);
    TestEqual(TEXT("Being hit by a player enters Alert"), EnemyActor->State, EDWEnemyState::Alert);
    TestEqual(TEXT("Alert exposes its dedicated animation state"), EnemyActor->GetCombatAnimationState(), EDWCombatAnimationState::Alert);
    EnemyActor->SetHealthForLoad(100.f);
    EnemyActor->PersistentId = TEXT("CombatRestorePlacedEnemy");
    const FTransform OriginalBody = EnemyActor->PlaceholderBody->GetRelativeTransform();
    TestTrue(TEXT("Attack starts before death"), EnemyActor->TryStartMeleeAttack(Victim));
    EnemyActor->TakeDamage(1000.f, DamageEvent, nullptr, nullptr);
    TestEqual(TEXT("Death cancels windup"), EnemyActor->AttackPhase, EDWCombatPhase::None);
    TestEqual(TEXT("Death has animation priority"), EnemyActor->GetCombatAnimationState(), EDWCombatAnimationState::Death);
    TestTrue(TEXT("Death visibly flattens the placeholder"), EnemyActor->PlaceholderBody->GetRelativeScale3D().Z < OriginalBody.GetScale3D().Z * 0.5f);
    TestFalse(TEXT("Persistent corpse remains available to save capture"), EnemyActor->IsActorBeingDestroyed());
    TestEqual(TEXT("Persistent corpse is not scheduled for destruction"), EnemyActor->GetLifeSpan(), 0.f);
    TestTrue(TEXT("Death presentation keeps skeletal animation ticking until cleanup"), EnemyActor->GetMesh()->IsComponentTickEnabled());
    EnemyActor->ApplyCombatKnockback(FVector::ForwardVector, 200.f, 0.2f);
    TestFalse(TEXT("Death rejects knockback"), EnemyActor->bIsCombatKnockedBack);
    EnemyActor->SetHealthForLoad(0.f);
    TestTrue(TEXT("Loading death hides persistent corpse immediately"), EnemyActor->IsHidden());
    TestFalse(TEXT("Hidden persistent corpse stops independent skeletal animation tick"), EnemyActor->GetMesh()->IsComponentTickEnabled());
    EnemyActor->SetHealthForLoad(75.f);
    TestTrue(TEXT("An earlier live save revives the same placed actor"), EnemyActor->IsAlive());
    TestEqual(TEXT("Restored health is retained"), EnemyActor->Health, 75.f);
    TestEqual(TEXT("Live restore resets AI state"), EnemyActor->State, EDWEnemyState::Patrol);
    TestFalse(TEXT("Live restore restores visibility"), EnemyActor->IsHidden());
    TestTrue(TEXT("Live restore restores collision"), EnemyActor->GetActorEnableCollision());
    TestTrue(TEXT("Live restore restores ticking"), EnemyActor->IsActorTickEnabled());
    TestTrue(TEXT("Live restore restores the original skeletal animation tick"), EnemyActor->GetMesh()->IsComponentTickEnabled());
    TestTrue(TEXT("Live restore restores movement"), EnemyActor->GetCharacterMovement()->MovementMode != MOVE_None);
    TestEqual(TEXT("No old attack survives restoring health"), EnemyActor->AttackPhase, EDWCombatPhase::None);
    TestTrue(TEXT("Live save restores the authored visual transform"), EnemyActor->PlaceholderBody->GetRelativeTransform().Equals(OriginalBody));
    const float Invalid = std::numeric_limits<float>::quiet_NaN();
    TestEqual(TEXT("NaN damage is rejected"), EnemyActor->TakeDamage(Invalid, DamageEvent, nullptr, nullptr), 0.f);
    EnemyActor->SetHealthForLoad(Invalid);
    TestEqual(TEXT("NaN save health does not poison current health"), EnemyActor->Health, 75.f);
    EnemyActor->ApplyCombatKnockback(FVector::ForwardVector, Invalid, 0.2f);
    TestFalse(TEXT("NaN knockback distance is rejected"), EnemyActor->bIsCombatKnockedBack);
    EnemyActor->AttackWindup = Invalid;
    EnemyActor->AttackActiveDuration = Invalid;
    EnemyActor->AttackRecovery = Invalid;
    TestTrue(TEXT("Invalid timing uses finite fallback values"), EnemyActor->TryStartMeleeAttack(Victim));
    TestTrue(TEXT("Animation receives a finite positive attack duration"), FMath::IsFinite(EnemyActor->GetCombatAttackDuration()) && EnemyActor->GetCombatAttackDuration() > 0.f);
    Scope.Advance(EnemyActor, 0.2f);
    EnemyActor->bAnimatePlaceholder = false;
    Scope.Advance(EnemyActor, 0.01f);
    TestTrue(TEXT("Disabling placeholder animation restores its authored transform immediately"), EnemyActor->PlaceholderBody->GetRelativeTransform().Equals(OriginalBody));
    EnemyActor->SetHealthForLoad(75.f);
    Scope.Advance(EnemyActor, 2.f);
    TestEqual(TEXT("Live restore cancels pending damage"), Victim->Health, 100.f);
    ADWEnemyCharacter* DormantEnemy = Scope.Enemy(FVector(3000.f, 0.f, 500.f));
    if (!TestNotNull(TEXT("Intentionally unticked enemy"), DormantEnemy)) return false;
    if (!DormantEnemy->HasActorBegunPlay()) DormantEnemy->DispatchBeginPlay();
    DormantEnemy->PersistentId = TEXT("CombatRestoreDormantEnemy");
    DormantEnemy->SetActorTickEnabled(false);
    DormantEnemy->GetMesh()->SetComponentTickEnabled(false);
    TestFalse(TEXT("Designer-disabled tick is the pre-death baseline"), DormantEnemy->IsActorTickEnabled());
    DormantEnemy->TakeDamage(1000.f, DamageEvent, nullptr, nullptr);
    DormantEnemy->SetHealthForLoad(0.f);
    DormantEnemy->SetHealthForLoad(50.f);
    TestTrue(TEXT("Designer-disabled tick does not prevent logical revival"), DormantEnemy->IsAlive());
    TestFalse(TEXT("Live restore preserves an intentionally disabled tick baseline"), DormantEnemy->IsActorTickEnabled());
    TestFalse(TEXT("Live restore preserves an intentionally disabled skeletal tick baseline"), DormantEnemy->GetMesh()->IsComponentTickEnabled());
    return true;
}

#endif
