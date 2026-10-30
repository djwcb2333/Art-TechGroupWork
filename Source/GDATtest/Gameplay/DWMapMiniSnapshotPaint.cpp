#include "DWMapMiniSnapshotPaint.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Materials/MaterialInterface.h"

namespace
{
void Shape(FSlateWindowElementList& E,int32 Layer,const FGeometry& G,UMaterialInterface* TintMaterial,const TArray<FVector2f>& P,FLinearColor Color,float Scale,float Alpha)
{
    if(!IsValid(TintMaterial)||P.Num()<3||Alpha<=0.f||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().GetRenderer())return;
    FVector2f Center=FVector2f::ZeroVector;for(auto V:P)Center+=V;Center/=float(P.Num());Color.A*=Alpha;
    const FColor Tint=Color.ToFColor(true);const auto Transform=G.GetAccumulatedRenderTransform();
    auto Vertex=[&](FVector2f Pos){auto V=FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,Pos,FVector4f(.5f,.5f,1,1),FVector2f(.5f,.5f),Tint);V.PixelSize[0]=1;V.PixelSize[1]=1;return V;};
    TArray<FSlateVertex> Vertices;TArray<SlateIndex> Indices;Vertices.Add(Vertex(Center));
    for(auto V:P)Vertices.Add(Vertex(V));
    for(int32 I=0;I<P.Num();++I){Indices.Add(0);Indices.Add(I+1);Indices.Add((I+1)%P.Num()+1);}
    ESlateDrawEffect Draw=ESlateDrawEffect::None;FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.SetResourceObject(TintMaterial);
    FSlateDrawElement::MakeCustomVerts(E,Layer,FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush),Vertices,Indices,nullptr,0,0);
    TArray<FVector2f> Edge=P;Edge.Add(P[0]);FSlateDrawElement::MakeLines(E,Layer+1,G.ToPaintGeometry(),Edge,Draw,FLinearColor(1,1,1,Alpha),true,1.35f*Scale);
}
void Marker(FSlateWindowElementList& E,int32 Layer,const FGeometry& G,UMaterialInterface* TintMaterial,FVector2f Center,float Radius,EDWMinimapMarkerShape Type,FLinearColor Color,float Scale,float Alpha)
{
    TArray<FVector2f> P;
    if(Type==EDWMinimapMarkerShape::Circle){for(int32 I=0;I<24;++I){const float A=2.f*PI*I/24.f;P.Add(Center+FVector2f(FMath::Cos(A),FMath::Sin(A))*Radius);}}
    else if(Type==EDWMinimapMarkerShape::Triangle)P={Center+FVector2f(0,-Radius),Center+FVector2f(Radius*.866f,Radius*.5f),Center+FVector2f(-Radius*.866f,Radius*.5f)};
    else P={Center+FVector2f(0,-Radius),Center+FVector2f(Radius,0),Center+FVector2f(0,Radius),Center+FVector2f(-Radius,0)};
    Shape(E,Layer,G,TintMaterial,P,Color,Scale,Alpha);
}
void Arrow(FSlateWindowElementList& E,int32 Layer,const FGeometry& G,UMaterialInterface* TintMaterial,FVector2f Center,FVector2f F,float Radius,FLinearColor Color,float Scale,float Alpha)
{
    if(!F.Normalize())F=FVector2f(0,-1);const FVector2f S(-F.Y,F.X);
    Shape(E,Layer,G,TintMaterial,{Center+F*Radius,Center-F*(Radius*.65f)+S*(Radius*.60f),Center-F*(Radius*.28f),Center-F*(Radius*.65f)-S*(Radius*.60f)},Color,Scale,Alpha);
}
}
int32 DWMapMiniSnapshotPaint::Paint(FSlateWindowElementList& E,int32 Layer,const FGeometry& G,
    UMaterialInterface* VertexTint,const DWMapMotionAdapter::FFrame& Frame,const TArray<FDWMinimapDisplayMarker>& Markers,
    const FDWMinimapNavigationTarget& Target,FVector Player,FVector Forward,FVector North,float Diameter,float Alpha)
{
    if(Alpha<=0.f)return Layer;
    FVector2D Center,End;if(!DWMapMotionAdapter::WorldToSlate(Frame,Player,Center))return Layer;
    const float Scale=Diameter/220.f,Inner=Diameter*.5f-(220.f*(6.f/180.f))*Scale;
    for(const auto& M:Markers)
    {
        FVector2D P;if(!DWMapMotionAdapter::WorldToSlate(Frame,M.WorldPosition,P))continue;
        if((P-Center).SizeSquared()>FMath::Square(FMath::Max(0.f,Inner-6.f*Scale)))continue;
        Marker(E,Layer,G,VertexTint,FVector2f(P),4.f*Scale,M.Shape,M.Color,Scale,Alpha);Layer+=2;
    }
    if(DWMapMotionAdapter::WorldToSlate(Frame,Player+Forward*100.f,End))
    {Arrow(E,Layer,G,VertexTint,FVector2f(Center),FVector2f(End-Center),11.f*Scale,FLinearColor::FromSRGBColor(FColor(25,159,181)),Scale,Alpha);Layer+=2;}
    if(Target.bValid)
    {
        FVector2D P;if(DWMapMotionAdapter::WorldToSlate(Frame,Target.WorldPosition,P))
        {
            FVector2D Offset=P-Center;const float R=7.f*Scale,Available=FMath::Max(0.f,Inner-R-2.f*Scale);
            if(Offset.SizeSquared()>FMath::Square(Available))
            {
                const FVector Delta=Target.NavigationPosition-Player;FVector2D Projected;
                FVector2D Direction=DWMapMotionAdapter::WorldToSlate(Frame,Player+FVector(Delta.X,Delta.Y,0.),Projected)?(Projected-Center).GetSafeNormal():FVector2D::ZeroVector;
                if(Direction.IsNearlyZero())Direction=Offset.GetSafeNormal();if(Direction.IsNearlyZero())Direction=FVector2D(0,-1);
                Arrow(E,Layer,G,VertexTint,FVector2f(Center+Direction*FMath::Max(0.f,Inner-12.f*Scale)),FVector2f(Direction),9.f*Scale,Target.Color,Scale,Alpha);
            }
            else Marker(E,Layer,G,VertexTint,FVector2f(P),R,Target.Shape,Target.Color,Scale,Alpha);
            Layer+=2;
        }
    }
    if(DWMapMotionAdapter::WorldToSlate(Frame,Player+North*100.f,End)&&FSlateApplication::IsInitialized()&&FSlateApplication::Get().GetRenderer())
    {
        FVector2D N=(End-Center).GetSafeNormal();if(N.IsNearlyZero())N=FVector2D(0,-1);const FVector2D East(-N.Y,N.X),Directions[]={N,East,-N,-East};
        const TCHAR* Labels[]={TEXT("N"),TEXT("E"),TEXT("S"),TEXT("W")};auto Font=FCoreStyle::GetDefaultFontStyle("Regular",10.f*Scale);Font.OutlineSettings.OutlineSize=0;
        auto Measure=FSlateApplication::Get().GetRenderer()->GetFontMeasureService();
        for(int32 I=0;I<4;++I){const FText T=FText::FromString(Labels[I]);const FVector2D S=Measure->Measure(T,Font);const FVector2D P=Center+Directions[I]*(Inner-10.f*Scale)-FVector2D(S.X*.5f,5.f*Scale);
            FSlateDrawElement::MakeText(E,Layer++,G.ToPaintGeometry(S,FSlateLayoutTransform(P)),T,Font,ESlateDrawEffect::None,FLinearColor(.18f,.16f,.13f,.78f*Alpha));}
    }
    return Layer;
}
