#if WITH_DEV_AUTOMATION_TESTS

#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWResourceNode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/AutomationTest.h"

namespace DWCombatTests
{
    /** A transient world only: no project map, editor session, disk save or PIE is touched. */
    struct FTestWorld
    {
        UWorld* World = nullptr;

        FTestWorld()
        {
            if (!GEngine) return;
            const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DWCombatTestWorld"));
            World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
            if (World)
            {
                GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
                World->InitializeActorsForPlay(FURL());
            }
        }

        ~FTestWorld()
        {
            if (!World) return;
            for (TActorIterator<AActor> It(World); It; ++It)
                if (It->HasActorBegunPlay()) It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }

        template <class T> T* Spawn()
        {
            if (!World) return nullptr;
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            return World->SpawnActor<T>(T::StaticClass(), FVector(0.f, 0.f, 500.f), FRotator::ZeroRotator, Params);
        }

        /** Advance only this test world's clock, then run the actual enemy update. */
        void AdvanceEnemy(ADWEnemyCharacter* Enemy, float Delta)
        {
            World->TimeSeconds += Delta;
            Enemy->Tick(Delta);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatDamageAndPersistenceTest, "DoughWorld.Combat.DamageAndDeathPersistence",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCombatDamageAndPersistenceTest::RunTest(const FString& Parameters)
{
    DWCombatTests::FTestWorld Scope;
    if (!TestNotNull(TEXT("Transient world created"), Scope.World)) return false;
    ADWEnemyCharacter* Enemy = Scope.Spawn<ADWEnemyCharacter>();
    ADWEnemyNest* Nest = Scope.Spawn<ADWEnemyNest>();
    if (!TestNotNull(TEXT("Enemy spawned"), Enemy) || !TestNotNull(TEXT("Nest spawned"), Nest)) return false;
    Enemy->PersistentId = TEXT("PlacedEnemy01");
    Enemy->SetHealthForLoad(100.f);
    FDamageEvent Event;
    TestEqual(TEXT("Valid hit reports effective damage"), Enemy->TakeDamage(30.f, Event, nullptr, nullptr), 30.f);
    TestEqual(TEXT("Damage reduces enemy health"), Enemy->Health, 70.f);
    TestEqual(TEXT("Negative damage cannot heal"), Enemy->TakeDamage(-10.f, Event, nullptr, nullptr), 0.f);
    TestEqual(TEXT("Health preserved after invalid damage"), Enemy->Health, 70.f);
    TestEqual(TEXT("Overkill reports only remaining health"), Enemy->TakeDamage(200.f, Event, nullptr, nullptr), 70.f);
    TestEqual(TEXT("Enemy health stops at zero"), Enemy->Health, 0.f);
    TestFalse(TEXT("Dead enemy is no longer alive"), Enemy->IsAlive());
    TestEqual(TEXT("Repeated hits on corpse give no damage reward"), Enemy->TakeDamage(10.f, Event, nullptr, nullptr), 0.f);
    TestFalse(TEXT("Persistent corpse remains a save-queryable actor"), Enemy->IsActorBeingDestroyed());
    TestEqual(TEXT("Persistent corpse is never scheduled for actor destruction"), Enemy->GetLifeSpan(), 0.f);
    bool bFoundDeadId = false;
    for (TActorIterator<ADWEnemyCharacter> It(Scope.World); It; ++It)
        if (It->PersistentId == TEXT("PlacedEnemy01") && It->Health == 0.f) bFoundDeadId = true;
    TestTrue(TEXT("Save enumeration retains killed authored enemy ID"), bFoundDeadId);

    ADWEnemyCharacter* DynamicEnemy = Scope.Spawn<ADWEnemyCharacter>();
    if (!TestNotNull(TEXT("Dynamic enemy spawned"), DynamicEnemy)) return false;
    DynamicEnemy->SetHealthForLoad(100.f);
    DynamicEnemy->TakeDamage(100.f, Event, nullptr, nullptr);
    TestTrue(TEXT("Nonpersistent spawned corpse schedules normal cleanup"), DynamicEnemy->GetLifeSpan() > 0.f);

    Nest->PersistentId = TEXT("Nest01");
    Nest->SetHealthForLoad(500.f);
    TestEqual(TEXT("Nest can be damaged by same damage API"), Nest->TakeDamage(125.f, Event, nullptr, nullptr), 125.f);
    TestEqual(TEXT("Nest partial health persists"), Nest->Health, 375.f);
    Nest->SetHealthForLoad(125.f);
    if (!Nest->HasActorBegunPlay()) Nest->DispatchBeginPlay();
    TestEqual(TEXT("BeginPlay never overwrites a previously restored nest health"), Nest->Health, 125.f);
    Nest->SetHealthForLoad(0.f);
    TestFalse(TEXT("Destroyed saved nest cannot spawn another enemy"), Nest->IsAlive());
    TestNull(TEXT("Spawn API refuses dead nest"), Nest->SpawnEnemy());
    TestFalse(TEXT("Destroyed nest remains available to save capture"), Nest->IsActorBeingDestroyed());
    ADWEnemyCharacter* RestoredEnemy = Scope.Spawn<ADWEnemyCharacter>();
    if (!TestNotNull(TEXT("Restored enemy spawned"), RestoredEnemy)) return false;
    RestoredEnemy->SetHealthForLoad(43.f);
    if (!RestoredEnemy->HasActorBegunPlay()) RestoredEnemy->DispatchBeginPlay();
    TestEqual(TEXT("BeginPlay never overwrites a previously restored enemy health"), RestoredEnemy->Health, 43.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatStrongestSlowTest, "DoughWorld.Combat.StrongestSlowRefreshAndExpiry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCombatStrongestSlowTest::RunTest(const FString& Parameters)
{
    DWCombatTests::FTestWorld Scope;
    if (!TestNotNull(TEXT("Transient world created"), Scope.World)) return false;
    ADWEnemyCharacter* Enemy = Scope.Spawn<ADWEnemyCharacter>();
    if (!TestNotNull(TEXT("Enemy spawned"), Enemy)) return false;
    Enemy->MoveSpeed = 250.f;
    Enemy->PatrolRadius = 0.f;
    Enemy->ApplySlow(30.f, 1.f);
    TestEqual(TEXT("Thirty percent slow gives 175 cm/s"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 175.f);
    Scope.AdvanceEnemy(Enemy, 0.4f);
    Enemy->ApplySlow(30.f, 1.f);
    TestEqual(TEXT("Equal effects refresh instead of multiply"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 175.f);
    Enemy->ApplySlow(60.f, 0.3f);
    // 250.f * (1.f - 60.f/100.f) is 99.99999237 in float32, one ULP below 100.
    // Use the engine's float-test tolerance, matching all other speed assertions.
    AddInfo(FString::Printf(TEXT("Strongest slow actual speed: %.9f cm/s"), Enemy->GetCharacterMovement()->MaxWalkSpeed));
    TestEqual(TEXT("Stronger overlapping area wins"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 100.f);
    Scope.AdvanceEnemy(Enemy, 0.31f);
    TestEqual(TEXT("After stronger expiry weaker active slow remains"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 175.f);
    Scope.AdvanceEnemy(Enemy, 0.4f);
    TestEqual(TEXT("Refreshed slow survives its original expiry time"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 175.f);
    Scope.AdvanceEnemy(Enemy, 0.4f);
    TestEqual(TEXT("Movement returns to configured baseline after all effects expire"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 250.f);
    Enemy->MoveSpeed = 300.f;
    Scope.AdvanceEnemy(Enemy, 0.01f);
    TestEqual(TEXT("Recovery follows edited movement speed"), Enemy->GetCharacterMovement()->MaxWalkSpeed, 300.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWResourceCommitTest, "DoughWorld.Combat.ResourceCommitAndRestore",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWResourceCommitTest::RunTest(const FString& Parameters)
{
    DWCombatTests::FTestWorld Scope;
    if (!TestNotNull(TEXT("Transient world created"), Scope.World)) return false;
    ADWDoughResourceNode* Dough = Scope.Spawn<ADWDoughResourceNode>();
    ADWWaterResourceNode* Water = Scope.Spawn<ADWWaterResourceNode>();
    ADWYeastResourceNode* Yeast = Scope.Spawn<ADWYeastResourceNode>();
    if (!TestNotNull(TEXT("Finite source spawned"), Dough) || !TestNotNull(TEXT("Water source spawned"), Water) || !TestNotNull(TEXT("Yeast source spawned"), Yeast)) return false;
    Dough->RestoreRemainingAmount(8);
    Dough->CommitHarvest(3);
    TestEqual(TEXT("Finite source deducts actual inventory acceptance"), Dough->RemainingAmount, 5);
    Dough->CommitHarvest(0);
    Dough->CommitHarvest(-2);
    TestEqual(TEXT("Empty or invalid commits do not consume resource"), Dough->RemainingAmount, 5);
    Dough->CommitHarvest(5);
    TestFalse(TEXT("Depleted finite source cannot harvest"), Dough->IsAvailable());
    Dough->CommitHarvest(3);
    TestEqual(TEXT("Repeated depletion never makes count negative"), Dough->RemainingAmount, 0);
    Dough->RestoreRemainingAmount(2);
    TestTrue(TEXT("Loading remaining stock restores availability"), Dough->IsAvailable());
    Dough->CommitHarvest(2);
    TestEqual(TEXT("Final partial batch can be committed"), Dough->RemainingAmount, 0);
    Water->RestoreRemainingAmount(0);
    for (int32 Index = 0; Index < 50; ++Index) Water->CommitHarvest(1);
    TestTrue(TEXT("Infinite water remains available after repeated harvest"), Water->IsAvailable());
    TestEqual(TEXT("Infinite source does not consume artificial stock"), Water->RemainingAmount, 0);
    Yeast->RestoreRemainingAmount(0);
    Yeast->CommitHarvest(5);
    TestTrue(TEXT("Infinite yeast remains available"), Yeast->IsAvailable());
    TestEqual(TEXT("Yeast keeps its own inventory identity"), Yeast->ItemId, FName(TEXT("Yeast")));
    return true;
}

#endif
