#include "DWAttackVfxResolver.h"
#include "NiagaraComponent.h"
#include "NiagaraSystem.h"
namespace DWAttackVfxResolver
{

 FNiagaraParameterWrites ResolveNiagaraParameters(const UNiagaraSystem* System,const FDWCombatEffectBinding& B,double HalfAngle)
 {
  FNiagaraParameterWrites Out;if(!System)return Out;
  TArray<FNiagaraVariable> Parameters;System->GetExposedParameters().GetParameters(Parameters);
  const auto Has=[&](FName Name,const FNiagaraTypeDefinition& Type){return !Name.IsNone()&&Parameters.Contains(FNiagaraVariable(Type,Name));};
  if(Has(B.ColorParameter,FNiagaraTypeDefinition::GetColorDef())&&(B.ColorParameter==TEXT("User.Color")||B.ColorParameter==TEXT("User.Color_01")))Out.Colors.Add(B.ColorParameter,B.Color);
  if(Has(B.SecondaryColorParameter,FNiagaraTypeDefinition::GetColorDef())&&(B.SecondaryColorParameter==TEXT("User.SecondaryColor")||B.SecondaryColorParameter==TEXT("User.Color_02")))Out.Colors.Add(B.SecondaryColorParameter,B.SecondaryColor);
  if(System->GetPathName()==TEXT("/Game/DoughWorld/VFX/Combat/NS_DW_GiantSlam_PaleYellow_ForwardSector_R02.NS_DW_GiantSlam_PaleYellow_ForwardSector_R02")&&Has(B.GiantHalfAngleDegreesParameter,FNiagaraTypeDefinition::GetFloatDef())&&FMath::IsFinite(HalfAngle)&&HalfAngle>=1.&&HalfAngle<=180.)Out.Floats.Add(B.GiantHalfAngleDegreesParameter,static_cast<float>(HalfAngle));
  if(Has(B.DelayParameter,FNiagaraTypeDefinition::GetFloatDef())&&B.DelayParameter==TEXT("User.Delay")&&FMath::IsFinite(B.DelaySeconds)&&B.DelaySeconds>=0.f)Out.Floats.Add(B.DelayParameter,B.DelaySeconds);
  if(Has(B.DurationParameter,FNiagaraTypeDefinition::GetFloatDef())&&B.DurationParameter==TEXT("User.Duration")&&FMath::IsFinite(B.DurationSeconds)&&B.DurationSeconds>0.f)Out.Floats.Add(B.DurationParameter,B.DurationSeconds);
  return Out;
 }

 bool ResolveEventLocalTransform(const FDWCombatEffectBinding& Binding,double Radius,FTransform& Out)
 {
  Out=Binding.LocalTransform;
  if(Radius>0.)
  {
   if(!FMath::IsFinite(Radius)||!FMath::IsFinite(Binding.AreaReferenceRadiusCm)||Binding.AreaReferenceRadiusCm<=0.f)return false;
   const double S=Radius/Binding.AreaReferenceRadiusCm;Out.SetScale3D(Out.GetScale3D()*FVector(S,S,1.));
  }
  return Out.IsValid();
 }
 bool RemoveEventRadiusScale(const FDWCombatEffectBinding& Binding,double Radius,const FTransform& Effective,FTransform& Out)
 {
  Out=Effective;
  if(Radius>0.)
  {
   if(!FMath::IsFinite(Radius)||!FMath::IsFinite(Binding.AreaReferenceRadiusCm)||Binding.AreaReferenceRadiusCm<=0.f)return false;
   const double S=Radius/Binding.AreaReferenceRadiusCm;Out.SetScale3D(Out.GetScale3D()/FVector(S,S,1.));
  }
  return Out.IsValid();
 }

