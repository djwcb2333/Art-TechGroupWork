// Candidate only: compile into GDATtest after the scoped declaration in DWGatheringPond.h.
#include "DWGatheringPond.h"
#if WITH_EDITOR
#include "DWGatheringPondMath.h"
#include "DWInteractionPromptComponent.h"
#include "Components/BoxComponent.h"
#include "Components/ChildActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Components/SplineComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "ProceduralMeshComponent.h"
#include <cmath>

bool ADWGatheringPond::CanRebuildBedInEditor(double CandidateBed,FString& Error) const
{
    auto Fail=[&Error](const TCHAR* Text){Error=Text;return false;};
    if(!GetWorld()||GetWorld()->WorldType!=EWorldType::Editor||HasActorBegunPlay()||!bUseSplineBoundary||
       !PondBoundary||!WaterMesh||!PondBank||!WaterArt||!OverlapRange||!DryBed||!WaterSurface||!Prompt||
       !PondBoundary->IsRegistered()||!WaterMesh->IsRegistered()||!PondBank->IsRegistered()||!bBoundaryValid)
        return Fail(TEXT("Only a fully initialized, valid editor spline pond may be adjusted."));
    if(!SlowedActors.IsEmpty()||WaterArt->GetChildActorClass()||WaterArt->GetChildActor()||!OverlapRange->bDrawOnlyIfSelected)
        return Fail(TEXT("Unexpected occupants or legacy child actor: refuse to rebuild or discard existing component state."));
    if(WaterMesh->GetNumSections()!=1||PondBank->GetNumSections()!=1||
       !WaterMesh->GetProcMeshSection(0)||!PondBank->GetProcMeshSection(0))
        return Fail(TEXT("Unexpected generated mesh sections; preserve them and inspect manually."));
    if(!std::isfinite(WaterHeightCm)||WaterHeightCm<=0.||WaterHeightCm>1000.||
       !std::isfinite(CandidateBed)||CandidateBed< -10000.||CandidateBed>=WaterHeightCm||
       !std::isfinite(OuterGroundHeightCm)||OuterGroundHeightCm< -10000.||
       OuterGroundHeightCm>WaterHeightCm+BankCrestAboveWaterCm||
       !std::isfinite(InnerSlopeWidthCm)||InnerSlopeWidthCm<=0.||InnerSlopeWidthCm>10000.||
       !std::isfinite(BankCrestAboveWaterCm)||BankCrestAboveWaterCm<=0.||BankCrestAboveWaterCm>1000.||
       !std::isfinite(BankCrestDistanceCm)||BankCrestDistanceCm<=0.||
       !std::isfinite(OuterBankWidthCm)||OuterBankWidthCm<=BankCrestDistanceCm||OuterBankWidthCm>10000.||
       !std::isfinite(BankGridSpacingCm)||BankGridSpacingCm<5.||BankGridSpacingCm>1000.||
       !std::isfinite(MaterialTilingCm)||MaterialTilingCm<=0.||MaterialTilingCm>100000.||
       BoundarySamplesPerSegment<1||BoundarySamplesPerSegment>32)
        return Fail(TEXT("Invalid height, bank, tiling or sample parameters."));
    const FVector Scale=GetActorScale3D();
    if(GetActorTransform().ContainsNaN()||Scale.ContainsNaN()||Scale.GetMin()<=0.||Scale.GetMax()>100.||
       GetActorUpVector().Z<=.999||!FMath::IsNearlyEqual(PondBoundary->GetRelativeLocation().Z,WaterHeightCm))
        return Fail(TEXT("Invalid transform or spline component Z. Resolve existing pond configuration before applying depth."));
    if(!PondBoundary->IsClosedLoop())return Fail(TEXT("Spline boundary must be closed."));
    const int32 Controls=PondBoundary->GetNumberOfSplinePoints();
    if(Controls<3||Controls>128||Controls*BoundarySamplesPerSegment>512)
        return Fail(TEXT("Invalid control or boundary sample count."));
    const FTransform SplineToActor=PondBoundary->GetComponentTransform().GetRelativeTransform(GetActorTransform());
    if(SplineToActor.ContainsNaN())return Fail(TEXT("Invalid spline component transform."));
    TArray<FVector2D> Polygon;Polygon.Reserve(Controls*BoundarySamplesPerSegment);
    for(int32 I=0;I<Controls;++I)for(int32 J=0;J<BoundarySamplesPerSegment;++J)
    {
        const float Key=static_cast<float>(I)+static_cast<float>(J)/BoundarySamplesPerSegment;
        const FVector Point=SplineToActor.TransformPosition(PondBoundary->GetLocationAtSplineInputKey(Key,ESplineCoordinateSpace::Local));
        Polygon.Emplace(Point.X,Point.Y);
    }
    TArray<int32> Triangles;
    if(!DWGatheringPondMath::TriangulatePolygon(Polygon,Triangles,&Error))return false;
    FBox2D Bounds(ForceInit);for(const auto& Point:Polygon)Bounds+=Point;
    const FVector2D Min=Bounds.Min-FVector2D(OuterBankWidthCm+BankGridSpacingCm);
    const FVector2D Max=Bounds.Max+FVector2D(OuterBankWidthCm+BankGridSpacingCm);
    const int32 X=FMath::CeilToInt((Max.X-Min.X)/BankGridSpacingCm),Y=FMath::CeilToInt((Max.Y-Min.Y)/BankGridSpacingCm);
    if(X<1||Y<1||X>192||Y>192)return Fail(TEXT("Bank grid exceeds its 192-cell limit."));
    Error.Reset();return true;
}

