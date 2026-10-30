#if WITH_DEV_AUTOMATION_TESTS
#include "DWRetiredItemPolicy.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWRetiredItemPolicyTest,
    "DoughWorld.Gathering.RetiredItems.ExplicitListAndLegacyInventory",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWRetiredItemPolicyTest::RunTest(const FString&)
{
    using namespace DWRetiredItemPolicy;
    const FName Retired[]={TEXT("Jelly"),TEXT("GummyCandy"),TEXT("Waffle"),TEXT("CandyCane"),TEXT("Marshmallow"),
        TEXT("LicoriceCandy"),TEXT("GingerbreadMan"),TEXT("Tofu"),TEXT("Cocoa"),TEXT("Beeswax"),TEXT("Salt"),TEXT("Acid"),
        TEXT("ElementC"),TEXT("ElementH"),TEXT("ElementO"),TEXT("ElementN"),TEXT("ElementP"),TEXT("ElementK"),TEXT("ElementS")};
    for(FName Id:Retired)TestTrue(TEXT("Explicit retirement is recognized"),IsRetired(Id));
    TestFalse(TEXT("Unknown identity is not implicitly retired"),IsRetired(TEXT("UnknownQA")));
    TestFalse(TEXT("Known active identity is retained"),IsRetired(TEXT("SugarChunk")));
    const TArray<FDWItemStack> Original={{TEXT("Jelly"),2},{TEXT("SugarChunk"),3},{TEXT("UnknownQA"),1}};
    TArray<FDWItemStack> Copy=Original;NormalizeLegacyInventory(Copy);
    TestEqual(TEXT("Only retired slot removed"),Copy.Num(),2);
    TestEqual(TEXT("Active slot retained"),Copy[0].ItemId,FName(TEXT("SugarChunk")));
    TestEqual(TEXT("Unknown remains for strict downstream validation"),Copy[1].ItemId,FName(TEXT("UnknownQA")));
    TestEqual(TEXT("Original saved memory remains untouched"),Original.Num(),3);
    FDWRecipeDefinition R;R.Inputs={{TEXT("Jelly"),1}};R.Outputs={{TEXT("SugarChunk"),1}};
    TestTrue(TEXT("Recipe with retired input rejected"),RecipeUsesRetired(R));
    R.Inputs={{TEXT("SugarChunk"),1}};R.Outputs={{TEXT("Waffle"),1}};
    TestTrue(TEXT("Recipe with retired output rejected"),RecipeUsesRetired(R));
    R.Outputs={{TEXT("SugarStick"),1}};
    TestFalse(TEXT("Active recipe is not retired"),RecipeUsesRetired(R));
    return true;
}
#endif
