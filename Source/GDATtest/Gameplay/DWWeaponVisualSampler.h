#pragma once
#include "CoreMinimal.h"
#include "DWWeaponAnimationSet.h"

/** Pure presentation values. No Actor, combat plan, authority, clock mutation or particle spawn. */
namespace DWWeaponVisualSampler
{
 struct FRequest
 {
  double SourceSeconds=0.;
  EDWAnimatedWeapon SelectedWeapon=EDWAnimatedWeapon::Spear;
  bool bPresentationActive=false,bActionActive=false,bDead=false;
  bool bFormalBowActive=false,bBowShotCommitted=false;
  bool bHasMesh=false,bHasPrimarySocket=false,bHasSecondarySocket=false;
  FTransform OwnerToWorld=FTransform::Identity;
  // World transforms retained separately to preserve the exact left-associative fallback multiply.
  FTransform PrimarySocketToWorld=FTransform::Identity,SecondarySocketToWorld=FTransform::Identity;
 };
 struct FGeometrySlice
 {
  const TArray<FVector>* A=nullptr;
  const TArray<FVector>* B=nullptr;
  float Alpha=0.f;
 };
 struct FFrame
 {
  FTransform PrimaryToOwner=FTransform::Identity,SecondaryToOwner=FTransform::Identity;
  FDWWeaponVisualSample Sample;
  bool bHeld=false,bHasSample=false,bPrimaryVisible=false,bSecondaryVisible=false;
  bool bPrimaryParticleWindow=false,bSecondaryParticleWindow=false;
  bool bPrimaryDeformedVisible=false,bSecondaryDeformedVisible=false;
  bool bPrimaryStaticVisible=false,bSecondaryStaticVisible=false;
  int32 VisualIndex=INDEX_NONE,GeometryIndex=INDEX_NONE;
  float GeometryAlpha=0.f;
  const FDWWeaponGeometryFrame* Geometry=nullptr;
  const FDWWeaponGeometryFrame* NextGeometry=nullptr;
  FGeometrySlice PrimaryGeometry,SecondaryGeometry;
  // Slices and frame pointers are valid only while the immutable source Set/Clip arrays are stable.
  // Do not retain them across asset edits, Undo/Redo, or context rebinding.
 };
 struct FTrailEndpoints { FVector Root=FVector::ZeroVector,Tip=FVector::ZeroVector; };
 GDATTEST_API FFrame EvaluateWeaponVisualFrame(const UDWWeaponAnimationSet& Set,const FDWWeaponAnimationClip& Clip,const FRequest& Request);
 GDATTEST_API FTrailEndpoints MapTrailEndpoints(const FTransform& WeaponToOwner,const FTransform& SourceWeaponToOwner,const FVector& AuthoredRoot,const FVector& AuthoredTip);
 GDATTEST_API FTransform ResolveTrailAttachment(const FTransform& WeaponToOwner,const FVector& MappedTip,const FTransform& LocalTransform,bool bAtAuthoredTip);
 GDATTEST_API FTransform ResolveTrailAttachment(const FTransform& WeaponToOwner,const FVector& MappedTip,const FVector& Offset,const FRotator& Rotation,const FVector& Scale,bool bAtAuthoredTip);
 GDATTEST_API TArray<FVector> InterpolateVertices(const TArray<FVector>& A,const TArray<FVector>& B,float Alpha);
 GDATTEST_API TArray<FVector> ScaleVerticesAboutPivot(const TArray<FVector>& Vertices,const FVector& Pivot,float Scale);
 // Preview source->animation time only. Runtime presentation-clock methods are never replaced by this.
 GDATTEST_API float AnimationSeconds(double SourceSeconds,float ClipSourceDuration,float AnimationLength);
}
