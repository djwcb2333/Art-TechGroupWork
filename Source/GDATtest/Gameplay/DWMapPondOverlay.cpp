#include "DWMapPondOverlay.h"
#include "DWMapPondGeometry.h"
#include "DWGatheringPond.h"
#include "Engine/World.h"
void UDWMapPondOverlay::Register(ADWGatheringPond* P)
{if(IsValid(P)&&P->GetWorld()==GetWorld()&&Ponds.Num()<256){Ponds.AddUnique(P);bDirty=true;}}
void UDWMapPondOverlay::Unregister(ADWGatheringPond* P)
{Ponds.Remove(P);bDirty=true;}
const TArray<uint8>& UDWMapPondOverlay::Prepare(const FDWMapImageSettings& I,int32 N)
{
 N=FMath::Clamp(N,256,1024);
 if(!GetWorld()||GetWorld()->bIsTearingDown||!I.IsValidCalibration()){Coverage.Reset();return Coverage;}
 if(Grid!=N||!Frame.SameAs(I))
 {Frame=I;Grid=N;Coverage.Reset();PreviousGeometry.Reset();++Revision;bFrameDirty=bDirty=true;}
 const double Now=GetWorld()->GetTimeSeconds();
 if(bDirty||(!bCollecting&&!bRasterizing&&Now>=NextCheck))
 {bDirty=false;bCollecting=true;bRasterizing=false;Probe=0;Signature.Reset();Snapshot.Reset();UVPolygons.Reset();}
 if(bCollecting)
 {
  // At most four ponds / 2048 vertices; no World scan.
  for(int32 Budget=4;Budget>0&&Probe<Ponds.Num();--Budget,++Probe)
  {
   auto* P=Ponds[Probe].Get();if(!IsValid(P)||P->IsActorBeingDestroyed()||P->GetWorld()!=GetWorld())continue;
   auto Polygon=P->GetMapOutlineWorld();if(Polygon.Num()<3||Polygon.Num()>512)continue;
   Signature.Emplace(Polygon.Num(),-1234567.);Signature.Append(Polygon);Snapshot.Add(MoveTemp(Polygon));
  }
  if(Probe<Ponds.Num())return Coverage;
  bCollecting=false;NextCheck=Now+.5;
  if(!bFrameDirty&&Signature==PreviousGeometry)return Coverage;
  PreviousGeometry=MoveTemp(Signature);Work.SetNumZeroed(N*N);
  UVPolygons=MoveTemp(Snapshot);PolygonIndex=0;Row=-1;bRasterizing=true;
 }
 if(bRasterizing)
 {
  // At most eight scanlines: <=4096 edge tests, sorting <=512 hits/row.
  for(int32 Budget=8;Budget>0&&PolygonIndex<UVPolygons.Num();--Budget)
  {
   auto& P=UVPolygons[PolygonIndex];
   if(Row<0)
   {
    bool Valid=true;double MinY=1.;
    for(auto& V:P){FVector2D U;if(!I.WorldToUV(V,U)){Valid=false;break;}V=U;MinY=FMath::Min(MinY,V.Y);}
    if(!Valid){++PolygonIndex;continue;}
    Row=FMath::Clamp(FMath::FloorToInt(FMath::Clamp(MinY,0.,1.)*N),0,N-1);
   }
   double MaxY=0.;for(const auto V:P)MaxY=FMath::Max(MaxY,V.Y);
   if(Row>N-1||Row>FMath::CeilToInt(FMath::Clamp(MaxY,0.,1.)*N)){++PolygonIndex;Row=-1;continue;}
   std::vector<DWMapPondGeometry::Point> Points;Points.reserve(P.Num());
   for(const auto V:P)Points.push_back({V.X,V.Y});
   DWMapPondGeometry::FillRow(Points,N,Row,Work.GetData());
   ++Row;
  }
  if(PolygonIndex>=UVPolygons.Num())
  {Coverage=MoveTemp(Work);UVPolygons.Reset();bRasterizing=bFrameDirty=false;++Revision;NextCheck=Now+.5;}
 }
 return Coverage;
}
void UDWMapPondOverlay::Deinitialize()
{Ponds.Reset();Coverage.Reset();Work.Reset();Snapshot.Reset();UVPolygons.Reset();PreviousGeometry.Reset();Super::Deinitialize();}
