#pragma once
#include "CoreMinimal.h"
#include "DWMapImage.h"
#include "DWWorldMapData.h"
#include "UObject/Object.h"
#include "Styling/SlateBrush.h"
#include "DWMapExploration.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;
struct FStreamableHandle;

namespace DWMapExploration
{
    constexpr int32 MaxLayers=4,MaxGrid=256,DefaultGrid=128,MaxTotalBytes=2*1024*1024;
    GDATTEST_API bool Validate(const TArray<FDWMapExplorationLayer>& Layers,FString& Error);
    GDATTEST_API bool SameFrame(const FDWMapExplorationLayer& Layer,const FDWMapImageSettings& Image,int32 Grid);
    GDATTEST_API int32 FindFrame(const TArray<FDWMapExplorationLayer>& Layers,const FDWMapImageSettings& Image,int32 Grid);
    GDATTEST_API FDWMapExplorationLayer MakeFrame(const FDWMapImageSettings& Image,int32 Grid);
    GDATTEST_API int32 FindOrAddFrame(TArray<FDWMapExplorationLayer>& Layers,const FDWMapImageSettings& Image,int32 Grid);
    GDATTEST_API bool Reveal(FDWMapExplorationLayer& Layer,FVector2D World,double RadiusCm,FIntRect& Dirty);
    GDATTEST_API bool IsRevealed(const FDWMapExplorationLayer& Layer,FVector2D World);
    GDATTEST_API uint8 Cell(const FDWMapExplorationLayer& Layer,int32 X,int32 Y);
}

/** Game-thread UI resource owner; no capture, readback, file IO, World scan or ticker. */
UCLASS()
class GDATTEST_API UDWMapExplorationResource : public UObject
{
    GENERATED_BODY()
public:
    void Prepare();
    void WarmMask(const FDWMapImageSettings& Image,int32 Grid);
    bool Sync(const FDWMapExplorationLayer& Layer,UTexture2D* BaseImage,const FIntRect& Dirty,const TArray<uint8>& Water=TArray<uint8>(),uint64 WaterRevision=0,FLinearColor WaterColor=FLinearColor::Transparent);
    UMaterialInterface* GetMaterial() const;
    const FSlateBrush* GetBrush() const;
    UMaterialInterface* GetPreparedTint() const {return TintMaterial.Get();}
    UTexture2D* GetPreparedCloseIcon() const {return CloseIcon.Get();}
private:
    UPROPERTY(Transient) TObjectPtr<UTexture2D> Mask;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> PondMask;
    uint64 PondRevision=MAX_uint64,PendingPondRevision=MAX_uint64;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> PendingPondMask;
    TArray<uint8> PondUploadCoverage;
    int32 PondUploadRow=0;
    UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Material;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> BoundImage;
    FDWMapExplorationLayer Frame;
    UPROPERTY(Transient) TObjectPtr<UMaterialInterface> TintMaterial;
    UPROPERTY(Transient) TObjectPtr<UTexture2D> CloseIcon;
    UPROPERTY(Transient) TSoftObjectPtr<UMaterialInterface> ParentAsset;
    UPROPERTY(Transient) TSoftObjectPtr<UMaterialInterface> TintAsset;
    UPROPERTY(Transient) TSoftObjectPtr<UTexture2D> CloseAsset;
    TSharedPtr<FStreamableHandle> AssetHandle;
    FSlateBrush Brush;
    TWeakObjectPtr<UTexture2D> BoundMask;
    bool bRequestedAssets=false,bResolvedAssets=false;
};
