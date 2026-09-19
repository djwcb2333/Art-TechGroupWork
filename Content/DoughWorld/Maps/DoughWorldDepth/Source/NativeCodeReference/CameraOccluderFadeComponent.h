#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "CameraOccluderFadeComponent.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;

/** A real actor supplies the position every scan; the offset is in that actor's local space. */
USTRUCT(BlueprintType)
struct GDATTEST_API FCameraFadeProtectedTarget
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade")
    TObjectPtr<AActor> Actor = nullptr;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade")
    FVector LocalOffset = FVector(0.f, 0.f, 70.f);

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0", Units="cm"))
    float RadiusCm = 30.f;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0", Units="cm"))
    float HalfHeightCm = 40.f;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FCameraFadeSlotStatus
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") TObjectPtr<UStaticMeshComponent> Mesh = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") int32 SlotIndex = INDEX_NONE;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") TObjectPtr<UMaterialInterface> OriginalMaterial = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") TObjectPtr<UMaterialInstanceDynamic> DynamicMaterial = nullptr;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") float OriginalFade = 1.f;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") float CurrentFade = 1.f;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") float FadeStrength = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") bool bOwnsCurrentMaterial = false;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") bool bOccludedLastScan = false;
};

USTRUCT(BlueprintType)
struct GDATTEST_API FCameraFadeViewSnapshot
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") bool bValid = false;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") FVector Location = FVector::ZeroVector;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") FRotator Rotation = FRotator::ZeroRotator;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") float FOV = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") float OrthoWidth = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") float AspectRatio = 0.f;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") bool bOrthographic = false;
    UPROPERTY(BlueprintReadOnly, Category="Camera Fade") float DistanceToPawn = 0.f;
};

USTRUCT()
struct FCameraOccluderMeshState
{
    GENERATED_BODY()
    UPROPERTY(Transient) TObjectPtr<UStaticMeshComponent> Mesh = nullptr;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInterface>> Originals;
    UPROPERTY(Transient) TArray<TObjectPtr<UMaterialInstanceDynamic>> Instances;
    UPROPERTY(Transient) TArray<float> OriginalFadeValues;
    FName BoundParameterName;
    float Strength = 0.f;
    double LastOccludedTime = -1.e9;
    bool bOccludedLastScan = false;
};

/** Single local view. Changes only tagged component materials, never camera or collision. */
UCLASS(ClassGroup=(Camera), meta=(BlueprintSpawnableComponent))
class GDATTEST_API UCameraOccluderFadeComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UCameraOccluderFadeComponent();

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade") FName ComponentTag = TEXT("CameraFade");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade") FName ParameterName = TEXT("CameraFade");
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0")) int32 LocalPlayerIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade") bool bProtectPlayer = true;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0", ClampMax="1")) float OccludedVisibility = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0.03", Units="s")) float ScanInterval = 0.08f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0", Units="s")) float ClearHoldSeconds = 0.2f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0.01", Units="s")) float FadeOutSeconds = 0.18f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0.01", Units="s")) float FadeInSeconds = 0.3f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0", Units="cm")) float BoundsPaddingCm = 10.f;

    /** Stops a segment just before its target, so a support surface at the target is less likely to fade. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0", Units="cm")) float TargetEndInsetCm = 8.f;

    /** Zero means unlimited. With no player, additional targets can still be protected by the current view. */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade", meta=(ClampMin="0", Units="cm")) float AdditionalTargetMaxDistanceCm = 650.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Camera Fade") TArray<FCameraFadeProtectedTarget> AdditionalTargets;

    /** Restore owned overrides, then rebind current tagged meshes/materials. Call after runtime replacement. */
    UFUNCTION(BlueprintCallable, Category="Camera Fade") void RefreshFadeMeshes();
    UFUNCTION(BlueprintCallable, Category="Camera Fade") void AddProtectedTarget(AActor* Target, FVector LocalOffset, float RadiusCm, float HalfHeightCm);
    UFUNCTION(BlueprintCallable, Category="Camera Fade") void RemoveProtectedTarget(AActor* Target);
    UFUNCTION(BlueprintCallable, Category="Camera Fade") void ClearProtectedTargets();

    /** Optional interaction hook. Call only when the selected interaction target changes, not every tick.
     * Replaces the additional-target list on all fade components in this world. Null clears it.
     * This deliberately does not invent an interaction-selection system.
     */
    UFUNCTION(BlueprintCallable, Category="Camera Fade", meta=(WorldContext="WorldContextObject"))
    static int32 SetInteractionTargetForWorld(UObject* WorldContextObject, AActor* Target, FVector LocalOffset, float RadiusCm, float HalfHeightCm);

    /** Deterministic scan entry point for Blueprint/Python PIE checks; normal tick performs the blend. */
    UFUNCTION(BlueprintCallable, Category="Camera Fade|Diagnostics") int32 EvaluateOcclusionNow();
    UFUNCTION(BlueprintPure, Category="Camera Fade|Diagnostics") TArray<FCameraFadeSlotStatus> GetFadeStatus() const;
    UFUNCTION(BlueprintPure, Category="Camera Fade|Diagnostics") FCameraFadeViewSnapshot GetViewSnapshot() const;
    UFUNCTION(BlueprintPure, Category="Camera Fade|Diagnostics") int32 GetManagedMeshCount() const { return States.Num(); }
    UFUNCTION(BlueprintPure, Category="Camera Fade|Diagnostics") int32 GetSkippedSlotCount() const { return SkippedSlotCount; }

    virtual void Activate(bool bReset = false) override;
    virtual void Deactivate() override;
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
    virtual void OnUnregister() override;
private:
    UPROPERTY(Transient) TArray<FCameraOccluderMeshState> States;
    int32 SkippedSlotCount = 0;
    double NextScanTime = 0.0;
    void RestoreOriginals();
    int32 ScanOcclusion(double Now);
};
