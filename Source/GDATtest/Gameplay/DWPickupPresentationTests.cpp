#if WITH_DEV_AUTOMATION_TESTS
#include "DWPickupPresentationComponent.h"
#include "DWPickupPresentationMath.h"
#include "DWGatheringWorld.h"
#include "DWGatheringProfile.h"
#include "DWPlayerCharacter.h"
#include "DWInventoryComponent.h"
#include "DWInventoryTransactionTestProbe.h"
#include "DWInteractionPromptComponent.h"
#include "DWCombatTestWorld.h"
#include "DWWeaponAnimationComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPickupMathTest,
    "DoughWorld.Gathering.Pickup.NormalizeElasticEnvelope",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPickupMathTest::RunTest(const FString&)
{
    using namespace DWPickupPresentationMath;
    const auto Start=Evaluate(0.f),Squash=Evaluate(.16f),Peak=Evaluate(.50f),End=Evaluate(1.f);
    TestEqual(TEXT("Initial height intact"),Start.Z,1.f);
    TestEqual(TEXT("Initial opacity intact"),Start.Opacity,1.f);
    TestTrue(TEXT("Anticipation compresses vertically"),Squash.Z<Start.Z&&Squash.XY>Start.XY);
    TestTrue(TEXT("Successful pickup visibly stretches and brightens"),Peak.Z>1.7f&&Peak.Flash>.99f);
    TestEqual(TEXT("Completion removes visual width"),End.XY,0.f);
    TestEqual(TEXT("Completion removes visual height"),End.Z,0.f);
    TestEqual(TEXT("Completion removes glow"),End.Flash,0.f);
    TestEqual(TEXT("100 cm mesh normalized to 30 cm"),NormalizeScale(100.f,30.f),.3f);
    TestEqual(TEXT("Small 10 cm mesh is enlarged to same width"),NormalizeScale(10.f,30.f),3.f);
    TestEqual(TEXT("Degenerate geometry cannot divide by zero"),NormalizeScale(0.f,30.f),1.f);
    TestTrue(TEXT("NaN progress remains finite"),FMath::IsFinite(Evaluate(std::numeric_limits<float>::quiet_NaN()).Z));
    for(float Boundary:{.16f,.50f,.72f})
    {
        const auto A=Evaluate(Boundary-.00001f),B=Evaluate(Boundary+.00001f);
        TestTrue(TEXT("Piecewise curve is continuous"),FMath::Abs(A.Z-B.Z)<.001f&&FMath::Abs(A.XY-B.XY)<.001f);
    }
    return true;
}

