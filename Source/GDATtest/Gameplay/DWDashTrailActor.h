#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DWDashTrailActor.generated.h"
class UStaticMeshComponent;
UCLASS(Blueprintable)
class GDATTEST_API ADWDashTrailActor : public AActor
{
    GENERATED_BODY()
public:
    ADWDashTrailActor();
    virtual void Tick(float DeltaSeconds) override;
    UPROPERTY(VisibleAnywhere,BlueprintReadOnly,Category="Dash Effect") TObjectPtr<UStaticMeshComponent> TrailMesh;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Dash Effect",meta=(ClampMin="0.01")) float Lifetime=0.28f;
protected:virtual void BeginPlay()override;
private:float Elapsed=0.f;FVector InitialScale;
};
