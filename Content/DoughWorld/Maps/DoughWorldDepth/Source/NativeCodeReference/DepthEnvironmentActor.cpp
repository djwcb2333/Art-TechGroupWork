#include "DepthEnvironmentActor.h"
#include "CameraOccluderFadeComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

ADepthEnvironmentActor::ADepthEnvironmentActor()
{
    PrimaryActorTick.bCanEverTick = false;
    SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
    // A static mesh cannot attach to a movable parent when components re-register.
    // These are fixed environment pieces; configure both mobilities before attachment.
    SceneRoot->SetMobility(EComponentMobility::Static);
    SetRootComponent(SceneRoot);
    EnvironmentMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("EnvironmentMesh"));
    EnvironmentMesh->SetMobility(EComponentMobility::Static);
    EnvironmentMesh->SetSimulatePhysics(false);
    EnvironmentMesh->SetupAttachment(SceneRoot);
    EnvironmentMesh->ComponentTags.Add(TEXT("CameraFade"));
    // Collision is deliberately left to each placed asset's setup. Fading never changes it.
    CameraFadeComponent = CreateDefaultSubobject<UCameraOccluderFadeComponent>(TEXT("CameraFadeComponent"));
}

ADepthEnvironmentActor* ADepthEnvironmentActor::SpawnTransientValidationActor(UObject* WorldContextObject, const FTransform& Transform)
{
    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World || World->WorldType != EWorldType::PIE) return nullptr;
    FActorSpawnParameters Parameters;
    Parameters.ObjectFlags |= RF_Transient;
    Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
    ADepthEnvironmentActor* Actor = World->SpawnActor<ADepthEnvironmentActor>(StaticClass(), Transform, Parameters);
    if (Actor)
    {
        // Only transient PIE fixtures move during validation. Change the child first, because
        // a movable child under a static parent is valid; the reverse combination is not.
        Actor->EnvironmentMesh->SetSimulatePhysics(false);
        Actor->EnvironmentMesh->SetMobility(EComponentMobility::Movable);
        Actor->SceneRoot->SetMobility(EComponentMobility::Movable);
    }
    return Actor;
}
