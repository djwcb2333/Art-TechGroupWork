#include "DWMapMarkerPaint.h"
#include "DWMapVisualMath.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Materials/MaterialInterface.h"

void DWMapMarkerPaint::Diamond(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
    UMaterialInterface* VertexTint,FVector2D Center,float Radius,bool bSelected,double PulseAge,float PulseAmplitude,float Frequency,float Damping,float Opacity)
{
    if(!IsValid(VertexTint)||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().GetRenderer()||Center.ContainsNaN()||
       !FMath::IsFinite(Center.X)||!FMath::IsFinite(Center.Y)||!FMath::IsFinite(Radius))return;
    const double Scale=PulseAge>=0.?DWMapVisualMath::Bounce(PulseAge,PulseAmplitude,Frequency,Damping):1.;
    const float R=FMath::Clamp(Radius,4.f,30.f)*float(Scale)*(bSelected?1.10f:1.f);
    const FVector2f P(Center);const TArray<FVector2f> Points={P+FVector2f(0,-R),P+FVector2f(R,0),P+FVector2f(0,R),P+FVector2f(-R,0)};
    const FSlateRenderTransform Transform=Geometry.GetAccumulatedRenderTransform();
    const FLinearColor Blue=FLinearColor::FromSRGBColor(bSelected?FColor(95,166,219):FColor(70,139,195));
    FLinearColor Fill=Blue;Fill.A=FMath::Clamp(Opacity,0.f,1.f);const FColor Tint=Fill.ToFColor(true);TArray<FSlateVertex> Vertices;TArray<SlateIndex> Indices;
    auto Vertex=[&](FVector2f Point){FSlateVertex V=FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,Point,FVector4f(.5f,.5f,1.f,1.f),FVector2f(.5f,.5f),Tint);V.PixelSize[0]=1;V.PixelSize[1]=1;return V;};
    Vertices.Add(Vertex(P));
    for(const auto Point:Points)Vertices.Add(Vertex(Point));
    for(int32 I=0;I<4;++I){Indices.Add(0);Indices.Add(SlateIndex(I+1));Indices.Add(SlateIndex((I+1)%4+1));}
    FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.SetResourceObject(VertexTint);
    FSlateDrawElement::MakeCustomVerts(Elements,Layer,FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush),Vertices,Indices,nullptr,0,0);
    TArray<FVector2f> Edge=Points;Edge.Add(Points[0]);
    FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Edge,ESlateDrawEffect::None,FLinearColor(1,1,1,Fill.A),true,bSelected?2.5f:2.f);
}
