#if WITH_DEV_AUTOMATION_TESTS

#include "DWMinimapNavigation.h"
#include "DWMinimap.h"
#include "Math/RotationMatrix.h"

#include "Components/SceneComponent.h"
#include "DWEnemyCharacter.h"
#include "DWEnemyNest.h"
#include "DWResourceNode.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"

namespace DWMinimapTests
{
    /** No project map, disk save, editor world, inventory or existing Actor is modified. */
    struct FTestWorld
    {
        UWorld* World = nullptr;

        FTestWorld()
        {
            if (!GEngine) return;
            const FName Name = MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DWMinimapTestWorld"));
            World = UWorld::CreateWorld(EWorldType::Game, false, Name, GetTransientPackage());
            if (!World) return;
            GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
            World->InitializeActorsForPlay(FURL());
        }

        ~FTestWorld()
        {
            if (!World) return;
            for (TActorIterator<AActor> It(World); It; ++It)
                if (It->HasActorBegunPlay()) It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
            World->DestroyWorld(false);
            GEngine->DestroyWorldContext(World);
        }

        template <class T> T* Spawn()
        {
            if (!World) return nullptr;
            FActorSpawnParameters Params;
            Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            return World->SpawnActor<T>(T::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
        }

        AActor* SpawnPoint(const FVector& Position)
        {
            AActor* Point = Spawn<AActor>();
            if (!Point) return nullptr;
            USceneComponent* Root = NewObject<USceneComponent>(Point, TEXT("TestRoot"));
            Point->SetRootComponent(Root);
            Point->AddInstanceComponent(Root);
            Root->RegisterComponent();
            Point->SetActorLocation(Position);
            return Point;
        }

        UDWMinimapMarkerComponent* AddMarker(AActor* Owner)
        {
            if (!Owner) return nullptr;
            UDWMinimapMarkerComponent* Marker = NewObject<UDWMinimapMarkerComponent>(Owner);
            Owner->AddInstanceComponent(Marker);
            Marker->RegisterComponent();
            return Marker;
        }
    };
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMinimapWorldIsolationTest, "DoughWorld.Minimap.Navigation.WorldIsolationAndSetClear",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWMinimapWorldIsolationTest::RunTest(const FString& Parameters)
{
    DWMinimapTests::FTestWorld First;
    DWMinimapTests::FTestWorld Second;
    if (!TestNotNull(TEXT("First isolated World"), First.World) || !TestNotNull(TEXT("Second isolated World"), Second.World)) return false;
    UDWMinimapNavigationSubsystem* A = First.World->GetSubsystem<UDWMinimapNavigationSubsystem>();
    UDWMinimapNavigationSubsystem* B = Second.World->GetSubsystem<UDWMinimapNavigationSubsystem>();
    if (!TestNotNull(TEXT("First World navigation"), A) || !TestNotNull(TEXT("Second World navigation"), B)) return false;
    TestTrue(TEXT("Worlds own distinct navigation instances"), A != B);
    TestTrue(TEXT("Typed context helper returns the same World's navigation"),
        UDWMinimapNavigationSubsystem::GetNavigationSubsystem(First.World) == A);
    TestNull(TEXT("Missing World context safely returns null"),
        UDWMinimapNavigationSubsystem::GetNavigationSubsystem(nullptr));
    TestFalse(TEXT("New World starts clear"), A->GetNavigationTarget().bValid);
    const FVector Position(10000.f, 20000.f, 650.f);
    TestTrue(TEXT("Finite static waypoint accepted"), A->SetWaypoint(Position, FText::FromString(TEXT("Test destination"))));
    const FDWMinimapNavigationTarget Target = A->GetNavigationTarget();
    TestTrue(TEXT("Selected waypoint valid"), Target.bValid);
    TestFalse(TEXT("Static waypoint has no actor source"), Target.bIsActorTarget);
    TestTrue(TEXT("Static waypoint retains Z for projection"), Target.WorldPosition.Equals(Position));
    TestTrue(TEXT("Static display and navigation positions agree"), Target.NavigationPosition.Equals(Position));
    TestEqual(TEXT("Waypoint label retained"), Target.Label.ToString(), FString(TEXT("Test destination")));
    TestNull(TEXT("Static waypoint has no selected marker"), A->GetTargetMarker());
    TestFalse(TEXT("Another World stays clear"), B->GetNavigationTarget().bValid);
    TestTrue(TEXT("Same World lookup retains state across consumers/HUD rebuild"), First.World->GetSubsystem<UDWMinimapNavigationSubsystem>()->GetNavigationTarget().bValid);
    AActor* OtherPoint = Second.SpawnPoint(FVector(500.f, 600.f, 700.f));
    UDWMinimapMarkerComponent* OtherMarker = Second.AddMarker(OtherPoint);
    if (!TestNotNull(TEXT("Other World actor marker"), OtherMarker)) return false;
    TestFalse(TEXT("Cross-World actor target rejected"), A->SetActorTarget(OtherMarker));
    TestTrue(TEXT("Rejected replacement preserves current target"), A->GetNavigationTarget().WorldPosition.Equals(Position));
    A->ClearWaypoint();
    TestFalse(TEXT("Explicit clear removes target"), A->GetNavigationTarget().bValid);
    A->ClearWaypoint();
    TestFalse(TEXT("Repeated clear is safe"), A->GetNavigationTarget().bValid);
    TestTrue(TEXT("World can select another point before EndPlay"), A->SetWaypoint(Position, FText::GetEmpty()));
    A->OnWorldEndPlay(*First.World);
    TestFalse(TEXT("World EndPlay clears old-map navigation"), A->GetNavigationTarget().bValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMinimapActorNavigationTest, "DoughWorld.Minimap.Navigation.ActorTrackingAndInvalidation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWMinimapActorNavigationTest::RunTest(const FString& Parameters)
{
    DWMinimapTests::FTestWorld Scope;
    if (!TestNotNull(TEXT("Isolated World created"), Scope.World)) return false;
    UDWMinimapNavigationSubsystem* Nav = Scope.World->GetSubsystem<UDWMinimapNavigationSubsystem>();
    AActor* Point = Scope.SpawnPoint(FVector(100.f, 200.f, 300.f));
    UDWMinimapMarkerComponent* Marker = Scope.AddMarker(Point);
    if (!TestNotNull(TEXT("Navigation created"), Nav) || !TestNotNull(TEXT("Actor marker created"), Marker)) return false;
    Marker->Label = FText::FromString(TEXT("Water well"));
    Marker->WorldOffset = FVector(10.f, 20.f, 30.f);
    TestTrue(TEXT("Registered marker valid"), Marker->IsMarkerValid());
    TestTrue(TEXT("Actor marker selected"), Nav->SetActorTarget(Marker));
    TestTrue(TEXT("Actor target source indicated"), Nav->GetNavigationTarget().bIsActorTarget);
    TestTrue(TEXT("World offset affects annotation only"), Nav->GetNavigationTarget().WorldPosition.Equals(FVector(110.f, 220.f, 330.f)));
    TestTrue(TEXT("Annotation never moves actor"), Point->GetActorLocation().Equals(FVector(100.f, 200.f, 300.f)));
    TestTrue(TEXT("Physical navigation destination excludes display offset"),
        Nav->GetNavigationTarget().NavigationPosition.Equals(Point->GetActorLocation()));
    TestTrue(TEXT("Display XY offset never changes physical navigation distance"),
        FMath::IsNearlyEqual(DWMinimapMath::DistanceXYMeters(FVector::ZeroVector, Nav->GetNavigationTarget().NavigationPosition),
            DWMinimapMath::DistanceXYMeters(FVector::ZeroVector, Point->GetActorLocation())));
    TestFalse(TEXT("Test fixture has a genuinely different visual distance"),
        FMath::IsNearlyEqual(DWMinimapMath::DistanceXYMeters(FVector::ZeroVector, Nav->GetNavigationTarget().WorldPosition),
            DWMinimapMath::DistanceXYMeters(FVector::ZeroVector, Point->GetActorLocation())));
    TestTrue(TEXT("Selected marker available for duplicate suppression"), Nav->GetTargetMarker() == Marker);
    USceneComponent* Anchor = NewObject<USceneComponent>(Point, TEXT("DisplayAnchor"));
    Point->AddInstanceComponent(Anchor);
    Anchor->SetupAttachment(Point->GetRootComponent());
    Anchor->RegisterComponent();
    Anchor->SetRelativeLocation(FVector(40.f, 50.f, 60.f));
    Marker->DisplayAnchor.OverrideComponent = Anchor;
    TestTrue(TEXT("Explicit same-Actor anchor supplies map display position"),
        Nav->GetNavigationTarget().WorldPosition.Equals(FVector(150.f, 270.f, 390.f)));
    TestTrue(TEXT("Different display anchor leaves physical navigation destination unchanged"),
        Nav->GetNavigationTarget().NavigationPosition.Equals(Point->GetActorLocation()));
    AActor* OtherPoint = Scope.SpawnPoint(FVector(100000.f, 200000.f, 300000.f));
    if (!TestNotNull(TEXT("Different actor for anchor guard"), OtherPoint)) return false;
    Marker->DisplayAnchor.OverrideComponent = OtherPoint->GetRootComponent();
    TestTrue(TEXT("Foreign-Actor anchor falls back to own Actor"),
        Nav->GetNavigationTarget().WorldPosition.Equals(FVector(110.f, 220.f, 330.f)));
    Marker->DisplayAnchor = FComponentReference();
    Point->SetActorLocation(FVector(1000.f, -2000.f, 450.f));
    TestTrue(TEXT("Moving actor resolves its latest position"), Nav->GetNavigationTarget().WorldPosition.Equals(FVector(1010.f, -1980.f, 480.f)));
    TestTrue(TEXT("Physical destination follows moving actor without the visual offset"),
        Nav->GetNavigationTarget().NavigationPosition.Equals(FVector(1000.f, -2000.f, 450.f)));
    Marker->Label = FText::FromString(TEXT("Moved well"));
    TestEqual(TEXT("Editable label remains live"), Nav->GetNavigationTarget().Label.ToString(), FString(TEXT("Moved well")));
    Marker->bEnabled = false;
    Nav->Tick(0.1f);
    TestFalse(TEXT("Disabled marker is automatically cleared without a HUD"), Nav->GetNavigationTarget().bValid);
    Marker->bEnabled = true;
    TestTrue(TEXT("Re-enabled marker can be selected again"), Nav->SetActorTarget(Marker));
    Marker->DestroyComponent();
    Nav->Tick(0.1f);
    TestFalse(TEXT("Destroyed component clears selection"), Nav->GetNavigationTarget().bValid);
    Marker = Scope.AddMarker(Point);
    if (!TestNotNull(TEXT("Replacement marker created"), Marker)) return false;
    TestTrue(TEXT("Replacement marker selected"), Nav->SetActorTarget(Marker));
    Point->Destroy();
    Nav->Tick(0.1f);
    TestFalse(TEXT("Destroyed Actor clears navigation automatically"), Nav->GetNavigationTarget().bValid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMinimapMarkerGameplayStateTest, "DoughWorld.Minimap.Navigation.GameplayAvailabilityIsReadOnly",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWMinimapMarkerGameplayStateTest::RunTest(const FString& Parameters)
{
    DWMinimapTests::FTestWorld Scope;
    if (!TestNotNull(TEXT("Isolated World created"), Scope.World)) return false;
    ADWEnemyCharacter* Enemy = Scope.Spawn<ADWEnemyCharacter>();
    ADWEnemyNest* Nest = Scope.Spawn<ADWEnemyNest>();
    ADWResourceNode* Resource = Scope.Spawn<ADWResourceNode>();
    UDWMinimapMarkerComponent* EnemyMarker = Scope.AddMarker(Enemy);
    UDWMinimapMarkerComponent* NestMarker = Scope.AddMarker(Nest);
    UDWMinimapMarkerComponent* ResourceMarker = Scope.AddMarker(Resource);
    if (!TestNotNull(TEXT("Enemy marker"), EnemyMarker) || !TestNotNull(TEXT("Nest marker"), NestMarker)
        || !TestNotNull(TEXT("Resource marker"), ResourceMarker)) return false;
    Enemy->Health = 100.f;
    Nest->Health = 500.f;
    Resource->RemainingAmount = 3;
    TestTrue(TEXT("Living enemy displayed"), EnemyMarker->IsMarkerValid());
    TestTrue(TEXT("Living nest displayed"), NestMarker->IsMarkerValid());
    TestTrue(TEXT("Available resource displayed"), ResourceMarker->IsMarkerValid());
    TestEqual(TEXT("Marker query consumes no stock"), Resource->RemainingAmount, 3);
    TestEqual(TEXT("Marker query deals no enemy damage"), Enemy->Health, 100.f);
    Enemy->Health = 0.f;
    Nest->Health = 0.f;
    Resource->RemainingAmount = 0;
    TestFalse(TEXT("Dead enemy hidden"), EnemyMarker->IsMarkerValid());
    TestFalse(TEXT("Dead nest hidden"), NestMarker->IsMarkerValid());
    TestFalse(TEXT("Depleted resource hidden"), ResourceMarker->IsMarkerValid());
    Resource->bInfinite = true;
    TestTrue(TEXT("Infinite resource remains available"), ResourceMarker->IsMarkerValid());
    ResourceMarker->bRespectOwnerGameplayState = false;
    Resource->bInfinite = false;
    TestTrue(TEXT("Explicit static landmark override supported"), ResourceMarker->IsMarkerValid());
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMinimapCompassMathTest, "DoughWorld.Minimap.Navigation.CompassAndCentimeterUnits",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDWMinimapCompassMathTest::RunTest(const FString& Parameters)
{
    const FVector Origin(40000.f, -60000.f, 900.f);
    TestEqual(TEXT("10000cm on X is 100m"), DWMinimapMath::DistanceXYMeters(Origin, Origin + FVector(10000.f, 0.f, 0.f)), 100.f);
    TestEqual(TEXT("3-4 triangle uses horizontal meters, ignores elevation"), DWMinimapMath::DistanceXYMeters(Origin, Origin + FVector(3000.f, 4000.f, 200000.f)), 50.f);
    TestEqual(TEXT("Same XY at another height has zero navigation distance"), DWMinimapMath::DistanceXYMeters(Origin, Origin + FVector(0.f, 0.f, 10000.f)), 0.f);
    const FVector Directions[] = {
        FVector(1.f, 0.f, 0.f), FVector(1.f, 1.f, 0.f), FVector(0.f, 1.f, 0.f), FVector(-1.f, 1.f, 0.f),
        FVector(-1.f, 0.f, 0.f), FVector(-1.f, -1.f, 0.f), FVector(0.f, -1.f, 0.f), FVector(1.f, -1.f, 0.f)};
    for (int32 Index = 0; Index < UE_ARRAY_COUNT(Directions); ++Index)
    {
        const float ExpectedBearing = Index * 45.f;
        const FVector Destination = Origin + Directions[Index] * 10000.f;
        TestTrue(FString::Printf(TEXT("NorthYaw0 compass octant %d"), Index),
            FMath::IsNearlyEqual(DWMinimapMath::BearingDegrees(Origin, Destination), ExpectedBearing, 0.001f));
        const FVector RotatedDelta = FRotator(0.f, 90.f, 0.f).RotateVector(Directions[Index] * 10000.f);
        TestTrue(FString::Printf(TEXT("NorthYaw90 compass octant %d"), Index),
            FMath::IsNearlyEqual(DWMinimapMath::BearingDegrees(Origin, Origin + RotatedDelta, 90.f), ExpectedBearing, 0.001f));
        const FVector2D Map = DWMinimapMath::WorldToNorthUp(Origin, Destination);
        TestTrue(FString::Printf(TEXT("NorthUp coordinates octant %d"), Index), Map.Equals(FVector2D(Directions[Index].Y, Directions[Index].X) * 10000.f, 0.001));
    }
    TestEqual(TEXT("Coincident target has deterministic bearing"), DWMinimapMath::BearingDegrees(Origin, Origin), 0.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMinimapCameraProjectionTest,"DoughWorld.Minimap.Camera.SharedRotatingProjection",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FDWMinimapCameraProjectionTest::RunTest(const FString& Parameters)
{
    for(float Yaw:{0.f,40.f,90.f,180.f,-90.f,359.f})
    {
        FDWMinimapProjection P;const FRotationMatrix Basis(FRotator(-70.f,Yaw,0.f));
        P.Right=Basis.GetUnitAxis(EAxis::Y);P.Up=Basis.GetUnitAxis(EAxis::Z);P.NorthScale=FMath::Sin(FMath::DegreesToRadians(70.f));
        const FVector Forward=FRotator(0.f,Yaw,0.f).Vector();
        TestTrue(TEXT("Camera forward is map up"),P.Direction(Forward).Equals(FVector2D(0,-1),.001));
        const FVector D=FVector(300,700,0);
        TestTrue(TEXT("Marker and offscreen heading use the same horizontal projection"),P.Project(D).GetSafeNormal().Equals(P.Direction(D),.001));
        const float R=FMath::DegreesToRadians(Yaw);
        TestTrue(TEXT("World north orbits opposite camera yaw"),P.Direction(FVector::XAxisVector).Equals(FVector2D(-FMath::Sin(R),-FMath::Cos(R)),.001));
    }
    return true;
}
#endif
