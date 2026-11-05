#include "DWCraftingTypes.h"
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWBuildingDefinition.h"
#include "UObject/StrongObjectPtr.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCraftViewPolicyTest,"DoughWorld.UI.Crafting.ClassificationAndLayout",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCraftViewPolicyTest::RunTest(const FString&)
{
 using namespace DWCrafting;
 FCategoryFacts Facts;Facts.Material=true;Facts.Weapon=true;
 TestTrue(TEXT("Real weapon role beats ingredient inference"),ResolveCategory(EDWItemCategory::Auto,Facts)==EDWCraftingCategory::Weapons);
 TestTrue(TEXT("Explicit item category wins"),ResolveCategory(EDWItemCategory::Material,Facts)==EDWCraftingCategory::Materials);
 TestTrue(TEXT("Unknown visible in Other"),ResolveCategory(EDWItemCategory::Auto,{})==EDWCraftingCategory::Other);
 TestTrue(TEXT("Materials actually filter"),Matches(EDWCraftingCategory::Materials,{EDWCraftingCategory::Materials,EDWCraftingCategory::Tools}));
 TestFalse(TEXT("Weapon tab excludes pure materials"),Matches(EDWCraftingCategory::Weapons,{EDWCraftingCategory::Materials}));
 TestTrue(TEXT("All keeps other and multi-output recipes"),Matches(EDWCraftingCategory::All,{EDWCraftingCategory::Other}));
 for(int32 I=0;I<12;++I)
 {const auto R=IngredientCell(I);TestTrue(TEXT("Two-column card stays inside content width with scroll clearance"),R.X>=0&&R.X+R.W<=484);for(int32 J=0;J<I;++J)TestFalse(TEXT("Material cards never overlap"),Overlaps(R,IngredientCell(J)));}
 TestEqual(TEXT("Recipe fills still match the live bag"),BackpackSlotExtent,88.f);
 TestEqual(TEXT("Artwork viewport matches the live bag"),BackpackIconExtent,64.f);
 TestTrue(TEXT("Five padded recipe slots leave scroll clearance"),RecipeColumns*BackpackCellExtent<=504.f);
 TestEqual(TEXT("Compact material square retains six-pixel artwork padding"),MaterialSlotExtent,MaterialIconExtent+12.f);
 TestEqual(TEXT("Compact material card retains seven-pixel top/bottom padding"),MaterialCardHeight,MaterialSlotExtent+14.f);
 TestEqual(TEXT("Material labels have a separate 162px-wide region"),MaterialCardWidth-14.f-MaterialSlotExtent-10.f,162.f);
 TestTrue(TEXT("Five or six costs fit without scrolling including trailing grid padding"),MaterialRowsWithoutScroll*MaterialRowStride<=MaterialViewportHeight);
 TestTrue(TEXT("Seven costs have a bounded scroll continuation, not invisible truncation"),4*MaterialRowStride>MaterialViewportHeight);
 TArray<FDWItemStack> Scaled;
 TestTrue(TEXT("Legacy sugar aliases aggregate before multiplying"),Scale({FDWItemStack(TEXT("SugarChunk"),2),FDWItemStack(TEXT("HardSugarChunk"),1)},3,true,Scaled));
 TestEqual(TEXT("Single real cost row"),Scaled.Num(),1);if(Scaled.Num())TestEqual(TEXT("Combined need"),Scaled[0].Quantity,9);
 TestFalse(TEXT("Invalid count rejected"),Scale({FDWItemStack(TEXT("Flour"),1)},0,true,Scaled));TestTrue(TEXT("No partial invalid projection"),Scaled.IsEmpty());
 TestFalse(TEXT("Multiplication overflow rejected"),Scale({FDWItemStack(TEXT("Flour"),MAX_int32)},99,true,Scaled));
 TestFalse(TEXT("Invalid later row leaves no partial output"),Scale({FDWItemStack(TEXT("Flour"),1),FDWItemStack(NAME_None,1)},2,true,Scaled));TestTrue(TEXT("Output empty after bad later row"),Scaled.IsEmpty());
 return true;
}

