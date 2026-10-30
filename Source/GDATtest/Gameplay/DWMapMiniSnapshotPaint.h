#pragma once
#include "CoreMinimal.h"
#include "DWMinimap.h"
#include "DWMapMotionAdapter.h"
class FSlateWindowElementList;
struct FGeometry;
class UMaterialInterface;
namespace DWMapMiniSnapshotPaint
{
    int32 Paint(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        UMaterialInterface* VertexTint,const DWMapMotionAdapter::FFrame& Frame,const TArray<FDWMinimapDisplayMarker>& Markers,
        const FDWMinimapNavigationTarget& Target,FVector Player,FVector Forward,FVector North,float Diameter,float Alpha);
}
