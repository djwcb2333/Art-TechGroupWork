#pragma once
#include "CoreMinimal.h"
#include "ProceduralMeshComponent.h"

namespace DWWeaponGeometry
{
    /** Same face tangent and position-weld smoothing rules as the engine helper, with spatially indexed overlap lookup. */
    void CalculateTangents(const TArray<FVector>& Vertices,const TArray<int32>& Triangles,const TArray<FVector2D>& UVs,
        TArray<FVector>& Normals,TArray<FProcMeshTangent>& Tangents,uint64* OutOverlapComparisons=nullptr);
}

/** Per-component upload cache. Exact comparisons preserve sub-frame deformation without quantizing animation time. */
struct FDWWeaponGeometryCache
{
    TArray<FVector> Vertices;
    TArray<int32> Triangles;
    TArray<FVector2D> SourceUVs;
};

/** Read-only native diagnostics for regression tests and profiling; no gameplay tuning. */
struct FDWWeaponGeometryStats
{
    uint64 TangentBuilds=0;
    uint64 MeshUploads=0;
    uint64 UnchangedSkips=0;
    uint64 OverlapComparisons=0;
};
