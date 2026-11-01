#include "DWMapMaskPaint.h"
#include "DWMapOpenPhases.h"
#include "DWMapVisualMath.h"
#include "Framework/Application/SlateApplication.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/SlateBrush.h"
#include "Styling/CoreStyle.h"

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
    float Progress,bool bSourceMaterialContainsRim,float Opacity,bool bCircleOnly)
{
    if(!IsValid(Material)||!FSlateApplication::IsInitialized()||!FSlateApplication::Get().GetRenderer()||
       !FMath::IsFinite(Progress)||!FMath::IsFinite(Opacity)||MiniPos.ContainsNaN()||MiniSize.ContainsNaN()||FramePos.ContainsNaN()||FrameSize.ContainsNaN())return false;
    const FVector2D View=Geometry.GetLocalSize();DWMapVisualMath::Rect Shape;double Radius=0.;
    const DWMapVisualMath::Rect Mini{MiniPos.X,MiniPos.Y,MiniPos.X+MiniSize.X,MiniPos.Y+MiniSize.Y};
    const DWMapVisualMath::Rect Frame{FramePos.X,FramePos.Y,FramePos.X+FrameSize.X,FramePos.Y+FrameSize.Y};
    if(FrameSize.X<=0.||FrameSize.Y<=0.)return false;
    if(bCircleOnly)
    {DWMapOpenPhases::Circle C;if(!DWMapOpenPhases::RevealCircle(MiniPos.X,MiniPos.Y,MiniSize.X,MiniSize.Y,View.X,View.Y,Progress,C))return false;
     Shape={C.X-C.Radius,C.Y-C.Radius,C.X+C.Radius,C.Y+C.Radius};Radius=C.Radius;}
    else if(!DWMapVisualMath::Morph(Mini,{View.X,View.Y},Progress,Shape,Radius))return false;
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

bool DWMapMaskPaint::PaintImage(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
    UTexture2D* Texture,const FDWMapImageSettings& Settings,const DWMapMotionMath::View& View,
    FVector2D MiniPos,FVector2D MiniSize,float Progress,float Opacity)
{
    if(!FSlateApplication::IsInitialized()||!FSlateApplication::Get().GetRenderer()||!Settings.IsValidCalibration()||
       !FMath::IsFinite(Opacity)||!DWMapMotionMath::Valid(View))return false;
    const FVector2D Size=Geometry.GetLocalSize();DWMapOpenPhases::Circle Circle;
    if(!DWMapOpenPhases::RevealCircle(MiniPos.X,MiniPos.Y,MiniSize.X,MiniSize.Y,Size.X,Size.Y,Progress,Circle))return false;
    const auto Outline=DWMapVisualMath::Outline({Circle.X-Circle.Radius,Circle.Y-Circle.Radius,Circle.X+Circle.Radius,Circle.Y+Circle.Radius},Circle.Radius);
    struct Point{FVector2D P,UV;};TArray<Point> Polygon;
    for(const auto P:Outline)
    {
        FVector2D World,UV;
        if(!DWMapImageMath::ScreenToWorld(View,FVector2D(P.X,P.Y),World)||!Settings.WorldToUV(World,UV))return false;
        Polygon.Add({FVector2D(P.X,P.Y),UV});
    }
    const FSlateRenderTransform Transform=Geometry.GetAccumulatedRenderTransform();
    const FColor Tint=FLinearColor(1,1,1,FMath::Clamp(Opacity,0.f,1.f)).ToFColor(true);
    auto Draw=[&](const TArray<Point>& Points,const FSlateBrush& Brush,int32 DrawLayer)
    {
        if(Points.Num()<3)return;
        TArray<FSlateVertex> V;TArray<SlateIndex> I;
        for(const auto& P:Points)V.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,FVector2f(P.P),FVector2f(P.UV),Tint));
        for(int32 K=1;K+1<Points.Num();++K){I.Add(0);I.Add(SlateIndex(K));I.Add(SlateIndex(K+1));}
        FSlateDrawElement::MakeCustomVerts(Elements,DrawLayer,FSlateApplication::Get().GetRenderer()->GetResourceHandle(Brush),V,I,nullptr,0,0);
    };
    // The coverage outside the authored image is plain white, never a stretched edge texel.
    TArray<Point> White=Polygon;for(auto& P:White)P.UV=FVector2D(.5,.5);
    Draw(White,*FCoreStyle::Get().GetBrush("WhiteBrush"),Layer);
    if(!IsValid(Texture))return true;
    // Clip the convex circular polygon to the image's four UV half-planes.
    for(int32 Plane=0;Plane<4&&!Polygon.IsEmpty();++Plane)
    {
        TArray<Point> Out;
        auto Distance=[&](const Point& P){return Plane==0?P.UV.X:Plane==1?1.-P.UV.X:Plane==2?P.UV.Y:1.-P.UV.Y;};
        Point A=Polygon.Last();double DA=Distance(A);
        for(const Point& B:Polygon)
        {
            const double DB=Distance(B);
            if((DA>=0.)!=(DB>=0.))
            {const double T=DA/(DA-DB);Out.Add({FMath::Lerp(A.P,B.P,T),FMath::Lerp(A.UV,B.UV,T)});}
            if(DB>=0.)Out.Add(B);A=B;DA=DB;
        }
        Polygon=MoveTemp(Out);
    }
    FSlateBrush Brush;Brush.DrawAs=ESlateBrushDrawType::Image;Brush.SetResourceObject(Texture);
    Draw(Polygon,Brush,Layer+1);return true;
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
