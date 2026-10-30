#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DWResourcePlacementSettings.generated.h"

class ADWGatheringInteractable;
class UDWResourcePlacementRecipe;

/** Transient editor draft. Nothing here is a gameplay save or a new map actor. */
UCLASS(Transient)
class UDWResourcePlacementSettings : public UObject
{
    GENERATED_BODY()
public:
    UPROPERTY(EditAnywhere,Category="资源",meta=(DisplayName="资源蓝图类"))
    TSoftClassPtr<ADWGatheringInteractable> ResourceClass;
    UPROPERTY(EditAnywhere,Category="资源",meta=(DisplayName="资源外观与采集配方"))
    TSoftObjectPtr<UDWResourcePlacementRecipe> Recipe;
    UPROPERTY(EditAnywhere,Category="性能",meta=(DisplayName="新资源启用提示预算"))
    bool bEnablePromptBudget=true;
    UPROPERTY(EditAnywhere,Category="性能",meta=(DisplayName="新拾取物启用描边预算"))
    bool bEnablePickupOutlineBudget=true;
    UPROPERTY(EditAnywhere,Category="性能",meta=(DisplayName="应用配方时同时调整所选旧对象预算"))
    bool bApplyBudgetToExisting=false;
    UPROPERTY(EditAnywhere,Category="性能",meta=(DisplayName="隐藏检查间隔（秒）",ClampMin="0",ClampMax="0.5"))
    float HiddenPromptPollSeconds=.1f;

    UPROPERTY(EditAnywhere,Category="区域",meta=(DisplayName="区域中心（世界厘米）"))
    FVector Center=FVector::ZeroVector;
    UPROPERTY(EditAnywhere,Category="区域",meta=(DisplayName="区域半尺寸（世界厘米）"))
    FVector Extent=FVector(1000.,1000.,2000.);
    UPROPERTY(EditAnywhere,Category="区域",meta=(DisplayName="数量",ClampMin="1",ClampMax="500"))
    int32 Count=30;
    UPROPERTY(EditAnywhere,Category="区域",meta=(DisplayName="最小水平中心间距（厘米）",ClampMin="0",ClampMax="100000"))
    double MinimumDistanceCm=250.;
    UPROPERTY(EditAnywhere,Category="区域",meta=(DisplayName="随机种子"))
    int32 Seed=20261029;
    UPROPERTY(EditAnywhere,Category="地面",meta=(DisplayName="最大坡度（角度）",ClampMin="0",ClampMax="89"))
    double MaxSlopeDegrees=30.;
    UPROPERTY(EditAnywhere,Category="地面",meta=(DisplayName="竖直偏移（厘米）",ClampMin="-10000",ClampMax="10000"))
    double GroundOffsetCm=0.;
    UPROPERTY(EditAnywhere,Category="外观",meta=(DisplayName="随机偏航最小角度"))
    double YawMin=0.;
    UPROPERTY(EditAnywhere,Category="外观",meta=(DisplayName="随机偏航最大角度"))
    double YawMax=360.;
    UPROPERTY(EditAnywhere,Category="外观",meta=(DisplayName="等比缩放最小值",ClampMin="0.05",ClampMax="10"))
    double ScaleMin=1.;
    UPROPERTY(EditAnywhere,Category="外观",meta=(DisplayName="等比缩放最大值",ClampMin="0.05",ClampMax="10"))
    double ScaleMax=1.;
};