namespace
{
struct FBag
{
 TStrongObjectPtr<UDWGameplayConfig> C{NewObject<UDWGameplayConfig>()};TStrongObjectPtr<UDWInventoryComponent> Bag{NewObject<UDWInventoryComponent>()};
 FBag(int32 Slots=6,int32 Limit=40)
 {
  C->Items.Reset();C->Recipes.Reset();C->ItemTable=nullptr;C->RecipeTable=nullptr;C->MaxInventorySlots=Slots;C->MaxStackSize=Limit;C->bRequireTransformationForCrafting=false;
  for(FName Id:{FName(TEXT("SugarChunk")),FName(TEXT("HardSugarChunk")),FName(TEXT("Flour")),FName(TEXT("Dough")),FName(TEXT("Water")),FName(TEXT("DWWeapon_Spear"))})
  {FDWItemDefinition I;I.ItemId=Id;C->Items.Add(I);}
  FDWRecipeDefinition R;R.RecipeId=TEXT("CraftBatchFixture");R.Inputs={FDWItemStack(TEXT("SugarChunk"),2),FDWItemStack(TEXT("HardSugarChunk"),1)};R.Outputs={FDWItemStack(TEXT("Dough"),1)};R.bRequiresYeast=false;C->Recipes.Add(R);Bag->InitializeInventory(C.Get());
 }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCraftBatchAtomicTest,"DoughWorld.Inventory.Crafting.BatchAtomic",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCraftBatchAtomicTest::RunTest(const FString&)
{
 FBag F;F.Bag->TryAddItem(TEXT("SugarChunk"),4);F.Bag->TryAddItem(TEXT("HardSugarChunk"),2);
 const int64 Revision=F.Bag->GetInventoryRevision();
 TestTrue(TEXT("Batch 2 preflight accepts mixed sugar stock"),F.Bag->CanCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2));
 TestEqual(TEXT("Preview never mutates inventory"),F.Bag->GetInventoryRevision(),Revision);
 TestTrue(TEXT("Batch commits all at once"),F.Bag->TryCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2));
 TestEqual(TEXT("One inventory revision, not two crafts"),F.Bag->GetInventoryRevision(),Revision+1);
 TestEqual(TEXT("Both outputs"),F.Bag->CountItem(TEXT("Dough")),2);
 TestEqual(TEXT("Whole canonical cost paid"),F.Bag->CountCraftingMaterial(TEXT("SugarChunk")),0);
 const auto After=F.Bag->GetInventoryRevision();
 TestFalse(TEXT("Missing costs fail whole batch"),F.Bag->TryCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2));TestEqual(TEXT("Failure no commit"),F.Bag->GetInventoryRevision(),After);
 FBag Stale;Stale.Bag->TryAddItem(TEXT("SugarChunk"),12);FDWPreparedInventoryChange Plan;
 TestTrue(TEXT("Prepare batch"),Stale.Bag->PrepareCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2,Plan));
 Stale.Bag->TryAddItem(TEXT("Water"),1);
 TestFalse(TEXT("Stale inventory token rejected"),Stale.Bag->CommitPrepared(MoveTemp(Plan)));TestEqual(TEXT("No stale cost removed"),Stale.Bag->CountCraftingMaterial(TEXT("SugarChunk")),12);
 FDWPreparedInventoryChange Changed;TestTrue(TEXT("Prepare again"),Stale.Bag->PrepareCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2,Changed));
 Stale.C->Recipes[0].Inputs[0].Quantity=3;
 TestFalse(TEXT("Changed recipe data rejected at commit"),Stale.Bag->CommitPrepared(MoveTemp(Changed)));TestEqual(TEXT("No changed-cost partial commit"),Stale.Bag->CountCraftingMaterial(TEXT("SugarChunk")),12);
 FBag Capacity(1,3);Capacity.C->Recipes[0].Inputs={FDWItemStack(TEXT("Flour"),1)};Capacity.Bag->TryAddItem(TEXT("Flour"),3);
 TestFalse(TEXT("Capacity checked for whole batch after input removal"),Capacity.Bag->TryCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2));
 TestEqual(TEXT("Capacity failure preserves all ingredients"),Capacity.Bag->CountItem(TEXT("Flour")),3);
 FBag Weapon;Weapon.C->Recipes[0].Outputs={FDWItemStack(TEXT("DWWeapon_Spear"),1)};Weapon.Bag->TryAddItem(TEXT("SugarChunk"),12);
 TestFalse(TEXT("Permanent weapons cannot be batch-created"),Weapon.Bag->CanCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2));
 TestFalse(TEXT("No unbounded batch count"),Weapon.Bag->CanCraftRecipeBatch(TEXT("CraftBatchFixture"),false,100));
 FBag Form;Form.Bag->TryAddItem(TEXT("SugarChunk"),12);Form.C->bRequireTransformationForCrafting=true;Form.C->Recipes[0].bRequiresYeast=true;
 TestFalse(TEXT("Whole batch respects yeast requirement"),Form.Bag->CanCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2));
 TestTrue(TEXT("Correct form can prepare"),Form.Bag->CanCraftRecipeBatch(TEXT("CraftBatchFixture"),true,2));
 FDWPreparedInventoryChange FormPlan;TestTrue(TEXT("Prepare before form policy change"),Form.Bag->PrepareCraftRecipeBatch(TEXT("CraftBatchFixture"),true,2,FormPlan));
 Form.C->Recipes[0].Outputs[0].Quantity=2;
 TestFalse(TEXT("Changed output policy rejects stale projection"),Form.Bag->CommitPrepared(MoveTemp(FormPlan)));
 FBag Single;Single.Bag->TryAddItem(TEXT("SugarChunk"),3);
 TestTrue(TEXT("Count one uses original single crafting path"),Single.Bag->TryCraftRecipeBatch(TEXT("CraftBatchFixture"),false,1));
 TestEqual(TEXT("Single output unchanged"),Single.Bag->CountItem(TEXT("Dough")),1);
 FBag Station;Station.C->bEnableGatheringRules=true;Station.C->RecipeStations.Add(TEXT("CraftBatchFixture"),TEXT("CraftingTable"));Station.Bag->TryAddItem(TEXT("SugarChunk"),12);
 bool Nearby=true;Station.Bag->SetCraftingContextValidator([&Nearby](FName,FName,FName){return Nearby;});
 FDWPreparedInventoryChange StationPlan;TestTrue(TEXT("Station permits prepare"),Station.Bag->PrepareCraftRecipeBatch(TEXT("CraftBatchFixture"),false,2,StationPlan));
 const auto StationRevision=Station.Bag->GetInventoryRevision();Nearby=false;
 TestFalse(TEXT("Station lost without inventory revision rejects commit"),Station.Bag->CommitPrepared(MoveTemp(StationPlan)));
 TestEqual(TEXT("Station rejection preserves all costs"),Station.Bag->CountCraftingMaterial(TEXT("SugarChunk")),12);
 TestEqual(TEXT("Station rejection no inventory publication"),Station.Bag->GetInventoryRevision(),StationRevision);
 return true;
}
#endif
