#pragma once

#include "CoreMinimal.h"
#include "Widgets/SLeafWidget.h"
#include "Rendering/DrawElements.h"

/** Material-free circular progress. Bind Fraction to saved sprint accumulation. */
class SDWRingProgress : public SLeafWidget
{
public:
    SLATE_BEGIN_ARGS(SDWRingProgress)
        : _Fraction(0.f), _Tint(FLinearColor(0.35f, 0.84f, 0.68f, 1.f)), _Size(92.f) {}
        SLATE_ATTRIBUTE(float, Fraction)
        SLATE_ATTRIBUTE(FLinearColor, Tint)
        SLATE_ARGUMENT(float, Size)
    SLATE_END_ARGS()

    void Construct(const FArguments& Args)
    {
        Fraction = Args._Fraction;
        Tint = Args._Tint;
        Diameter = Args._Size;
    }

    virtual FVector2D ComputeDesiredSize(float) const override { return FVector2D(Diameter); }
    void SetDiameter(float Value) { Diameter=FMath::Max(16.f,Value); Invalidate(EInvalidateWidgetReason::Layout); }

    virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& Geometry,
        const FSlateRect& CullingRect, FSlateWindowElementList& Elements,
        int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const override
    {
        const FVector2D Size = Geometry.GetLocalSize();
        const FVector2D Center = Size * .5f;
        const float Radius = FMath::Max(1.f, FMath::Min(Size.X, Size.Y) * .5f - 7.f);
        constexpr int32 Segments = 96;
        TArray<FVector2D> Track;
        Track.Reserve(Segments + 1);
        for (int32 I = 0; I <= Segments; ++I)
        {
            const float Angle = -HALF_PI + TWO_PI * I / Segments;
            Track.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
        }
        FSlateDrawElement::MakeLines(Elements, Layer, Geometry.ToPaintGeometry(), Track,
            ESlateDrawEffect::None, FLinearColor(.15f, .18f, .20f, .95f), true, 7.f);

        const float Progress = FMath::Clamp(Fraction.Get(), 0.f, 1.f);
        if (Progress > 0.f)
        {
            TArray<FVector2D> Arc;
            const int32 Count = FMath::Max(2, FMath::CeilToInt(Progress * Segments));
            Arc.Reserve(Count + 1);
            for (int32 I = 0; I <= Count; ++I)
            {
                const float Angle = -HALF_PI + TWO_PI * Progress * I / Count;
                Arc.Add(Center + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
            }
            FSlateDrawElement::MakeLines(Elements, Layer + 1, Geometry.ToPaintGeometry(), Arc,
                ESlateDrawEffect::None, Tint.Get() * Style.GetColorAndOpacityTint(), true, 7.f);
        }
        return Layer + 1;
    }

private:
    TAttribute<float> Fraction;
    TAttribute<FLinearColor> Tint;
    float Diameter = 92.f;
};
