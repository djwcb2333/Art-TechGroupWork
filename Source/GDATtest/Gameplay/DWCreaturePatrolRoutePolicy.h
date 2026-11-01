#pragma once
#include "CoreMinimal.h"

/** Pure geometry: the entire pawn, not just its center, stays within the patrol disk. */
namespace DWCreaturePatrolRoutePolicy
{
 inline bool Inside(const FVector& Point,const FVector& Center,double Outer,double CapsuleRadius)
 {
  const double Safe=Outer-CapsuleRadius;
  return !Point.ContainsNaN()&&!Center.ContainsNaN()&&FMath::IsFinite(Outer)&&FMath::IsFinite(CapsuleRadius)&&
   CapsuleRadius>0.&&Safe>=0.&&FVector::DistSquared2D(Point,Center)<=Safe*Safe;
 }
 inline bool Target(const FVector& Point,const FVector& Center,double Inner,double Outer,double CapsuleRadius)
 {
  return FMath::IsFinite(Inner)&&Inner>=0.&&Inside(Point,Center,Outer,CapsuleRadius)&&
   FVector::DistSquared2D(Point,Center)>=FMath::Square(Inner+CapsuleRadius);
 }
}
