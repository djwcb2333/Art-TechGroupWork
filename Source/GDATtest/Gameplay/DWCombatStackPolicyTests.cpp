#if WITH_DEV_AUTOMATION_TESTS
#include "DWInventoryComponent.h"
#include "DWCombatRules.h"
#include "DWCombatItemRegistry.h"
#include "DWGameplayConfig.h"
#include "DWInventoryTransactionTestProbe.h"
#include "Dom/JsonObject.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/StrongObjectPtr.h"

namespace DWCombatStackTests
{
    const FName Other(TEXT("DW_QA_StackOther"));
    TSharedPtr<const FDWCombatRules> RulesWithLimit(int32 Limit,FString& Error)
    {
        FString Text;TSharedPtr<FJsonObject> Root;TSharedPtr<const FDWCombatRules> Rules;
        if(!FFileHelper::LoadFileToString(Text,*FPaths::Combine(FPaths::ProjectContentDir(),TEXT("DoughWorld/Gameplay/Combat/DWCombatRules.json")))||
            !FDWCombatRules::LoadFromJson(Text,Rules,Error)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root))return nullptr;
        bool bFound=false;
        for(const auto& Row:Root->GetArrayField(TEXT("parameters")))
            if(Row->AsObject()->GetStringField(TEXT("id"))==TEXT("core.stack_limit"))
            {Row->AsObject()->SetNumberField(TEXT("value"),Limit);bFound=true;break;}
        if(!bFound)return nullptr;
        Text.Reset();FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
        return FDWCombatRules::LoadFromJson(Text,Rules,Error)?Rules:nullptr;
    }
    struct FFixture
    {
        TStrongObjectPtr<UDWGameplayConfig> Config{NewObject<UDWGameplayConfig>()};
        TStrongObjectPtr<UDWInventoryComponent> Bag{NewObject<UDWInventoryComponent>()};
        explicit FFixture(bool bRegisterYeast=true)
        {
            Config->Items.Reset();Config->MaxStackSize=99;Config->MaxInventorySlots=10;
            for(FName Id:{FDWCombatItemRegistry::MoldCoreId(),FDWCombatItemRegistry::YeastCoreId(),Other})
            {
                if(!bRegisterYeast&&Id==FDWCombatItemRegistry::YeastCoreId())continue;
                FDWItemDefinition Def;Def.ItemId=Id;Config->Items.Add(Def);
            }
            Bag->InitializeInventory(Config.Get());
            TMap<FName,int32> Overrides;Overrides.Add(Other,7);Bag->TrySetStackLimitOverrides(Overrides);
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatCoreStackBridgeTest,"DoughWorld.InventoryTransactions.CorePolicy.FormalCountAndIndependentStacks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatCoreStackBridgeTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatStackTests;
    FString Error;const auto Rules=RulesWithLimit(13,Error);if(!TestTrue(TEXT("Isolated non-default formal cap loads"),Rules.IsValid()))return false;
    FFixture F;
    FDWPreparedInventoryChange OldPlan;F.Bag->PrepareAddItem(Other,1,OldPlan);
    const int64 Before=F.Bag->GetInventoryRevision();
    if(!TestTrue(TEXT("Apply formal cap"),F.Bag->TryApplyCombatStackPolicy(*Rules,Error)))return false;
    TestEqual(TEXT("Policy invalidates prepared inventory revision"),F.Bag->GetInventoryRevision(),Before+1);
    TestFalse(TEXT("Old pre-policy plan is stale"),F.Bag->CommitPrepared(MoveTemp(OldPlan)));
    TestEqual(TEXT("Mold follows formal value rather than literal 40"),F.Bag->GetEffectiveStackLimit(FDWCombatItemRegistry::MoldCoreId()),13);
    TestEqual(TEXT("Yeast independently follows same formal cap"),F.Bag->GetEffectiveStackLimit(FDWCombatItemRegistry::YeastCoreId()),13);
    TestEqual(TEXT("Unrelated item-specific policy remains 7"),F.Bag->GetEffectiveStackLimit(Other),7);
    TestTrue(TEXT("Mold overflow allocates second stack"),F.Bag->TryAddItem(FDWCombatItemRegistry::MoldCoreId(),14));
    TestTrue(TEXT("Yeast does not merge with Mold"),F.Bag->TryAddItem(FDWCombatItemRegistry::YeastCoreId(),1));
    TestEqual(TEXT("Two mold stacks and separate yeast"),F.Bag->GetSlots().Num(),3);
    TestFalse(TEXT("Save restore rejects a core stack above the same policy"),F.Bag->SetSlots({FDWItemStack(FDWCombatItemRegistry::YeastCoreId(),14)}));
    TestEqual(TEXT("Rejected restore preserves all mold"),F.Bag->CountItem(FDWCombatItemRegistry::MoldCoreId()),14);
    TestEqual(TEXT("Rejected restore preserves all yeast"),F.Bag->CountItem(FDWCombatItemRegistry::YeastCoreId()),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatCorePolicyRejectionTest,"DoughWorld.InventoryTransactions.CorePolicy.RejectWithoutPartialPolicyOrLostItems",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatCorePolicyRejectionTest::RunTest(const FString& Parameters)
{
    using namespace DWCombatStackTests;
    FString Error;const auto Rules=RulesWithLimit(13,Error);if(!TestTrue(TEXT("Rules fixture"),Rules.IsValid()))return false;
    FFixture F;F.Bag->TryAddItem(FDWCombatItemRegistry::MoldCoreId(),20);
    const int64 Before=F.Bag->GetInventoryRevision();
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    int32 Notices=0;Probe->TestHook=[&](){++Notices;};F.Bag->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestFalse(TEXT("Reducing below existing stack rejects"),F.Bag->TryApplyCombatStackPolicy(*Rules,Error));
    TestEqual(TEXT("No items repacked or deleted"),F.Bag->CountItem(FDWCombatItemRegistry::MoldCoreId()),20);
    TestEqual(TEXT("No partial mold cap"),F.Bag->GetEffectiveStackLimit(FDWCombatItemRegistry::MoldCoreId()),99);
    TestEqual(TEXT("No partial yeast cap"),F.Bag->GetEffectiveStackLimit(FDWCombatItemRegistry::YeastCoreId()),99);
    TestEqual(TEXT("Rejection preserves revision"),F.Bag->GetInventoryRevision(),Before);
    TestEqual(TEXT("Rejected policy has no notification"),Notices,0);
    FFixture Missing(false);const int64 MissingBefore=Missing.Bag->GetInventoryRevision();
    TestFalse(TEXT("Missing second core registration rejects entire policy"),Missing.Bag->TryApplyCombatStackPolicy(*Rules,Error));
    TestEqual(TEXT("Missing id does not change first cap"),Missing.Bag->GetEffectiveStackLimit(FDWCombatItemRegistry::MoldCoreId()),99);
    TestEqual(TEXT("Missing id preserves revision"),Missing.Bag->GetInventoryRevision(),MissingBefore);
    return true;
}
#endif
