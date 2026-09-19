#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraOccluderFadeComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

USTRUCT()
struct FCameraOccluderMeshState
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Mesh = nullptr;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Originals;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Instances;
    UPROPERTY(Transient) TArray<float> OriginalFadeValues;
    float Strength = 0.f;
    double LastOccludedTime = -1000000.0;
};

// Candidate implementation for a SINGLE local player. Not yet compiled in the user's project.
// Add to an occluding actor. Tag only its canopy/roof/wall StaticMeshComponents "CameraFade".
// No export macro is needed when used within one module; add the module API macro for cross-module use.
UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class UCameraOccluderFadeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCameraOccluderFadeComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade")
    FName ComponentTag = TEXT("CameraFade");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade")
    FName ParameterName = TEXT("CameraFade");

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0.02", ClampMax="0.8"))
    float OccludedVisibility = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0.03"))
    float ScanInterval = 0.08f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0"))
    float ClearHoldSeconds = 0.2f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0.01"))
    float FadeOutSeconds = 0.18f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0.01"))
    float FadeInSeconds = 0.3f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0"))
    float BoundsPaddingCm = 10.f;

    // Call again after replacing tagged meshes or changing their materials at runtime.
    UFUNCTION(BlueprintCallable, Category="Camera Fade")
    void RefreshFadeMeshes();

    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    UPROPERTY(Transient) TArray<FCameraOccluderMeshState> States;
    double NextScanTime = 0.0;
    void RestoreOriginals();
    void ScanOcclusion(double Now);
};
