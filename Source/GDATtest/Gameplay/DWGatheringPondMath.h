#pragma once
#include "CoreMinimal.h"
#include <cmath>

/** Pure geometry/rate guards shared by the pond and its automation checks. */
namespace DWGatheringPondMath
{
// All polygon coordinates are in the pond's local XY plane. Water and gameplay use
// exactly these samples; no convex hull, center fan or hidden ellipse is substituted.
inline double Cross2(const FVector2D& A,const FVector2D& B,const FVector2D& C)
{return (B.X-A.X)*(C.Y-A.Y)-(B.Y-A.Y)*(C.X-A.X);}

inline double SegmentDistanceSquared(const FVector2D& P,const FVector2D& A,const FVector2D& B)
{
    const FVector2D D=B-A;const double L=D.SizeSquared();
    const double T=L>UE_DOUBLE_SMALL_NUMBER?FMath::Clamp(FVector2D::DotProduct(P-A,D)/L,0.,1.):0.;
    return (P-(A+D*T)).SizeSquared();
}

inline double SignedArea2(const TArray<FVector2D>& Polygon)
{
    double Area=0.;
    for(int32 I=0;I<Polygon.Num();++I)
    {const auto& A=Polygon[I];const auto& B=Polygon[(I+1)%Polygon.Num()];Area+=A.X*B.Y-A.Y*B.X;}
    return Area;
}

inline bool SegmentsTouch(const FVector2D& A,const FVector2D& B,const FVector2D& C,const FVector2D& D)
{
    constexpr double E=.0001;
    const double AB_C=Cross2(A,B,C),AB_D=Cross2(A,B,D),CD_A=Cross2(C,D,A),CD_B=Cross2(C,D,B);
    if(((AB_C>E&&AB_D<-E)||(AB_C<-E&&AB_D>E))&&
       ((CD_A>E&&CD_B<-E)||(CD_A<-E&&CD_B>E)))return true;
    return (FMath::Abs(AB_C)<=E&&SegmentDistanceSquared(C,A,B)<=E*E)||
        (FMath::Abs(AB_D)<=E&&SegmentDistanceSquared(D,A,B)<=E*E)||
        (FMath::Abs(CD_A)<=E&&SegmentDistanceSquared(A,C,D)<=E*E)||
        (FMath::Abs(CD_B)<=E&&SegmentDistanceSquared(B,C,D)<=E*E);
}

inline bool ValidateSimplePolygon(const TArray<FVector2D>& Polygon,FString* Error=nullptr)
{
    auto Fail=[Error](const TCHAR* Message){if(Error)*Error=Message;return false;};
    const int32 N=Polygon.Num();
    if(N<3||N>512)return Fail(TEXT("Boundary needs 3 to 512 sampled vertices."));
    for(int32 I=0;I<N;++I)
    {
        const auto& A=Polygon[I];const auto& B=Polygon[(I+1)%N];const auto& C=Polygon[(I+2)%N];
        if(A.ContainsNaN()||FMath::Abs(A.X)>100000.||FMath::Abs(A.Y)>100000.)
            return Fail(TEXT("Boundary contains non-finite or out-of-range coordinates."));
        if((B-A).SizeSquared()<.0001)return Fail(TEXT("Boundary contains duplicate adjacent samples."));
        if(FMath::Abs(Cross2(A,B,C))<.0001&&FVector2D::DotProduct(B-A,C-B)<0.)
            return Fail(TEXT("Boundary doubles back over an adjacent edge."));
        for(int32 J=I+1;J<N;++J)
        {
            if(J==I+1||(I==0&&J==N-1))continue;
            if(SegmentsTouch(A,B,Polygon[J],Polygon[(J+1)%N]))
                return Fail(TEXT("Boundary self-intersects or touches a non-adjacent edge."));
        }
    }
    if(!std::isfinite(SignedArea2(Polygon))||FMath::Abs(SignedArea2(Polygon))<1.)
        return Fail(TEXT("Boundary has zero or negligible area."));
    if(Error)Error->Reset();return true;
}

/** Caller provides a validated simple polygon. Boundary itself counts as inside. */
inline bool ContainsPoint(const TArray<FVector2D>& Polygon,const FVector2D& Point,double EdgeTolerance=.001)
{
    if(Polygon.Num()<3||Point.ContainsNaN())return false;
    bool Inside=false;
    for(int32 I=0,J=Polygon.Num()-1;I<Polygon.Num();J=I++)
    {
        const auto& A=Polygon[J];const auto& B=Polygon[I];
        if(SegmentDistanceSquared(Point,A,B)<=EdgeTolerance*EdgeTolerance)return true;
        if((A.Y>Point.Y)!=(B.Y>Point.Y))
        {
            const double X=A.X+(Point.Y-A.Y)*(B.X-A.X)/(B.Y-A.Y);
            if(Point.X<X)Inside=!Inside;
        }
    }
    return Inside;
}

inline double DistanceToBoundary(const TArray<FVector2D>& Polygon,const FVector2D& Point)
{
    double DistanceSquared=TNumericLimits<double>::Max();
    for(int32 I=0;I<Polygon.Num();++I)
        DistanceSquared=FMath::Min(DistanceSquared,SegmentDistanceSquared(Point,Polygon[I],Polygon[(I+1)%Polygon.Num()]));
    return FMath::Sqrt(DistanceSquared);
}

/** Ear clipping preserves notches; returned triangles face local +Z for either input winding. */
inline bool TriangulatePolygon(const TArray<FVector2D>& Polygon,TArray<int32>& Triangles,FString* Error=nullptr)
{
    Triangles.Reset();if(!ValidateSimplePolygon(Polygon,Error))return false;
    TArray<int32> Active;const int32 N=Polygon.Num();Active.Reserve(N);
    for(int32 I=0;I<N;++I)Active.Add(SignedArea2(Polygon)>0.?I:N-1-I);
    // Linear spline sections can produce collinear samples. Remove these only from
    // the triangulation index list; retain the authoritative sampled outline.
    bool Removed=true;
    while(Removed&&Active.Num()>3)
    {
        Removed=false;
        for(int32 I=0;I<Active.Num();++I)
        {
            const int32 A=Active[(I+Active.Num()-1)%Active.Num()],B=Active[I],C=Active[(I+1)%Active.Num()];
            if(FMath::Abs(Cross2(Polygon[A],Polygon[B],Polygon[C]))<.0001)
            {Active.RemoveAt(I);Removed=true;break;}
        }
    }
    while(Active.Num()>3)
    {
        bool Found=false;
        for(int32 I=0;I<Active.Num();++I)
        {
            const int32 A=Active[(I+Active.Num()-1)%Active.Num()],B=Active[I],C=Active[(I+1)%Active.Num()];
            if(Cross2(Polygon[A],Polygon[B],Polygon[C])<=.0001)continue;
            bool Occupied=false;
            for(const int32 P:Active)
            {
                if(P==A||P==B||P==C)continue;
                if(Cross2(Polygon[A],Polygon[B],Polygon[P])>=-.0001&&
                   Cross2(Polygon[B],Polygon[C],Polygon[P])>=-.0001&&
                   Cross2(Polygon[C],Polygon[A],Polygon[P])>=-.0001){Occupied=true;break;}
            }
            if(Occupied)continue;
            Triangles.Append({A,B,C});Active.RemoveAt(I);Found=true;break;
        }
        if(!Found){Triangles.Reset();if(Error)*Error=TEXT("Boundary triangulation failed; no fallback circle was generated.");return false;}
    }
    if(Active.Num()!=3||Cross2(Polygon[Active[0]],Polygon[Active[1]],Polygon[Active[2]])<=.0001)
    {Triangles.Reset();if(Error)*Error=TEXT("Boundary has a degenerate final triangle.");return false;}
    Triangles.Append(Active);return true;
}

/** Height and expansion are WORLD centimeters, independent of positive XY scale. */
inline bool ContainsPolygonFeet(const TArray<FVector2D>& LocalPolygon,const FTransform& Transform,
    const FVector& WorldFeet,double LocalWaterHeight,double HeightTolerance,double ExtraRadius=0.)
{
    const FVector Scale=Transform.GetScale3D();
    if(LocalPolygon.Num()<3||Transform.ContainsNaN()||WorldFeet.ContainsNaN()||Scale.GetMin()<=0.||
       Transform.GetUnitAxis(EAxis::Z).Z<.999||!std::isfinite(LocalWaterHeight)||
       !std::isfinite(HeightTolerance)||!std::isfinite(ExtraRadius)||HeightTolerance<0.||ExtraRadius<0.)return false;
    const double SurfaceZ=Transform.TransformPosition(FVector(0.,0.,LocalWaterHeight)).Z;
    if(FMath::Abs(WorldFeet.Z-SurfaceZ)>HeightTolerance)return false;
    TArray<FVector2D> WorldPolygon;WorldPolygon.Reserve(LocalPolygon.Num());
    for(const auto& V:LocalPolygon)
    {
        const FVector W=Transform.TransformPosition(FVector(V.X,V.Y,LocalWaterHeight));
        if(W.ContainsNaN())return false;WorldPolygon.Emplace(W.X,W.Y);
    }
    const FVector2D Point(WorldFeet.X,WorldFeet.Y);
    return ContainsPoint(WorldPolygon,Point)||
        (ExtraRadius>0.&&DistanceToBoundary(WorldPolygon,Point)<=ExtraRadius);
}

/** Signed distance is negative inside the water outline. No radial/center assumption. */
inline double BankHeight(double SignedDistance,double WaterHeight,double BedHeight,
    double InnerSlopeWidth,double CrestAboveWater,double CrestDistance,double OuterWidth,double OuterGroundHeight=0.)
{
    if(SignedDistance<=0.)
        return FMath::Lerp(BedHeight,WaterHeight,FMath::Clamp(1.+SignedDistance/InnerSlopeWidth,0.,1.));
    if(SignedDistance<CrestDistance)
        return FMath::Lerp(WaterHeight,WaterHeight+CrestAboveWater,SignedDistance/CrestDistance);
    const double T=FMath::Clamp((SignedDistance-CrestDistance)/(OuterWidth-CrestDistance),0.,1.);
    // Straight slope avoids an unexpectedly steep middle segment on a walkable bank.
    return FMath::Lerp(WaterHeight+CrestAboveWater,OuterGroundHeight,T);
}

inline bool ContainsFeet(const FVector& Center,const FVector& Feet,double RadiusX,double RadiusY,
    double HeightTolerance,double ExtraRadius=0.)
{
    if(Center.ContainsNaN()||Feet.ContainsNaN()||!std::isfinite(RadiusX)||!std::isfinite(RadiusY)||
        !std::isfinite(HeightTolerance)||!std::isfinite(ExtraRadius)||RadiusX<=0.||RadiusY<=0.||
        HeightTolerance<0.||ExtraRadius<0.)return false;
    const double XRadius=RadiusX+ExtraRadius,YRadius=RadiusY+ExtraRadius;
    if(!std::isfinite(XRadius)||!std::isfinite(YRadius))return false;
    const FVector Delta=Feet-Center;
    if(!std::isfinite(Delta.X)||!std::isfinite(Delta.Y)||!std::isfinite(Delta.Z)||
        FMath::Abs(Delta.Z)>HeightTolerance)return false;
    const double X=Delta.X/XRadius,Y=Delta.Y/YRadius;
    return std::isfinite(X)&&std::isfinite(Y)&&X*X+Y*Y<=1.;
}
inline double RefillRequest(double RatePerSecond,double DeltaSeconds,double Reserve)
{
    if(!std::isfinite(RatePerSecond)||!std::isfinite(DeltaSeconds)||!std::isfinite(Reserve)||
        RatePerSecond<=0.||DeltaSeconds<=0.||Reserve<=0.)return 0.;
    return DeltaSeconds>=Reserve/RatePerSecond?Reserve:RatePerSecond*DeltaSeconds;
}
}
