#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "UObject/StrongObjectPtr.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWSugarCraftAliasTest,"DoughWorld.Items.Sugar.CraftAliasWithoutWorld",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWSugarCraftAliasTest::RunTest(const FString&)
{
    TStrongObjectPtr<UDWGameplayConfig> Config(NewObject<UDWGameplayConfig>());
    Config->MaxInventorySlots=4;Config->MaxStackSize=10;
    for(FName Id:{FName(TEXT("SugarChunk")),FName(TEXT("HardSugarChunk")),FName(TEXT("QAProduct"))})
    {FDWItemDefinition D;D.ItemId=Id;Config->Items.Add(D);}
    FDWRecipeDefinition Recipe;Recipe.RecipeId=TEXT("QASugarRecipe");Recipe.bRequiresYeast=false;
    Recipe.Inputs={{TEXT("SugarChunk"),3}};Recipe.Outputs={{TEXT("QAProduct"),1}};Config->Recipes.Add(Recipe);
    TStrongObjectPtr<UDWInventoryComponent> Bag(NewObject<UDWInventoryComponent>());Bag->InitializeInventory(Config.Get());
    if(!TestTrue(TEXT("Mixed legacy/current fixture"),Bag->SetSlots({{TEXT("SugarChunk"),1},{TEXT("HardSugarChunk"),3}})))return false;
    TestEqual(TEXT("Exact identity count contract retained"),Bag->CountItem(TEXT("SugarChunk")),1);
    TestEqual(TEXT("Craft availability includes legacy units"),Bag->CountCraftingMaterial(TEXT("SugarChunk")),4);
    FDWInventorySlotSnapshot Old;Bag->CaptureSlotSnapshot(0,Old);
    FDWPreparedInventoryChange Plan;
    TestTrue(TEXT("Prepare mixed payment"),Bag->PrepareCraftRecipe(Recipe.RecipeId,false,Plan));
    TestEqual(TEXT("Preparation does not spend"),Bag->CountCraftingMaterial(TEXT("SugarChunk")),4);
    TestTrue(TEXT("Commit exact three once"),Bag->CommitPrepared(MoveTemp(Plan)));
    TestEqual(TEXT("Conserved remainder"),Bag->CountCraftingMaterial(TEXT("SugarChunk")),1);
    TestEqual(TEXT("Output once"),Bag->CountItem(TEXT("QAProduct")),1);
    TestFalse(TEXT("Old slot still rejected"),Bag->IsSlotSnapshotCurrent(Old));
    TestFalse(TEXT("Moved token cannot replay"),Bag->CommitPrepared(MoveTemp(Plan)));
    TestFalse(TEXT("Insufficient alias pool cannot craft"),Bag->TryCraft(Recipe.RecipeId,false));
    TestEqual(TEXT("Failed craft preserves remainder"),Bag->CountCraftingMaterial(TEXT("SugarChunk")),1);
    Bag->SetSlots({{TEXT("HardSugarChunk"),3}});
    TestTrue(TEXT("Old saved stack usable without migration"),Bag->TryCraft(Recipe.RecipeId,false));
    TestEqual(TEXT("No old units lost or duplicated"),Bag->CountItem(TEXT("QAProduct")),1);
    Bag->SetSlots({{TEXT("SugarChunk"),3}});FDWPreparedInventoryChange Stale;
    TestTrue(TEXT("Prepare revision test"),Bag->PrepareCraftRecipe(Recipe.RecipeId,false,Stale));
    Bag->TryAddItem(TEXT("Flour"),1);
    TestFalse(TEXT("Changed revision rejects whole craft"),Bag->CommitPrepared(MoveTemp(Stale)));
    TestEqual(TEXT("Revision rejection preserves sugar"),Bag->CountItem(TEXT("SugarChunk")),3);
    TestFalse(TEXT("Unknown identity remains invalid"),Bag->SetSlots({{TEXT("QAUnknown"),1}}));
    return true;
}
#endif
