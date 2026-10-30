#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatTestWorld.h"
#include "DWGatheringProfile.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"

namespace DWStarterSpearTests
{
const FName Sugar(TEXT("SugarSpear")),Legacy(TEXT("DWWeapon_Spear")),Recipe(TEXT("GatherCraft_SugarSpear_CraftingTable"));
const FName LegacyRecipe(TEXT("DW_QA_Starter_LegacySpear"));
TArray<FDWItemStack> Starter()
{
    return {{TEXT("DWWeapon_ShieldAxe"),1},{TEXT("DWWeapon_Greatsword"),1},{TEXT("DWWeapon_DualDaggers"),1},
        {TEXT("DWWeapon_SporeGun"),1},{TEXT("DWWeapon_MoldBow"),1},{TEXT("DWWeapon_Shotgun"),1},
        {TEXT("Alcohol"),6},{TEXT("DWAmmo_Spore"),60},{TEXT("DWAmmo_MoldArrow"),30},{TEXT("DWAmmo_BreadShell"),18}};
}
bool SameSlots(const TArray<FDWItemStack>& A,const TArray<FDWItemStack>& B)
{if(A.Num()!=B.Num())return false;for(int32 I=0;I<A.Num();++I)if(A[I].ItemId!=B[I].ItemId||A[I].Quantity!=B[I].Quantity)return false;return true;}
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;TStrongObjectPtr<UDWGatheringProfile> Profile;
    UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;FString Error;bool bReady=false,bNearStation=true;
    FString Playthrough=FGuid::NewGuid().ToString(EGuidFormats::Digits);FDWCombatStarterGrantState Receipt;
    explicit FFixture(int32 Capacity=24)
    {
        World=DWCombatTestWorld::Create(GI,TEXT("StarterSpearWorld"));if(!World)return;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        P=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);
        if(P&&!P->HasActorBegunPlay())P->DispatchBeginPlay();if(!P||!P->HasCombatVitalsReady()||!P->GetGameplayConfig()||!W()||!GI->GetCombatRules()||GI->HasActiveSlot())return;
        Profile.Reset(NewObject<UDWGatheringProfile>());Profile->Items=P->GetGameplayConfig()->Items;Profile->Recipes.Reset();
        Profile->ItemTable=nullptr;Profile->RecipeTable=nullptr;Profile->MaxInventorySlots=Capacity;Profile->MaxStackSize=40;Profile->bRequireTransformationForCrafting=false;
        auto Item=[&](FName Id){if(!Profile->GetItemDefinition(Id)){FDWItemDefinition D;D.ItemId=Id;Profile->Items.Add(D);}};
        for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
        {Item(Binding.ItemId);if(!Binding.AmmoItemId.IsNone())Item(Binding.AmmoItemId);if(Binding.ItemRole==EDWCombatItemRole::PermanentWeapon)Profile->StackLimits.Add(Binding.ItemId,1);}
        for(FName Id:{Sugar,FName(TEXT("SimpleSugarSpear")),FDWCombatItemRegistry::MoldCoreId(),FDWCombatItemRegistry::YeastCoreId(),FName(TEXT("SugarStick")),FName(TEXT("HardSugarChunk")),FName(TEXT("BottledSyrup"))})Item(Id);
        Profile->StackLimits.Add(Sugar,1);
        FDWRecipeDefinition R;R.RecipeId=Recipe;R.Inputs={{TEXT("SugarStick"),2},{TEXT("HardSugarChunk"),2},{TEXT("BottledSyrup"),1}};R.Outputs={{Sugar,1}};R.bRequiresYeast=false;
        Profile->Recipes.Add(R);R.RecipeId=LegacyRecipe;R.Outputs={{Legacy,1}};Profile->Recipes.Add(R);
        Profile->RecipeStations.Add(Recipe,TEXT("GatherStation_CraftingTable"));Profile->RecipeStations.Add(LegacyRecipe,TEXT("GatherStation_CraftingTable"));
        if(!Bag()->SetSlots({}))return;P->GameplayConfig=Profile.Get();Bag()->InitializeInventory(Profile.Get());
        Bag()->SetCraftingContextValidator([this](FName,FName Station,FName){return bNearStation&&Station==TEXT("GatherStation_CraftingTable");});
        Receipt.PlaythroughId=Playthrough;bReady=W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty();
    }
    ~FFixture()
    {
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    UDWInventoryComponent* Bag()const{return P?P->GetInventory():nullptr;}
    UDWWeaponAnimationComponent* W()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    bool Materials(){return Bag()->SetSlots({{TEXT("SugarStick"),2},{TEXT("HardSugarChunk"),2},{TEXT("BottledSyrup"),1}});}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWStarterAliasIdentityTest,"DoughWorld.Equipment.StarterAndSpear.AliasesPreserveLegacyIdentityAndInventory",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWStarterAliasIdentityTest::RunTest(const FString&)
{
    using namespace DWStarterSpearTests;FFixture F;if(!F.bReady)return false;
    TestTrue(TEXT("SugarSpear resolves existing spear binding"),FDWCombatItemRegistry::FindItem(Sugar)==FDWCombatItemRegistry::FindPresentation(EDWAnimatedWeapon::Spear));
    TestTrue(TEXT("SugarSpear is permanent"),FDWCombatItemRegistry::GetItemRole(Sugar)==EDWCombatItemRole::PermanentWeapon);
    TestFalse(TEXT("SimpleSugarSpear not silently merged"),FDWCombatItemRegistry::SamePermanentWeapon(TEXT("SimpleSugarSpear"),Sugar));
    TestEqual(TEXT("Internal identity unchanged"),FDWCombatItemRegistry::CanonicalWeaponItemId(Sugar),Legacy);
    FName Id;TestTrue(TEXT("Internal ToItem keeps old schema identity"),DWCombatEquipment::ToItemId(EDWAnimatedWeapon::Spear,Id)&&Id==Legacy);
    TestTrue(TEXT("Display inventory uses SugarSpear"),DWCombatEquipment::ToInventoryItemId(EDWAnimatedWeapon::Spear,Id)&&Id==Sugar);
    FDWCombatWeaponResourceSnapshot Saved;Saved.OwnedPermanentWeaponIds.Add(Legacy);Saved.CurrentEquipmentId=Legacy;
    TestTrue(TEXT("Old recorded IDs restore without migration"),F.W()->RestoreCombatWeaponState(Saved,*F.GI->GetCombatRules(),F.Error));
    TestTrue(TEXT("Both names observe one ownership fact"),DWCombatEquipment::OwnsPermanent(F.W()->GetCombatWeaponStateSnapshot(),Sugar)&&DWCombatEquipment::OwnsPermanent(F.W()->GetCombatWeaponStateSnapshot(),Legacy));
    TestTrue(TEXT("Old current ID retained"),F.W()->GetCurrentEquipmentId()==Legacy&&F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Legacy));
    Saved.OwnedPermanentWeaponIds={Sugar};Saved.CurrentEquipmentId=Sugar;
    TestTrue(TEXT("Alias ownership record also validated"),F.W()->RestoreCombatWeaponState(Saved,*F.GI->GetCombatRules(),F.Error));
    TestTrue(TEXT("Existing alias record not rewritten"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Sugar)&&!F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Legacy));
    Saved.OwnedPermanentWeaponIds.Add(Legacy);TestFalse(TEXT("Two aliases cannot masquerade as two owned identities"),DWCombatEquipment::ValidateIdentity(Saved,F.Error));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWStarterSugarCraftTest,"DoughWorld.Equipment.StarterAndSpear.SugarRecipeIsOneAtomicAcquisition",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWStarterSugarCraftTest::RunTest(const FString&)
{
    using namespace DWStarterSpearTests;FFixture F;if(!F.bReady||!F.Materials())return false;
    const auto Before=F.Bag()->GetInventorySlots();const auto Revision=F.Bag()->GetInventoryRevision();F.bNearStation=false;
    TestFalse(TEXT("Existing crafting-table requirement retained"),FDWCombatEquipmentTransaction::TryCraft(*F.W(),Recipe,false));
    TestTrue(TEXT("Missing station costs nothing"),SameSlots(Before,F.Bag()->GetSlots()));TestEqual(TEXT("Missing station revision unchanged"),F.Bag()->GetInventoryRevision(),Revision);
    F.bNearStation=true;TestFalse(TEXT("Public inventory-only craft blocked for SugarSpear"),F.Bag()->TryCraft(Recipe,false));
    int32 Notices=0;TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    Probe->TestHook=[&]()
    {
        ++Notices;TestEqual(TEXT("Observer sees exact Sugar output"),F.Bag()->CountItem(Sugar),1);TestEqual(TEXT("No second legacy item created"),F.Bag()->CountItem(Legacy),0);
        TestTrue(TEXT("Ownership already committed"),DWCombatEquipment::OwnsPermanent(F.W()->GetCombatWeaponStateSnapshot(),Sugar));
        TestEqual(TEXT("Exact original costs paid"),F.Bag()->CountItem(TEXT("SugarStick"))+F.Bag()->CountItem(TEXT("HardSugarChunk"))+F.Bag()->CountItem(TEXT("BottledSyrup")),0);
    };
    F.Bag()->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Typed original recipe succeeds"),FDWCombatEquipmentTransaction::TryCraft(*F.W(),Recipe,false));
    TestEqual(TEXT("One inventory notification"),Notices,1);TestTrue(TEXT("No auto-equip"),F.W()->GetCurrentEquipmentId().IsNone());
    F.Bag()->OnChanged.RemoveDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);Probe->TestHook=nullptr;
    F.Bag()->SetSlots({{Sugar,1},{TEXT("SugarStick"),2},{TEXT("HardSugarChunk"),2},{TEXT("BottledSyrup"),1}});
    const auto OwnedBefore=F.Bag()->GetInventorySlots();TestFalse(TEXT("Historical legacy recipe cannot grant second identity when owned"),FDWCombatEquipmentTransaction::TryCraft(*F.W(),LegacyRecipe,false));
    TestTrue(TEXT("Alias duplicate leaves all original IDs and materials intact"),SameSlots(OwnedBefore,F.Bag()->GetSlots()));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWStarterExistingItemTest,"DoughWorld.Equipment.StarterAndSpear.ExistingSugarItemClaimsWithoutAddingOrChangingSlots",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWStarterExistingItemTest::RunTest(const FString&)
{
    using namespace DWStarterSpearTests;FFixture F;if(!F.bReady||!F.Bag()->SetSlots({{Sugar,1}}))return false;
    FDWInventorySlotSnapshot Slot;if(!F.Bag()->CaptureSlotSnapshot(0,Slot))return false;const auto Before=F.Bag()->GetInventorySlots();const auto IR=F.Bag()->GetInventoryRevision(),WR=F.W()->GetCombatWeaponResourceRevision();
    TestTrue(TEXT("Existing valid item can acquire missing ownership"),FDWCombatEquipmentTransaction::ClaimExistingPermanent(*F.W(),Slot));
    TestTrue(TEXT("Slots preserved exactly"),SameSlots(Before,F.Bag()->GetSlots()));TestEqual(TEXT("No synthetic inventory mutation"),F.Bag()->GetInventoryRevision(),IR);
    TestEqual(TEXT("No added legacy physical item"),F.Bag()->CountItem(Legacy),0);TestEqual(TEXT("Exactly one ownership mutation"),F.W()->GetCombatWeaponResourceRevision(),WR+1);
    TestTrue(TEXT("Original selection snapshot remains current"),F.Bag()->IsSlotSnapshotCurrent(Slot));
    TestTrue(TEXT("Claim repeat is idempotent"),FDWCombatEquipmentTransaction::ClaimExistingPermanent(*F.W(),Slot));TestEqual(TEXT("No duplicate ownership revision"),F.W()->GetCombatWeaponResourceRevision(),WR+1);
    TestTrue(TEXT("Claim does not equip"),F.W()->GetCurrentEquipmentId().IsNone());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWStarterBatchAtomicTest,"DoughWorld.Equipment.StarterAndSpear.BatchFactsAndReceiptAreVisibleBeforeCallback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWStarterBatchAtomicTest::RunTest(const FString&)
{
    using namespace DWStarterSpearTests;FFixture F;if(!F.bReady)return false;const auto Items=Starter();const auto IR=F.Bag()->GetInventoryRevision(),WR=F.W()->GetCombatWeaponResourceRevision();
    int32 InventoryNotices=0,WeaponNotices=0;TStrongObjectPtr<UDWInventoryTransactionTestProbe> I(NewObject<UDWInventoryTransactionTestProbe>());
    TStrongObjectPtr<UDWWeaponResourceTestProbe> W(NewObject<UDWWeaponResourceTestProbe>());
    I->TestHook=[&]()
    {
        ++InventoryNotices;TestTrue(TEXT("Stable receipt committed before inventory listener"),F.Receipt.bGranted&&F.Receipt.PlaythroughId==F.Playthrough&&F.Receipt.Revision==1);
        TestEqual(TEXT("All six permanent owned together"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Num(),6);
        for(const auto& Item:Items)TestEqual(*Item.ItemId.ToString(),F.Bag()->CountItem(Item.ItemId),Item.Quantity);
        TestFalse(TEXT("Spear not given"),DWCombatEquipment::OwnsPermanent(F.W()->GetCombatWeaponStateSnapshot(),Sugar));
        const auto Repeat=FDWCombatEquipmentTransaction::GrantStarterBatch(*F.W(),Items,F.Playthrough,F.Receipt);
        TestTrue(TEXT("Callback repeat cannot refill"),!Repeat.bCommittedThisCall&&Repeat.bAlreadyGranted);
        TStrongObjectPtr<UDWSaveGame> Save(NewObject<UDWSaveGame>());F.P->CaptureSaveData(Save.Get());
        TestEqual(TEXT("In-memory capture sees all owned facts"),Save->CombatPlayerResources.OwnedPermanentWeaponIds.Num(),6);
    };
    W->TestHook=[&](int32 Loaded,int64 Revision,bool Reload){++WeaponNotices;TestEqual(TEXT("Inventory event precedes weapon event"),InventoryNotices,1);TestEqual(TEXT("Loaded magazine not filled"),Loaded,0);TestEqual(TEXT("Single resource revision"),Revision,WR+1);TestFalse(TEXT("Not reload"),Reload);};
    F.Bag()->OnChanged.AddDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);F.W()->OnWeaponResourceChanged.AddDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);
    const auto Result=FDWCombatEquipmentTransaction::GrantStarterBatch(*F.W(),Items,F.Playthrough,F.Receipt);TestTrue(TEXT("Batch commits"),Result.bCommittedThisCall);
    TestEqual(TEXT("One bag revision for entire batch"),F.Bag()->GetInventoryRevision(),IR+1);TestEqual(TEXT("One inventory notice"),InventoryNotices,1);TestEqual(TEXT("One weapon notice"),WeaponNotices,1);
    TestTrue(TEXT("Batch leaves empty-hand current"),F.W()->GetCurrentEquipmentId().IsNone());TestEqual(TEXT("No SugarSpear"),F.Bag()->CountItem(Sugar),0);TestEqual(TEXT("No legacy physical spear"),F.Bag()->CountItem(Legacy),0);
    F.Bag()->OnChanged.RemoveDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);I->TestHook=nullptr;F.W()->OnWeaponResourceChanged.RemoveDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);W->TestHook=nullptr;
    F.Bag()->TryRemoveItem(TEXT("Alcohol"),1);const auto Reduced=F.Bag()->GetInventorySlots();const auto After=F.Bag()->GetInventoryRevision();
    TestTrue(TEXT("Reloaded native receipt remains already granted"),FDWCombatEquipmentTransaction::GrantStarterBatch(*F.W(),Items,F.Playthrough,F.Receipt).bAlreadyGranted);
    TestTrue(TEXT("Spent stock not replenished"),SameSlots(Reduced,F.Bag()->GetSlots()));TestEqual(TEXT("No repeated bag revision"),F.Bag()->GetInventoryRevision(),After);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWStarterBatchRejectTest,"DoughWorld.Equipment.StarterAndSpear.CapacityAndInvalidBatchesLeaveAllFactsUnchanged",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWStarterBatchRejectTest::RunTest(const FString&)
{
    using namespace DWStarterSpearTests;
    for(int32 Mode=0;Mode<5;++Mode)
    {
        FFixture F(Mode==0?6:24);if(!F.bReady)return false;auto Items=Starter();if(Mode==1)Items.Add({Sugar,1});if(Mode==2)Items.Add({TEXT("DWAmmo_Spore"),1});if(Mode==3)Items[0].Quantity=0;if(Mode==4)Items.RemoveAt(0);
        const auto Before=F.Bag()->GetInventorySlots();const auto IR=F.Bag()->GetInventoryRevision(),WR=F.W()->GetCombatWeaponResourceRevision();
        const auto Result=FDWCombatEquipmentTransaction::GrantStarterBatch(*F.W(),Items,F.Playthrough,F.Receipt);
        TestFalse(TEXT("Rejected whole batch"),Result.bCommittedThisCall);TestTrue(TEXT("No partial physical items after candidate progressed"),SameSlots(Before,F.Bag()->GetSlots()));
        TestTrue(TEXT("No partial owned identities"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty());TestTrue(TEXT("Receipt remains not granted"),!F.Receipt.bGranted&&F.Receipt.Revision==0);
        TestEqual(TEXT("No rejected inventory revision"),F.Bag()->GetInventoryRevision(),IR);TestEqual(TEXT("No rejected weapon revision"),F.W()->GetCombatWeaponResourceRevision(),WR);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWStarterReceiptScopeTest,"DoughWorld.Equipment.StarterAndSpear.StableReceiptSurvivesPawnReplacementAndRejectsOtherPlaythrough",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWStarterReceiptScopeTest::RunTest(const FString&)
{
    using namespace DWStarterSpearTests;FFixture First,Replacement;if(!First.bReady||!Replacement.bReady)return false;const auto Items=Starter();
    if(!TestTrue(TEXT("First isolated playthrough gets batch once"),FDWCombatEquipmentTransaction::GrantStarterBatch(*First.W(),Items,First.Playthrough,First.Receipt).bCommittedThisCall))return false;
    // The LANG wrapper carries these exact two persisted fields; native revision is recreated on restore.
    FDWCombatStarterGrantState Restored;Restored.PlaythroughId=First.Receipt.PlaythroughId;Restored.bGranted=First.Receipt.bGranted;
    const auto Before=Replacement.Bag()->GetInventorySlots();
    const auto Repeat=FDWCombatEquipmentTransaction::GrantStarterBatch(*Replacement.W(),Items,First.Playthrough,Restored);
    TestTrue(TEXT("New Pawn does not obtain a second batch in the same playthrough"),Repeat.bAlreadyGranted&&!Repeat.bCommittedThisCall);
    TestTrue(TEXT("No refill on replacement"),SameSlots(Before,Replacement.Bag()->GetSlots()));
    TestFalse(TEXT("Cannot rebind granted receipt to fresh UUID"),FDWCombatEquipmentTransaction::GrantStarterBatch(*Replacement.W(),Items,Replacement.Playthrough,Restored).bCommittedThisCall);
    TestEqual(TEXT("Receipt keeps original playthrough"),Restored.PlaythroughId,First.Playthrough);return true;
}
#endif
