#include "DWMapMotionAdapter.h"
#include "DWMinimap.h"
#include "DWWorldMap.h"
#include "Engine/TextureRenderTarget2D.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace DWMapMotionAdapter
{
static DWMapMotionMath::Rect Rect(FVector2D O,FVector2D S){return {O.X,O.Y,S.X,S.Y};}
static DWMapMotionMath::P3 Point(FVector V){return {V.X,V.Y,V.Z};}
static bool MakeFrame(const DWMapMotionMath::View& View,FFrame& Out)
{
    double Width=0.;DWMapMotionMath::P2 Scale;
    if(!DWMapMotionMath::CaptureScale(View,Width,Scale))return false;
    FFrame Next;Next.View=View;Next.OrthoWidth=Width;Next.MaterialUVScale=FVector2D(Scale.X,Scale.Y);Out=Next;return true;
}
bool SnapshotMinimap(const UDWMinimapComponent* Mini,FVector2D TopLeft,FVector2D Size,FFrame& Out,FString& Error)
{
    if(!IsValid(Mini)||!Mini->IsMinimapVisible()||!IsValid(Mini->GetDepthRenderTarget())||!IsValid(Mini->GetDisplayMaterial())||
       !Mini->GetOwner()||!Mini->GetOwner()->GetWorld())
    {Error=TEXT("The live minimap or its captured frame is unavailable.");return false;}
    const auto& Captured=Mini->GetProjection();const FVector P=Mini->GetPlayerLocation();
    const FLinearColor TextureOffset=Mini->GetDisplayMaterial()->K2_GetVectorParameterValue(TEXT("PlayerOffset"));
    const FVector2D MarkerOffset=Captured.Project(P);
    if(!FMath::IsFinite(TextureOffset.R)||!FMath::IsFinite(TextureOffset.G)||!FMath::IsFinite(Size.X)||
       !FMath::IsFinite(MarkerOffset.X)||!FMath::IsFinite(MarkerOffset.Y)||
       FMath::Abs(TextureOffset.R-MarkerOffset.X)*Size.X>.5||FMath::Abs(TextureOffset.G-MarkerOffset.Y)*Size.X>.5)
    {Error=TEXT("The displayed minimap texture and marker frame are not synchronized.");return false;}
    const double Pitch=FMath::RadiansToDegrees(FMath::Atan2(-FMath::Sqrt(Captured.Up.X*Captured.Up.X+Captured.Up.Y*Captured.Up.Y),Captured.Up.Z));
    DWMapMotionMath::View V;
    if(Size.X<=0.||!FMath::IsFinite(Size.Y)||FMath::Abs(Size.X-Size.Y)>.01*FMath::Max(Size.X,Size.Y)||
       !DWMapMotionMath::Make(Point(P),Rect(TopLeft,Size),double(Captured.Width)/Size.X,Mini->GetCaptureYaw(),Pitch,V))
    {Error=TEXT("The minimap projection or onscreen geometry is invalid.");return false;}
    // The captured basis, not a newly edited pitch property, is authoritative.
    const FVector ExpectedRight(V.Right.X,V.Right.Y,V.Right.Z),ExpectedUp(V.Up.X,V.Up.Y,V.Up.Z);
    if(!Captured.Right.Equals(ExpectedRight,0.001)||!Captured.Up.Equals(ExpectedUp,0.001)||
       !FMath::IsNearlyEqual(double(Captured.NorthScale),V.NorthScale,.001)||!MakeFrame(V,Out))
    {Error=TEXT("The captured minimap basis cannot be safely reconstructed.");return false;}
    Error.Reset();return true;
}
bool SnapshotFullMap(const UDWWorldMapSubsystem* Map,float PlayerZ,FVector2D TopLeft,FVector2D Size,FFrame& Out,FString& Error)
{
    if(!IsValid(Map)||!Map->IsMapOpen()||!FMath::IsFinite(PlayerZ))
    {Error=TEXT("The current full map is unavailable.");return false;}
    const auto& Existing=Map->GetMotionEndView();double Units=0.;DWMapMotionMath::View V;
    if(!DWWorldMapMath::Valid(Existing)||!DWMapMotionMath::FullMapUnits(Existing.CaptureWidth,Rect(TopLeft,Size),Existing.Zoom,Units)||
       !DWMapMotionMath::Make({Existing.Center.X,Existing.Center.Y,PlayerZ},Rect(TopLeft,Size),Units,Existing.NorthYaw,-90.,V)||!MakeFrame(V,Out))
    {Error=TEXT("The full-map viewport is invalid.");return false;}
    Error.Reset();return true;
}
bool Interpolate(const FFrame& Start,const FFrame& End,FVector2D TopLeft,FVector2D Size,double Eased,FFrame& Out)
{DWMapMotionMath::View V;return DWMapMotionMath::Interpolate(Start.View,End.View,Rect(TopLeft,Size),Eased,V)&&MakeFrame(V,Out);}
bool WorldToSlate(const FFrame& Frame,FVector World,FVector2D& Out)
{DWMapMotionMath::P2 P;if(!DWMapMotionMath::WorldToScreen(Frame.View,Point(World),P))return false;Out=FVector2D(P.X,P.Y);return true;}
bool CopyMiniSnapshotToWorldMaterial(const UDWMinimapComponent* Mini,UMaterialInstanceDynamic* Destination,FString& Error)
{
    if(!IsValid(Mini)||!Mini->IsMinimapVisible()||!IsValid(Destination)||!IsValid(Mini->GetDisplayMaterial())||
       !IsValid(Mini->GetDepthRenderTarget())||Destination==Mini->GetDisplayMaterial())
    {Error=TEXT("The live minimap depth or independent transition material is unavailable.");return false;}
    auto* Source=Mini->GetDisplayMaterial();
    const FLinearColor Offset=Source->K2_GetVectorParameterValue(TEXT("PlayerOffset"));
    const FLinearColor Info=Source->K2_GetVectorParameterValue(TEXT("CaptureInfo"));
    const FName Keys[]={TEXT("CaptureRight"),TEXT("CaptureUp"),TEXT("CaptureForward"),TEXT("CaptureInfo"),
        TEXT("GroundTint"),TEXT("SurfaceTint"),TEXT("WallTint")};
    FLinearColor Values[UE_ARRAY_COUNT(Keys)];
    for(int32 I=0;I<UE_ARRAY_COUNT(Keys);++I)
    {
        Values[I]=Source->K2_GetVectorParameterValue(Keys[I]);const auto& C=Values[I];
        if(!FMath::IsFinite(C.R)||!FMath::IsFinite(C.G)||!FMath::IsFinite(C.B)||!FMath::IsFinite(C.A))
        {Error=TEXT("The minimap's material frame contains an invalid value.");return false;}
    }
    const float Edge=Source->K2_GetScalarParameterValue(TEXT("EdgeSamples"));
    DWMapMotionMath::P2 Scale,Shift;
    if(!FMath::IsFinite(Edge)||Info.R<=0.f||!DWMapMotionMath::MiniMaterialMapping(Info.G,{Offset.R,Offset.G},Scale,Shift))
    {Error=TEXT("The minimap's shader mapping is invalid.");return false;}
    // Commit only after validating the entire source snapshot.
    Destination->SetTextureParameterValue(TEXT("WorldMapDepth"),Mini->GetDepthRenderTarget());
    Destination->SetVectorParameterValue(TEXT("WorldMapView"),FLinearColor(Scale.X,Scale.Y,Shift.X,Shift.Y));
    for(int32 I=0;I<UE_ARRAY_COUNT(Keys);++I)Destination->SetVectorParameterValue(Keys[I],Values[I]);
    Destination->SetScalarParameterValue(TEXT("EdgeSamples"),Edge);
    Error.Reset();return true;
}
DWMapMotionMath::Target ProjectClip(double X,double Y,double W,FVector2D ViewOrigin,FVector2D ViewSize,
    FVector2D SafeOrigin,FVector2D SafeSize,FVector2D PreviousDirection)
{return DWMapMotionMath::ProjectTarget(X,Y,W,Rect(ViewOrigin,ViewSize),Rect(SafeOrigin,SafeSize),{PreviousDirection.X,PreviousDirection.Y});}
DWMapMotionMath::Target ProjectClipWithCameraDepth(double X,double Y,double W,double CameraDepth,
    FVector2D ViewOrigin,FVector2D ViewSize,FVector2D SafeOrigin,FVector2D SafeSize,FVector2D PreviousDirection)
{return DWMapMotionMath::ProjectTarget(X,Y,W,Rect(ViewOrigin,ViewSize),Rect(SafeOrigin,SafeSize),{PreviousDirection.X,PreviousDirection.Y},true,CameraDepth);}
}
