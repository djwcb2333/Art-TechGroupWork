#if WITH_DEV_AUTOMATION_TESTS

#include "DWEnemyNest.h"
#include "DWEnemyCharacter.h"
#include "Animation/AnimSequence.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/DamageEvents.h"
#include "EngineUtils.h"
#include "GameFramework/DefaultPawn.h"
#include "GameFramework/PlayerController.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace DWEnemySpawnTests
{
    struct FWorldScope
    {
        UWorld* World = nullptr;
        FWorldScope()
        {
            if (!GEngine) return;
            World = UWorld::CreateWorld(EWorldType::Game, false,
                MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DWEnemySpawnWorld")), GetTransientPackage());
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
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            return World->SpawnActor<T>(T::StaticClass(), Location, FRotator::ZeroRotator, Params);
        }
        ADWEnemyCharacter* Enemy(FVector Location = FVector(0.f, 0.f, 500.f))
        {
            ADWEnemyCharacter* Result = Spawn<ADWEnemyCharacter>(Location);
            if (Result)
            {
                Result->SetHealthForLoad(100.f);
                Result->bRequireLineOfSight = false;
                Result->bShowCombatDebug = false;
                Result->PatrolRadius = 0.f;
                Result->AttackRange = 175.f;
            }
            return Result;
        }
        void Advance(ADWEnemyCharacter* Enemy, float Seconds)
        {
            World->TimeSeconds += Seconds;
            Enemy->Tick(Seconds);
        }
    };

    FDWEnemySpawnEntry Entry(UClass* Class, float Weight, int32 Cap = 0)
    {
        FDWEnemySpawnEntry Result;
        Result.EnemyClass = Class;
        Result.Weight = Weight;
        Result.MaxAliveOfType = Cap;
        return Result;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemySpawnWeightedRulesTest, "DoughWorld.Enemies.Spawning.WeightedRulesAndCaps",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemySpawnWeightedRulesTest::RunTest(const FString& Parameters)
{
    // Metadata-only transient subclasses: no project blueprint, asset load or actor instance is required.
    UClass* Small = NewObject<UClass>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UClass::StaticClass(), TEXT("DWSpawnSmallType")), RF_Transient);
    UClass* Giant = NewObject<UClass>(GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UClass::StaticClass(), TEXT("DWSpawnGiantType")), RF_Transient);
    Small->SetSuperStruct(ADWEnemyCharacter::StaticClass());
    Giant->SetSuperStruct(ADWEnemyCharacter::StaticClass());
    TArray<FDWEnemySpawnEntry> Entries;
    Entries.Add(DWEnemySpawnTests::Entry(Small, 85.f));
    Entries.Add(DWEnemySpawnTests::Entry(Giant, 15.f, 2));
    TMap<UClass*, int32> Alive;
    int32 SmallCount = 0, GiantCount = 0;
    for (int32 Index = 0; Index < 1000; ++Index)
    {
        UClass* Selected = DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, (Index + .5) / 1000.);
        if (Selected == Small) ++SmallCount;
        if (Selected == Giant) ++GiantCount;
    }
    TestEqual(TEXT("85 percent selection interval belongs to small enemies"), SmallCount, 850);
    TestEqual(TEXT("15 percent selection interval belongs to giants"), GiantCount, 150);
    TestTrue(TEXT("Upper endpoint remains a valid last entry"), DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, 1.) == Giant);
    Alive.Add(Giant, 2);
    Alive.Add(Small, 100);
    TestTrue(TEXT("Capped giant is excluded and unlimited small stays eligible"), DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, .99) == Small);
    Entries.Add(DWEnemySpawnTests::Entry(Giant, 100.f, 0));
    TestTrue(TEXT("Duplicate uncapped row cannot bypass an existing giant cap"), DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, .99) == Small);
    Entries[0].MaxAliveOfType = 100;
    TestNull(TEXT("All capped types produce no selection"), DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, .5));
    Alive[Giant] = 1;
    TestTrue(TEXT("Removing one living giant makes that type eligible again"), DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, .1) == Giant);
    Entries.Reset();
    Entries.Add(DWEnemySpawnTests::Entry(nullptr, 999.f));
    Entries.Add(DWEnemySpawnTests::Entry(ADWEnemyCharacter::StaticClass(), std::numeric_limits<float>::quiet_NaN()));
    Entries.Add(DWEnemySpawnTests::Entry(ADWEnemyCharacter::StaticClass(), -1.f));
    Entries.Add(DWEnemySpawnTests::Entry(ADWEnemyCharacter::StaticClass(), 0.f));
    TestNull(TEXT("Invalid and nonpositive weights never create a usable pool"), DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, .5));
    Entries.Add(DWEnemySpawnTests::Entry(ADWEnemyCharacter::StaticClass(), 1.f));
    TestTrue(TEXT("NaN draw remains finite and deterministic"), DWEnemySpawnRules::SelectWeightedClass(Entries, Alive, std::numeric_limits<double>::quiet_NaN()) == ADWEnemyCharacter::StaticClass());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemySpawnRangeRulesTest, "DoughWorld.Enemies.Spawning.LegacyAndRangeBoundaries",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemySpawnRangeRulesTest::RunTest(const FString& Parameters)
{
    TestEqual(TEXT("Random interval disabled preserves the authored fixed interval"), DWEnemySpawnRules::SampleInterval(false, 4.5f, 12.f, 20.f, .5f), 4.5f);
    TestEqual(TEXT("Swapped random bounds are ordered"), DWEnemySpawnRules::SampleInterval(true, 10.f, 20.f, 12.f, .25f), 14.f);
    TestEqual(TEXT("Interval lower endpoint"), DWEnemySpawnRules::SampleInterval(true, 10.f, 12.f, 20.f, 0.f), 12.f);
    TestEqual(TEXT("Interval upper endpoint"), DWEnemySpawnRules::SampleInterval(true, 10.f, 12.f, 20.f, 1.f), 20.f);
    TestEqual(TEXT("Non-finite fixed interval gets a usable fallback"), DWEnemySpawnRules::SampleInterval(false, std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f, 0.f), 10.f);
    TestEqual(TEXT("Zero interval cannot divide by zero"), DWEnemySpawnRules::SampleInterval(false, 0.f, 0.f, 0.f, 0.f), .1f);
    const FVector2D Legacy = DWEnemySpawnRules::RadiusRange(false, 220.f, 500.f, 1000.f);
    TestTrue(TEXT("Legacy radius remains a fixed 220cm ring"), Legacy.Equals(FVector2D(220., 220.)));
    TestTrue(TEXT("Legacy radius minimum stays 150cm"), DWEnemySpawnRules::RadiusRange(false, 0.f, 0.f, 0.f).Equals(FVector2D(150., 150.)));
    TestTrue(TEXT("Reversed annulus bounds are ordered"), DWEnemySpawnRules::RadiusRange(true, 220.f, 900.f, 100.f).Equals(FVector2D(100., 900.)));
    TestTrue(TEXT("Negative annulus inner radius clamps to zero"), DWEnemySpawnRules::RadiusRange(true, 220.f, -1.f, 800.f).Equals(FVector2D(0., 800.)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemySpawnRuntimeGuardsTest, "DoughWorld.Enemies.Spawning.OwnershipCapsAndNavigationGuards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemySpawnRuntimeGuardsTest::RunTest(const FString& Parameters)
{
    DWEnemySpawnTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Transient world"), Scope.World)) return false;
    ADWEnemyNest* Nest = Scope.Spawn<ADWEnemyNest>();
    if (!TestNotNull(TEXT("Nest"), Nest)) return false;
    const bool OriginalDamageFlag = Nest->CanBeDamaged();
    Nest->NestMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    Nest->SpawnRadius = 600.f;
    Nest->EnemyClass = ADWEnemyCharacter::StaticClass();
    ADWEnemyCharacter* First = Nest->SpawnEnemy();
    if (!TestNotNull(TEXT("Empty weighted list keeps legacy single-class spawning"), First)) return false;
    TestTrue(TEXT("Spawned enemy retains existing save owner"), First->SourceNest == Nest);
    Nest->MaxAliveEnemies = 1;
    TestNull(TEXT("Total cap blocks an additional spawn"), Nest->SpawnEnemy());
    Nest->RegisterRestoredEnemy(First);
    TestNull(TEXT("Restored ownership registration preserves the same living cap"), Nest->SpawnEnemy());
    Nest->MaxAliveEnemies = 0;
    Nest->WeightedEnemies.Add(DWEnemySpawnTests::Entry(ADWEnemyCharacter::StaticClass(), 1.f, 1));
    TestNull(TEXT("Type cap is respected when total cap is unlimited"), Nest->SpawnEnemy());
    First->SetHealthForLoad(0.f);
    ADWEnemyCharacter* Replacement = Nest->SpawnEnemy();
    if (!TestNotNull(TEXT("A dead enemy releases its living type cap"), Replacement)) return false;
    Replacement->SetHealthForLoad(0.f);
    Nest->WeightedEnemies[0].Weight = 0.f;
    TestNull(TEXT("Configured exhausted pool never silently uses EnemyClass fallback"), Nest->SpawnEnemy());
    Nest->WeightedEnemies.Reset();
    Nest->bRequireNavigableSpawn = true;
    TestNull(TEXT("Strict navigation cannot use the legacy air fallback in this empty test world"), Nest->SpawnEnemy());
    Nest->bRequireNavigableSpawn = false;
    Nest->bAmbientSpawner = true;
    Nest->OnConstruction(Nest->GetActorTransform());
    TestFalse(TEXT("Ambient node hides its nest mesh"), Nest->NestMesh->IsVisible());
    TestFalse(TEXT("Ambient node cannot be damaged"), Nest->CanBeDamaged());
    const float BeforeHealth = Nest->Health;
    FDamageEvent Damage;
    TestEqual(TEXT("Ambient damage API refuses a hit"), Nest->TakeDamage(50.f, Damage, nullptr, nullptr), 0.f);
    TestEqual(TEXT("Ambient health is unchanged"), Nest->Health, BeforeHealth);
    TestNull(TEXT("Ambient mode cannot spawn without a player/navigation/ground"), Nest->SpawnEnemy());
    Nest->bAmbientSpawner = false;
    Nest->OnConstruction(Nest->GetActorTransform());
    TestTrue(TEXT("Leaving ambient mode restores the authored visibility"), Nest->NestMesh->IsVisible());
    TestEqual(TEXT("Leaving ambient mode restores the authored damage flag"), Nest->CanBeDamaged(), OriginalDamageFlag);
    APlayerController* Controller = Scope.Spawn<APlayerController>();
    ADefaultPawn* Player = Scope.Spawn<ADefaultPawn>();
    if (!TestNotNull(TEXT("Isolated player controller"), Controller) || !TestNotNull(TEXT("Isolated player pawn"), Player)) return false;
    Controller->Possess(Player);
    Nest->MinimumPlayerSpawnDistance = 100000.f;
    TestNull(TEXT("No legacy spawn candidate may ignore the requested player clearance"), Nest->SpawnEnemy());
    Nest->SetHealthForLoad(0.f);
    TestNull(TEXT("Saved destroyed nest remains unable to spawn"), Nest->SpawnEnemy());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWEnemyAttackVariantSnapshotTest, "DoughWorld.Enemies.AttackVariants.SnapshotAndOneHit",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWEnemyAttackVariantSnapshotTest::RunTest(const FString& Parameters)
{
    DWEnemySpawnTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Transient world"), Scope.World)) return false;
    ADWEnemyCharacter* Attacker = Scope.Enemy();
    ADWEnemyCharacter* Victim = Scope.Enemy(FVector(125.f, 0.f, 500.f));
    if (!TestNotNull(TEXT("Attacker"), Attacker) || !TestNotNull(TEXT("Victim"), Victim)) return false;
    UAnimSequence* LegacyAnimation = NewObject<UAnimSequence>(GetTransientPackage());
    UAnimSequence* VariantAnimation = NewObject<UAnimSequence>(GetTransientPackage());
    Attacker->AttackAnimation = LegacyAnimation;
    FDWEnemyAttackVariant Variant;
    Variant.Animation = VariantAnimation;
    Variant.Damage = 17.f;
    Variant.Range = 300.f;
    Variant.Windup = .2f;
    Variant.Active = .1f;
    Variant.Recovery = .3f;
    Variant.Interval = 1.2f;
    Attacker->AttackVariants.Add(Variant);
    TestTrue(TEXT("Variant attack begins through the ordinary approach gate"), Attacker->TryStartMeleeAttack(Victim));
    TestEqual(TEXT("Variant selected exactly once"), Attacker->CurrentAttackVariantIndex, 0);
    TestTrue(TEXT("Selected animation is exposed to the existing AnimInstance"), Attacker->GetCurrentAttackAnimation() == VariantAnimation);
    TestTrue(TEXT("Chosen durations form a 0.6 second action"), FMath::IsNearlyEqual(Attacker->GetCombatAttackDuration(), .6f));
    Attacker->AttackVariants[0].Damage = 99.f;
    Attacker->AttackVariants[0].Range = 1.f;
    Attacker->AttackVariants[0].Windup = 20.f;
    Attacker->AttackVariants[0].Animation = LegacyAnimation;
    Attacker->AttackDamage = 88.f;
    Attacker->AttackRange = 1.f;
    TestFalse(TEXT("Repeated attack request cannot redraw the variant"), Attacker->TryStartMeleeAttack(Victim));
    Scope.Advance(Attacker, .21f);
    TestEqual(TEXT("In-flight edits do not change the snapshotted damage/range/timing"), Victim->Health, 83.f);
    TestTrue(TEXT("In-flight edits do not change the chosen animation"), Attacker->GetCurrentAttackAnimation() == VariantAnimation);
    Scope.Advance(Attacker, .05f);
    TestEqual(TEXT("Active-window repeat never applies a second hit"), Victim->Health, 83.f);
    Scope.Advance(Attacker, .4f);
    TestEqual(TEXT("Variant reaches the original none phase after recovery"), Attacker->AttackPhase, EDWCombatPhase::None);
    TestTrue(TEXT("Outgoing blend retains the selected animation"), Attacker->GetCurrentAttackAnimation() == VariantAnimation);
    Attacker->AttackVariants.Reset();
    Attacker->AttackRange = 175.f;
    Attacker->AttackDamage = 5.f;
    Scope.World->TimeSeconds += 1.2f;
    TestTrue(TEXT("Cleared variants use the original default attack next time"), Attacker->TryStartMeleeAttack(Victim));
    TestEqual(TEXT("Fallback attack reports no variant index"), Attacker->CurrentAttackVariantIndex, INDEX_NONE);
    TestTrue(TEXT("Fallback uses the original attack slot"), Attacker->GetCurrentAttackAnimation() == LegacyAnimation);
    Attacker->SetHealthForLoad(100.f);
    TestEqual(TEXT("Loading clears pending attack phase"), Attacker->AttackPhase, EDWCombatPhase::None);
    TestTrue(TEXT("Loading clears old variant presentation"), Attacker->GetCurrentAttackAnimation() == LegacyAnimation);
    Variant.SelectionWeight = std::numeric_limits<float>::quiet_NaN();
    Attacker->AttackVariants.Add(Variant);
    TestTrue(TEXT("Invalid variant weights retain a usable ordinary attack"), Attacker->TryStartMeleeAttack(Victim));
    TestEqual(TEXT("Invalid weights do not choose a variant"), Attacker->CurrentAttackVariantIndex, INDEX_NONE);
    Attacker->SetHealthForLoad(100.f);
    Attacker->AttackVariants[0] = Variant;
    Attacker->AttackVariants[0].SelectionWeight = 1.f;
    Attacker->AttackVariants[0].Range = 1.f;
    TestTrue(TEXT("Main AttackRange remains the approach/start rule"), Attacker->TryStartMeleeAttack(Victim));
    const float HealthBeforeShortStrike = Victim->Health;
    Scope.Advance(Attacker, .21f);
    TestEqual(TEXT("Selected shorter strike range does not bypass geometric hit checks"), Victim->Health, HealthBeforeShortStrike);
    return true;
}

#endif
