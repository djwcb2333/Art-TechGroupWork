#include "DWMinimapWidget.h"
#include "DWMinimap.h"
#include "DWLocalizationLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Fonts/FontMeasure.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateRenderer.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

namespace DWMinimapSlate
{
    constexpr float ReferenceDiameter=220.f;
    constexpr float ReferenceRingWidth=10.f;

    // A solid-color polygon uses Slate's white resource; it creates no texture or UE asset.
    static void Polygon(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        const TArray<FVector2f>& Points,const FLinearColor& Color)
    {
        if(Points.Num()<3) return;
        FVector2f Center=FVector2f::ZeroVector;
        for(const FVector2f& Point:Points) Center+=Point;
        Center/=float(Points.Num());
        TArray<FSlateVertex> Vertices;
        TArray<SlateIndex> Indices;
        const FColor Tint=Color.ToFColor(true);
        const FSlateRenderTransform Transform=Geometry.GetAccumulatedRenderTransform();
        Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,Center,FVector2f(.5f,.5f),Tint));
        for(const FVector2f& Point:Points)
            Vertices.Add(FSlateVertex::Make<ESlateVertexRounding::Disabled>(Transform,Point,FVector2f(.5f,.5f),Tint));
        for(int32 Index=0;Index<Points.Num();++Index)
        {
            Indices.Add(0);
            Indices.Add(SlateIndex(Index+1));
            Indices.Add(SlateIndex((Index+1)%Points.Num()+1));
        }
        const FSlateBrush* White=FCoreStyle::Get().GetBrush("WhiteBrush");
        FSlateDrawElement::MakeCustomVerts(Elements,Layer,
            FSlateApplication::Get().GetRenderer()->GetResourceHandle(*White),Vertices,Indices,nullptr,0,0);
    }

    static TArray<FVector2f> CirclePoints(FVector2f Center,float Radius,int32 Segments)
    {
        TArray<FVector2f> Points;
        Points.Reserve(Segments);
        for(int32 Index=0;Index<Segments;++Index)
        {
            const float Angle=2.f*PI*float(Index)/float(Segments);
            Points.Add(Center+FVector2f(FMath::Cos(Angle),FMath::Sin(Angle))*Radius);
        }
        return Points;
    }

    static void OutlinedPolygon(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        TArray<FVector2f> Points,const FLinearColor& Color,float Scale)
    {
        Polygon(Elements,Layer,Geometry,Points,Color);
        if(Points.Num()<3)return;
        const FVector2f First=Points[0];
        Points.Add(First);
        FSlateDrawElement::MakeLines(Elements,Layer+1,Geometry.ToPaintGeometry(),Points,
            ESlateDrawEffect::None,FLinearColor::White,true,1.35f*Scale);
    }

    static void Marker(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        FVector2f Center,float Radius,EDWMinimapMarkerShape Shape,const FLinearColor& Color,float Scale)
    {
        TArray<FVector2f> Points;
        switch(Shape)
        {
        case EDWMinimapMarkerShape::Circle:
            Points=CirclePoints(Center,Radius,24);
            break;
        case EDWMinimapMarkerShape::Triangle:
            Points={Center+FVector2f(0.f,-Radius),Center+FVector2f(Radius*.866f,Radius*.5f),
                Center+FVector2f(-Radius*.866f,Radius*.5f)};
            break;
        default:
            Points={Center+FVector2f(0.f,-Radius),Center+FVector2f(Radius,0.f),
                Center+FVector2f(0.f,Radius),Center+FVector2f(-Radius,0.f)};
            break;
        }
        OutlinedPolygon(Elements,Layer,Geometry,Points,Color,Scale);
    }

    static void Arrow(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        FVector2f Center,FVector2f Forward,float Radius,const FLinearColor& Color,float Scale)
    {
        if(!Forward.Normalize()) Forward=FVector2f(0.f,-1.f);
        const FVector2f Side(-Forward.Y,Forward.X);
        const TArray<FVector2f> Points={Center+Forward*Radius,
            Center-Forward*(Radius*.65f)+Side*(Radius*.60f),
            Center-Forward*(Radius*.28f),
            Center-Forward*(Radius*.65f)-Side*(Radius*.60f)};
        OutlinedPolygon(Elements,Layer,Geometry,Points,Color,Scale);
    }

    static FSlateFontInfo Font(float Size)
    {
        FSlateFontInfo Result=FCoreStyle::GetDefaultFontStyle("Regular",Size);
        Result.OutlineSettings.OutlineSize=1;
        Result.OutlineSettings.OutlineColor=FLinearColor(.025f,.020f,.018f,.90f);
        return Result;
    }

    static FVector2f Measure(const FText& Text,const FSlateFontInfo& Info)
    {
        return FVector2f(FSlateApplication::Get().GetRenderer()->GetFontMeasureService()->Measure(Text,Info));
    }

    static void CenterText(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        const FText& Text,float CenterX,float Top,float Width,const FSlateFontInfo& Info,const FLinearColor& Color)
    {
        const FVector2f Size=Measure(Text,Info);
        // Explicit clipping is rectangular only for the information row, never for map markers.
        const FGeometry ClipGeometry=Geometry.MakeChild(FVector2f(Width,Size.Y+3.f),
            FSlateLayoutTransform(FVector2f(CenterX-Width*.5f,Top)));
        Elements.PushClip(FSlateClippingZone(ClipGeometry));
        FSlateDrawElement::MakeText(Elements,Layer,
            Geometry.ToPaintGeometry(Size,FSlateLayoutTransform(FVector2f(CenterX-Size.X*.5f,Top))),
            Text,Info,ESlateDrawEffect::None,Color);
        Elements.PopClip();
    }

    static int32 Cardinals(FSlateWindowElementList& Elements,int32 Layer,const FGeometry& Geometry,
        FVector2f Center,float Diameter,float Scale,FVector2f North=FVector2f(0.f,-1.f))
    {
        FSlateFontInfo CompassFont=Font(10.f*Scale);
        CompassFont.OutlineSettings.OutlineSize=0;
        const FLinearColor Color(.18f,.16f,.13f,.78f);
        const float RingWidth=ReferenceRingWidth*Scale;
        const float InnerRadius=Diameter*.5f-RingWidth;
        const FVector2f East(-North.Y,North.X);
        const FVector2f Directions[]={North,East,-North,-East};
        const TCHAR* Labels[]={TEXT("N"),TEXT("E"),TEXT("S"),TEXT("W")};
        for(int32 I=0;I<4;++I)
        {
            const FVector2f Position=Center+Directions[I]*(InnerRadius-10.f*Scale);
            CenterText(Elements,Layer++,Geometry,FText::FromString(Labels[I]),Position.X,
                Position.Y-5.f*Scale,20.f*Scale,CompassFont,Color);
        }
        return Layer;
    }

    static FText Compass(float Bearing)
    {
        static const TCHAR* Names[]={TEXT("N"),TEXT("NE"),TEXT("E"),TEXT("SE"),TEXT("S"),TEXT("SW"),TEXT("W"),TEXT("NW")};
        return FText::FromString(Names[FMath::FloorToInt((Bearing+22.5f)/45.f)%8]);
    }

    static FText FitLabel(const FText& Label,const FText& Suffix,float Width,const FSlateFontInfo& Info)
    {
        FString Name=Label.ToString();
        Name.ReplaceInline(TEXT("\r"),TEXT(" "));
        Name.ReplaceInline(TEXT("\n"),TEXT(" "));
        const FString Tail=Suffix.ToString();
        const float Available=FMath::Max(0.f,Width-Measure(Suffix,Info).X);
        const bool bNeedsEllipsis=Measure(FText::FromString(Name),Info).X>Available;
        if(bNeedsEllipsis)
        {
            while(!Name.IsEmpty() && Measure(FText::FromString(Name+TEXT("…")),Info).X>Available)
            {
                Name.LeftChopInline(1);
                // Do not leave the first half of a UTF-16 surrogate pair at the end.
                if(!Name.IsEmpty() && Name[Name.Len()-1]>=0xD800 && Name[Name.Len()-1]<=0xDBFF) Name.LeftChopInline(1);
            }
            Name+=TEXT("…");
        }
        return FText::FromString(Name+Tail);
    }

    static FText NavigationSuffix(const UDWMinimapComponent* Map,const FDWMinimapNavigationTarget& Target)
    {
        const FVector Player=Map->GetPlayerLocation();
        const float Meters=DWMinimapMath::DistanceXYMeters(Player,Target.NavigationPosition);
        const float Bearing=DWMinimapMath::BearingDegrees(Player,Target.NavigationPosition,Map->NorthYaw);
        FNumberFormattingOptions NumberOptions;
        NumberOptions.SetMaximumFractionalDigits(0);
        return FText::Format(FText::FromString(TEXT(" · {0} {1} m")),Compass(Bearing),FText::AsNumber(Meters,&NumberOptions));
    }

    static FText NavigationLabel(const UDWMinimapComponent* Map,const FDWMinimapNavigationTarget& Target)
    {
        return Target.Label.IsEmpty()?DWText(Map,TEXT("标记地点"),TEXT("Waypoint")):Target.Label;
    }

    static bool IsTargetOutside(const UDWMinimapComponent* Map,const FDWMinimapNavigationTarget& Target,float Diameter)
    {
        if(!Target.bValid) return false;
        const FDWMinimapProjection& Projection=Map->GetProjection();
        const FVector2D Offset=(Projection.Project(Target.WorldPosition)-Projection.Project(Map->GetPlayerLocation()))*Diameter;
        const float Scale=Diameter/ReferenceDiameter;
        const float AvailableRadius=FMath::Max(0.f,Diameter*.5f-ReferenceRingWidth*Scale-7.f*Scale-2.f*Scale);
        return Offset.SizeSquared()>FMath::Square(AvailableRadius);
    }
}

