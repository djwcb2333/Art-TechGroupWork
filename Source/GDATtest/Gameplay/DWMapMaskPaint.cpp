#include "DWMapMaskPaint.h"
#include "DWMapVisualMath.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/SlateBrush.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

namespace
{
    void CloseMapRim(TArray<FVector2f>& Points)
    {
        if(Points.IsEmpty())return;
        const FVector2f First=Points[0];
        Points.Add(First);
    }
}

bool DWMapMaskPaint::Paint(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
    UMaterialInterface* Material,FVector2D MiniPos,FVector2D MiniSize,FVector2D FramePos,FVector2D FrameSize,
    float Progress,bool bSourceMaterialContainsRim,float Opacity)
{
    if(!IsValid(Material)||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().GetRenderer()||
       !FMath::IsFinite(Progress)||!FMath::IsFinite(Opacity)||MiniPos.ContainsNaN()||MiniSize.ContainsNaN()||FramePos.ContainsNaN()||FrameSize.ContainsNaN())return false;
    const FVector2D View=Geometry.GetLocalSize();DWMapVisualMath::Rect Shape;double Radius=0.;
    const DWMapVisualMath::Rect Mini{MiniPos.X,MiniPos.Y,MiniPos.X+MiniSize.X,MiniPos.Y+MiniSize.Y};
    const DWMapVisualMath::Rect Frame{FramePos.X,FramePos.Y,FramePos.X+FrameSize.X,FramePos.Y+FrameSize.Y};
    if(!DWMapVisualMath::Morph(Mini,{View.X,View.Y},Progress,Shape,Radius)||FrameSize.X<=0.||FrameSize.Y<=0.)return false;
    const auto Polygon=DWMapVisualMath::Outline(Shape,Radius);if(Polygon.size()<3)return false;
    const DWMapVisualMath::Point Center{(Shape.Left+Shape.Right)*.5,(Shape.Top+Shape.Bottom)*.5};
    DWMapVisualMath::Point CenterUV;if(!DWMapVisualMath::UV(Center,Frame,CenterUV))return false;
    const FSlateRenderTransform Transform=Geometry.GetAccumulatedRenderTransform();
    const FVector2D AbsoluteSize=Geometry.GetAbsoluteSize();
    const FVector2D Scale(AbsoluteSize.X/FMath::Max(1.,View.X),AbsoluteSize.Y/FMath::Max(1.,View.Y));
    TArray<FSlateVertex> Vertices;TArray<SlateIndex> Indices;Vertices.Reserve(int32(Polygon.size())+1);Indices.Reserve(int32(Polygon.size())*3);
    const FColor White=FLinearColor(1,1,1,FMath::Clamp(Opacity,0.f,1.f)).ToFColor(true);
    auto Vertex=[&](const DWMapVisualMath::Point P,const DWMapVisualMath::Point UV)
    {
        const FVector2f Tex(UV.X,UV.Y);
        FSlateVertex V=FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(P.X,P.Y),FVector4f(Tex.X,Tex.Y,1.f,1.f),Tex,White);
        V.PixelSize[0]=uint16(FMath::Clamp(FMath::RoundToInt(FrameSize.X*Scale.X),1,65535));
        V.PixelSize[1]=uint16(FMath::Clamp(FMath::RoundToInt(FrameSize.Y*Scale.Y),1,65535));
        return V;
    };
    Vertices.Add(Vertex(Center,CenterUV));
    for(const auto P:Polygon)
    {
        DWMapVisualMath::Point UV;if(!DWMapVisualMath::UV(P,Frame,UV))return false;
        Vertices.Add(Vertex(P,UV));
    }
    for(int32 I=0;I<int32(Polygon.size());++I){Indices.Add(0);Indices.Add(SlateIndex(I+1));Indices.Add(SlateIndex((I+1)%int32(Polygon.size())+1));}
    FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.SetResourceObject(Material);
    FSlateDrawElement::MakeCustomVerts(Elements,Layer,FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush),Vertices,Indices,nullptr,0,0);
    if(!bSourceMaterialContainsRim&&Progress<1.f)
    {
        const float Alpha=FMath::Clamp((1.f-Progress)*1.5f*Opacity,0.f,1.f);
        const float RingWidth=float(MiniSize.X)*(6.f/180.f);
        const double Inset=FMath::Min(double(RingWidth)*.5,FMath::Min((Shape.Right-Shape.Left)*.49,(Shape.Bottom-Shape.Top)*.49));
        const DWMapVisualMath::Rect InsetShape{Shape.Left+Inset,Shape.Top+Inset,Shape.Right-Inset,Shape.Bottom-Inset};
        const auto RimPolygon=DWMapVisualMath::Outline(InsetShape,FMath::Max(0.,Radius-Inset));
        TArray<FVector2f> Rim;Rim.Reserve(int32(RimPolygon.size())+1);
        for(const auto P:RimPolygon)Rim.Add(FVector2f(P.X,P.Y));
        if(Rim.IsEmpty())return true;
        CloseMapRim(Rim);
        FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Rim,ESlateDrawEffect::None,FLinearColor(1,1,1,Alpha),true,RingWidth);
    }
    return true;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDWMapRimClosureTest,"DoughWorld.UI.Map.RimClosure",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FDWMapRimClosureTest::RunTest(const FString& Parameters)
{
    TArray<FVector2f> Empty;CloseMapRim(Empty);TestEqual(TEXT("Empty rim remains empty"),Empty.Num(),0);
    TArray<FVector2f> Rim;Rim.Reserve(53);
    for(int32 I=0;I<52;++I)Rim.Add(FVector2f(float(I),float(I+1)));
    const FVector2f First=Rim[0];CloseMapRim(Rim);
    TestEqual(TEXT("Closed rim contains one additional point"),Rim.Num(),53);
    TestTrue(TEXT("Closed rim ends at its first point"),Rim.Last()==First);
    return true;
}
#endif
