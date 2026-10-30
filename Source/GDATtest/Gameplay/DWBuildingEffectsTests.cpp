#if WITH_DEV_AUTOMATION_TESTS
#include "DWBuildingEffectsComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/WorldSettings.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/AutomationTest.h"

namespace DWBuildingEffectsTests
{
struct FWorldScope
{
    UWorld* World = nullptr;
    AActor* Owner = nullptr;
    UStaticMeshComponent* Source = nullptr;
    UDWBuildingEffectsComponent* Effects = nullptr;
    UMaterialInterface* BaseMaterial = nullptr;
    UMaterialInterface* OriginalOverlay = nullptr;
    FTransform OriginalActorTransform;
    FTransform OriginalMeshTransform;
    FName OriginalCollisionProfile;

    explicit FWorldScope(bool bAssetsAvailable = true, bool bMeshIsRoot = false)
    {
        if (!GEngine) return;
        World = UWorld::CreateWorld(EWorldType::Game, false,
            MakeUniqueObjectName(GetTransientPackage(), UWorld::StaticClass(), TEXT("DWBuildingEffectsTestWorld")), GetTransientPackage());
        if (!World) return;
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
        Owner = World->SpawnActor<AActor>();
        UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
        UMaterialInterface* ShapeMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
        if (!Owner || !Cube || !ShapeMaterial) return;
        Source = NewObject<UStaticMeshComponent>(Owner, TEXT("GameplayMesh"));
        Source->SetMobility(EComponentMobility::Movable);
        Source->SetStaticMesh(Cube);
        if (bMeshIsRoot) Owner->SetRootComponent(Source);
        else
        {
            USceneComponent* Root = NewObject<USceneComponent>(Owner, TEXT("GameplayRoot"));
            Root->SetMobility(EComponentMobility::Movable);
            Owner->SetRootComponent(Root);
            Root->RegisterComponent();
            Source->SetupAttachment(Root);
        }
        Source->SetCollisionProfileName(TEXT("BlockAllDynamic"));
        Source->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
        Source->SetGenerateOverlapEvents(true);
        Source->SetCanEverAffectNavigation(true);
        Source->SetRenderCustomDepth(true);
        Source->SetCustomDepthStencilValue(203);
        BaseMaterial = UMaterialInstanceDynamic::Create(ShapeMaterial, Owner);
        OriginalOverlay = UMaterialInstanceDynamic::Create(ShapeMaterial, Owner);
        Source->SetMaterial(0, BaseMaterial);
        Source->SetOverlayMaterial(OriginalOverlay);
        Source->RegisterComponent();
        Owner->SetActorTransform(FTransform(FRotator(0, 27, 0), FVector(320, -170, 130), FVector(1.2, .9, 1.1)));
        if (!bMeshIsRoot) Source->SetRelativeTransform(FTransform(FRotator::ZeroRotator, FVector(11, 8, 36), FVector(.8, 1.3, .7)));
        OriginalActorTransform = Owner->GetActorTransform();
        OriginalMeshTransform = Source->GetComponentTransform();
        OriginalCollisionProfile = Source->GetCollisionProfileName();
        Effects = NewObject<UDWBuildingEffectsComponent>(Owner, TEXT("EffectsUnderTest"));
        // Inject engine assets so these behavioral tests do not depend on project art/cooked shaders.
        Effects->OutlineMaterial = bAssetsAvailable ? TSoftObjectPtr<UMaterialInterface>(ShapeMaterial) : TSoftObjectPtr<UMaterialInterface>();
        Effects->FlashMaterial = bAssetsAvailable ? TSoftObjectPtr<UMaterialInterface>(ShapeMaterial) : TSoftObjectPtr<UMaterialInterface>();
        Effects->CloudMaterial = bAssetsAvailable ? TSoftObjectPtr<UMaterialInterface>(ShapeMaterial) : TSoftObjectPtr<UMaterialInterface>();
        Effects->CloudMesh = bAssetsAvailable ? TSoftObjectPtr<UStaticMesh>(Cube) : TSoftObjectPtr<UStaticMesh>();
        Owner->AddInstanceComponent(Effects);
        Effects->RegisterComponent();
        if (!Owner->HasActorBegunPlay()) Owner->DispatchBeginPlay();
    }
    ~FWorldScope()
    {
        if (!World) return;
        if (World->GetWorldSettings()) World->GetWorldSettings()->SetPauserPlayerState(nullptr);
        for (TActorIterator<AActor> It(World); It; ++It)
            if (It->HasActorBegunPlay()) It->RouteEndPlay(EEndPlayReason::EndPlayInEditor);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    }
    bool Ready() const { return World && Owner && Source && BaseMaterial && Effects && Effects->HasBegunPlay(); }
    void Advance(float Seconds) { Effects->TickComponent(Seconds, LEVELTICK_All, nullptr); }
    TArray<UStaticMeshComponent*> Proxies() const
    {
        TArray<UStaticMeshComponent*> Result;
        if (!Owner) return Result;
        TInlineComponentArray<UStaticMeshComponent*> Meshes;
        Owner->GetComponents(Meshes);
        for (auto* Mesh : Meshes)
            if (IsValid(Mesh) && Mesh->IsRegistered() && Mesh->ComponentHasTag(TEXT("DWBuildingEffectsProxy"))) Result.Add(Mesh);
        return Result;
    }
    void CheckGameplaySurface(FAutomationTestBase& Test, UMaterialInterface* ExpectedMaterial = nullptr) const
    {
        Test.TestTrue(TEXT("Actor transform never participates in bounce"), Owner->GetActorTransform().Equals(OriginalActorTransform, .0001));
        Test.TestTrue(TEXT("Source transform remains exact during and after bounce"), Source->GetComponentTransform().Equals(OriginalMeshTransform, .0001));
        Test.TestEqual(TEXT("Collision profile survives"), Source->GetCollisionProfileName(), OriginalCollisionProfile);
        Test.TestTrue(TEXT("Query and physics collision survive"), Source->GetCollisionEnabled() == ECollisionEnabled::QueryAndPhysics);
        Test.TestTrue(TEXT("Pawn remains blocked"), Source->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block);
        Test.TestTrue(TEXT("Visibility trace remains blocked"), Source->GetCollisionResponseToChannel(ECC_Visibility) == ECR_Block);
        Test.TestTrue(TEXT("Source overlap setting survives"), Source->GetGenerateOverlapEvents());
        Test.TestTrue(TEXT("Source navigation relevance survives"), Source->CanEverAffectNavigation());
        Test.TestTrue(TEXT("Source visible flag stays true for CameraOccluderFade"), Source->GetVisibleFlag());
        Test.TestTrue(TEXT("Source CustomDepth enable survives"), bool(Source->bRenderCustomDepth));
        Test.TestEqual(TEXT("Source stencil survives"), Source->CustomDepthStencilValue, 203);
        Test.TestEqual(TEXT("Source material is never replaced by effect"), Source->GetMaterial(0), ExpectedMaterial ? ExpectedMaterial : BaseMaterial);
        Test.TestEqual(TEXT("Existing source overlay survives"), Source->GetOverlayMaterial(), OriginalOverlay);
        for (auto* Proxy : Proxies())
        {
            Test.TestTrue(TEXT("Effect proxy has no collision"), Proxy->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
            Test.TestFalse(TEXT("Effect proxy cannot produce overlaps"), Proxy->GetGenerateOverlapEvents());
            Test.TestFalse(TEXT("Effect proxy cannot change navigation"), Proxy->CanEverAffectNavigation());
            Test.TestFalse(TEXT("Effect proxy does not write source stencil"), bool(Proxy->bRenderCustomDepth));
        }
    }
};

// Shared fixture entry for the separately registered cloud-controls regression.
// Keeping it here avoids duplicating isolated-world/player-independent setup.
bool RunCloudControls(FAutomationTestBase& Test)
{
    FWorldScope F;
    if (!Test.TestTrue(TEXT("Cloud-controls isolated fixture ready"), F.Ready())) return false;
    F.Effects->CloudPuffCount = 0;
    if (!Test.TestTrue(TEXT("Zero clouds does not reject construction bounce"), F.Effects->PlayConstructionEffect())) return false;
    F.Advance(.3f);
    Test.TestTrue(TEXT("Bounce remains active with clouds disabled"), F.Effects->bConstructionActive);
    Test.TestTrue(TEXT("Source is still borrowed by the visual bounce"), F.Effects->IsTemporarilyHiddenSource(F.Source));
    Test.TestEqual(TEXT("Zero configured clouds produces zero active puffs"), F.Effects->ActiveCloudCount, 0);
    bool bCloudComponentWithZero = false;
    for (auto* Proxy : F.Proxies()) if (Cast<UInstancedStaticMeshComponent>(Proxy)) bCloudComponentWithZero = true;
    Test.TestFalse(TEXT("Zero clouds skips the ISM entirely"), bCloudComponentWithZero);

    const auto SampleCloud = [&](int32 RequestedCount, int32 ExpectedCount, float Radius, FVector& OutScale) -> bool
    {
        F.Effects->ResetConstructionEffect(true);
        Test.TestFalse(TEXT("Reset between cloud samples restores source"), bool(F.Source->bHiddenInGame));
        F.Effects->CloudPuffCount = RequestedCount;
        F.Effects->CloudPuffRadius = Radius;
        if (!Test.TestTrue(TEXT("Configured cloud episode starts"), F.Effects->PlayConstructionEffect())) return false;
        F.Advance(.3f); // Both radius samples observe the same real component-animation age.
        UInstancedStaticMeshComponent* Cloud = nullptr;
        for (auto* Proxy : F.Proxies()) if (auto* ISM = Cast<UInstancedStaticMeshComponent>(Proxy))
        {
            if (Cloud) { Test.AddError(TEXT("More than one cloud ISM created for an episode")); return false; }
            Cloud = ISM;
        }
        if (!Test.TestNotNull(TEXT("A real cloud ISM exists"), Cloud)) return false;
        Test.TestEqual(TEXT("Actual ISM instance count follows requested/clamped count"), Cloud->GetInstanceCount(), ExpectedCount);
        Test.TestEqual(TEXT("Active cloud debug count agrees with real ISM"), F.Effects->ActiveCloudCount, ExpectedCount);
        Test.TestTrue(TEXT("Expanded cloud count remains non-colliding"), Cloud->GetCollisionEnabled() == ECollisionEnabled::NoCollision);
        Test.TestFalse(TEXT("Expanded cloud count does not affect navigation"), Cloud->CanEverAffectNavigation());
        FTransform Instance;
        if (!Test.TestTrue(TEXT("Read actual first instance in world space"), Cloud->GetInstanceTransform(0, Instance, true))) return false;
        OutScale = Instance.GetScale3D();
        return Test.TestTrue(TEXT("Sampled instance has nonzero physical size"), OutScale.GetMin() > KINDA_SMALL_NUMBER);
    };

    FVector Radius20, Radius40, ClampedScale;
    if (!SampleCloud(18, 18, 20.f, Radius20) || !SampleCloud(18, 18, 40.f, Radius40)) return false;
    Test.TestTrue(TEXT("Doubling radius doubles actual instance X size"), FMath::IsNearlyEqual(Radius40.X / Radius20.X, 2., .001));
    Test.TestTrue(TEXT("Doubling radius doubles actual instance Y size"), FMath::IsNearlyEqual(Radius40.Y / Radius20.Y, 2., .001));
    Test.TestTrue(TEXT("Doubling radius doubles actual instance Z size"), FMath::IsNearlyEqual(Radius40.Z / Radius20.Z, 2., .001));
    if (!SampleCloud(33, 32, 40.f, ClampedScale)) return false;
    F.Effects->ResetConstructionEffect(true);
    Test.TestFalse(TEXT("Cloud-control testing leaves source rendering restored"), bool(F.Source->bHiddenInGame));
    F.CheckGameplaySurface(Test);
    return true;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingEffectsLifecycle,
    "DoughWorld.BuildingEffects.Lifecycle.ExplicitCommitOnceAndReplay", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWBuildingEffectsLifecycle::RunTest(const FString&)
{
    DWBuildingEffectsTests::FWorldScope F;
    if (!TestTrue(TEXT("Isolated game world and initialized component"), F.Ready())) return false;
    F.Advance(.2f);
    TestFalse(TEXT("BeginPlay and ordinary ticking cannot play construction"), F.Effects->bConstructionActive);
    TestFalse(TEXT("Initialization does not consume first-build entitlement"), F.Effects->bConstructionPlayed);
    TestFalse(TEXT("Placed/restored-looking source remains visible"), bool(F.Source->bHiddenInGame));
    TestEqual(TEXT("No automatic playback"), F.Effects->ConstructionPlayCount, 0);
    TestTrue(TEXT("Explicit successful construction can play"), F.Effects->PlayConstructionEffect());
    TestTrue(TEXT("Only source rendering is temporarily hidden"), bool(F.Source->bHiddenInGame));
    TestTrue(TEXT("Only an active borrowed source may bypass camera-fade hidden filtering"), F.Effects->IsTemporarilyHiddenSource(F.Source));
    F.CheckGameplaySurface(*this);
    bool bAnimatedCopyFound = false;
    for (auto* Proxy : F.Proxies())
        if (Proxy->GetMaterial(0) == F.BaseMaterial && !Proxy->GetComponentTransform().Equals(F.OriginalMeshTransform, .01)) bAnimatedCopyFound = true;
    TestTrue(TEXT("A real visual copy moves while source stays fixed"), bAnimatedCopyFound);
    F.Advance(.23f);
    TestTrue(TEXT("Cloud instances actually exist during the burst"), F.Effects->ActiveCloudCount >= 8 && F.Effects->ActiveCloudCount <= 10);
    const float BeforeRepeat = F.Effects->ActiveElapsed;
    TestFalse(TEXT("Duplicate commit cannot restart playback"), F.Effects->PlayConstructionEffect());
    TestEqual(TEXT("Rejected duplicate preserves elapsed progress"), F.Effects->ActiveElapsed, BeforeRepeat);
    TestEqual(TEXT("Duplicate commit cannot increment playback count"), F.Effects->ConstructionPlayCount, 1);
    F.Advance(2.f);
    TestFalse(TEXT("Natural completion restores source rendering"), bool(F.Source->bHiddenInGame));
    TestFalse(TEXT("Natural completion clears active state"), F.Effects->bConstructionActive);
    TestFalse(TEXT("Camera-fade exception expires at completion"), F.Effects->IsTemporarilyHiddenSource(F.Source));
    TestEqual(TEXT("Natural completion removes all cloud instances"), F.Effects->ActiveCloudCount, 0);
    F.CheckGameplaySurface(*this);
    TestFalse(TEXT("Completed instance still rejects another build event"), F.Effects->PlayConstructionEffect());
    TestTrue(TEXT("Explicit preview may deliberately replay"), F.Effects->PreviewConstructionEffect());
    TestEqual(TEXT("Preview is a second actual episode"), F.Effects->ConstructionPlayCount, 2);
    F.Effects->ResetConstructionEffect(false);
    TestFalse(TEXT("Reset without permission cannot rearm gameplay event"), F.Effects->PlayConstructionEffect());
    TestFalse(TEXT("Reset restores source immediately"), bool(F.Source->bHiddenInGame));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingEffectsCleanup,
    "DoughWorld.BuildingEffects.Lifecycle.MaterialSyncAndDeactivateRestore", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWBuildingEffectsCleanup::RunTest(const FString&)
{
    // Also exercise a generic StaticMesh-root actor, not just a Station with a scene root.
    DWBuildingEffectsTests::FWorldScope F(true, true);
    if (!TestTrue(TEXT("Mesh-root fixture ready"), F.Ready())) return false;
    if (!TestTrue(TEXT("Construction starts"), F.Effects->PlayConstructionEffect())) return false;
    for (auto* Proxy : F.Proxies()) TestTrue(TEXT("Proxy is never attached to the hidden source"), Proxy->GetAttachParent() != F.Source);
    UMaterialInterface* Shape = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
    UMaterialInterface* NewFadeMID = UMaterialInstanceDynamic::Create(Shape, F.Owner);
    F.Source->SetMaterial(0, NewFadeMID); // Simulate camera fade replacing its MID after initialization.
    F.Advance(.17f);
    bool bLatestSurfaceShared = false;
    for (auto* Proxy : F.Proxies()) if (Proxy->GetMaterial(0) == NewFadeMID)
    {
        bLatestSurfaceShared = true;
        TestEqual(TEXT("Bounce copy keeps existing overlay"), Proxy->GetOverlayMaterial(), F.OriginalOverlay);
    }
    TestTrue(TEXT("Bounce shares the replacement source MID"), bLatestSurfaceShared);
    F.CheckGameplaySurface(*this, NewFadeMID);
    F.Effects->Deactivate();
    TestFalse(TEXT("Deactivate cannot strand original source hidden"), bool(F.Source->bHiddenInGame));
    TestFalse(TEXT("Deactivate stops the episode"), F.Effects->bConstructionActive);
    TestEqual(TEXT("Deactivate destroys transient outline, bounce, flash and cloud proxies"), F.Proxies().Num(), 0);
    F.CheckGameplaySurface(*this, NewFadeMID);
    F.Effects->Activate();
    F.Advance(.1f);
    TestFalse(TEXT("Reactivation cannot implicitly replay construction"), F.Effects->bConstructionActive);
    TestFalse(TEXT("Reactivation cannot rearm once-only event"), F.Effects->PlayConstructionEffect());
    F.Effects->SetHighlightAmount(.5f);
    F.Owner->SetActorHiddenInGame(true);
    F.Advance(0.f);
    for (auto* Proxy : F.Proxies()) TestFalse(TEXT("Hidden owner suppresses effect proxy visibility"), Proxy->IsVisible());
    F.Owner->SetActorHiddenInGame(false);
    TestTrue(TEXT("Explicit preview can restart before unregister"), F.Effects->PreviewConstructionEffect());
    F.Effects->UnregisterComponent();
    TestFalse(TEXT("Unregister restores borrowed source rendering"), bool(F.Source->bHiddenInGame));
    TestEqual(TEXT("Unregister destroys every transient proxy"), F.Proxies().Num(), 0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingEffectsMissingAssets,
    "DoughWorld.BuildingEffects.Safety.UnassignedAssetsAndHiddenSource", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWBuildingEffectsMissingAssets::RunTest(const FString&)
{
    DWBuildingEffectsTests::FWorldScope F(false);
    if (!TestTrue(TEXT("Fixture with unassigned optional assets ready"), F.Ready())) return false;
    TestTrue(TEXT("Missing optional art is reported"), F.Effects->MissingAssetCount > 0);
    TestTrue(TEXT("Missing outline/flash/cloud art still permits mesh bounce"), F.Effects->PlayConstructionEffect());
    F.Advance(.23f);
    TestEqual(TEXT("No phantom cloud burst with missing art"), F.Effects->ActiveCloudCount, 0);
    F.CheckGameplaySurface(*this);
    F.Advance(2.f);
    TestFalse(TEXT("Missing art cannot strand source rendering"), bool(F.Source->bHiddenInGame));
    F.Effects->ResetConstructionEffect(true);
    F.Source->SetHiddenInGame(true, false);
    TestFalse(TEXT("Already hidden source must not be revealed by construction"), F.Effects->PlayConstructionEffect());
    F.Effects->Deactivate();
    TestTrue(TEXT("Cleanup preserves an independently hidden source"), bool(F.Source->bHiddenInGame));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWBuildingEffectsPause,
    "DoughWorld.BuildingEffects.Timing.PauseAndManualIntermediateHighlight", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWBuildingEffectsPause::RunTest(const FString&)
{
    DWBuildingEffectsTests::FWorldScope F;
    if (!TestTrue(TEXT("Pause fixture ready"), F.Ready())) return false;
    F.Effects->SetHighlightAmount(.37f);
    TestFalse(TEXT("Manual preview opts out of prompt following"), F.Effects->bAutoFollowPrompt);
    F.Advance(0.f);
    TestTrue(TEXT("Intermediate alpha is not coerced to on/off"), FMath::IsNearlyEqual(F.Effects->HighlightAlpha, .37f));
    if (!TestTrue(TEXT("Construction starts before pausing"), F.Effects->PlayConstructionEffect())) return false;
    F.Advance(.1f);
    const float BeforePause = F.Effects->ActiveElapsed;
    APlayerState* Pauser = F.World->SpawnActor<APlayerState>();
    if (!TestNotNull(TEXT("Pause owner"), Pauser)) return false;
    F.World->GetWorldSettings()->SetPauserPlayerState(Pauser);
    if (!TestTrue(TEXT("World is actually paused"), F.World->IsPaused())) return false;
    F.Advance(10.f);
    TestEqual(TEXT("Paused component tick cannot advance construction"), F.Effects->ActiveElapsed, BeforePause);
    TestEqual(TEXT("Paused outline hides immediately"), F.Effects->HighlightAlpha, 0.f);
    TestTrue(TEXT("Pause retains unfinished episode"), F.Effects->bConstructionActive);
    F.CheckGameplaySurface(*this);
    F.World->GetWorldSettings()->SetPauserPlayerState(nullptr);
    F.Advance(0.f);
    TestTrue(TEXT("Unpause restores manual intermediate alpha"), FMath::IsNearlyEqual(F.Effects->HighlightAlpha, .37f));
    TestEqual(TEXT("Zero elapsed unpause cannot consume animation time"), F.Effects->ActiveElapsed, BeforePause);
    F.Advance(.1f);
    TestTrue(TEXT("Construction resumes after unpause"), F.Effects->ActiveElapsed > BeforePause);
    F.Effects->Deactivate();
    TestFalse(TEXT("Paused/resumed cleanup restores source"), bool(F.Source->bHiddenInGame));
    return true;
}
#endif