class SDWMinimap : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SDWMinimap) : _DrawFooter(true) {}
        SLATE_ARGUMENT(TWeakObjectPtr<UDWMinimapComponent>,Component)
        SLATE_ARGUMENT(TWeakObjectPtr<UDWMinimapWidget>,Host)
        SLATE_ARGUMENT(TWeakObjectPtr<UDWMinimapView>,PreviewOwner)
        SLATE_ARGUMENT(bool,DrawFooter)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Component=Args._Component;
        Host=Args._Host;
        PreviewOwner=Args._PreviewOwner;
        bDrawFooter=Args._DrawFooter;
        SetVisibility(EVisibility::HitTestInvisible);
        // Moving actors and the selected target are resolved live, independent of capture frequency.
        SetCanTick(false);
        ForceVolatile(true);
    }

    virtual FVector2D ComputeDesiredSize(float) const override
    {
        const float Diameter=GetDesiredDiameter();
        return FVector2D(Diameter,Diameter+(bDrawFooter?44.f:0.f));
    }

    virtual int32 OnPaint(const FPaintArgs& Args,const FGeometry& Geometry,const FSlateRect& Culling,
        FSlateWindowElementList& Elements,int32 Layer,const FWidgetStyle& Style,bool bParentEnabled) const override
    {
        const UDWMinimapComponent* Map=ResolveComponent();
        if(Map && !Map->IsMinimapVisible()) return Layer;
        if(!Map && !PreviewOwner.IsValid()) return Layer;
        // A surface is always a true circle, including when a Designer slot stretches its allocated size.
        const float AvailableHeight=float(Geometry.GetLocalSize().Y)-(bDrawFooter?44.f:0.f);
        const float Diameter=FMath::Min3(GetDesiredDiameter(),float(Geometry.GetLocalSize().X),AvailableHeight);
        if(Diameter<1.f) return Layer;
        const float Scale=Diameter/DWMinimapSlate::ReferenceDiameter;
        const float RingWidth=DWMinimapSlate::ReferenceRingWidth*Scale;
        const float InnerRadius=Diameter*.5f-RingWidth;
        const FVector2f Center(Diameter*.5f,Diameter*.5f);
        const float MarkerRadius=4.f*Scale;
        const FGeometry MapGeometry=Geometry.MakeChild(FVector2f(Diameter,Diameter),FSlateLayoutTransform());

        if(UMaterialInstanceDynamic* Material=Map?Map->GetDisplayMaterial():nullptr)
        {
            FSlateBrush Brush;
            Brush.DrawAs=ESlateBrushDrawType::Image;
            Brush.ImageSize=FVector2f(Diameter,Diameter);
            Brush.SetResourceObject(Material);
            FSlateDrawElement::MakeBox(Elements,Layer,MapGeometry.ToPaintGeometry(),&Brush,
                ESlateDrawEffect::None,Style.GetColorAndOpacityTint());
        }
        else
        {
            // Diagnostics still show an honest blank map; no fictional texture substitutes for missing capture.
            DWMinimapSlate::Polygon(Elements,Layer,Geometry,
                DWMinimapSlate::CirclePoints(Center,Diameter*.5f,96),FLinearColor::White);
            DWMinimapSlate::Polygon(Elements,Layer+1,Geometry,
                DWMinimapSlate::CirclePoints(Center,InnerRadius,96),FLinearColor(.64f,.60f,.53f,1.f));
        }

        int32 DrawLayer=Layer+2;
        if(!Map)
        {
            // Designer has no capture/pawn. Show only a plain, explicitly non-world placeholder.
            DWMinimapSlate::Arrow(Elements,DrawLayer,Geometry,Center,FVector2f(0.f,-1.f),11.f*Scale,
                FLinearColor::FromSRGBColor(FColor(25,159,181)),Scale);
            DrawLayer+=2;
            return DWMinimapSlate::Cardinals(Elements,DrawLayer,Geometry,Center,Diameter,Scale);
        }

        const FDWMinimapProjection& Projection=Map->GetProjection();
        const FVector PlayerPosition=Map->GetPlayerLocation();
        const FVector2D PlayerShift=Projection.Project(PlayerPosition);
        auto Project=[&](const FVector& Position)
        {
            return FVector2f((Projection.Project(Position)-PlayerShift)*Diameter);
        };
        TArray<FDWMinimapDisplayMarker> Markers;
        Map->GatherDisplayMarkers(Markers);
        Markers.StableSort([](const FDWMinimapDisplayMarker& A,const FDWMinimapDisplayMarker& B){return A.Priority<B.Priority;});
        for(const FDWMinimapDisplayMarker& Marker:Markers)
        {
            const FVector2f Offset=Project(Marker.WorldPosition);
            if(Offset.SizeSquared()>FMath::Square(FMath::Max(0.f,InnerRadius-MarkerRadius-2.f*Scale))) continue;
            DWMinimapSlate::Marker(Elements,DrawLayer,Geometry,Center+Offset,MarkerRadius,Marker.Shape,Marker.Color,Scale);
            DrawLayer+=2;
        }

        DWMinimapSlate::Arrow(Elements,DrawLayer,Geometry,Center,FVector2f(Map->GetPlayerForwardOnMap()),
            11.f*Scale,FLinearColor::FromSRGBColor(FColor(25,159,181)),Scale);
        DrawLayer+=2;
        const FDWMinimapNavigationTarget Target=Map->GetNavigationTarget();
        bool bOutside=false;
        if(Target.bValid)
        {
            const FVector2f Offset=Project(Target.WorldPosition);
            const float SelectedRadius=7.f*Scale;
            const float AvailableRadius=FMath::Max(0.f,InnerRadius-SelectedRadius-2.f*Scale);
            bOutside=Offset.SizeSquared()>FMath::Square(AvailableRadius);
            if(bOutside)
            {
                const FVector Delta=Target.NavigationPosition-PlayerPosition;
                FVector2f Direction(Map->GetWorldDirectionOnMap(FVector(Delta.X,Delta.Y,0.f)));
                if(Direction.IsNearlyZero())Direction=Offset.GetSafeNormal();
                const float EdgeRadius=FMath::Max(0.f,InnerRadius-12.f*Scale);
                DWMinimapSlate::Arrow(Elements,DrawLayer,Geometry,Center+Direction*EdgeRadius,Direction,
                    9.f*Scale,Target.Color,Scale);
            }
            else
                DWMinimapSlate::Marker(Elements,DrawLayer,Geometry,Center+Offset,SelectedRadius,Target.Shape,Target.Color,Scale);
            DrawLayer+=2;

            if(bDrawFooter)
            {
                // Native fallback retains the same values as the optional Designer-authored TextBlocks.
                const FText Suffix=DWMinimapSlate::NavigationSuffix(Map,Target);
                const FText Label=DWMinimapSlate::NavigationLabel(Map,Target);
                const FSlateFontInfo LabelFont=DWMinimapSlate::Font(12.f);
                const FText Row=DWMinimapSlate::FitLabel(Label,Suffix,Diameter-4.f*Scale,LabelFont);
                DWMinimapSlate::CenterText(Elements,DrawLayer++,Geometry,Row,Center.X,Diameter+5.f,
                    Diameter,LabelFont,FLinearColor::White);
                if(bOutside)
                    DWMinimapSlate::CenterText(Elements,DrawLayer++,Geometry,
                        DWText(Map,TEXT("当前位置范围外"),TEXT("Outside minimap")),Center.X,Diameter+22.f,
                        Diameter,DWMinimapSlate::Font(10.f),FLinearColor(.85f,.81f,.72f,1.f));
            }
        }

        // World north remains truthful while the letters orbit the circle, staying upright.
        const FVector North=FRotator(0.f,Map->NorthYaw,0.f).Vector();
        return DWMinimapSlate::Cardinals(Elements,DrawLayer,Geometry,Center,Diameter,Scale,FVector2f(Map->GetWorldDirectionOnMap(North)));
    }
