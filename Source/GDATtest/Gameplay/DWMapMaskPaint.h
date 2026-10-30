#pragma once
#include "CoreMinimal.h"

class UMaterialInterface;
class FSlateWindowElementList;
struct FGeometry;

/** Paint only the committed material/frame pair; world projection belongs to map backend. */
namespace DWMapMaskPaint
{
    bool Paint(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        UMaterialInterface* Material,FVector2D MinimapTopLeft,FVector2D MinimapSize,
        FVector2D MaterialFrameTopLeft,FVector2D MaterialFrameSize,float EasedProgress,
        bool bSourceMaterialContainsRim,float Opacity=1.f);
}
