#if WITH_DEV_AUTOMATION_TESTS
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWGameplayConfig.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <type_traits>

static_assert(!std::is_copy_constructible_v<FDWPreparedInventoryChange>);
static_assert(!std::is_copy_assignable_v<FDWPreparedInventoryChange>);

namespace DWInventoryTransactionTests
{
    struct FWorldScope
    {
        UWorld* World=nullptr;
        FWorldScope()
        {
            if(!GEngine)return;
            World=UWorld::CreateWorld(EWorldType::Game,false,
                MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWInventoryLifecycleWorld")),GetTransientPackage());
            if(!World)return;
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
        }
        ~FWorldScope()
        {
            if(!World)return;
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }
    };

    const FName Alpha(TEXT("DW_QA_Transaction_Alpha"));
    const FName Beta(TEXT("DW_QA_Transaction_Beta"));
    const FName Core(TEXT("DW_QA_Transaction_Core"));
    const FName RecipeId(TEXT("DW_QA_Transaction_Recipe"));

    struct FFixture
    {
        TStrongObjectPtr<UDWGameplayConfig> Config{NewObject<UDWGameplayConfig>()};
        TStrongObjectPtr<UDWInventoryComponent> Bag{NewObject<UDWInventoryComponent>()};
        bool bReady=false;
        FFixture()
        {
            // Only isolated transient objects register these synthetic test ids.
            Config->Items.Reset(); Config->Recipes.Reset();
            Config->MaxInventorySlots=24; Config->MaxStackSize=99;
            for(const FName Id:{Alpha,Beta,Core})
            {
                FDWItemDefinition Item; Item.ItemId=Id; Item.bUsable=Id==Alpha;
                Config->Items.Add(Item);
            }
            FDWRecipeDefinition Recipe; Recipe.RecipeId=RecipeId;
            Recipe.Inputs={FDWItemStack(Alpha,2)}; Recipe.Outputs={FDWItemStack(Beta,1)};
            Config->Recipes.Add(Recipe); Config->bRequireTransformationForCrafting=false;
            Bag->InitializeInventory(Config.Get());
            TMap<FName,int32> Overrides; Overrides.Add(Core,40);
            bReady=Bag->TrySetStackLimitOverrides(Overrides);
        }
    };