private:
    const UDWMinimapComponent* ResolveComponent() const
    {
        if(const UDWMinimapWidget* Widget=Host.Get()) return Widget->GetMinimapComponent();
        return Component.Get();
    }
    float GetDesiredDiameter() const
    {
        if(const UDWMinimapComponent* Map=ResolveComponent()) return FMath::Clamp(Map->Diameter,160.f,400.f);
        if(const UDWMinimapView* Preview=PreviewOwner.Get()) return FMath::Clamp(Preview->PreviewDiameter,160.f,400.f);
        return DWMinimapSlate::ReferenceDiameter;
    }
    TWeakObjectPtr<UDWMinimapComponent> Component;
    TWeakObjectPtr<UDWMinimapWidget> Host;
    TWeakObjectPtr<UDWMinimapView> PreviewOwner;
    bool bDrawFooter=true;
};

UDWMinimapView::UDWMinimapView()
{
    SetVisibility(ESlateVisibility::HitTestInvisible);
}

TSharedRef<SWidget> UDWMinimapView::RebuildWidget()
{
    // Keep the host weakly, and resolve its component during painting: UMG may rebuild before InitializeMinimap.
    UDWMinimapWidget* Host=GetTypedOuter<UDWMinimapWidget>();
    return SNew(SDWMinimap).Host(Host).PreviewOwner(this).DrawFooter(false);
}

