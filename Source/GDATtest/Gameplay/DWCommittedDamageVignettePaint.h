#pragma once
#include "CoreMinimal.h"
#include "Rendering/DrawElements.h"

// Four procedural strips. Corners overlap, but the composed alpha is bounded
// by MaxCornerOpacity. The central rectangle is never painted.
namespace DWCommittedDamageVignette
{
inline void Paint(FSlateWindowElementList& Out,int32 Layer,const FGeometry& Geometry,
    float Intensity,FLinearColor Color,float MaxCornerOpacity,float EdgeFraction)
{
    if(!FMath::IsFinite(Intensity)||Intensity<=0.f)return;
    const FVector2f Size(Geometry.GetLocalSize());
    if(!FMath::IsFinite(Size.X)||!FMath::IsFinite(Size.Y)||Size.X<=0.f||Size.Y<=0.f)return;
    const float Cap=FMath::IsFinite(MaxCornerOpacity)?FMath::Clamp(MaxCornerOpacity,0.f,.4f):.28f;
    const float Edge=FMath::IsFinite(EdgeFraction)?FMath::Clamp(EdgeFraction,.04f,.24f):.14f;
    const float Composed=Cap*FMath::Clamp(Intensity,0.f,1.f);
    const float Alpha=1.f-FMath::Sqrt(FMath::Max(0.f,1.f-Composed));
    Color.R=FMath::IsFinite(Color.R)?FMath::Clamp(Color.R,0.f,1.f):.65f;
    Color.G=FMath::IsFinite(Color.G)?FMath::Clamp(Color.G,0.f,1.f):.06f;
    Color.B=FMath::IsFinite(Color.B)?FMath::Clamp(Color.B,0.f,1.f):.03f;
    Color.A=Alpha;FLinearColor Transparent=Color;Transparent.A=0.f;
    const float W=Size.X*Edge,H=Size.Y*Edge;
    const auto Strip=[&](FVector2f Pos,FVector2f Extent,EOrientation Orientation,bool FadeIn)
    {
        TArray<FSlateGradientStop> Stops;
        Stops.Emplace(FVector2f::ZeroVector,FadeIn?Transparent:Color);
        Stops.Emplace(Extent,FadeIn?Color:Transparent);
        FSlateDrawElement::MakeGradient(Out,Layer,
            Geometry.ToPaintGeometry(Extent,FSlateLayoutTransform(Pos)),MoveTemp(Stops),Orientation);
    };
    // Slate orientation names describe stop direction: Vertical varies in X.
    Strip(FVector2f(0,0),FVector2f(W,Size.Y),Orient_Vertical,false);
    Strip(FVector2f(Size.X-W,0),FVector2f(W,Size.Y),Orient_Vertical,true);
    Strip(FVector2f(0,0),FVector2f(Size.X,H),Orient_Horizontal,false);
    Strip(FVector2f(0,Size.Y-H),FVector2f(Size.X,H),Orient_Horizontal,true);
}
}
