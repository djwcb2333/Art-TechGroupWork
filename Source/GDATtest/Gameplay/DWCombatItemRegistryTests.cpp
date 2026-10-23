#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatItemRegistry.h"
#include "DWGameplayConfig.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatItemBindingTest,"DoughWorld.Combat.ItemBinding.RegistrationAndRoleSeparation",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatItemBindingTest::RunTest(const FString& Parameters)
{
    TStrongObjectPtr<UDWGameplayConfig> Config(NewObject<UDWGameplayConfig>());
    FString Error;
    TestFalse(TEXT("Legacy-only configuration cannot claim combat registration ready"),FDWCombatItemRegistry::ValidateItems(Config.Get(),Error));
    TestTrue(TEXT("Failure is explicit"),!Error.IsEmpty());
    const auto* OriginalDough=Config->GetItemDefinition(TEXT("Dough"));
    const float DoughHeal=OriginalDough?OriginalDough->HealAmount:0.f;
    TSet<FName> Ids{FDWCombatItemRegistry::MoldCoreId(),FDWCombatItemRegistry::YeastCoreId()};
    for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
    {
        Ids.Add(Binding.ItemId);if(!Binding.AmmoItemId.IsNone())Ids.Add(Binding.AmmoItemId);
        TestNotNull(TEXT("Presentation identity resolves independently from enum ordering"),FDWCombatItemRegistry::FindPresentation(Binding.Presentation));
    }
    for(FName Id:Ids)if(!Config->GetItemDefinition(Id)) {FDWItemDefinition Item;Item.ItemId=Id;Config->Items.Add(Item);}
    TestTrue(TEXT("All registered combat identities validate"),FDWCombatItemRegistry::ValidateItems(Config.Get(),Error));
    TestTrue(TEXT("Success clears prior error"),Error.IsEmpty());
    TestEqual(TEXT("Legacy craft Yeast is not a core"),FDWCombatItemRegistry::GetItemRole(TEXT("Yeast")),EDWCombatItemRole::Unrelated);
    TestEqual(TEXT("Alcohol remains consumable stock, never a permanent grant"),FDWCombatItemRegistry::GetItemRole(TEXT("Alcohol")),EDWCombatItemRole::ThrowableStock);
    TestEqual(TEXT("Reserve shell differs from owned shotgun"),FDWCombatItemRegistry::GetItemRole(TEXT("DWAmmo_BreadShell")),EDWCombatItemRole::ReserveAmmo);
    TestEqual(TEXT("Original Dough healing remains intact"),Config->GetItemDefinition(TEXT("Dough"))->HealAmount,DoughHeal);
    TestNull(TEXT("No implicit unarmed equipment item"),FDWCombatItemRegistry::FindWeapon(TEXT("unarmed")));
    FDWItemDefinition Duplicate=*Config->GetItemDefinition(TEXT("Alcohol"));Config->Items.Add(Duplicate);
    TestFalse(TEXT("Ambiguous registrations reject"),FDWCombatItemRegistry::ValidateItems(Config.Get(),Error));Config->Items.Pop();
    for(auto& Item:Config->Items)if(Item.ItemId==FDWCombatItemRegistry::MoldCoreId())Item.HealAmount=1.f;
    TestFalse(TEXT("Core cannot silently take legacy healing path too"),FDWCombatItemRegistry::ValidateItems(Config.Get(),Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWCombatItemPresentationTest,"DoughWorld.Combat.ItemBinding.LivePresentationIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWCombatItemPresentationTest::RunTest(const FString& Parameters)
{
    FString Error;
    TArray<UDWWeaponAnimationSet*> Sets;
    for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
        Sets.Add(Cast<UDWWeaponAnimationSet>(Binding.PresentationAsset.TryLoad()));
    if(!TestTrue(TEXT("Current project presentations resolve exact enum and asset identity"),FDWCombatItemRegistry::ValidatePresentationBindings(Sets,Error)))
    {AddError(Error);return false;}
    const int32 SpearIndex=Sets.IndexOfByPredicate([](const auto* Set){return Set&&Set->Weapon==EDWAnimatedWeapon::Spear;});
    if(!TestTrue(TEXT("Original spear slot exists"),SpearIndex!=INDEX_NONE))return false;
    auto* OriginalSpear=Sets[SpearIndex];
    auto* ApprovedR1=Cast<UDWWeaponAnimationSet>(FSoftObjectPath(TEXT("/Game/DoughWorld/Gameplay/Combat/Weapons/DA_DWHeroWeapon_Spear_HC04_CandidateA_R1.DA_DWHeroWeapon_Spear_HC04_CandidateA_R1")).TryLoad());
    if(!TestNotNull(TEXT("Approved saved R1 spear presentation exists"),ApprovedR1))return false;
    Sets[SpearIndex]=ApprovedR1;
    TestTrue(TEXT("Only the approved R1 spear alias is accepted"),FDWCombatItemRegistry::ValidatePresentationBindings(Sets,Error));
    auto* Unapproved=NewObject<UDWWeaponAnimationSet>();Unapproved->Weapon=EDWAnimatedWeapon::Spear;
    Sets[SpearIndex]=Unapproved;
    TestFalse(TEXT("Matching enum alone cannot authorize a different asset"),FDWCombatItemRegistry::ValidatePresentationBindings(Sets,Error));
    Sets[SpearIndex]=OriginalSpear;
    Sets[1]=Sets[0];
    TestFalse(TEXT("Duplicate enum/path rejects before gameplay"),FDWCombatItemRegistry::ValidatePresentationBindings(Sets,Error));
    Sets[1]=nullptr;
    TestFalse(TEXT("Missing asset rejects instead of falling back"),FDWCombatItemRegistry::ValidatePresentationBindings(Sets,Error));
    return true;
}
#endif
