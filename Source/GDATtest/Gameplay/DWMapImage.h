#pragma once
#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Engine/Texture2D.h"
#include "DWMapMotionMath.h"
#include "DWMapImage.generated.h"

struct FStreamableHandle;
/** One image and its world calibration, shared by minimap and full map. Units are centimeters. */
USTRUCT(BlueprintType)
struct GDATTEST_API FDWMapImageSettings
{
    GENERATED_BODY()
    FDWMapImageSettings();
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Map Image") TSoftObjectPtr<UTexture2D> Image;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Map Image",meta=(Units="cm")) FVector2D WorldCenterCm=FVector2D::ZeroVector;
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Map Image",meta=(ClampMin="1",Units="cm")) FVector2D WorldSizeCm=FVector2D(100000.,100000.);
    /** Direction of the image's TOP in world space: 0 = +X, 90 = +Y. Independent of camera yaw. */
    UPROPERTY(EditAnywhere,BlueprintReadWrite,Category="Map Image",meta=(Units="deg")) float ImageUpYaw=0.f;
    bool IsValidCalibration() const;
    bool SameAs(const FDWMapImageSettings& Other) const;
    bool WorldToUV(FVector2D World,FVector2D& UV) const;
    bool UVToWorld(FVector2D UV,FVector2D& World) const;
    double WidthAtViewYaw(double ViewYaw) const;
};

/** Small shared async texture owner. No world, capture, render target, readback or callback. */
UCLASS()
class GDATTEST_API UDWMapImageResource : public UObject
{
    GENERATED_BODY()
public:
    void Request(const FDWMapImageSettings& Settings);
    void Cancel();
    bool Poll(bool& Ready,FString& Error);
    UTexture2D* GetReadyTexture() const;
    uint64 GetGeneration() const {return Generation;}
    const FDWMapImageSettings& GetSettings() const {return Current;}
    virtual void BeginDestroy() override;
private:
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Texture;
    FDWMapImageSettings Current;
    TSharedPtr<FStreamableHandle> Handle;
    uint64 Generation=0;
    bool bRequested=false;
};

namespace DWMapImageMath
{
    /** Inverse of WorldToScreen for the horizontal image view. No camera or world queries. */
    GDATTEST_API bool ScreenToWorld(const DWMapMotionMath::View& View,FVector2D Pixel,FVector2D& World);
}
