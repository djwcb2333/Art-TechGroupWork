#if WITH_DEV_AUTOMATION_TESTS
#include "DWGatheringWorldPersistence.h"
#include "DWCombatEffectsSave.h"
#include "DWGatheringWorld.h"
#include "DWGatheringPresentationComponent.h"
#include "DWPickupPresentationComponent.h"
#include "DWGatheringProfile.h"
#include "DWBuildingDefinition.h"
#include "DWPlaceableBuilding.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "DWSaveGame.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>
#include <type_traits>

namespace DWGatheringPersistenceTests
{
const FString Lang=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_LanguagePlaytest");
const FString Legacy=TEXT("/Game/DoughWorld/Maps/Gameplay/Levels/L_DoughWorld_CombatTest");
FDWGatheringWorldSave Running()
{FDWGatheringWorldSave S;S.Schema=1;S.MapPackage=Lang;return S;}
FDWGatheringActorSave Resource()
{
    FDWGatheringActorSave A;A.Id=TEXT("LANG01_RESOURCE_1");A.Kind=EDWGatheringSavedKind::Resource;
    A.ClassPath=ADWGatheringResource::StaticClass()->GetPathName();A.AuthoredPath=Lang+TEXT(".PersistentLevel.Resource1");
    A.MaxDurability=100.;A.Durability=75.;A.RequiredTool=TEXT("Axe");A.Drops={FDWItemStack(TEXT("Flour"),3)};return A;
}
FDWGatheringActorSave Pickup()
{
    FDWGatheringActorSave A;A.Id=TEXT("LANG01_DROP_1");A.Kind=EDWGatheringSavedKind::Pickup;
    A.ClassPath=ADWGatheringPickup::StaticClass()->GetPathName();A.bRuntimeSpawned=true;
    A.ItemId=TEXT("Flour");A.Quantity=2;return A;
}
struct FWorldScope
{
    UWorld* W=nullptr;
    FWorldScope()
    {
        if(!GEngine)return;
        W=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("GatheringSaveFacts")),GetTransientPackage());
        if(W){GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);W->InitializeActorsForPlay(FURL());}
    }
    ~FWorldScope(){if(W){W->DestroyWorld(false);GEngine->DestroyWorldContext(W);}}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSaveLegacy,
    "DoughWorld.Gathering.Persistence.LegacyEmptyAndStrictNewAdventure",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSaveLegacy::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FString Error;FDWGatheringWorldSave Empty,S;
    TestTrue(TEXT("Old maps accept completely empty optional block"),FDWGatheringWorldPersistence::Validate(Empty,Legacy,{},false,Error));
    TestFalse(TEXT("LANG cannot treat missing schema as a fresh session"),FDWGatheringWorldPersistence::Validate(Empty,Lang,{},false,Error));
    Empty.ActiveTool=TEXT("Axe");
    TestFalse(TEXT("Schema zero cannot conceal facts"),FDWGatheringWorldPersistence::Validate(Empty,Legacy,{},false,Error));
    TestTrue(TEXT("Explicit new LANG block"),FDWGatheringWorldPersistence::MakeNewAdventure(Lang,S,Error));
    TestEqual(TEXT("LANG schema"),S.Schema,1);
    S.Actors.Add(Resource());
    TestFalse(TEXT("Fresh marker cannot carry running resource damage"),FDWGatheringWorldPersistence::Validate(S,Lang,{},true,Error));
    TestFalse(TEXT("LANG block cannot be applied to another map"),FDWGatheringWorldPersistence::Validate(S,Legacy,{},false,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSaveValidation,
    "DoughWorld.Gathering.Persistence.StableIdentityFiniteFactsAndDropEntitlement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSaveValidation::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FString Error;auto S=Running();S.Actors.Add(Resource());
    TestTrue(TEXT("Partial durable resource is valid"),FDWGatheringWorldPersistence::Validate(S,Lang,{},false,Error));
    auto Bad=S;const FDWGatheringActorSave DuplicateActor=Bad.Actors[0];Bad.Actors.Add(DuplicateActor);
    TestFalse(TEXT("Duplicate identity rejects"),FDWGatheringWorldPersistence::Validate(Bad,Lang,{},false,Error));
    Bad=S;Bad.Actors[0].Durability=std::numeric_limits<double>::quiet_NaN();
    TestFalse(TEXT("NaN durability rejects"),FDWGatheringWorldPersistence::Validate(Bad,Lang,{},false,Error));
    Bad=S;Bad.Actors[0].Durability=0.;
    TestFalse(TEXT("Pending drops cannot be silently omitted"),FDWGatheringWorldPersistence::Validate(Bad,Lang,{},false,Error));
    Bad.Actors[0].bDropsReleased=true;
    TestTrue(TEXT("Settled exhausted entitlement is saved"),FDWGatheringWorldPersistence::Validate(Bad,Lang,{},false,Error));
    Bad=S;Bad.Actors[0].bDropsReleased=true;
    TestFalse(TEXT("Released rewards cannot coexist with remaining durability"),FDWGatheringWorldPersistence::Validate(Bad,Lang,{},false,Error));
    Bad=S;Bad.Actors[0].Id=TEXT("UEDPIE_0_FallbackActorPath");
    TestFalse(TEXT("PIE-dependent identity rejects"),FDWGatheringWorldPersistence::Validate(Bad,Lang,{},false,Error));
    Bad=S;Bad.Actors[0].bRuntimeSpawned=true;Bad.Actors[0].AuthoredPath.Reset();
    TestFalse(TEXT("Runtime resource spawning is outside the approved subset"),FDWGatheringWorldPersistence::Validate(Bad,Lang,{},false,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSavePickup,
    "DoughWorld.Gathering.Persistence.PickupQuantityClassAndToolInventoryAgreement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSavePickup::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FString Error;auto S=Running();S.Actors.Add(Pickup());
    TestTrue(TEXT("Approved runtime pickup"),FDWGatheringWorldPersistence::Validate(S,Lang,{},false,Error));
    S.Actors[0].Quantity=0;
    TestTrue(TEXT("Zero remaining pickup is retained as a fact"),FDWGatheringWorldPersistence::Validate(S,Lang,{},false,Error));
    S.Actors[0].Quantity=-1;
    TestFalse(TEXT("Negative pickup rejects"),FDWGatheringWorldPersistence::Validate(S,Lang,{},false,Error));
    S.Actors[0]=Pickup();S.Actors[0].ClassPath=TEXT("/Game/Unapproved.Arbitrary_C");
    TestFalse(TEXT("Arbitrary runtime class cannot be requested by a save"),FDWGatheringWorldPersistence::Validate(S,Lang,{},false,Error));
    S.Actors.Reset();S.ActiveTool=TEXT("Axe");
    TestFalse(TEXT("Tool absent from aggregate inventory rejects"),FDWGatheringWorldPersistence::Validate(S,Lang,{},false,Error));
    TestTrue(TEXT("Same aggregate inventory supplies selected tool"),FDWGatheringWorldPersistence::Validate(S,Lang,{FDWItemStack(TEXT("Axe"),1)},false,Error));
    S.ToolCooldownRemaining=std::numeric_limits<double>::infinity();
    TestFalse(TEXT("Infinite cooldown rejects"),FDWGatheringWorldPersistence::Validate(S,Lang,{FDWItemStack(TEXT("Axe"),1)},false,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSaveConfig,
    "DoughWorld.Gathering.Persistence.DedicatedProfileAndThreeAuditedBuildingIdentities",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSaveConfig::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FString Error;
    TestTrue(TEXT("Dedicated LANG profile identity"),FDWGatheringWorldPersistence::IsApprovedProfilePath(TEXT("/Game/DoughWorld/Core/Data/DA_DWLanguagePlaytest.DA_DWLanguagePlaytest")));
    TestFalse(TEXT("Old shared gathering profile does not receive a save exemption"),FDWGatheringWorldPersistence::IsApprovedProfilePath(TEXT("/Game/DoughWorld/Core/Data/DA_DWGathering_Test.DA_DWGathering_Test")));
    for(const TCHAR* Item:{TEXT("CraftingTable"),TEXT("Furnace"),TEXT("FermentationVat")})
    {
        const FString Definition=FString::Printf(TEXT("/Game/DoughWorld/Core/Data/DA_DWBuilding_%s.DA_DWBuilding_%s"),Item,Item);
        const FString Class=FString::Printf(TEXT("/Game/DoughWorld/Gameplay/Buildings/BP_DWBuilding_%s.BP_DWBuilding_%s_C"),Item,Item);
        TestTrue(TEXT("Representation tuple accepts one exact audited class/definition, independently of visual readiness"),FDWGatheringWorldPersistence::IsApprovedBuildingIdentity(FName(Item),Definition,Class));
        TestFalse(TEXT("An arbitrary registered replacement Blueprint is still rejected"),FDWGatheringWorldPersistence::IsApprovedBuildingIdentity(FName(Item),Definition,TEXT("/Game/Other/RegisteredButUnaudited.RegisteredButUnaudited_C")));
        TestFalse(TEXT("Definition path cannot be replaced just by preserving item id"),FDWGatheringWorldPersistence::IsApprovedBuildingIdentity(FName(Item),TEXT("/Game/Other/Definition.Definition"),Class));
    }
    TStrongObjectPtr<UDWGatheringProfile> OtherProfile(NewObject<UDWGatheringProfile>());
    TestFalse(TEXT("A valid-looking transient profile cannot bypass runtime profile admission"),FDWGatheringWorldPersistence::ValidateForConfig(*OtherProfile.Get(),Running(),{},Error));
    return true; // Full map actor-initialization-order checks still require the actual newly authored LANG map.
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSaveMemory,
    "DoughWorld.Gathering.Persistence.SaveGameSerializationIncludesWholeBlock",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSaveMemory::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;
    TStrongObjectPtr<UDWSaveGame> S(NewObject<UDWSaveGame>());S->MapPackage=Lang;S->CombatWorld.Gathering=Running();
    S->CombatWorld.Gathering.Actors={Resource(),Pickup()};S->Inventory={FDWItemStack(TEXT("Flour"),17)};
    TArray<uint8> Bytes;if(!TestTrue(TEXT("Serialize SaveGame to isolated memory"),UGameplayStatics::SaveGameToMemory(S.Get(),Bytes)))return false;
    TStrongObjectPtr<UDWSaveGame> Loaded(Cast<UDWSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes)));
    if(!TestNotNull(TEXT("Decode SaveGame"),Loaded.Get()))return false;
    TestTrue(TEXT("Optional gathering block and all rows survive serialization"),FDWGatheringWorldSave::StaticStruct()->CompareScriptStruct(&S->CombatWorld.Gathering,&Loaded->CombatWorld.Gathering,PPF_None));
    TestEqual(TEXT("Same aggregate retains bag"),Loaded->Inventory[0].Quantity,17);
    return true; // Memory serialization deliberately does not claim a real file save/load.
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSavePureFacts,
    "DoughWorld.Gathering.Persistence.AdoptionDoesNotResetPresentationOrReplayDrops",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSavePureFacts::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FWorldScope F;if(!TestNotNull(TEXT("Transient fixture world"),F.W))return false;
    auto* Resource=F.W->SpawnActor<ADWGatheringResource>();auto* PickupActor=F.W->SpawnActor<ADWGatheringPickup>();
    if(!TestNotNull(TEXT("Resource fixture"),Resource)||!TestNotNull(TEXT("Pickup fixture"),PickupActor))return false;
    Resource->GatheringPresentation->AcceptedHitCount=9;Resource->AdoptResourceState(0.,true,-1.f);
    TestEqual(TEXT("Pure adoption leaves the presentation counter unchanged"),Resource->GatheringPresentation->AcceptedHitCount,9);
    TestTrue(TEXT("Reward entitlement already consumed"),Resource->bDropsReleased);
    TestFalse(TEXT("Adoption cannot grant drops in a non-gathering fixture"),Resource->ReleaseDrops());
    PickupActor->AdoptGatheringPersistenceFacts(TEXT("Flour"),4);
    TestEqual(TEXT("Pickup logical quantity is adopted"),PickupActor->Quantity,4);
    TestEqual(TEXT("No success animation is inferred by fact assignment"),PickupActor->PickupPresentation->CollectionPlayCount,0);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringMeshSymmetry,
    "DoughWorld.Gathering.Persistence.ExhaustedThenAliveRestoresAuthoredMeshVisibilityAndCollision",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringMeshSymmetry::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FWorldScope F;FString Error;if(!F.W)return false;
    auto* Stump=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube"));if(!Stump)return false;
    for(int32 Case=0;Case<2;++Case)
    {
        auto* R=F.W->SpawnActor<ADWGatheringResource>();if(!R)return false;
        const bool Visible=Case==0,Hidden=Case!=0;
        const auto Collision=Case==0?ECollisionEnabled::QueryOnly:ECollisionEnabled::PhysicsOnly;
        R->SetActorEnableCollision(true);R->Mesh->SetVisibility(Visible,false);R->Mesh->SetHiddenInGame(Hidden,false);R->Mesh->SetCollisionEnabled(Collision);
        R->GatheringPresentation->Style=EDWGatheringDestructionStyle::TreeFall;R->GatheringPresentation->StumpMesh=Stump;
        // No PlayAcceptedHit/CaptureSource has happened in this fixture.
        R->AdoptResourceState(0.,true,-1.f);R->ResetGatheringPersistencePresentation();
        if(!TestTrue(TEXT("Publish exhausted facts without replaying a hit"),R->PublishGatheringPersistencePresentation(Error)))return false;
        R->SetActorEnableCollision(true); // The fixture has no GI profile; isolate the component mode from actor-level admission.
        TestFalse(TEXT("Exhausted primary mesh hidden"),R->Mesh->GetVisibleFlag());
        TestTrue(TEXT("Exhausted primary mesh cannot collide"),R->Mesh->GetCollisionEnabled()==ECollisionEnabled::NoCollision);
        TestNull(TEXT("Exhausted old save does not reconstruct a persistent stump"),R->FindComponentByTag(UStaticMeshComponent::StaticClass(),TEXT("DWGatheringPersistentStump")));
        TestTrue(TEXT("Restored exhausted resource keeps reward already released"),R->bDropsReleased);
        TestEqual(TEXT("Old save restore does not replay an accepted hit"),R->GatheringPresentation->AcceptedHitCount,0);
        R->AdoptResourceState(75.,false,-1.f);R->ResetGatheringPersistencePresentation();
        if(!TestTrue(TEXT("Publish earlier living facts"),R->PublishGatheringPersistencePresentation(Error)))return false;
        R->SetActorEnableCollision(true);
        TestEqual(TEXT("Original per-instance visible flag restored"),R->Mesh->GetVisibleFlag(),Visible);
        TestEqual(TEXT("Original hidden-in-game flag restored"),bool(R->Mesh->bHiddenInGame),Hidden);
        TestTrue(TEXT("Original custom collision mode restored, not a hardcoded preset"),R->Mesh->GetCollisionEnabled()==Collision);
        TestEqual(TEXT("No accepted hit was synthesized"),R->GatheringPresentation->AcceptedHitCount,0);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSaveBoundary,
    "DoughWorld.Gathering.Persistence.IncompleteInteractionRejectsWithoutConsumingEntitlement",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSaveBoundary::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FWorldScope F;FString Error;if(!F.W)return false;
    auto* R=F.W->SpawnActor<ADWGatheringResource>();auto* Q=F.W->SpawnActor<ADWGatheringPickup>();if(!R||!Q)return false;
    R->Durability=0.;R->bDropsReleased=false;
    TestFalse(TEXT("Pending reward blocks capture"),R->CanCaptureGatheringPersistence(Error));
    TestFalse(TEXT("Rejected capture leaves reward entitlement intact"),R->bDropsReleased);
    R->bDropsReleased=true;R->GatheringPresentation->bExhaustionStarted=true;R->GatheringPresentation->bContactedGround=false;
    TestFalse(TEXT("Awaiting real contact blocks capture"),R->CanCaptureGatheringPersistence(Error));
    R->GatheringPresentation->bContactedGround=true;
    TestTrue(TEXT("Settled facts permit capture"),R->CanCaptureGatheringPersistence(Error));
    R->GatheringPresentation->Style=EDWGatheringDestructionStyle::TreeFall;
    TestFalse(TEXT("Tree stump still dissolving blocks capture"),R->CanCaptureGatheringPersistence(Error));
    R->GatheringPresentation->bPresentationFinished=true;
    TestTrue(TEXT("Finished tree presentation permits capture"),R->CanCaptureGatheringPersistence(Error));
    Q->PickupPresentation->bCollectionActive=true;
    TestFalse(TEXT("Pickup commit effect blocks capture"),Q->CanCaptureGatheringPersistence(Error));
    TestEqual(TEXT("Rejected capture leaves world stock"),Q->Quantity,1);
    Q->PickupPresentation->bCollectionActive=false;
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringSavePlan,
    "DoughWorld.Gathering.Persistence.MoveOnlyPlanRejectsUnpreparedAndAllowsSafeAbort",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringSavePlan::RunTest(const FString&)
{
    static_assert(!std::is_copy_constructible<FDWGatheringRestorePlan>::value,"Restore plans must not duplicate adoption ownership.");
    FString Error;FDWGatheringRestorePlan Empty;
    TestFalse(TEXT("Unprepared plan cannot commit"),Empty.CanCommit(Error));
    Empty.DiscardStagedObjects();Empty.DiscardStagedObjects();
    FDWGatheringRestorePlan Moved(MoveTemp(Empty));
    TestFalse(TEXT("Moving an empty plan does not make it executable"),Moved.CanCommit(Error));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringAuthorityReferences,
    "DoughWorld.Gathering.Persistence.TypedAuthorityResourceCrosscheckAndLazyLegacy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringAuthorityReferences::RunTest(const FString&)
{
    using namespace DWGatheringPersistenceTests;FString Error;auto S=Running();S.Actors.Add(Resource());
    DWCombatCommit::FLedger Ledger;Ledger.SetWorldEpoch(10);Ledger.SetLife({1,1});
    FDWCombatAuthoritySave Empty;Empty.LastEntity=1;Empty.Ledger=Ledger.CaptureSnapshot();
    TestTrue(TEXT("Untouched resources need no authority registration"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,Empty,Error));
    Ledger.SetLife({2,3});FDWCombatAuthoritySave A;A.LastEntity=2;A.Ledger=Ledger.CaptureSnapshot();
    FDWCombatActorIdentitySave Actor;Actor.StableId=TEXT("gathering:resource:")+Lang+TEXT(":")+S.Actors[0].Id.ToString();
    Actor.Life={2,3};Actor.RestoreRevision=7;Actor.Kind=EDWCombatSavedActorKind::Other;A.Actors.Add(Actor);
    TestTrue(TEXT("Typed resource and exact ledger life/revision are accepted"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,A,Error));
    auto Bad=A;Bad.Actors[0].StableId=TEXT("gathering:resource:/Game/Other:")+S.Actors[0].Id.ToString();
    TestFalse(TEXT("Wrong map cannot bind a resource"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,Bad,Error));
    Bad=A;Bad.Actors[0].StableId+=TEXT("_Unknown");TestFalse(TEXT("Unknown id cannot bind"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,Bad,Error));
    Bad=A;Bad.Actors[0].StableId=TEXT("gathering:pickup:")+Lang+TEXT(":")+S.Actors[0].Id.ToString();
    TestFalse(TEXT("Unknown typed gathering namespace is not silently ignored"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,Bad,Error));
    Bad=A;Bad.Actors[0].Kind=EDWCombatSavedActorKind::Player;TestFalse(TEXT("Wrong actor kind cannot masquerade as resource"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,Bad,Error));
    Bad=A;++Bad.Actors[0].Life.Life;TestFalse(TEXT("Actor and ledger life must agree"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,Bad,Error));
    Bad=A;Bad.Actors[0].RestoreRevision=0;TestFalse(TEXT("Invalid restore revision is retained as a rejection"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(S,Bad,Error));
    auto Duplicate=S;const FDWGatheringActorSave Copy=Duplicate.Actors[0];Duplicate.Actors.Add(Copy);
    TestFalse(TEXT("Duplicate gathering rows reject before mapping"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(Duplicate,A,Error));
    FDWGatheringWorldSave LegacyBlock;
    TestTrue(TEXT("Legacy empty block remains valid without typed metadata"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(LegacyBlock,Empty,Error));
    TestFalse(TEXT("Schema zero cannot conceal typed resource references"),FDWGatheringWorldPersistence::ValidateAuthorityReferences(LegacyBlock,A,Error));
    return true;
}

#endif
