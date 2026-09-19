#if WITH_DEV_AUTOMATION_TESTS

#include "DWGameInstance.h"
#include "DWGameplayConfig.h"
#include "DWInventoryComponent.h"
#include "DWSaveGame.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace DWCoreTests
{
    UDWInventoryComponent* MakeInventory(UDWGameplayConfig*& OutConfig, int32 Slots = 24, int32 StackSize = 40)
    {
        OutConfig = NewObject<UDWGameplayConfig>();
        OutConfig->MaxInventorySlots = Slots;
        OutConfig->MaxStackSize = StackSize;
        UDWInventoryComponent* Inventory = NewObject<UDWInventoryComponent>();
        Inventory->InitializeInventory(OutConfig);
        return Inventory;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryAtomicTest, "DoughWorld.Core.Inventory.AtomicStacking",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWInventoryAtomicTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;
    UDWInventoryComponent* Inventory = DWCoreTests::MakeInventory(Config, 2, 40);
    TestTrue(TEXT("Initial 39 items fit"), Inventory->TryAddItem(TEXT("Water"), 39));
    TestTrue(TEXT("Additional 2 spill into second slot"), Inventory->TryAddItem(TEXT("Water"), 2));
    TestEqual(TEXT("Two slots used"), Inventory->GetSlots().Num(), 2);
    TestEqual(TEXT("First stack capped at 40"), Inventory->GetSlots()[0].Quantity, 40);
    TestEqual(TEXT("Overflow preserved"), Inventory->GetSlots()[1].Quantity, 1);
    TestFalse(TEXT("Full-capacity failure is atomic"), Inventory->TryAddItem(TEXT("Water"), 40));
    TestEqual(TEXT("No partial addition on failure"), Inventory->CountItem(TEXT("Water")), 41);
    TestFalse(TEXT("Different item cannot occupy full slots"), Inventory->TryAddItem(TEXT("Yeast"), 1));
    TestFalse(TEXT("Cannot remove more than owned"), Inventory->TryRemoveItem(TEXT("Water"), 42));
    TestEqual(TEXT("Failed removal preserves all items"), Inventory->CountItem(TEXT("Water")), 41);
    TestTrue(TEXT("Removal spans stacks"), Inventory->TryRemoveItem(TEXT("Water"), 40));
    TestEqual(TEXT("Empty stacks compacted"), Inventory->GetSlots().Num(), 1);
    TestEqual(TEXT("One water remains"), Inventory->CountItem(TEXT("Water")), 1);
    TestFalse(TEXT("Unknown item rejected"), Inventory->TryAddItem(TEXT("Undefined"), 1));
    TestFalse(TEXT("Negative amount rejected"), Inventory->TryAddItem(TEXT("Water"), -1));
    TestFalse(TEXT("Zero amount rejected"), Inventory->TryRemoveItem(TEXT("Water"), 0));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCraftingAtomicTest, "DoughWorld.Core.Crafting.AtomicRecipes",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWCraftingAtomicTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;
    UDWInventoryComponent* Inventory = DWCoreTests::MakeInventory(Config, 2, 40);
    Inventory->TryAddItem(TEXT("Flour"), 2);
    Inventory->TryAddItem(TEXT("Water"), 2);
    TestFalse(TEXT("Dough form cannot craft yeast recipe"), Inventory->TryCraft(TEXT("CraftDough"), false));
    TestEqual(TEXT("Form denial consumes no flour"), Inventory->CountItem(TEXT("Flour")), 2);
    TestTrue(TEXT("Craft uses space freed by consumed inputs"), Inventory->TryCraft(TEXT("CraftDough"), true));
    TestEqual(TEXT("Exactly one dough made"), Inventory->CountItem(TEXT("Dough")), 1);
    TestEqual(TEXT("All flour consumed"), Inventory->CountItem(TEXT("Flour")), 0);
    TestEqual(TEXT("All water consumed"), Inventory->CountItem(TEXT("Water")), 0);

    Inventory->SetSlots({FDWItemStack(TEXT("Flour"), 40), FDWItemStack(TEXT("Water"), 40)});
    TestFalse(TEXT("Output must fit after partial ingredient removal"), Inventory->TryCraft(TEXT("CraftDough"), true));
    TestEqual(TEXT("Output-full failure preserves flour"), Inventory->CountItem(TEXT("Flour")), 40);
    TestEqual(TEXT("Output-full failure preserves water"), Inventory->CountItem(TEXT("Water")), 40);

    FDWRecipeDefinition Recycling;
    Recycling.RecipeId = TEXT("RecycleWater");
    Recycling.Inputs = {FDWItemStack(TEXT("Water"), 3), FDWItemStack(TEXT("Water"), 2)};
    Recycling.Outputs = {FDWItemStack(TEXT("Water"), 5)};
    Config->Recipes.Add(Recycling);
    Inventory->SetSlots({FDWItemStack(TEXT("Water"), 4)});
    TestFalse(TEXT("Duplicate recipe inputs require their combined amount"), Inventory->TryCraft(TEXT("RecycleWater"), true));
    TestEqual(TEXT("Duplicate-input failure atomic"), Inventory->CountItem(TEXT("Water")), 4);
    Inventory->SetSlots({FDWItemStack(TEXT("Water"), 40), FDWItemStack(TEXT("Flour"), 40)});
    TestTrue(TEXT("Same-item output correctly reuses ingredient capacity"), Inventory->TryCraft(TEXT("RecycleWater"), true));
    TestEqual(TEXT("Recycled amount unchanged"), Inventory->CountItem(TEXT("Water")), 40);

    Inventory->SetSlots({FDWItemStack(TEXT("Dough"), 3), FDWItemStack(TEXT("Yeast"), 2)});
    TestTrue(TEXT("Alcohol recipe executes"), Inventory->TryCraft(TEXT("CraftAlcohol"), true));
    TestEqual(TEXT("One alcohol made"), Inventory->CountItem(TEXT("Alcohol")), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryLoadAndUseTest, "DoughWorld.Core.Inventory.LoadValidationAndUse",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWInventoryLoadAndUseTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;
    UDWInventoryComponent* Inventory = DWCoreTests::MakeInventory(Config, 2, 40);
    Inventory->TryAddItem(TEXT("Dough"), 2);
    TestFalse(TEXT("Over-limit stack save rejected"), Inventory->SetSlots({FDWItemStack(TEXT("Water"), 41)}));
    TestFalse(TEXT("Unknown saved item rejected"), Inventory->SetSlots({FDWItemStack(TEXT("Wood"), 1)}));
    TestFalse(TEXT("Negative saved item rejected"), Inventory->SetSlots({FDWItemStack(TEXT("Water"), -1)}));
    TestFalse(TEXT("Over-capacity save rejected"), Inventory->SetSlots({FDWItemStack(TEXT("Water"), 1), FDWItemStack(TEXT("Yeast"), 1), FDWItemStack(TEXT("Flour"), 1)}));
    TestEqual(TEXT("Bad loads never discard existing contents"), Inventory->CountItem(TEXT("Dough")), 2);
    TestFalse(TEXT("Invalid use slot rejected"), Inventory->ConsumeOneAtSlot(-1));
    TestTrue(TEXT("Dough use consumes exactly one"), Inventory->ConsumeOneAtSlot(0));
    TestEqual(TEXT("One dough left"), Inventory->CountItem(TEXT("Dough")), 1);
    Inventory->SetSlots({FDWItemStack(TEXT("Water"), 1)});
    TestFalse(TEXT("Non-usable material not consumed"), Inventory->ConsumeOneAtSlot(0));
    TestEqual(TEXT("Water kept"), Inventory->CountItem(TEXT("Water")), 1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWDataTableConfigTest, "DoughWorld.Core.Config.DataTableOverrides",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWDataTableConfigTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config = NewObject<UDWGameplayConfig>();
    TestEqual(TEXT("Five default items"), Config->Items.Num(), 5);
    TestEqual(TEXT("Two default recipes"), Config->Recipes.Num(), 2);
    TestTrue(TEXT("Every default recipe yeast-only"), Config->Recipes[0].bRequiresYeast && Config->Recipes[1].bRequiresYeast);
    UDataTable* ItemTable = NewObject<UDataTable>();
    ItemTable->RowStruct = FDWItemDefinition::StaticStruct();
    for (const FDWItemDefinition& Item : Config->Items)
    {
        FDWItemDefinition Row = Item;
        if (Row.ItemId == TEXT("Dough")) Row.HealAmount = 17.f;
        ItemTable->AddRow(Row.ItemId, Row);
    }
    UDataTable* RecipeTable = NewObject<UDataTable>();
    RecipeTable->RowStruct = FDWRecipeDefinition::StaticStruct();
    for (const FDWRecipeDefinition& Recipe : Config->Recipes)
    {
        FDWRecipeDefinition Row = Recipe;
        if (Row.RecipeId == TEXT("CraftDough")) Row.Outputs[0].Quantity = 2;
        RecipeTable->AddRow(Row.RecipeId, Row);
    }
    Config->ItemTable = ItemTable;
    Config->RecipeTable = RecipeTable;
    Config->RefreshDefinitionsFromTables();
    TestEqual(TEXT("Item table controls use amount"), Config->GetItemDefinition(TEXT("Dough"))->HealAmount, 17.f);
    TestEqual(TEXT("Recipe table controls output amount"), Config->GetRecipeDefinition(TEXT("CraftDough"))->Outputs[0].Quantity, 2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSaveRoundTripTest, "DoughWorld.Core.Save.MemoryRoundTrip",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWSaveRoundTripTest::RunTest(const FString& Parameters)
{
    UDWSaveGame* Save = NewObject<UDWSaveGame>();
    Save->DisplayName = TEXT("断续疾跑测试");
    Save->Timestamp = FDateTime::UtcNow();
    Save->MapPackage = TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_GameplayPrototype");
    Save->Health = 71.5f;
    Save->Transformation = 62.f;
    Save->SprintAlcoholElapsed = 2.75f;
    Save->TransformationDecayElapsed = 4.5f;
    Save->bYeastForm = true;
    Save->bHasPlayerTransform = true;
    Save->PlayerTransform = FTransform(FRotator(0.f, 35.f, 0.f), FVector(175.f, -30.f, 98.f));
    Save->Inventory = {FDWItemStack(TEXT("Alcohol"), 40), FDWItemStack(TEXT("Water"), 7)};
    FDWPersistedActorState Actor;
    Actor.ActorId = TEXT("TestNest");
    Actor.Health = 125.f;
    Actor.bDestroyed = false;
    Save->WorldActors.Add(Actor);
    TArray<uint8> Bytes;
    TestTrue(TEXT("Save serializes entirely in memory"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
    UDWSaveGame* Loaded = Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    if (!TestNotNull(TEXT("Save class restores"), Loaded)) return false;
    TestEqual(TEXT("Health persists"), Loaded->Health, 71.5f);
    TestEqual(TEXT("Transformation persists"), Loaded->Transformation, 62.f);
    TestEqual(TEXT("Partial sprint progress persists"), Loaded->SprintAlcoholElapsed, 2.75f);
    TestEqual(TEXT("Partial decay progress persists"), Loaded->TransformationDecayElapsed, 4.5f);
    TestTrue(TEXT("Form persists"), Loaded->bYeastForm);
    TestTrue(TEXT("Position persists"), Loaded->PlayerTransform.Equals(Save->PlayerTransform));
    TestEqual(TEXT("Two stacks restore"), Loaded->Inventory.Num(), 2);
    if (Loaded->Inventory.Num() == 2) TestEqual(TEXT("Stack amount restores"), Loaded->Inventory[0].Quantity, 40);
    TestEqual(TEXT("World actor state restores"), Loaded->WorldActors.Num(), 1);
    if (Loaded->WorldActors.Num() == 1) TestEqual(TEXT("Nest damage restores"), Loaded->WorldActors[0].Health, 125.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSaveSlotBoundsTest, "DoughWorld.Core.Save.SlotBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWSaveSlotBoundsTest::RunTest(const FString& Parameters)
{
    UDWGameInstance* Instance = NewObject<UDWGameInstance>();
    TestFalse(TEXT("No active slot at startup"), Instance->HasActiveSlot());
    TestEqual(TEXT("Exactly three supported slots"), UDWGameInstance::SaveSlotCount, 3);
    TestFalse(TEXT("Negative create index rejected before file access"), Instance->NewGame(-1, TEXT("Test")));
    TestFalse(TEXT("Fourth create slot rejected before file access"), Instance->NewGame(3, TEXT("Test")));
    TestFalse(TEXT("Fourth load slot rejected before file access"), Instance->LoadGameSlot(3));
    TestFalse(TEXT("Negative delete index rejected before file access"), Instance->DeleteGameSlot(-1));
    TestFalse(TEXT("Title cannot save into any slot"), Instance->SaveCurrentGame());
    return true;
}

#endif
