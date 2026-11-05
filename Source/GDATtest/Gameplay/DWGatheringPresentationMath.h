#pragma once
#include "CoreMinimal.h"
namespace DWGatheringPresentationMath
{
    inline float Unit(float V) { return FMath::IsFinite(V) ? FMath::Clamp(V,0.f,1.f) : 0.f; }
    inline float GatheredFraction(double Current,double Maximum)
    {
        if(!FMath::IsFinite(Current)||!FMath::IsFinite(Maximum)||Maximum<=0.)return 0.f;
        return float(FMath::Clamp(1.-Current/Maximum,0.,1.));
    }
    // Accelerating, gravity-inspired presentation; this is deliberately not Chaos simulation.
    inline float FallFraction(float T) { return FMath::Pow(Unit(T),2.25f); }
    // The first six drops match the sugar-tree ring. Wider outer rings keep larger tables readable.
    inline FVector ScatterOffset(int32 Slot)
    {
        const int32 Index = FMath::Max(0, Slot);
        const double Angle = double(Index) * 2.3999632297;
        const double Radius = 100. + 48. * (Index / 6);
        return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.);
    }
    inline FVector ScatterPoint(const FVector& Start,const FVector& End,float T,float Height)
    {
        const float A=Unit(T),H=FMath::IsFinite(Height)?FMath::Max(0.f,Height):0.f;
        return FMath::Lerp(Start,End,double(A))+FVector(0,0,4.f*H*A*(1.f-A));
    }
    inline FVector RotateAbout(const FVector& Point,const FVector& Pivot,const FQuat& Rotation)
    { return Pivot+Rotation.RotateVector(Point-Pivot); }
    inline float DissolveFraction(float AfterContact,float Hold,float Duration)
    {
        if(!FMath::IsFinite(AfterContact)||!FMath::IsFinite(Hold)||!FMath::IsFinite(Duration))return 0.f;
        return Unit((AfterContact-FMath::Max(0.f,Hold))/FMath::Max(.05f,Duration));
    }
}
