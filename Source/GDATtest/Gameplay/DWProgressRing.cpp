#include "DWProgressRing.h"
#include "SDWRingProgress.h"
void UDWProgressRing::SetFraction(float Value) { Fraction=FMath::Clamp(Value,0.f,1.f); if(Ring.IsValid())Ring->Invalidate(EInvalidateWidgetReason::Paint); }
TSharedRef<SWidget> UDWProgressRing::RebuildWidget()
{
    return SAssignNew(Ring,SDWRingProgress).Fraction_Lambda([this](){return Fraction;}).Tint_Lambda([this](){return Tint;}).Size(Diameter);
}
void UDWProgressRing::SynchronizeProperties(){Super::SynchronizeProperties();if(Ring.IsValid()){Ring->SetDiameter(Diameter);Ring->Invalidate(EInvalidateWidgetReason::Paint);}}
void UDWProgressRing::ReleaseSlateResources(bool Children){Super::ReleaseSlateResources(Children);Ring.Reset();}