    bool SameSlots(const TArray<FDWItemStack>& A,const TArray<FDWItemStack>& B)
    {
        if(A.Num()!=B.Num())return false;
        for(int32 Index=0;Index<A.Num();++Index)
            if(A[Index].ItemId!=B[Index].ItemId||A[Index].Quantity!=B[Index].Quantity)return false;
        return true;
    }
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryPreparedNoSideEffectsTest,"DoughWorld.InventoryTransactions.PrepareNoSideEffectsAndSingleUse",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryPreparedNoSideEffectsTest::RunTest(const FString& Parameters)
{
    using namespace DWInventoryTransactionTests;
    FFixture F; TestTrue(TEXT("Fixture policy"),F.bReady); F.Bag->SetSlots({FDWItemStack(Alpha,2)});
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    int32 Notifications=0; Probe->TestHook=[&Notifications](){++Notifications;};
    F.Bag->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    const auto Before=F.Bag->GetInventorySlots(); const int64 Revision=F.Bag->GetInventoryRevision();
    FDWPreparedInventoryChange Prepared;
    TestTrue(TEXT("Prepare split add"),F.Bag->PrepareAddItem(Core,41,Prepared));
    TestTrue(TEXT("Preparation keeps live slots"),SameSlots(Before,F.Bag->GetSlots()));
    TestEqual(TEXT("Preparation keeps revision"),F.Bag->GetInventoryRevision(),Revision);
    TestEqual(TEXT("Preparation never broadcasts"),Notifications,0);
    TestEqual(TEXT("Plan captures expected revision"),Prepared.GetExpectedRevision(),Revision);
    const auto& Candidate=Prepared.GetCandidateSlots();
    TestEqual(TEXT("Candidate contains existing stack plus 40 and 1"),Candidate.Num(),3);
    if(Candidate.Num()==3){TestEqual(TEXT("Core cap"),Candidate[1].Quantity,40);TestEqual(TEXT("Core remainder"),Candidate[2].Quantity,1);}
    TestTrue(TEXT("First commit succeeds"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    TestTrue(TEXT("Plan consumed before return"),Prepared.IsConsumed());
    TestEqual(TEXT("Exactly one new revision"),F.Bag->GetInventoryRevision(),Revision+1);
    TestEqual(TEXT("Exactly one commit notification"),Notifications,1);
    TestFalse(TEXT("Duplicate commit rejected"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    TestEqual(TEXT("Duplicate emits nothing"),Notifications,1); TestEqual(TEXT("No duplicate items"),F.Bag->CountItem(Core),41);
    TestTrue(TEXT("Prepare another valid action"),F.Bag->PrepareAddItem(Beta,1,Prepared));
    TestFalse(TEXT("Failed reprepare rejects missing quantity"),F.Bag->PrepareRemoveItem(Alpha,99,Prepared));
    TestTrue(TEXT("Failed preparation invalidates prior output handle"),Prepared.IsConsumed());
    TestFalse(TEXT("Old prepared action cannot accidentally survive failed preparation"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryPreparedIdentityTest,"DoughWorld.InventoryTransactions.OwnerRevisionAndMoveIdentity",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryPreparedIdentityTest::RunTest(const FString& Parameters)
{
    using namespace DWInventoryTransactionTests;
    FFixture A,B; A.Bag->SetSlots({FDWItemStack(Alpha,2)}); B.Bag->SetSlots({FDWItemStack(Alpha,2)});
    TestEqual(TEXT("Owners deliberately have same revision"),A.Bag->GetInventoryRevision(),B.Bag->GetInventoryRevision());
    FDWPreparedInventoryChange Original; A.Bag->PrepareAddItem(Alpha,1,Original);
    TestFalse(TEXT("Equal revision in another bag cannot accept token"),B.Bag->CommitPrepared(MoveTemp(Original)));
    TestFalse(TEXT("Wrong-owner rejection does not consume rightful token"),Original.IsConsumed());
    FDWPreparedInventoryChange Moved(MoveTemp(Original));
    TestTrue(TEXT("Moving invalidates source token"),Original.IsConsumed());
    TestFalse(TEXT("Moved-from token cannot clear entire inventory"),A.Bag->CommitPrepared(MoveTemp(Original)));
    TestTrue(TEXT("Moved token still belongs to correct owner"),A.Bag->CommitPrepared(MoveTemp(Moved)));
    TestEqual(TEXT("Original inventory adds once"),A.Bag->CountItem(Alpha),3);
    TestEqual(TEXT("Other owner untouched"),B.Bag->CountItem(Alpha),2);
    A.Bag->PrepareRemoveItem(Alpha,1,Original);
    A.Bag->TryAddItem(Alpha,1); A.Bag->TryRemoveItem(Alpha,1);
    TestEqual(TEXT("ABA restores count"),A.Bag->CountItem(Alpha),3);
    TestFalse(TEXT("ABA cannot restore transaction revision"),A.Bag->CommitPrepared(MoveTemp(Original)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryPreparedValidationTest,"DoughWorld.InventoryTransactions.RevalidateFullCandidateAndExactSlot",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryPreparedValidationTest::RunTest(const FString& Parameters)
{
    using namespace DWInventoryTransactionTests;
    FFixture F; F.Bag->SetSlots({FDWItemStack(Alpha,2),FDWItemStack(Alpha,3)});
    FDWInventorySlotSnapshot Snapshot; F.Bag->CaptureSlotSnapshot(1,Snapshot);
    FDWPreparedInventoryChange Prepared;
    if(!TestTrue(TEXT("Prepare exact usable slot"),F.Bag->PrepareConsumeSnapshot(Snapshot,Prepared)))return false;
    if(!TestEqual(TEXT("Exact-slot candidate preserves two stacks"),Prepared.GetCandidateSlots().Num(),2))return false;
    TestEqual(TEXT("Other same-item stack not consumed in candidate"),Prepared.GetCandidateSlots()[0].Quantity,2);
    TestEqual(TEXT("Selected stack loses exactly one in candidate"),Prepared.GetCandidateSlots()[1].Quantity,2);
    const auto Before=F.Bag->GetInventorySlots(); const int64 Revision=F.Bag->GetInventoryRevision();
    F.Config->Items[0].bUsable=false;
    TestFalse(TEXT("Commit rechecks usable definition"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    TestTrue(TEXT("Rejected usable change preserves all slots"),SameSlots(Before,F.Bag->GetSlots()));
    TestEqual(TEXT("Rejection preserves revision"),F.Bag->GetInventoryRevision(),Revision);
    F.Config->Items[0].bUsable=true; TestTrue(TEXT("Restored valid policy allows unchanged token"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    TestEqual(TEXT("Exact slot committed"),F.Bag->GetSlots()[1].Quantity,2);
    F.Bag->PrepareAddItem(Core,41,Prepared);
    const auto BeforeCap=F.Bag->GetInventorySlots(); const int64 BeforeCapRevision=F.Bag->GetInventoryRevision();
    F.Config->MaxInventorySlots=2;
    TestFalse(TEXT("Commit validates entire candidate under current capacity"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    TestTrue(TEXT("Over-capacity commit loses nothing"),SameSlots(BeforeCap,F.Bag->GetSlots()));
    TestEqual(TEXT("Capacity rejection never commits a revision"),F.Bag->GetInventoryRevision(),BeforeCapRevision);
    F.Config->MaxInventorySlots=24;
    F.Bag->CaptureSlotSnapshot(0,Snapshot); F.Bag->TryAddItem(Beta,1);
    TestFalse(TEXT("Preparing from stale snapshot rejects"),F.Bag->PrepareRemoveSnapshot(Snapshot,1,Prepared));
    TestTrue(TEXT("Stale preparation invalidates result"),Prepared.IsConsumed());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryStackPolicyTest,"DoughWorld.InventoryTransactions.PerItemCapsAndAtomicPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryStackPolicyTest::RunTest(const FString& Parameters)
{
    using namespace DWInventoryTransactionTests;
    FFixture F;
    TestEqual(TEXT("Core has injected 40 cap"),F.Bag->GetEffectiveStackLimit(Core),40);
    TestEqual(TEXT("Legacy item keeps global 99"),F.Bag->GetEffectiveStackLimit(Alpha),99);
    TestTrue(TEXT("Core add spans three valid stacks"),F.Bag->TryAddItem(Core,81));
    TestEqual(TEXT("81 core uses 3 slots"),F.Bag->GetSlots().Num(),3);
    F.Config->MaxInventorySlots=3; const auto Full=F.Bag->GetInventorySlots();
    TestFalse(TEXT("Capacity counts per-item cap, not global cap"),F.Bag->TryAddItem(Core,40));
    TestTrue(TEXT("Failed add keeps exact contents"),SameSlots(Full,F.Bag->GetSlots()));
    F.Config->MaxInventorySlots=24;
    TestFalse(TEXT("Save setter rejects core over per-item cap"),F.Bag->SetSlots({FDWItemStack(Core,41)}));
    TestTrue(TEXT("Legacy stack above 40 still allowed"),F.Bag->SetSlots({FDWItemStack(Alpha,90)}));
    TestTrue(TEXT("Prepare mixed partial stacks"),F.Bag->SetSlots({FDWItemStack(Core,25),FDWItemStack(Core,30),FDWItemStack(Alpha,70),FDWItemStack(Alpha,20)}));
    TestTrue(TEXT("Quick stack all uses each own cap"),F.Bag->TryQuickStack(NAME_None));
    TestEqual(TEXT("Core quick-stack total conserved"),F.Bag->CountItem(Core),55);
    TestEqual(TEXT("Legacy quick-stack total conserved"),F.Bag->CountItem(Alpha),90);
    TestTrue(TEXT("Quick-stack output validates"),F.Bag->AreSlotsValid(F.Bag->GetSlots()));
    if(!TestTrue(TEXT("Prepare two core stacks for merge"),F.Bag->SetSlots({FDWItemStack(Core,25),FDWItemStack(Core,30)})))return false;
    FDWInventorySlotSnapshot Snapshot; F.Bag->CaptureSlotSnapshot(1,Snapshot);
    TestTrue(TEXT("Move merges only to per-item cap"),F.Bag->TryMoveOrMergeSnapshot(Snapshot,0));
    TestEqual(TEXT("Target capped at 40"),F.Bag->GetSlots()[0].Quantity,40);
    TestEqual(TEXT("Overflow stays at source"),F.Bag->GetSlots()[1].Quantity,15);
    int32 NewSlot=INDEX_NONE; TestTrue(TEXT("Core split uses same validity policy"),F.Bag->TrySplitSlot(0,5,NewSlot));
    TestTrue(TEXT("Split result validates"),F.Bag->AreSlotsValid(F.Bag->GetSlots()));
    TestEqual(TEXT("Split preserves every core"),F.Bag->CountItem(Core),55);
    FDWPreparedInventoryChange Prepared; F.Bag->PrepareAddItem(Beta,1,Prepared);
    const int64 Revision=F.Bag->GetInventoryRevision(); const auto Before=F.Bag->GetInventorySlots();
    TMap<FName,int32> Bad; Bad.Add(Core,10);
    TestFalse(TEXT("Policy cannot silently split or delete over-cap old stacks"),F.Bag->TrySetStackLimitOverrides(Bad));
    TestEqual(TEXT("Rejected policy preserves cap"),F.Bag->GetEffectiveStackLimit(Core),40);
    TestTrue(TEXT("Rejected policy preserves slots"),SameSlots(Before,F.Bag->GetSlots()));
    TestEqual(TEXT("Rejected policy preserves revision"),F.Bag->GetInventoryRevision(),Revision);
    Bad.Reset(); Bad.Add(TEXT("DW_QA_NotRegistered"),40);
    TestFalse(TEXT("Unknown override id rejected"),F.Bag->TrySetStackLimitOverrides(Bad));
    Bad.Reset(); Bad.Add(Core,0); TestFalse(TEXT("Zero cap rejected"),F.Bag->TrySetStackLimitOverrides(Bad));
    TMap<FName,int32> Valid; Valid.Add(Core,40);
    TestTrue(TEXT("Valid explicit policy install succeeds"),F.Bag->TrySetStackLimitOverrides(Valid));
    TestFalse(TEXT("Policy install invalidates outstanding token"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    FFixture LowGlobal; LowGlobal.Config->MaxStackSize=10;
    TestTrue(TEXT("Explicit core cap may exceed old global cap"),LowGlobal.Bag->TryAddItem(Core,41));
    TestEqual(TEXT("Core still uses 40 plus 1 with global cap 10"),LowGlobal.Bag->GetSlots().Num(),2);
    TestTrue(TEXT("Old item still uses smaller global cap"),LowGlobal.Bag->TryAddItem(Alpha,11));
    TestEqual(TEXT("Legacy 11 requires two more slots at global cap 10"),LowGlobal.Bag->GetSlots().Num(),4);
    TestTrue(TEXT("Both cap directions validate"),LowGlobal.Bag->AreSlotsValid(LowGlobal.Bag->GetSlots()));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryNotifyReentryTest,"DoughWorld.InventoryTransactions.NotifyAfterCompleteStateAndReentry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryNotifyReentryTest::RunTest(const FString& Parameters)
{
    using namespace DWInventoryTransactionTests;
    FFixture F; F.Bag->SetSlots({FDWItemStack(Alpha,2)});
    FDWPreparedInventoryChange Prepared; F.Bag->PrepareRemoveItem(Alpha,1,Prepared);
    const int64 Revision=F.Bag->GetInventoryRevision(); int32 Notifications=0; bool bEntered=false;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    Probe->TestHook=[&]()
    {
        ++Notifications;
        TestTrue(TEXT("Synchronous listener already sees consumed outer token"),Prepared.IsConsumed());
        TestEqual(TEXT("Synchronous listener sees final outer slots"),F.Bag->CountItem(Alpha),1);
        if(!bEntered)
        {
            bEntered=true;
            TestEqual(TEXT("Revision committed before first callback"),F.Bag->GetInventoryRevision(),Revision+1);
            TestFalse(TEXT("Callback cannot replay outer transaction"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
            TestTrue(TEXT("Callback may independently commit new valid transaction"),F.Bag->TryAddItem(Beta,1));
        }
        else
        {
            TestEqual(TEXT("Nested callback sees full nested state"),F.Bag->CountItem(Beta),1);
            TestEqual(TEXT("Nested callback sees next revision"),F.Bag->GetInventoryRevision(),Revision+2);
        }
    };
    F.Bag->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Outer transaction returns success after synchronous reentry"),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    TestEqual(TEXT("Two real commits produce two notifications"),Notifications,2);
    TestEqual(TEXT("No duplicate consumption"),F.Bag->CountItem(Alpha),1);
    TestEqual(TEXT("Nested write preserved"),F.Bag->CountItem(Beta),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryLegacyRevisionTest,"DoughWorld.InventoryTransactions.LegacyEntryPointsInvalidatePreparedChanges",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryLegacyRevisionTest::RunTest(const FString& Parameters)
{
    using namespace DWInventoryTransactionTests;
    FFixture F;
    auto Verify=[&](const TCHAR* Name,TFunctionRef<bool()> Action)
    {
        F.Bag->SetSlots({FDWItemStack(Alpha,10),FDWItemStack(Alpha,5),FDWItemStack(Beta,2)});
        FDWPreparedInventoryChange Prepared; F.Bag->PrepareAddItem(Alpha,1,Prepared);
        const int64 Revision=F.Bag->GetInventoryRevision();
        TestTrue(Name,Action());
        TestEqual(FString::Printf(TEXT("%s commits exactly one revision"),Name),F.Bag->GetInventoryRevision(),Revision+1);
        TestFalse(FString::Printf(TEXT("%s rejects preceding token"),Name),F.Bag->CommitPrepared(MoveTemp(Prepared)));
    };
    Verify(TEXT("TryAddItem"),[&](){return F.Bag->TryAddItem(Alpha,1);});
    Verify(TEXT("TryRemoveItem"),[&](){return F.Bag->TryRemoveItem(Alpha,1);});
    Verify(TEXT("TryCraft"),[&](){return F.Bag->TryCraft(RecipeId,false);});
    Verify(TEXT("ConsumeOneAtSlot"),[&](){return F.Bag->ConsumeOneAtSlot(0);});
    Verify(TEXT("SetSlots"),[&](){return F.Bag->SetSlots({FDWItemStack(Alpha,7)});});
    Verify(TEXT("TrySplitSlot"),[&](){int32 Out=INDEX_NONE;return F.Bag->TrySplitSlot(0,2,Out)&&Out==3;});
    Verify(TEXT("TryQuickStack"),[&](){return F.Bag->TryQuickStack(Alpha);});
    Verify(TEXT("TryMoveOrMergeSnapshot"),[&](){FDWInventorySlotSnapshot S;return F.Bag->CaptureSlotSnapshot(1,S)&&F.Bag->TryMoveOrMergeSnapshot(S,0);});
    Verify(TEXT("TryDiscardSnapshot"),[&](){FDWInventorySlotSnapshot S;return F.Bag->CaptureSlotSnapshot(0,S)&&F.Bag->TryDiscardSnapshot(S);});
    Verify(TEXT("InitializeInventory"),[&](){F.Bag->InitializeInventory(F.Config.Get());return true;});
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWInventoryLifecycleWriteTest,"DoughWorld.InventoryTransactions.DestructionCallbacksRejectWritesBeforeGarbage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWInventoryLifecycleWriteTest::RunTest(const FString& Parameters)
{
    using namespace DWInventoryTransactionTests;
    FFixture F;
    TestTrue(TEXT("Ownerless transient inventory remains writable"),F.Bag->SetSlots({FDWItemStack(Alpha,5)}));
    FDWPreparedInventoryChange DetachedPlan;
    if(!TestTrue(TEXT("Prepare before component lifecycle ends"),F.Bag->PrepareAddItem(Beta,1,DetachedPlan)))return false;
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    int32 Notifications=0;
    Probe->TestHook=[&Notifications](){++Notifications;};
    F.Bag->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);

    auto VerifyAllWritesReject=[&](UDWInventoryComponent* Bag,FDWPreparedInventoryChange& Prepared)
    {
        TestTrue(TEXT("Destruction boundary still precedes UObject garbage marking"),IsValid(Bag));
        const auto Before=Bag->GetInventorySlots(); const int64 Revision=Bag->GetInventoryRevision();
        FDWInventorySlotSnapshot Snapshot; Bag->CaptureSlotSnapshot(0,Snapshot);
        FDWPreparedInventoryChange Rejected;
        TestFalse(TEXT("Pre-destruction prepared token cannot commit"),Bag->CommitPrepared(MoveTemp(Prepared)));
        TestFalse(TEXT("Rejected lifecycle commit does not consume handle"),Prepared.IsConsumed());
        TestFalse(TEXT("Prepare add rejects lifecycle end"),Bag->PrepareAddItem(Beta,1,Rejected));
        TestFalse(TEXT("Prepare remove rejects lifecycle end"),Bag->PrepareRemoveItem(Alpha,1,Rejected));
        TestFalse(TEXT("Prepare exact slot rejects lifecycle end"),Bag->PrepareRemoveSnapshot(Snapshot,1,Rejected));
        TestFalse(TEXT("Prepare usable item rejects lifecycle end"),Bag->PrepareConsumeSnapshot(Snapshot,Rejected));
        TestTrue(TEXT("Failed preparation leaves invalid output"),Rejected.IsConsumed());
        TMap<FName,int32> Overrides; Overrides.Add(Core,40);
        TestFalse(TEXT("Policy change rejects lifecycle end"),Bag->TrySetStackLimitOverrides(Overrides));
        Bag->InitializeInventory(F.Config.Get());
        TestFalse(TEXT("Legacy add rejects lifecycle end"),Bag->TryAddItem(Beta,1));
        TestFalse(TEXT("Legacy remove rejects lifecycle end"),Bag->TryRemoveItem(Alpha,1));
        TestFalse(TEXT("Legacy consume rejects lifecycle end"),Bag->ConsumeOneAtSlot(0));
        TestFalse(TEXT("Legacy craft rejects lifecycle end"),Bag->TryCraft(RecipeId,false));
        TestFalse(TEXT("Save slot setter rejects lifecycle end"),Bag->SetSlots({FDWItemStack(Alpha,1)}));
        TestFalse(TEXT("Discard rejects lifecycle end"),Bag->TryDiscardSnapshot(Snapshot));
        int32 NewSlot=123;
        TestFalse(TEXT("Split rejects lifecycle end"),Bag->TrySplitSlot(0,1,NewSlot));
        TestEqual(TEXT("Rejected split retains old failure output"),NewSlot,INDEX_NONE);
        TestTrue(TEXT("Lifecycle rejection preserves every slot"),SameSlots(Before,Bag->GetSlots()));
        TestEqual(TEXT("Lifecycle rejection preserves revision"),Bag->GetInventoryRevision(),Revision);
    };

    // The hierarchy teardown path invokes this callback directly before MarkAsGarbage.
    // It does not necessarily pass through DestroyComponent / bIsBeingDestroyed.
    F.Bag->OnComponentDestroyed(true);
    TestFalse(TEXT("Direct lifecycle callback does not set engine DestroyComponent flag"),F.Bag->IsBeingDestroyed());
    VerifyAllWritesReject(F.Bag.Get(),DetachedPlan);
    TestEqual(TEXT("Component lifecycle rejection never broadcasts"),Notifications,0);

    FWorldScope Scope;
    if(!TestNotNull(TEXT("Isolated transient world"),Scope.World))return false;
    AActor* Owner=Scope.World->SpawnActor<AActor>();
    if(!TestNotNull(TEXT("Transient inventory owner"),Owner))return false;
    TStrongObjectPtr<UDWInventoryComponent> OwnedBag(NewObject<UDWInventoryComponent>(Owner));
    Owner->AddInstanceComponent(OwnedBag.Get()); OwnedBag->RegisterComponent();
    OwnedBag->InitializeInventory(F.Config.Get()); OwnedBag->SetSlots({FDWItemStack(Alpha,5)});
    FDWPreparedInventoryChange OwnedPlan;
    if(!TestTrue(TEXT("Owned inventory initially writable"),OwnedBag->PrepareAddItem(Beta,1,OwnedPlan)))return false;
    OwnedBag->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    bool bObservedActualDestroy=false;
    const FDelegateHandle Handle=Scope.World->AddOnActorDestroyedHandler(FOnActorDestroyed::FDelegate::CreateLambda(
        [&](AActor* Destroyed)
        {
            if(Destroyed!=Owner)return;
            bObservedActualDestroy=true;
            TestTrue(TEXT("Real actor destroy callback still has a valid UObject"),IsValid(Owner));
            TestTrue(TEXT("Owner destruction flag is already set"),Owner->IsActorBeingDestroyed());
            TestFalse(TEXT("Component has not independently entered DestroyComponent"),OwnedBag->IsBeingDestroyed());
            VerifyAllWritesReject(OwnedBag.Get(),OwnedPlan);
        }));
    const bool bDestroyAccepted=Owner->Destroy();
    Scope.World->RemoveOnActorDestroyedHandler(Handle);
    TestTrue(TEXT("Real actor destruction succeeded"),bDestroyAccepted);
    TestTrue(TEXT("Verified writes inside engine destruction callback"),bObservedActualDestroy);
    TestEqual(TEXT("Owner destruction rejection never broadcasts"),Notifications,0);
    return true;
}
#endif
