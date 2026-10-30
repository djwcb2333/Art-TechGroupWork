#include "DWGatheringPond.h"
#include "DWGatheringPondMath.h"
#include "DWPondEdgeGeometry.h"
#include "ProceduralMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include <cmath>

void ADWGatheringPond::RefreshEdgeFoamVisibility()
{
    if(!EdgeFoamMesh)return;
    const bool Visible=bEnableEdgeFoam&&!bIsDry&&(!bUseSplineBoundary||(bBoundaryValid&&IsConfigurationValid()))&&EdgeFoamMesh->GetNumSections()>0;
    EdgeFoamMesh->SetVisibility(Visible);EdgeFoamMesh->SetHiddenInGame(!Visible);
}

void ADWGatheringPond::RebuildEdgeFoam()
{
    if(!EdgeFoamMesh)return;
    EdgeFoamMesh->ClearAllMeshSections();EdgeFoamMID=nullptr;
    auto Finish=[this](){RefreshEdgeFoamVisibility();};
    if(!bEnableEdgeFoam||!EdgeFoamMaterial||!std::isfinite(EdgeFoamWidthCm)||EdgeFoamWidthCm<=0.||!std::isfinite(EdgeFoamGridSpacingCm)||EdgeFoamGridSpacingCm<=0.||!std::isfinite(EdgeFoamUVScaleCm)||EdgeFoamUVScaleCm<=0.||!std::isfinite(EdgeFoamLiftCm)||EdgeFoamLiftCm<0.||EdgeFoamLiftCm>5.||GetActorTransform().ContainsNaN()||GetActorScale3D().GetMin()<=0.||GetActorUpVector().Z<=.999){Finish();return;}
    TArray<FVector2D> Polygon=BoundaryPolygon;double Z=WaterHeightCm;
    if(!bUseSplineBoundary)
    {
        if(!std::isfinite(RadiusCm)||RadiusCm<=0.){Finish();return;}
        Polygon.Reset();for(int32 I=0;I<64;++I){const double A=2.*PI*I/64.;Polygon.Emplace(RadiusCm*FMath::Cos(A),RadiusCm*FMath::Sin(A));}Z=2.;
    }
    else if(!bBoundaryValid){Finish();return;}
    TArray<int32> Triangles;FString Error;if(!DWGatheringPondMath::TriangulatePolygon(Polygon,Triangles,&Error)){Finish();return;}
    using namespace DWPondEdgeGeometry;
    std::vector<P> Points;for(const auto& V:Polygon)Points.push_back({V.X,V.Y});
    std::vector<Tri> Water;for(int32 I=0;I<Triangles.Num();I+=3)Water.push_back({Points[Triangles[I]],Points[Triangles[I+1]],Points[Triangles[I+2]]});
    std::vector<Face> Faces;
    if(!Build(Points,Water,EdgeFoamWidthCm,EdgeFoamGridSpacingCm,FMath::Clamp(EdgeFoamMaxGridCells,8,192),static_cast<size_t>(FMath::Clamp(EdgeFoamMaxTriangles,64,32768)),Faces)){Finish();return;}
    TArray<FVector> Vertices,Normals;TArray<FVector2D> UV;TArray<FLinearColor> Colors;TArray<FProcMeshTangent> Tangents;TArray<int32> Indices;
    const int32 Count=static_cast<int32>(Faces.size())*3;Vertices.Reserve(Count);Normals.Reserve(Count);UV.Reserve(Count);Colors.Reserve(Count);Tangents.Reserve(Count);Indices.Reserve(Count);
    for(const auto& Face:Faces)
    {
        const int32 Base=Vertices.Num();
        for(const auto& V:Face){Vertices.Emplace(V.Position.X,V.Position.Y,Z+EdgeFoamLiftCm);Normals.Add(FVector::UpVector);UV.Emplace(V.Position.X/EdgeFoamUVScaleCm,-V.Distance/EdgeFoamWidthCm);Colors.Add(FLinearColor::White);Tangents.Emplace(1.f,0.f,0.f);}
        Indices.Append({Base,Base+2,Base+1});
    }
    EdgeFoamMesh->CreateMeshSection_LinearColor(0,Vertices,Indices,Normals,UV,Colors,Tangents,false);
    EdgeFoamMID=UMaterialInstanceDynamic::Create(EdgeFoamMaterial,this);
    if(!EdgeFoamMID){EdgeFoamMesh->ClearAllMeshSections();Finish();return;}
    const float Opacity=FMath::IsFinite(EdgeFoamOpacity)?FMath::Clamp(EdgeFoamOpacity,0.f,1.f):.85f;
    const float Speed=FMath::IsFinite(EdgeFoamSpeed)?FMath::Clamp(EdgeFoamSpeed,-4.f,4.f):-.4f;
    const float Noise=FMath::IsFinite(EdgeFoamNoiseSize)?FMath::Clamp(EdgeFoamNoiseSize,.01f,100.f):1.f;
    const bool FiniteTint=FMath::IsFinite(EdgeFoamColor.R)&&FMath::IsFinite(EdgeFoamColor.G)&&FMath::IsFinite(EdgeFoamColor.B)&&FMath::IsFinite(EdgeFoamColor.A);
    const FLinearColor Tint=FiniteTint?EdgeFoamColor:FLinearColor::White;
    EdgeFoamMID->SetVectorParameterValue(TEXT("Foam Color"),Tint);
    EdgeFoamMID->SetScalarParameterValue(TEXT("Opacity"),Opacity);
    EdgeFoamMID->SetScalarParameterValue(TEXT("Fall Speed"),Speed);
    EdgeFoamMID->SetScalarParameterValue(TEXT("Noise Size"),Noise);
    EdgeFoamMesh->SetMaterial(0,EdgeFoamMID);
    const float Cull=FMath::IsFinite(EdgeFoamCullDistanceCm)?FMath::Clamp(EdgeFoamCullDistanceCm,0.f,100000.f):6000.f;
    EdgeFoamMesh->SetCullDistance(Cull);Finish();
}
