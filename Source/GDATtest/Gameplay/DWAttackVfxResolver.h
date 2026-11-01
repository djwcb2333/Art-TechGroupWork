#pragma once
#include "CoreMinimal.h"
#include "DWAttackVfxProfile.h"
#include "DWWeaponVisualSampler.h"
class UFXSystemComponent;
class UFXSystemAsset;
namespace DWAttackVfxResolver
{
 struct FAnchorContext
 {
  FTransform OwnerToWorld=FTransform::Identity;
  FTransform CommittedToWorld=FTransform::Identity;
  FTransform CarrierToWorld=FTransform::Identity;
  bool bHasCommitted=false,bHasCarrier=false;
  TMap<FName,FTransform> CharacterSocketsToWorld;
  // Existing model socket transforms, relative to the sampled weapon component.
  TMap<FName,FTransform> PrimaryMeshSockets,SecondaryMeshSockets;
 };

 struct FNiagaraParameterWrites
 {
  TMap<FName,FLinearColor> Colors;
  TMap<FName,float> Floats;
 };
 // Pure asset metadata/value resolution. Applies no component or gameplay side effects.
 GDATTEST_API FNiagaraParameterWrites ResolveNiagaraParameters(const UNiagaraSystem* System,const FDWCombatEffectBinding& Binding,double HalfAngleDegrees=180.);

 // Event visual-only XY radius scaling, same as existing PublishCommitted.
 GDATTEST_API bool ResolveEventLocalTransform(const FDWCombatEffectBinding& Binding,double Radius,FTransform& Out);
 GDATTEST_API bool RemoveEventRadiusScale(const FDWCombatEffectBinding& Binding,double Radius,const FTransform& EffectiveLocal,FTransform& OutBaseLocal);
 // Applies the original exact whitelist to this supplied FX component only; no spawn/activation/world/gameplay.
 GDATTEST_API void ApplyParameters(UFXSystemComponent*,UFXSystemAsset*,const FDWCombatEffectBinding&,double HalfAngleDegrees=180.);
 GDATTEST_API TArray<FDWAttackVfxEntry> ResolveEvents(const UDWAttackVfxProfile* Profile,EDWCombatPresentationKind Kind,const FDWCombatEffectBinding* Legacy);
 GDATTEST_API FDWAttackVfxTrailGroup ResolveTrails(const UDWAttackVfxProfile* Profile,const UDWWeaponAnimationSet& Legacy);
 GDATTEST_API bool ResolveAnchorFrame(const FDWAttackVfxEntry& Entry,const DWWeaponVisualSampler::FFrame& Frame,const FAnchorContext& Context,FTransform& OutParentToWorld);
 GDATTEST_API bool IsSourceWindowOpen(const FDWAttackVfxSourceWindow& Window,FName Role,double SourceSeconds,double Duration);
 GDATTEST_API bool IsVisualLifetimeCurrent(uint64 CapturedEpoch,uint64 CurrentEpoch,uint64 CapturedEntity,uint64 CapturedLife,uint64 CurrentEntity,uint64 CurrentLife);
 GDATTEST_API bool ValidateProfile(const UDWAttackVfxProfile&,bool bWeaponVisualDomain,FString& Error);
 GDATTEST_API bool ValidateEntryForEvent(EDWCombatPresentationKind Kind,const FDWAttackVfxEntry& Entry,FString& Error);
}
