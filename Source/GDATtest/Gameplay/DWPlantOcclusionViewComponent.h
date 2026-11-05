#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWPlantOcclusionViewComponent.generated.h"
class UMaterialParameterCollection;

/** Publishes the existing plant occlusion protocol once per local player, not per plant. */
UCLASS(ClassGroup=(Rendering),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWPlantOcclusionViewComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWPlantOcclusionViewComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Plant Occlusion",meta=(DisplayName="启用植被遮挡透明")) bool bEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Plant Occlusion",meta=(DisplayName="透明效果强度",ClampMin="0",ClampMax="1")) float Strength=1.f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Plant Occlusion") TSoftObjectPtr<UMaterialParameterCollection> Parameters;
 UFUNCTION(BlueprintCallable,Category="Plant Occlusion") void SetPlantOcclusionEnabled(bool bEnable);
 virtual void Deactivate() override;
protected:
 virtual void BeginPlay() override;
 virtual void TickComponent(float DeltaTime,ELevelTick TickType,FActorComponentTickFunction* ThisTickFunction) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void OnUnregister() override;
private:
 UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> LoadedParameters;
 FVector LastPosition=FVector::ZeroVector;
 float LastStrength=-1.f;
 void ClearOwnedParameters();
};