namespace DWPickupTests
{
struct FFixture
{
    TStrongObjectPtr<UDWGameInstance> GI;
    TStrongObjectPtr<UDWGatheringProfile> Config;
    UWorld* World=nullptr;ADWPlayerCharacter* Player=nullptr;ADWGatheringPickup* Pickup=nullptr;
    bool Ready=false;
    FFixture()
    {
        World=DWCombatTestWorld::Create(GI,TEXT("DWPickupVisualTransactionWorld"));if(!World)return;
        if(!Cast<UDWGatheringProfile>(GI->GetConfig()))return;
        FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        Player=World->SpawnActor<ADWPlayerCharacter>(ADWPlayerCharacter::StaticClass(),FVector(0,0,100),FRotator::ZeroRotator,P);
        if(!Player)return;if(!Player->HasActorBegunPlay())Player->DispatchBeginPlay();
        Player->SetActorTickEnabled(false);if(Player->WeaponAnimationComponent)Player->WeaponAnimationComponent->bEnabled=false;
        if(!Player->HasCombatVitalsReady()||!Cast<UDWGatheringProfile>(Player->GetGameplayConfig()))return;
        Config.Reset(DuplicateObject<UDWGatheringProfile>(Cast<UDWGatheringProfile>(Player->GetGameplayConfig()),Player));
        Config->MaxInventorySlots=1;Config->MaxStackSize=5;Config->StackLimits.Add(TEXT("SugarChunk"),5);
        if(!Bag()->SetSlots({}))return;Player->GameplayConfig=Config.Get();Bag()->InitializeInventory(Config.Get());
        Pickup=World->SpawnActor<ADWGatheringPickup>(ADWGatheringPickup::StaticClass(),FVector(100,0,100),FRotator::ZeroRotator,P);
        if(!Pickup)return;Pickup->ItemId=TEXT("SugarChunk");Pickup->Quantity=5;
        if(!Pickup->HasActorBegunPlay())Pickup->DispatchBeginPlay();Pickup->RefreshPickup();
        Ready=Pickup->IsInteractionAvailable()&&Bag()->SetSlots({FDWItemStack(TEXT("SugarChunk"),4)});
    }
    UDWInventoryComponent* Bag()const{return Player?Player->GetInventory():nullptr;}
    ~FFixture()
    {
        if(!World)return;
        for(TActorIterator<AActor> It(World);It;++It)if(It->HasActorBegunPlay())It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        if(GI.IsValid())GI->Shutdown();World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPickupTransactionVisualTest,
    "DoughWorld.Gathering.Pickup.CommitPartialReentryAndVisualLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPickupTransactionVisualTest::RunTest(const FString&)
{
    using namespace DWPickupTests;FFixture F;if(!TestTrue(TEXT("Isolated real gathering player and inventory"),F.Ready))return false;
    auto* C=F.Pickup->PickupPresentation.Get();if(!TestNotNull(TEXT("Dedicated pickup presentation"),C))return false;
    // Same-mesh authored material overrides and anisotropic proportions survive normalization.
    auto* AuthoredMaterial=UMaterialInstanceDynamic::Create(F.Pickup->Mesh->GetMaterial(0),F.Pickup);
    if(!TestNotNull(TEXT("Distinct authored material fixture"),AuthoredMaterial))return false;
    F.Pickup->Mesh->SetMaterial(0,AuthoredMaterial);
    F.Pickup->Mesh->SetRelativeScale3D(FVector(.4,.6,.8));
    C->NormalizePickupVisual();
    const FVector NormalizedScale=F.Pickup->Mesh->GetRelativeScale3D();
    TestTrue(TEXT("Normalization preserves authored XY proportions"),FMath::IsNearlyEqual(NormalizedScale.Y/NormalizedScale.X,1.5,.0001));
    TestTrue(TEXT("Normalization preserves authored Z proportions"),FMath::IsNearlyEqual(NormalizedScale.Z/NormalizedScale.X,2.,.0001));
    TestTrue(TEXT("Normalization preserves same-mesh custom material"),F.Pickup->Mesh->GetMaterial(0)==AuthoredMaterial);
    C->NormalizePickupVisual();
    TestTrue(TEXT("Normalization is idempotent"),F.Pickup->Mesh->GetRelativeScale3D().Equals(NormalizedScale,.0001));
    const FTransform ActorBefore=F.Pickup->GetActorTransform();
    const FTransform MeshBefore=F.Pickup->Mesh->GetRelativeTransform();
    const auto CollisionBefore=F.Pickup->Mesh->GetCollisionEnabled();
    const auto ResponsesBefore=F.Pickup->Mesh->GetCollisionResponseToChannels();
    const FName IdentityBefore=F.Pickup->PersistentId;
    TestTrue(TEXT("Sugar uses imported art, not Cube"),F.Pickup->Mesh->GetStaticMesh()&&F.Pickup->Mesh->GetStaticMesh()->GetName()==TEXT("SM_LooseSugar_Regular_R03"));
    TestTrue(TEXT("Width normalized to 30 cm"),FMath::IsNearlyEqual(C->MeasuredWidthCm,30.f,.01f));
    TStrongObjectPtr<UDWInventoryTransactionTestProbe> Probe(NewObject<UDWInventoryTransactionTestProbe>());
    bool Reentry=false,Coherent=false;int32 Notices=0;
    Probe->TestHook=[&](){++Notices;Coherent=F.Pickup->Quantity==4&&F.Bag()->CountItem(TEXT("SugarChunk"))==5;
        Reentry|=F.Pickup->TryCollect(F.Player);};
    F.Bag()->OnChanged.AddDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Partial inventory transaction commits"),F.Pickup->TryCollect(F.Player));
    F.Bag()->OnChanged.RemoveDynamic(Probe.Get(),&UDWInventoryTransactionTestProbe::OnInventoryChanged);
    TestTrue(TEXT("Inventory callback sees coherent quantity"),Coherent);TestFalse(TEXT("Callback cannot double collect"),Reentry);
    TestEqual(TEXT("One inventory change"),Notices,1);TestEqual(TEXT("Exactly one committed visual"),C->CollectionPlayCount,1);
    TestEqual(TEXT("Remainder stays four"),F.Pickup->Quantity,4);TestTrue(TEXT("Visual active after successful commit"),C->bCollectionActive);
    TestFalse(TEXT("Actor stays visible until animation ends"),F.Pickup->IsHidden());
    TestFalse(TEXT("Remainder cannot collect during its effect"),F.Pickup->IsInteractionAvailable());
    TestFalse(TEXT("Visual cannot collide"),F.Pickup->GetActorEnableCollision());
    TestFalse(TEXT("Repeated input during effect rejected"),F.Pickup->TryCollect(F.Player));
    C->TickComponent(.275f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Actual flash rises during elastic stretch"),C->CurrentFlash>.9f);
    TestTrue(TEXT("Source transform is not stretched"),F.Pickup->Mesh->GetRelativeTransform().Equals(MeshBefore,.001));
    TestTrue(TEXT("Pickup flash never replaces source material"),F.Pickup->Mesh->GetMaterial(0)==AuthoredMaterial);
    C->TickComponent(.5f,LEVELTICK_All,nullptr);
    TestFalse(TEXT("Effect completes"),C->bCollectionActive);TestTrue(TEXT("Remainder becomes available"),F.Pickup->IsInteractionAvailable());
    TestTrue(TEXT("Remainder actor collision restored"),F.Pickup->GetActorEnableCollision());
    TestTrue(TEXT("Original mesh collision enabled mode restored"),F.Pickup->Mesh->GetCollisionEnabled()==CollisionBefore);
    TestTrue(TEXT("Original collision channel response preserved"),F.Pickup->Mesh->GetCollisionResponseToChannels()==ResponsesBefore);
    TestFalse(TEXT("Original mesh visible again"),F.Pickup->Mesh->bHiddenInGame);
    TestFalse(TEXT("Full bag rejects another pickup"),F.Pickup->TryCollect(F.Player));
    TestEqual(TEXT("Full bag cannot trigger a visual"),C->CollectionPlayCount,1);TestEqual(TEXT("Failure preserves remainder"),F.Pickup->Quantity,4);
    TestTrue(TEXT("Make isolated test capacity"),F.Bag()->SetSlots({}));
    TestTrue(TEXT("Remaining four successfully collected"),F.Pickup->TryCollect(F.Player));
    TestEqual(TEXT("No remaining stock"),F.Pickup->Quantity,0);TestEqual(TEXT("Exactly four credited"),F.Bag()->CountItem(TEXT("SugarChunk")),4);
    TestFalse(TEXT("Empty logical actor retains visual until finish"),F.Pickup->IsHidden());
    C->TickComponent(1.f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Fully consumed actor hidden after effect"),F.Pickup->IsHidden());TestFalse(TEXT("Fully consumed actor has no collision"),F.Pickup->GetActorEnableCollision());
    TestFalse(TEXT("Actor identity retained instead of Destroy"),F.Pickup->IsActorBeingDestroyed());
    TestTrue(TEXT("Actor transform retained"),F.Pickup->GetActorTransform().Equals(ActorBefore,.001));
    TestEqual(TEXT("Persistent identity retained"),F.Pickup->PersistentId,IdentityBefore);
    TestEqual(TEXT("Exactly two successful transaction visuals"),C->CollectionPlayCount,2);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWPickupUnregisterLifecycleTest,
    "DoughWorld.Gathering.Pickup.TemporaryUnregisterRestoresRemainder",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWPickupUnregisterLifecycleTest::RunTest(const FString&)
{
    using namespace DWPickupTests;FFixture F;if(!TestTrue(TEXT("Isolated gathering fixture"),F.Ready))return false;
    auto* C=F.Pickup->PickupPresentation.Get();
    const FTransform ActorBefore=F.Pickup->GetActorTransform();
    const FTransform MeshBefore=F.Pickup->Mesh->GetRelativeTransform();
    const auto CollisionBefore=F.Pickup->Mesh->GetCollisionEnabled();
    const auto ResponsesBefore=F.Pickup->Mesh->GetCollisionResponseToChannels();
    if(!TestTrue(TEXT("Partial pickup commits before temporary unregister"),F.Pickup->TryCollect(F.Player)))return false;
    TestEqual(TEXT("Partial remainder before unregister"),F.Pickup->Quantity,4);
    TestTrue(TEXT("Collection is active before unregister"),C->bCollectionActive);
    TestFalse(TEXT("Collection temporarily disabled owner collision"),F.Pickup->GetActorEnableCollision());
    TestFalse(TEXT("Collection temporarily disabled prompt"),F.Pickup->Prompt->bPromptEnabled);
    C->UnregisterComponent();
    TestFalse(TEXT("Component is unregistered"),C->IsRegistered());
    TestFalse(TEXT("Temporary cleanup clears active visual"),C->bCollectionActive);
    TestFalse(TEXT("Borrowed source hide restored"),F.Pickup->Mesh->bHiddenInGame);
    TestTrue(TEXT("Surviving remainder is interactive immediately"),F.Pickup->IsInteractionAvailable());
    TestTrue(TEXT("Surviving remainder collision restored immediately"),F.Pickup->GetActorEnableCollision());
    TestTrue(TEXT("Surviving remainder prompt restored immediately"),F.Pickup->Prompt->bPromptEnabled);
    TestTrue(TEXT("Mesh collision mode preserved"),F.Pickup->Mesh->GetCollisionEnabled()==CollisionBefore);
    TestTrue(TEXT("Collision channel configuration preserved"),F.Pickup->Mesh->GetCollisionResponseToChannels()==ResponsesBefore);
    TestTrue(TEXT("Original source transform restored"),F.Pickup->Mesh->GetRelativeTransform().Equals(MeshBefore,.001));
    TestTrue(TEXT("Actor transform preserved"),F.Pickup->GetActorTransform().Equals(ActorBefore,.001));
    TestEqual(TEXT("Cleanup cannot change inventory"),F.Bag()->CountItem(TEXT("SugarChunk")),5);
    TestEqual(TEXT("Cleanup cannot change world quantity"),F.Pickup->Quantity,4);
    C->RegisterComponent();C->TickComponent(.01f,LEVELTICK_All,nullptr);
    TestTrue(TEXT("Component re-registers"),C->IsRegistered());
    TestTrue(TEXT("Reregistered remainder still has prompt"),F.Pickup->Prompt->bPromptEnabled);
    TestTrue(TEXT("Reregistered remainder still has collision"),F.Pickup->GetActorEnableCollision());
    TestEqual(TEXT("Registration does not replay collected feedback"),C->CollectionPlayCount,1);
    // A real EndPlay follows a different path: cleanup must not republish interaction
    // on actors that are leaving the world or being destroyed.
    if(!TestTrue(TEXT("Prepare second isolated partial transaction"),F.Bag()->SetSlots({FDWItemStack(TEXT("SugarChunk"),4)})))return false;
    if(!TestTrue(TEXT("Partial pickup works after reregister"),F.Pickup->TryCollect(F.Player)))return false;
    TestEqual(TEXT("Second transaction leaves three"),F.Pickup->Quantity,3);
    F.Pickup->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
    TestFalse(TEXT("EndPlay clears feedback"),C->bCollectionActive);
    TestFalse(TEXT("EndPlay does not restore collision"),F.Pickup->GetActorEnableCollision());
    TestFalse(TEXT("EndPlay does not republish prompt"),F.Pickup->Prompt->bPromptEnabled);
    if(C->IsRegistered())C->UnregisterComponent();
    TestFalse(TEXT("Unregister after EndPlay still does not restore collision"),F.Pickup->GetActorEnableCollision());
    TestFalse(TEXT("Unregister after EndPlay still does not republish prompt"),F.Pickup->Prompt->bPromptEnabled);
    TestEqual(TEXT("EndPlay preserves already committed inventory"),F.Bag()->CountItem(TEXT("SugarChunk")),5);
    TestEqual(TEXT("EndPlay preserves already committed remainder"),F.Pickup->Quantity,3);
    return true;
}
#endif
