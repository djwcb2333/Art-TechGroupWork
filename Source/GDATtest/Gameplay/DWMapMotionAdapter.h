#pragma once
#include "CoreMinimal.h"
#include "DWMapMotionMath.h"

class UDWMinimapComponent;
class UDWWorldMapSubsystem;
class UMaterialInstanceDynamic;

/** Native-only calculations; never owns an Actor, material, pause, save or widget. */
namespace DWMapMotionAdapter
{
struct FFrame
{
    DWMapMotionMath::View View;
    double OrthoWidth=0.;
    FVector2D MaterialUVScale=FVector2D::ZeroVector;
};

/** Screen bounds are player-local Slate units, not desktop/physical pixels. */
GDATTEST_API bool SnapshotMinimap(const UDWMinimapComponent* Mini,FVector2D TopLeft,FVector2D Size,FFrame& Out,FString& Error);
/** Requires the small read-only GetMotionEndView addition in WorldMapMotion_ReadOnlyAccessor.patch. */
GDATTEST_API bool SnapshotFullMap(const UDWWorldMapSubsystem* Map,float PlayerZ,FVector2D TopLeft,FVector2D Size,FFrame& Out,FString& Error);
/** Pass the same eased progress used by the UI's mask/rectangle; do not ease it again. */
GDATTEST_API bool Interpolate(const FFrame& Start,const FFrame& End,FVector2D TopLeft,FVector2D Size,double Eased,FFrame& Out);
/** Returns player-local Slate coordinates; drawing child geometry must convert once. */
GDATTEST_API bool WorldToSlate(const FFrame& Frame,FVector World,FVector2D& Out);
/** Map the live mini depth/parameters into an existing, independent WorldMap MID.
 *  Does not capture, create assets, save, or add a circle/ring; UI owns clipping.
 *  Caller must strongly retain Destination and the referenced live mini while in use.
 */
GDATTEST_API bool CopyMiniSnapshotToWorldMaterial(const UDWMinimapComponent* Mini,UMaterialInstanceDynamic* Destination,FString& Error);
/** clip=(x,y,w) comes from the actual local player's view-projection matrix. */
GDATTEST_API DWMapMotionMath::Target ProjectClip(double X,double Y,double W,FVector2D ViewOrigin,FVector2D ViewSize,
    FVector2D SafeOrigin,FVector2D SafeSize,FVector2D PreviousDirection);
/** For orthographic or custom projection use explicit signed camera-forward depth. */
GDATTEST_API DWMapMotionMath::Target ProjectClipWithCameraDepth(double X,double Y,double W,double CameraDepth,
    FVector2D ViewOrigin,FVector2D ViewSize,FVector2D SafeOrigin,FVector2D SafeSize,FVector2D PreviousDirection);
}
