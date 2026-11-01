#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DesertBuildingCutoutComponent.generated.h"
class UPrimitiveComponent;
class UMaterialInterface;
/** Native diagnostics and ownership for designer-only circular cutout eligibility. */
UCLASS(ClassGroup=(Rendering),meta=(BlueprintSpawnableComponent))
class DESERTBUILDINGLAB_API UDesertBuildingCutoutComponent : public UActorComponent
{
    GENERATED_BODY()
public:
    UDesertBuildingCutoutComponent();
    UFUNCTION(BlueprintPure,Category="Building|Cutout|Diagnostics")
    static TArray<float> ReadRuntimePrimitiveData(const UPrimitiveComponent* Mesh);
    UFUNCTION(BlueprintCallable,Category="Building|Cutout|Diagnostics")
    static TArray<FString> FinishMaterialCompilationAndReadErrors(UMaterialInterface* Material);
    UFUNCTION(BlueprintPure,Category="Building|Cutout|Diagnostics")
    static TArray<FGuid> ReadCompiledDependencyStateIds(UMaterialInterface* Material,bool Functions);
    UFUNCTION(BlueprintPure,Category="Building|Cutout|Diagnostics")
    static FGuid ReadDependencyAssetStateId(const UObject* Asset);
    UFUNCTION(BlueprintCallable,Category="Building|Cutout") void RefreshEligibility();
    UFUNCTION(BlueprintCallable,Category="Building|Cutout") void ReleaseEligibility();
    UFUNCTION(BlueprintCallable,Category="Building|Cutout") void SetCutoutEnabled(bool Enabled);
    UPROPERTY(EditAnywhere,BlueprintReadOnly,Category="Building|Cutout") bool bCutoutEnabled=true;
    UFUNCTION(BlueprintPure,Category="Building|Cutout|Diagnostics") int32 GetOwnedMeshCount() const {return PreviousValues.Num();}
    static constexpr int32 EligibilityIndex=16;
protected:
    virtual void BeginPlay() override;
    virtual void EndPlay(const EEndPlayReason::Type Reason) override;
    virtual void OnUnregister() override;
    virtual void Deactivate() override;
    virtual void Activate(bool bReset=false) override;
private:
    TMap<TWeakObjectPtr<UPrimitiveComponent>,float> PreviousValues;
};
