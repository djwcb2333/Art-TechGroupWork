#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DepthInteractionFocusActor.generated.h"

class USceneComponent;

/** Spatial focus proxy for nearby real resource/building anchors; not interaction gameplay. */
UCLASS(Blueprintable)
class GDATTEST_API ADepthInteractionFocusActor : public AActor
{
    GENERATED_BODY()
public:
    ADepthInteractionFocusActor();
    virtual void Tick(float DeltaSeconds) override;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Interaction Visibility") TObjectPtr<USceneComponent> SceneRoot;
    UPROPERTY(EditInstanceOnly, BlueprintReadWrite, Category="Interaction Visibility") TArray<TObjectPtr<AActor>> ProtectedInteractionAnchors;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Visibility", meta=(ClampMin="1", Units="cm")) float FocusRadiusCm = 500.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Visibility", meta=(ClampMin="0")) int32 LocalPlayerIndex = 0;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Visibility") FVector TargetLocalOffset = FVector(0.f, 0.f, 80.f);
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Visibility", meta=(ClampMin="0", Units="cm")) float ProtectedRadiusCm = 60.f;
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Interaction Visibility", meta=(ClampMin="0", Units="cm")) float ProtectedHalfHeightCm = 90.f;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Interaction Visibility|State") TObjectPtr<AActor> CurrentFocus;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Interaction Visibility|State") int32 PublishCount = 0;
    UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category="Interaction Visibility|State") int32 LastUpdatedFadeComponentCount = 0;

    /** Re-evaluate registered anchors only; publishes globally only if the selected target changes. */
    UFUNCTION(BlueprintCallable, Category="Interaction Visibility") void RefreshNearestFocus();
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
private:
    bool bHasPublishedState = false;
    bool bPublishedNonNullTarget = false;
    void PublishIfChanged(AActor* Target);
};