void UDWMinimapView::SynchronizeProperties()
{
    Super::SynchronizeProperties();
    InvalidateLayoutAndVolatility();
}

#if WITH_EDITOR
const FText UDWMinimapView::GetPaletteCategory()
{
    return FText::FromString(TEXT("DoughWorld"));
}
#endif

UDWMinimapComponent* UDWMinimapWidget::GetMinimapComponent() const
{
    return MinimapComponent.Get();
}

void UDWMinimapWidget::InitializeMinimap(UDWMinimapComponent* InComponent)
{
    MinimapComponent=InComponent;
    SetVisibility(ESlateVisibility::HitTestInvisible);
    UWorld* World=InComponent?InComponent->GetWorld():nullptr;
    UDWMinimapNavigationSubsystem* TaskNavigation=World?World->GetSubsystem<UDWMinimapNavigationSubsystem>():nullptr;
    if(NavigationSubsystem.Get()!=TaskNavigation)
    {
        if(UDWMinimapNavigationSubsystem* Previous=NavigationSubsystem.Get())
            Previous->OnNavigationTargetChanged.RemoveDynamic(this,&UDWMinimapWidget::HandleNavigationChanged);
        NavigationSubsystem=TaskNavigation;
        if(TaskNavigation)
            TaskNavigation->OnNavigationTargetChanged.AddUniqueDynamic(this,&UDWMinimapWidget::HandleNavigationChanged);
        FinishNavigationFeedback();
    }
    const float Diameter=InComponent?FMath::Clamp(InComponent->Diameter,160.f,400.f):DWMinimapSlate::ReferenceDiameter;
    // The widget includes a 44-unit footer; keep the map circle's center fixed during uniform scaling.
    SetRenderTransformPivot(FVector2D(.5f,Diameter/(2.f*(Diameter+44.f))));
    UpdateNavigationText(InComponent && InComponent->IsMinimapVisible());
    InvalidateLayoutAndVolatility();
}

