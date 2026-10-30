#if WITH_DEV_AUTOMATION_TESTS
#include "DWCombatEquipment.h"
#include "DWCombatItemRegistry.h"
#include "DWCombatAuthoritySubsystem.h"
#include "DWCombatTestWorld.h"
#include "DWGatheringProfile.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWWeaponResourceTestProbe.h"
#include "DWPlayerCharacter.h"
#include "DWWeaponAnimationComponent.h"
#include "DWSaveGame.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"

namespace DWE1CraftTests
{
const FName Material(TEXT("DW_QA_E1Craft_Material")),Filler(TEXT("DW_QA_E1Craft_Filler"));
const FName Station(TEXT("DW_QA_E1Craft_Station"));
FName RecipeFor(FName Item){return FName(*(TEXT("DW_QA_E1Craft_")+Item.ToString()));}
bool SameSlots(const TArray<FDWItemStack>& A,const TArray<FDWItemStack>& B)
{
    if(A.Num()!=B.Num())return false;
    for(int32 Index=0;Index<A.Num();++Index)if(A[Index].ItemId!=B[Index].ItemId||A[Index].Quantity!=B[Index].Quantity)return false;
    return true;
}
bool SameResources(const FDWCombatWeaponResourceSnapshot& A,const FDWCombatWeaponResourceSnapshot& B)
{
    if(A.CurrentEquipmentId!=B.CurrentEquipmentId||A.LoadedShotgunShells!=B.LoadedShotgunShells||
        A.OwnedPermanentWeaponIds.Num()!=B.OwnedPermanentWeaponIds.Num())return false;
    for(FName Id:A.OwnedPermanentWeaponIds)if(!B.OwnedPermanentWeaponIds.Contains(Id))return false;
    return true;
}
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;TStrongObjectPtr<UDWGatheringProfile> Profile;
    UWorld* World=nullptr;ADWPlayerCharacter* P=nullptr;FString Error;bool bReady=false;
    explicit FFixture(int32 Slots=2)
    {
        World=DWCombatTestWorld::Create(GI,TEXT("E1CraftWorld"));if(!World)return;
        FActorSpawnParameters Params;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        P=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,500),FRotator::ZeroRotator,Params);
        if(P&&!P->HasActorBegunPlay())P->DispatchBeginPlay();
        if(!P||!P->HasCombatVitalsReady()||!P->GetGameplayConfig()||!W()||!GI->GetCombatRules()||GI->HasActiveSlot())return;
        Profile.Reset(NewObject<UDWGatheringProfile>());
        // Only this transient fixture owns its synthetic recipes/items. Never edit the live DA.
        Profile->Items=P->GetGameplayConfig()->Items;Profile->Recipes.Reset();Profile->ItemTable=nullptr;Profile->RecipeTable=nullptr;
        Profile->MaxInventorySlots=Slots;Profile->MaxStackSize=10;Profile->bRequireTransformationForCrafting=false;
        auto AddItem=[&](FName Id)
        {if(!Profile->GetItemDefinition(Id)){FDWItemDefinition Item;Item.ItemId=Id;Profile->Items.Add(Item);}};
        AddItem(Material);AddItem(Filler);AddItem(FDWCombatItemRegistry::MoldCoreId());AddItem(FDWCombatItemRegistry::YeastCoreId());
        for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
        {
            AddItem(Binding.ItemId);if(!Binding.AmmoItemId.IsNone())AddItem(Binding.AmmoItemId);
            if(Binding.ItemRole!=EDWCombatItemRole::PermanentWeapon)continue;
            FDWRecipeDefinition Recipe;Recipe.RecipeId=RecipeFor(Binding.ItemId);
            Recipe.Inputs={FDWItemStack(Material,2)};Recipe.Outputs={FDWItemStack(Binding.ItemId,1)};
            Recipe.bRequiresYeast=true; // The profile switch is off: neither preview nor execution requires transformation.
            Profile->Recipes.Add(Recipe);Profile->StackLimits.Add(Binding.ItemId,1);
        }
        if(!Bag()->SetSlots({}))return;
        P->GameplayConfig=Profile.Get();Bag()->InitializeInventory(Profile.Get());
        bReady=Bag()->AreSlotsValid(Bag()->GetSlots())&&W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty();
    }
    ~FFixture()
    {
        if(!World)return;World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
    UDWInventoryComponent* Bag()const{return P?P->GetInventory():nullptr;}
    UDWWeaponAnimationComponent* W()const{return P?P->WeaponAnimationComponent.Get():nullptr;}
    FDWCombatEquipmentGrantRequest Request(FName Item,uint64 Ordinal=1)const
    {auto* A=UDWCombatAuthoritySubsystem::Get(P);return {A->BeginAction(P),A->CaptureActorLife(P),Ordinal,W()->GetCombatWeaponRestoreEpoch(),Item};}
    void End(const FDWCombatEquipmentGrantRequest& R)const
    {if(auto* A=UDWCombatAuthoritySubsystem::Get(P))A->EndAction(R.Action);}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftSevenWeapons,"DoughWorld.Crafting.Permanent.AllSevenAtomicAndNoAutoEquipOrAmmo",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftSevenWeapons::RunTest(const FString&)
{
    using namespace DWE1CraftTests;FFixture F(12);if(!TestTrue(TEXT("Isolated formal player"),F.bReady))return false;
    int32 Crafted=0;
    for(const auto& Binding:FDWCombatItemRegistry::GetWeaponBindings())
    {
        if(Binding.ItemRole!=EDWCombatItemRole::PermanentWeapon)continue;
        if(!TestTrue(TEXT("Seed exact isolated material"),F.Bag()->TryAddItem(Material,2)))return false;
        const auto BagRevision=F.Bag()->GetInventoryRevision(),WeaponRevision=F.W()->GetCombatWeaponResourceRevision();
        TestTrue(TEXT("No transformation required for preview"),F.Bag()->CanCraft(RecipeFor(Binding.ItemId),false));
        const auto R=F.Request(Binding.ItemId);
        const auto First=FDWCombatEquipmentTransaction::Craft(*F.W(),R,RecipeFor(Binding.ItemId),(Crafted%2)==0);
        TestTrue(*Binding.ItemId.ToString(),First.bCommittedThisCall);
        TestEqual(TEXT("Exactly one physical weapon"),F.Bag()->CountItem(Binding.ItemId),1);
        TestEqual(TEXT("Exact material cost"),F.Bag()->CountItem(Material),0);
        TestTrue(TEXT("Permanent ownership committed"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Binding.ItemId));
        TestEqual(TEXT("One bag revision"),F.Bag()->GetInventoryRevision(),BagRevision+1);
        TestEqual(TEXT("One weapon revision"),F.W()->GetCombatWeaponResourceRevision(),WeaponRevision+1);
        TestFalse(TEXT("Repeated receipt cannot craft twice"),FDWCombatEquipmentTransaction::Craft(*F.W(),R,RecipeFor(Binding.ItemId),false).bCommittedThisCall);
        TestFalse(TEXT("Owned weapon preview rejects"),F.Bag()->CanCraft(RecipeFor(Binding.ItemId),true));
        TestTrue(TEXT("Current equipment remains empty-hand"),F.W()->GetCurrentEquipmentId().IsNone());
        TestEqual(TEXT("No loaded shells granted"),F.W()->GetCombatWeaponStateSnapshot().LoadedShotgunShells,0);
        if(!Binding.AmmoItemId.IsNone())TestEqual(TEXT("No reserve ammo granted"),F.Bag()->CountItem(Binding.AmmoItemId),0);
        F.End(R);++Crafted;
    }
    TestEqual(TEXT("Seven permanent registrations covered"),Crafted,7);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftFailures,"DoughWorld.Crafting.Permanent.MaterialCapacityAndOwnedFailuresAreZeroChange",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftFailures::RunTest(const FString&)
{
    using namespace DWE1CraftTests;FFixture F;if(!F.bReady)return false;
    const FName Item(TEXT("DWWeapon_Spear"));const auto Recipe=RecipeFor(Item);
    auto CheckRejected=[&](const TCHAR* Label)
    {
        const auto Slots=F.Bag()->GetInventorySlots();const auto Resources=F.W()->GetCombatWeaponStateSnapshot();
        const auto IR=F.Bag()->GetInventoryRevision(),WR=F.W()->GetCombatWeaponResourceRevision();
        TestFalse(Label,F.P->CraftRecipe(Recipe));
        TestTrue(TEXT("Every slot unchanged"),SameSlots(Slots,F.Bag()->GetSlots()));
        TestTrue(TEXT("Ownership/equipment/shells unchanged"),SameResources(Resources,F.W()->GetCombatWeaponStateSnapshot()));
        TestEqual(TEXT("Rejected bag revision unchanged"),F.Bag()->GetInventoryRevision(),IR);
        TestEqual(TEXT("Rejected weapon revision unchanged"),F.W()->GetCombatWeaponResourceRevision(),WR);
    };
    F.Bag()->SetSlots({FDWItemStack(Material,1)});CheckRejected(TEXT("Insufficient ingredients"));
    F.Bag()->SetSlots({FDWItemStack(Material,3),FDWItemStack(Filler,10)});CheckRejected(TEXT("Cost leaves both occupied slots"));
    F.Bag()->SetSlots({FDWItemStack(Material,2),FDWItemStack(Filler,10)});
    TestTrue(TEXT("Full bag succeeds when exact payment frees a slot"),F.P->CraftRecipe(Recipe));
    TestEqual(TEXT("Filler preserved"),F.Bag()->CountItem(Filler),10);
    TestEqual(TEXT("One weapon after freeing capacity"),F.Bag()->CountItem(Item),1);
    F.Bag()->SetSlots({FDWItemStack(Material,2),FDWItemStack(Item,1)});CheckRejected(TEXT("Already owned rejects with materials available"));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftPublicBypass,"DoughWorld.Crafting.Permanent.PublicInventoryCannotCommitOwnershipAlone",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftPublicBypass::RunTest(const FString&)
{
    using namespace DWE1CraftTests;FFixture F;if(!F.bReady)return false;const FName Item(TEXT("DWWeapon_Spear"));
    F.Bag()->SetSlots({FDWItemStack(Material,2)});const auto Revision=F.Bag()->GetInventoryRevision();
    FDWPreparedInventoryChange Plan;TestTrue(TEXT("Typed candidate prepares without side effects"),F.Bag()->PrepareCraftRecipe(RecipeFor(Item),false,Plan));
    TestEqual(TEXT("Candidate marks cross-state requirement"),Plan.GetPermanentWeaponOutput(),Item);
    TestFalse(TEXT("Public prepared commit rejects"),F.Bag()->CommitPrepared(MoveTemp(Plan)));
    TestFalse(TEXT("Public TryCraft cannot bypass typed owner"),F.Bag()->TryCraft(RecipeFor(Item),false));
    TestEqual(TEXT("No input removed"),F.Bag()->CountItem(Material),2);TestEqual(TEXT("No orphan physical weapon"),F.Bag()->CountItem(Item),0);
    TestEqual(TEXT("No bypass revision"),F.Bag()->GetInventoryRevision(),Revision);
    TStrongObjectPtr<UDWInventoryComponent> Ownerless(NewObject<UDWInventoryComponent>());Ownerless->InitializeInventory(F.Profile.Get());Ownerless->TryAddItem(Material,2);
    TestFalse(TEXT("Missing native owner/authority fails preview closed"),Ownerless->CanCraft(RecipeFor(Item),false));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftStationRevalidation,"DoughWorld.Crafting.Permanent.FinalStationAndBothRevisionsRevalidated",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftStationRevalidation::RunTest(const FString&)
{
    using namespace DWE1CraftTests;const FName Item(TEXT("DWWeapon_Spear"));
    for(int32 Mode=0;Mode<3;++Mode)
    {
        FFixture F;if(!F.bReady)return false;const auto Recipe=RecipeFor(Item);
        F.Profile->RecipeStations.Add(Recipe,Station);F.Bag()->SetSlots({FDWItemStack(Material,2)});
        int32 Calls=0;bool Injected=false;int64 InjectedIR=0,InjectedWR=0;
        F.Bag()->SetCraftingContextValidator([&](FName,FName Required,FName)
        {
            if(Required!=Station)return false;
            // Prepare queries twice; the third is the final commit-time station recheck.
            if(++Calls==3)
            {
                if(Mode==0)return false;
                if(Mode==1)Injected=F.Bag()->TryAddItem(Filler,1);
                else Injected=F.W()->RestoreCombatWeaponState(F.W()->GetCombatWeaponStateSnapshot(),*F.GI->GetCombatRules(),F.Error);
                InjectedIR=F.Bag()->GetInventoryRevision();InjectedWR=F.W()->GetCombatWeaponResourceRevision();
            }
            return true;
        });
        const auto Before=F.Bag()->GetInventorySlots();const auto IR=F.Bag()->GetInventoryRevision(),WR=F.W()->GetCombatWeaponResourceRevision();
        const auto R=F.Request(Item);const auto Result=FDWCombatEquipmentTransaction::Craft(*F.W(),R,Recipe,false);
        TestFalse(TEXT("Invalidated prepared craft rejects"),Result.bCommittedThisCall);
        TestEqual(TEXT("Ingredients not paid"),F.Bag()->CountItem(Material),2);TestEqual(TEXT("No physical weapon"),F.Bag()->CountItem(Item),0);
        TestTrue(TEXT("No ownership registered"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty());
        if(Mode==0)
        {
            TestTrue(TEXT("Leaving station changes no slots"),SameSlots(Before,F.Bag()->GetSlots()));
            TestEqual(TEXT("Leaving station changes no bag revision"),F.Bag()->GetInventoryRevision(),IR);
            TestEqual(TEXT("Leaving station changes no weapon revision"),F.W()->GetCombatWeaponResourceRevision(),WR);
        }
        else
        {
            TestTrue(TEXT("Independent mutation was exercised"),Injected);
            TestEqual(TEXT("Craft does not replace later inventory revision"),F.Bag()->GetInventoryRevision(),InjectedIR);
            TestEqual(TEXT("Craft does not replace later weapon revision"),F.W()->GetCombatWeaponResourceRevision(),InjectedWR);
            TestEqual(TEXT("Independent filler retained exactly once"),F.Bag()->CountItem(Filler),Mode==1?1:0);
        }
        F.Bag()->SetCraftingContextValidator(nullptr);F.End(R);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftPausedPlayer,"DoughWorld.Crafting.Permanent.PausedCraftMenuPlayerEntryWorks",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftPausedPlayer::RunTest(const FString&)
{
    using namespace DWE1CraftTests;FFixture F;if(!F.bReady)return false;const FName Item(TEXT("DWWeapon_Spear"));
    F.Bag()->SetSlots({FDWItemStack(Material,2)});auto* Pauser=F.World->SpawnActor<APlayerState>();if(!Pauser)return false;
    F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    TestTrue(TEXT("Real world is paused"),UGameplayStatics::IsGamePaused(F.P));
    TestFalse(TEXT("Combat qualification rejects pause"),F.P->IsCombatActionAllowed());
    TestTrue(TEXT("Craft preview remains available"),F.Bag()->CanCraft(RecipeFor(Item),false));
    TestTrue(TEXT("Real Player::CraftRecipe succeeds while craft menu pauses simulation"),F.P->CraftRecipe(RecipeFor(Item)));
    TestTrue(TEXT("Paused acquisition includes ownership"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Item));
    TestEqual(TEXT("Paused acquisition is exactly one item"),F.Bag()->CountItem(Item),1);
    TestTrue(TEXT("Paused crafting does not equip"),F.W()->GetCurrentEquipmentId().IsNone());return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftObserverReentry,"DoughWorld.Crafting.Permanent.ObserversSeeAtomicFactsAndCannotDuplicate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftObserverReentry::RunTest(const FString&)
{
    using namespace DWE1CraftTests;FFixture F;if(!F.bReady)return false;const FName Item(TEXT("DWWeapon_Shotgun"));
    F.Bag()->SetSlots({FDWItemStack(Material,4)});const auto IR=F.Bag()->GetInventoryRevision(),WR=F.W()->GetCombatWeaponResourceRevision();
    int32 InventoryNotices=0,WeaponNotices=0;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> I(NewObject<UDWInventoryTransactionTestProbe>());
    TStrongObjectPtr<UDWWeaponResourceTestProbe> W(NewObject<UDWWeaponResourceTestProbe>());
    I->TestHook=[&]()
    {
        ++InventoryNotices;
        TestEqual(TEXT("Observer sees paid ingredients"),F.Bag()->CountItem(Material),2);
        TestEqual(TEXT("Observer sees one physical weapon"),F.Bag()->CountItem(Item),1);
        TestTrue(TEXT("Observer sees ownership before callback"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Item));
        TestEqual(TEXT("Bag version adopted before callback"),F.Bag()->GetInventoryRevision(),IR+1);
        TestEqual(TEXT("Weapon version adopted before callback"),F.W()->GetCombatWeaponResourceRevision(),WR+1);
        TStrongObjectPtr<UDWSaveGame> MemorySave(NewObject<UDWSaveGame>());F.P->CaptureSaveData(MemorySave.Get());
        TestTrue(TEXT("In-memory save sees complete acquisition"),MemorySave->CombatPlayerResources.OwnedPermanentWeaponIds.Contains(Item)&&
            MemorySave->Inventory.ContainsByPredicate([&](const FDWItemStack& S){return S.ItemId==Item&&S.Quantity==1;}));
        TestFalse(TEXT("Reentrant acquisition refuses existing ownership"),FDWCombatEquipmentTransaction::TryCraft(*F.W(),RecipeFor(Item),false));
    };
    W->TestHook=[&](int32 Loaded,int64 Revision,bool Reload)
    {++WeaponNotices;TestEqual(TEXT("Inventory notice precedes weapon notice"),InventoryNotices,1);TestEqual(TEXT("No loaded shells gifted"),Loaded,0);TestEqual(TEXT("Notice version"),Revision,WR+1);TestFalse(TEXT("Craft is not reload"),Reload);};
    F.Bag()->OnChanged.AddDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    F.W()->OnWeaponResourceChanged.AddDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);
    TestTrue(TEXT("Player craft commits"),F.P->CraftRecipe(RecipeFor(Item)));
    TestEqual(TEXT("One inventory notification"),InventoryNotices,1);TestEqual(TEXT("One weapon notification"),WeaponNotices,1);
    F.Bag()->OnChanged.RemoveDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);I->TestHook=nullptr;
    F.W()->OnWeaponResourceChanged.RemoveDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);W->TestHook=nullptr;return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftPausedActiveAction,"DoughWorld.Crafting.Permanent.PausedAcquisitionPreservesActiveActionAndEquipment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftPausedActiveAction::RunTest(const FString&)
{
    using namespace DWE1CraftTests;FFixture F;if(!F.bReady)return false;const FName Item(TEXT("DWWeapon_Spear"));
    F.Bag()->SetSlots({FDWItemStack(Material,2)});
    if(!TestTrue(TEXT("Begin real formal empty-hand attack before opening menu"),F.W()->PrimaryPressed(F.P->GetActorLocation()+FVector(400,0,0))))return false;
    F.W()->PublishAcceptedWeaponStartEffects();
    if(!TestTrue(TEXT("A real action is active before pause"),F.W()->IsBusy()&&F.W()->IsFormalUnarmedActive()))return false;
    const auto Selection=F.W()->SelectedWeapon;const auto Equipment=F.W()->GetCurrentEquipmentId();
    const auto Role=F.W()->GetActiveRole();const auto Phase=F.W()->GetFormalUnarmedPhase();
    const auto Serial=F.W()->ActionSerial,Events=F.W()->DispatchedEventCount;
    const auto Boundary=F.W()->GetCombatBoundaryRevision();
    const auto Elapsed=F.W()->GetElapsedSeconds(),Position=F.W()->GetPositionSeconds(),Duration=F.W()->GetActionDuration();
    const auto Lock=F.W()->GetRemainingAttackLockSeconds();
    auto* Pauser=F.World->SpawnActor<APlayerState>();if(!Pauser)return false;
    // ShowMenu(Crafting) pauses an existing action; acquisition must neither cancel nor tick it.
    F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    TestTrue(TEXT("Paused simulation with active action is exercised"),UGameplayStatics::IsGamePaused(F.P)&&F.W()->IsBusy());
    TestFalse(TEXT("Global combat gate remains suspended"),F.P->IsCombatActionAllowed());
    TestTrue(TEXT("Paused active action does not block acquisition preview"),F.Bag()->CanCraft(RecipeFor(Item),false));
    TestTrue(TEXT("Player craft acquires new weapon while action stays frozen"),F.P->CraftRecipe(RecipeFor(Item)));
    TestTrue(TEXT("Active action was not cancelled"),F.W()->IsBusy()&&F.W()->IsFormalUnarmedActive());
    TestEqual(TEXT("Original action identity preserved"),F.W()->ActionSerial,Serial);
    TestEqual(TEXT("No action events dispatched by acquisition"),F.W()->DispatchedEventCount,Events);
    TestEqual(TEXT("Original gameplay boundary revision preserved"),F.W()->GetCombatBoundaryRevision(),Boundary);
    TestEqual(TEXT("Original gameplay role preserved"),F.W()->GetActiveRole(),Role);
    TestTrue(TEXT("Original formal phase preserved"),F.W()->GetFormalUnarmedPhase()==Phase);
    TestEqual(TEXT("Action elapsed time did not advance or restart"),F.W()->GetElapsedSeconds(),Elapsed);
    TestEqual(TEXT("Source position did not advance or restart"),F.W()->GetPositionSeconds(),Position);
    TestEqual(TEXT("Action duration preserved"),F.W()->GetActionDuration(),Duration);
    TestEqual(TEXT("Existing attack lock preserved"),F.W()->GetRemainingAttackLockSeconds(),Lock);
    TestTrue(TEXT("Acquisition retains original equipment and selection"),F.W()->GetCurrentEquipmentId()==Equipment&&F.W()->SelectedWeapon==Selection);
    TestTrue(TEXT("New identity is owned without becoming current"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Item)&&F.W()->GetCurrentEquipmentId()!=Item);
    TestEqual(TEXT("Exactly one newly crafted item"),F.Bag()->CountItem(Item),1);return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWE1CraftLifecycleReentry,"DoughWorld.Crafting.Permanent.ListenerRestoreOrDestroyWinsWithoutStaleNotice",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWE1CraftLifecycleReentry::RunTest(const FString&)
{
    using namespace DWE1CraftTests;const FName Item(TEXT("DWWeapon_Spear"));
    for(int32 Mode=0;Mode<2;++Mode)
    {
        FFixture F;if(!F.bReady)return false;F.Bag()->SetSlots({FDWItemStack(Material,2)});
        int32 InventoryNotices=0,WeaponNotices=0;bool Changed=false;
        TStrongObjectPtr<UDWInventoryTransactionTestProbe> I(NewObject<UDWInventoryTransactionTestProbe>());
        TStrongObjectPtr<UDWWeaponResourceTestProbe> W(NewObject<UDWWeaponResourceTestProbe>());
        I->TestHook=[&]()
        {
            ++InventoryNotices;TestTrue(TEXT("Full acquisition exists before destructive listener"),F.Bag()->CountItem(Item)==1&&F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.Contains(Item));
            if(Mode==0){FDWCombatWeaponResourceSnapshot Empty;Changed=F.W()->RestoreCombatWeaponState(Empty,*F.GI->GetCombatRules(),F.Error);}
            else Changed=F.P->Destroy();
        };
        W->TestHook=[&](int32,int64,bool){++WeaponNotices;};
        F.Bag()->OnChanged.AddDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
        F.W()->OnWeaponResourceChanged.AddDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);
        TestTrue(TEXT("Fixed acquisition remains successful after callback"),F.P->CraftRecipe(RecipeFor(Item)));
        TestTrue(TEXT("Restore/destroy listener exercised"),Changed);TestEqual(TEXT("One inventory notice"),InventoryNotices,1);
        TestEqual(TEXT("No old-epoch/life weapon notice"),WeaponNotices,0);
        if(Mode==0){TestTrue(TEXT("Later restored ownership remains authoritative"),F.W()->GetCombatWeaponStateSnapshot().OwnedPermanentWeaponIds.IsEmpty());TestEqual(TEXT("No post-listener bag rewrite"),F.Bag()->CountItem(Item),1);}
        F.Bag()->OnChanged.RemoveDynamic(I.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);I->TestHook=nullptr;
        F.W()->OnWeaponResourceChanged.RemoveDynamic(W.Get(),&UDWWeaponResourceTestProbe::OnResources);W->TestHook=nullptr;
    }
    return true;
}
#endif
