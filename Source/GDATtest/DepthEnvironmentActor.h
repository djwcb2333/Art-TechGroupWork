#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DepthEnvironmentActor.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UCameraOccluderFadeComponent;

/** A local canopy, mushroom cap, roof, or upper wall. Place trunks/doors/floors separately. */
UCLASS(Blueprintable)
class GDATTEST_API ADepthEnvironmentActor : public AActor
{
    GENERATED_BODY()
public:
    ADepthEnvironmentActor();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Environment") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Environment") TObjectPtr<UStaticMeshComponent> EnvironmentMesh;
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Environment") TObjectPtr<UCameraOccluderFadeComponent> CameraFadeComponent;

    /** PIE-only fixture factory. Never writes editor levels or packages; null outside PIE. */
    UFUNCTION(BlueprintCallable, Category="Camera Fade|Diagnostics", meta=(WorldContext="WorldContextObject"))
    static ADepthEnvironmentActor* SpawnTransientValidationActor(UObject* WorldContextObject, const FTransform& Transform);
};
