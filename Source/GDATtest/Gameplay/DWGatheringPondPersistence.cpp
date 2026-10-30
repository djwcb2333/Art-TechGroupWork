#include "DWGatheringPondPersistence.h"
#include "DWGatheringPond.h"
#include "Components/SplineComponent.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include <cmath>

bool FDWGatheringPondPersistence::Validate(const FDWGatheringPondSave& S,FString& Error)
{
    const FString Id=S.Id.ToString();
    if(S.Id.IsNone()||Id.Len()>160||Id.Contains(TEXT("UEDPIE_"))||Id.Contains(TEXT("/"))||Id.Contains(TEXT(":"))||
       S.AuthoredPath.IsEmpty()||S.AuthoredPath.Contains(TEXT("UEDPIE_"))||S.ClassPath.IsEmpty()||
       !S.Transform.IsValid()||!S.SplineTransform.IsValid()||!S.bClosed||S.Points.Num()<3||S.Points.Num()>128||
       !std::isfinite(S.InitialReserve)||S.InitialReserve<0.||!std::isfinite(S.RemainingWater)||
       S.RemainingWater<0.||S.RemainingWater>S.InitialReserve||!std::isfinite(S.Radius)||S.Radius<=0.||S.Radius>100000.||!std::isfinite(S.WaterHeight)||
       !std::isfinite(S.BedHeight)||!std::isfinite(S.OuterGround)||!std::isfinite(S.InnerSlope)||
       !std::isfinite(S.CrestAbove)||!std::isfinite(S.CrestDistance)||!std::isfinite(S.OuterWidth)||
       !std::isfinite(S.GridSpacing)||!std::isfinite(S.Tiling)||!std::isfinite(S.RefillRate)||
       !std::isfinite(S.SlowFraction)||!std::isfinite(S.SurfaceTolerance)||!std::isfinite(S.StationExtraRadius)||
       S.WaterHeight<=0.||S.WaterHeight>1000.||S.BedHeight< -10000.||S.BedHeight>=S.WaterHeight||
       S.OuterGround< -10000.||S.OuterGround>S.WaterHeight+S.CrestAbove||S.InnerSlope<=0.||S.InnerSlope>10000.||
       S.CrestAbove<=0.||S.CrestAbove>1000.||S.CrestDistance<=0.||S.OuterWidth<=S.CrestDistance||S.OuterWidth>10000.||
       S.GridSpacing<5.||S.GridSpacing>1000.||S.Tiling<=0.||S.Tiling>100000.||S.Samples<1||S.Samples>32||
       S.Points.Num()*S.Samples>512||S.RefillRate<0.||S.SlowFraction<0.||S.SlowFraction>1.||
       S.SurfaceTolerance<0.||S.SurfaceTolerance>10000.||S.StationExtraRadius<0.||S.StationExtraRadius>100000.)
    {Error=TEXT("Invalid authored pond identity, geometry or water facts; save remains protected.");return false;}
    for(const auto& P:S.Points)if(P.Position.ContainsNaN()||P.Arrive.ContainsNaN()||P.Leave.ContainsNaN()||
        P.Rotation.ContainsNaN()||P.Scale.ContainsNaN()||P.Type>4)
    {Error=TEXT("Invalid saved pond spline point.");return false;}
    Error.Reset();return true;
}
bool FDWGatheringPondPersistence::Capture(const ADWGatheringPond& P,FDWGatheringPondSave& Out,FString& Error)
{
    if(!IsValid(&P)||P.IsActorBeingDestroyed()||!P.GetWorld()||P.GetWorld()->bIsTearingDown||!P.HasActorBegunPlay()||
       !P.bAuthoredPersistence||!P.bUseSplineBoundary||!P.bBoundaryValid||!P.CanPersistPondConfiguration()||!P.PondBoundary)
    {Error=TEXT("Pond is not a ready authored spline instance.");return false;}
    FDWGatheringPondSave S;S.Id=P.PersistentId;
    S.AuthoredPath=UWorld::RemovePIEPrefix(P.GetPathName());S.ClassPath=P.GetClass()->GetPathName();
    S.Transform=P.GetActorTransform();S.SplineTransform=P.PondBoundary->GetRelativeTransform();
    S.InitialReserve=P.InitialReserve;S.RemainingWater=P.RemainingWater;S.Radius=P.RadiusCm;S.WaterHeight=P.WaterHeightCm;S.BedHeight=P.BedHeightCm;
    S.OuterGround=P.OuterGroundHeightCm;S.InnerSlope=P.InnerSlopeWidthCm;S.CrestAbove=P.BankCrestAboveWaterCm;
    S.CrestDistance=P.BankCrestDistanceCm;S.OuterWidth=P.OuterBankWidthCm;S.GridSpacing=P.BankGridSpacingCm;S.Tiling=P.MaterialTilingCm;
    S.RefillRate=P.RefillPerSecond;S.SlowFraction=P.SlowFraction;S.SurfaceTolerance=P.SurfaceHeightToleranceCm;
    S.StationExtraRadius=P.StationExtraRadiusCm;S.Samples=P.BoundarySamplesPerSegment;
    S.WaterMaterial=FSoftObjectPath(P.WaterMaterial.Get());S.BankMaterial=FSoftObjectPath(P.BankMaterial.Get());
    S.bClosed=P.PondBoundary->IsClosedLoop();
    const int32 Count=P.PondBoundary->GetNumberOfSplinePoints();
    if(Count<3||Count>128){Error=TEXT("Invalid authored pond spline count.");return false;}
    for(int32 I=0;I<Count;++I)
    {
        FDWGatheringPondPointSave X;
        X.Position=P.PondBoundary->GetLocationAtSplinePoint(I,ESplineCoordinateSpace::Local);
        X.Arrive=P.PondBoundary->GetArriveTangentAtSplinePoint(I,ESplineCoordinateSpace::Local);
        X.Leave=P.PondBoundary->GetLeaveTangentAtSplinePoint(I,ESplineCoordinateSpace::Local);
        X.Rotation=P.PondBoundary->GetRotationAtSplinePoint(I,ESplineCoordinateSpace::Local);
        X.Scale=P.PondBoundary->GetScaleAtSplinePoint(I);X.Type=uint8(P.PondBoundary->GetSplinePointType(I));S.Points.Add(X);
    }
    if(!Validate(S,Error))return false;Out=MoveTemp(S);return true;
}
bool FDWGatheringPondPersistence::MatchAuthored(const ADWGatheringPond& P,const FDWGatheringPondSave& Saved,FString& Error)
{
    FDWGatheringPondSave Current;if(!Capture(P,Current,Error)||!Validate(Saved,Error))return false;
    Current.RemainingWater=Saved.RemainingWater;
    if(!FDWGatheringPondSave::StaticStruct()->CompareScriptStruct(&Current,&Saved,PPF_None))
    {Error=TEXT("Saved pond identity, shape, transform, capacity or configuration changed.");return false;}
    Error.Reset();return true;
}
