#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWBuildingAuthoringLibrary.h"
#include "DWGameplayConfig.h"
#include "Engine/DataTable.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingAuthoringTableTruth,"DoughWorld.BuildingAuthoring.LiveTableTruth",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingAuthoringTableTruth::RunTest(const FString&)
{
    auto* C=NewObject<UDWGameplayConfig>();C->Items.Reset();C->Recipes.Reset();
    FDWItemDefinition Stale;Stale.ItemId=TEXT("OldArrayOnly");C->Items.Add(Stale);
    auto* Table=NewObject<UDataTable>();Table->RowStruct=FDWItemDefinition::StaticStruct();C->ItemTable=Table;
    FDWItemDefinition Row;Row.DisplayName=FText::FromString(TEXT("新物品"));Table->AddRow(TEXT("NewTableItem"),Row);
    TArray<FDWItemDefinition> Items;TArray<FDWRecipeDefinition> Recipes;FString Error;
    TestTrue(TEXT("Read table succeeds"),UDWBuildingAuthoringLibrary::ReadEffectiveDefinitions(C,Items,Recipes,Error));
    TestEqual(TEXT("Table replaces stale array"),Items.Num(),1);
    if(Items.Num())TestEqual(TEXT("Empty ID uses row name"),Items[0].ItemId,FName(TEXT("NewTableItem")));
    FDWItemDefinition Another;Another.ItemId=TEXT("AddedWithoutAssetSave");Table->AddRow(TEXT("AnotherRow"),Another);
    TestEqual(TEXT("A row added in memory appears without asset-registry event"),UDWBuildingAuthoringLibrary::GetItemOptions(C).Num(),2);
    Another.ItemId=TEXT("NewTableItem");Table->AddRow(TEXT("DuplicateRow"),Another);
    TestFalse(TEXT("Duplicate effective IDs reject read"),UDWBuildingAuthoringLibrary::ReadEffectiveDefinitions(C,Items,Recipes,Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingAuthoringItemSwitch,"DoughWorld.BuildingAuthoring.ItemSwitchClearsRecipe",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingAuthoringItemSwitch::RunTest(const FString&)
{
    auto* C=NewObject<UDWGameplayConfig>();C->Items.Reset();C->Recipes.Reset();C->ItemTable=nullptr;C->RecipeTable=nullptr;
    FDWItemDefinition A;A.ItemId=TEXT("Furnace");FDWItemDefinition B;B.ItemId=TEXT("CraftingTable");C->Items={A,B};
    FDWRecipeDefinition Recipe;Recipe.RecipeId=TEXT("FurnaceRecipe");Recipe.Inputs={FDWItemStack(TEXT("CraftingTable"),2)};Recipe.Outputs={FDWItemStack(TEXT("Furnace"),1)};C->Recipes.Add(Recipe);
    auto* D=UDWBuildingAuthoringLibrary::CreateDraft(C);
    TestTrue(TEXT("Select existing furnace"),UDWBuildingAuthoringLibrary::SelectExistingItem(D,TEXT("Furnace")));
    TestEqual(TEXT("Matched furnace recipe"),D->RecipeId,FName(TEXT("FurnaceRecipe")));
    TestTrue(TEXT("Select table without recipe"),UDWBuildingAuthoringLibrary::SelectExistingItem(D,TEXT("CraftingTable")));
    TestTrue(TEXT("No stale recipe ID"),D->RecipeId.IsNone());TestTrue(TEXT("No stale ingredients"),D->Ingredients.IsEmpty());
    TestFalse(TEXT("No inherited recipe update authority"),D->bUpdateExistingRecipe);TestFalse(TEXT("No fabricated table recipe"),D->bCreateRecipe);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingAuthoringExplicitPublish,"DoughWorld.BuildingAuthoring.PublishRequiresConfirmation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWBuildingAuthoringExplicitPublish::RunTest(const FString&)
{
    auto* C=NewObject<UDWGameplayConfig>();auto* D=UDWBuildingAuthoringLibrary::CreateDraft(C);const int32 Before=C->Items.Num();
    const auto R=UDWBuildingAuthoringLibrary::PublishDraft(D,false);
    TestFalse(TEXT("Unconfirmed publish fails safely"),R.bSuccess);TestTrue(TEXT("No changed packages"),R.ChangedPackages.IsEmpty());
    TestEqual(TEXT("No item mutation"),C->Items.Num(),Before);return true;
}
#endif
