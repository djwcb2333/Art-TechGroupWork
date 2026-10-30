#pragma once
#include "CoreMinimal.h"
class FSlateWindowElementList;
struct FGeometry;
class UMaterialInterface;

namespace DWMapMarkerPaint
{
    /** Diamonds use viewport-local Slate coordinates; pass a negative age for no pulse. */
    void Diamond(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        UMaterialInterface* VertexTint,FVector2D Center,float Radius,bool bSelected,double PulseAge,float PulseAmplitude,float Frequency,float Damping,float Opacity=1.f);
}
