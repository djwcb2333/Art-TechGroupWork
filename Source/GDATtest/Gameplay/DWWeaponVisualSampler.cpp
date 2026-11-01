#include "DWWeaponVisualSampler.h"

namespace DWWeaponVisualSampler
{
 namespace
 {
  float Positive(float Value,float Fallback=.001f){return FMath::IsFinite(Value)&&Value>0.f?Value:Fallback;}
  template<typename T> int32 SampleIndex(const TArray<T>& Samples,double Time)
  {
   int32 Result=0;for(int32 I=1;I<Samples.Num()&&Samples[I].TimeSeconds<=Time;++I)Result=I;return Result;
  }
 }
 TArray<FVector> InterpolateVertices(const TArray<FVector>& A,const TArray<FVector>& B,float Alpha)
 {
  if(A.Num()!=B.Num()||Alpha<=0.f)return A;
  TArray<FVector> Result;Result.SetNumUninitialized(A.Num());
  for(int32 I=0;I<A.Num();++I)Result[I]=FMath::Lerp(A[I],B[I],Alpha);return Result;
 }
 TArray<FVector> ScaleVerticesAboutPivot(const TArray<FVector>& Vertices,const FVector& Pivot,float Scale)
 {
  if(Scale==1.f)return Vertices;
  TArray<FVector> Result;Result.Reserve(Vertices.Num());
  for(const FVector& Vertex:Vertices)Result.Add(Pivot+(Vertex-Pivot)*Scale);return Result;
 }
 FFrame EvaluateWeaponVisualFrame(const UDWWeaponAnimationSet& Set,const FDWWeaponAnimationClip& Clip,const FRequest& In)
 {
  FFrame Out;const double SourceSeconds=In.SourceSeconds;
  Out.bHeld=In.bPresentationActive&&SourceSeconds>=Clip.HeldStartSeconds&&SourceSeconds<Clip.HeldEndSeconds;
  Out.PrimaryToOwner=Set.PrimaryBack;Out.SecondaryToOwner=Set.SecondaryBack;
  Out.bPrimaryVisible=!In.bDead;Out.bSecondaryVisible=Out.bPrimaryVisible;
  if(In.bPresentationActive&&Set.bUseBakedVisualSamples&&!Clip.VisualSamples.IsEmpty())
  {
   Out.VisualIndex=SampleIndex(Clip.VisualSamples,SourceSeconds);Out.Sample=Clip.VisualSamples[Out.VisualIndex];
   if(Clip.VisualSamples.IsValidIndex(Out.VisualIndex+1))
   {
    const auto& B=Clip.VisualSamples[Out.VisualIndex+1];
    const float Alpha=static_cast<float>(FMath::Clamp((SourceSeconds-Out.Sample.TimeSeconds)/Positive(B.TimeSeconds-Out.Sample.TimeSeconds),0.,1.));
    // UE Blend is shortest-path normalized quaternion LERP, not SLERP/Euler interpolation.
    Out.Sample.PrimaryTransform.Blend(Clip.VisualSamples[Out.VisualIndex].PrimaryTransform,B.PrimaryTransform,Alpha);
    Out.Sample.SecondaryTransform.Blend(Clip.VisualSamples[Out.VisualIndex].SecondaryTransform,B.SecondaryTransform,Alpha);
    Out.Sample.PrimaryTrailRoot=FMath::Lerp(Out.Sample.PrimaryTrailRoot,B.PrimaryTrailRoot,Alpha);
    Out.Sample.PrimaryTrailTip=FMath::Lerp(Out.Sample.PrimaryTrailTip,B.PrimaryTrailTip,Alpha);
    Out.Sample.SecondaryTrailRoot=FMath::Lerp(Out.Sample.SecondaryTrailRoot,B.SecondaryTrailRoot,Alpha);
    Out.Sample.SecondaryTrailTip=FMath::Lerp(Out.Sample.SecondaryTrailTip,B.SecondaryTrailTip,Alpha);
   }
   Out.PrimaryToOwner=Out.bHeld?Set.PrimaryGrip*Out.Sample.PrimaryTransform:Set.PrimaryBack;
   Out.SecondaryToOwner=Out.bHeld?Set.SecondaryGrip*Out.Sample.SecondaryTransform:Set.SecondaryBack;
   Out.bPrimaryVisible=Out.Sample.bPrimaryVisible;Out.bSecondaryVisible=Out.Sample.bSecondaryVisible;Out.bHasSample=true;
   if(!Out.bHeld){Out.bPrimaryVisible=!In.bDead;Out.bSecondaryVisible=Out.bPrimaryVisible;}
  }
  else if(Out.bHeld&&In.bHasMesh)
  {
   if(In.bHasPrimarySocket)Out.PrimaryToOwner=Set.PrimaryGrip*In.PrimarySocketToWorld*In.OwnerToWorld.Inverse();
   if(In.bHasSecondarySocket)Out.SecondaryToOwner=Set.SecondaryGrip*In.SecondarySocketToWorld*In.OwnerToWorld.Inverse();
  }
  if(In.SelectedWeapon==EDWAnimatedWeapon::MoldBow)
   Out.bSecondaryVisible=Out.bSecondaryVisible&&Out.bHeld&&!In.bDead&&!(In.bFormalBowActive&&In.bBowShotCommitted);
  Out.PrimaryToOwner.SetScale3D(Out.PrimaryToOwner.GetScale3D()*FMath::Max(.01f,Set.VisualScale));
  Out.SecondaryToOwner.SetScale3D(Out.SecondaryToOwner.GetScale3D()*FMath::Max(.01f,Set.VisualScale));
  if(In.bPresentationActive&&!Clip.GeometryFrames.IsEmpty())
  {
   Out.GeometryIndex=SampleIndex(Clip.GeometryFrames,SourceSeconds);Out.Geometry=&Clip.GeometryFrames[Out.GeometryIndex];
   if(Clip.GeometryFrames.IsValidIndex(Out.GeometryIndex+1))
   {
    Out.NextGeometry=&Clip.GeometryFrames[Out.GeometryIndex+1];
    Out.GeometryAlpha=static_cast<float>(FMath::Clamp((SourceSeconds-Out.Geometry->TimeSeconds)/Positive(Out.NextGeometry->TimeSeconds-Out.Geometry->TimeSeconds),0.,1.));
   }
  }
  Out.PrimaryGeometry.A=Out.Geometry&&!Out.Geometry->PrimaryVertices.IsEmpty()?&Out.Geometry->PrimaryVertices:&Set.PrimaryGeometry.Vertices;
  Out.SecondaryGeometry.A=Out.Geometry&&!Out.Geometry->SecondaryVertices.IsEmpty()?&Out.Geometry->SecondaryVertices:&Set.SecondaryGeometry.Vertices;
  Out.PrimaryGeometry.B=Out.NextGeometry?&Out.NextGeometry->PrimaryVertices:nullptr;
  Out.SecondaryGeometry.B=Out.NextGeometry?&Out.NextGeometry->SecondaryVertices:nullptr;
  Out.PrimaryGeometry.Alpha=Out.SecondaryGeometry.Alpha=Out.GeometryAlpha;
  // Keep existing preference predicates. ApplyGeometry still owns existing topology validation/cache.
  Out.bPrimaryDeformedVisible=Out.bPrimaryVisible&&!Out.PrimaryGeometry.A->IsEmpty()&&!Set.PrimaryGeometry.Triangles.IsEmpty();
  Out.bSecondaryDeformedVisible=Out.bSecondaryVisible&&!Out.SecondaryGeometry.A->IsEmpty()&&!Set.SecondaryGeometry.Triangles.IsEmpty();
  Out.bPrimaryStaticVisible=Out.bPrimaryVisible&&Set.PrimaryMesh&&!Out.bPrimaryDeformedVisible;
  Out.bSecondaryStaticVisible=Out.bSecondaryVisible&&Set.SecondaryMesh&&!Out.bSecondaryDeformedVisible;
  const bool ParticleWindow=In.bActionActive&&Out.bHasSample&&Out.Sample.bTrailActive&&!In.bDead;
  Out.bPrimaryParticleWindow=ParticleWindow&&Out.bPrimaryVisible;Out.bSecondaryParticleWindow=ParticleWindow&&Out.bSecondaryVisible;
  return Out;
 }
 FTrailEndpoints MapTrailEndpoints(const FTransform& Weapon,const FTransform& SourceWeapon,const FVector& Root,const FVector& Tip)
 {
  return {Weapon.TransformPosition(SourceWeapon.InverseTransformPosition(Root)),Weapon.TransformPosition(SourceWeapon.InverseTransformPosition(Tip))};
 }
 FTransform ResolveTrailAttachment(const FTransform& Weapon,const FVector& Tip,const FTransform& Local,bool bAtTip)
 {
  FTransform Attachment=Local*Weapon;
  if(bAtTip)Attachment.SetTranslation(Tip+Weapon.TransformVector(Local.GetTranslation()));return Attachment;
 }
 FTransform ResolveTrailAttachment(const FTransform& Weapon,const FVector& Tip,const FVector& Offset,const FRotator& Rotation,const FVector& Scale,bool bAtTip)
 {
  return ResolveTrailAttachment(Weapon,Tip,FTransform(Rotation,Offset,Scale),bAtTip);
 }
 float AnimationSeconds(double SourceSeconds,float SourceDuration,float Length)
 {
  return static_cast<float>(Length*FMath::Clamp(SourceSeconds/Positive(SourceDuration),0.,1.));
 }
}
