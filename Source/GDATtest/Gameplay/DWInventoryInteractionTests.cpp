#if WITH_DEV_AUTOMATION_TESTS
#include "DWInventoryComponent.h"
#include "DWGameplayConfig.h"
#include "DWSaveGame.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace DWInventoryUXTests
{
    UDWInventoryComponent* Make(UDWGameplayConfig*& Config,int32 Slots=24,int32 Stack=40)
    {
        Config=NewObject<UDWGameplayConfig>();Config->MaxInventorySlots=Slots;Config->MaxStackSize=Stack;
        UDWInventoryComponent* Bag=NewObject<UDWInventoryComponent>();Bag->InitializeInventory(Config);return Bag;
    }
    bool Same(const TArray<FDWItemStack>& A,const TArray<FDWItemStack>& B)
    {
        if(A.Num()!=B.Num())return false;
        for(int32 I=0;I<A.Num();++I)if(A[I].ItemId!=B[I].ItemId||A[I].Quantity!=B[I].Quantity)return false;
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventorySplitUXTest,"DoughWorld.InventoryUX.AtomicSplitAndCapacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventorySplitUXTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;UDWInventoryComponent* Bag=DWInventoryUXTests::Make(Config,3);
    Bag->TryAddItem(TEXT("Water"),11);int32 NewSlot=INDEX_NONE;
    TestTrue(TEXT("Split one succeeds"),Bag->TrySplitSlot(0,1,NewSlot));TestEqual(TEXT("New slot index"),NewSlot,1);
    TestEqual(TEXT("Source retains ten"),Bag->GetSlots()[0].Quantity,10);TestEqual(TEXT("One in new stack"),Bag->GetSlots()[1].Quantity,1);
    TestTrue(TEXT("Split half succeeds"),Bag->TrySplitSlot(0,5,NewSlot));TestEqual(TEXT("All eleven remain"),Bag->CountItem(TEXT("Water")),11);
    const auto Before=Bag->GetInventorySlots();const int64 Revision=Bag->GetInventoryRevision();
    TestFalse(TEXT("Full bag split rejected"),Bag->TrySplitSlot(0,1,NewSlot));TestEqual(TEXT("Failure clears result index"),NewSlot,INDEX_NONE);
    TestTrue(TEXT("Full bag rejection preserves exact slots"),DWInventoryUXTests::Same(Before,Bag->GetSlots()));TestEqual(TEXT("Failure preserves revision"),Bag->GetInventoryRevision(),Revision);
    TestFalse(TEXT("Cannot split entire stack"),Bag->TrySplitSlot(0,5,NewSlot));TestFalse(TEXT("Cannot split zero"),Bag->TrySplitSlot(0,0,NewSlot));
    TestFalse(TEXT("Cannot split negative"),Bag->TrySplitSlot(0,-1,NewSlot));TestFalse(TEXT("Cannot split invalid slot"),Bag->TrySplitSlot(40,1,NewSlot));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryDiscardUXTest,"DoughWorld.InventoryUX.ConfirmedDiscardRejectsStaleState",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryDiscardUXTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;UDWInventoryComponent* Bag=DWInventoryUXTests::Make(Config);
    Bag->SetSlots({FDWItemStack(TEXT("Water"),7),FDWItemStack(TEXT("Flour"),3),FDWItemStack(TEXT("Water"),12)});
    FDWInventorySlotSnapshot Pending;TestTrue(TEXT("Capture exact third stack"),Bag->CaptureSlotSnapshot(2,Pending));
    TestEqual(TEXT("Showing or cancelling intent consumes nothing"),Bag->CountItem(TEXT("Water")),19);
    TestTrue(TEXT("Confirmed discard succeeds"),Bag->TryDiscardSnapshot(Pending));TestEqual(TEXT("Only chosen stack removed"),Bag->CountItem(TEXT("Water")),7);
    TestEqual(TEXT("Unrelated items preserved"),Bag->CountItem(TEXT("Flour")),3);TestFalse(TEXT("Confirm cannot execute twice"),Bag->TryDiscardSnapshot(Pending));
    Bag->CaptureSlotSnapshot(0,Pending);Bag->TryAddItem(TEXT("Water"),1);Bag->TryRemoveItem(TEXT("Water"),1);
    TestFalse(TEXT("ABA mutation invalidates old intent even when counts match"),Bag->TryDiscardSnapshot(Pending));TestEqual(TEXT("ABA guard loses nothing"),Bag->CountItem(TEXT("Water")),7);
    Bag->CaptureSlotSnapshot(1,Pending);Bag->TryRemoveItem(TEXT("Water"),7);
    TestFalse(TEXT("Compacted slot cannot discard another item"),Bag->TryDiscardSnapshot(Pending));TestEqual(TEXT("Flour remains"),Bag->CountItem(TEXT("Flour")),3);
    TestFalse(TEXT("Invalid slot has no intent"),Bag->CaptureSlotSnapshot(99,Pending));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryTransferUXTest,"DoughWorld.InventoryUX.MergeSwapAndQuickStack",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryTransferUXTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;UDWInventoryComponent* Bag=DWInventoryUXTests::Make(Config);
    Bag->SetSlots({FDWItemStack(TEXT("Water"),30),FDWItemStack(TEXT("Water"),20),FDWItemStack(TEXT("Flour"),8)});
    FDWInventorySlotSnapshot Drag;Bag->CaptureSlotSnapshot(1,Drag);
    TestTrue(TEXT("Drag merges only capacity"),Bag->TryMoveOrMergeSnapshot(Drag,0));TestEqual(TEXT("Target at limit"),Bag->GetSlots()[0].Quantity,40);TestEqual(TEXT("Overflow stays source"),Bag->GetSlots()[1].Quantity,10);
    TestEqual(TEXT("Merge total conserved"),Bag->CountItem(TEXT("Water")),50);Bag->CaptureSlotSnapshot(1,Drag);
    TestFalse(TEXT("Full target does not consume source"),Bag->TryMoveOrMergeSnapshot(Drag,0));TestTrue(TEXT("Different items swap"),Bag->TryMoveOrMergeSnapshot(Drag,2));
    TestEqual(TEXT("Swap moves flour"),Bag->GetSlots()[1].ItemId,FName(TEXT("Flour")));TestEqual(TEXT("Swap moves water"),Bag->GetSlots()[2].Quantity,10);
    Bag->CaptureSlotSnapshot(0,Drag);TestTrue(TEXT("Drop on empty goes compact end"),Bag->TryMoveOrMergeSnapshot(Drag,23));TestEqual(TEXT("Moved stack at end"),Bag->GetSlots().Last().Quantity,40);
    Bag->SetSlots({FDWItemStack(TEXT("Water"),2),FDWItemStack(TEXT("Flour"),3),FDWItemStack(TEXT("Water"),7),FDWItemStack(TEXT("Flour"),4)});
    TestTrue(TEXT("Quick stack chosen item"),Bag->TryQuickStack(TEXT("Water")));TestEqual(TEXT("Water conserved"),Bag->CountItem(TEXT("Water")),9);TestEqual(TEXT("Only water stacks combined"),Bag->GetSlots().Num(),3);
    TestEqual(TEXT("Flour arrangement retained"),Bag->GetSlots()[1].Quantity,3);TestTrue(TEXT("Quick stack all"),Bag->TryQuickStack(NAME_None));TestEqual(TEXT("Two final stacks"),Bag->GetSlots().Num(),2);
    TestEqual(TEXT("Flour conserved"),Bag->CountItem(TEXT("Flour")),7);TestFalse(TEXT("Already combined is harmless"),Bag->TryQuickStack(NAME_None));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventorySaveUXTest,"DoughWorld.InventoryUX.SplitStacksSaveRoundTripInMemory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventorySaveUXTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;UDWInventoryComponent* Bag=DWInventoryUXTests::Make(Config);Bag->TryAddItem(TEXT("Water"),17);int32 Index;
    Bag->TrySplitSlot(0,8,Index);Bag->TrySplitSlot(0,1,Index);
    UDWSaveGame* Save=NewObject<UDWSaveGame>();Save->Inventory=Bag->GetInventorySlots();TArray<uint8> Bytes;
    TestEqual(TEXT("Save format unchanged"),Save->Version,4);TestTrue(TEXT("Serialize entirely in memory"),UGameplayStatics::SaveGameToMemory(Save,Bytes));
    UDWSaveGame* Loaded=Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
    TestNotNull(TEXT("Current save type reloads"),Loaded);if(!Loaded)return false;
    UDWGameplayConfig* OtherConfig;UDWInventoryComponent* Restored=DWInventoryUXTests::Make(OtherConfig);
    TestTrue(TEXT("Saved split layout valid"),Restored->SetSlots(Loaded->Inventory));TestTrue(TEXT("Exact split layout retained"),DWInventoryUXTests::Same(Bag->GetSlots(),Restored->GetSlots()));
    TestEqual(TEXT("Round trip total"),Restored->CountItem(TEXT("Water")),17);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCraftingFormToggleUXTest,"DoughWorld.InventoryUX.CraftingFormGateToggle",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCraftingFormToggleUXTest::RunTest(const FString& Parameters)
{
    UDWGameplayConfig* Config;UDWInventoryComponent* Bag=DWInventoryUXTests::Make(Config);
    FDWRecipeDefinition Recipe;Recipe.RecipeId=TEXT("UXFormGate");Recipe.bRequiresYeast=true;Recipe.Inputs={FDWItemStack(TEXT("Water"),1)};Recipe.Outputs={FDWItemStack(TEXT("Flour"),1)};Config->Recipes.Add(Recipe);
    Bag->TryAddItem(TEXT("Water"),4);TestFalse(TEXT("New config defaults gate off"),Config->bRequireTransformationForCrafting);
    TestTrue(TEXT("Gate off allows dough form preview"),Bag->CanCraft(Recipe.RecipeId,false));TestTrue(TEXT("Gate off allows dough form craft"),Bag->TryCraft(Recipe.RecipeId,false));
    Config->bRequireTransformationForCrafting=true;const auto Before=Bag->GetInventorySlots();
    TestFalse(TEXT("Gate on blocks dough preview"),Bag->CanCraft(Recipe.RecipeId,false));TestFalse(TEXT("Gate on blocks dough craft"),Bag->TryCraft(Recipe.RecipeId,false));TestTrue(TEXT("Denied craft consumes nothing"),DWInventoryUXTests::Same(Before,Bag->GetSlots()));
    TestTrue(TEXT("Gate on permits yeast"),Bag->TryCraft(Recipe.RecipeId,true));
    Config->Recipes.Last().bRequiresYeast=false;TestTrue(TEXT("Recipe without form flag remains available"),Bag->TryCraft(Recipe.RecipeId,false));
    Config->Recipes.Last().bRequiresYeast=true;Config->bRequireTransformationForCrafting=false;
    TestTrue(TEXT("Gate off remains available to yeast"),Bag->TryCraft(Recipe.RecipeId,true));TestEqual(TEXT("Exactly four outputs"),Bag->CountItem(TEXT("Flour")),4);
    return true;
}
#endif