void UDWMinimapWidget::HandleNavigationChanged()
{
    const UDWMinimapComponent* Map=MinimapComponent.Get();
    if(!bAnimateNavigationChanges || !Map || !Map->IsMinimapVisible())
    {
        FinishNavigationFeedback();
        return;
    }
    // The shared subsystem signals selection/invalidation, never each actor movement update.
    NavigationPulseElapsed=0.f;
    SetRenderScale(FVector2D(1.f,1.f));
}

void UDWMinimapWidget::FinishNavigationFeedback()
{
    NavigationPulseElapsed=.34f;
    SetRenderScale(FVector2D(1.f,1.f));
}

void UDWMinimapWidget::SetVisibility(ESlateVisibility InVisibility)
{
    // Deactivate can stop component ticking before the next feedback update; cancel synchronously when hidden.
    if(InVisibility==ESlateVisibility::Hidden || InVisibility==ESlateVisibility::Collapsed)
        FinishNavigationFeedback();
    Super::SetVisibility(InVisibility);
}

void UDWMinimapWidget::UpdateNavigationFeedback(float DeltaSeconds,bool bVisible)
{
    const UDWMinimapComponent* Map=MinimapComponent.Get();
    const float Diameter=Map?FMath::Clamp(Map->Diameter,160.f,400.f):DWMinimapSlate::ReferenceDiameter;
    SetRenderTransformPivot(FVector2D(.5f,Diameter/(2.f*(Diameter+44.f))));
    UpdateNavigationText(bVisible);
    if(!bVisible || !bAnimateNavigationChanges || !Map)
    {
        FinishNavigationFeedback();
        return;
    }
    constexpr float Duration=.34f;
    if(NavigationPulseElapsed>=Duration) return;
    if(FMath::IsFinite(DeltaSeconds))
        NavigationPulseElapsed=FMath::Min(Duration,NavigationPulseElapsed+FMath::Max(0.f,DeltaSeconds));
    if(NavigationPulseElapsed>=Duration)
    {
        FinishNavigationFeedback();
        return;
    }
    static constexpr float ScaleKeys[]={1.f,.97f,1.018f,.995f,1.f};
    const float Progress=NavigationPulseElapsed/Duration*4.f;
    const int32 Segment=FMath::Clamp(FMath::FloorToInt(Progress),0,3);
    const float Alpha=FMath::Clamp(Progress-float(Segment),0.f,1.f);
    const float Ease=Alpha*Alpha*(3.f-2.f*Alpha);
    const float Scale=FMath::Lerp(ScaleKeys[Segment],ScaleKeys[Segment+1],Ease);
    SetRenderScale(FVector2D(Scale,Scale));
}