bool ADWGatheringPond::TrySetBedHeightInEditor(double CandidateBed,FString& Error)
{
    FString OriginalError;
    // Validate both possible geometries before touching any serialized or component state.
    if(!CanRebuildBedInEditor(BedHeightCm,OriginalError))
    {Error=TEXT("Original pond cannot be rebuilt safely: ")+OriginalError;return false;}
    if(!CanRebuildBedInEditor(CandidateBed,Error))return false;

    const double OldBed=BedHeightCm;
    const auto OldPolygon=BoundaryPolygon;const FTransform OldRangeTransform=LastRangeTransform;
    const bool OldBoundaryValid=bBoundaryValid,OldDry=bIsDry,OldAppliedDry=bAppliedDryState,OldAppliedSurface=bAppliedSurfaceEnabled;
    const int32 OldCount=BoundarySampleCount;const FString OldMessage=BoundaryValidationMessage;
    const FProcMeshSection OldWater=*WaterMesh->GetProcMeshSection(0),OldBank=*PondBank->GetProcMeshSection(0);
    const FSplineCurves OldCurves=PondBoundary->GetSplineCurves();
    UMaterialInterface* OldWaterMaterial=WaterMesh->GetMaterial(0);UMaterialInterface* OldBankMaterial=PondBank->GetMaterial(0);
    const FVector OldBoxExtent=OverlapRange->GetUnscaledBoxExtent();
    const float OldPromptRadius=Prompt->GenericDetectionRadius,OldPromptHeight=Prompt->HeightTolerance;
    const bool OldPromptEnabled=Prompt->bPromptEnabled;
    struct FSnapshot
    {
        USceneComponent* Component=nullptr;FTransform Relative;bool Visible=false,Hidden=false,Overlap=false;
        ECollisionEnabled::Type Collision=ECollisionEnabled::NoCollision;
    };
    TArray<FSnapshot> Components;
    for(USceneComponent* Component:{static_cast<USceneComponent*>(PondBoundary.Get()),static_cast<USceneComponent*>(WaterMesh.Get()),
        static_cast<USceneComponent*>(PondBank.Get()),static_cast<USceneComponent*>(DryBed.Get()),static_cast<USceneComponent*>(WaterSurface.Get()),
        static_cast<USceneComponent*>(OverlapRange.Get()),static_cast<USceneComponent*>(WaterArt.Get())})
    {
        FSnapshot S;S.Component=Component;S.Relative=Component->GetRelativeTransform();S.Visible=Component->IsVisible();S.Hidden=Component->bHiddenInGame;
        if(const auto* P=Cast<UPrimitiveComponent>(Component)){S.Collision=P->GetCollisionEnabled();S.Overlap=P->GetGenerateOverlapEvents();}
        Components.Add(S);
    }
    BedHeightCm=CandidateBed;RebuildPondGeometry();
    bool SameControls=PondBoundary->GetSplineCurves()==OldCurves&&OverlapRange->GetUnscaledBoxExtent().Equals(OldBoxExtent,.001)&&
        WaterMesh->GetMaterial(0)==OldWaterMaterial&&PondBank->GetMaterial(0)==OldBankMaterial&&
        Prompt->GenericDetectionRadius==OldPromptRadius&&Prompt->HeightTolerance==OldPromptHeight&&Prompt->bPromptEnabled==OldPromptEnabled;
    for(const auto& S:Components)
    {
        SameControls=SameControls&&S.Component->GetRelativeTransform().Equals(S.Relative,.001)&&
            S.Component->IsVisible()==S.Visible&&bool(S.Component->bHiddenInGame)==S.Hidden;
        if(const auto* P=Cast<UPrimitiveComponent>(S.Component))
            SameControls=SameControls&&P->GetCollisionEnabled()==S.Collision&&P->GetGenerateOverlapEvents()==S.Overlap;
    }
    if(bBoundaryValid&&SameControls){Error.Reset();return true;}

    const FString Failure=bBoundaryValid?TEXT("Geometry rebuild changed protected spline or component data."):BoundaryValidationMessage;
    // The user-owned component state is restored from the captured copy, not by
    // calling destructive Rebuild a second time. All scene mutations remain inside the transaction.
    BedHeightCm=OldBed;PondBoundary->SetSpline(OldCurves);BoundaryPolygon=OldPolygon;LastRangeTransform=OldRangeTransform;
    bBoundaryValid=OldBoundaryValid;bIsDry=OldDry;bAppliedDryState=OldAppliedDry;bAppliedSurfaceEnabled=OldAppliedSurface;
    BoundarySampleCount=OldCount;BoundaryValidationMessage=OldMessage;
    WaterMesh->ClearAllMeshSections();PondBank->ClearAllMeshSections();
    WaterMesh->SetProcMeshSection(0,OldWater);PondBank->SetProcMeshSection(0,OldBank);
    WaterMesh->SetMaterial(0,OldWaterMaterial);PondBank->SetMaterial(0,OldBankMaterial);
    OverlapRange->SetBoxExtent(OldBoxExtent,false);
    for(const auto& S:Components)
    {
        S.Component->SetRelativeTransform(S.Relative);S.Component->SetVisibility(S.Visible,false);S.Component->SetHiddenInGame(S.Hidden,false);
        if(auto* P=Cast<UPrimitiveComponent>(S.Component)){P->SetCollisionEnabled(S.Collision);P->SetGenerateOverlapEvents(S.Overlap);}
    }
    Prompt->GenericDetectionRadius=OldPromptRadius;Prompt->HeightTolerance=OldPromptHeight;Prompt->bPromptEnabled=OldPromptEnabled;
    Error=TEXT("Pond rebuild failed; previous bed, generated sections, component transforms, collision and visibility were restored: ")+Failure;
    return false;
}
#endif
