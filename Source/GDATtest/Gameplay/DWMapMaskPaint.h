#pragma once
#include "CoreMinimal.h"
#include "DWMapImage.h"

class UMaterialInterface;
class FSlateWindowElementList;
struct FGeometry;

/** Paint only the committed material/frame pair; world projection belongs to map backend. */
namespace DWMapMaskPaint
{
    bool PaintImage(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        UTexture2D* Texture,const FDWMapImageSettings& Settings,const DWMapMotionMath::View& View,
        FVector2D MinimapTopLeft,FVector2D MinimapSize,float Progress,float Opacity=1.f);
    bool Paint(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        UMaterialInterface* Material,FVector2D MinimapTopLeft,FVector2D MinimapSize,
        FVector2D MaterialFrameTopLeft,FVector2D MaterialFrameSize,float EasedProgress,
        bool bSourceMaterialContainsRim,float Opacity=1.f,bool bCircleOnly=false);
}