void UDWMinimapWidget::UpdateNavigationText(bool bVisible)
{
    const UDWMinimapComponent* Map=MinimapComponent.Get();
    const FDWMinimapNavigationTarget Target=Map?Map->GetNavigationTarget():FDWMinimapNavigationTarget();
    const bool bHasTarget=bVisible && Map && Target.bValid;
    if(NavigationInfoText)
    {
        if(bHasTarget)
        {
            const float Diameter=FMath::Clamp(Map->Diameter,160.f,400.f);
            const float CachedWidth=float(NavigationInfoText->GetCachedGeometry().GetLocalSize().X);
            const float Width=CachedWidth>1.f?CachedWidth:Diameter-4.f*Diameter/DWMinimapSlate::ReferenceDiameter;
            const FText Row=DWMinimapSlate::FitLabel(DWMinimapSlate::NavigationLabel(Map,Target),
                DWMinimapSlate::NavigationSuffix(Map,Target),Width,NavigationInfoText->GetFont());
            if(!NavigationInfoText->GetText().EqualTo(Row)) NavigationInfoText->SetText(Row);
        }
        else if(!NavigationInfoText->GetText().IsEmpty()) NavigationInfoText->SetText(FText::GetEmpty());
        NavigationInfoText->SetVisibility(bHasTarget?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    }
    if(OutsideInfoText)
    {
        const bool bOutside=bHasTarget && DWMinimapSlate::IsTargetOutside(Map,Target,FMath::Clamp(Map->Diameter,160.f,400.f));
        const FText Outside=bOutside?DWText(Map,TEXT("当前位置范围外"),TEXT("Outside minimap")):FText::GetEmpty();
        if(!OutsideInfoText->GetText().EqualTo(Outside)) OutsideInfoText->SetText(Outside);
        OutsideInfoText->SetVisibility(bOutside?ESlateVisibility::HitTestInvisible:ESlateVisibility::Hidden);
    }
}

void UDWMinimapWidget::NativeDestruct()
{
    if(UDWMinimapNavigationSubsystem* TaskNavigation=NavigationSubsystem.Get())
        TaskNavigation->OnNavigationTargetChanged.RemoveDynamic(this,&UDWMinimapWidget::HandleNavigationChanged);
    NavigationSubsystem.Reset();
    FinishNavigationFeedback();
    Super::NativeDestruct();
}

TSharedRef<SWidget> UDWMinimapWidget::RebuildWidget()
{
    if(WidgetTree && WidgetTree->RootWidget) return Super::RebuildWidget();
    return SNew(SDWMinimap).Host(this);
}