 void ApplyParameters(UFXSystemComponent* FX,UFXSystemAsset* Asset,const FDWCombatEffectBinding& Binding,double HalfAngleDegrees)
 {
  if(auto* N=Cast<UNiagaraComponent>(FX))if(auto* System=Cast<UNiagaraSystem>(Asset))
  {
   const auto Writes=ResolveNiagaraParameters(System,Binding,HalfAngleDegrees);
   for(const auto& Pair:Writes.Colors)N->SetVariableLinearColor(Pair.Key,Pair.Value);
   for(const auto& Pair:Writes.Floats)N->SetVariableFloat(Pair.Key,Pair.Value);
  }
 }
 TArray<FDWAttackVfxEntry> ResolveEvents(const UDWAttackVfxProfile* Profile,EDWCombatPresentationKind Kind,const FDWCombatEffectBinding* Legacy)
 {
  if(Profile)if(const auto* Group=Profile->EventOverrides.Find(Kind))
  {
   TArray<FDWAttackVfxEntry> Out;TSet<FGuid> Seen;
   for(const auto& E:Group->Entries)if(E.EntryId.IsValid()&&!Seen.Contains(E.EntryId)){Seen.Add(E.EntryId);Out.Add(E);}return Out;
  }
  if(!Legacy)return {};
  FDWAttackVfxEntry Entry;/* Legacy fallback has no data EntryId. Editor assigns IDs only when authoring/promotion. */
  Entry.Effect=*Legacy;Entry.Anchor=Kind==EDWCombatPresentationKind::BowSlowArea?EDWAttackVfxAnchor::AreaCarrier:EDWAttackVfxAnchor::CommittedWorld;
  Entry.Follow=Kind==EDWCombatPresentationKind::BowSlowArea?EDWAttackVfxFollow::FollowAnchor:EDWAttackVfxFollow::SampleAtBirth;
  return {Entry};
 }
 FDWAttackVfxTrailGroup ResolveTrails(const UDWAttackVfxProfile* Profile,const UDWWeaponAnimationSet& Set)
 {
  if(Profile&&Profile->bOverrideLegacyTrails)return Profile->Trails;
  FDWAttackVfxTrailGroup Out;
  // Legacy two fixed component slots have no data GUID; never allocate identities in Runtime.
  Out.Primary.System=Set.PrimaryTrailSystem;Out.Secondary.System=Set.SecondaryTrailSystem;
  Out.Primary.LocalTransform=FTransform(Set.PrimaryTrailRotation,Set.PrimaryTrailOffset,Set.PrimaryTrailScale);
  Out.Secondary.LocalTransform=FTransform(Set.SecondaryTrailRotation,Set.SecondaryTrailOffset,Set.SecondaryTrailScale);
  Out.bAttachAtAuthoredTip=Set.bAttachTrailAtAuthoredTip;Out.StartParameter=Set.TrailStartParameterName;Out.EndParameter=Set.TrailEndParameterName;
  Out.bParticleReplacesGeometry=Set.bParticleTrailReplacesGeometry;Out.bUseGeometryTrail=Set.bUseGeometryTrail;Out.GeometryMaterial=Set.TrailMaterial;
  return Out;
 }
 bool ResolveAnchorFrame(const FDWAttackVfxEntry& Entry,const DWWeaponVisualSampler::FFrame& F,const FAnchorContext& C,FTransform& Out)
 {
  switch(Entry.Anchor)
  {
   case EDWAttackVfxAnchor::Owner:Out=C.OwnerToWorld;break;
   case EDWAttackVfxAnchor::CommittedWorld:if(!C.bHasCommitted)return false;Out=C.CommittedToWorld;break;
   case EDWAttackVfxAnchor::AreaCarrier:if(!C.bHasCarrier)return false;Out=C.CarrierToWorld;break;
   case EDWAttackVfxAnchor::CharacterBoneOrSocket:
    if(const auto* T=C.CharacterSocketsToWorld.Find(Entry.BoneOrSocket))Out=*T;else return false;break;
   case EDWAttackVfxAnchor::PrimaryWeapon:Out=F.PrimaryToOwner*C.OwnerToWorld;break;
   case EDWAttackVfxAnchor::SecondaryWeapon:Out=F.SecondaryToOwner*C.OwnerToWorld;break;
   case EDWAttackVfxAnchor::PrimaryMeshSocket:
    if(const auto* T=C.PrimaryMeshSockets.Find(Entry.BoneOrSocket))Out=*T*F.PrimaryToOwner*C.OwnerToWorld;else return false;break;
   case EDWAttackVfxAnchor::SecondaryMeshSocket:
    if(const auto* T=C.SecondaryMeshSockets.Find(Entry.BoneOrSocket))Out=*T*F.SecondaryToOwner*C.OwnerToWorld;else return false;break;
   case EDWAttackVfxAnchor::PrimaryAuthoredTip:
   case EDWAttackVfxAnchor::SecondaryAuthoredTip:
   {
    if(!F.bHasSample)return false;const bool Primary=Entry.Anchor==EDWAttackVfxAnchor::PrimaryAuthoredTip;
    const FTransform& W=Primary?F.PrimaryToOwner:F.SecondaryToOwner;
    const FTransform& S=Primary?F.Sample.PrimaryTransform:F.Sample.SecondaryTransform;
    const FVector Tip=Primary?F.Sample.PrimaryTrailTip:F.Sample.SecondaryTrailTip;
    Out=W;Out.SetTranslation(W.TransformPosition(S.InverseTransformPosition(Tip)));Out=Out*C.OwnerToWorld;break;
   }
   default:return false;
  }
  return Out.IsValid();
 }
 bool IsSourceWindowOpen(const FDWAttackVfxSourceWindow& W,FName Role,double Source,double Duration)
 {
  if(!W.Entry.bEnabled||(!W.ClipRole.IsNone()&&W.ClipRole!=Role)||!FMath::IsFinite(Source)||!FMath::IsFinite(Duration)||Duration<=0.||
   !FMath::IsFinite(W.StartNormalized)||!FMath::IsFinite(W.EndNormalized)||W.StartNormalized<0.f||W.EndNormalized>1.f||W.EndNormalized<=W.StartNormalized)return false;
  return Source>=W.StartNormalized*Duration&&Source<W.EndNormalized*Duration;
 }
 bool IsVisualLifetimeCurrent(uint64 E,uint64 NowE,uint64 Entity,uint64 Life,uint64 NowEntity,uint64 NowLife)
 {return E==NowE&&Entity==NowEntity&&Life==NowLife;}
 bool ValidateProfile(const UDWAttackVfxProfile& P,bool WeaponDomain,FString& Error)
 {
  TSet<FGuid> Seen;
  const auto Entry=[&](const FDWAttackVfxEntry& E)
  {
   if(!E.EntryId.IsValid()||Seen.Contains(E.EntryId)||!E.Effect.LocalTransform.IsValid()||
    !FMath::IsFinite(E.Effect.OneShotLifetimeSeconds)||E.Effect.OneShotLifetimeSeconds<=0.f||
    !FMath::IsFinite(E.VisualDelaySeconds)||E.VisualDelaySeconds<0.f)return false;
   Seen.Add(E.EntryId);return true;
  };
  if(WeaponDomain&&!P.EventOverrides.IsEmpty()){Error=TEXT("Weapon profiles consume Trails/SourceWindows, not actor committed event groups.");return false;}
  if(!WeaponDomain&&(P.bOverrideLegacyTrails||!P.SourceWindows.IsEmpty())){Error=TEXT("Actor event profiles do not consume weapon source windows or trails.");return false;}
  for(const auto& Pair:P.EventOverrides)for(const auto& E:Pair.Value.Entries)
   if(!Entry(E)||!ValidateEntryForEvent(Pair.Key,E,Error)){if(Error.IsEmpty())Error=TEXT("Invalid or duplicated cosmetic entry ID.");return false;}
  if(P.bOverrideLegacyTrails)
  {
   for(const auto* T:{&P.Trails.Primary,&P.Trails.Secondary})
   {
    if(!T->EntryId.IsValid()||Seen.Contains(T->EntryId)||!T->LocalTransform.IsValid()||
     !FMath::IsFinite(T->StartNormalized)||!FMath::IsFinite(T->EndNormalized)||T->StartNormalized<0.f||T->EndNormalized>1.f||T->EndNormalized<=T->StartNormalized)
    {Error=TEXT("Invalid trail ID, transform or half-open visual window.");return false;}Seen.Add(T->EntryId);
   }
  }
  for(const auto& W:P.SourceWindows)
   if(!Entry(W.Entry)||W.Entry.VisualDelaySeconds!=0.f||W.Entry.Anchor==EDWAttackVfxAnchor::CommittedWorld||W.Entry.Anchor==EDWAttackVfxAnchor::AreaCarrier||
    !FMath::IsFinite(W.StartNormalized)||!FMath::IsFinite(W.EndNormalized)||W.StartNormalized<0.f||W.EndNormalized>1.f||W.EndNormalized<=W.StartNormalized)
   {Error=TEXT("Source windows require unique IDs, source-relative timing and a non-committed anchor; event delay is not applicable.");return false;}
  Error.Reset();return true;
 }
 bool ValidateEntryForEvent(EDWCombatPresentationKind Kind,const FDWAttackVfxEntry& E,FString& Error)
 {
  if(!E.EntryId.IsValid()||!E.Effect.LocalTransform.IsValid()||!FMath::IsFinite(E.VisualDelaySeconds)||E.VisualDelaySeconds<0.f)
  {Error=TEXT("Invalid cosmetic entry ID, transform or visual delay.");return false;}
  if(Kind==EDWCombatPresentationKind::BowSlowArea&&E.VisualDelaySeconds!=0.f)
  {Error=TEXT("Bow area uses its original carrier start/lifetime; separate event visual delay is not supported.");return false;}
  if(Kind==EDWCombatPresentationKind::BowSlowArea&&(E.Anchor!=EDWAttackVfxAnchor::AreaCarrier||E.Follow!=EDWAttackVfxFollow::FollowAnchor))
  {Error=TEXT("Bow area appearance must stay owned by its existing area carrier.");return false;}
  const bool WorldOnly=Kind==EDWCombatPresentationKind::GreatswordSlam||Kind==EDWCombatPresentationKind::GiantSlam||Kind==EDWCombatPresentationKind::DaggerAssassination;
  if(WorldOnly&&(E.Anchor!=EDWAttackVfxAnchor::CommittedWorld||E.Follow!=EDWAttackVfxFollow::SampleAtBirth))
  {Error=TEXT("Ground/frozen-target effects must preserve the original committed world frame.");return false;}
  if(Kind!=EDWCombatPresentationKind::BowSlowArea&&E.Anchor==EDWAttackVfxAnchor::AreaCarrier)
  {Error=TEXT("This committed event has no area carrier.");return false;}
  Error.Reset();return true;
 }
}
