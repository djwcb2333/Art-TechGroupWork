#if WITH_DEV_AUTOMATION_TESTS

#include "DWPlayerCharacter.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWSaveGame.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"

namespace DWTimerTests
{
    /** A separate transient world; tests never load, tick, or modify the user's map. */
    struct FPlayerWorld
    {
        UWorld* World = nullptr;
        ADWPlayerCharacter* Player = nullptr;

        FPlayerWorld()
        {
            UWorld::InitializationValues Initialization;
            Initialization.AllowAudioPlayback(false).CreateNavigation(false).CreateAISystem(false)
                .ShouldSimulatePhysics(false).CreateFXSystem(false).SetTransactional(false);
            World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
                ERHIFeatureLevel::Num, &Initialization);
            if (World) Player = SpawnPlayer(FVector(0.f, 0.f, 120.f));
        }

        ADWPlayerCharacter* SpawnPlayer(const FVector& Location)
        {
            FActorSpawnParameters Parameters;
            Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            ADWPlayerCharacter* Result = World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(), Location, FRotator::ZeroRotator, Parameters);
            if (Result)
            {
                Result->GameplayConfig = NewObject<UDWGameplayConfig>(Result);
                Result->Inventory->InitializeInventory(Result->GameplayConfig);
                Result->Health = Result->GameplayConfig->MaxHealth;
            }
            return Result;
        }

        ~FPlayerWorld()
        {
            if (World) World->DestroyWorld(false);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSprintAccumulationTest, "DoughWorld.Gameplay.Timers.InterruptedSprintAndSave",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWSprintAccumulationTest::RunTest(const FString& Parameters)
{
    DWTimerTests::FPlayerWorld Fixture;
    ADWPlayerCharacter* Player = Fixture.Player;
    if (!TestNotNull(TEXT("Transient player created"), Player)) return false;
    Player->TickGameplayClocks(1.25f, true);
    TestEqual(TEXT("Partial sprint retained"), Player->SprintAlcoholElapsed, 1.25f);
    TestEqual(TEXT("No early alcohol"), Player->Inventory->CountItem(TEXT("Alcohol")), 0);
    const float HealthAfterSprint = Player->Health;
    Player->TickGameplayClocks(8.f, false);
    TestEqual(TEXT("Stopping for eight seconds never clears sprint progress"), Player->SprintAlcoholElapsed, 1.25f);
    TestEqual(TEXT("Stopped player pays no sprint health cost"), Player->Health, HealthAfterSprint);
    Player->TickGameplayClocks(1.75f, true);
    TestEqual(TEXT("Separated sprint segments totaling three seconds produce one alcohol"), Player->Inventory->CountItem(TEXT("Alcohol")), 1);
    TestEqual(TEXT("Exact completed interval leaves zero remainder"), Player->SprintAlcoholElapsed, 0.f);

    Player->TickGameplayClocks(2.5f, true);
    UDWSaveGame* Save = NewObject<UDWSaveGame>();
    Player->CaptureSaveData(Save);
    ADWPlayerCharacter* Restored = Fixture.SpawnPlayer(FVector(500.f, 0.f, 120.f));
    if (!TestNotNull(TEXT("Restored player created"), Restored)) return false;
    Restored->ApplySaveData(Save);
    TestEqual(TEXT("Partial sprint survives player save and restore"), Restored->SprintAlcoholElapsed, 2.5f);
    TestEqual(TEXT("Decay clock survives player save and restore"), Restored->TransformationDecayElapsed, Player->TransformationDecayElapsed);
    Restored->TickGameplayClocks(0.5f, true);
    TestEqual(TEXT("Restored progress completes without starting again"), Restored->Inventory->CountItem(TEXT("Alcohol")), 2);
    TestEqual(TEXT("Restored interval consumed once"), Restored->SprintAlcoholElapsed, 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSprintHealthPercentageTest, "DoughWorld.Gameplay.Timers.SprintUsesMaximumHealthPercent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWSprintHealthPercentageTest::RunTest(const FString& Parameters)
{
    DWTimerTests::FPlayerWorld Fixture;
    ADWPlayerCharacter* Player = Fixture.Player;
    if (!TestNotNull(TEXT("Transient player created"), Player)) return false;
    Player->GameplayConfig->MaxHealth = 250.f;
    Player->Health = 200.f;
    Player->TickGameplayClocks(2.f, true);
    TestTrue(TEXT("Two percent per second uses max health, not remaining health"), FMath::IsNearlyEqual(Player->Health, 190.f));
    Player->TickGameplayClocks(4.f, false);
    TestTrue(TEXT("Idle time does not charge health"), FMath::IsNearlyEqual(Player->Health, 190.f));
    Player->GameplayConfig->SprintHealthCostPercentPerSecond = 4.f;
    Player->TickGameplayClocks(0.5f, true);
    TestTrue(TEXT("Edited health cost applies without hardcoded two-percent value"), FMath::IsNearlyEqual(Player->Health, 185.f));
    Player->GameplayConfig->SprintHealthCostPercentPerSecond = 0.f;
    Player->TickGameplayClocks(1.f, true);
    TestTrue(TEXT("Zero configured health cost is supported"), FMath::IsNearlyEqual(Player->Health, 185.f));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWTransformationDecayTest, "DoughWorld.Gameplay.Timers.TransformationDecayAndAttackGain",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWTransformationDecayTest::RunTest(const FString& Parameters)
{
    DWTimerTests::FPlayerWorld Fixture;
    ADWPlayerCharacter* Player = Fixture.Player;
    if (!TestNotNull(TEXT("Transient player created"), Player)) return false;
    Player->GameplayConfig->MaxTransformation = 200.f;
    Player->Transformation = 50.f;
    Player->TickGameplayClocks(4.5f, false);
    TestEqual(TEXT("No decay before complete five-second interval"), Player->Transformation, 50.f);
    Player->TickGameplayClocks(0.5f, false);
    TestEqual(TEXT("Three percent of configurable maximum decays at five seconds"), Player->Transformation, 44.f);
    Player->TickGameplayClocks(10.25f, false);
    TestEqual(TEXT("Long frame accounts for both completed decay intervals"), Player->Transformation, 32.f);
    TestEqual(TEXT("Decay interval remainder retained"), Player->TransformationDecayElapsed, 0.25f);
    Player->NotifyAlcoholHit();
    TestEqual(TEXT("One landed attack adds two percent of configured maximum"), Player->Transformation, 36.f);
    Player->Transformation = 2.f;
    Player->TransformationDecayElapsed = 4.75f;
    Player->TickGameplayClocks(0.25f, false);
    TestEqual(TEXT("Decay never produces a negative transformation value"), Player->Transformation, 0.f);
    Player->Transformation = 199.f;
    Player->NotifyAlcoholHit();
    TestEqual(TEXT("Attack gain clamps to configured maximum"), Player->Transformation, 200.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSprintFullBagTest, "DoughWorld.Gameplay.Timers.FullBagPreservesEarnedProgress",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWSprintFullBagTest::RunTest(const FString& Parameters)
{
    DWTimerTests::FPlayerWorld Fixture;
    ADWPlayerCharacter* Player = Fixture.Player;
    if (!TestNotNull(TEXT("Transient player created"), Player)) return false;
    Player->GameplayConfig->MaxInventorySlots = 1;
    Player->GameplayConfig->MaxStackSize = 40;
    Player->Inventory->TryAddItem(TEXT("Water"), 40);
    Player->TickGameplayClocks(2.9f, true);
    Player->TickGameplayClocks(0.2f, true);
    TestEqual(TEXT("Full bag cannot receive alcohol"), Player->Inventory->CountItem(TEXT("Alcohol")), 0);
    TestTrue(TEXT("Full bag preserves the completed period and fractional remainder"), FMath::IsNearlyEqual(Player->SprintAlcoholElapsed, 3.1f, 0.001f));
    Player->Inventory->TryRemoveItem(TEXT("Water"), 40);
    Player->TickGameplayClocks(0.1f, false);
    TestEqual(TEXT("Earned alcohol delivered after space becomes available"), Player->Inventory->CountItem(TEXT("Alcohol")), 1);
    TestTrue(TEXT("Remaining sprint time survives deferred delivery"), FMath::IsNearlyEqual(Player->SprintAlcoholElapsed, 0.1f, 0.001f));
    return true;
}

#endif
