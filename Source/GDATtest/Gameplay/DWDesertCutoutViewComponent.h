#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DWDesertCutoutViewComponent.generated.h"
class UMaterialParameterCollection;
/** One local view publishes data only. The designer plugin never depends on this gameplay module. */
UCLASS(ClassGroup=(Rendering),meta=(BlueprintSpawnableComponent))
class GDATTEST_API UDWDesertCutoutViewComponent : public UActorComponent
{
 GENERATED_BODY()
public:
 UDWDesertCutoutViewComponent();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Designer Cutout") bool bCutoutEnabled=true;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Designer Cutout",meta=(ClampMin="30",ClampMax="2000",Units="cm")) float RadiusCm=180.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Designer Cutout",meta=(ClampMin="0",Units="cm")) float FeatherCm=25.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Designer Cutout",meta=(ClampMin="0",Units="cm")) float MinimumHeightAboveFeetCm=30.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Designer Cutout",meta=(ClampMin="0",Units="cm")) float DepthInsetCm=10.f;
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Designer Cutout",meta=(ClampMin="0.1",ClampMax="64")) float EdgePower=27.385183334f;
 UPROPERTY(EditDefaultsOnly,BlueprintReadOnly,Category="Designer Cutout") TSoftObjectPtr<UMaterialParameterCollection> Parameters;
protected:
 virtual void BeginPlay() override;
 virtual void TickComponent(float DeltaTime,ELevelTick Type,FActorComponentTickFunction* Tick) override;
 virtual void EndPlay(const EEndPlayReason::Type Reason) override;
 virtual void OnUnregister() override;
 virtual void Deactivate() override;
private:
 UPROPERTY(Transient) TObjectPtr<UMaterialParameterCollection> LoadedParameters;
 void ClearOwnedView();
};
