#if WITH_DEV_AUTOMATION_TESTS
#include "DWGatheringWorld.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include <limits>

namespace DWGatheringWorldTests
{
struct FWorldScope
{
    UWorld* World = nullptr;
    FWorldScope()
    {
        if (!GEngine) return;
        World = UWorld::CreateWorld(EWorldType::Game, false,
            MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DWGatheringGuardWorld")), GetTransientPackage());
        if (!World) return;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
    }
    ~FWorldScope()
    {
        if (!World) return;
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringWorldGuards,
    "DoughWorld.Gathering.World.HeightReachAndInactiveGuards", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWGatheringWorldGuards::RunTest(const FString&)
{
    DWGatheringWorldTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Isolated transient test world"), Scope.World)) return false;
    auto* Resource = Scope.World->SpawnActor<ADWGatheringResource>();
    auto* User = Scope.World->SpawnActor<AActor>();
    if (!Resource || !User) return false;
    auto* UserRoot = NewObject<USceneComponent>(User);
    User->SetRootComponent(UserRoot); UserRoot->RegisterComponent();
    Resource->SetActorLocation(FVector::ZeroVector);
    User->SetActorLocation(FVector(250., 0., 180.));
    TestTrue(TEXT("Exact XY and height boundaries included"), Resource->IsWithinReach(User));
    User->SetActorLocation(FVector(250.01, 0., 0.));
    TestFalse(TEXT("Outside interaction radius rejects"), Resource->IsWithinReach(User));
    User->SetActorLocation(FVector(0., 0., -4500.));
    TestFalse(TEXT("Matching XY in canyon below rejects"), Resource->IsWithinReach(User));
    User->SetActorLocation(FVector(200., 0., 0.));
    TestFalse(TEXT("Tool reach can reduce interaction radius"), Resource->IsWithinReach(User, 150.f));
    TestFalse(TEXT("Nonfinite reach rejects"), Resource->IsWithinReach(User, std::numeric_limits<float>::quiet_NaN()));
    TestFalse(TEXT("Non gathering world cannot interact"), Resource->IsInteractionAvailable());
    TestFalse(TEXT("Missing player cannot damage resource"), Resource->ApplyToolHit(nullptr, TEXT("Axe"), 25., 250.f));
    TestEqual(TEXT("Rejected hit preserves durability"), Resource->Durability, 100.);
    TestFalse(TEXT("No active profile cannot release drops"), Resource->ReleaseDrops());
    TestFalse(TEXT("Rejected release does not consume drop entitlement"), Resource->bDropsReleased);
    Resource->SetStaged(true);
    TestFalse(TEXT("Staged resources cannot interact"), Resource->IsInteractionAvailable());
    TestTrue(TEXT("Actor logical root is independent of pop visual"), Resource->GetRootComponent() != Resource->Mesh.Get());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWGatheringStationIdentity,
    "DoughWorld.Gathering.World.StationIdentityAndExhaustedObjects", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWGatheringStationIdentity::RunTest(const FString&)
{
    DWGatheringWorldTests::FWorldScope Scope;
    if (!TestNotNull(TEXT("Isolated transient test world"), Scope.World)) return false;
    auto* Station = Scope.World->SpawnActor<ADWGatheringStation>();
    auto* Pickup = Scope.World->SpawnActor<ADWGatheringPickup>();
    auto* Resource = Scope.World->SpawnActor<ADWGatheringResource>();
    if (!Station || !Pickup || !Resource) return false;
    TestTrue(TEXT("Furnace mapping accepts"), Station->ConfigureBuilding(TEXT("Furnace")));
    TestEqual(TEXT("Furnace station identity"), Station->StationId, FName(TEXT("GatherStation_Furnace")));
    TestTrue(TEXT("Vat mapping accepts"), Station->ConfigureBuilding(TEXT("FermentationVat")));
    TestEqual(TEXT("Vat station identity"), Station->StationId, FName(TEXT("GatherStation_FermentationVat")));
    TestFalse(TEXT("Unknown building rejected"), Station->ConfigureBuilding(TEXT("Unknown")));
    TestEqual(TEXT("Unknown building leaves identity unchanged"), Station->BuildingItemId, FName(TEXT("FermentationVat")));
    TestFalse(TEXT("Null picker leaves world stock unchanged"), Pickup->TryCollect(nullptr));
    TestEqual(TEXT("Null picker quantity preserved"), Pickup->Quantity, 1);
    Pickup->Quantity = 0; Pickup->RefreshPickup();
    TestFalse(TEXT("Empty pickup cannot interact"), Pickup->IsInteractionAvailable());
    TestFalse(TEXT("Empty pickup remains an actor"), Pickup->IsActorBeingDestroyed());
    Resource->AdoptResourceState(0., true, .2f); Resource->PublishResourceState();
    TestFalse(TEXT("Exhausted resource remains an actor"), Resource->IsActorBeingDestroyed());
    TestTrue(TEXT("Exhausted resource keeps released fact"), Resource->bDropsReleased);
    TestFalse(TEXT("Released resources cannot release twice"), Resource->ReleaseDrops());
    return true;
}
#endif
