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
    if(!IsValid(Mini)||!Mini->IsMinimapVisible()||!Mini->GetOwner()||!Mini->GetOwner()->GetWorld()||!Mini->ImageMap.IsValidCalibration())
    {Error=TEXT("The live minimap image/view is unavailable.");return false;}
    const auto& Projection=Mini->GetProjection();const FVector P=Mini->GetPlayerLocation();DWMapMotionMath::View V;
    if(Size.X<=0.||!FMath::IsFinite(Size.Y)||FMath::Abs(Size.X-Size.Y)>.01*FMath::Max(Size.X,Size.Y)||
       !DWMapMotionMath::Make(Point(P),Rect(TopLeft,Size),double(Projection.Width)/Size.X,Mini->GetCaptureYaw(),-90.,V)||!MakeFrame(V,Out))
    {Error=TEXT("The minimap image projection or onscreen geometry is invalid.");return false;}
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
bool CopyMiniSnapshotToWorldMaterial(const UDWMinimapComponent*,UMaterialInstanceDynamic*,FString& Error)
{Error=TEXT("Legacy depth/material snapshots are disabled; use the shared calibrated map image.");return false;}

DWMapMotionMath::Target ProjectClip(double X,double Y,double W,FVector2D ViewOrigin,FVector2D ViewSize,
    FVector2D SafeOrigin,FVector2D SafeSize,FVector2D PreviousDirection)
{return DWMapMotionMath::ProjectTarget(X,Y,W,Rect(ViewOrigin,ViewSize),Rect(SafeOrigin,SafeSize),{PreviousDirection.X,PreviousDirection.Y});}
DWMapMotionMath::Target ProjectClipWithCameraDepth(double X,double Y,double W,double CameraDepth,
    FVector2D ViewOrigin,FVector2D ViewSize,FVector2D SafeOrigin,FVector2D SafeSize,FVector2D PreviousDirection)
{return DWMapMotionMath::ProjectTarget(X,Y,W,Rect(ViewOrigin,ViewSize),Rect(SafeOrigin,SafeSize),{PreviousDirection.X,PreviousDirection.Y},true,CameraDepth);}
}
