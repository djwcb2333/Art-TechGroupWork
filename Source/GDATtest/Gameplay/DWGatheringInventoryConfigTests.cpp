#if WITH_DEV_AUTOMATION_TESTS
#include "DWGatheringProfile.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWGameInstance.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

namespace DWGatheringConfigTests
{
    struct FFixture
    {
        TStrongObjectPtr<UDWGatheringProfile> Profile{NewObject<UDWGatheringProfile>()};
        TStrongObjectPtr<UDWInventoryComponent> Bag{NewObject<UDWInventoryComponent>()};
        FFixture()
        {
            Profile->Items.Reset();Profile->Recipes.Reset();Profile->ItemTable=nullptr;Profile->RecipeTable=nullptr;
            Profile->MaxInventorySlots=2;Profile->MaxStackSize=40;Profile->bRequireTransformationForCrafting=false;
            for(FName Id:{FName(TEXT("Flour")),FName(TEXT("Water")),FName(TEXT("Dough")),FName(TEXT("Furnace")),FName(TEXT("DW_QA_Core"))})
            {FDWItemDefinition Item;Item.ItemId=Id;Profile->Items.Add(Item);}
            Profile->StackLimits.Add(TEXT("Flour"),10);Profile->StackLimits.Add(TEXT("Water"),10);Profile->StackLimits.Add(TEXT("Dough"),10);
            Profile->NonInventoryItems.Add(TEXT("Furnace"));
            const auto AddRecipe=[&](FName Id,int32 Flour,FName Output)
            {FDWRecipeDefinition R;R.RecipeId=Id;R.Inputs={FDWItemStack(TEXT("Flour"),Flour)};R.Outputs={FDWItemStack(Output,1)};Profile->Recipes.Add(R);};
            AddRecipe(TEXT("Hand"),2,TEXT("Dough"));AddRecipe(TEXT("Station"),3,TEXT("Dough"));AddRecipe(TEXT("Build"),5,TEXT("Furnace"));
            Profile->RecipeStations.Add(TEXT("Station"),TEXT("GatherStation_CraftingTable"));
            Profile->RecipeBuildings.Add(TEXT("Build"),TEXT("Furnace"));
            Bag->InitializeInventory(Profile.Get());
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringIsolatedProfileLimits,"DoughWorld.Gathering.Configuration.IsolatedCapsAndBuildings",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringIsolatedProfileLimits::RunTest(const FString&)
{
    using namespace DWGatheringConfigTests;FFixture F;FString Error;
    TestTrue(TEXT("Profile definitions valid"),F.Profile->ValidateGatheringDefinitions(Error));
    TestEqual(TEXT("New profile flour cap"),F.Bag->GetEffectiveStackLimit(TEXT("Flour")),10);
    TestEqual(TEXT("Unrelated cap remains original"),F.Bag->GetEffectiveStackLimit(TEXT("DW_QA_Core")),40);
    TMap<FName,int32> Runtime;Runtime.Add(TEXT("DW_QA_Core"),3);
    TestTrue(TEXT("Explicit combat-style override accepted"),F.Bag->TrySetStackLimitOverrides(Runtime));
    TestEqual(TEXT("Core override retained"),F.Bag->GetEffectiveStackLimit(TEXT("DW_QA_Core")),3);
    TestEqual(TEXT("Flour profile cap unaffected by core override"),F.Bag->GetEffectiveStackLimit(TEXT("Flour")),10);
    TestFalse(TEXT("Over-cap save stack rejected"),F.Bag->SetSlots({FDWItemStack(TEXT("Flour"),11)}));
    TestFalse(TEXT("Building cannot enter bag"),F.Bag->TryAddItem(TEXT("Furnace"),1));
    TestFalse(TEXT("Building cannot enter through saved slots"),F.Bag->SetSlots({FDWItemStack(TEXT("Furnace"),1)}));
    TestEqual(TEXT("Building capacity zero"),F.Bag->GetAvailableCapacity(TEXT("Furnace")),0);
    TStrongObjectPtr<UDWGameplayConfig> Original(NewObject<UDWGameplayConfig>());
    TStrongObjectPtr<UDWInventoryComponent> OriginalBag(NewObject<UDWInventoryComponent>());
    OriginalBag->InitializeInventory(Original.Get());
    TestEqual(TEXT("Original config cap unchanged"),OriginalBag->GetEffectiveStackLimit(TEXT("Flour")),40);
    TestTrue(TEXT("Original saved flour40 still valid"),OriginalBag->SetSlots({FDWItemStack(TEXT("Flour"),40)}));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPartialCapacity,"DoughWorld.Gathering.Configuration.PartialPickupCapacity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPartialCapacity::RunTest(const FString&)
{
    using namespace DWGatheringConfigTests;FFixture F;
    TestTrue(TEXT("Prepare nearly full bag"),F.Bag->SetSlots({FDWItemStack(TEXT("Flour"),9),FDWItemStack(TEXT("Water"),10)}));
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    int32 Notices=0;Probe->TestHook=[&](){++Notices;};F.Bag->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestEqual(TEXT("Existing partial stack capacity"),F.Bag->GetAvailableCapacity(TEXT("Flour")),1);
    TestEqual(TEXT("Full other item capacity"),F.Bag->GetAvailableCapacity(TEXT("Water")),0);
    FDWPreparedInventoryChange Plan;int32 Accepted=99;
    TestTrue(TEXT("Prepare partial pickup"),F.Bag->PrepareAddItemUpTo(TEXT("Flour"),5,Accepted,Plan));
    TestEqual(TEXT("Exactly one accepted candidate"),Accepted,1);
    TestEqual(TEXT("Preparation does not change bag"),F.Bag->CountItem(TEXT("Flour")),9);
    TestEqual(TEXT("Preparation emits no UI notice"),Notices,0);
    TestTrue(TEXT("Single commit accepted"),F.Bag->CommitPrepared(MoveTemp(Plan)));
    TestEqual(TEXT("One coherent inventory notice"),Notices,1);
    TestEqual(TEXT("Bag reaches cap"),F.Bag->CountItem(TEXT("Flour")),10);
    TestFalse(TEXT("Cannot replay candidate"),F.Bag->CommitPrepared(MoveTemp(Plan)));
    TestFalse(TEXT("Full bag partial request rejected"),F.Bag->TryAddItemUpTo(TEXT("Flour"),5,Accepted));
    TestEqual(TEXT("Rejected request returns zero"),Accepted,0);
    TestEqual(TEXT("Rejected request emits nothing"),Notices,1);
    TestFalse(TEXT("Negative amount rejected"),F.Bag->PrepareAddItemUpTo(TEXT("Flour"),-1,Accepted,Plan));
    TestFalse(TEXT("Unknown item rejected"),F.Bag->PrepareAddItemUpTo(TEXT("Missing"),1,Accepted,Plan));
    TestFalse(TEXT("Building partial add rejected"),F.Bag->PrepareAddItemUpTo(TEXT("Furnace"),1,Accepted,Plan));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringCraftContext,"DoughWorld.Gathering.Configuration.CraftContextAndPreparedBuilding",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringCraftContext::RunTest(const FString&)
{
    using namespace DWGatheringConfigTests;FFixture F;
    TestTrue(TEXT("Materials set"),F.Bag->SetSlots({FDWItemStack(TEXT("Flour"),10)}));
    TestTrue(TEXT("Handcraft requires no hook"),F.Bag->CanCraft(TEXT("Hand"),false));
    TestFalse(TEXT("Unbound station fails closed"),F.Bag->CanCraft(TEXT("Station"),false));
    TestFalse(TEXT("Unbound building placement fails closed"),F.Bag->CanCraft(TEXT("Build"),false));
    bool NearStation=true,CanPlace=true;
    F.Bag->SetCraftingContextValidator([&](FName,FName Station,FName Building)
    {return (Station.IsNone()||NearStation)&&(Building.IsNone()||CanPlace);});
    TestTrue(TEXT("Station recipe available through existing CanCraft"),F.Bag->CanCraft(TEXT("Station"),false));
    FDWPreparedInventoryChange StationPlan;
    TestTrue(TEXT("Station recipe prepared"),F.Bag->PrepareCraftRecipe(TEXT("Station"),false,StationPlan));
    NearStation=false;
    TestFalse(TEXT("Lost station invalidates prepared craft without revision change"),F.Bag->CommitPrepared(MoveTemp(StationPlan)));
    TestEqual(TEXT("Lost station preserves all inputs"),F.Bag->CountItem(TEXT("Flour")),10);
    NearStation=true;
    TestTrue(TEXT("Ordinary station craft works"),F.Bag->TryCraft(TEXT("Station"),false));
    TestEqual(TEXT("Ordinary craft output"),F.Bag->CountItem(TEXT("Dough")),1);
    FDWPreparedInventoryChange Building;
    TestTrue(TEXT("Building material-only candidate prepared"),F.Bag->PrepareCraftRecipe(TEXT("Build"),false,Building));
    TestEqual(TEXT("Building output identity retained"),Building.GetBuildingOutput(),FName(TEXT("Furnace")));
    int32 CandidateFlour=0,CandidateBuildings=0;
    for(const auto& Item:Building.GetCandidateSlots()){if(Item.ItemId==TEXT("Flour"))CandidateFlour+=Item.Quantity;if(Item.ItemId==TEXT("Furnace"))CandidateBuildings+=Item.Quantity;}
    TestEqual(TEXT("Building candidate removes materials"),CandidateFlour,2);
    TestEqual(TEXT("Building candidate never adds building to bag"),CandidateBuildings,0);
    TestFalse(TEXT("Public inventory commit cannot bypass building coordinator"),F.Bag->CommitPrepared(MoveTemp(Building)));
    TestEqual(TEXT("Rejected standalone building commit keeps materials"),F.Bag->CountItem(TEXT("Flour")),7);
    CanPlace=false;TestFalse(TEXT("Placement invalid makes existing CanCraft false"),F.Bag->CanCraft(TEXT("Build"),false));
    F.Bag->SetCraftingContextValidator(nullptr);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringMapProfileResolution,"DoughWorld.Gathering.Configuration.MapSpecificResolution",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringMapProfileResolution::RunTest(const FString&)
{
    using namespace DWGatheringConfigTests;FFixture F;
    TStrongObjectPtr<UDWGameInstance> GI(NewObject<UDWGameInstance>());GI->SoftConfigPath.Reset();
    UDWGameplayConfig* Original=GI->GetConfigForMap(TEXT("/Game/DW_QA_Original"));
    if(!TestNotNull(TEXT("Transient original fallback"),Original))return false;
    const FName TestMap(TEXT("/Game/DW_QA_Gathering"));
    Original->MapConfigOverrides.Add(TestMap,TSoftObjectPtr<UDWGameplayConfig>(F.Profile.Get()));
    TestTrue(TEXT("Registered map resolves profile before travel"),GI->GetConfigForMap(TestMap.ToString())==F.Profile.Get());
    TestTrue(TEXT("PIE map name resolves same profile"),GI->GetConfigForMap(TEXT("/Game/UEDPIE_2_DW_QA_Gathering"))==F.Profile.Get());
    TestTrue(TEXT("Old map still resolves original after new map lookup"),GI->GetConfigForMap(TEXT("/Game/DW_QA_Original"))==Original);
    TestFalse(TEXT("Cross-profile inventory travel incompatible"),GI->AreGameplayMapProfilesCompatible(TEXT("/Game/DW_QA_Original"),TestMap.ToString()));
    TestTrue(TEXT("Two original-profile maps compatible"),GI->AreGameplayMapProfilesCompatible(TEXT("/Game/DW_QA_Original"),TEXT("/Game/DW_QA_Original2")));
    Original->MapConfigOverrides.Add(TEXT("/Game/DW_QA_Missing"),TSoftObjectPtr<UDWGameplayConfig>());
    TestNull(TEXT("Explicit missing profile fails closed"),GI->GetConfigForMap(TEXT("/Game/DW_QA_Missing")));
    TestFalse(TEXT("Missing target profile cannot carry inventory"),GI->AreGameplayMapProfilesCompatible(TEXT("/Game/DW_QA_Original"),TEXT("/Game/DW_QA_Missing")));
    return true;
}
#endif
