#pragma once
#include "CoreMinimal.h"
#include "Engine/TriggerBox.h"
#include "DWOpeningExitVolume.generated.h"

class APawn;
class APlayerController;

/** Authored spatial boundary only. No dialogue, stage, save or travel ownership. */
UCLASS(Blueprintable,meta=(DisplayName="DW Opening Exit Volume"))
class GDATTEST_API ADWOpeningExitVolume : public ATriggerBox
{
 GENERATED_BODY()
public:
 ADWOpeningExitVolume();
 UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Opening|Exit",meta=(DisplayName="启用出口体积 / Exit Volume Enabled",ToolTip="关闭后导演不触发出口；不会回退到旧半径。")) bool bExitVolumeEnabled=true;
 bool ContainsExitPlayer(const APawn* Candidate,const APlayerController* ExpectedController,FString& Reason) const;
 bool IsAvailable(FString& Reason) const;
 // Pure geometry API permits code tests without constructing any gameplay world.
 static bool IsValidBoxGeometry(const FTransform& BoxTransform,const FVector& UnscaledExtent);
};
