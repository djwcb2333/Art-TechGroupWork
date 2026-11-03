#if WITH_DEV_AUTOMATION_TESTS
#include "DWGatheringPhysicsRules.h"
#include "DWGatheringPresentationMath.h"
#include "DWGatheringPresentationComponent.h"
#include "DWGatheringWorld.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#include "Misc/AutomationTest.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPhysicsRulesTest,"DoughWorld.Gathering.Physics.GroundRulesAndContactClock",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPhysicsRulesTest::RunTest(const FString&)
{
    using namespace DWGatheringPhysicsRules;
    TestFalse(TEXT("No fallback before the configured deadline"),SettlementDeadlineReached(5.99f,6.f));
    TestTrue(TEXT("Deadline is inclusive and bounded"),SettlementDeadlineReached(6.f,6.f));
    TestTrue(TEXT("Repeated deadline checks stay ready without granting anything"),SettlementDeadlineReached(7.f,6.f));
    TestFalse(TEXT("Invalid elapsed cannot claim completion"),SettlementDeadlineReached(std::numeric_limits<float>::quiet_NaN(),6.f));
    TestFalse(TEXT("Negative elapsed cannot claim completion"),SettlementDeadlineReached(-1.f,6.f));
    TestFalse(TEXT("Invalid config uses finite default"),SettlementDeadlineReached(5.99f,std::numeric_limits<float>::infinity()));
    TestTrue(TEXT("Author delay is bounded at sixty seconds"),SettlementDeadlineReached(60.f,100000.f));
    TestTrue(TEXT("Static upward floor is contact"),IsGroundContact(true,false,false,false,1.f,.45f));
    TestFalse(TEXT("Player collision never starts ground sequence"),IsGroundContact(true,false,true,false,1.f,.45f));
    TestFalse(TEXT("Own stump is not the final ground"),IsGroundContact(true,true,false,false,1.f,.45f));
    TestFalse(TEXT("Other falling pieces cannot count as ground"),IsGroundContact(true,false,false,true,1.f,.45f));
    TestFalse(TEXT("Wall collision does not count as ground"),IsGroundContact(true,false,false,false,0.f,.45f));
    TestFalse(TEXT("Ceiling collision does not count as ground"),IsGroundContact(true,false,false,false,-1.f,.45f));
    TestFalse(TEXT("NaN normal is rejected"),IsGroundContact(true,false,false,false,std::numeric_limits<float>::quiet_NaN(),.45f));
    TestEqual(TEXT("Unlanded body never dissolves"),DissolveAfterContact(-1.f,2.f,1.15f),0.f);
    TestEqual(TEXT("1.99 seconds after contact is intact"),DissolveAfterContact(1.99f,2.f,1.15f),0.f);
    TestTrue(TEXT("2.5 seconds after contact is dissolving"),DissolveAfterContact(2.5f,2.f,1.15f)>0.f);
    TestEqual(TEXT("Full dissolve completes after configured hold plus fade"),DissolveAfterContact(3.2f,2.f,1.15f),1.f);
    TestEqual(TEXT("Per-node progress stays health-owned"),DWGatheringPresentationMath::GatheredFraction(75.,100.),.25f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPhysicsAssetsTest,"DoughWorld.Gathering.Physics.ImportedSimpleConvexAssets",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPhysicsAssetsTest::RunTest(const FString&)
{
    TArray<FString> Paths={TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Models/SugarTree/Physics/SM_DW_SugarTreeStump"),
        TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Models/SugarTree/Physics/SM_DW_SugarTreeUpper")};
    for(int32 I=0;I<8;++I)Paths.Add(FString::Printf(TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Models/ClumpedFlour/Fragments/SM_DW_FlourFragment_%02d"),I));
    for(const FString& Path:Paths)
    {
        auto* Mesh=LoadObject<UStaticMesh>(nullptr,*Path);
        if(!TestNotNull(*Path,Mesh))continue;
        const UBodySetup* Body=Mesh->GetBodySetup();
        if(!TestNotNull(*(Path+TEXT(" body setup")),Body))continue;
        TestTrue(*(Path+TEXT(" real convex hull present")),Body->AggGeom.ConvexElems.Num()>0);
        TestTrue(*(Path+TEXT(" not ComplexAsSimple")),Body->GetCollisionTraceFlag()!=CTF_UseComplexAsSimple);
        for(const auto& Convex:Body->AggGeom.ConvexElems)TestTrue(*(Path+TEXT(" convex vertices")),Convex.VertexData.Num()>=4);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringPhysicsReleaseTest,"DoughWorld.Gathering.Physics.BodyReleaseHasNoTimedMovementOrRewards",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWGatheringPhysicsReleaseTest::RunTest(const FString&)
{
    if(!GEngine)return false;
    auto* Upper=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Models/SugarTree/Physics/SM_DW_SugarTreeUpper"));
    auto* Stump=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Models/SugarTree/Physics/SM_DW_SugarTreeStump"));
    auto* Source=LoadObject<UStaticMesh>(nullptr,TEXT("/Game/DoughWorld/Gameplay/Resources/Art/Models/SugarTree/SM_SugarTree_Regular_4K"));
    if(!TestNotNull(TEXT("Imported upper"),Upper)||!TestNotNull(TEXT("Imported stump"),Stump)||!TestNotNull(TEXT("Original sugar tree"),Source))return false;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false,MakeUniqueObjectName(GetTransientPackage(),UWorld::StaticClass(),TEXT("DWPhysicalGatherTest")),GetTransientPackage());
    if(!World)return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    auto* Resource=World->SpawnActor<ADWGatheringResource>();
    if(Resource)
    {
        Resource->SetActorLocation(FVector(0,0,1000));Resource->SetActorHiddenInGame(false);
        Resource->Mesh->SetStaticMesh(Source);Resource->Mesh->SetRelativeTransform(FTransform::Identity);
        auto* C=Resource->GatheringPresentation.Get();C->Style=EDWGatheringDestructionStyle::TreeFall;
        C->UpperTreeMesh=Upper;C->StumpMesh=Stump;C->DustCount=0;
        C->PlayAcceptedHit(FVector(-200,0,1000),1.f,true);
        TestFalse(TEXT("Valid physics asset setup"),C->bPhysicsSetupFailed);
        TestTrue(TEXT("Stump retained independently"),C->HasPersistentStump());
        TestEqual(TEXT("One independent upper body"),C->GetActivePhysicsBodyCount(),1);
        auto* Body=C->GetUpperTreeComponent();
        if(TestNotNull(TEXT("Real upper component"),Body))
        {
            TestTrue(TEXT("Chaos simulation enabled"),Body->IsSimulatingPhysics());
            TestTrue(TEXT("Gravity enabled"),Body->IsGravityEnabled());
            TestEqual(TEXT("Player does not trigger physical contact"),Body->GetCollisionResponseToChannel(ECC_Pawn),ECR_Ignore);
            const FTransform Before=Body->GetComponentTransform();
            // Advance ONLY the presentation tick, not the physics world. No amount
            // of elapsed presentation time may teleport the rigid body or fake a hit.
            C->TickComponent(10.f,LEVELTICK_All,nullptr);
            TestTrue(TEXT("Presentation tick does not kinematically move body"),Body->GetComponentTransform().Equals(Before,.001));
            TestFalse(TEXT("No timed fake floor contact"),C->bContactedGround);
            TestEqual(TEXT("No ungrounded fade"),C->CurrentDissolve,0.f);
        }
        TestEqual(TEXT("Visual release cannot debit health"),Resource->Durability,100.);
        TestFalse(TEXT("Visual release cannot award loot"),Resource->bDropsReleased);
        C->PlayAcceptedHit(FVector(-200,0,1000),1.f,true);
        TestEqual(TEXT("Repeated exhausted callback cannot add another upper"),C->GetActivePhysicsBodyCount(),1);
        C->ResetPresentation();
        TestFalse(TEXT("Reset destroys temporary stump"),C->HasPersistentStump());
        TestEqual(TEXT("Reset cleans all detached rigid bodies"),C->GetPhysicsPieces().Num(),0);
        TestFalse(TEXT("Reset never replays loot"),Resource->bDropsReleased);
    }
    else AddError(TEXT("Could not spawn isolated resource"));
    World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    return true;
}
#endif
